// H1-01 GDI 繪製橋接 — gtest
//
// 兩個層次：
//   1. 版面計算（row_layout / fill_rect）是不碰 GDI 的純函式 → 直接斷言幾何。
//   2. 繪製本體以**記憶體 DIB** 測試 → 可以真的去讀像素，不需要視窗。
//      這是關鍵：只斷言「paint_widget 沒有崩潰」等於沒有測試（見知識庫 K-003 的通則），
//      所以這裡實際取樣像素，證明量表真的依 fill_ratio 畫出不同長度。
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "widget_painter.hpp"

using ds::host::default_theme;
using ds::host::fill_rect;
using ds::host::paint_widget;
using ds::host::row_layout;
using ds::widgets::MetricRenderEntry;
using ds::widgets::SystemStatusRenderModel;

namespace {

RECT make_rect(LONG l, LONG t, LONG r, LONG b) { return RECT{l, t, r, b}; }

MetricRenderEntry entry(const char* label, double ratio, bool available,
                        const char* text) {
    MetricRenderEntry e;
    e.label = label;
    e.available = available;
    e.display_text = text;
    e.element.fill_ratio = ratio;
    return e;
}

// 一塊可以直接讀像素的離屏畫布。
class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down，第 0 列即最上面一列
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        dc_ = ::CreateCompatibleDC(nullptr);
        bmp_ = ::CreateDIBSection(dc_, &bi, DIB_RGB_COLORS,
                                  reinterpret_cast<void**>(&bits_), nullptr, 0);
        old_ = ::SelectObject(dc_, bmp_);
    }
    ~Canvas() {
        ::SelectObject(dc_, old_);
        ::DeleteObject(bmp_);
        ::DeleteDC(dc_);
    }
    HDC dc() const { return dc_; }
    RECT bounds() const { return RECT{0, 0, w_, h_}; }
    // 回傳 (x,y) 的 RGB（DIB 為 BGRA 排列）。
    COLORREF at(int x, int y) const {
        const std::uint8_t* p = bits_ + (static_cast<std::size_t>(y) * w_ + x) * 4;
        return RGB(p[2], p[1], p[0]);
    }
    // 統計整張圖裡等於某色的像素數。
    int count_of(COLORREF c) const {
        int n = 0;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (at(x, y) == c) ++n;
        return n;
    }

private:
    int w_, h_;
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    std::uint8_t* bits_ = nullptr;
};

SystemStatusRenderModel model_of(std::vector<MetricRenderEntry> entries) {
    SystemStatusRenderModel m;
    m.entries = std::move(entries);
    return m;
}

}  // namespace

// --- 版面計算（純函式）------------------------------------------------------

TEST(WidgetPainter, RowLayoutRejectsOutOfRange) {
    const RECT b = make_rect(0, 0, 320, 132);
    for (int bad : {-1, 3, 99}) {
        const auto r = row_layout(b, bad, 3);
        EXPECT_EQ(r.track.left, 0);
        EXPECT_EQ(r.track.right, 0);
    }
    const auto none = row_layout(b, 0, 0);  // row_count <= 0
    EXPECT_EQ(none.track.right, 0);
}

TEST(WidgetPainter, RowsAreOrderedTopToBottomAndDoNotOverlap) {
    const RECT b = make_rect(0, 0, 320, 132);
    const auto r0 = row_layout(b, 0, 3);
    const auto r1 = row_layout(b, 1, 3);
    const auto r2 = row_layout(b, 2, 3);
    EXPECT_LT(r0.track.top, r1.track.top);
    EXPECT_LT(r1.track.top, r2.track.top);
    EXPECT_LE(r0.label.bottom, r1.label.top);
    EXPECT_LE(r1.label.bottom, r2.label.top);
}

TEST(WidgetPainter, RowRegionsAreLeftToRightAndDisjoint) {
    const RECT b = make_rect(0, 0, 320, 132);
    const auto r = row_layout(b, 0, 3);
    EXPECT_LT(r.label.right, r.track.left);   // 標籤在軌道左邊，且有間隙
    EXPECT_LT(r.track.right, r.value.left);   // 軌道在數值左邊，且有間隙
    EXPECT_GT(r.track.right, r.track.left);   // 軌道有實際寬度
}

