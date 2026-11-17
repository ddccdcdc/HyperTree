#include "scanner.h"
#include "filters.h"
#include "win_utils.h"
#include <winioctl.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <execution>
#include <unordered_set>

namespace {

constexpr uint64_t NTFS_ROOT_FRN = 5;
constexpr size_t   MFT_CHUNK_BYTES = 16 * 1024 * 1024; // 16 MB sequential reads

#ifndef FILE_ATTRIBUTE_RECALL_ON_OPEN
#define FILE_ATTRIBUTE_RECALL_ON_OPEN 0x00040000
#endif
#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif

inline bool IsCloudPlaceholderAttrs(DWORD attrs) {
    return (attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0 ||
           (attrs & FILE_ATTRIBUTE_RECALL_ON_OPEN) != 0 ||
           ((attrs & FILE_ATTRIBUTE_OFFLINE) != 0 && (attrs & FILE_ATTRIBUTE_SPARSE_FILE) != 0);
}

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
    std::vector<uint8_t> visited(n, 0);
    std::vector<uint32_t> order;
    order.reserve(n);
    if (root_idx < n) {
        visited[root_idx] = 1;
        order.push_back(root_idx);
    }
    size_t head = 0;
    while (head < order.size()) {
        if ((head & 0xFFFF) == 0 && progress.is_cancelled()) {
            outErr = L"Scan cancelled.";
            return nullptr;
        }
        uint32_t cur = order[head++];
        uint32_t c = nodes[cur].first_child;
        while (c != NONE_IDX && c < n) {
            if (!visited[c]) {
                visited[c] = 1;
                order.push_back(c);
            }
            c = nodes[c].next_sibling;
        }
    }

    // Bottom-up size & count accumulation + collect multi-child dirs for sorting
    std::vector<uint32_t> sortDirs;
    sortDirs.reserve(n / 8 + 16);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        uint32_t idx = *it;
        const TreeNode& node = nodes[idx];
        if (node.is_dir()) {
            uint32_t c = node.first_child;
            if (c != NONE_IDX && nodes[c].next_sibling != NONE_IDX) {
                sortDirs.push_back(idx);
            }
        } else {
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

    // Sort children of multi-child directories in parallel by allocated size descending
    std::for_each(std::execution::par, sortDirs.begin(), sortDirs.end(), [&](uint32_t idx) {
        TreeNode& node = nodes[idx];
        thread_local std::vector<uint32_t> childBuf;
        childBuf.clear();
        uint32_t c = node.first_child;
        while (c != NONE_IDX) {
            childBuf.push_back(c);
            c = nodes[c].next_sibling;
        }
        std::sort(childBuf.begin(), childBuf.end(), [&](uint32_t a, uint32_t b) {
            if (nodes[a].allocated != nodes[b].allocated) return nodes[a].allocated > nodes[b].allocated;
            if (nodes[a].size != nodes[b].size) return nodes[a].size > nodes[b].size;
            return nodes[a].is_dir() > nodes[b].is_dir();
        });
        node.first_child = childBuf[0];
        for (size_t k = 0; k + 1 < childBuf.size(); ++k) {
            nodes[childBuf[k]].next_sibling = childBuf[k + 1];
        }
        nodes[childBuf.back()].next_sibling = NONE_IDX;
    });

    // Collect all reachable files and sort largest files first in parallel
    std::vector<uint32_t> top_files;
    top_files.reserve(order.size());
    for (uint32_t idx : order) {
        if (!nodes[idx].is_dir()) top_files.push_back(idx);
    }
    if (top_files.size() > 250000) {
        auto mid = std::partition(std::execution::par, top_files.begin(), top_files.end(), [&](uint32_t idx) {
            return nodes[idx].size >= 32768;
        });
        std::sort(std::execution::par, top_files.begin(), mid, [&](uint32_t a, uint32_t b) {
            return nodes[a].size > nodes[b].size;
        });
        std::sort(std::execution::par, mid, top_files.end(), [&](uint32_t a, uint32_t b) {
            return nodes[a].size > nodes[b].size;
        });
    } else {
        std::sort(std::execution::par, top_files.begin(), top_files.end(), [&](uint32_t a, uint32_t b) {
            return nodes[a].size > nodes[b].size;
        });
    }

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

void SaveHyperCacheSync(
    wchar_t driveLetter, uint64_t journalId, int64_t nextUsn, uint32_t rootIdx,
    const std::vector<TreeNode>& nodes, const std::vector<wchar_t>& names,
    const std::vector<ExtStat>& exts, const std::vector<uint32_t>& topFiles,
    const std::vector<uint64_t>& nodeFrns,
    uint64_t totalFiles, uint64_t totalFolders, uint64_t totalSize, uint64_t totalAllocated);

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

    // Open raw volume with OVERLAPPED + NO_BUFFERING for concurrent NVMe DMA reads
    HandleGuard volGuard;
    volGuard.h = CreateFileW(
        volPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED, nullptr);
    if (volGuard.h == INVALID_HANDLE_VALUE) {
        outErr = L"Cannot open raw volume.";
        return nullptr;
    }

    // Also open a synchronous handle for fast FSCTL / record queries
    HandleGuard volSync;
    volSync.h = CreateFileW(
        volPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE fsctlVol = (volSync.h != INVALID_HANDLE_VALUE) ? volSync.h : volGuard.h;

    NTFS_VOLUME_DATA_BUFFER volData{};
    DWORD bytesRet = 0;
    if (!DeviceIoControl(fsctlVol, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0,
        &volData, sizeof(volData), &bytesRet, nullptr)) {
        outErr = L"Volume is not NTFS.";
        return nullptr;
    }

    // Snapshot USN Journal cursor before scanning $MFT so subsequent incremental sync catches all changes
    uint64_t snapJournalId = 0;
    USN snapNextUsn = 0;
    {
        USN_JOURNAL_DATA_V0 ujd{};
        DWORD uret = 0;
        if (DeviceIoControl(fsctlVol, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &ujd, sizeof(ujd), &uret, nullptr)) {
            snapJournalId = ujd.UsnJournalID;
            snapNextUsn = ujd.NextUsn;
        }
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
        extents = GetMftExtentsFromRecordZero(fsctlVol, recordSize);
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

    const size_t estPerWorker = std::max<size_t>(32768, static_cast<size_t>((mftValidBytes / recordSize) / numWorkers));
    for (unsigned w = 0; w < numWorkers; ++w) {
        workers.emplace_back([&, w, estPerWorker]() {
            auto& out = workerOutputs[w];
            out.records.reserve(estPerWorker);
            out.names.reserve(estPerWorker * 18);

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
                    if (attrOff < 42 || attrOff >= bytesUsed) continue;

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
                    if (IsCloudPlaceholderAttrs(stdAttrs)) {
                        flags |= FLAG_IS_CLOUD;
                        if (stdAttrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) {
                            allocSize = 0;
                        }
                    }
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

    // Pipelined 4-slot asynchronous OVERLAPPED I/O ring on current thread (keeps NVMe queue busy while workers parse)
    uint64_t remainingValid = mftValidBytes;
    uint64_t currentFrn = 0;
    const size_t chunkAligned = (MFT_CHUNK_BYTES / bytesPerSector) * bytesPerSector;

    struct ReadJob {
        uint64_t diskOffset;
        DWORD toRead;
        size_t validBytes;
    };
    std::vector<ReadJob> jobs;
    std::vector<std::pair<size_t, uint64_t>> sparseSkips; // job index -> records skipped before it
    {
        uint64_t rem = mftValidBytes;
        for (const auto& ext : extents) {
            if (rem == 0) break;
            uint64_t extBytes = std::min<uint64_t>(ext.clusters * bytesPerCluster, rem);
            if (ext.lcn < 0) {
                sparseSkips.emplace_back(jobs.size(), extBytes / recordSize);
                rem -= extBytes;
                continue;
            }
            uint64_t baseOff = static_cast<uint64_t>(ext.lcn) * bytesPerCluster;
            size_t extRem = static_cast<size_t>(extBytes);
            size_t extPos = 0;
            while (extRem > 0) {
                size_t raw = std::min(extRem, chunkAligned);
                DWORD toRead = static_cast<DWORD>(((raw + bytesPerSector - 1) / bytesPerSector) * bytesPerSector);
                jobs.push_back(ReadJob{ baseOff + extPos, toRead, raw });
                extPos += raw;
                extRem -= raw;
            }
            rem -= extBytes;
        }
    }

    constexpr size_t RING = 4;
    struct IoSlot {
        uint8_t* buf = nullptr;
        OVERLAPPED ov{};
        HANDLE hEv = nullptr;
        size_t validBytes = 0;
        bool pending = false;
    };
    IoSlot slots[RING];
    const size_t slotCap = chunkAligned + bytesPerSector;
    for (size_t s = 0; s < RING; ++s) {
        slots[s].buf = static_cast<uint8_t*>(VirtualAlloc(nullptr, slotCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        slots[s].hEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }

    auto issueJob = [&](size_t slotIdx, size_t jobIdx) {
        auto& sl = slots[slotIdx];
        const auto& jb = jobs[jobIdx];
        ResetEvent(sl.hEv);
        ZeroMemory(&sl.ov, sizeof(sl.ov));
        sl.ov.Offset = static_cast<DWORD>(jb.diskOffset & 0xFFFFFFFFULL);
        sl.ov.OffsetHigh = static_cast<DWORD>(jb.diskOffset >> 32);
        sl.ov.hEvent = sl.hEv;
        sl.validBytes = jb.validBytes;
        DWORD dummy = 0;
        BOOL ok = ReadFile(volGuard.h, sl.buf, jb.toRead, &dummy, &sl.ov);
        sl.pending = (ok || GetLastError() == ERROR_IO_PENDING);
    };

    size_t nextSubmit = 0;
    for (size_t s = 0; s < RING && nextSubmit < jobs.size(); ++s) {
        issueJob(s, nextSubmit++);
    }

    std::vector<uint8_t> leftover;
    leftover.reserve(recordSize);
    size_t skipCursor = 0;

    for (size_t completeIdx = 0; completeIdx < jobs.size() && !progress.is_cancelled(); ++completeIdx) {
        while (skipCursor < sparseSkips.size() && sparseSkips[skipCursor].first == completeIdx) {
            currentFrn += sparseSkips[skipCursor++].second;
        }
        size_t s = completeIdx % RING;
        auto& sl = slots[s];
        if (!sl.pending) break;
        DWORD actual = 0;
        if (!GetOverlappedResult(volGuard.h, &sl.ov, &actual, TRUE) || actual == 0) break;

        size_t validInBuf = std::min<size_t>(actual, sl.validBytes);
        progress.mft_bytes_read.fetch_add(validInBuf, std::memory_order_relaxed);

        size_t totalChunkBytes = leftover.size() + validInBuf;
        size_t remMod = totalChunkBytes % recordSize;
        size_t usableBytes = totalChunkBytes - remMod;

        std::vector<uint8_t> buf(usableBytes);
        if (!leftover.empty()) {
            size_t copyFromBuf = std::min(usableBytes, leftover.size());
            memcpy(buf.data(), leftover.data(), copyFromBuf);
            if (usableBytes > leftover.size()) {
                memcpy(buf.data() + leftover.size(), sl.buf, usableBytes - leftover.size());
            }
            if (remMod > 0 && validInBuf >= remMod) {
                leftover.assign(sl.buf + validInBuf - remMod, sl.buf + validInBuf);
            } else {
                leftover.clear();
            }
        } else {
            memcpy(buf.data(), sl.buf, usableBytes);
            if (remMod > 0) {
                leftover.assign(sl.buf + usableBytes, sl.buf + validInBuf);
            }
        }

        // Re-submit slot immediately so NVMe stays 3-4 reads ahead while we enqueue
        if (nextSubmit < jobs.size() && !progress.is_cancelled()) {
            issueJob(s, nextSubmit++);
        }

        uint64_t numRecs = usableBytes / recordSize;
        if (numRecs > 0) {
            MftChunk chunk{ currentFrn, std::move(buf) };
            currentFrn += numRecs;
            std::unique_lock<std::mutex> lk(qMtx);
            qNotFull.wait(lk, [&]() { return queue.size() < 6 || progress.is_cancelled(); });
            if (progress.is_cancelled()) break;
            queue.push_back(std::move(chunk));
            qNotEmpty.notify_one();
        }
    }

    for (size_t s = 0; s < RING; ++s) {
        if (slots[s].pending) {
            DWORD dummy = 0;
            GetOverlappedResult(volGuard.h, &slots[s].ov, &dummy, TRUE);
        }
        if (slots[s].hEv) CloseHandle(slots[s].hEv);
        if (slots[s].buf) VirtualFree(slots[s].buf, 0, MEM_RELEASE);
    }
    (void)remainingValid;

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
    std::vector<uint64_t> nodeFrns;
    nodeFrns.reserve(totalRecs + 1);

    for (const auto& wo : workerOutputs) {
        size_t baseNameOff = names.size();
        names.insert(names.end(), wo.names.begin(), wo.names.end());
        for (const auto& r : wo.records) {
            uint32_t idx = static_cast<uint32_t>(nodes.size());
            if (r.frn >= frnToIdx.size()) {
                frnToIdx.resize(static_cast<size_t>(r.frn) + 1024, NONE_IDX);
            }
            frnToIdx[static_cast<size_t>(r.frn)] = idx;

            uint32_t nameOff = static_cast<uint32_t>(baseNameOff + r.name_off);
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
            nodeFrns.push_back(r.frn);
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
    auto tree = FinalizeTree(std::move(nodes), std::move(names), std::move(interner), rootIdx, rootPath, ScanMode::NtfsMft, startTime, progress, outErr);
    if (tree && snapJournalId != 0) {
        SaveHyperCacheSync(driveLetter, snapJournalId, snapNextUsn, tree->root_idx,
            tree->nodes, tree->names, tree->extensions, tree->top_files, nodeFrns,
            tree->total_files, tree->total_folders, tree->total_size, tree->total_allocated);
    }
    return tree;
}

// ============================================================================
// HyperCache Binary Snapshot + Live NTFS USN Journal Sync (< 1s Rescan)
// ============================================================================
struct HyperCacheHeader {
    uint64_t magic;
    uint32_t version;
    uint32_t root_idx;
    wchar_t  drive_letter;
    uint16_t reserved[3];
    uint64_t journal_id;
    int64_t  next_usn;
    uint64_t node_count;
    uint64_t name_chars;
    uint64_t ext_count;
    uint64_t top_file_count;
    uint64_t total_files;
    uint64_t total_folders;
    uint64_t total_size;
    uint64_t total_allocated;
};
constexpr uint64_t HYPERCACHE_MAGIC = 0x4854434143484533ULL;

inline std::wstring GetHyperCachePath(wchar_t driveLetter) {
    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(appData) + L"\\HyperTree" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t fn[32];
    swprintf_s(fn, L"\\cache_%c.htc", static_cast<wchar_t>(towupper(driveLetter)));
    return dir + fn;
}

inline bool WriteAllBytes(HANDLE h, const void* data, size_t bytes) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    while (bytes > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes, 64ULL * 1024 * 1024));
        DWORD wr = 0;
        if (!WriteFile(h, p, chunk, &wr, nullptr) || wr == 0) return false;
        p += wr;
        bytes -= wr;
    }
    return true;
}

inline bool ReadAllBytes(HANDLE h, void* data, size_t bytes) {
    uint8_t* p = static_cast<uint8_t*>(data);
    while (bytes > 0) {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes, 64ULL * 1024 * 1024));
        DWORD rd = 0;
        if (!ReadFile(h, p, chunk, &rd, nullptr) || rd == 0) return false;
        p += rd;
        bytes -= rd;
    }
    return true;
}

void SaveHyperCacheSync(
    wchar_t driveLetter,
    uint64_t journalId,
    int64_t nextUsn,
    uint32_t rootIdx,
    const std::vector<TreeNode>& nodes,
    const std::vector<wchar_t>& names,
    const std::vector<ExtStat>& exts,
    const std::vector<uint32_t>& topFiles,
    const std::vector<uint64_t>& nodeFrns,
    uint64_t totalFiles,
    uint64_t totalFolders,
    uint64_t totalSize,
    uint64_t totalAllocated)
{
    if (journalId == 0 || nodes.empty() || nodeFrns.size() != nodes.size()) return;
    std::wstring path = GetHyperCachePath(driveLetter);
    std::wstring tmpPath = path + L".tmp";
    HandleGuard hf;
    hf.h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hf.h == INVALID_HANDLE_VALUE) return;

    HyperCacheHeader hdr{};
    hdr.magic = HYPERCACHE_MAGIC;
    hdr.version = 1;
    hdr.root_idx = rootIdx;
    hdr.drive_letter = static_cast<wchar_t>(towupper(driveLetter));
    hdr.journal_id = journalId;
    hdr.next_usn = nextUsn;
    hdr.node_count = nodes.size();
    hdr.name_chars = names.size();
    hdr.ext_count = exts.size();
    hdr.top_file_count = topFiles.size();
    hdr.total_files = totalFiles;
    hdr.total_folders = totalFolders;
    hdr.total_size = totalSize;
    hdr.total_allocated = totalAllocated;

    bool ok = WriteAllBytes(hf.h, &hdr, sizeof(hdr));
    if (ok) ok = WriteAllBytes(hf.h, nodes.data(), nodes.size() * sizeof(TreeNode));
    if (ok) ok = WriteAllBytes(hf.h, names.data(), names.size() * sizeof(wchar_t));
    if (ok) ok = WriteAllBytes(hf.h, topFiles.data(), topFiles.size() * sizeof(uint32_t));
    if (ok) ok = WriteAllBytes(hf.h, nodeFrns.data(), nodeFrns.size() * sizeof(uint64_t));
    if (ok) {
        for (const auto& es : exts) {
            uint32_t slen = static_cast<uint32_t>(es.ext.size());
            if (!WriteAllBytes(hf.h, &es.size, sizeof(es.size)) ||
                !WriteAllBytes(hf.h, &es.allocated, sizeof(es.allocated)) ||
                !WriteAllBytes(hf.h, &es.count, sizeof(es.count)) ||
                !WriteAllBytes(hf.h, &es.color_rgb, sizeof(es.color_rgb)) ||
                !WriteAllBytes(hf.h, &slen, sizeof(slen)) ||
                (slen > 0 && !WriteAllBytes(hf.h, es.ext.data(), slen * sizeof(wchar_t)))) {
                ok = false;
                break;
            }
        }
    }
    CloseHandle(hf.h);
    hf.h = INVALID_HANDLE_VALUE;
    if (ok) MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmpPath.c_str());
}

// Fast incremental sync from HyperCache + live NTFS USN Journal
std::unique_ptr<DiskTree> TrySyncHyperCacheUsn(
    wchar_t driveLetter,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime,
    std::wstring& outErr)
{
    EnablePrivilege(SE_BACKUP_NAME);
    EnablePrivilege(SE_MANAGE_VOLUME_NAME);

    std::wstring path = GetHyperCachePath(driveLetter);
    HandleGuard hf;
    hf.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hf.h == INVALID_HANDLE_VALUE) return nullptr;

    HyperCacheHeader hdr{};
    if (!ReadAllBytes(hf.h, &hdr, sizeof(hdr))) return nullptr;
    if (hdr.magic != HYPERCACHE_MAGIC || hdr.version != 1 || hdr.node_count == 0 ||
        hdr.root_idx >= hdr.node_count || hdr.journal_id == 0) {
        return nullptr;
    }

    // Open volume synchronously to check USN journal before heavy reads
    wchar_t volPath[16];
    swprintf_s(volPath, L"\\\\.\\%c:", driveLetter);
    HandleGuard volGuard;
    volGuard.h = CreateFileW(volPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (volGuard.h == INVALID_HANDLE_VALUE) return nullptr;

    USN_JOURNAL_DATA_V0 ujd{};
    DWORD bytesRet = 0;
    if (!DeviceIoControl(volGuard.h, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &ujd, sizeof(ujd), &bytesRet, nullptr)) {
        return nullptr;
    }
    if (ujd.UsnJournalID != hdr.journal_id || hdr.next_usn < ujd.LowestValidUsn || hdr.next_usn > ujd.NextUsn) {
        return nullptr;
    }

    // Read USN changes since hdr.next_usn
    std::vector<uint64_t> changedFrns;
    std::unordered_set<uint64_t> changedSet;
    USN latestUsn = ujd.NextUsn;
    if (hdr.next_usn < ujd.NextUsn) {
        READ_USN_JOURNAL_DATA_V0 rujd{};
        rujd.StartUsn = hdr.next_usn;
        rujd.ReasonMask = USN_REASON_DATA_OVERWRITE | USN_REASON_DATA_EXTEND | USN_REASON_DATA_TRUNCATION |
                          USN_REASON_FILE_CREATE | USN_REASON_FILE_DELETE | USN_REASON_RENAME_NEW_NAME |
                          USN_REASON_BASIC_INFO_CHANGE;
        rujd.UsnJournalID = ujd.UsnJournalID;
        std::vector<uint8_t> usnBuf(256 * 1024);
        while (rujd.StartUsn < ujd.NextUsn && !progress.is_cancelled()) {
            DWORD ret = 0;
            if (!DeviceIoControl(volGuard.h, FSCTL_READ_USN_JOURNAL, &rujd, sizeof(rujd),
                usnBuf.data(), static_cast<DWORD>(usnBuf.size()), &ret, nullptr) || ret <= sizeof(USN)) {
                break;
            }
            USN nextStart = ReadU64(usnBuf.data());
            size_t off = sizeof(USN);
            while (off + sizeof(DWORD) * 2 < ret) {
                auto* ur = reinterpret_cast<const USN_RECORD_V2*>(usnBuf.data() + off);
                if (ur->RecordLength == 0 || off + ur->RecordLength > ret) break;
                uint64_t frn = ur->FileReferenceNumber & 0x0000FFFFFFFFFFFFULL;
                if (changedSet.insert(frn).second) {
                    changedFrns.push_back(frn);
                }
                if (changedFrns.size() > 25000) {
                    return nullptr; // Too many changes; fall back to full MFT scan
                }
                off += ur->RecordLength;
            }
            if (nextStart <= rujd.StartUsn) break;
            rujd.StartUsn = nextStart;
            latestUsn = nextStart;
        }
    }

    progress.set_phase(ScanPhase::BuildingTree);
    std::vector<TreeNode> nodes(hdr.node_count);
    std::vector<wchar_t> names(hdr.name_chars);
    std::vector<uint32_t> topFiles(hdr.top_file_count);
    std::vector<uint64_t> nodeFrns(hdr.node_count);
    std::vector<ExtStat> exts(hdr.ext_count);

    if (!ReadAllBytes(hf.h, nodes.data(), nodes.size() * sizeof(TreeNode)) ||
        !ReadAllBytes(hf.h, names.data(), names.size() * sizeof(wchar_t)) ||
        !ReadAllBytes(hf.h, topFiles.data(), topFiles.size() * sizeof(uint32_t)) ||
        !ReadAllBytes(hf.h, nodeFrns.data(), nodeFrns.size() * sizeof(uint64_t))) {
        return nullptr;
    }
    for (size_t i = 0; i < exts.size(); ++i) {
        auto& es = exts[i];
        uint32_t slen = 0;
        if (!ReadAllBytes(hf.h, &es.size, sizeof(es.size)) ||
            !ReadAllBytes(hf.h, &es.allocated, sizeof(es.allocated)) ||
            !ReadAllBytes(hf.h, &es.count, sizeof(es.count)) ||
            !ReadAllBytes(hf.h, &es.color_rgb, sizeof(es.color_rgb)) ||
            !ReadAllBytes(hf.h, &slen, sizeof(slen)) || slen > 64) {
            return nullptr;
        }
        es.ext.resize(slen);
        if (slen > 0 && !ReadAllBytes(hf.h, es.ext.data(), slen * sizeof(wchar_t))) {
            return nullptr;
        }
    }

    wchar_t rootPath[8];
    swprintf_s(rootPath, L"%c:\\", driveLetter);

    if (changedFrns.empty()) {
        auto tree = std::make_unique<DiskTree>();
        tree->root_idx = hdr.root_idx;
        tree->root_path = rootPath;
        tree->scan_mode = ScanMode::HyperCacheUsn;
        tree->total_files = hdr.total_files;
        tree->total_folders = hdr.total_folders;
        tree->total_size = hdr.total_size;
        tree->total_allocated = hdr.total_allocated;
        tree->nodes = std::move(nodes);
        tree->names = std::move(names);
        tree->extensions = std::move(exts);
        tree->top_files = std::move(topFiles);
        tree->scan_duration_secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        progress.files.store(tree->total_files, std::memory_order_relaxed);
        progress.folders.store(tree->total_folders, std::memory_order_relaxed);
        progress.bytes_processed.store(tree->total_size, std::memory_order_relaxed);
        return tree;
    }

    NTFS_VOLUME_DATA_BUFFER volData{};
    if (!DeviceIoControl(volGuard.h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &volData, sizeof(volData), &bytesRet, nullptr)) {
        return nullptr;
    }
    const size_t bytesPerSector = std::max<size_t>(volData.BytesPerSector, 512);
    const size_t recordSize = std::max<size_t>(volData.BytesPerFileRecordSegment, 1024);

    ExtInterner interner;
    interner.stats.clear();
    interner.map.clear();
    for (size_t i = 0; i < exts.size(); ++i) {
        interner.stats.push_back(exts[i]);
        interner.stats.back().size = 0;
        interner.stats.back().allocated = 0;
        interner.stats.back().count = 0;
        std::wstring key = (i == 0 || exts[i].ext.empty() || exts[i].ext[0] != L'.') ? L"" : exts[i].ext.substr(1);
        interner.map.emplace(std::move(key), static_cast<uint16_t>(i));
    }

    uint64_t maxFrn = 0;
    for (uint64_t f : nodeFrns) maxFrn = std::max(maxFrn, f);
    for (uint64_t f : changedFrns) maxFrn = std::max(maxFrn, f);
    std::vector<uint32_t> frnToIdx(static_cast<size_t>(maxFrn) + 64, NONE_IDX);
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i) {
        if (nodeFrns[i] < frnToIdx.size()) frnToIdx[static_cast<size_t>(nodeFrns[i])] = i;
    }

    for (auto& nd : nodes) {
        if (nd.is_dir()) {
            nd.size = 0;
            nd.allocated = 0;
            nd.files = 0;
            nd.folders = 0;
        }
    }

    std::vector<uint8_t> recBuf(sizeof(NTFS_FILE_RECORD_OUTPUT_BUFFER) + recordSize + 64);
    for (uint64_t cfrn : changedFrns) {
        if (cfrn < 24 && cfrn != NTFS_ROOT_FRN) continue;
        NTFS_FILE_RECORD_INPUT_BUFFER inBuf{};
        inBuf.FileReferenceNumber.QuadPart = static_cast<LONGLONG>(cfrn);
        DWORD outSz = 0;
        if (!DeviceIoControl(volGuard.h, FSCTL_GET_NTFS_FILE_RECORD, &inBuf, sizeof(inBuf),
            recBuf.data(), static_cast<DWORD>(recBuf.size()), &outSz, nullptr)) {
            continue;
        }
        auto* outHdr = reinterpret_cast<const NTFS_FILE_RECORD_OUTPUT_BUFFER*>(recBuf.data());
        if (outHdr->FileRecordLength < recordSize) continue;
        std::vector<uint8_t> recCopy(outHdr->FileRecordBuffer, outHdr->FileRecordBuffer + recordSize);
        uint8_t* rec = recCopy.data();

        bool alive = (memcmp(rec, "FILE", 4) == 0) && ((ReadU16(rec + 22) & 0x01) != 0);
        uint32_t existingIdx = (cfrn < frnToIdx.size()) ? frnToIdx[static_cast<size_t>(cfrn)] : NONE_IDX;
        if (!alive) {
            if (existingIdx != NONE_IDX && existingIdx != hdr.root_idx) {
                nodes[existingIdx].parent = NONE_IDX;
                nodes[existingIdx].size = 0;
                nodes[existingIdx].allocated = 0;
            }
            continue;
        }

        uint16_t usaOff = ReadU16(rec + 4);
        uint16_t usaCnt = ReadU16(rec + 6);
        if (usaCnt < 2 || static_cast<size_t>(usaOff) + static_cast<size_t>(usaCnt) * 2 > recordSize) continue;
        uint8_t usn0 = rec[usaOff], usn1 = rec[usaOff + 1];
        bool fixOk = true;
        for (uint16_t s = 1; s < usaCnt; ++s) {
            size_t secEnd = static_cast<size_t>(s) * bytesPerSector;
            if (secEnd > recordSize) break;
            if (rec[secEnd - 2] != usn0 || rec[secEnd - 1] != usn1) { fixOk = false; break; }
            rec[secEnd - 2] = rec[usaOff + s * 2];
            rec[secEnd - 1] = rec[usaOff + s * 2 + 1];
        }
        if (!fixOk) continue;

        uint64_t baseRecord = ReadU64(rec + 32) & 0x0000FFFFFFFFFFFFULL;
        if (baseRecord != 0) continue;
        uint16_t recFlags = ReadU16(rec + 22);
        bool isDir = (recFlags & 0x02) != 0;
        size_t attrOff = ReadU16(rec + 20);
        size_t bytesUsed = std::min<size_t>(ReadU32(rec + 24), recordSize);
        if (attrOff < 42 || attrOff >= bytesUsed) continue;

        int64_t modified = 0;
        uint32_t stdAttrs = 0;
        const wchar_t* bestNamePtr = nullptr;
        uint16_t bestNameLen = 0;
        uint8_t bestNsRank = 255;
        uint64_t parentFrn = NTFS_ROOT_FRN;
        uint64_t fileSize = 0;
        uint64_t allocSize = 0;

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
            } else if (attrType == 0x80 && attrNameLen == 0) {
                if (nonRes == 0) {
                    fileSize = ReadU32(rec + attrOff + 16);
                    allocSize = 0;
                } else if (attrLen >= 56 && ReadU64(rec + attrOff + 16) == 0) {
                    uint64_t alloc = ReadU64(rec + attrOff + 40);
                    uint64_t dsize = ReadU64(rec + attrOff + 48);
                    uint16_t aflags = ReadU16(rec + attrOff + 12);
                    if ((aflags & 0x8001) != 0 && attrLen >= 72) alloc = ReadU64(rec + attrOff + 64);
                    fileSize = dsize;
                    allocSize = alloc;
                }
            }
            attrOff += attrLen;
        }
        if (!bestNamePtr || bestNameLen == 0) continue;
        if (cfrn < 24 && cfrn != NTFS_ROOT_FRN && bestNamePtr[0] == L'$') continue;

        uint8_t flags = 0;
        if (isDir) flags |= FLAG_IS_DIR;
        if (stdAttrs & FILE_ATTRIBUTE_HIDDEN) flags |= FLAG_IS_HIDDEN;
        if (stdAttrs & FILE_ATTRIBUTE_SYSTEM) flags |= FLAG_IS_SYSTEM;
        if (stdAttrs & FILE_ATTRIBUTE_COMPRESSED) flags |= FLAG_IS_COMPRESSED;
        if (IsCloudPlaceholderAttrs(stdAttrs)) {
            flags |= FLAG_IS_CLOUD;
            if (stdAttrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) allocSize = 0;
        }

        uint32_t parentIdx = (cfrn == hdr.root_idx) ? NONE_IDX : ((parentFrn < frnToIdx.size()) ? frnToIdx[static_cast<size_t>(parentFrn)] : NONE_IDX);
        uint32_t nameOff = static_cast<uint32_t>(names.size());
        names.insert(names.end(), bestNamePtr, bestNamePtr + bestNameLen);
        uint16_t extId = isDir ? 0 : interner.intern(std::wstring_view(names.data() + nameOff, bestNameLen));

        if (existingIdx != NONE_IDX) {
            nodes[existingIdx] = TreeNode{
                isDir ? 0 : fileSize, isDir ? 0 : allocSize, modified,
                parentIdx, NONE_IDX, NONE_IDX, 0, 0,
                nameOff, bestNameLen, extId, flags
            };
        } else {
            uint32_t newIdx = static_cast<uint32_t>(nodes.size());
            if (cfrn < frnToIdx.size()) frnToIdx[static_cast<size_t>(cfrn)] = newIdx;
            nodeFrns.push_back(cfrn);
            nodes.push_back(TreeNode{
                isDir ? 0 : fileSize, isDir ? 0 : allocSize, modified,
                parentIdx, NONE_IDX, NONE_IDX, 0, 0,
                nameOff, bestNameLen, extId, flags
            });
        }
    }

    auto tree = FinalizeTree(std::move(nodes), std::move(names), std::move(interner), hdr.root_idx, rootPath, ScanMode::HyperCacheUsn, startTime, progress, outErr);
    if (tree) {
        SaveHyperCacheSync(driveLetter, ujd.UsnJournalID, latestUsn, tree->root_idx,
            tree->nodes, tree->names, tree->extensions, tree->top_files, nodeFrns,
            tree->total_files, tree->total_folders, tree->total_size, tree->total_allocated);
    }
    return tree;
}

inline std::wstring GetHyperCacheDir() {
    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(appData) + L"\\HyperTree" : L".";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

inline std::wstring GetStdCachePath(wchar_t driveLetter) {
    wchar_t fn[32];
    swprintf_s(fn, L"\\cache_%c_std.htc", static_cast<wchar_t>(towupper(driveLetter)));
    return GetHyperCacheDir() + fn;
}

inline std::wstring NormalizeScanRoot(std::wstring p) {
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();
    if (p.size() == 2 && p[1] == L':') p.push_back(L'\\');
    return p;
}

inline std::wstring GetFolderStdCachePath(const std::wstring& folderPath) {
    std::wstring norm = NormalizeScanRoot(folderPath);
    uint64_t h = 14695981039346656037ULL;
    for (wchar_t c : norm) {
        wchar_t lc = FastToLower(c);
        h ^= static_cast<uint64_t>(lc);
        h *= 1099511628211ULL;
    }
    wchar_t fn[48];
    swprintf_s(fn, L"\\cache_fld_%016llx.htc", static_cast<unsigned long long>(h));
    return GetHyperCacheDir() + fn;
}

void SaveStdCacheToPath(const std::wstring& path, wchar_t driveLetter, const DiskTree& tree) {
    if (tree.nodes.empty()) return;
    std::wstring tmpPath = path + L".tmp";
    HandleGuard hf;
    hf.h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hf.h == INVALID_HANDLE_VALUE) return;

    HyperCacheHeader hdr{};
    hdr.magic = HYPERCACHE_MAGIC;
    hdr.version = 2;
    hdr.root_idx = tree.root_idx;
    hdr.drive_letter = driveLetter ? static_cast<wchar_t>(towupper(driveLetter)) : L'_';
    hdr.node_count = tree.nodes.size();
    hdr.name_chars = tree.names.size();
    hdr.ext_count = tree.extensions.size();
    hdr.top_file_count = tree.top_files.size();
    hdr.total_files = tree.total_files;
    hdr.total_folders = tree.total_folders;
    hdr.total_size = tree.total_size;
    hdr.total_allocated = tree.total_allocated;

    bool ok = WriteAllBytes(hf.h, &hdr, sizeof(hdr));
    if (ok) ok = WriteAllBytes(hf.h, tree.nodes.data(), tree.nodes.size() * sizeof(TreeNode));
    if (ok) ok = WriteAllBytes(hf.h, tree.names.data(), tree.names.size() * sizeof(wchar_t));
    if (ok) ok = WriteAllBytes(hf.h, tree.top_files.data(), tree.top_files.size() * sizeof(uint32_t));
    if (ok) {
        for (const auto& es : tree.extensions) {
            uint32_t slen = static_cast<uint32_t>(es.ext.size());
            if (!WriteAllBytes(hf.h, &es.size, sizeof(es.size)) ||
                !WriteAllBytes(hf.h, &es.allocated, sizeof(es.allocated)) ||
                !WriteAllBytes(hf.h, &es.count, sizeof(es.count)) ||
                !WriteAllBytes(hf.h, &es.color_rgb, sizeof(es.color_rgb)) ||
                !WriteAllBytes(hf.h, &slen, sizeof(slen)) ||
                (slen > 0 && !WriteAllBytes(hf.h, es.ext.data(), slen * sizeof(wchar_t)))) {
                ok = false;
                break;
            }
        }
    }
    CloseHandle(hf.h);
    hf.h = INVALID_HANDLE_VALUE;
    if (ok) MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileW(tmpPath.c_str());
}

void SaveStdHyperCache(wchar_t driveLetter, const DiskTree& tree) {
    SaveStdCacheToPath(GetStdCachePath(driveLetter), driveLetter, tree);
}

std::unique_ptr<DiskTree> TryLoadStdCacheFromPath(
    const std::wstring& path,
    const std::wstring& rootPath,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime)
{
    HandleGuard hf;
    hf.h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hf.h == INVALID_HANDLE_VALUE) return nullptr;

