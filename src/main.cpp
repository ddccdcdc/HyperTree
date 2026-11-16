#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "i18n.h"
#include "model.h"
#include "scanner.h"
#include "treemap.h"
#include "win_utils.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

constexpr UINT WM_SCAN_DONE  = WM_APP + 1;
constexpr UINT WM_TM_SELECT  = WM_APP + 10;
constexpr UINT WM_TM_ZOOM    = WM_APP + 11;
constexpr UINT WM_TM_CONTEXT = WM_APP + 12;

enum ControlId : int {
    IDC_DRIVE_COMBO = 101,
    IDC_BTN_FOLDER  = 102,
    IDC_BTN_SCAN    = 103,
    IDC_BTN_ELEVATE = 104,
    IDC_BTN_EXPORT  = 105,
    IDC_CHK_TREEMAP = 106,
    IDC_BTN_THEME   = 107,
    IDC_PROGRESS    = 108,
    IDC_STATUS_LBL  = 109,
    IDC_TABS        = 110,
    IDC_LIST_TREE   = 111,
    IDC_LIST_FILES  = 112,
    IDC_LIST_TYPES  = 113,
    IDC_EDIT_SEARCH = 114,
    IDC_MINSIZE_CMB = 115,
    IDC_BTN_CLEAR   = 116,
    IDC_BTN_TM_UP   = 117,
    IDC_BTN_TM_ROOT = 118,
    IDC_TM_LBL      = 119,
    IDC_CHK_SYSTEM  = 120,
    IDC_BTN_LANG    = 121,
};

enum ContextMenuId : UINT {
    IDM_OPEN_EXPLORER = 2001,
    IDM_OPEN_FILE     = 2002,
    IDM_ZOOM_TREEMAP  = 2003,
    IDM_COPY_PATH     = 2004,
    IDM_PROPERTIES    = 2005,
    IDM_RECYCLE_BIN   = 2006,
    IDM_DELETE_PERM   = 2007,
    IDM_LANG_BASE     = 3000,
};

struct FlatRow {
    uint32_t node_idx;
    uint16_t depth;
    bool has_children;
    bool is_expanded;
};

struct AppState {
    HINSTANCE hInst = nullptr;
    HWND hwnd = nullptr;
    HFONT uiFont = nullptr;
    HFONT boldFont = nullptr;
    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
    bool darkMode = true;
    bool isAdmin = false;

    std::vector<DriveInfo> drives;
    bool useCustomFolder = false;
    std::wstring customFolder;

    // Controls
    HWND driveCombo = nullptr;
    HWND btnFolder = nullptr;
    HWND btnScan = nullptr;
    HWND btnElevate = nullptr;
    HWND btnExport = nullptr;
    HWND chkTreemap = nullptr;
    HWND chkSystem = nullptr;
    HWND btnLang = nullptr;
    HWND btnTheme = nullptr;
    HWND progressBar = nullptr;
    HWND statusLbl = nullptr;
    HWND tabCtrl = nullptr;
    HWND listTree = nullptr;
    HWND listFiles = nullptr;
    HWND listTypes = nullptr;
    HWND editSearch = nullptr;
    HWND minSizeCombo = nullptr;
    HWND btnClear = nullptr;
    HWND btnTmUp = nullptr;
    HWND btnTmRoot = nullptr;
    HWND tmLbl = nullptr;
    TreemapControl treemap;

    // Scan state
    std::atomic<bool> scanning{ false };
    ScanProgress progress;
    std::chrono::steady_clock::time_point scanStart;
    std::mutex resultMtx;
    ScanResult pendingResult;

    // Data
    std::unique_ptr<DiskTree> tree;
    std::unordered_set<uint32_t> expanded;
    std::vector<FlatRow> flatRows;
    std::vector<uint32_t> filteredFiles;
    std::vector<size_t> sortedTypeIndices;
    uint32_t selectedNode = NONE_IDX;
    uint32_t treemapRoot = 0;
    bool showTreemap = true;
    bool showSystemFiles = false;
    LangId lang = LangId::English;
    int fileSortCol = 1;
    bool fileSortAsc = false;
    int typeSortCol = 2;
    bool typeSortAsc = false;
    int activeTab = 0;
    int treemapHeight = 250;
};

AppState g_app;

// Dynamic uxtheme.dll dark mode helpers (Windows 10 1903+ / Windows 11)
namespace DarkThemeApi {
    enum PreferredAppMode { Default = 0, AllowDark = 1, ForceDark = 2, ForceLight = 3 };
    using FnSetPreferredAppMode = int(WINAPI*)(PreferredAppMode);
    using FnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);
    using FnRefreshImmersiveColorPolicyState = void(WINAPI*)();
    using FnFlushMenuThemes = void(WINAPI*)();

    struct Api {
        FnSetPreferredAppMode setAppMode = nullptr;
        FnAllowDarkModeForWindow allowWnd = nullptr;
        FnRefreshImmersiveColorPolicyState refreshPolicy = nullptr;
        FnFlushMenuThemes flushMenus = nullptr;
        Api() {
            HMODULE h = LoadLibraryW(L"uxtheme.dll");
            if (!h) return;
            setAppMode = reinterpret_cast<FnSetPreferredAppMode>(GetProcAddress(h, MAKEINTRESOURCEA(135)));
            allowWnd = reinterpret_cast<FnAllowDarkModeForWindow>(GetProcAddress(h, MAKEINTRESOURCEA(133)));
            refreshPolicy = reinterpret_cast<FnRefreshImmersiveColorPolicyState>(GetProcAddress(h, MAKEINTRESOURCEA(104)));
            flushMenus = reinterpret_cast<FnFlushMenuThemes>(GetProcAddress(h, MAKEINTRESOURCEA(136)));
        }
    };

    inline Api& Get() {
        static Api api;
        return api;
    }

    inline void SetAppDark(bool dark) {
        auto& a = Get();
        if (a.setAppMode) a.setAppMode(dark ? ForceDark : ForceLight);
        if (a.refreshPolicy) a.refreshPolicy();
        if (a.flushMenus) a.flushMenus();
    }

    inline void AllowWindow(HWND h, bool dark) {
        auto& a = Get();
        if (h && a.allowWnd) a.allowWnd(h, dark ? TRUE : FALSE);
    }
}

