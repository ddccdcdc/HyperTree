#pragma once
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdio>

constexpr uint32_t NONE_IDX = 0xFFFFFFFFu;

constexpr uint8_t FLAG_IS_DIR        = 1 << 0;
constexpr uint8_t FLAG_IS_HIDDEN     = 1 << 1;
constexpr uint8_t FLAG_IS_SYSTEM     = 1 << 2;
constexpr uint8_t FLAG_IS_COMPRESSED = 1 << 3;
constexpr uint8_t FLAG_IS_CLOUD      = 1 << 4;

enum class ScanMode : uint8_t {
    NtfsMft = 0,
    ParallelWin32 = 1,
    HyperCacheUsn = 2,
    HyperCacheInstant = 3,
};

inline const wchar_t* ScanModeLabel(ScanMode m) {
    switch (m) {
        case ScanMode::NtfsMft:           return L"Raw NTFS $MFT";
        case ScanMode::HyperCacheUsn:     return L"HyperCache + USN Sync";
        case ScanMode::HyperCacheInstant: return L"HyperCache Instant (Shift+Scan = Full)";
        default:                          return L"Parallel NtOpen";
    }
}

struct TreeNode {
    uint64_t size;
    uint64_t allocated;
    int64_t  modified;
    uint32_t parent;
    uint32_t first_child;
    uint32_t next_sibling;
    uint32_t files;
    uint32_t folders;
    uint32_t name_off;
    uint16_t name_len;
    uint16_t ext_id;
    uint8_t  flags;

    inline bool is_dir() const { return (flags & FLAG_IS_DIR) != 0; }
    inline bool is_cloud() const { return (flags & FLAG_IS_CLOUD) != 0; }
};

struct ExtStat {
    std::wstring ext;
    uint64_t size = 0;
    uint64_t allocated = 0;
    uint64_t count = 0;
    uint32_t color_rgb = 0x788291;
};

struct DiskTree {
    std::vector<TreeNode> nodes;
    std::vector<wchar_t>  names;
    uint32_t root_idx = 0;
    std::wstring root_path;
    std::vector<ExtStat> extensions;
    std::vector<uint32_t> top_files;
    ScanMode scan_mode = ScanMode::ParallelWin32;
    double scan_duration_secs = 0.0;
    uint64_t total_files = 0;
    uint64_t total_folders = 0;
    uint64_t total_size = 0;
    uint64_t total_allocated = 0;

    inline std::wstring_view node_name(uint32_t idx) const {
        const auto& n = nodes[idx];
        return std::wstring_view(names.data() + n.name_off, n.name_len);
    }

    inline uint32_t node_color(uint32_t idx) const {
        const auto& n = nodes[idx];
        if (n.is_dir()) return 0x4682B4;
        if (n.ext_id < extensions.size()) return extensions[n.ext_id].color_rgb;
        return 0x788291;
    }

    std::wstring full_path(uint32_t idx) const {
        if (idx == root_idx || idx >= nodes.size()) return root_path;
        std::vector<std::wstring_view> parts;
        parts.reserve(12);
        uint32_t cur = idx;
        int guard = 0;
        while (cur != NONE_IDX && cur != root_idx && cur < nodes.size() && guard++ < 256) {
            parts.push_back(node_name(cur));
            cur = nodes[cur].parent;
        }
        std::wstring out = root_path;
        while (!out.empty() && out.back() == L'\\') out.pop_back();
        for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
            out.push_back(L'\\');
            out.append(*it);
        }
        if (out.size() == 2 && out[1] == L':') out.push_back(L'\\');
        return out;
    }
};