    HyperCacheHeader hdr{};
    if (!ReadAllBytes(hf.h, &hdr, sizeof(hdr))) return nullptr;
    if (hdr.magic != HYPERCACHE_MAGIC || hdr.version != 2 || hdr.node_count == 0 || hdr.root_idx >= hdr.node_count) {
        return nullptr;
    }

    progress.set_phase(ScanPhase::BuildingTree);
    auto tree = std::make_unique<DiskTree>();
    tree->root_idx = hdr.root_idx;
    tree->root_path = rootPath;
    tree->scan_mode = ScanMode::HyperCacheInstant;
    tree->total_files = hdr.total_files;
    tree->total_folders = hdr.total_folders;
    tree->total_size = hdr.total_size;
    tree->total_allocated = hdr.total_allocated;
    tree->nodes.resize(hdr.node_count);
    tree->names.resize(hdr.name_chars);
    tree->top_files.resize(hdr.top_file_count);
    tree->extensions.resize(hdr.ext_count);

    if (!ReadAllBytes(hf.h, tree->nodes.data(), tree->nodes.size() * sizeof(TreeNode)) ||
        !ReadAllBytes(hf.h, tree->names.data(), tree->names.size() * sizeof(wchar_t)) ||
        !ReadAllBytes(hf.h, tree->top_files.data(), tree->top_files.size() * sizeof(uint32_t))) {
        return nullptr;
    }
    for (size_t i = 0; i < tree->extensions.size(); ++i) {
        auto& es = tree->extensions[i];
        uint32_t slen = 0;
        if (!ReadAllBytes(hf.h, &es.size, sizeof(es.size)) ||
            !ReadAllBytes(hf.h, &es.allocated, sizeof(es.allocated)) ||
            !ReadAllBytes(hf.h, &es.count, sizeof(es.count)) ||
            !ReadAllBytes(hf.h, &es.color_rgb, sizeof(es.color_rgb)) ||
            !ReadAllBytes(hf.h, &slen, sizeof(slen)) || slen > 64) {
            return nullptr;
        }
        es.ext.resize(slen);
        if (slen > 0 && !ReadAllBytes(hf.h, es.ext.data(), slen * sizeof(wchar_t))) {
            return nullptr;
        }
    }

