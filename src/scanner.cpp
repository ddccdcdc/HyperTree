#include "scanner.h"
#include "win_utils.h"
#include <winioctl.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <execution>

namespace {

constexpr uint64_t NTFS_ROOT_FRN = 5;
constexpr size_t   MFT_CHUNK_BYTES = 8 * 1024 * 1024; // 8 MB sequential reads

struct HandleGuard {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~HandleGuard() {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) CloseHandle(h);
    }
};

inline uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}
inline uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}
inline uint64_t ReadU64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}
inline int64_t ReadI64(const uint8_t* p) {
    int64_t v;
    memcpy(&v, p, 8);
    return v;
}

std::unique_ptr<DiskTree> FinalizeTree(
    std::vector<TreeNode>&& nodes,
    std::vector<wchar_t>&& names,
    ExtInterner&& interner,
    uint32_t root_idx,
    const std::wstring& root_path,
    ScanMode mode,
    std::chrono::steady_clock::time_point startTime,
    ScanProgress& progress,
    std::wstring& outErr)
{
    if (progress.is_cancelled()) {
        outErr = L"Scan cancelled.";
        return nullptr;
    }
    progress.set_phase(ScanPhase::BuildingTree);

    const uint32_t n = static_cast<uint32_t>(nodes.size());
    for (auto& node : nodes) {
        node.first_child = NONE_IDX;
        node.next_sibling = NONE_IDX;
    }

    for (uint32_t i = 0; i < n; ++i) {
        if (i == root_idx) continue;
        uint32_t p = nodes[i].parent;
        if (p != NONE_IDX && p < n && p != i) {
            nodes[i].next_sibling = nodes[p].first_child;
            nodes[p].first_child = i;
        }
    }

    // Iterative BFS for topological order
    std::vector<uint32_t> order;
    order.reserve(n);
    order.push_back(root_idx);
    size_t head = 0;
    while (head < order.size()) {
        if ((head & 0xFFFF) == 0 && progress.is_cancelled()) {
            outErr = L"Scan cancelled.";
            return nullptr;
        }
        uint32_t cur = order[head++];
        uint32_t c = nodes[cur].first_child;
        while (c != NONE_IDX) {
            order.push_back(c);
            c = nodes[c].next_sibling;
        }
    }

    // Bottom-up size & count accumulation
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        uint32_t idx = *it;
        const TreeNode& node = nodes[idx];
        if (!node.is_dir()) {
            if (node.ext_id < interner.stats.size()) {
                auto& st = interner.stats[node.ext_id];
                st.size += node.size;
                st.allocated += node.allocated;
                st.count += 1;
            }
        }
        uint32_t p = node.parent;
        if (idx != root_idx && p != NONE_IDX && p < n && p != idx) {
            uint32_t addFiles = node.is_dir() ? node.files : 1;
            uint32_t addFolders = node.is_dir() ? (node.folders + 1) : 0;
            TreeNode& par = nodes[p];
            par.size += node.size;
            par.allocated += node.allocated;
            par.files += addFiles;
            par.folders += addFolders;
        }
    }

    if (progress.is_cancelled()) {
        outErr = L"Scan cancelled.";
        return nullptr;
    }
    progress.set_phase(ScanPhase::Sorting);

    // Sort children of every directory in parallel by size descending
    std::for_each(std::execution::par, order.begin(), order.end(), [&](uint32_t idx) {
        TreeNode& node = nodes[idx];
        if (!node.is_dir() || node.first_child == NONE_IDX) return;
        thread_local std::vector<uint32_t> childBuf;
        childBuf.clear();
        uint32_t c = node.first_child;
        while (c != NONE_IDX) {
            childBuf.push_back(c);
            c = nodes[c].next_sibling;
        }
        if (childBuf.size() > 1) {
            std::sort(childBuf.begin(), childBuf.end(), [&](uint32_t a, uint32_t b) {
                if (nodes[a].size != nodes[b].size) return nodes[a].size > nodes[b].size;
                return nodes[a].is_dir() > nodes[b].is_dir();
            });
            node.first_child = childBuf[0];
            for (size_t k = 0; k + 1 < childBuf.size(); ++k) {
                nodes[childBuf[k]].next_sibling = childBuf[k + 1];
            }
            nodes[childBuf.back()].next_sibling = NONE_IDX;
        }
    });

    // Collect all reachable files and sort in parallel by size descending
    std::vector<uint32_t> top_files;
    top_files.reserve(order.size());
    for (uint32_t idx : order) {
        if (!nodes[idx].is_dir()) top_files.push_back(idx);
    }
    std::sort(std::execution::par, top_files.begin(), top_files.end(), [&](uint32_t a, uint32_t b) {
        return nodes[a].size > nodes[b].size;
    });

    // Sort extensions by size descending and remap node.ext_id
    std::vector<std::pair<uint16_t, ExtStat>> indexedExts;
    indexedExts.reserve(interner.stats.size());
    for (size_t i = 0; i < interner.stats.size(); ++i) {
        indexedExts.emplace_back(static_cast<uint16_t>(i), std::move(interner.stats[i]));
    }
    std::sort(indexedExts.begin(), indexedExts.end(), [](const auto& a, const auto& b) {
        return a.second.size > b.second.size;
    });
    std::vector<uint16_t> remap(indexedExts.size(), 0);
    std::vector<ExtStat> sortedExts;
    sortedExts.reserve(indexedExts.size());
    for (size_t newId = 0; newId < indexedExts.size(); ++newId) {
        remap[indexedExts[newId].first] = static_cast<uint16_t>(newId);
        sortedExts.push_back(std::move(indexedExts[newId].second));
    }
    std::for_each(std::execution::par, nodes.begin(), nodes.end(), [&](TreeNode& node) {
        if (!node.is_dir() && node.ext_id < remap.size()) {
            node.ext_id = remap[node.ext_id];
        }
    });

    auto tree = std::make_unique<DiskTree>();
    tree->root_idx = root_idx;
    tree->root_path = root_path;
    tree->scan_mode = mode;
    tree->total_files = nodes[root_idx].files;
    tree->total_folders = nodes[root_idx].folders;
    tree->total_size = nodes[root_idx].size;
    tree->total_allocated = nodes[root_idx].allocated;
    tree->nodes = std::move(nodes);
    tree->names = std::move(names);
    tree->extensions = std::move(sortedExts);
    tree->top_files = std::move(top_files);
    tree->scan_duration_secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    return tree;
}

