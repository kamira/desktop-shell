// W1-05 win32 自繪選單呈現 — gtest
//
// 三塊，全部不需要真的彈出選單：
//   1. E11-01 TrayMenu → E7-13 Item 森林的轉換
//   2. 邊緣翻轉（純幾何）
//   3. 繪製結果（記憶體 DIB 讀像素）
//
// 「使用者真的用滑鼠點選單」那一段需要 UI 自動化，本檔不涵蓋，由操作驗收負責。
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "owner_drawn_menu.hpp"

using ds::host::default_menu_theme;
using ds::host::MenuTheme;
using ds::host::OwnerDrawnMenu;
using ds::host::paint_menu_panel;
using ds::host::place_menu_popup;
using ds::host::tray_menu_to_items;
using ds::host::TrayMenu;
using ds::host::TrayMenuItem;

namespace {

// 與 host 實際使用的托盤選單同構：兩個 Checkbox、一個分隔線、一個 Action。
TrayMenu sample_menu() {
    TrayMenu m;
    m.items().push_back(TrayMenuItem::checkbox("最上層顯示", "widget.toggle_topmost", true));
    m.items().push_back(TrayMenuItem::checkbox("點擊穿透", "widget.toggle_passthrough", false));
    m.items().push_back(TrayMenuItem::separator());
    m.items().push_back(TrayMenuItem::action("結束", "app.quit"));
    return m;
}

class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
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
    int count_of(COLORREF c) const {
        int n = 0;
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                const std::uint8_t* p = bits_ + (static_cast<std::size_t>(y) * w_ + x) * 4;
                if (RGB(p[2], p[1], p[0]) == c) ++n;
            }
        }
        return n;
    }

private:
    int w_, h_;
    HDC dc_ = nullptr;
    HBITMAP bmp_ = nullptr;
    HGDIOBJ old_ = nullptr;
    std::uint8_t* bits_ = nullptr;
};

}  // namespace

// --- TrayMenu → Item 森林 ----------------------------------------------------

TEST(OwnerDrawnMenu, ConvertsTrayMenuPreservingOrderAndLabels) {
    const auto items = tray_menu_to_items(sample_menu());
    ASSERT_EQ(items.size(), 4u);
    EXPECT_EQ(items[0].label(), "最上層顯示");
    EXPECT_EQ(items[1].label(), "點擊穿透");
    EXPECT_EQ(items[3].label(), "結束");
    EXPECT_EQ(items[0].id(), "widget.toggle_topmost") << "有命令 id 者直接沿用";
}

// 分隔線沒有命令 id，但 E11-02 要求 id 唯一——必須合成且彼此不同。
TEST(OwnerDrawnMenu, SynthesisesUniqueIdsForItemsWithoutCommand) {
    TrayMenu m;
    m.items().push_back(TrayMenuItem::separator());
    m.items().push_back(TrayMenuItem::separator());
    const auto items = tray_menu_to_items(m);
    ASSERT_EQ(items.size(), 2u);
    EXPECT_FALSE(items[0].id().empty());
    EXPECT_NE(items[0].id(), items[1].id()) << "兩條分隔線的 id 不得相同";
}

// kind / checked / enabled 要如實帶到 value payload，否則 E11-02 建不出正確模型。
TEST(OwnerDrawnMenu, CarriesKindCheckedEnabledIntoPayload) {
    const auto items = tray_menu_to_items(sample_menu());
    const auto* kind = items[0].value().find(ds::host::menu_item_keys::kKind);
    const auto* checked = items[0].value().find(ds::host::menu_item_keys::kChecked);
    ASSERT_NE(kind, nullptr);
    ASSERT_NE(checked, nullptr);
    EXPECT_EQ(kind->as_string(), "checkbox");
    EXPECT_TRUE(checked->as_bool()) << "第一項是已勾選的 checkbox";
}

// 模型建得起來，且列數與來源一致。
TEST(OwnerDrawnMenu, BuildsRenderModelWithOneRowPerItem) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    const auto m = menu.render_model();
    ASSERT_FALSE(m.panels.empty());
    EXPECT_EQ(m.panels.front().rows.size(), 4u);
    EXPECT_GT(m.panels.front().size.height, 0.0);
}

