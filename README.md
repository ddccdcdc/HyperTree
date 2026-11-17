<p align="center">
  <img src="assets/logo.png" alt="HyperTree Logo" width="140" />
</p>

<h1 align="center">HyperTree</h1>

<p align="center">
  <strong>Ultra-fast, native Windows disk space analyzer powered by overlapped NTFS <code>$MFT</code> streaming, live USN Journal + HyperCache instant rescans, a native <code>NtOpenFile</code> + <code>NtQueryDirectoryFile</code> kernel scanner, power filtering, virtualized Win32 lists, and a Direct2D squarified treemap.</strong>
</p>

<p align="center">
  <a href="https://github.com/ddccdcdc/HyperTree/releases/latest/download/hypertree.exe">
    <img src="https://img.shields.io/badge/Download-hypertree.exe%20(Windows%20x64)-10B981?style=for-the-badge&logo=windows&logoColor=white" alt="Download hypertree.exe" />
  </a>
  &nbsp;
  <a href="https://github.com/ddccdcdc/HyperTree/releases/latest">
    <img src="https://img.shields.io/github/v/release/ddccdcdc/HyperTree?style=for-the-badge&color=3B82F6&label=Latest%20Release" alt="Latest Release" />
  </a>
  &nbsp;
  <a href="LICENSE">
    <img src="https://img.shields.io/badge/License-PolyForm%20Noncommercial%201.0.0-6366F1?style=for-the-badge" alt="License: PolyForm Noncommercial 1.0.0" />
  </a>
</p>

---

## Key Highlights

- **Dual-Engine Extreme Scanning & Sub-Second `HyperCache`**
  - **Tier 1 — Overlapped Raw NTFS `$MFT` + `HyperCache + USN Sync` (Administrator Mode)**: Streams `$MFT` extents via a double-buffered overlapped **16 MB** async I/O ring (`FSCTL_GET_NTFS_VOLUME_DATA` + `FSCTL_GET_RETRIEVAL_POINTERS` with `FSCTL_GET_NTFS_FILE_RECORD` fallback) and parses MFT records in parallel across all CPU cores. Repeat scans synchronize only deltas via the live **NTFS USN Change Journal** (`FSCTL_READ_USN_JOURNAL`) for near-instant updates.
  - **Tier 2 — Native `NtOpenFile` + `NtQueryDirectoryFile` + `HyperCache Instant` (Standard / Non-Admin Mode)**: Launches instantly without a UAC prompt (`asInvoker`), scans full cold NTFS/non-NTFS drives and folders using parallel relative-handle `NtOpenFile` + `NtQueryDirectoryFile` (`FileIdFullDirectoryInformation`, class `38`) with `FindFirstFileExW` fallback (`11.49s` on `C:\Windows`; `~68.5s` cold across `4.24M` files + `771K` folders), and reloads on repeat scans via **`HyperCache Instant` in `0.68s`** (auto-invalidated on in-app deletes; hold `Shift` on **Scan** or choose **Filters → Force Full Rescan** to bypass cache).
- **True On-Disk Allocated Space Accuracy**
  - **Hard-Link Deduplication (`WinSxS` / `System32`)**: Deduplicates 64-bit NTFS `FileId`s across both `$MFT` and standard modes with contiguous open-addressed `FlatFileIdSet` hashing so hard-linked system files are never double-counted.
  - **OneDrive Files On-Demand & Sparse Files**: Detects `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS` / `FILE_ATTRIBUTE_RECALL_ON_OPEN` cloud placeholders and sparse/compressed files, weighting the treemap, folder sorting, and `% Parent` bars by **actual allocated disk usage** so cloud-only placeholders (`0 B` allocated) never skew your disk map.
- **Unified Visibility Filters & Power File Search (12 Languages)**
  - **Global `Filters ▾` Menu (Synced across Tree View, File View & Treemap)**: Consistent visibility toggles (**Show System Files**, **Show Hidden Files**, **Show Cloud-Only Files**) plus **Hide Items `< 1 MB` (Tree & Map)** with an active filter count badge (`Filters (N) ▾`) and **Force Full Rescan (Bypass Cache)**.
  - **Localized File View Filters**: Filter instantaneously across all **12 languages** by type (**Videos**, **Images**, **Audio**, **Archives**, **Documents**, **Executables**, **Code**, **Cloud Only**), modification age (**Last 24 Hours**, **Last 7 Days**, **Last 30 Days**, **Older than 1 Year**), and minimum size (`> 1 MB` to `> 1 GB`, including `> 500 MB`) with live matched count and total byte summary.
  - **Power Search Query Syntax**: Supports wildcards & globs (`*setup*.exe`, `log_2?.txt`), OR alternatives (`*.mp4|*.mkv`), multi-extension lists (`ext:mp4;mkv;mov`, `ext:*.zip;*.7z`), exclusions (`-winsxs`, `-*.tmp`, `-ext:txt;dll`), inline size bounds (`>50mb`, `>=100mb`, `<1.5gb`), and `\path\ keyword` matching on an async background thread with zero typing lag.
- **Zero-Jank, Always-Responsive UI**
  - **Virtualized Lists (`LVS_OWNERDATA`)**: Renders millions of rows instantaneously with zero scrolling lag.
  - **Direct2D Squarified Treemap**: Interactive cushion/tile visualization with hover tooltips, click-to-select, right-click actions, and double-click folder zoom.
- **Comprehensive File Management & Built-In Auto-Updater**
  - **Tree View**, **File View**, and **File Types** breakdown with column sorting and **CSV Export**.
  - **Right-Click Context Actions**: **Open**, **Open in Explorer**, **Copy Full Path**, **Properties**, **Move to Recycle Bin**, and **Delete Permanently**.
  - **One-Click Auto-Updater**: Automatically checks GitHub Releases in the background; when a new version is published, an emerald **Upgrade** pill appears on the main interface to download, verify, replace `hypertree.exe`, and relaunch in one click.
- **12 Languages + Full Arabic RTL & Dark/Light Themes**
  - Supports **English**, **العربية (Arabic with full `WS_EX_LAYOUTRTL` & DirectWrite RTL)**, **中文 (简体)**, **Español**, **Français**, **Deutsch**, **Português**, **Русский**, **日本語**, **한국어**, **हिन्दी**, and **Türkçe**.
  - Seamless Windows 10/11 **Dark & Light Mode** across the titlebar, headers, tabs, controls, and treemap.

---

## Quick Start

1. Click the **Download `hypertree.exe`** button above (single standalone executable, ~496 KB, no installer required).
2. Run `hypertree.exe`—it opens immediately in standard mode without requiring Administrator privileges (`asInvoker`).
3. *(Optional)* Click **Elevate (MFT Mode)** in the toolbar at any time to relaunch as Administrator for raw NTFS `$MFT` + live USN Journal scanning (including locked system folders such as `System Volume Information` and `WindowsApps`).
4. Choose a drive or folder (or pass a folder path via CLI: `hypertree.exe "D:\Projects"`) and click **Scan** (`F5`). Hold `Shift` while clicking **Scan** (or use **Filters → Force Full Rescan**) to bypass `HyperCache`.

---

## Building from Source

Requires **Visual Studio 2019 or 2022** with **Desktop development with C++** (x64):

```bat
build.bat
```

This compiles `src\app.rc`, embeds `src\app.manifest`, and produces a standalone `hypertree.exe` with zero third-party runtime dependencies.

---

## License

Licensed under the **PolyForm Noncommercial License 1.0.0** — free for personal, educational, academic, and non-commercial use; commercial use and commercial redistribution are prohibited without prior written permission. See [LICENSE](LICENSE).