LRESULT CALLBACK TabCtrlSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, g_app.bgBrush);
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, g_app.bgBrush);

        int count = TabCtrl_GetItemCount(hwnd);
        int cur = TabCtrl_GetCurSel(hwnd);
        HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, g_app.uiFont));
        SetBkMode(hdc, TRANSPARENT);

        for (int i = 0; i < count; ++i) {
            RECT itemRc{};
            TabCtrl_GetItemRect(hwnd, i, &itemRc);
            bool sel = (i == cur);
            COLORREF tabBg = sel
                ? (g_app.darkMode ? RGB(44, 47, 56) : RGB(255, 255, 255))
                : (g_app.darkMode ? RGB(32, 34, 40) : RGB(232, 235, 240));
            COLORREF tabFg = sel
                ? (g_app.darkMode ? RGB(245, 247, 250) : RGB(18, 20, 24))
                : (g_app.darkMode ? RGB(175, 180, 192) : RGB(75, 80, 90));

            HBRUSH br = CreateSolidBrush(tabBg);
            FillRect(hdc, &itemRc, br);
            DeleteObject(br);

            if (sel) {
                RECT accent{ itemRc.left, itemRc.bottom - 2, itemRc.right, itemRc.bottom };
                HBRUSH accBr = CreateSolidBrush(RGB(56, 152, 236));
                FillRect(hdc, &accent, accBr);
                DeleteObject(accBr);
            }

            wchar_t text[128]{};
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = text;
            ti.cchTextMax = 128;
            TabCtrl_GetItem(hwnd, i, &ti);
            SetTextColor(hdc, tabFg);
            DrawTextW(hdc, text, -1, &itemRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        HPEN sepPen = CreatePen(PS_SOLID, 1, g_app.darkMode ? RGB(52, 55, 64) : RGB(210, 214, 222));
        HGDIOBJ oldPen = SelectObject(hdc, sepPen);
        MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr);
        LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(sepPen);

        SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void ApplyTheme() {
    const auto& L = GetLangStrings(g_app.lang);
    DarkThemeApi::SetAppDark(g_app.darkMode);
    BOOL dark = g_app.darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(g_app.hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    DarkThemeApi::AllowWindow(g_app.hwnd, g_app.darkMode);

    if (g_app.bgBrush) DeleteObject(g_app.bgBrush);
    if (g_app.editBrush) DeleteObject(g_app.editBrush);
    COLORREF bg = g_app.darkMode ? RGB(28, 29, 34) : RGB(244, 245, 248);
    COLORREF listBg = g_app.darkMode ? RGB(28, 29, 34) : RGB(255, 255, 255);
    COLORREF editBg = g_app.darkMode ? RGB(38, 40, 48) : RGB(255, 255, 255);
    COLORREF fg = g_app.darkMode ? RGB(230, 232, 238) : RGB(24, 25, 28);
    g_app.bgBrush = CreateSolidBrush(bg);
    g_app.editBrush = CreateSolidBrush(editBg);

    for (HWND lv : { g_app.listTree, g_app.listFiles, g_app.listTypes }) {
        if (lv) {
            DarkThemeApi::AllowWindow(lv, g_app.darkMode);
            SetWindowTheme(lv, g_app.darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
            ListView_SetBkColor(lv, listBg);
            ListView_SetTextBkColor(lv, listBg);
            ListView_SetTextColor(lv, fg);
            HWND hdr = ListView_GetHeader(lv);
            if (hdr) {
                DarkThemeApi::AllowWindow(hdr, g_app.darkMode);
                SetWindowTheme(hdr, g_app.darkMode ? L"DarkMode_ItemsView" : L"ItemsView", nullptr);
            }
        }
    }
    for (HWND cmb : { g_app.driveCombo, g_app.minSizeCombo }) {
        if (cmb) {
            DarkThemeApi::AllowWindow(cmb, g_app.darkMode);
            SetWindowTheme(cmb, g_app.darkMode ? L"DarkMode_CFD" : L"CFD", nullptr);
            COMBOBOXINFO cbi{ sizeof(cbi) };
            if (GetComboBoxInfo(cmb, &cbi)) {
                DarkThemeApi::AllowWindow(cbi.hwndList, g_app.darkMode);
                SetWindowTheme(cbi.hwndList, g_app.darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
                DarkThemeApi::AllowWindow(cbi.hwndItem, g_app.darkMode);
                SetWindowTheme(cbi.hwndItem, g_app.darkMode ? L"DarkMode_CFD" : L"CFD", nullptr);
            }
        }
    }
    for (HWND c : { g_app.editSearch, g_app.btnFolder, g_app.btnScan, g_app.btnElevate,
                    g_app.btnExport, g_app.btnLang, g_app.btnTheme, g_app.btnClear,
                    g_app.btnTmUp, g_app.btnTmRoot, g_app.chkTreemap, g_app.chkSystem,
                    g_app.progressBar, g_app.statusLbl, g_app.tmLbl, g_app.tabCtrl }) {
        if (c) {
            DarkThemeApi::AllowWindow(c, g_app.darkMode);
            SetWindowTheme(c, g_app.darkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        }
    }

    g_app.treemap.SetDarkMode(g_app.darkMode);
    SetWindowTextW(g_app.btnTheme, g_app.darkMode ? L.btn_light : L.btn_dark);
    RedrawWindow(g_app.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void SetWindowRtl(HWND h, bool rtl) {
    if (!h) return;
    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (rtl) ex |= WS_EX_LAYOUTRTL;
    else ex &= ~static_cast<LONG_PTR>(WS_EX_LAYOUTRTL);
    SetWindowLongPtrW(h, GWL_EXSTYLE, ex);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void UpdateTreemapHeader();

void ApplyLanguage() {
    const auto& L = GetLangStrings(g_app.lang);
    SetWindowTextW(g_app.hwnd, L"HyperTree");
    SetWindowTextW(g_app.btnFolder, L.btn_folder);
    SetWindowTextW(g_app.btnScan, g_app.scanning.load() ? L.btn_cancel : L.btn_scan);
    SetWindowTextW(g_app.btnElevate, g_app.isAdmin ? L.btn_admin_ready : L.btn_elevate);
    SetWindowTextW(g_app.btnExport, L.btn_export);
    SetWindowTextW(g_app.chkTreemap, L.chk_treemap);
    SetWindowTextW(g_app.chkSystem, L.chk_system);
    SetWindowTextW(g_app.btnLang, L.btn_lang);
    SetWindowTextW(g_app.btnTheme, g_app.darkMode ? L.btn_light : L.btn_dark);
    SetWindowTextW(g_app.btnClear, L.btn_clear);
    SetWindowTextW(g_app.btnTmUp, L.btn_up);
    SetWindowTextW(g_app.btnTmRoot, L.btn_root);
    SendMessageW(g_app.editSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L.search_hint));

    if (!g_app.tree && !g_app.scanning.load()) {
        SetWindowTextW(g_app.statusLbl, L.status_ready);
    }

    auto setTab = [&](int idx, const wchar_t* text) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT;
        ti.pszText = const_cast<wchar_t*>(text);
        TabCtrl_SetItem(g_app.tabCtrl, idx, &ti);
    };
    setTab(0, L.tab_tree);
    setTab(1, L.tab_files);
    setTab(2, L.tab_types);

    auto setCol = [](HWND lv, int idx, const wchar_t* title) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT;
        col.pszText = const_cast<wchar_t*>(title);
        ListView_SetColumn(lv, idx, &col);
    };
    setCol(g_app.listTree, 0, L.col_name);
    setCol(g_app.listTree, 1, L.col_pct_parent);
    setCol(g_app.listTree, 2, L.col_size);
    setCol(g_app.listTree, 3, L.col_allocated);
    setCol(g_app.listTree, 4, L.col_modified);
    setCol(g_app.listTree, 5, L.col_files);
    setCol(g_app.listTree, 6, L.col_folders);

    setCol(g_app.listFiles, 0, L.col_name_path);
    setCol(g_app.listFiles, 1, L.col_size);
    setCol(g_app.listFiles, 2, L.col_allocated);
    setCol(g_app.listFiles, 3, L.col_modified);

    setCol(g_app.listTypes, 0, L.col_ext);
    setCol(g_app.listTypes, 1, L.col_pct_drive);
    setCol(g_app.listTypes, 2, L.col_total_size);
    setCol(g_app.listTypes, 3, L.col_allocated);
    setCol(g_app.listTypes, 4, L.col_files);

    // Apply RTL layout direction across main window and controls
    SetWindowRtl(g_app.hwnd, L.rtl);
    for (HWND h : {
        g_app.driveCombo, g_app.btnFolder, g_app.btnScan, g_app.btnElevate,
        g_app.btnExport, g_app.chkTreemap, g_app.chkSystem, g_app.btnLang,
        g_app.btnTheme, g_app.progressBar, g_app.statusLbl, g_app.tabCtrl,
        g_app.listTree, g_app.listFiles, g_app.listTypes, g_app.editSearch,
        g_app.minSizeCombo, g_app.btnClear, g_app.btnTmUp, g_app.btnTmRoot, g_app.tmLbl
    }) {
        SetWindowRtl(h, L.rtl);
    }
    // Propagate WS_EX_LAYOUTRTL to each ListView's child SysHeader32 so header columns align with rows
    for (HWND lv : { g_app.listTree, g_app.listFiles, g_app.listTypes }) {
        if (!lv) continue;
        HWND hdr = ListView_GetHeader(lv);
        if (hdr) SetWindowRtl(hdr, L.rtl);
        int w0 = ListView_GetColumnWidth(lv, 0);
        if (w0 > 0) ListView_SetColumnWidth(lv, 0, w0);
    }
    g_app.treemap.SetRtl(L.rtl);
    UpdateTreemapHeader();
    RedrawWindow(g_app.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void PopulateDrives() {
    g_app.drives = ListDrives();
    SendMessageW(g_app.driveCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& d : g_app.drives) {
        std::wstring lbl = d.label.empty() ? d.type_label() : d.label;
        wchar_t line[256];
        swprintf_s(line, L"%c:\\ [%s]  %s / %s (%s)",
            d.letter, lbl.c_str(),
            FormatBytes(d.used_bytes()).c_str(),
            FormatBytes(d.total_bytes).c_str(),
            d.fs_name.empty() ? L"Unknown" : d.fs_name.c_str());
        SendMessageW(g_app.driveCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line));
    }
    if (!g_app.drives.empty()) {
        SendMessageW(g_app.driveCombo, CB_SETCURSEL, 0, 0);
    }
}

void RebuildFlatRows() {
    g_app.flatRows.clear();
    if (!g_app.tree || g_app.tree->nodes.empty()) {
        ListView_SetItemCountEx(g_app.listTree, 0, LVSICF_NOINVALIDATEALL);
        return;
    }

    std::vector<std::pair<uint32_t, uint16_t>> stack;
    stack.reserve(256);
    stack.emplace_back(g_app.tree->root_idx, 0);

    while (!stack.empty()) {
        auto [idx, depth] = stack.back();
        stack.pop_back();
        const auto& n = g_app.tree->nodes[idx];
        if (!g_app.showSystemFiles && idx != g_app.tree->root_idx && (n.flags & FLAG_IS_SYSTEM) != 0) {
            continue;
        }
        bool hasChildren = n.is_dir() && n.first_child != NONE_IDX;
        bool isExp = hasChildren && g_app.expanded.count(idx) > 0;
        g_app.flatRows.push_back(FlatRow{ idx, depth, hasChildren, isExp });
        if (isExp) {
            std::vector<uint32_t> children;
            uint32_t c = n.first_child;
            while (c != NONE_IDX) {
                if (g_app.showSystemFiles || (g_app.tree->nodes[c].flags & FLAG_IS_SYSTEM) == 0) {
                    children.push_back(c);
                }
                c = g_app.tree->nodes[c].next_sibling;
            }
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                stack.emplace_back(*it, static_cast<uint16_t>(depth + 1));
            }
        }
    }
    ListView_SetItemCountEx(g_app.listTree, static_cast<int>(g_app.flatRows.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(g_app.listTree, nullptr, FALSE);
}

void SortFilteredFiles() {
    if (!g_app.tree || g_app.filteredFiles.size() <= 1) return;
    int col = g_app.fileSortCol;
    bool asc = g_app.fileSortAsc;
    std::sort(g_app.filteredFiles.begin(), g_app.filteredFiles.end(), [&](uint32_t a, uint32_t b) {
        const auto& na = g_app.tree->nodes[a];
        const auto& nb = g_app.tree->nodes[b];
        int cmp = 0;
        if (col == 0) {
            cmp = g_app.tree->node_name(a).compare(g_app.tree->node_name(b));
        } else if (col == 1) {
            cmp = (na.size < nb.size) ? -1 : (na.size > nb.size ? 1 : 0);
        } else if (col == 2) {
            cmp = (na.allocated < nb.allocated) ? -1 : (na.allocated > nb.allocated ? 1 : 0);
        } else if (col == 3) {
            cmp = (na.modified < nb.modified) ? -1 : (na.modified > nb.modified ? 1 : 0);
        }
        return asc ? (cmp < 0) : (cmp > 0);
    });
    InvalidateRect(g_app.listFiles, nullptr, FALSE);
}

void SortFileTypes() {
    if (!g_app.tree) return;
    g_app.sortedTypeIndices.resize(g_app.tree->extensions.size());
    for (size_t i = 0; i < g_app.sortedTypeIndices.size(); ++i) g_app.sortedTypeIndices[i] = i;
    int col = g_app.typeSortCol;
    bool asc = g_app.typeSortAsc;
    std::sort(g_app.sortedTypeIndices.begin(), g_app.sortedTypeIndices.end(), [&](size_t ia, size_t ib) {
        const auto& a = g_app.tree->extensions[ia];
        const auto& b = g_app.tree->extensions[ib];
        int cmp = 0;
        if (col == 0) cmp = a.ext.compare(b.ext);
        else if (col == 1 || col == 2) cmp = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
        else if (col == 3) cmp = (a.allocated < b.allocated) ? -1 : (a.allocated > b.allocated ? 1 : 0);
        else if (col == 4) cmp = (a.count < b.count) ? -1 : (a.count > b.count ? 1 : 0);
        return asc ? (cmp < 0) : (cmp > 0);
    });
    ListView_SetItemCountEx(g_app.listTypes, static_cast<int>(g_app.sortedTypeIndices.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(g_app.listTypes, nullptr, FALSE);
}

inline wchar_t FastToLower(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c - L'A' + L'a');
    if (c < 128) return c;
    return static_cast<wchar_t>(towlower(c));
}

bool ContainsCaseInsensitive(std::wstring_view hay, std::wstring_view lowerNeedle) {
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

bool EndsWithCaseInsensitive(std::wstring_view hay, std::wstring_view lowerSuffix) {
    if (hay.size() < lowerSuffix.size()) return false;
    size_t off = hay.size() - lowerSuffix.size();
    for (size_t i = 0; i < lowerSuffix.size(); ++i) {
        if (FastToLower(hay[off + i]) != lowerSuffix[i]) return false;
    }
    return true;
}

void RebuildFileFilter() {
    g_app.filteredFiles.clear();
    if (!g_app.tree) {
        ListView_SetItemCountEx(g_app.listFiles, 0, 0);
        return;
    }

    wchar_t qBuf[256]{};
    GetWindowTextW(g_app.editSearch, qBuf, 255);
    std::wstring q(qBuf);
    for (auto& c : q) c = FastToLower(c);

    int minSel = static_cast<int>(SendMessageW(g_app.minSizeCombo, CB_GETCURSEL, 0, 0));
    uint64_t minBytes = 0;
    if (minSel == 1) minBytes = 1024ULL * 1024ULL;
    else if (minSel == 2) minBytes = 10ULL * 1024ULL * 1024ULL;
    else if (minSel == 3) minBytes = 100ULL * 1024ULL * 1024ULL;
    else if (minSel == 4) minBytes = 1024ULL * 1024ULL * 1024ULL;

    const size_t cap = 200000;
    std::wstring_view extFilter;
    if (q.size() > 2 && q[0] == L'*' && q[1] == L'.') extFilter = std::wstring_view(q).substr(1);
    const bool pathSearch = (q.find(L'\\') != std::wstring::npos || q.find(L'/') != std::wstring::npos);

    for (uint32_t idx : g_app.tree->top_files) {
        const auto& n = g_app.tree->nodes[idx];
        if (n.size < minBytes) break;
        if (!g_app.showSystemFiles && (n.flags & FLAG_IS_SYSTEM) != 0) continue;
        if (!q.empty()) {
            if (!extFilter.empty()) {
                if (!EndsWithCaseInsensitive(g_app.tree->node_name(idx), extFilter)) continue;
            } else if (!pathSearch) {
                if (!ContainsCaseInsensitive(g_app.tree->node_name(idx), q)) continue;
            } else {
                std::wstring full = g_app.tree->full_path(idx);
                if (!ContainsCaseInsensitive(full, q)) continue;
            }
        }
        g_app.filteredFiles.push_back(idx);
        if (g_app.filteredFiles.size() >= cap) break;
    }
    if (g_app.fileSortCol != 1 || g_app.fileSortAsc) {
        SortFilteredFiles();
    }
    ListView_SetItemCountEx(g_app.listFiles, static_cast<int>(g_app.filteredFiles.size()), LVSICF_NOINVALIDATEALL);
    InvalidateRect(g_app.listFiles, nullptr, FALSE);
}

void UpdateTreemapHeader() {
    const auto& L = GetLangStrings(g_app.lang);
    if (!g_app.tree || g_app.treemapRoot >= g_app.tree->nodes.size()) {
        SetWindowTextW(g_app.tmLbl, L.treemap_lbl);
        EnableWindow(g_app.btnTmUp, FALSE);
        EnableWindow(g_app.btnTmRoot, FALSE);
        return;
    }
    std::wstring path = g_app.tree->full_path(g_app.treemapRoot);
    uint64_t sz = g_app.tree->nodes[g_app.treemapRoot].size;
    wchar_t buf[512];
    swprintf_s(buf, L"%s: %s  (%s)", L.treemap_lbl, path.c_str(), FormatBytes(sz).c_str());
    SetWindowTextW(g_app.tmLbl, buf);

    bool atRoot = (g_app.treemapRoot == g_app.tree->root_idx);
    EnableWindow(g_app.btnTmUp, !atRoot && g_app.tree->nodes[g_app.treemapRoot].parent != NONE_IDX);
    EnableWindow(g_app.btnTmRoot, !atRoot);
}

void SetTreemapRoot(uint32_t rootIdx) {
    if (!g_app.tree || rootIdx >= g_app.tree->nodes.size()) return;
    g_app.treemapRoot = rootIdx;
    g_app.treemap.SetData(g_app.tree.get(), rootIdx, g_app.selectedNode);
    UpdateTreemapHeader();
}

void LayoutControls() {
    RECT rc{};
    GetClientRect(g_app.hwnd, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;
    if (W < 200 || H < 200) return;

    const int pad = 8;
    int y = pad;

    // Row 1: Toolbar
    int x = pad;
    int comboW = std::min(320, std::max(150, W - 760));
    MoveWindow(g_app.driveCombo, x, y, comboW, 240, TRUE); x += comboW + 6;
    MoveWindow(g_app.btnFolder, x, y, 76, 26, TRUE); x += 82;
    MoveWindow(g_app.btnScan, x, y, 86, 26, TRUE); x += 92;
    MoveWindow(g_app.btnElevate, x, y, 108, 26, TRUE); x += 114;
    MoveWindow(g_app.btnExport, x, y, 86, 26, TRUE);

    MoveWindow(g_app.btnTheme, W - pad - 60, y, 60, 26, TRUE);
    MoveWindow(g_app.btnLang, W - pad - 144, y, 78, 26, TRUE);
    MoveWindow(g_app.chkTreemap, W - pad - 226, y + 3, 76, 20, TRUE);
    MoveWindow(g_app.chkSystem, W - pad - 332, y + 3, 100, 20, TRUE);

    y += 32;

    // Row 2: Progress + Status
    MoveWindow(g_app.progressBar, pad, y + 2, 200, 16, TRUE);
    MoveWindow(g_app.statusLbl, pad + 210, y + 2, std::max(100, W - pad * 2 - 210), 18, TRUE);
    y += 24;

    // Split bottom treemap
    int tmTotalH = g_app.showTreemap ? std::min(g_app.treemapHeight, std::max(120, H / 2)) : 0;
    int tabAreaH = std::max(150, H - y - tmTotalH - pad);

    MoveWindow(g_app.tabCtrl, pad, y, W - pad * 2, 26, TRUE);
    int contentY = y + 28;
    int contentH = std::max(100, tabAreaH - 28);

    // Tab 0: Tree ListView
    MoveWindow(g_app.listTree, pad, contentY, W - pad * 2, contentH, TRUE);

    // Tab 1: File Search bar + ListView
    MoveWindow(g_app.editSearch, pad, contentY + 2, 260, 24, TRUE);
    MoveWindow(g_app.btnClear, pad + 266, contentY + 2, 56, 24, TRUE);
    MoveWindow(g_app.minSizeCombo, pad + 328, contentY + 2, 110, 160, TRUE);
    MoveWindow(g_app.listFiles, pad, contentY + 30, W - pad * 2, std::max(60, contentH - 30), TRUE);

    // Tab 2: File Types ListView
    MoveWindow(g_app.listTypes, pad, contentY, W - pad * 2, contentH, TRUE);

    // Treemap area
    if (g_app.showTreemap) {
        int tmY = y + tabAreaH + 4;
        MoveWindow(g_app.btnTmUp, pad, tmY, 44, 22, TRUE);
        MoveWindow(g_app.btnTmRoot, pad + 50, tmY, 52, 22, TRUE);
        MoveWindow(g_app.tmLbl, pad + 110, tmY + 3, std::max(100, W - pad * 2 - 110), 18, TRUE);
        g_app.treemap.Resize(pad, tmY + 26, W - pad * 2, std::max(60, H - (tmY + 26) - pad));
        ShowWindow(g_app.btnTmUp, SW_SHOW);
        ShowWindow(g_app.btnTmRoot, SW_SHOW);
        ShowWindow(g_app.tmLbl, SW_SHOW);
        ShowWindow(g_app.treemap.hwnd, SW_SHOW);
    } else {
        ShowWindow(g_app.btnTmUp, SW_HIDE);
        ShowWindow(g_app.btnTmRoot, SW_HIDE);
        ShowWindow(g_app.tmLbl, SW_HIDE);
        ShowWindow(g_app.treemap.hwnd, SW_HIDE);
    }
}

void SwitchTab(int tab) {
    g_app.activeTab = tab;
    ShowWindow(g_app.listTree, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.editSearch, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.btnClear, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.minSizeCombo, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.listFiles, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_app.listTypes, tab == 2 ? SW_SHOW : SW_HIDE);
}

void StartScan(bool isDrive, wchar_t driveLetter, const std::wstring& folderPath) {
    if (g_app.scanning.load()) return;
    g_app.progress.reset();
    g_app.scanning.store(true);
    g_app.scanStart = std::chrono::steady_clock::now();
    SetWindowTextW(g_app.btnScan, L"Cancel");
    EnableWindow(g_app.btnFolder, FALSE);
    EnableWindow(g_app.btnExport, FALSE);
    SendMessageW(g_app.progressBar, PBM_SETPOS, 0, 0);
    SetTimer(g_app.hwnd, 1, 33, nullptr);

    std::thread([isDrive, driveLetter, folderPath]() {
        ScanResult res = ScanDriveOrFolder(folderPath, isDrive, driveLetter, g_app.progress);
        {
            std::lock_guard<std::mutex> lk(g_app.resultMtx);
            g_app.pendingResult = std::move(res);
        }
        g_app.scanning.store(false);
        PostMessageW(g_app.hwnd, WM_SCAN_DONE, 0, 0);
    }).detach();
}

void ShowNodeContextMenu(uint32_t nodeIdx, int screenX, int screenY) {
    if (!g_app.tree || nodeIdx >= g_app.tree->nodes.size()) return;
    const auto& L = GetLangStrings(g_app.lang);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_OPEN_FILE, L.ctx_open);
    AppendMenuW(menu, MF_STRING, IDM_OPEN_EXPLORER, L.ctx_explorer);
    if (g_app.tree->nodes[nodeIdx].is_dir()) {
        AppendMenuW(menu, MF_STRING, IDM_ZOOM_TREEMAP, L.ctx_zoom);
    }
    AppendMenuW(menu, MF_STRING, IDM_COPY_PATH, L.ctx_copy);
    AppendMenuW(menu, MF_STRING, IDM_PROPERTIES, L.ctx_props);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_RECYCLE_BIN, L.ctx_recycle);
    AppendMenuW(menu, MF_STRING, IDM_DELETE_PERM, L.ctx_delete);

    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, g_app.hwnd, nullptr);
    DestroyMenu(menu);

    std::wstring path = g_app.tree->full_path(nodeIdx);
    switch (cmd) {
        case IDM_OPEN_EXPLORER: OpenInExplorer(path); break;
        case IDM_OPEN_FILE:     OpenFileItem(path); break;
        case IDM_ZOOM_TREEMAP:  SetTreemapRoot(nodeIdx); break;
        case IDM_COPY_PATH:
            CopyTextToClipboard(g_app.hwnd, path);
            SetWindowTextW(g_app.statusLbl, L.msg_copied);
            break;
        case IDM_PROPERTIES:    ShowProperties(path); break;
        case IDM_RECYCLE_BIN: {
            std::wstring msg = std::wstring(L.msg_recycle_body) + path;
            if (MessageBoxW(g_app.hwnd, msg.c_str(), L.msg_recycle_title, MB_YESNO | MB_ICONWARNING) == IDYES) {
                if (MoveToRecycleBin(path)) {
                    g_app.tree->nodes[nodeIdx].size = 0;
                    g_app.tree->nodes[nodeIdx].allocated = 0;
                    g_app.treemap.SetData(g_app.tree.get(), g_app.treemapRoot, g_app.selectedNode);
                    InvalidateRect(g_app.listTree, nullptr, FALSE);
                    InvalidateRect(g_app.listFiles, nullptr, FALSE);
                }
            }
            break;
        }
        case IDM_DELETE_PERM: {
            std::wstring msg = std::wstring(L.msg_delete_body) + path;
            if (MessageBoxW(g_app.hwnd, msg.c_str(), L.msg_delete_title, MB_YESNO | MB_ICONSTOP) == IDYES) {
                if (DeletePermanently(path)) {
                    g_app.tree->nodes[nodeIdx].size = 0;
                    g_app.tree->nodes[nodeIdx].allocated = 0;
                    g_app.treemap.SetData(g_app.tree.get(), g_app.treemapRoot, g_app.selectedNode);
                    InvalidateRect(g_app.listTree, nullptr, FALSE);
                    InvalidateRect(g_app.listFiles, nullptr, FALSE);
                }
            }
            break;
        }
    }
}

LRESULT HandleCustomDrawHeader(HWND hdr, NMCUSTOMDRAW* cd) {
    switch (cd->dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT: {
            int idx = static_cast<int>(cd->dwItemSpec);
            RECT rc = cd->rc;
            COLORREF bg = g_app.darkMode ? RGB(36, 38, 45) : RGB(240, 242, 246);
            COLORREF fg = g_app.darkMode ? RGB(225, 228, 236) : RGB(30, 32, 38);
            COLORREF sep = g_app.darkMode ? RGB(58, 61, 72) : RGB(214, 218, 226);

            HBRUSH hb = CreateSolidBrush(bg);
            FillRect(cd->hdc, &rc, hb);
            DeleteObject(hb);

            HPEN pen = CreatePen(PS_SOLID, 1, sep);
            HGDIOBJ oldPen = SelectObject(cd->hdc, pen);
            MoveToEx(cd->hdc, rc.right - 1, rc.top + 4, nullptr);
            LineTo(cd->hdc, rc.right - 1, rc.bottom - 4);
            MoveToEx(cd->hdc, rc.left, rc.bottom - 1, nullptr);
            LineTo(cd->hdc, rc.right, rc.bottom - 1);
            SelectObject(cd->hdc, oldPen);
            DeleteObject(pen);

            wchar_t text[128]{};
            HDITEMW hdi{};
            hdi.mask = HDI_TEXT | HDI_FORMAT;
            hdi.pszText = text;
            hdi.cchTextMax = 128;
            Header_GetItem(hdr, idx, &hdi);

            UINT fmt = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
            if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER) fmt |= DT_CENTER;
            else if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT) fmt |= DT_RIGHT;
            else fmt |= DT_LEFT;

            RECT tr{ rc.left + 8, rc.top, rc.right - 8, rc.bottom };
            SetBkMode(cd->hdc, TRANSPARENT);
            SetTextColor(cd->hdc, fg);
            SelectObject(cd->hdc, g_app.uiFont);
            DrawTextW(cd->hdc, text, -1, &tr, fmt);
            return CDRF_SKIPDEFAULT;
        }
    }
    return CDRF_DODEFAULT;
}

LRESULT HandleCustomDrawTree(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            return CDRF_NOTIFYSUBITEMDRAW;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
            int rowIdx = static_cast<int>(cd->nmcd.dwItemSpec);
            if (!g_app.tree || rowIdx < 0 || rowIdx >= static_cast<int>(g_app.flatRows.size())) {
                return CDRF_DODEFAULT;
            }
            const FlatRow& row = g_app.flatRows[rowIdx];
            const TreeNode& node = g_app.tree->nodes[row.node_idx];
            bool isSel = (row.node_idx == g_app.selectedNode);
            COLORREF bg = isSel
                ? RGB(42, 92, 158)
                : (g_app.darkMode ? RGB(28, 29, 34) : RGB(255, 255, 255));
            COLORREF fg = isSel
                ? RGB(255, 255, 255)
                : (g_app.darkMode ? RGB(230, 232, 238) : RGB(24, 25, 28));

            RECT rc{};
            ListView_GetSubItemRect(g_app.listTree, rowIdx, cd->iSubItem, LVIR_BOUNDS, &rc);
            if (cd->iSubItem == 0) {
                ListView_GetSubItemRect(g_app.listTree, rowIdx, 0, LVIR_LABEL, &rc);
            }

            HBRUSH hb = CreateSolidBrush(bg);
            FillRect(cd->nmcd.hdc, &rc, hb);
            DeleteObject(hb);

            SetBkMode(cd->nmcd.hdc, TRANSPARENT);
            SetTextColor(cd->nmcd.hdc, fg);
            SelectObject(cd->nmcd.hdc, g_app.uiFont);

            if (cd->iSubItem == 0) {
                int x = rc.left + 4 + static_cast<int>(row.depth) * 16;
                int cy = (rc.top + rc.bottom) / 2;
                if (row.has_children) {
                    const wchar_t* ch = row.is_expanded ? L"[-]" : L"[+]";
                    RECT cr{ x, rc.top, x + 22, rc.bottom };
                    DrawTextW(cd->nmcd.hdc, ch, 3, &cr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }
                x += 22;
                uint32_t col = node.is_dir() ? 0xEBB441 : (node.is_cloud() ? 0x3898EC : g_app.tree->node_color(row.node_idx));
                HBRUSH dotBr = CreateSolidBrush(RGB((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF));
                HGDIOBJ oldBr = SelectObject(cd->nmcd.hdc, dotBr);
                HPEN nullPen = static_cast<HPEN>(GetStockObject(NULL_PEN));
                HGDIOBJ oldPen = SelectObject(cd->nmcd.hdc, nullPen);
                Ellipse(cd->nmcd.hdc, x, cy - 4, x + 9, cy + 5);
                SelectObject(cd->nmcd.hdc, oldBr);
                SelectObject(cd->nmcd.hdc, oldPen);
                DeleteObject(dotBr);

                x += 14;
                RECT tr{ x, rc.top, rc.right - 4, rc.bottom };
                std::wstring_view name = g_app.tree->node_name(row.node_idx);
                DrawTextW(cd->nmcd.hdc, name.data(), static_cast<int>(name.size()), &tr,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                return CDRF_SKIPDEFAULT;
            } else if (cd->iSubItem == 1) {
                uint64_t pAlloc = (node.parent != NONE_IDX) ? g_app.tree->nodes[node.parent].allocated : node.allocated;
                double pct = 0.0;
                if (pAlloc > 0) {
                    pct = std::min(1.0, static_cast<double>(node.allocated) / static_cast<double>(pAlloc));
                } else {
                    uint64_t pSize = (node.parent != NONE_IDX)
                        ? std::max<uint64_t>(1, g_app.tree->nodes[node.parent].size)
                        : std::max<uint64_t>(1, node.size);
                    pct = std::min(1.0, static_cast<double>(node.size) / static_cast<double>(pSize));
                }
                RECT barRc{ rc.left + 6, rc.top + 4, rc.right - 6, rc.bottom - 4 };
                HBRUSH trackBr = CreateSolidBrush(g_app.darkMode ? RGB(52, 54, 62) : RGB(220, 224, 230));
                FillRect(cd->nmcd.hdc, &barRc, trackBr);
                DeleteObject(trackBr);

                RECT fillRc = barRc;
                fillRc.right = barRc.left + static_cast<int>((barRc.right - barRc.left) * pct);
                HBRUSH fillBr = CreateSolidBrush(RGB(56, 152, 236));
                FillRect(cd->nmcd.hdc, &fillRc, fillBr);
                DeleteObject(fillBr);

                wchar_t pctBuf[32];
                swprintf_s(pctBuf, L"%.1f%%", pct * 100.0);
                SetTextColor(cd->nmcd.hdc, g_app.darkMode || pct > 0.45 ? RGB(255, 255, 255) : fg);
                DrawTextW(cd->nmcd.hdc, pctBuf, -1, &barRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return CDRF_SKIPDEFAULT;
            }
            return CDRF_NEWFONT;
        }
    }
    return CDRF_DODEFAULT;
}

void SetupListColumns() {
    auto addCol = [](HWND lv, int idx, const wchar_t* title, int w, int fmt = LVCFMT_LEFT) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = fmt;
        col.cx = w;
        col.pszText = const_cast<wchar_t*>(title);
        ListView_InsertColumn(lv, idx, &col);
    };

    DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER;
    ListView_SetExtendedListViewStyle(g_app.listTree, ex);
    addCol(g_app.listTree, 0, L"Name", 400);
    addCol(g_app.listTree, 1, L"% Parent", 100, LVCFMT_CENTER);
    addCol(g_app.listTree, 2, L"Size", 100, LVCFMT_RIGHT);
    addCol(g_app.listTree, 3, L"Allocated", 100, LVCFMT_RIGHT);
    addCol(g_app.listTree, 4, L"Modified", 130);
    addCol(g_app.listTree, 5, L"Files", 85, LVCFMT_RIGHT);
    addCol(g_app.listTree, 6, L"Folders", 85, LVCFMT_RIGHT);

    ListView_SetExtendedListViewStyle(g_app.listFiles, ex);
    addCol(g_app.listFiles, 0, L"Name & Path", 580);
    addCol(g_app.listFiles, 1, L"Size", 105, LVCFMT_RIGHT);
    addCol(g_app.listFiles, 2, L"Allocated", 105, LVCFMT_RIGHT);
    addCol(g_app.listFiles, 3, L"Modified", 130);

    ListView_SetExtendedListViewStyle(g_app.listTypes, ex);
    addCol(g_app.listTypes, 0, L"Extension", 160);
    addCol(g_app.listTypes, 1, L"% of Drive", 100, LVCFMT_RIGHT);
    addCol(g_app.listTypes, 2, L"Total Size", 120, LVCFMT_RIGHT);
    addCol(g_app.listTypes, 3, L"Allocated", 120, LVCFMT_RIGHT);
    addCol(g_app.listTypes, 4, L"Files", 100, LVCFMT_RIGHT);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_app.hwnd = hwnd;
            g_app.isAdmin = IsElevated();
            g_app.uiFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            g_app.boldFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

            auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int id) -> HWND {
                HWND h = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                    0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_app.hInst, nullptr);
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.uiFont), TRUE);
                return h;
            };

            g_app.driveCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_DRIVE_COMBO);
            g_app.btnFolder  = mk(L"BUTTON", L"Folder...", BS_PUSHBUTTON, IDC_BTN_FOLDER);
            g_app.btnScan    = mk(L"BUTTON", L"Scan", BS_DEFPUSHBUTTON, IDC_BTN_SCAN);
            g_app.btnElevate = mk(L"BUTTON",
                g_app.isAdmin ? L"Admin (MFT Ready)" : L"Elevate (MFT Mode)",
                BS_PUSHBUTTON, IDC_BTN_ELEVATE);
            if (g_app.isAdmin) EnableWindow(g_app.btnElevate, FALSE);
            g_app.btnExport  = mk(L"BUTTON", L"Export CSV", BS_PUSHBUTTON, IDC_BTN_EXPORT);
            EnableWindow(g_app.btnExport, FALSE);
            g_app.chkTreemap = mk(L"BUTTON", L"Treemap", BS_AUTOCHECKBOX, IDC_CHK_TREEMAP);
            SendMessageW(g_app.chkTreemap, BM_SETCHECK, BST_CHECKED, 0);
            g_app.chkSystem  = mk(L"BUTTON", L"System Files", BS_AUTOCHECKBOX, IDC_CHK_SYSTEM);
            g_app.btnLang    = mk(L"BUTTON", L"Language", BS_PUSHBUTTON, IDC_BTN_LANG);
            g_app.btnTheme   = mk(L"BUTTON", L"Light", BS_PUSHBUTTON, IDC_BTN_THEME);

            g_app.progressBar = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS);
            SendMessageW(g_app.progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
            g_app.statusLbl = mk(L"STATIC", L"Select a drive or folder and click Scan.", SS_LEFT | SS_NOPREFIX, IDC_STATUS_LBL);

            g_app.tabCtrl = mk(WC_TABCONTROLW, L"", TCS_TABS, IDC_TABS);
            SetWindowSubclass(g_app.tabCtrl, TabCtrlSubclassProc, 1, 0);
            auto addTab = [&](int idx, const wchar_t* text) {
                TCITEMW ti{};
                ti.mask = TCIF_TEXT;
                ti.pszText = const_cast<wchar_t*>(text);
                TabCtrl_InsertItem(g_app.tabCtrl, idx, &ti);
            };
            addTab(0, L"Tree View");
            addTab(1, L"File View");
            addTab(2, L"File Types");

            DWORD lvStyle = LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER;
            g_app.listTree  = mk(WC_LISTVIEWW, L"", lvStyle, IDC_LIST_TREE);
            g_app.listFiles = mk(WC_LISTVIEWW, L"", lvStyle, IDC_LIST_FILES);
            g_app.listTypes = mk(WC_LISTVIEWW, L"", lvStyle, IDC_LIST_TYPES);
            auto lvSub = [](HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
                if (m == WM_NOTIFY) {
                    auto* nm = reinterpret_cast<NMHDR*>(l);
                    if (nm->code == NM_CUSTOMDRAW && nm->hwndFrom == ListView_GetHeader(h)) {
                        return HandleCustomDrawHeader(nm->hwndFrom, reinterpret_cast<NMCUSTOMDRAW*>(l));
                    }
                }
                return DefSubclassProc(h, m, w, l);
            };
            SetWindowSubclass(g_app.listTree, lvSub, 1, 0);
            SetWindowSubclass(g_app.listFiles, lvSub, 1, 0);
            SetWindowSubclass(g_app.listTypes, lvSub, 1, 0);

            g_app.editSearch   = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(IDC_EDIT_SEARCH), g_app.hInst, nullptr);
            SendMessageW(g_app.editSearch, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.uiFont), TRUE);
            SendMessageW(g_app.editSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
            SendMessageW(g_app.editSearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search files or *.ext..."));
            g_app.btnClear     = mk(L"BUTTON", L"Clear", BS_PUSHBUTTON, IDC_BTN_CLEAR);
            g_app.minSizeCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_MINSIZE_CMB);
            for (const wchar_t* s : { L"All sizes", L"> 1 MB", L"> 10 MB", L"> 100 MB", L"> 1 GB" }) {
                SendMessageW(g_app.minSizeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            SendMessageW(g_app.minSizeCombo, CB_SETCURSEL, 0, 0);

            g_app.btnTmUp   = mk(L"BUTTON", L"Up", BS_PUSHBUTTON, IDC_BTN_TM_UP);
            g_app.btnTmRoot = mk(L"BUTTON", L"Root", BS_PUSHBUTTON, IDC_BTN_TM_ROOT);
            g_app.tmLbl     = mk(L"STATIC", L"Treemap", SS_LEFT | SS_NOPREFIX | SS_PATHELLIPSIS, IDC_TM_LBL);
            g_app.treemap.Create(hwnd, g_app.hInst, 0, 0, 100, 100);

            SetupListColumns();
            PopulateDrives();
            SwitchTab(0);
            ApplyTheme();
            ApplyLanguage();
            LayoutControls();
            return 0;
        }
        case WM_SIZE:
            LayoutControls();
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkColor(hdc, g_app.darkMode ? RGB(28, 29, 34) : RGB(244, 245, 248));
            SetTextColor(hdc, g_app.darkMode ? RGB(230, 232, 238) : RGB(24, 25, 28));
            return reinterpret_cast<LRESULT>(g_app.bgBrush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkColor(hdc, g_app.darkMode ? RGB(38, 40, 48) : RGB(255, 255, 255));
            SetTextColor(hdc, g_app.darkMode ? RGB(230, 232, 238) : RGB(24, 25, 28));
            return reinterpret_cast<LRESULT>(g_app.editBrush);
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, g_app.darkMode ? RGB(230, 232, 238) : RGB(24, 25, 28));
            return reinterpret_cast<LRESULT>(g_app.bgBrush);
        }
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, g_app.bgBrush);
            return 1;
        }
        case WM_SETCURSOR: {
            HWND target = reinterpret_cast<HWND>(wp);
            if (target == g_app.editSearch) {
                SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                return TRUE;
            }
            if (LOWORD(lp) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                return TRUE;
            }
            break;
        }
        case WM_TIMER: {
            if (wp == 2) {
                KillTimer(hwnd, 2);
                RebuildFileFilter();
                return 0;
            }
            if (wp == 1 && g_app.scanning.load()) {
                auto phase = static_cast<ScanPhase>(g_app.progress.phase.load(std::memory_order_relaxed));
                uint64_t files = g_app.progress.files.load(std::memory_order_relaxed);
                uint64_t folders = g_app.progress.folders.load(std::memory_order_relaxed);
                uint64_t bytes = g_app.progress.bytes_processed.load(std::memory_order_relaxed);
                uint64_t mftRead = g_app.progress.mft_bytes_read.load(std::memory_order_relaxed);
                uint64_t mftTot = g_app.progress.mft_total_bytes.load(std::memory_order_relaxed);
                double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_app.scanStart).count();

                int pos = 0;
                if (mftTot > 0 && phase == ScanPhase::ReadingMft) {
                    pos = static_cast<int>((static_cast<double>(mftRead) / static_cast<double>(mftTot)) * 1000.0);
                } else {
                    pos = static_cast<int>(fmod(secs * 400.0, 1000.0));
                }
                SendMessageW(g_app.progressBar, PBM_SETPOS, pos, 0);

                wchar_t msgBuf[256];
                swprintf_s(msgBuf, L"%s  |  %s files, %s folders  |  %s  |  %.2fs",
                    ScanPhaseLabel(phase),
                    FormatCount(files).c_str(),
                    FormatCount(folders).c_str(),
                    FormatBytes(bytes).c_str(),
                    secs);
                SetWindowTextW(g_app.statusLbl, msgBuf);
            }
            return 0;
        }
        case WM_SCAN_DONE: {
            KillTimer(hwnd, 1);
            SetWindowTextW(g_app.btnScan, GetLangStrings(g_app.lang).btn_scan);
            EnableWindow(g_app.btnFolder, TRUE);
            SendMessageW(g_app.progressBar, PBM_SETPOS, 1000, 0);

            ScanResult res;
            {
                std::lock_guard<std::mutex> lk(g_app.resultMtx);
                res = std::move(g_app.pendingResult);
            }
            if (res.tree) {
                g_app.tree = std::move(res.tree);
                g_app.expanded.clear();
                g_app.expanded.insert(g_app.tree->root_idx);
                g_app.selectedNode = g_app.tree->root_idx;
                RebuildFlatRows();
                RebuildFileFilter();
                SortFileTypes();
                SetTreemapRoot(g_app.tree->root_idx);
                EnableWindow(g_app.btnExport, TRUE);

                wchar_t msgBuf[512];
                swprintf_s(msgBuf, L"Mode: %s  |  %s  |  %s files, %s folders  |  Logical: %s  |  Allocated: %s  |  Time: %.3fs",
                    ScanModeLabel(g_app.tree->scan_mode),
                    g_app.tree->root_path.c_str(),
                    FormatCount(g_app.tree->total_files).c_str(),
                    FormatCount(g_app.tree->total_folders).c_str(),
                    FormatBytes(g_app.tree->total_size).c_str(),
                    FormatBytes(g_app.tree->total_allocated).c_str(),
                    g_app.tree->scan_duration_secs);
                SetWindowTextW(g_app.statusLbl, msgBuf);
                PopulateDrives();
            } else {
                SendMessageW(g_app.progressBar, PBM_SETPOS, 0, 0);
                SetWindowTextW(g_app.statusLbl, res.error.empty() ? L"Scan failed." : res.error.c_str());
            }
            return 0;
        }
        case WM_TM_SELECT: {
            g_app.selectedNode = static_cast<uint32_t>(wp);
            InvalidateRect(g_app.listTree, nullptr, FALSE);
            return 0;
        }
        case WM_TM_ZOOM: {
            uint32_t idx = static_cast<uint32_t>(wp);
            if (g_app.tree && idx < g_app.tree->nodes.size() && g_app.tree->nodes[idx].is_dir()) {
                SetTreemapRoot(idx);
            }
            return 0;
        }
        case WM_TM_CONTEXT: {
            uint32_t idx = static_cast<uint32_t>(wp);
            ShowNodeContextMenu(idx, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_BTN_SCAN) {
                if (g_app.scanning.load()) {
                    g_app.progress.cancel.store(true);
                } else if (g_app.useCustomFolder) {
                    StartScan(false, 0, g_app.customFolder);
                } else {
                    int sel = static_cast<int>(SendMessageW(g_app.driveCombo, CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(g_app.drives.size())) {
                        StartScan(true, g_app.drives[sel].letter, g_app.drives[sel].root);
                    }
                }
            } else if (id == IDC_DRIVE_COMBO && code == CBN_SELCHANGE) {
                g_app.useCustomFolder = false;
            } else if (id == IDC_BTN_FOLDER) {
                std::wstring folder;
                if (PickFolderDialog(hwnd, folder)) {
                    g_app.useCustomFolder = true;
                    g_app.customFolder = folder;
                    StartScan(false, 0, folder);
                }
            } else if (id == IDC_BTN_ELEVATE) {
                if (RelaunchElevated()) {
                    PostQuitMessage(0);
                }
            } else if (id == IDC_BTN_EXPORT && g_app.tree) {
                std::wstring path;
                if (SaveCsvDialog(hwnd, path)) {
                    size_t rows = 0;
                    if (ExportTreeCsv(*g_app.tree, path, rows)) {
                        wchar_t msg[128];
                        swprintf_s(msg, L"Exported %s rows to CSV.", FormatCount(rows).c_str());
                        SetWindowTextW(g_app.statusLbl, msg);
                    }
                }
            } else if (id == IDC_CHK_TREEMAP) {
                g_app.showTreemap = (SendMessageW(g_app.chkTreemap, BM_GETCHECK, 0, 0) == BST_CHECKED);
                LayoutControls();
            } else if (id == IDC_CHK_SYSTEM) {
                g_app.showSystemFiles = (SendMessageW(g_app.chkSystem, BM_GETCHECK, 0, 0) == BST_CHECKED);
                g_app.treemap.SetHideSystem(!g_app.showSystemFiles);
                RebuildFlatRows();
                RebuildFileFilter();
            } else if (id == IDC_BTN_LANG) {
                HMENU menu = CreatePopupMenu();
                for (size_t i = 0; i < g_langs.size(); ++i) {
                    UINT flags = MF_STRING | (static_cast<size_t>(g_app.lang) == i ? MF_CHECKED : 0);
                    AppendMenuW(menu, flags, IDM_LANG_BASE + static_cast<UINT>(i), g_langs[i].native_name);
                }
                RECT br{};
                GetWindowRect(g_app.btnLang, &br);
                UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, br.left, br.bottom, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd >= IDM_LANG_BASE && cmd < IDM_LANG_BASE + static_cast<UINT>(g_langs.size())) {
                    g_app.lang = static_cast<LangId>(cmd - IDM_LANG_BASE);
                    SaveLanguage(g_app.lang);
                    ApplyLanguage();
                }
            } else if (id == IDC_BTN_THEME) {
                g_app.darkMode = !g_app.darkMode;
                ApplyTheme();
            } else if (id == IDC_EDIT_SEARCH && code == EN_CHANGE) {
                KillTimer(hwnd, 2);
                SetTimer(hwnd, 2, 120, nullptr);
            } else if (id == IDC_MINSIZE_CMB && code == CBN_SELCHANGE) {
                RebuildFileFilter();
            } else if (id == IDC_BTN_CLEAR) {
                KillTimer(hwnd, 2);
                SetWindowTextW(g_app.editSearch, L"");
                RebuildFileFilter();
            } else if (id == IDC_BTN_TM_UP && g_app.tree) {
                uint32_t p = g_app.tree->nodes[g_app.treemapRoot].parent;
                if (p != NONE_IDX) SetTreemapRoot(p);
            } else if (id == IDC_BTN_TM_ROOT && g_app.tree) {
                SetTreemapRoot(g_app.tree->root_idx);
            }
            return 0;
        }
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->code == NM_CUSTOMDRAW &&
                (nm->hwndFrom == ListView_GetHeader(g_app.listTree) ||
                 nm->hwndFrom == ListView_GetHeader(g_app.listFiles) ||
                 nm->hwndFrom == ListView_GetHeader(g_app.listTypes))) {
                return HandleCustomDrawHeader(nm->hwndFrom, reinterpret_cast<NMCUSTOMDRAW*>(lp));
            }
            if (nm->hwndFrom == g_app.tabCtrl && nm->code == TCN_SELCHANGE) {
                SwitchTab(TabCtrl_GetCurSel(g_app.tabCtrl));
                return 0;
            }
            if (nm->hwndFrom == g_app.listTree) {
                if (nm->code == NM_CUSTOMDRAW) {
                    return HandleCustomDrawTree(reinterpret_cast<NMLVCUSTOMDRAW*>(lp));
                } else if (nm->code == LVN_GETDISPINFOW) {
                    auto* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
                    int r = di->item.iItem;
                    if (g_app.tree && r >= 0 && r < static_cast<int>(g_app.flatRows.size()) && (di->item.mask & LVIF_TEXT)) {
                        const auto& n = g_app.tree->nodes[g_app.flatRows[r].node_idx];
                        static thread_local std::wstring tmp;
                        switch (di->item.iSubItem) {
                            case 2: tmp = FormatBytes(n.size); break;
                            case 3: tmp = FormatBytes(n.allocated); break;
                            case 4: tmp = FormatFileTime(n.modified); break;
                            case 5: tmp = n.is_dir() ? FormatCount(n.files) : L""; break;
                            case 6: tmp = n.is_dir() ? FormatCount(n.folders) : L""; break;
                            default: tmp.clear(); break;
                        }
                        wcsncpy_s(di->item.pszText, di->item.cchTextMax, tmp.c_str(), _TRUNCATE);
                    }
                    return 0;
                } else if (nm->code == NM_CLICK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.flatRows.size())) {
                        const FlatRow& row = g_app.flatRows[ia->iItem];
                        g_app.selectedNode = row.node_idx;
                        g_app.treemap.SetSelected(row.node_idx);
                        int indentX = 4 + static_cast<int>(row.depth) * 16;
                        if (row.has_children && ia->ptAction.x >= indentX && ia->ptAction.x <= indentX + 24) {
                            if (row.is_expanded) g_app.expanded.erase(row.node_idx);
                            else g_app.expanded.insert(row.node_idx);
                            RebuildFlatRows();
                        } else {
                            InvalidateRect(g_app.listTree, nullptr, FALSE);
                            if (!g_app.tree->nodes[row.node_idx].is_dir()) {
                                POINT pt = ia->ptAction;
                                ClientToScreen(g_app.listTree, &pt);
                                ShowNodeContextMenu(row.node_idx, pt.x, pt.y);
                            }
                        }
                    }
                    return 0;
                } else if (nm->code == NM_DBLCLK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.flatRows.size())) {
                        const FlatRow& row = g_app.flatRows[ia->iItem];
                        if (row.has_children) {
                            if (row.is_expanded) g_app.expanded.erase(row.node_idx);
                            else g_app.expanded.insert(row.node_idx);
                            RebuildFlatRows();
                            SetTreemapRoot(row.node_idx);
                        } else {
                            OpenFileItem(g_app.tree->full_path(row.node_idx));
                        }
                    }
                    return 0;
                } else if (nm->code == NM_RCLICK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.flatRows.size())) {
                        POINT pt = ia->ptAction;
                        ClientToScreen(g_app.listTree, &pt);
                        ShowNodeContextMenu(g_app.flatRows[ia->iItem].node_idx, pt.x, pt.y);
                    }
                    return 0;
                }
            } else if (nm->hwndFrom == g_app.listFiles) {
                if (nm->code == LVN_GETDISPINFOW) {
                    auto* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
                    int r = di->item.iItem;
                    if (g_app.tree && r >= 0 && r < static_cast<int>(g_app.filteredFiles.size()) && (di->item.mask & LVIF_TEXT)) {
                        uint32_t idx = g_app.filteredFiles[r];
                        const auto& n = g_app.tree->nodes[idx];
                        static thread_local std::wstring tmp;
                        switch (di->item.iSubItem) {
                            case 0: tmp = g_app.tree->full_path(idx); break;
                            case 1: tmp = FormatBytes(n.size); break;
                            case 2: tmp = FormatBytes(n.allocated); break;
                            case 3: tmp = FormatFileTime(n.modified); break;
                            default: tmp.clear(); break;
                        }
                        wcsncpy_s(di->item.pszText, di->item.cchTextMax, tmp.c_str(), _TRUNCATE);
                    }
                    return 0;
                } else if (nm->code == NM_CLICK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.filteredFiles.size())) {
                        uint32_t idx = g_app.filteredFiles[ia->iItem];
                        g_app.selectedNode = idx;
                        g_app.treemap.SetSelected(idx);
                        POINT pt = ia->ptAction;
                        ClientToScreen(g_app.listFiles, &pt);
                        ShowNodeContextMenu(idx, pt.x, pt.y);
                    }
                    return 0;
                } else if (nm->code == NM_DBLCLK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.filteredFiles.size())) {
                        OpenInExplorer(g_app.tree->full_path(g_app.filteredFiles[ia->iItem]));
                    }
                    return 0;
                } else if (nm->code == LVN_COLUMNCLICK) {
                    auto* lvc = reinterpret_cast<NMLISTVIEW*>(lp);
                    if (g_app.fileSortCol == lvc->iSubItem) g_app.fileSortAsc = !g_app.fileSortAsc;
                    else { g_app.fileSortCol = lvc->iSubItem; g_app.fileSortAsc = (lvc->iSubItem == 0); }
                    SortFilteredFiles();
                    return 0;
                } else if (nm->code == NM_RCLICK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.filteredFiles.size())) {
                        POINT pt = ia->ptAction;
                        ClientToScreen(g_app.listFiles, &pt);
                        ShowNodeContextMenu(g_app.filteredFiles[ia->iItem], pt.x, pt.y);
                    }
                    return 0;
                }
            } else if (nm->hwndFrom == g_app.listTypes) {
                if (nm->code == LVN_GETDISPINFOW) {
                    auto* di = reinterpret_cast<NMLVDISPINFOW*>(lp);
                    int r = di->item.iItem;
                    if (g_app.tree && r >= 0 && r < static_cast<int>(g_app.sortedTypeIndices.size()) && (di->item.mask & LVIF_TEXT)) {
                        const auto& st = g_app.tree->extensions[g_app.sortedTypeIndices[r]];
                        double total = std::max<double>(1.0, static_cast<double>(g_app.tree->total_size));
                        static thread_local std::wstring tmp;
                        wchar_t buf[64];
                        switch (di->item.iSubItem) {
                            case 0: tmp = st.ext; break;
                            case 1: swprintf_s(buf, L"%.2f%%", (st.size / total) * 100.0); tmp = buf; break;
                            case 2: tmp = FormatBytes(st.size); break;
                            case 3: tmp = FormatBytes(st.allocated); break;
                            case 4: tmp = FormatCount(st.count); break;
                            default: tmp.clear(); break;
                        }
                        wcsncpy_s(di->item.pszText, di->item.cchTextMax, tmp.c_str(), _TRUNCATE);
                    }
                    return 0;
                } else if (nm->code == LVN_COLUMNCLICK) {
                    auto* lvc = reinterpret_cast<NMLISTVIEW*>(lp);
                    if (g_app.typeSortCol == lvc->iSubItem) g_app.typeSortAsc = !g_app.typeSortAsc;
                    else { g_app.typeSortCol = lvc->iSubItem; g_app.typeSortAsc = (lvc->iSubItem == 0); }
                    SortFileTypes();
                    return 0;
                } else if (nm->code == NM_DBLCLK) {
                    auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                    if (g_app.tree && ia->iItem >= 0 && ia->iItem < static_cast<int>(g_app.sortedTypeIndices.size())) {
                        const auto& st = g_app.tree->extensions[g_app.sortedTypeIndices[ia->iItem]];
                        if (!st.ext.empty() && st.ext[0] == L'.') {
                            std::wstring pat = L"*" + st.ext;
                            SetWindowTextW(g_app.editSearch, pat.c_str());
                            TabCtrl_SetCurSel(g_app.tabCtrl, 1);
                            SwitchTab(1);
                            RebuildFileFilter();
                        }
                    }
                    return 0;
                }
            }
            break;
        }
        case WM_DESTROY:
            g_app.treemap.Cleanup();
            if (g_app.bgBrush) DeleteObject(g_app.bgBrush);
            if (g_app.editBrush) DeleteObject(g_app.editBrush);
            if (g_app.uiFont) DeleteObject(g_app.uiFont);
            if (g_app.boldFont) DeleteObject(g_app.boldFont);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct LangDlgState {
    HWND listBox = nullptr;
    HFONT font = nullptr;
    LangId chosen = LangId::English;
    bool done = false;
};