// fill_rect 是量表長度的唯一來源，逐條釘死。
TEST(WidgetPainter, FillRectIsMonotonicAndClamped) {
    const RECT track = make_rect(100, 10, 200, 20);  // 寬 100
    EXPECT_EQ(fill_rect(track, 0.0).right, 100);     // 空
    EXPECT_EQ(fill_rect(track, 1.0).right, 200);     // 滿
    EXPECT_EQ(fill_rect(track, 0.5).right, 150);     // 半
    EXPECT_EQ(fill_rect(track, -5.0).right, 100);    // 負值夾到 0
    EXPECT_EQ(fill_rect(track, 42.0).right, 200);    // 超過 1 夾到滿
    // 單調性：比例愈大，填充不得變短。
    LONG prev = 0;
    for (int i = 0; i <= 10; ++i) {
        const LONG w = fill_rect(track, i / 10.0).right - track.left;
        EXPECT_GE(w, prev);
        prev = w;
    }
}

TEST(WidgetPainter, FillRectTreatsNanAsEmpty) {
    const RECT track = make_rect(0, 0, 100, 10);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(fill_rect(track, nan).right, track.left);  // NaN 不得畫成滿的
}

// --- 繪製本體（讀真實像素）---------------------------------------------------

TEST(WidgetPainter, PaintsBackgroundOverWholeBounds) {
    Canvas c(320, 132);
    const auto theme = default_theme();
    paint_widget(c.dc(), c.bounds(), model_of({}), L"t", theme);
    EXPECT_EQ(c.at(0, 0), theme.background);
    EXPECT_EQ(c.at(319, 131), theme.background);
}

// 核心斷言：fill_ratio 愈大，填充色的像素愈多。
// 這條若過不了，代表量表沒有真的跟著資料走——正是先前主控台驗證器踩過的坑。
TEST(WidgetPainter, MoreFillRatioPaintsMoreFillPixels) {
    const auto theme = default_theme();
    int last = -1;
    for (double ratio : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        Canvas c(320, 132);
        paint_widget(c.dc(), c.bounds(),
                     model_of({entry("CPU", ratio, true, "x")}), L"t", theme);
        const int n = c.count_of(theme.fill);
        EXPECT_GT(n, last) << "ratio=" << ratio;
        last = n;
    }
}

TEST(WidgetPainter, ZeroRatioPaintsNoFillButStillPaintsTrack) {
    Canvas c(320, 132);
    const auto theme = default_theme();
    paint_widget(c.dc(), c.bounds(),
                 model_of({entry("CPU", 0.0, true, "0%")}), L"t", theme);
    EXPECT_EQ(c.count_of(theme.fill), 0);   // 沒有填充
    EXPECT_GT(c.count_of(theme.track), 0);  // 但軌道要看得見，否則使用者不知道有這條量表
}

// NFR-03 優雅降級：不可用的條目不得畫出填充，且**不得影響其他條目**。
TEST(WidgetPainter, UnavailableEntryDegradesWithoutAffectingOthers) {
    const auto theme = default_theme();

    Canvas only_ok(320, 132);
    paint_widget(only_ok.dc(), only_ok.bounds(),
                 model_of({entry("CPU", 1.0, true, "100%"),
                           entry("GPU", 1.0, true, "100%")}), L"t", theme);
    const int two_ok = only_ok.count_of(theme.fill);

    Canvas mixed(320, 132);
    paint_widget(mixed.dc(), mixed.bounds(),
                 model_of({entry("CPU", 1.0, true, "100%"),
                           entry("GPU", 1.0, false, "—")}), L"t", theme);
    const int one_ok = mixed.count_of(theme.fill);

    EXPECT_GT(one_ok, 0) << "可用的那筆仍須正常繪製";
    EXPECT_LT(one_ok, two_ok) << "不可用的那筆不得畫出填充";
}

// 空 model 不得崩潰，且畫面仍是有效的背景（不是垃圾像素）。
TEST(WidgetPainter, EmptyModelIsSafe) {
    Canvas c(320, 132);
    const auto theme = default_theme();
    paint_widget(c.dc(), c.bounds(), model_of({}), L"標題", theme);
    EXPECT_EQ(c.count_of(theme.fill), 0);
    EXPECT_GT(c.count_of(theme.background), 0);
}

// 退化尺寸不得崩潰（視窗被縮到極小時會發生）。
TEST(WidgetPainter, DegenerateBoundsAreSafe) {
    Canvas c(8, 8);
    const auto theme = default_theme();
    paint_widget(c.dc(), make_rect(0, 0, 8, 8),
                 model_of({entry("CPU", 0.5, true, "50%")}), L"t", theme);
    SUCCEED();  // 走到這裡即代表沒有崩潰；上面各測試已負責內容正確性
}