inline uint32_t ColorForExt(std::wstring_view ext) {
    auto eq = [&](const wchar_t* s) { return ext == s; };
    if (eq(L"mp4") || eq(L"mkv") || eq(L"avi") || eq(L"mov") || eq(L"wmv") || eq(L"webm") || eq(L"m4v") || eq(L"flv"))
        return 0x935ADB;
    if (eq(L"mp3") || eq(L"wav") || eq(L"flac") || eq(L"aac") || eq(L"ogg") || eq(L"m4a") || eq(L"wma"))
        return 0x48A9C5;
    if (eq(L"jpg") || eq(L"jpeg") || eq(L"png") || eq(L"gif") || eq(L"bmp") || eq(L"webp") || eq(L"svg") || eq(L"psd") || eq(L"heic"))
        return 0x44BD76;
    if (eq(L"zip") || eq(L"rar") || eq(L"7z") || eq(L"tar") || eq(L"gz") || eq(L"xz") || eq(L"iso") || eq(L"img") || eq(L"vhd") || eq(L"vhdx"))
        return 0xEB8C34;
    if (eq(L"exe") || eq(L"dll") || eq(L"sys") || eq(L"msi") || eq(L"cab") || eq(L"ocx"))
        return 0xDC4C56;
    if (eq(L"pdf") || eq(L"doc") || eq(L"docx") || eq(L"xls") || eq(L"xlsx") || eq(L"ppt") || eq(L"pptx") || eq(L"txt") || eq(L"md") || eq(L"csv"))
        return 0x3898EC;
    if (eq(L"cpp") || eq(L"c") || eq(L"h") || eq(L"hpp") || eq(L"rs") || eq(L"py") || eq(L"js") || eq(L"ts") || eq(L"json") || eq(L"xml") || eq(L"pdb") || eq(L"lib"))
        return 0xD4AF37;
    if (eq(L"db") || eq(L"sqlite") || eq(L"mdb") || eq(L"mdf") || eq(L"ldf") || eq(L"log") || eq(L"bak"))
        return 0xB46E5A;

    uint32_t h = 2166136261u;
    for (wchar_t c : ext) {
        h ^= static_cast<uint32_t>(c);
        h *= 16777619u;
    }
    uint8_t r = 80 + (h & 0x7F);
    uint8_t g = 80 + ((h >> 8) & 0x7F);
    uint8_t b = 80 + ((h >> 16) & 0x7F);
    return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

struct WStringHash {
    using is_transparent = void;
    size_t operator()(std::wstring_view sv) const noexcept {
        size_t h = 14695981039346656037ULL;
        for (wchar_t c : sv) {
            h ^= static_cast<size_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }
};

class ExtInterner {
public:
    std::vector<ExtStat> stats;
    std::unordered_map<std::wstring, uint16_t, WStringHash, std::equal_to<>> map;

    ExtInterner() {
        stats.reserve(256);
        map.reserve(256);
        stats.push_back(ExtStat{ L"(no ext)", 0, 0, 0, 0x788291 });
        map.emplace(L"", 0);
    }

    uint16_t intern(std::wstring_view name) {
        size_t dot = name.rfind(L'.');
        if (dot == std::wstring_view::npos || dot + 1 >= name.size() || name.size() - dot > 16) {
            return 0;
        }
        size_t extLen = name.size() - dot - 1;
        wchar_t buf[16];
        for (size_t i = 0; i < extLen; ++i) {
            wchar_t c = name[dot + 1 + i];
            if (c >= L'A' && c <= L'Z') c = c - L'A' + L'a';
            buf[i] = c;
        }
        std::wstring_view lower(buf, extLen);
        auto it = map.find(lower);
        if (it != map.end()) return it->second;
        if (stats.size() >= 65534) return 0;
        uint16_t id = static_cast<uint16_t>(stats.size());
        uint32_t col = ColorForExt(lower);
        std::wstring lowerStr(lower);
        stats.push_back(ExtStat{ L"." + lowerStr, 0, 0, 0, col });
        map.emplace(std::move(lowerStr), id);
        return id;
    }
};

inline std::wstring FormatBytes(uint64_t bytes) {
    wchar_t buf[64];
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    constexpr double TB = GB * 1024.0;
    double b = static_cast<double>(bytes);
    if (b >= TB) swprintf_s(buf, L"%.2f TB", b / TB);
    else if (b >= GB) swprintf_s(buf, L"%.2f GB", b / GB);
    else if (b >= MB) swprintf_s(buf, L"%.2f MB", b / MB);
    else if (b >= KB) swprintf_s(buf, L"%.1f KB", b / KB);
    else swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

inline std::wstring FormatCount(uint64_t n) {
    std::wstring s = std::to_wstring(n);
    std::wstring out;
    out.reserve(s.size() + s.size() / 3);
    for (size_t i = 0; i < s.size(); ++i) {
        if (i > 0 && (s.size() - i) % 3 == 0) out.push_back(L',');
        out.push_back(s[i]);
    }
    return out;
}

inline std::wstring FormatFileTime(int64_t ft) {
    if (ft <= 0) return L"";
    FILETIME ftime;
    ftime.dwLowDateTime = static_cast<DWORD>(ft & 0xFFFFFFFFULL);
    ftime.dwHighDateTime = static_cast<DWORD>((ft >> 32) & 0xFFFFFFFFULL);
    FILETIME localFt;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&ftime, &localFt) || !FileTimeToSystemTime(&localFt, &st)) {
        return L"";
    }
    wchar_t buf[32];
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

inline bool ExportTreeCsv(const DiskTree& tree, const std::wstring& path, size_t& outRows) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::string buf;
    buf.reserve(256 * 1024);
    // UTF-8 BOM
    buf.append("\xEF\xBB\xBFPath,Type,Size,Allocated,Files,Folders,Modified\r\n");

    auto flush = [&]() -> bool {
        if (buf.empty()) return true;
        DWORD written = 0;
        bool ok = WriteFile(h, buf.data(), static_cast<DWORD>(buf.size()), &written, nullptr) != 0;
        buf.clear();
        return ok;
    };

    auto appendUtf8 = [&](const std::wstring& ws) {
        if (ws.empty()) return;
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            size_t old = buf.size();
            buf.resize(old + len);
            WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), buf.data() + old, len, nullptr, nullptr);
        }
    };

    outRows = 0;
    std::vector<uint32_t> stack;
    stack.push_back(tree.root_idx);
    while (!stack.empty()) {
        uint32_t idx = stack.back();
        stack.pop_back();
        const auto& n = tree.nodes[idx];
        std::wstring full = tree.full_path(idx);
        buf.push_back('"');
        for (wchar_t c : full) {
            if (c == L'"') {
                buf.append("\"\"");
            } else if (c < 0x80) {
                buf.push_back(static_cast<char>(c));
            } else {
                std::wstring tmp(1, c);
                appendUtf8(tmp);
            }
        }
        buf.push_back('"');
        char line[160];
        std::wstring mod = FormatFileTime(n.modified);
        std::string mod8;
        for (wchar_t c : mod) mod8.push_back(static_cast<char>(c));
        snprintf(line, sizeof(line), ",%s,%llu,%llu,%u,%u,%s\r\n",
            n.is_dir() ? "Folder" : "File",
            static_cast<unsigned long long>(n.size),
            static_cast<unsigned long long>(n.allocated),
            n.files, n.folders, mod8.c_str());
        buf.append(line);
        ++outRows;

        if (buf.size() >= 200 * 1024) {
            if (!flush()) { CloseHandle(h); return false; }
        }

        if (n.is_dir()) {
            std::vector<uint32_t> children;
            uint32_t c = n.first_child;
            while (c != NONE_IDX) {
                children.push_back(c);
                c = tree.nodes[c].next_sibling;
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.push_back(*it);
            }
        }
    }

    bool ok = flush();
    CloseHandle(h);
    return ok;
}
