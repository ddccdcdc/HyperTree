#pragma once
#include "model.h"
#include <d2d1.h>
#include <dwrite.h>
#include <vector>

struct TreemapTile {
    uint32_t node_idx;
    D2D1_RECT_F rect;
    uint32_t color_rgb;
    bool is_dir;
};

class TreemapControl {
public:
    HWND hwnd = nullptr;

    bool Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h);
    void Resize(int x, int y, int w, int h);
    void SetData(const DiskTree* tree, uint32_t rootIdx, uint32_t selectedIdx);
    void SetSelected(uint32_t selectedIdx);
    void SetHideSystem(bool hide);
    void SetRtl(bool rtl);
    void SetDarkMode(bool dark);
    void Invalidate();
    void Cleanup();

    uint32_t HitTest(int px, int py) const;
    uint32_t HoveredNode() const { return hovered_idx; }

private:
    const DiskTree* tree = nullptr;
    uint32_t root_idx = NONE_IDX;
    uint32_t selected_idx = NONE_IDX;
    uint32_t hovered_idx = NONE_IDX;
    bool layout_dirty = true;
    bool hide_system = true;
    bool is_rtl = false;
    bool dark_mode = true;
    std::vector<TreemapTile> tiles;

    ID2D1Factory* d2d_factory = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteFactory* dw_factory = nullptr;
    IDWriteTextFormat* text_fmt = nullptr;
    HWND tooltip_hwnd = nullptr;
    uint32_t last_tip_idx = NONE_IDX;

    void EnsureResources();
    void DiscardResources();
    void RebuildLayout();
    void Render();
    void UpdateTooltip(int px, int py);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