// --- 邊緣翻轉（純幾何）-------------------------------------------------------

TEST(MenuPlacement, PlacesBelowRightWhenThereIsRoom) {
    const RECT work = {0, 0, 1920, 1040};
    const POINT p = place_menu_popup(POINT{500, 400}, 200, 120, work);
    EXPECT_EQ(p.x, 500);
    EXPECT_EQ(p.y, 400);
}

// 右邊放不下 → 往左長。原生選單免費提供這個行為，自繪必須自己做。
TEST(MenuPlacement, FlipsLeftWhenNotEnoughRoomOnRight) {
    const RECT work = {0, 0, 1920, 1040};
    const POINT p = place_menu_popup(POINT{1900, 400}, 200, 120, work);
    EXPECT_EQ(p.x, 1700) << "應以游標為右緣往左長";
}

// 下面放不下 → 往上長（系統匣在右下角時的常態）。
TEST(MenuPlacement, FlipsUpWhenNotEnoughRoomBelow) {
    const RECT work = {0, 0, 1920, 1040};
    const POINT p = place_menu_popup(POINT{500, 1030}, 200, 120, work);
    EXPECT_EQ(p.y, 910);
}

TEST(MenuPlacement, FlipsBothAtBottomRightCorner) {
    const RECT work = {0, 0, 1920, 1040};
    const POINT p = place_menu_popup(POINT{1910, 1035}, 200, 120, work);
    EXPECT_EQ(p.x, 1710);
    EXPECT_EQ(p.y, 915);
}

// 面板比工作區還大時夾到工作區內——寧可貼邊也不要跑出畫面。
TEST(MenuPlacement, ClampsWhenPanelLargerThanWorkArea) {
    const RECT work = {0, 0, 300, 200};
    const POINT p = place_menu_popup(POINT{150, 100}, 400, 300, work);
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
}

// 工作區原點非 (0,0)（工作列在上 / 左）。
TEST(MenuPlacement, RespectsNonZeroWorkAreaOrigin) {
    const RECT work = {80, 40, 1920, 1040};
    const POINT p = place_menu_popup(POINT{100, 60}, 400, 300, work);
    EXPECT_GE(p.x, 80);
    EXPECT_GE(p.y, 40);
}

// --- 繪製（讀真實像素）-------------------------------------------------------

TEST(MenuPainting, PaintsBackgroundAcrossPanel) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    Canvas c(220, 140);
    const auto theme = default_menu_theme();
    paint_menu_panel(c.dc(), c.bounds(), menu.render_model().panels.front(),
                     menu.model(), theme);
    EXPECT_GT(c.count_of(theme.background), 0);
}

// 分隔線必須真的畫出來——否則選單看起來只是四個擠在一起的項目。
TEST(MenuPainting, DrawsSeparatorLine) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    Canvas c(220, 140);
    const auto theme = default_menu_theme();
    paint_menu_panel(c.dc(), c.bounds(), menu.render_model().panels.front(),
                     menu.model(), theme);
    EXPECT_GT(c.count_of(theme.separator), 0) << "分隔線沒畫出來";
}

// 沒有游標時不得出現高亮——一打開就有一列被選中會誤導使用者。
TEST(MenuPainting, NoHighlightBeforeAnyNavigation) {
    OwnerDrawnMenu menu;
    ASSERT_TRUE(menu.set_menu(sample_menu()));
    Canvas c(220, 140);
    const auto theme = default_menu_theme();
    paint_menu_panel(c.dc(), c.bounds(), menu.render_model().panels.front(),
                     menu.model(), theme);
    EXPECT_EQ(c.count_of(theme.highlight), 0) << "尚未巡覽就出現高亮";
}

// 空選單不得崩潰。
TEST(MenuPainting, EmptyMenuIsSafe) {
    OwnerDrawnMenu menu;
    TrayMenu empty;
    menu.set_menu(empty);
    const auto m = menu.render_model();
    if (!m.panels.empty()) {
        Canvas c(120, 40);
        paint_menu_panel(c.dc(), c.bounds(), m.panels.front(), menu.model(),
                         default_menu_theme());
    }
    SUCCEED();
}