    tree->scan_duration_secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
    progress.files.store(tree->total_files, std::memory_order_relaxed);
    progress.folders.store(tree->total_folders, std::memory_order_relaxed);
    progress.bytes_processed.store(tree->total_size, std::memory_order_relaxed);
    return tree;
}

std::unique_ptr<DiskTree> TryLoadStdHyperCache(
    wchar_t driveLetter,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime)
{
    wchar_t rootPath[8];
    swprintf_s(rootPath, L"%c:\\", static_cast<wchar_t>(towupper(driveLetter)));
    return TryLoadStdCacheFromPath(GetStdCachePath(driveLetter), rootPath, progress, startTime);
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

inline std::wstring MakeExtendedDirPath(const std::wstring& dirPath) {
    std::wstring clean = dirPath;
    while (clean.size() > 3 && clean.back() == L'\\') clean.pop_back();
    if (clean.rfind(L"\\\\?\\", 0) == 0) return clean;
    if (clean.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + clean.substr(2);
    return L"\\\\?\\" + clean;
}

struct FlatFileIdSet {
    std::vector<uint64_t> table;
    size_t mask = 0;

    void init(size_t expectedCount) {
        size_t cap = 1024;
        while (cap < expectedCount * 2) cap <<= 1;
        table.assign(cap, 0ULL);
        mask = cap - 1;
    }

    // Returns true if newly inserted, false if already present
    inline bool insert(uint64_t id) {
        if (id == 0) return true;
        uint64_t h = id * 11400714819323198485ULL;
        size_t idx = static_cast<size_t>((h ^ (h >> 32)) & mask);
        while (true) {
            uint64_t cur = table[idx];
            if (cur == 0) {
                table[idx] = id;
                return true;
            }
            if (cur == id) return false;
            idx = (idx + 1) & mask;
        }
    }
};

// Native NtQueryDirectoryFile(FileIdFullDirectoryInformation = 38) — skips 8.3 short-name overhead
struct IO_STATUS_BLOCK_LOCAL {
    union {
        LONG Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
};

struct FILE_ID_FULL_DIR_INFORMATION_LOCAL {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
    ULONG EaSize;
    LARGE_INTEGER FileId;
    WCHAR FileName[1];
};

struct UNICODE_STRING_LOCAL {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};

struct OBJECT_ATTRIBUTES_LOCAL {
    ULONG Length;
    HANDLE RootDirectory;
    UNICODE_STRING_LOCAL* ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
};

using FnNtOpenFile = LONG(NTAPI*)(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    OBJECT_ATTRIBUTES_LOCAL* ObjectAttributes,
    IO_STATUS_BLOCK_LOCAL* IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions);

using FnNtQueryDirectoryFile = LONG(NTAPI*)(
    HANDLE FileHandle,
    HANDLE Event,
    PVOID ApcRoutine,
    PVOID ApcContext,
    IO_STATUS_BLOCK_LOCAL* IoStatusBlock,
    PVOID FileInformation,
    ULONG Length,
    ULONG FileInformationClass,
    BOOLEAN ReturnSingleEntry,
    PVOID FileName,
    BOOLEAN RestartScan);

using FnNtClose = LONG(NTAPI*)(HANDLE Handle);

inline FnNtOpenFile GetNtOpenFile() {
    static FnNtOpenFile fn = reinterpret_cast<FnNtOpenFile>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtOpenFile"));
    return fn;
}

inline FnNtQueryDirectoryFile GetNtQueryDir() {
    static FnNtQueryDirectoryFile fn = reinterpret_cast<FnNtQueryDirectoryFile>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile"));
    return fn;
}

inline FnNtClose GetNtClose() {
    static FnNtClose fn = reinterpret_cast<FnNtClose>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtClose"));
    return fn;
}

inline std::wstring MakeNtDirPath(const std::wstring& dirPath) {
    std::wstring clean = dirPath;
    while (clean.size() > 3 && clean.back() == L'\\') clean.pop_back();
    if (clean.rfind(L"\\??\\", 0) == 0) return clean;
    if (clean.rfind(L"\\\\?\\", 0) == 0) return L"\\??\\" + clean.substr(4);
    if (clean.rfind(L"\\\\", 0) == 0) return L"\\??\\UNC\\" + clean.substr(2);
    return L"\\??\\" + clean;
}

inline bool NeedsHardLinkDedup(const std::wstring& dirPath) {
    return ContainsCaseInsensitive(dirPath, L"winsxs") ||
           ContainsCaseInsensitive(dirPath, L"system32");
}

struct DirEntryRaw {
    uint32_t parent_dir_id;
    uint32_t self_dir_id; // valid when (flags & FLAG_IS_DIR) != 0
    uint32_t name_off;
    uint16_t name_len;
    uint64_t size;
    uint64_t allocated;
    int64_t  modified;
    uint64_t file_id;
    uint8_t  flags;
};

struct WorkerWin32Out {
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

    std::mutex qMtx;
    std::condition_variable qCv;
    std::deque<std::pair<uint32_t, std::wstring>> dirQueue;
    dirQueue.emplace_back(0u, MakeNtDirPath(cleanRoot));
    size_t activeWorkers = 0;
    bool allDone = false;
    std::atomic<uint32_t> nextDirId{ 1 };

    const unsigned hw = std::max(4u, std::thread::hardware_concurrency());
    const unsigned numThreads = std::min(32u, std::max(16u, hw * 2));
    std::vector<WorkerWin32Out> workerOuts(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    FnNtOpenFile ntOpenFile = GetNtOpenFile();
    FnNtQueryDirectoryFile ntQueryDir = GetNtQueryDir();
    FnNtClose ntClose = GetNtClose();

    for (unsigned t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            auto& out = workerOuts[t];
            out.entries.reserve(32768);
            out.names.reserve(32768 * 16);
            std::vector<uint8_t> dirBuf(65536);
            std::vector<std::pair<uint32_t, std::wstring>> batch;
            batch.reserve(16);
            std::vector<std::pair<uint32_t, std::wstring>> found;
            found.reserve(256);

            while (true) {
                batch.clear();
                {
                    std::unique_lock<std::mutex> lk(qMtx);
                    qCv.wait(lk, [&]() { return !dirQueue.empty() || allDone || progress.is_cancelled(); });
                    if ((dirQueue.empty() && allDone) || progress.is_cancelled()) break;
                    size_t take = std::min<size_t>(16, (dirQueue.size() + numThreads - 1) / numThreads);
                    if (take == 0) take = 1;
                    for (size_t k = 0; k < take && !dirQueue.empty(); ++k) {
                        batch.push_back(std::move(dirQueue.back()));
                        dirQueue.pop_back();
                    }
                    ++activeWorkers;
                }

                found.clear();
                uint64_t lf = 0, ld = 0, lb = 0;

                for (auto& task : batch) {
                    if (progress.is_cancelled()) break;
                    uint32_t parentDirId = task.first;
                    std::wstring& ntPath = task.second;
                    std::wstring base = ntPath;
                    while (base.size() > 7 && base.back() == L'\\') base.pop_back();

                    bool enumOk = false;
                    if (ntOpenFile && ntQueryDir && ntClose) {
                        UNICODE_STRING_LOCAL u{};
                        u.Length = static_cast<USHORT>(ntPath.size() * sizeof(wchar_t));
                        u.MaximumLength = u.Length;
                        u.Buffer = ntPath.data();
                        OBJECT_ATTRIBUTES_LOCAL oa{};
                        oa.Length = sizeof(oa);
                        oa.ObjectName = &u;
                        oa.Attributes = 0x40; // OBJ_CASE_INSENSITIVE
                        IO_STATUS_BLOCK_LOCAL iosb{};
                        HANDLE hDir = nullptr;
                        if (ntOpenFile(&hDir, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            0x00004000 | 0x00000020 | 0x00000001 /*BACKUP_INTENT | SYNC | DIRECTORY*/) >= 0 && hDir) {
                            enumOk = true;
                            LONG st = ntQueryDir(hDir, nullptr, nullptr, nullptr, &iosb,
                                dirBuf.data(), static_cast<ULONG>(dirBuf.size()),
                                38 /*FileIdFullDirectoryInformation*/, FALSE, nullptr, TRUE);
                            while (st >= 0 && !progress.is_cancelled()) {
                                size_t off = 0;
                                while (true) {
                                    auto* info = reinterpret_cast<const FILE_ID_FULL_DIR_INFORMATION_LOCAL*>(dirBuf.data() + off);
                                    size_t len = info->FileNameLength / sizeof(wchar_t);
                                    const wchar_t* fn = info->FileName;
                                    bool isDot = (len == 1 && fn[0] == L'.') || (len == 2 && fn[0] == L'.' && fn[1] == L'.');
                                    if (!isDot && len > 0) {
                                        DWORD attrs = info->FileAttributes;
                                        bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                                        bool isReparse = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                                        if (!(isDir && isReparse)) {
                                            uint8_t flags = 0;
                                            uint32_t selfDirId = 0;
                                            if (isDir) {
                                                flags |= FLAG_IS_DIR;
                                                ++ld;
                                                selfDirId = nextDirId.fetch_add(1, std::memory_order_relaxed);
                                            }
                                            if (attrs & FILE_ATTRIBUTE_HIDDEN) flags |= FLAG_IS_HIDDEN;
                                            if (attrs & FILE_ATTRIBUTE_SYSTEM) flags |= FLAG_IS_SYSTEM;
                                            if (attrs & FILE_ATTRIBUTE_COMPRESSED) flags |= FLAG_IS_COMPRESSED;
                                            if (IsCloudPlaceholderAttrs(attrs)) flags |= FLAG_IS_CLOUD;

                                            uint64_t sz = isDir ? 0 : static_cast<uint64_t>(std::max<LONGLONG>(0, info->EndOfFile.QuadPart));
                                            uint64_t alloc = (!isDir && sz > 0 && !(attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))
                                                ? static_cast<uint64_t>(std::max<LONGLONG>(0, info->AllocationSize.QuadPart)) : 0;
                                            int64_t mod = info->LastWriteTime.QuadPart;
                                            uint64_t fid = static_cast<uint64_t>(info->FileId.QuadPart);

                                            if (!isDir) { ++lf; lb += sz; }
                                            uint32_t noff = static_cast<uint32_t>(out.names.size());
                                            uint16_t nlen = static_cast<uint16_t>(std::min<size_t>(len, 65535));
                                            out.names.insert(out.names.end(), fn, fn + nlen);
                                            out.entries.push_back(DirEntryRaw{ parentDirId, selfDirId, noff, nlen, sz, alloc, mod, fid, flags });

                                            if (isDir) {
                                                std::wstring sub;
                                                sub.reserve(base.size() + 1 + nlen);
                                                sub.append(base);
                                                sub.push_back(L'\\');
                                                sub.append(fn, nlen);
                                                found.emplace_back(selfDirId, std::move(sub));
                                            }
                                        }
                                    }
                                    if (info->NextEntryOffset == 0) break;
                                    off += info->NextEntryOffset;
                                }
                                st = ntQueryDir(hDir, nullptr, nullptr, nullptr, &iosb,
                                    dirBuf.data(), static_cast<ULONG>(dirBuf.size()),
                                    38 /*FileIdFullDirectoryInformation*/, FALSE, nullptr, FALSE);
                            }
                            ntClose(hDir);
                        }
                    }

                    if (!enumOk) {
                        std::wstring winPath;
                        if (ntPath.rfind(L"\\??\\UNC\\", 0) == 0) {
                            winPath = L"\\\\" + ntPath.substr(8);
                        } else if (ntPath.rfind(L"\\??\\", 0) == 0) {
                            winPath = ntPath.substr(4);
                        } else {
                            winPath = ntPath;
                        }
                        std::wstring pattern = MakeExtendedPattern(winPath);
                        WIN32_FIND_DATAW fd{};
                        HANDLE hFind = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                            FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                        if (hFind != INVALID_HANDLE_VALUE) {
                            do {
                                const wchar_t* fn = fd.cFileName;
                                if (fn[0] == L'.' && (fn[1] == L'\0' || (fn[1] == L'.' && fn[2] == L'\0'))) continue;
                                DWORD attrs = fd.dwFileAttributes;
                                bool isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                                bool isReparse = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                                if (isDir && isReparse) continue;
                                size_t len = wcslen(fn);
                                if (len == 0) continue;
                                uint8_t flags = 0;
                                uint32_t selfDirId = 0;
                                if (isDir) {
                                    flags |= FLAG_IS_DIR;
                                    ++ld;
                                    selfDirId = nextDirId.fetch_add(1, std::memory_order_relaxed);
                                }
                                if (attrs & FILE_ATTRIBUTE_HIDDEN) flags |= FLAG_IS_HIDDEN;
                                if (attrs & FILE_ATTRIBUTE_SYSTEM) flags |= FLAG_IS_SYSTEM;
                                if (attrs & FILE_ATTRIBUTE_COMPRESSED) flags |= FLAG_IS_COMPRESSED;
                                if (IsCloudPlaceholderAttrs(attrs)) flags |= FLAG_IS_CLOUD;
                                uint64_t sz = isDir ? 0 : ((static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
                                uint64_t alloc = (!isDir && sz > 0 && !(attrs & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS))
                                    ? (((sz + 4095) / 4096) * 4096) : 0;
                                int64_t mod = (static_cast<int64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
                                if (!isDir) { ++lf; lb += sz; }
                                uint32_t noff = static_cast<uint32_t>(out.names.size());
                                uint16_t nlen = static_cast<uint16_t>(std::min<size_t>(len, 65535));
                                out.names.insert(out.names.end(), fn, fn + nlen);
                                out.entries.push_back(DirEntryRaw{ parentDirId, selfDirId, noff, nlen, sz, alloc, mod, 0, flags });
                                if (isDir) {
                                    std::wstring sub = base + L"\\";
                                    sub.append(fn, nlen);
                                    found.emplace_back(selfDirId, std::move(sub));
                                }
                            } while (FindNextFileW(hFind, &fd));
                            FindClose(hFind);
                        }
                    }
                }

                progress.files.fetch_add(lf, std::memory_order_relaxed);
                progress.folders.fetch_add(ld, std::memory_order_relaxed);
                progress.bytes_processed.fetch_add(lb, std::memory_order_relaxed);

                {
                    std::lock_guard<std::mutex> lk(qMtx);
                    for (auto& sub : found) dirQueue.push_back(std::move(sub));
                    --activeWorkers;
                    if (dirQueue.empty() && activeWorkers == 0) {
                        allDone = true;
                        qCv.notify_all();
                    } else if (!found.empty()) {
                        qCv.notify_all();
                    }
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    if (progress.is_cancelled()) {
        outErr = L"Scan cancelled.";
        return nullptr;
    }

    size_t totalEntries = 0;
    size_t totalNameChars = cleanRoot.size();
    for (const auto& wo : workerOuts) {
        totalEntries += wo.entries.size();
        totalNameChars += wo.names.size();
    }

    const uint32_t maxDirId = nextDirId.load(std::memory_order_relaxed);
    std::vector<uint32_t> dirIdToNodeIdx(maxDirId, NONE_IDX);

    std::vector<TreeNode> nodes;
    nodes.reserve(totalEntries + 1);
    std::vector<wchar_t> names;
    names.reserve(totalNameChars);
    ExtInterner interner;
    FlatFileIdSet seenFileIds;
    seenFileIds.init(totalEntries + 1);

    names.insert(names.end(), cleanRoot.begin(), cleanRoot.end());
    nodes.push_back(TreeNode{
        0, 0, 0, NONE_IDX, NONE_IDX, NONE_IDX, 0, 0,
        0, static_cast<uint16_t>(std::min<size_t>(cleanRoot.size(), 65535)),
        0, FLAG_IS_DIR
    });
    dirIdToNodeIdx[0] = 0;

    for (const auto& wo : workerOuts) {
        size_t baseNameOff = names.size();
        names.insert(names.end(), wo.names.begin(), wo.names.end());
        for (const auto& e : wo.entries) {
            uint32_t idx = static_cast<uint32_t>(nodes.size());
            uint32_t nameOff = static_cast<uint32_t>(baseNameOff + e.name_off);
            bool isDir = (e.flags & FLAG_IS_DIR) != 0;
            if (isDir && e.self_dir_id < maxDirId) {
                dirIdToNodeIdx[e.self_dir_id] = idx;
            }
            uint64_t alloc = e.allocated;
            if (!isDir && e.file_id != 0 && !seenFileIds.insert(e.file_id)) {
                alloc = 0; // Hard link already counted once on disk
            }
            uint16_t extId = isDir ? 0 : interner.intern(std::wstring_view(names.data() + nameOff, e.name_len));
            nodes.push_back(TreeNode{
                e.size, alloc, e.modified,
                e.parent_dir_id, NONE_IDX, NONE_IDX, 0, 0,
                nameOff, e.name_len, extId, e.flags
            });
        }
    }

    for (size_t i = 1; i < nodes.size(); ++i) {
        uint32_t pid = nodes[i].parent;
        nodes[i].parent = (pid < maxDirId) ? dirIdToNodeIdx[pid] : 0;
    }

    return FinalizeTree(std::move(nodes), std::move(names), std::move(interner), 0, cleanRoot, ScanMode::ParallelWin32, startTime, progress, outErr);
}

std::unique_ptr<DiskTree> ExtractSubtreeFromTree(
    std::unique_ptr<DiskTree> srcPtr,
    const std::wstring& cleanTarget,
    ScanProgress& progress,
    std::chrono::steady_clock::time_point startTime,
    std::wstring& outErr)
{
    if (!srcPtr || srcPtr->nodes.empty()) return nullptr;
    const DiskTree& src = *srcPtr;

    std::wstring base = NormalizeScanRoot(src.root_path);
    while (!base.empty() && base.back() == L'\\') base.pop_back();
    std::wstring tgt = cleanTarget;
    while (!tgt.empty() && tgt.back() == L'\\') tgt.pop_back();

    if (tgt.size() < base.size() || _wcsnicmp(tgt.data(), base.data(), base.size()) != 0) {
        return nullptr;
    }
    if (tgt.size() == base.size()) {
        return srcPtr;
    }
    if (tgt[base.size()] != L'\\') return nullptr;

    uint32_t cur = src.root_idx;
    size_t pos = base.size() + 1;
    while (pos < tgt.size()) {
        size_t sep = tgt.find(L'\\', pos);
        if (sep == std::wstring::npos) sep = tgt.size();
        std::wstring_view part(tgt.data() + pos, sep - pos);
        pos = sep + 1;
        if (part.empty()) continue;

        uint32_t next = NONE_IDX;
        uint32_t c = src.nodes[cur].first_child;
        while (c != NONE_IDX && c < src.nodes.size()) {
            if (src.nodes[c].is_dir()) {
                auto nm = src.node_name(c);
                if (nm.size() == part.size() && _wcsnicmp(nm.data(), part.data(), part.size()) == 0) {
                    next = c;
                    break;
                }
            }
            c = src.nodes[c].next_sibling;
        }
        if (next == NONE_IDX) return nullptr;
        cur = next;
    }

    const uint32_t estNodes = std::max<uint32_t>(16u, src.nodes[cur].files + src.nodes[cur].folders + 2u);
    std::vector<TreeNode> nodes;
    nodes.reserve(estNodes);
    std::vector<wchar_t> names;
    names.reserve(estNodes * 16);
    ExtInterner interner;

    names.insert(names.end(), cleanTarget.begin(), cleanTarget.end());
    nodes.push_back(TreeNode{
        0, 0, src.nodes[cur].modified, NONE_IDX, NONE_IDX, NONE_IDX, 0, 0,
        0, static_cast<uint16_t>(std::min<size_t>(cleanTarget.size(), 65535)),
        0, static_cast<uint8_t>(src.nodes[cur].flags | FLAG_IS_DIR)
    });

    struct StackItem {
        uint32_t src_idx;
        uint32_t dst_parent;
    };
    std::vector<StackItem> stack;
    stack.reserve(256);
    uint32_t c = src.nodes[cur].first_child;
    while (c != NONE_IDX && c < src.nodes.size()) {
        stack.push_back(StackItem{ c, 0 });
        c = src.nodes[c].next_sibling;
    }

    uint64_t lf = 0, ld = 0, lb = 0;
    while (!stack.empty()) {
        auto item = stack.back();
        stack.pop_back();
        if (progress.is_cancelled()) {
            outErr = L"Scan cancelled.";
            return nullptr;
        }
        const auto& sn = src.nodes[item.src_idx];
        auto nm = src.node_name(item.src_idx);
        uint32_t nameOff = static_cast<uint32_t>(names.size());
        uint16_t nameLen = static_cast<uint16_t>(std::min<size_t>(nm.size(), 65535));
        names.insert(names.end(), nm.begin(), nm.begin() + nameLen);

        bool isDir = sn.is_dir();
        uint16_t extId = isDir ? 0 : interner.intern(nm);
        uint32_t dstIdx = static_cast<uint32_t>(nodes.size());
        nodes.push_back(TreeNode{
            isDir ? 0 : sn.size,
            isDir ? 0 : sn.allocated,
            sn.modified,
            item.dst_parent,
            NONE_IDX, NONE_IDX, 0, 0,
            nameOff, nameLen, extId, sn.flags
        });

        if (isDir) {
            ++ld;
            uint32_t ch = sn.first_child;
            while (ch != NONE_IDX && ch < src.nodes.size()) {
                stack.push_back(StackItem{ ch, dstIdx });
                ch = src.nodes[ch].next_sibling;
            }
        } else {
            ++lf;
            lb += sn.size;
        }
    }
    progress.files.store(lf, std::memory_order_relaxed);
    progress.folders.store(ld, std::memory_order_relaxed);
    progress.bytes_processed.store(lb, std::memory_order_relaxed);

    return FinalizeTree(std::move(nodes), std::move(names), std::move(interner), 0, cleanTarget, src.scan_mode, startTime, progress, outErr);
}

} // namespace

void InvalidateStdHyperCache(wchar_t driveLetter, const std::wstring& rootPath) {
    if (driveLetter) DeleteFileW(GetStdCachePath(driveLetter).c_str());
    if (!rootPath.empty()) DeleteFileW(GetFolderStdCachePath(rootPath).c_str());
}

ScanResult ScanDriveOrFolder(const std::wstring& targetPath, bool isDrive, wchar_t driveLetter, ScanProgress& progress, bool forceFull) {
    auto startTime = std::chrono::steady_clock::now();
    ScanResult res;

    std::wstring cleanTarget = isDrive ? L"" : NormalizeScanRoot(targetPath);
    if (!isDrive && cleanTarget.size() == 3 && cleanTarget[1] == L':' && cleanTarget[2] == L'\\') {
        isDrive = true;
        driveLetter = static_cast<wchar_t>(towupper(cleanTarget[0]));
    }
    if (!isDrive && cleanTarget.size() >= 3 && cleanTarget[1] == L':' && cleanTarget[2] == L'\\') {
        driveLetter = static_cast<wchar_t>(towupper(cleanTarget[0]));
    }

    if (isDrive) {
        if (!forceFull) {
            res.tree = TrySyncHyperCacheUsn(driveLetter, progress, startTime, res.error);
            if (res.tree || progress.is_cancelled()) return res;
            res.error.clear();
        }
        res.tree = TryScanNtfsMft(driveLetter, progress, startTime, res.error);
        if (res.tree || progress.is_cancelled()) return res;
        res.error.clear();

        if (!forceFull) {
            res.tree = TryLoadStdHyperCache(driveLetter, progress, startTime);
            if (res.tree || progress.is_cancelled()) return res;
        }
        wchar_t root[8];
        swprintf_s(root, L"%c:\\", driveLetter);
        res.tree = ScanParallelWin32(root, progress, startTime, res.error);
        if (res.tree) {
            SaveStdHyperCache(driveLetter, *res.tree);
        }
        return res;
    }

    // Specific folder path: use identical high-speed engines (USN Sync / $MFT / HyperCache Instant -> parallel NtOpenFile)
    if (driveLetter != 0) {
        if (!forceFull) {
            auto driveTree = TrySyncHyperCacheUsn(driveLetter, progress, startTime, res.error);
            if (progress.is_cancelled()) return res;
            if (driveTree) {
                res.tree = ExtractSubtreeFromTree(std::move(driveTree), cleanTarget, progress, startTime, res.error);
                if (res.tree || progress.is_cancelled()) return res;
            }
            res.error.clear();
        }

        // If elevated on local NTFS, full $MFT + USN warms the volume cache and extracts the exact folder subtree in a few seconds
        auto mftTree = TryScanNtfsMft(driveLetter, progress, startTime, res.error);
        if (progress.is_cancelled()) return res;
        if (mftTree) {
            res.tree = ExtractSubtreeFromTree(std::move(mftTree), cleanTarget, progress, startTime, res.error);
            if (res.tree || progress.is_cancelled()) return res;
        }
        res.error.clear();
    }

    // Non-admin (or non-NTFS / network share): folder HyperCache Instant (< 0.2s), else drive HyperCache subtree extraction
    std::wstring fldCachePath = GetFolderStdCachePath(cleanTarget);
    if (!forceFull) {
        res.tree = TryLoadStdCacheFromPath(fldCachePath, cleanTarget, progress, startTime);
        if (res.tree || progress.is_cancelled()) return res;

        if (driveLetter != 0) {
            auto driveStdTree = TryLoadStdHyperCache(driveLetter, progress, startTime);
            if (progress.is_cancelled()) return res;
            if (driveStdTree) {
                res.tree = ExtractSubtreeFromTree(std::move(driveStdTree), cleanTarget, progress, startTime, res.error);
                if (res.tree) {
                    SaveStdCacheToPath(fldCachePath, driveLetter, *res.tree);
                    return res;
                }
            }
        }
    }

    res.tree = ScanParallelWin32(cleanTarget, progress, startTime, res.error);
    if (res.tree) {
        SaveStdCacheToPath(fldCachePath, driveLetter, *res.tree);
    }
    return res;
}
