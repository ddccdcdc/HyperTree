<p align="center">
  <img src="assets/logo.png" alt="HyperTree Logo" width="140" />
</p>

<h1 align="center">HyperTree</h1>

<p align="center">
  <strong>Ultra-fast, native Windows disk space analyzer powered by direct NTFS <code>$MFT</code> streaming, a work-stealing <code>NtQueryDirectoryFile</code> kernel scanner, virtualized Win32 lists, and a Direct2D squarified treemap.</strong>
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

- **Dual-Engine Extreme Scanning Speed**
  - **Tier 1 — Direct Raw NTFS `$MFT` Streaming (3–6s)**: When elevated, streams `$MFT` extents sequentially in **16 MB** aligned chunks (`FSCTL_GET_NTFS_VOLUME_DATA` + `FSCTL_GET_RETRIEVAL_POINTERS` with `FSCTL_GET_NTFS_FILE_RECORD` fallback) and parses MFT records in parallel across all CPU cores.
  - **Tier 2 — Work-Stealing `NtQueryDirectoryFile` Engine (Standard / Non-Admin Mode)**: Launches instantly without a UAC prompt (`asInvoker`) and saturates NVMe queue depth using a barrier-free multi-threaded work-stealing pool powered by `NtQueryDirectoryFile` (`FileIdFullDirectoryInformation`, skipping 8.3 short-name overhead) with `FindFirstFileExW` fallback for network shares and non-NTFS volumes.
- **True On-Disk Allocated Space Accuracy**
  - **Hard-Link Deduplication (`WinSxS` / `System32`)**: Deduplicates 64-bit NTFS `FileId`s across both `$MFT` and standard modes so hard-linked system files are never double-counted.
  - **OneDrive Files On-Demand & Sparse Files**: Detects `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS` / `FILE_ATTRIBUTE_RECALL_ON_OPEN` cloud placeholders and sparse/compressed files, weighting the treemap, folder sorting, and `% Parent` bars by **actual allocated disk usage** so cloud-only placeholders (`0 B` allocated) never skew your disk map.
- **Zero-Jank, Always-Responsive UI**
  - **Asynchronous Zero-Lag Search**: Background multi-million-file search thread with atomic cancellation and zero-allocation `std::wstring_view` matching—type at any speed with zero UI delay.
  - **Virtualized Lists (`LVS_OWNERDATA`)**: Renders millions of rows instantaneously with zero scrolling lag.
  - **Direct2D Squarified Treemap**: Interactive cushion/tile visualization with hover tooltips, click-to-select, and double-click folder zoom.
- **Comprehensive File Management & Built-In Auto-Updater**
  - **Tree View**, **File View** (wildcard `*.ext`, substring, and path search + size filters), and **File Types** breakdown with column sorting and **CSV Export**.
  - **System Files Filter**: Hides `FILE_ATTRIBUTE_SYSTEM` items by default, with a toolbar toggle.
  - **Right-Click Context Actions**: **Open**, **Open in Explorer**, **Copy Full Path**, **Properties**, **Move to Recycle Bin**, and **Delete Permanently**.
  - **One-Click Auto-Updater**: Automatically checks GitHub Releases in the background; when a new version is published, an emerald **Upgrade** badge appears on the main interface to download, verify, replace `hypertree.exe`, and relaunch in one click.
- **12 Languages + Full Arabic RTL & Dark/Light Themes**
  - Supports **English**, **العربية (Arabic with full `WS_EX_LAYOUTRTL` & DirectWrite RTL)**, **中文 (简体)**, **Español**, **Français**, **Deutsch**, **Português**, **Русский**, **日本語**, **한국어**, **हिन्दी**, and **Türkçe**.
  - Seamless Windows 10/11 **Dark & Light Mode** across the titlebar, headers, tabs, controls, and treemap.

---

## Quick Start

1. Click the **Download `hypertree.exe`** button above (single standalone executable, ~390 KB, no installer required).
2. Run `hypertree.exe`—it opens immediately in standard mode without requiring Administrator privileges.
3. *(Optional)* Click **Elevate (MFT Mode)** in the toolbar at any time to relaunch as Administrator for 3–6 second raw NTFS `$MFT` scanning.
4. Choose a drive or folder (or pass a folder path via CLI: `hypertree.exe "D:\Projects"`) and click **Scan** (`F5`).

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
