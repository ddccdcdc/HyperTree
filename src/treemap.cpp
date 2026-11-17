#include "treemap.h"
#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

constexpr UINT WM_TM_SELECT  = WM_APP + 10;
constexpr UINT WM_TM_ZOOM    = WM_APP + 11;
constexpr UINT WM_TM_CONTEXT = WM_APP + 12;

namespace {

constexpr size_t MAX_TILES = 3500;
constexpr int MAX_DEPTH = 4;

inline D2D1_COLOR_F ColorFromRgb(uint32_t rgb, float scale = 1.0f) {
    float r = std::min(1.0f, (((rgb >> 16) & 0xFF) / 255.0f) * scale);
    float g = std::min(1.0f, (((rgb >> 8) & 0xFF) / 255.0f) * scale);
    float b = std::min(1.0f, ((rgb & 0xFF) / 255.0f) * scale);
    return D2D1::ColorF(r, g, b, 1.0f);
}

double WorstAspect(const std::vector<std::pair<uint32_t, double>>& row, double shortSide) {
    double sum = 0.0;
    for (const auto& item : row) sum += item.second;
    if (sum <= 0.0 || shortSide <= 0.0) return 1e30;
    double s2 = sum * sum;
    double w2 = shortSide * shortSide;
    double maxR = 0.0;
    for (const auto& item : row) {
        if (item.second <= 0.0) continue;
        double r1 = (w2 * item.second) / s2;
        double r2 = s2 / (w2 * item.second);
        maxR = std::max(maxR, std::max(r1, r2));
    }
    return maxR;
}

D2D1_RECT_F LayoutRow(const std::vector<std::pair<uint32_t, double>>& row, D2D1_RECT_F rect, std::vector<std::pair<uint32_t, D2D1_RECT_F>>& out) {
    double sum = 0.0;
    for (const auto& item : row) sum += item.second;
    if (sum <= 0.0) return rect;

    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (w >= h) {
        float rowW = std::min(w, static_cast<float>(sum / h));
        float y = rect.top;
        for (const auto& item : row) {
            float ih = static_cast<float>((item.second / sum) * h);
            out.push_back({ item.first, D2D1::RectF(rect.left, y, rect.left + rowW, y + ih) });
            y += ih;
        }
        return D2D1::RectF(rect.left + rowW, rect.top, rect.right, rect.bottom);
    } else {
        float rowH = std::min(h, static_cast<float>(sum / w));
        float x = rect.left;
        for (const auto& item : row) {
            float iw = static_cast<float>((item.second / sum) * w);
            out.push_back({ item.first, D2D1::RectF(x, rect.top, x + iw, rect.top + rowH) });
            x += iw;
        }
        return D2D1::RectF(rect.left, rect.top + rowH, rect.right, rect.bottom);
    }
}

void Squarify(const std::vector<std::pair<uint32_t, double>>& items, D2D1_RECT_F rect, std::vector<std::pair<uint32_t, D2D1_RECT_F>>& out) {
    std::vector<std::pair<uint32_t, double>> row;
    row.reserve(16);
    size_t i = 0;
    while (i < items.size()) {
        float w = rect.right - rect.left;
        float h = rect.bottom - rect.top;
        double shortSide = std::min(w, h);
        if (shortSide <= 0.5) break;

        if (row.empty()) {
            row.push_back(items[i++]);
            continue;
        }
        double curWorst = WorstAspect(row, shortSide);
        row.push_back(items[i]);
        double newWorst = WorstAspect(row, shortSide);
        if (newWorst <= curWorst) {
            ++i;
        } else {
            row.pop_back();
            rect = LayoutRow(row, rect, out);
            row.clear();
        }
    }
    if (!row.empty()) {
        LayoutRow(row, rect, out);
    }
}

void LayoutNode(const DiskTree& tree, uint32_t dirIdx, D2D1_RECT_F rect, int depth, const VisibilityFilter& filters, std::vector<TreemapTile>& out) {
    float w = rect.right - rect.left;
    float h = rect.bottom - rect.top;
    if (out.size() >= MAX_TILES || w < 3.0f || h < 3.0f) return;

    std::vector<std::pair<uint32_t, uint64_t>> children;
    uint64_t totalSize = 0;
    bool useAlloc = tree.nodes[dirIdx].allocated > 0;
    uint32_t c = tree.nodes[dirIdx].first_child;
    while (c != NONE_IDX) {
        const auto& cn = tree.nodes[c];
        if (filters.AllowsNode(cn, false, true)) {
            uint64_t sz = useAlloc ? cn.allocated : cn.size;
            if (sz > 0) {
                children.emplace_back(c, sz);
                totalSize += sz;
            }
        }
        c = cn.next_sibling;
    }
    if (children.empty() || totalSize == 0) return;

    double scale = (static_cast<double>(w) * h) / static_cast<double>(totalSize);
    std::vector<std::pair<uint32_t, double>> items;
    items.reserve(children.size());
    for (const auto& [idx, sz] : children) {
        double area = static_cast<double>(sz) * scale;
        if (area < 4.0) break; // sorted descending
        items.emplace_back(idx, area);
    }
    if (items.empty()) return;

    std::vector<std::pair<uint32_t, D2D1_RECT_F>> placed;
    placed.reserve(items.size());
    Squarify(items, rect, placed);

    for (const auto& [cidx, cr] : placed) {
        if (out.size() >= MAX_TILES) break;
        const TreeNode& node = tree.nodes[cidx];
        float cw = cr.right - cr.left;
        float ch = cr.bottom - cr.top;
        if (node.is_dir() && depth < MAX_DEPTH && cw >= 10.0f && ch >= 10.0f) {
            size_t before = out.size();
            D2D1_RECT_F inner = D2D1::RectF(cr.left + 1.5f, cr.top + 1.5f, cr.right - 1.5f, cr.bottom - 1.5f);
            LayoutNode(tree, cidx, inner, depth + 1, filters, out);
            if (out.size() == before) {
                out.push_back(TreemapTile{ cidx, cr, 0x4476A8, true });
            }
        } else {
            uint32_t col = node.is_dir() ? 0x4476A8 : tree.node_color(cidx);
            out.push_back(TreemapTile{ cidx, cr, col, node.is_dir() });
        }
    }
}

} // namespace