LRESULT CALLBACK LangDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<LangDlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            st = static_cast<LangDlgState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            return TRUE;
        }
        case WM_CREATE: {
            st->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            HWND lbl = CreateWindowExW(0, L"STATIC", L"Choose your preferred language / \u0627\u062E\u062A\u0631 \u0627\u0644\u0644\u063A\u0629 / \u9009\u62E9\u8BED\u8A00:",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 14, 340, 22, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(lbl, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);

            st->listBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                16, 42, 340, 240, hwnd, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
            SendMessageW(st->listBox, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            for (const auto& lang : g_langs) {
                SendMessageW(st->listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(lang.native_name));
            }
            SendMessageW(st->listBox, LB_SETCURSEL, 0, 0);

            HWND okBtn = CreateWindowExW(0, L"BUTTON", L"Continue",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 246, 294, 110, 30,
                hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
            SendMessageW(okBtn, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), TRUE);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDOK || (id == 1001 && code == LBN_DBLCLK)) {
                int sel = static_cast<int>(SendMessageW(st->listBox, LB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(g_langs.size())) {
                    st->chosen = static_cast<LangId>(sel);
                }
                st->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st->font) DeleteObject(st->font);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LangId ShowLanguagePickerDialog(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = LangDlgProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"HyperTreeLangDlg";
    RegisterClassExW(&wc);

    LangDlgState st;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 390, h = 376;
    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"HyperTreeLangDlg", L"HyperTree",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw - w) / 2, (sh - h) / 2, w, h,
        nullptr, nullptr, hInst, &st);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return st.chosen;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (cmdLine && cmdLine[0]) {
        std::wstring arg(cmdLine);
        while (!arg.empty() && (arg.front() == L' ' || arg.front() == L'"')) arg.erase(arg.begin());
        while (!arg.empty() && (arg.back() == L' ' || arg.back() == L'"')) arg.pop_back();
        DWORD attr = GetFileAttributesW(arg.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            g_app.useCustomFolder = true;
            g_app.customFolder = arg;
        }
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    if (!LoadSavedLanguage(g_app.lang)) {
        g_app.lang = ShowLanguagePickerDialog(hInst);
        SaveLanguage(g_app.lang);
    }

    g_app.hInst = hInst;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.lpszClassName = L"HyperTreeMainWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"HyperTreeMainWnd", L"HyperTree",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5) {
            SendMessageW(hwnd, WM_COMMAND, IDC_BTN_SCAN, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
