#pragma once

#include "model.h"
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

inline wchar_t FastToLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
    if (c < 128) return c;
    return static_cast<wchar_t>(towlower(c));
}

inline bool ContainsCaseInsensitive(std::wstring_view hay, std::wstring_view lowerNeedle) {
    if (lowerNeedle.empty()) return true;
    if (hay.size() < lowerNeedle.size()) return false;
    const size_t limit = hay.size() - lowerNeedle.size();
    for (size_t i = 0; i <= limit; ++i) {
        size_t j = 0;
        while (j < lowerNeedle.size() && FastToLower(hay[i + j]) == lowerNeedle[j]) ++j;
        if (j == lowerNeedle.size()) return true;
    }
    return false;
}

inline bool EndsWithCaseInsensitive(std::wstring_view hay, std::wstring_view lowerSuffix) {
    if (hay.size() < lowerSuffix.size()) return false;
    size_t off = hay.size() - lowerSuffix.size();
    for (size_t i = 0; i < lowerSuffix.size(); ++i) {
        if (FastToLower(hay[off + i]) != lowerSuffix[i]) return false;
    }
    return true;
}

struct VisibilityFilter {
    bool hide_system = true;
    bool hide_hidden = false;
    bool hide_cloud = false;
    uint64_t min_tree_bytes = 0;

    bool operator==(const VisibilityFilter& o) const noexcept {
        return hide_system == o.hide_system &&
               hide_hidden == o.hide_hidden &&
               hide_cloud == o.hide_cloud &&
               min_tree_bytes == o.min_tree_bytes;
    }
    bool operator!=(const VisibilityFilter& o) const noexcept { return !(*this == o); }

    bool AllowsNode(const TreeNode& n, bool isRoot, bool applyMinTreeSize) const noexcept {
        if (isRoot) return true;
        if (hide_system && (n.flags & FLAG_IS_SYSTEM) != 0) return false;
        if (hide_hidden && (n.flags & FLAG_IS_HIDDEN) != 0) return false;
        if (hide_cloud && (n.flags & FLAG_IS_CLOUD) != 0) return false;
        if (applyMinTreeSize && min_tree_bytes > 0 && n.size < min_tree_bytes) return false;
        return true;
    }
};

enum class FileCategory : int {
    All = 0,
    Videos,
    Images,
    Audio,
    Archives,
    Documents,
    Executables,
    Code,
    CloudOnly,
    Count
};

inline FileCategory ClassifyExtension(std::wstring_view ext) {
    if (!ext.empty() && ext[0] == L'.') ext.remove_prefix(1);
    if (ext.empty()) return FileCategory::All;

    struct ExtEntry {
        const wchar_t* ext;
        FileCategory cat;
    };
    static constexpr ExtEntry kTable[] = {
        { L"mp4",  FileCategory::Videos }, { L"mkv",  FileCategory::Videos },
        { L"avi",  FileCategory::Videos }, { L"mov",  FileCategory::Videos },
        { L"wmv",  FileCategory::Videos }, { L"webm", FileCategory::Videos },
        { L"m4v",  FileCategory::Videos }, { L"flv",  FileCategory::Videos },
        { L"ts",   FileCategory::Videos }, { L"mpg",  FileCategory::Videos },
        { L"mpeg", FileCategory::Videos },

        { L"jpg",  FileCategory::Images }, { L"jpeg", FileCategory::Images },
        { L"png",  FileCategory::Images }, { L"gif",  FileCategory::Images },
        { L"bmp",  FileCategory::Images }, { L"webp", FileCategory::Images },
        { L"svg",  FileCategory::Images }, { L"psd",  FileCategory::Images },
        { L"heic", FileCategory::Images }, { L"ico",  FileCategory::Images },
        { L"tif",  FileCategory::Images }, { L"tiff", FileCategory::Images },
        { L"raw",  FileCategory::Images }, { L"dng",  FileCategory::Images },

        { L"mp3",  FileCategory::Audio },  { L"wav",  FileCategory::Audio },
        { L"flac", FileCategory::Audio },  { L"aac",  FileCategory::Audio },
        { L"ogg",  FileCategory::Audio },  { L"m4a",  FileCategory::Audio },
        { L"wma",  FileCategory::Audio },  { L"opus", FileCategory::Audio },

        { L"zip",  FileCategory::Archives }, { L"rar",  FileCategory::Archives },
        { L"7z",   FileCategory::Archives }, { L"tar",  FileCategory::Archives },
        { L"gz",   FileCategory::Archives }, { L"xz",   FileCategory::Archives },
        { L"zst",  FileCategory::Archives }, { L"bz2",  FileCategory::Archives },
        { L"iso",  FileCategory::Archives }, { L"img",  FileCategory::Archives },
        { L"vhd",  FileCategory::Archives }, { L"vhdx", FileCategory::Archives },
        { L"vmdk", FileCategory::Archives },

        { L"pdf",  FileCategory::Documents }, { L"doc",  FileCategory::Documents },
        { L"docx", FileCategory::Documents }, { L"xls",  FileCategory::Documents },
        { L"xlsx", FileCategory::Documents }, { L"ppt",  FileCategory::Documents },
        { L"pptx", FileCategory::Documents }, { L"txt",  FileCategory::Documents },
        { L"md",   FileCategory::Documents }, { L"csv",  FileCategory::Documents },
        { L"rtf",  FileCategory::Documents }, { L"epub", FileCategory::Documents },

        { L"exe",  FileCategory::Executables }, { L"dll",  FileCategory::Executables },
        { L"sys",  FileCategory::Executables }, { L"msi",  FileCategory::Executables },
        { L"cab",  FileCategory::Executables }, { L"ocx",  FileCategory::Executables },
        { L"msix", FileCategory::Executables }, { L"appx", FileCategory::Executables },

        { L"cpp",  FileCategory::Code }, { L"c",    FileCategory::Code },
        { L"h",    FileCategory::Code }, { L"hpp",  FileCategory::Code },
        { L"cc",   FileCategory::Code }, { L"cs",   FileCategory::Code },
        { L"rs",   FileCategory::Code }, { L"py",   FileCategory::Code },
        { L"js",   FileCategory::Code }, { L"jsx",  FileCategory::Code },
        { L"tsx",  FileCategory::Code }, { L"go",   FileCategory::Code },
        { L"java", FileCategory::Code }, { L"json", FileCategory::Code },
        { L"xml",  FileCategory::Code }, { L"yaml", FileCategory::Code },
        { L"yml",  FileCategory::Code }, { L"toml", FileCategory::Code },
        { L"sql",  FileCategory::Code }, { L"pdb",  FileCategory::Code },
        { L"lib",  FileCategory::Code },
    };

    for (const auto& entry : kTable) {
        if (ext == entry.ext) return entry.cat;
    }
    return FileCategory::All;
}

