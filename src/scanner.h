#pragma once
#include "model.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>

enum class ScanPhase : uint8_t {
    Idle = 0,
    ReadingMft = 1,
    ParsingRecords = 2,
    WalkingWin32 = 3,
    BuildingTree = 4,
    Sorting = 5,
};

inline const wchar_t* ScanPhaseLabel(ScanPhase p) {
    switch (p) {
        case ScanPhase::ReadingMft:     return L"Reading NTFS $MFT...";
        case ScanPhase::ParsingRecords: return L"Parsing MFT records...";
        case ScanPhase::WalkingWin32:   return L"Scanning folders (Parallel Win32)...";
        case ScanPhase::BuildingTree:   return L"Building directory tree...";
        case ScanPhase::Sorting:        return L"Sorting & indexing...";
        default:                        return L"Idle";
    }
}

struct ScanProgress {
    std::atomic<uint8_t>  phase{ 0 };
    std::atomic<uint64_t> files{ 0 };
    std::atomic<uint64_t> folders{ 0 };
    std::atomic<uint64_t> bytes_processed{ 0 };
    std::atomic<uint64_t> mft_bytes_read{ 0 };
    std::atomic<uint64_t> mft_total_bytes{ 0 };
    std::atomic<bool>     cancel{ false };

    void reset() {
        phase.store(0, std::memory_order_relaxed);
        files.store(0, std::memory_order_relaxed);
        folders.store(0, std::memory_order_relaxed);
        bytes_processed.store(0, std::memory_order_relaxed);
        mft_bytes_read.store(0, std::memory_order_relaxed);
        mft_total_bytes.store(0, std::memory_order_relaxed);
        cancel.store(false, std::memory_order_relaxed);
    }

    inline bool is_cancelled() const { return cancel.load(std::memory_order_relaxed); }
    inline void set_phase(ScanPhase p) { phase.store(static_cast<uint8_t>(p), std::memory_order_relaxed); }
};

struct ScanResult {
    std::unique_ptr<DiskTree> tree;
    std::wstring error;
};

ScanResult ScanDriveOrFolder(const std::wstring& targetPath, bool isDrive, wchar_t driveLetter, ScanProgress& progress);