bool TreemapControl::Create(HWND parent, HINSTANCE hInst, int x, int y, int w, int h) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dw_factory));
    if (dw_factory) {
        dw_factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-us", &text_fmt);
        if (text_fmt) {
            text_fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HyperTreeMapClass";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        0, L"HyperTreeMapClass", L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        x, y, w, h, parent, nullptr, hInst, this);
    if (!hwnd) return false;

    tooltip_hwnd = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        hwnd, nullptr, hInst, nullptr);
    if (tooltip_hwnd) {
        TOOLINFOW ti{};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = hwnd;
        ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
        ti.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(tooltip_hwnd, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(tooltip_hwnd, TTM_SETMAXTIPWIDTH, 0, 500);
    }
    return true;
}

void TreemapControl::Resize(int x, int y, int w, int h) {
    if (!hwnd) return;
    MoveWindow(hwnd, x, y, w, h, TRUE);
    layout_dirty = true;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void TreemapControl::SetData(const DiskTree* t, uint32_t rootIdx, uint32_t selIdx) {
    tree = t;
    root_idx = rootIdx;
    selected_idx = selIdx;
    hovered_idx = NONE_IDX;
    layout_dirty = true;
    Invalidate();
}

void TreemapControl::SetSelected(uint32_t selIdx) {
    if (selected_idx != selIdx) {
        selected_idx = selIdx;
        Invalidate();
    }
}

void TreemapControl::SetFilters(const VisibilityFilter& f) {
    if (filters != f) {
        filters = f;
        layout_dirty = true;
        Invalidate();
    }
}

void TreemapControl::SetRtl(bool rtl) {
    is_rtl = rtl;
    if (text_fmt) {
        text_fmt->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT : DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
        text_fmt->SetTextAlignment(rtl ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    Invalidate();
}

void TreemapControl::SetDarkMode(bool dark) {
    if (dark_mode != dark) {
        dark_mode = dark;
        Invalidate();
    }
}

void TreemapControl::Invalidate() {
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void TreemapControl::Cleanup() {
    DiscardResources();
    if (text_fmt) { text_fmt->Release(); text_fmt = nullptr; }
    if (dw_factory) { dw_factory->Release(); dw_factory = nullptr; }
    if (d2d_factory) { d2d_factory->Release(); d2d_factory = nullptr; }
}

void TreemapControl::EnsureResources() {
    if (rt || !d2d_factory || !hwnd) return;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    HRESULT hr = d2d_factory->CreateDCRenderTarget(&props, &rt);
    if (SUCCEEDED(hr) && rt) {
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush);
    }
}

void TreemapControl::DiscardResources() {
    if (brush) { brush->Release(); brush = nullptr; }
    if (rt) { rt->Release(); rt = nullptr; }
}

void TreemapControl::RebuildLayout() {
    tiles.clear();
    layout_dirty = false;
    if (!tree || root_idx == NONE_IDX || root_idx >= tree->nodes.size() || !hwnd) return;

    RECT rc{};
    GetClientRect(hwnd, &rc);
    float w = static_cast<float>(rc.right - rc.left);
    float h = static_cast<float>(rc.bottom - rc.top);
    if (w < 4.0f || h < 4.0f) return;

    LayoutNode(*tree, root_idx, D2D1::RectF(0.0f, 0.0f, w, h), 0, filters, tiles);
}

uint32_t TreemapControl::HitTest(int px, int py) const {
    float x = static_cast<float>(px);
    float y = static_cast<float>(py);
    for (auto it = tiles.rbegin(); it != tiles.rend(); ++it) {
        const auto& r = it->rect;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            return it->node_idx;
        }
    }
    return NONE_IDX;
}

void TreemapControl::UpdateTooltip(int px, int py) {
    uint32_t hit = HitTest(px, py);
    if (hit != hovered_idx) {
        hovered_idx = hit;
        Invalidate();
    }
    if (!tooltip_hwnd || !tree || hit == last_tip_idx) return;
    last_tip_idx = hit;

    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uId = reinterpret_cast<UINT_PTR>(hwnd);
    if (hit == NONE_IDX || hit >= tree->nodes.size()) {
        ti.lpszText = const_cast<wchar_t*>(L"");
        SendMessageW(tooltip_hwnd, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        return;
    }

    const auto& n = tree->nodes[hit];
    uint64_t rootSz = std::max<uint64_t>(1, tree->nodes[root_idx].size);
    double pct = (static_cast<double>(n.size) / static_cast<double>(rootSz)) * 100.0;
    std::wstring full = tree->full_path(hit);
    wchar_t info[1024];
    if (n.is_dir()) {
        swprintf_s(info, L"%s\nSize: %s (%.2f%%) | Allocated: %s\n%s files, %s folders (double-click to zoom)",
            full.c_str(), FormatBytes(n.size).c_str(), pct, FormatBytes(n.allocated).c_str(),
            FormatCount(n.files).c_str(), FormatCount(n.folders).c_str());
    } else {
        swprintf_s(info, L"%s\nSize: %s (%.2f%%) | Allocated: %s",
            full.c_str(), FormatBytes(n.size).c_str(), pct, FormatBytes(n.allocated).c_str());
    }
    ti.lpszText = info;
    SendMessageW(tooltip_hwnd, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
}

void TreemapControl::Render(HDC hdc) {
    if (!hdc) return;
    EnsureResources();
    if (!rt || !brush) return;
    if (layout_dirty) RebuildLayout();

    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (FAILED(rt->BindDC(hdc, &rc))) return;

    rt->BeginDraw();
    rt->Clear(dark_mode
        ? D2D1::ColorF(0.11f, 0.11f, 0.13f, 1.0f)
        : D2D1::ColorF(0.94f, 0.95f, 0.96f, 1.0f));

    if (tree && !tiles.empty()) {
        for (const auto& tile : tiles) {
            bool isHov = (tile.node_idx == hovered_idx);
            bool isSel = (tile.node_idx == selected_idx);
            brush->SetColor(ColorFromRgb(tile.color_rgb, isHov ? 1.22f : 1.0f));
            rt->FillRectangle(tile.rect, brush);

            brush->SetColor(isSel ? D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
            rt->DrawRectangle(tile.rect, brush, isSel ? 2.0f : 0.8f);

            float w = tile.rect.right - tile.rect.left;
            float h = tile.rect.bottom - tile.rect.top;
            if (text_fmt && w > 52.0f && h > 18.0f) {
                std::wstring_view name = tree->node_name(tile.node_idx);
                D2D1_RECT_F tr = D2D1::RectF(tile.rect.left + 3.0f, tile.rect.top + 1.0f, tile.rect.right - 3.0f, tile.rect.bottom - 2.0f);
                rt->PushAxisAlignedClip(tr, D2D1_ANTIALIAS_MODE_ALIASED);
                brush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f));
                rt->DrawText(name.data(), static_cast<UINT32>(name.size()), text_fmt, tr, brush);
                rt->PopAxisAlignedClip();
            }
        }
    }

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardResources();
    }
}

LRESULT CALLBACK TreemapControl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TreemapControl* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TreemapControl*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd = hwnd;
    } else {
        self = reinterpret_cast<TreemapControl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            self->Render(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            self->Render(reinterpret_cast<HDC>(wp));
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            self->UpdateTooltip(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (self->hovered_idx != NONE_IDX) {
                self->hovered_idx = NONE_IDX;
                self->Invalidate();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            uint32_t hit = self->HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit != NONE_IDX) {
                self->SetSelected(hit);
                PostMessageW(GetParent(hwnd), WM_TM_SELECT, hit, 0);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            uint32_t hit = self->HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit != NONE_IDX) {
                PostMessageW(GetParent(hwnd), WM_TM_ZOOM, hit, 0);
            }
            return 0;
        }
        case WM_RBUTTONUP: {
            uint32_t hit = self->HitTest(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hit != NONE_IDX) {
                self->SetSelected(hit);
                POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ClientToScreen(hwnd, &pt);
                PostMessageW(GetParent(hwnd), WM_TM_CONTEXT, hit, MAKELPARAM(pt.x, pt.y));
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