enum class AgeFilter : int {
    Any = 0,
    Last24h,
    Last7d,
    Last30d,
    OlderThan1y,
    Count
};

inline bool AgeMatches(uint64_t modifiedFt, AgeFilter age, uint64_t nowFt) noexcept {
    if (age == AgeFilter::Any) return true;
    if (modifiedFt == 0) return false;
    if (modifiedFt > nowFt) return age != AgeFilter::OlderThan1y;

    constexpr uint64_t kDay100ns = 86400ULL * 10000000ULL;
    const uint64_t diff = nowFt - modifiedFt;
    switch (age) {
        case AgeFilter::Last24h:     return diff <= kDay100ns;
        case AgeFilter::Last7d:      return diff <= 7ULL * kDay100ns;
        case AgeFilter::Last30d:     return diff <= 30ULL * kDay100ns;
        case AgeFilter::OlderThan1y: return diff >= 365ULL * kDay100ns;
        default: return true;
    }
}

struct ParsedSearchQuery {
    struct Term {
        std::wstring text;
        bool is_path = false;
    };

    uint64_t min_size = 0;
    uint64_t max_size = UINT64_MAX;
    std::vector<std::wstring> include_exts;
    std::vector<std::wstring> exclude_exts;
    std::vector<Term> includes;
    std::vector<Term> excludes;

    bool empty() const noexcept {
        return min_size == 0 && max_size == UINT64_MAX &&
               include_exts.empty() && exclude_exts.empty() &&
               includes.empty() && excludes.empty();
    }
};

inline bool TryParseByteSizeLiteral(std::wstring_view s, uint64_t& outBytes) {
    if (s.empty()) return false;
    size_t pos = 0;
    double val = 0.0;
    bool hasDigit = false;
    while (pos < s.size() && s[pos] >= L'0' && s[pos] <= L'9') {
        val = val * 10.0 + static_cast<double>(s[pos] - L'0');
        hasDigit = true;
        ++pos;
    }
    if (pos < s.size() && s[pos] == L'.') {
        ++pos;
        double frac = 0.1;
        while (pos < s.size() && s[pos] >= L'0' && s[pos] <= L'9') {
            val += static_cast<double>(s[pos] - L'0') * frac;
            frac *= 0.1;
            hasDigit = true;
            ++pos;
        }
    }
    if (!hasDigit) return false;

    std::wstring_view unit = s.substr(pos);
    double mult = 1.0;
    if (unit.empty() || unit == L"b") mult = 1.0;
    else if (unit == L"k" || unit == L"kb") mult = 1024.0;
    else if (unit == L"m" || unit == L"mb") mult = 1024.0 * 1024.0;
    else if (unit == L"g" || unit == L"gb") mult = 1024.0 * 1024.0 * 1024.0;
    else if (unit == L"t" || unit == L"tb") mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else return false;

    outBytes = static_cast<uint64_t>(val * mult);
    return true;
}