// ============================================================================
// Tier 1: Direct Raw NTFS $MFT Scanner
// ============================================================================
struct MftExtent {
    int64_t lcn;
    uint64_t clusters;
};

struct ParsedMftRec {
    uint64_t frn;
    uint64_t parent_frn;
    uint64_t size;
    uint64_t allocated;
    int64_t  modified;
    uint32_t name_off;
    uint16_t name_len;
    uint8_t  flags;
};

struct WorkerMftOutput {
    std::vector<ParsedMftRec> records;
    std::vector<wchar_t> names;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> ext_data; // base_frn, size, alloc
};

struct MftChunk {
    uint64_t base_frn;
    std::vector<uint8_t> data;
};

std::vector<MftExtent> GetMftExtentsFromRecordZero(HANDLE volHandle, size_t recordSize) {
    std::vector<MftExtent> extents;
    NTFS_FILE_RECORD_INPUT_BUFFER inBuf{};
    inBuf.FileReferenceNumber.QuadPart = 0;

    std::vector<uint8_t> recBuf(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + recordSize + 64);
    DWORD ret = 0;
    if (!DeviceIoControl(
            volHandle, FSCTL_GET_NTFS_FILE_RECORD,
            &inBuf, sizeof(inBuf),
            recBuf.data(), static_cast<DWORD>(recBuf.size()),
            &ret, nullptr)) {
        return extents;
    }

    auto* outHdr = reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(recBuf.data());
    const uint8_t* rec = outHdr->FileRecordBuffer;
    uint32_t recLen = outHdr->FileRecordLength;
    if (recLen < 48 || memcmp(rec, "FILE", 4) != 0) return extents;

    size_t attrOff = ReadU16(rec + 20);
    size_t bytesUsed = std::min<size_t>(ReadU32(rec + 24), recLen);
    while (attrOff + 16 <= bytesUsed) {
        uint32_t attrType = ReadU32(rec + attrOff);
        if (attrType == 0xFFFFFFFFu) break;
        uint32_t attrLen = ReadU32(rec + attrOff + 4);
        if (attrLen < 16 || attrOff + attrLen > bytesUsed) break;
        uint8_t nonRes = rec[attrOff + 8];
        uint8_t nameLen = rec[attrOff + 9];
        if (attrType == 0x80 && nonRes == 1 && nameLen == 0 && attrLen >= 64) {
            uint16_t runOff = ReadU16(rec + attrOff + 32);
            size_t p = attrOff + runOff;
            size_t end = attrOff + attrLen;
            int64_t curLcn = 0;
            while (p < end && rec[p] != 0) {
                uint8_t hdr = rec[p++];
                uint8_t lenBytes = hdr & 0x0F;
                uint8_t offBytes = (hdr >> 4) & 0x0F;
                if (lenBytes == 0 || p + lenBytes + offBytes > end) break;

                uint64_t clusters = 0;
                for (uint8_t k = 0; k < lenBytes; ++k) {
                    clusters |= static_cast<uint64_t>(rec[p++]) << (k * 8);
                }
                if (offBytes == 0) {
                    extents.push_back(MftExtent{ -1, clusters });
                } else {
                    int64_t delta = 0;
                    for (uint8_t k = 0; k < offBytes; ++k) {
                        delta |= static_cast<int64_t>(rec[p++]) << (k * 8);
                    }
                    // Sign extend
                    if (offBytes < 8 && (delta & (1LL << (offBytes * 8 - 1)))) {
                        delta |= (~0ULL) << (offBytes * 8);
                    }
                    curLcn += delta;
                    extents.push_back(MftExtent{ curLcn, clusters });
                }
            }
            break;
        }
        attrOff += attrLen;
    }
    return extents;
}

