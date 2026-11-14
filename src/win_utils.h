#pragma once
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commdlg.h>
#include <string>
#include <vector>

struct DriveInfo {
    wchar_t letter = L'C';
    std::wstring root;
    std::wstring label;
    std::wstring fs_name;
    UINT drive_type = 0;
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;

    uint64_t used_bytes() const {
        return total_bytes > free_bytes ? (total_bytes - free_bytes) : 0;
    }

    const wchar_t* type_label() const {
        switch (drive_type) {
            case DRIVE_REMOVABLE: return L"Removable";
            case DRIVE_FIXED:     return L"Local Disk";
            case DRIVE_REMOTE:    return L"Network";
            case DRIVE_CDROM:     return L"Optical";
            case DRIVE_RAMDISK:   return L"RAM Disk";
            default:              return L"Drive";
        }
    }
};

inline bool EnablePrivilege(const wchar_t* privName) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, privName, &tp.Privileges[0].Luid) != 0 &&
              AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0 &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

inline bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD retLen = 0;
    BOOL ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &retLen);
    CloseHandle(token);
    return ok && elev.TokenIsElevated != 0;
}

inline bool RelaunchElevated() {
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;
    HINSTANCE res = ShellExecuteW(nullptr, L"runas", exePath, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(res) > 32;
}

inline std::vector<DriveInfo> ListDrives() {
    std::vector<DriveInfo> out;
    wchar_t buf[512]{};
    DWORD len = GetLogicalDriveStringsW(511, buf);
    if (len == 0) return out;

    const wchar_t* p = buf;
    while (*p) {
        std::wstring root(p);
        p += root.size() + 1;

        DriveInfo d;
        d.letter = static_cast<wchar_t>(towupper(root[0]));
        d.root = root;
        d.drive_type = GetDriveTypeW(root.c_str());

        wchar_t volName[MAX_PATH + 1]{};
        wchar_t fsName[64]{};
        if (GetVolumeInformationW(root.c_str(), volName, MAX_PATH, nullptr, nullptr, nullptr, fsName, 63)) {
            d.label = volName;
            d.fs_name = fsName;
        }

        ULARGE_INTEGER avail{}, total{}, freeBytes{};
        if (GetDiskFreeSpaceExW(root.c_str(), &avail, &total, &freeBytes)) {
            d.total_bytes = total.QuadPart;
            d.free_bytes = freeBytes.QuadPart;
        }
        out.push_back(std::move(d));
    }
    return out;
}

inline void OpenInExplorer(const std::wstring& path) {
    std::wstring clean = path;
    while (clean.size() > 3 && clean.back() == L'\\') clean.pop_back();
    std::wstring args = L"/select,\"" + clean + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

inline void OpenFileItem(const std::wstring& path) {
    std::wstring dir;
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
        dir = path.substr(0, slash + 1);
    }
    // Use nullptr verb (default shell action) so all file types open properly
    HINSTANCE res = ShellExecuteW(
        nullptr, nullptr, path.c_str(), nullptr,
        dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(res) <= 32) {
        // Fallback: open with / "openas" dialog if no default association exists
        ShellExecuteW(
            nullptr, L"openas", path.c_str(), nullptr,
            dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    }
}

inline void ShowProperties(const std::wstring& path) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = L"properties";
    sei.lpFile = path.c_str();
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

inline bool MoveToRecycleBin(const std::wstring& path) {
    std::wstring from = path;
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    int res = SHFileOperationW(&op);
    return res == 0 && !op.fAnyOperationsAborted;
}

inline bool DeletePermanently(const std::wstring& path) {
    std::wstring from = path;
    from.push_back(L'\0');
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.data();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT;
    int res = SHFileOperationW(&op);
    return res == 0 && !op.fAnyOperationsAborted;
}

inline void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

inline bool PickFolderDialog(HWND owner, std::wstring& outPath) {
    IFileDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd) return false;

    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(L"Select Folder to Scan");

    bool ok = false;
    if (SUCCEEDED(pfd->Show(owner))) {
        IShellItem* psi = nullptr;
        if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
                outPath = pszPath;
                CoTaskMemFree(pszPath);
                ok = true;
            }
            psi->Release();
        }
    }
    pfd->Release();
    return ok;
}

inline bool SaveCsvDialog(HWND owner, std::wstring& outPath) {
    wchar_t fileBuf[MAX_PATH] = L"hypertree_export.csv";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&ofn)) {
        outPath = fileBuf;
        return true;
    }
    return false;
}