inline void AppendNormalizedExtList(std::wstring_view rawList, std::vector<std::wstring>& dst) {
    size_t i = 0;
    while (i < rawList.size()) {
        size_t sep = rawList.find_first_of(L";,", i);
        if (sep == std::wstring_view::npos) sep = rawList.size();
        std::wstring_view item = rawList.substr(i, sep - i);
        if (!item.empty()) {
            std::wstring normalized;
            if (item[0] != L'.') normalized.push_back(L'.');
            normalized.append(item);
            dst.push_back(std::move(normalized));
        }
        i = sep + 1;
    }
}

inline ParsedSearchQuery ParseSearchQuery(std::wstring_view lowerQuery) {
    ParsedSearchQuery pq;
    size_t i = 0;
    while (i < lowerQuery.size()) {
        while (i < lowerQuery.size() && (lowerQuery[i] == L' ' || lowerQuery[i] == L'\t')) ++i;
        if (i >= lowerQuery.size()) break;
        size_t end = i;
        while (end < lowerQuery.size() && lowerQuery[end] != L' ' && lowerQuery[end] != L'\t') ++end;

        std::wstring_view tok = lowerQuery.substr(i, end - i);
        i = end;

        bool neg = false;
        if (tok.size() > 1 && tok[0] == L'-') {
            neg = true;
            tok.remove_prefix(1);
        }

        if (!neg && tok.size() > 1 && (tok[0] == L'>' || tok[0] == L'<')) {
            uint64_t bytes = 0;
            if (TryParseByteSizeLiteral(tok.substr(1), bytes)) {
                if (tok[0] == L'>') pq.min_size = std::max(pq.min_size, bytes);
                else pq.max_size = std::min(pq.max_size, bytes);
                continue;
            }
        }

        if (tok.size() > 4 && tok.rfind(L"ext:", 0) == 0) {
            AppendNormalizedExtList(tok.substr(4), neg ? pq.exclude_exts : pq.include_exts);
            continue;
        }

        if (tok.size() > 2 && tok[0] == L'*' && tok[1] == L'.' && tok.find(L'*', 2) == std::wstring_view::npos) {
            (neg ? pq.exclude_exts : pq.include_exts).emplace_back(tok.substr(1));
            continue;
        }

        ParsedSearchQuery::Term term;
        term.is_path = (tok.find(L'\\') != std::wstring_view::npos || tok.find(L'/') != std::wstring_view::npos);
        term.text.assign(tok);
        (neg ? pq.excludes : pq.includes).push_back(std::move(term));
    }
    return pq;
}

inline bool MatchesSearchQuery(const DiskTree& tree, uint32_t idx, const ParsedSearchQuery& pq) {
    if (pq.empty()) return true;
    const auto& n = tree.nodes[idx];
    if (n.size < pq.min_size || n.size > pq.max_size) return false;

    std::wstring_view name = tree.node_name(idx);

    if (!pq.include_exts.empty()) {
        bool anyExt = false;
        for (const auto& ext : pq.include_exts) {
            if (EndsWithCaseInsensitive(name, ext)) {
                anyExt = true;
                break;
            }
        }
        if (!anyExt) return false;
    }
    for (const auto& ext : pq.exclude_exts) {
        if (EndsWithCaseInsensitive(name, ext)) return false;
    }

    bool needFullPath = false;
    for (const auto& t : pq.includes) {
        if (t.is_path) {
            needFullPath = true;
        } else if (!ContainsCaseInsensitive(name, t.text)) {
            return false;
        }
    }

    if (!pq.excludes.empty()) {
        for (const auto& t : pq.excludes) {
            if (!t.is_path && ContainsCaseInsensitive(name, t.text)) return false;
        }
        needFullPath = true;
    }

    if (needFullPath) {
        std::wstring full = tree.full_path(idx);
        for (const auto& t : pq.includes) {
            if (t.is_path && !ContainsCaseInsensitive(full, t.text)) return false;
        }
        for (const auto& t : pq.excludes) {
            if (ContainsCaseInsensitive(full, t.text)) return false;
        }
    }

    return true;
}

inline void SortNodeIndexList(const DiskTree& tree, std::vector<uint32_t>& indices, int col, bool asc) {
    if (indices.size() <= 1) return;
    std::sort(indices.begin(), indices.end(), [&](uint32_t a, uint32_t b) {
        const auto& na = tree.nodes[a];
        const auto& nb = tree.nodes[b];
        int cmp = 0;
        switch (col) {
            case 0: {
                auto sa = tree.node_name(a);
                auto sb = tree.node_name(b);
                cmp = _wcsnicmp(sa.data(), sb.data(), std::min(sa.size(), sb.size()));
                if (cmp == 0 && sa.size() != sb.size()) cmp = (sa.size() < sb.size()) ? -1 : 1;
                break;
            }
            case 1: cmp = (na.size < nb.size) ? -1 : (na.size > nb.size ? 1 : 0); break;
            case 2: cmp = (na.allocated < nb.allocated) ? -1 : (na.allocated > nb.allocated ? 1 : 0); break;
            case 3: cmp = (na.modified < nb.modified) ? -1 : (na.modified > nb.modified ? 1 : 0); break;
        }
        return asc ? (cmp < 0) : (cmp > 0);
    });
}
