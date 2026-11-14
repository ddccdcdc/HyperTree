<p align="center">
  <img src="assets/logo.png" alt="HyperTree Logo" width="140" />
</p>

<h1 align="center">HyperTree</h1>

<p align="center">
  <strong>Ultra-fast, native Windows disk space analyzer powered by direct NTFS <code>$MFT</code> reading, multi-threaded Win32 enumeration, virtualized UI lists, and a hardware-accelerated Direct2D treemap.</strong>
</p>

<p align="center">
  <a href="https://github.com/ddccdcdc/HyperTree/releases/latest/download/hypertree.exe">
    <img src="https://img.shields.io/badge/Download-hypertree.exe%20(Windows%20x64)-10B981?style=for-the-badge&logo=windows&logoColor=white" alt="Download hypertree.exe" />
  </a>
</p>

---

## Features

- **Extreme Scanning Speed (3–6 Seconds)**
  - **Direct Raw NTFS `$MFT` Reader**: Reads `$MFT` extents sequentially in 8 MB aligned chunks (`FSCTL_GET_NTFS_VOLUME_DATA` + `FSCTL_GET_RETRIEVAL_POINTERS` with `FSCTL_GET_NTFS_FILE_RECORD` fallback) and parses MFT records in parallel across all CPU cores.
  - **Parallel Win32 Fallback**: Automatically uses multi-threaded `FindFirstFileExW` (`FindExInfoBasic` + `FIND_FIRST_EX_LARGE_FETCH`) for specific folders, non-NTFS volumes, or network shares.
- **Zero-Jank, Always-Responsive UI**
  - Lock-free atomic progress reporting (`30 FPS` telemetry) keeps the interface smooth under heavy I/O and CPU load.
  - Native Win32 Virtual `SysListView32` (`LVS_OWNERDATA`) renders millions of entries instantaneously.
  - Hardware-accelerated **Direct2D + DirectWrite Squarified Treemap** with hover tooltips, click-to-select, and double-click zoom.
- **Comprehensive File Management**
  - **Tree View** with inline `% of Parent` usage bars, **File View** with instant wildcard/substring search (`*.mp4`, `node_modules`) and size filters, and **File Types** extension breakdown.
  - **Column Sorting** across File View and File Types.
  - **System Files Filter**: Hides `FILE_ATTRIBUTE_SYSTEM` entries by default, with a **System Files** toggle in the toolbar.
  - **One-Click File Actions**: Click any file (or right-click any item) to **Open**, **Open in Explorer**, **Copy Full Path**, view **Properties**, **Move to Recycle Bin**, or **Delete Permanently**.
  - **CSV Export** of the scanned directory tree.
- **12 Languages + Full RTL Support**
  - First-launch language selector and live toolbar switcher supporting **English**, **العربية (Arabic with full `WS_EX_LAYOUTRTL` & DirectWrite RTL)**, **中文 (简体)**, **Español**, **Français**, **Deutsch**, **Português**, **Русский**, **日本語**, **한국어**, **हिन्दी**, and **Türkçe**.
- **Light & Dark Themes**
  - Full Windows 10/11 Dark & Light mode support across the titlebar, lists, inputs, and Direct2D treemap.

---

## Quick Start

1. Click the **Download `hypertree.exe`** button above.
2. Run `hypertree.exe` (accept the Windows Administrator prompt so it can read the NTFS `$MFT` directly).
3. Select your preferred language on first launch.
4. Choose a drive or folder and click **Scan** (or press `F5`).

---

## Building from Source

Requires **Visual Studio 2019 or 2022** with **Desktop development with C++** (x64):

```bat
build.bat
```

This compiles `src\app.rc`, links `src\app.manifest`, and outputs a standalone `hypertree.exe` (~385 KB, zero external DLL dependencies).

---

## License

Copyright © 2026 HyperTree. All Rights Reserved. See [LICENSE](LICENSE).