std::unique_ptr<DiskTree> TryScanNtfsMft(
    wchar_t driveLetter,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime,
    std::wstring& outErr)
{
    EnablePrivilege(SE_BACKUP_NAME);
    EnablePrivilege(SE_MANAGE_VOLUME_NAME);

    wchar_t volPath[16];
    swprintf_s(volPath, L"\\\\.\\%c:", driveLetter);

    HandleGuard volGuard;
    volGuard.h = CreateFileW(
        volPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    if (volGuard.h == INVALID_HANDLE_VALUE) {
        outErr = L"Cannot open raw volume.";
        return nullptr;
    }

    NTFS_VOLUME_DATA_BUFFER volData{};
    DWORD bytesRet = 0;
    if (!DeviceIoControl(volGuard.h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
        &volData, sizeof(volData), &bytesRet, nullptr)) {
        outErr = L"Volume is not NTFS.";
        return nullptr;
    }

    const size_t bytesPerSector = std::max<size_t>(volData.BytesPerSector, 512);
    const uint64_t bytesPerCluster = volData.BytesPerCluster;
    const size_t recordSize = std::max<size_t>(volData.BytesPerFileRecordSegment, 1024);
    const uint64_t mftValidBytes = static_cast<uint64_t>(volData.MftValidDataLength.QuadPart);
    if (mftValidBytes == 0) {
        outErr = L"Empty $MFT.";
        return nullptr;
    }

    // Get physical extents of $MFT (try $MFT handle first, then fallback to MFT record 0)
    std::vector<MftExtent> extents;
    wchar_t mftPath[16];
    swprintf_s(mftPath, L"%c:\\$MFT", driveLetter);
    HandleGuard mftGuard;
    mftGuard.h = CreateFileW(
        mftPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (mftGuard.h != INVALID_HANDLE_VALUE) {
        STARTING_VCN_INPUT_BUFFER startVcn{};
        std::vector<uint8_t> outBuf(64 * 1024);
        while (true) {
            DWORD ret = 0;
            BOOL ok = DeviceIoControl(
                mftGuard.h, FSCTL_GET_RETRIEVAL_POINTERS,
                &startVcn, sizeof(startVcn),
                outBuf.data(), static_cast<DWORD>(outBuf.size()),
                &ret, nullptr);
            DWORD err = GetLastError();
            if (!ok && err != ERROR_MORE_DATA && err != ERROR_HANDLE_EOF) {
                extents.clear();
                break;
            }
            auto* rpb = reinterpret_cast<const RETRIEVAL_POINTERS_BUFFER*>(outBuf.data());
            uint64_t prevVcn = static_cast<uint64_t>(rpb->StartingVcn.QuadPart);
            for (DWORD i = 0; i < rpb->ExtentCount; ++i) {
                uint64_t nextVcn = static_cast<uint64_t>(rpb->Extents[i].NextVcn.QuadPart);
                uint64_t clusters = nextVcn > prevVcn ? (nextVcn - prevVcn) : 0;
                extents.push_back(MftExtent{ rpb->Extents[i].Lcn.QuadPart, clusters });
                prevVcn = nextVcn;
            }
            if (ok || err == ERROR_HANDLE_EOF || rpb->ExtentCount == 0) break;
            startVcn.StartingVcn.QuadPart = static_cast<LONGLONG>(prevVcn);
        }
    }
    if (extents.empty()) {
        extents = GetMftExtentsFromRecordZero(volGuard.h, recordSize);
    }

    if (extents.empty()) {
        outErr = L"No $MFT extents found.";
        return nullptr;
    }

    progress.set_phase(ScanPhase::ReadingMft);
    progress.mft_total_bytes.store(mftValidBytes, std::memory_order_relaxed);
    progress.mft_bytes_read.store(0, std::memory_order_relaxed);

    // Bounded producer-consumer queue for pipelined I/O + parallel CPU parsing
    std::mutex qMtx;
    std::condition_variable qNotFull, qNotEmpty;
    std::deque<MftChunk> queue;
    bool readerDone = false;

    const unsigned numWorkers = std::max(2u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<WorkerMftOutput> workerOutputs(numWorkers);
    std::vector<std::thread> workers;
    workers.reserve(numWorkers);

    for (unsigned w = 0; w < numWorkers; ++w) {
        workers.emplace_back([&, w]() {
            auto& out = workerOutputs[w];
            out.records.reserve(32768);
            out.names.reserve(32768 * 16);

            while (true) {
                MftChunk chunk;
                {
                    std::unique_lock<std::mutex> lk(qMtx);
                    qNotEmpty.wait(lk, [&]() { return !queue.empty() || readerDone || progress.is_cancelled(); });
                    if (progress.is_cancelled()) break;
                    if (queue.empty() && readerDone) break;
                    chunk = std::move(queue.front());
                    queue.pop_front();
                    qNotFull.notify_one();
                }

                const size_t count = chunk.data.size() / recordSize;
                uint64_t localFiles = 0, localFolders = 0, localBytes = 0;

                for (size_t i = 0; i < count; ++i) {
                    uint8_t* rec = chunk.data.data() + i * recordSize;
                    if (memcmp(rec, "FILE", 4) != 0) continue;
                    uint16_t recFlags = ReadU16(rec + 22);
                    if ((recFlags & 0x01) == 0) continue; // not in use

                    // Apply USA fixup
                    uint16_t usaOff = ReadU16(rec + 4);
                    uint16_t usaCnt = ReadU16(rec + 6);
                    if (usaCnt < 2 || static_cast<size_t>(usaOff) + static_cast<size_t>(usaCnt) * 2 > recordSize) continue;
                    uint8_t usn0 = rec[usaOff];
                    uint8_t usn1 = rec[usaOff + 1];
                    bool fixOk = true;
                    for (uint16_t s = 1; s < usaCnt; ++s) {
                        size_t secEnd = static_cast<size_t>(s) * bytesPerSector;
                        if (secEnd > recordSize) break;
                        if (rec[secEnd - 2] != usn0 || rec[secEnd - 1] != usn1) {
                            fixOk = false;
                            break;
                        }
                        rec[secEnd - 2] = rec[usaOff + s * 2];
                        rec[secEnd - 1] = rec[usaOff + s * 2 + 1];
                    }
                    if (!fixOk) continue;

                    uint64_t frn = chunk.base_frn + i;
                    uint64_t baseRecord = ReadU64(rec + 32) & 0x0000FFFFFFFFFFFFULL;
                    bool isDir = (recFlags & 0x02) != 0;

                    size_t attrOff = ReadU16(rec + 20);
                    size_t bytesUsed = std::min<size_t>(ReadU32(rec + 24), recordSize);

                    int64_t modified = 0;
                    uint32_t stdAttrs = 0;
                    const wchar_t* bestNamePtr = nullptr;
                    uint16_t bestNameLen = 0;
                    uint8_t bestNsRank = 255;
                    uint64_t parentFrn = NTFS_ROOT_FRN;
                    uint64_t fileSize = 0;
                    uint64_t allocSize = 0;
                    bool gotData = false;

                    while (attrOff + 16 <= bytesUsed) {
                        uint32_t attrType = ReadU32(rec + attrOff);
                        if (attrType == 0xFFFFFFFFu) break;
                        uint32_t attrLen = ReadU32(rec + attrOff + 4);
                        if (attrLen < 16 || attrOff + attrLen > bytesUsed) break;
                        uint8_t nonRes = rec[attrOff + 8];
                        uint8_t attrNameLen = rec[attrOff + 9];

                        if (attrType == 0x10 && nonRes == 0) {
                            uint16_t vOff = ReadU16(rec + attrOff + 20);
                            uint32_t vLen = ReadU32(rec + attrOff + 16);
                            size_t abs = attrOff + vOff;
                            if (vLen >= 36 && abs + 36 <= attrOff + attrLen) {
                                modified = ReadI64(rec + abs + 8);
                                stdAttrs = ReadU32(rec + abs + 32);
                            }
                        } else if (attrType == 0x30 && nonRes == 0) {
                            uint16_t vOff = ReadU16(rec + attrOff + 20);
                            uint32_t vLen = ReadU32(rec + attrOff + 16);
                            size_t abs = attrOff + vOff;
                            if (vLen >= 66 && abs + 66 <= attrOff + attrLen) {
                                uint64_t pfrn = ReadU64(rec + abs) & 0x0000FFFFFFFFFFFFULL;
                                uint8_t nlen = rec[abs + 64];
                                uint8_t ns = rec[abs + 65];
                                size_t nbytes = abs + 66;
                                if (nbytes + static_cast<size_t>(nlen) * 2 <= attrOff + attrLen) {
                                    uint8_t rank = (ns == 1 || ns == 3) ? 0 : (ns == 0 ? 1 : 2);
                                    if (rank < bestNsRank) {
                                        bestNsRank = rank;
                                        bestNamePtr = reinterpret_cast<const wchar_t*>(rec + nbytes);
                                        bestNameLen = nlen;
                                        parentFrn = pfrn;
                                    }
                                }
                            }
                        } else if (attrType == 0x80 && attrNameLen == 0 && !gotData) {
                            if (nonRes == 0) {
                                fileSize = ReadU32(rec + attrOff + 16);
                                allocSize = 0;
                                gotData = true;
                            } else if (attrLen >= 56) {
                                uint64_t startVcn = ReadU64(rec + attrOff + 16);
                                if (startVcn == 0) {
                                    uint64_t alloc = ReadU64(rec + attrOff + 40);
                                    uint64_t dsize = ReadU64(rec + attrOff + 48);
                                    uint16_t aflags = ReadU16(rec + attrOff + 12);
                                    if ((aflags & 0x8001) != 0 && attrLen >= 72) {
                                        alloc = ReadU64(rec + attrOff + 64);
                                    }
                                    fileSize = dsize;
                                    allocSize = alloc;
                                    gotData = true;
                                }
                            }
                        }
                        attrOff += attrLen;
                    }

                    if (baseRecord != 0) {
                        if (gotData && fileSize > 0) {
                            out.ext_data.emplace_back(baseRecord, fileSize, allocSize);
                        }
                        continue;
                    }
                    if (!bestNamePtr || bestNameLen == 0) continue;
                    // Skip NTFS metafiles ($MFT, $LogFile, etc.) except root FRN 5
                    if (frn < 24 && frn != NTFS_ROOT_FRN && bestNamePtr[0] == L'$') continue;

                    uint8_t flags = 0;
                    if (isDir) {
                        flags |= FLAG_IS_DIR;
                        ++localFolders;
                    } else {
                        ++localFiles;
                        localBytes += fileSize;
                    }
                    if (stdAttrs & FILE_ATTRIBUTE_HIDDEN) flags |= FLAG_IS_HIDDEN;
                    if (stdAttrs & FILE_ATTRIBUTE_SYSTEM) flags |= FLAG_IS_SYSTEM;
                    if (stdAttrs & FILE_ATTRIBUTE_COMPRESSED) flags |= FLAG_IS_COMPRESSED;

                    uint32_t nameOff = static_cast<uint32_t>(out.names.size());
                    out.names.insert(out.names.end(), bestNamePtr, bestNamePtr + bestNameLen);

                    out.records.push_back(ParsedMftRec{
                        frn,
                        parentFrn,
                        isDir ? 0 : fileSize,
                        isDir ? 0 : allocSize,
                        modified,
                        nameOff,
                        bestNameLen,
                        flags
                    });
                }

                progress.files.fetch_add(localFiles, std::memory_order_relaxed);
                progress.folders.fetch_add(localFolders, std::memory_order_relaxed);
                progress.bytes_processed.fetch_add(localBytes, std::memory_order_relaxed);
            }
        });
    }

    // Sequential reader loop on current thread
    uint64_t remainingValid = mftValidBytes;
    uint64_t currentFrn = 0;
    const size_t chunkAligned = (MFT_CHUNK_BYTES / bytesPerSector) * bytesPerSector;
    std::vector<uint8_t> leftover;
    leftover.reserve(recordSize);

    for (const auto& ext : extents) {
        if (remainingValid == 0 || progress.is_cancelled()) break;
        uint64_t extBytes = std::min<uint64_t>(ext.clusters * bytesPerCluster, remainingValid);
        if (ext.lcn < 0) {
            currentFrn += extBytes / recordSize;
            remainingValid -= extBytes;
            continue;
        }

        LARGE_INTEGER offset{};
        offset.QuadPart = ext.lcn * static_cast<LONGLONG>(bytesPerCluster);
        if (!SetFilePointerEx(volGuard.h, offset, nullptr, FILE_BEGIN)) break;

        size_t extRem = static_cast<size_t>(extBytes);
        while (extRem > 0 && !progress.is_cancelled()) {
            size_t toReadRaw = std::min(extRem, chunkAligned);
            size_t toRead = ((toReadRaw + bytesPerSector - 1) / bytesPerSector) * bytesPerSector;
            std::vector<uint8_t> buf(toRead);
            DWORD actual = 0;
            if (!ReadFile(volGuard.h, buf.data(), static_cast<DWORD>(toRead), &actual, nullptr) || actual == 0) {
                break;
            }
            size_t validInBuf = std::min<size_t>(actual, extRem);
            buf.resize(validInBuf);
            progress.mft_bytes_read.fetch_add(validInBuf, std::memory_order_relaxed);

            if (!leftover.empty()) {
                std::vector<uint8_t> combined;
                combined.reserve(leftover.size() + buf.size());
                combined.insert(combined.end(), leftover.begin(), leftover.end());
                combined.insert(combined.end(), buf.begin(), buf.end());
                leftover.clear();
                buf = std::move(combined);
            }

            size_t remMod = buf.size() % recordSize;
            if (remMod != 0) {
                size_t splitAt = buf.size() - remMod;
                leftover.assign(buf.begin() + splitAt, buf.end());
                buf.resize(splitAt);
            }

            uint64_t numRecs = buf.size() / recordSize;
            if (numRecs > 0) {
                MftChunk chunk{ currentFrn, std::move(buf) };
                currentFrn += numRecs;
                std::unique_lock<std::mutex> lk(qMtx);
                qNotFull.wait(lk, [&]() { return queue.size() < 6 || progress.is_cancelled(); });
                if (progress.is_cancelled()) break;
                queue.push_back(std::move(chunk));
                qNotEmpty.notify_one();
            }

            extRem -= validInBuf;
            remainingValid -= validInBuf;
        }
    }

    {
        std::lock_guard<std::mutex> lk(qMtx);
        readerDone = true;
    }
    qNotEmpty.notify_all();
    for (auto& t : workers) t.join();

    if (progress.is_cancelled()) {
        outErr = L"Scan cancelled.";
        return nullptr;
    }

    progress.set_phase(ScanPhase::ParsingRecords);

    size_t totalRecs = 0;
    size_t totalNameChars = 0;
    for (const auto& wo : workerOutputs) {
        totalRecs += wo.records.size();
        totalNameChars += wo.names.size();
    }

    std::vector<TreeNode> nodes;
    nodes.reserve(totalRecs + 1);
    std::vector<wchar_t> names;
    names.reserve(totalNameChars);
    ExtInterner interner;

    size_t mapLen = std::max<size_t>(static_cast<size_t>(mftValidBytes / recordSize) + 16, totalRecs + 16);
    std::vector<uint32_t> frnToIdx(mapLen, NONE_IDX);
    std::vector<uint64_t> parentFrns;
    parentFrns.reserve(totalRecs + 1);

    for (const auto& wo : workerOutputs) {
        for (const auto& r : wo.records) {
            uint32_t idx = static_cast<uint32_t>(nodes.size());
            if (r.frn >= frnToIdx.size()) {
                frnToIdx.resize(static_cast<size_t>(r.frn) + 1024, NONE_IDX);
            }
            frnToIdx[static_cast<size_t>(r.frn)] = idx;

            uint32_t nameOff = static_cast<uint32_t>(names.size());
            names.insert(names.end(), wo.names.data() + r.name_off, wo.names.data() + r.name_off + r.name_len);
            bool isDir = (r.flags & FLAG_IS_DIR) != 0;
            uint16_t extId = isDir ? 0 : interner.intern(std::wstring_view(names.data() + nameOff, r.name_len));

            nodes.push_back(TreeNode{
                r.size,
                r.allocated,
                r.modified,
                NONE_IDX,
                NONE_IDX,
                NONE_IDX,
                0,
                0,
                nameOff,
                r.name_len,
                extId,
                r.flags
            });
            parentFrns.push_back(r.parent_frn);
        }
    }

    for (const auto& wo : workerOutputs) {
        for (const auto& [baseFrn, sz, alloc] : wo.ext_data) {
            if (baseFrn < frnToIdx.size()) {
                uint32_t idx = frnToIdx[static_cast<size_t>(baseFrn)];
                if (idx != NONE_IDX && !nodes[idx].is_dir() && nodes[idx].size == 0) {
                    nodes[idx].size = sz;
                    nodes[idx].allocated = alloc;
                }
            }
        }
    }

    uint32_t rootIdx = (NTFS_ROOT_FRN < frnToIdx.size()) ? frnToIdx[NTFS_ROOT_FRN] : NONE_IDX;
    if (rootIdx == NONE_IDX) {
        outErr = L"NTFS root directory not found in $MFT.";
        return nullptr;
    }

    for (size_t i = 0; i < nodes.size(); ++i) {
        if (i == rootIdx) {
            nodes[i].parent = NONE_IDX;
            continue;
        }
        uint64_t pfrn = parentFrns[i];
        if (pfrn < frnToIdx.size()) {
            nodes[i].parent = frnToIdx[static_cast<size_t>(pfrn)];
        }
    }

    wchar_t rootPath[8];
    swprintf_s(rootPath, L"%c:\\", driveLetter);
    return FinalizeTree(std::move(nodes), std::move(names), std::move(interner), rootIdx, rootPath, ScanMode::NtfsMft, startTime, progress, outErr);
}

// ============================================================================
// Tier 2: Parallel Win32 FindFirstFileExW Scanner
// ============================================================================
std::wstring MakeExtendedPattern(const std::wstring& dir) {
    std::wstring clean = dir;
    while (clean.size() > 1 && clean.back() == L'\\') clean.pop_back();
    if (clean.rfind(L"\\\\?\\", 0) == 0 || clean.rfind(L"\\\\.\\", 0) == 0) {
        return clean + L"\\*";
    }
    if (clean.rfind(L"\\\\", 0) == 0) {
        return L"\\\\?\\UNC\\" + clean.substr(2) + L"\\*";
    }
    return L"\\\\?\\" + clean + L"\\*";
}

struct DirEntryRaw {
    uint32_t name_off;
    uint16_t name_len;
    uint64_t size;
    uint64_t allocated;
    int64_t  modified;
    uint8_t  flags;
};

struct DirEnumResult {
    uint32_t parent_idx;
    std::wstring parent_path;
    std::vector<DirEntryRaw> entries;
    std::vector<wchar_t> names;
};

std::unique_ptr<DiskTree> ScanParallelWin32(
    const std::wstring& rootPathIn,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime,
    std::wstring& outErr)
{
    progress.set_phase(ScanPhase::WalkingWin32);

    std::wstring cleanRoot = rootPathIn;
    while (cleanRoot.size() > 3 && cleanRoot.back() == L'\\') cleanRoot.pop_back();
    if (cleanRoot.size() == 2 && cleanRoot[1] == L':') cleanRoot.push_back(L'\\');

    std::vector<TreeNode> nodes;
    nodes.reserve(65536);
    std::vector<wchar_t> names;
    names.reserve(65536 * 16);
    ExtInterner interner;

    names.insert(names.end(), cleanRoot.begin(), cleanRoot.end());
    nodes.push_back(TreeNode{
        0, 0, 0, NONE_IDX, NONE_IDX, NONE_IDX, 0, 0,
        0, static_cast<uint16_t>(std::min<size_t>(cleanRoot.size(), 65535)),
        0, FLAG_IS_DIR
    });

    std::vector<std::pair<uint32_t, std::wstring>> frontier;
    frontier.emplace_back(0, cleanRoot);

    while (!frontier.empty()) {
        if (progress.is_cancelled()) {
            outErr = L"Scan cancelled.";
            return nullptr;
        }

        std::vector<DirEnumResult> results(frontier.size());
        std::atomic<size_t> nextIdx{ 0 };
        unsigned numThreads = std::min<unsigned>(
            static_cast<unsigned>(frontier.size()),
            std::max(2u, std::min(16u, std::thread::hardware_concurrency()))
        );

        auto worker = [&]() {
            while (true) {
                if (progress.is_cancelled()) break;
                size_t i = nextIdx.fetch_add(1, std::memory_order_relaxed);
                if (i >= frontier.size()) break;

                auto& res = results[i];
                res.parent_idx = frontier[i].first;
                res.parent_path = std::move(frontier[i].second);

                std::wstring pattern = MakeExtendedPattern(res.parent_path);
                WIN32_FIND_DATAW fd{};
                HANDLE hFind = FindFirstFileExW(
                    pattern.c_str(),
                    FindExInfoBasic,
                    &fd,
                    FindExSearchNameMatch,
                    nullptr,
                    FIND_FIRST_EX_LARGE_FETCH);
                if (hFind == INVALID_HANDLE_VALUE) continue;

                uint64_t localFiles = 0, localFolders = 0, localBytes = 0;
                do {
                    const wchar_t* fn = fd.cFileName;
                    if (fn[0] == L'.' && (fn[1] == L'\0' || (fn[1] == L'.' && fn[2] == L'\0'))) {
                        continue;
                    }
                    DWORD attrs = fd.dwFileAttributes;
                    bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    bool isReparse = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    if (isDir && isReparse) continue; // skip junctions/symlinks

                    size_t len = wcslen(fn);
                    if (len == 0) continue;

                    uint8_t flags = 0;
                    if (isDir) { flags |= FLAG_IS_DIR; ++localFolders; }
                    if (attrs & FILE_ATTRIBUTE_HIDDEN) flags |= FLAG_IS_HIDDEN;
                    if (attrs & FILE_ATTRIBUTE_SYSTEM) flags |= FLAG_IS_SYSTEM;
                    if (attrs & FILE_ATTRIBUTE_COMPRESSED) flags |= FLAG_IS_COMPRESSED;

                    uint64_t sz = isDir ? 0 : ((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
                    uint64_t alloc = sz == 0 ? 0 : (((sz + 4095) / 4096) * 4096);
                    int64_t mod = (static_cast<int64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;

                    if (!isDir) {
                        ++localFiles;
                        localBytes += sz;
                    }

                    uint32_t noff = static_cast<uint32_t>(res.names.size());
                    uint16_t nlen = static_cast<uint16_t>(std::min<size_t>(len, 65535));
                    res.names.insert(res.names.end(), fn, fn + nlen);
                    res.entries.push_back(DirEntryRaw{ noff, nlen, sz, alloc, mod, flags });
                } while (FindNextFileW(hFind, &fd));

                FindClose(hFind);
                progress.files.fetch_add(localFiles, std::memory_order_relaxed);
                progress.folders.fetch_add(localFolders, std::memory_order_relaxed);
                progress.bytes_processed.fetch_add(localBytes, std::memory_order_relaxed);
            }
        };

        if (numThreads <= 1) {
            worker();
        } else {
            std::vector<std::thread> threads;
            threads.reserve(numThreads);
            for (unsigned t = 0; t < numThreads; ++t) threads.emplace_back(worker);
            for (auto& t : threads) t.join();
        }

        std::vector<std::pair<uint32_t, std::wstring>> nextFrontier;
        for (const auto& res : results) {
            std::wstring base = res.parent_path;
            while (!base.empty() && base.back() == L'\\') base.pop_back();

            for (const auto& e : res.entries) {
                uint32_t idx = static_cast<uint32_t>(nodes.size());
                uint32_t nameOff = static_cast<uint32_t>(names.size());
                names.insert(names.end(), res.names.data() + e.name_off, res.names.data() + e.name_off + e.name_len);
                bool isDir = (e.flags & FLAG_IS_DIR) != 0;
                uint16_t extId = isDir ? 0 : interner.intern(std::wstring_view(names.data() + nameOff, e.name_len));

                nodes.push_back(TreeNode{
                    e.size, e.allocated, e.modified,
                    res.parent_idx, NONE_IDX, NONE_IDX, 0, 0,
                    nameOff, e.name_len, extId, e.flags
                });

                if (isDir) {
                    std::wstring sub = base + L"\\";
                    sub.append(names.data() + nameOff, e.name_len);
                    nextFrontier.emplace_back(idx, std::move(sub));
                }
            }
        }
        frontier = std::move(nextFrontier);
    }

    return FinalizeTree(std::move(nodes), std::move(names), std::move(interner), 0, cleanRoot, ScanMode::ParallelWin32, startTime, progress, outErr);
}

} // namespace

ScanResult ScanDriveOrFolder(const std::wstring& targetPath, bool isDrive, wchar_t driveLetter, ScanProgress& progress) {
    auto startTime = std::chrono::steady_clock::now();
    ScanResult res;
    if (isDrive) {
        res.tree = TryScanNtfsMft(driveLetter, progress, startTime, res.error);
        if (res.tree || progress.is_cancelled()) return res;
        res.error.clear();
        wchar_t root[8];
        swprintf_s(root, L"%c:\\", driveLetter);
        res.tree = ScanParallelWin32(root, progress, startTime, res.error);
        return res;
    } else {
        res.tree = ScanParallelWin32(targetPath, progress, startTime, res.error);
        return res;
    }
}
