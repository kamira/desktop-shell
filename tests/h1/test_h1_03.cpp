// H1-03 拖曳裝配 + 位置持久化 — gtest
//
// 最重要的一條是 **round-trip**：像素 → AnchorSpec → 像素必須回到原點。
// 若這條不成立，「拖到哪裡就記住哪裡」整個功能就是壞的，而且會壞得很安靜——
// widget 每次開機都往同一個方向漂一點點，使用者要好幾天才會發現。
//
// 也測「檔案壞掉」與「換螢幕尺寸」這兩個真的會發生的情境。
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "position_store.hpp"

using ds::host::default_positions_path;
using ds::host::pixels_from_spec;
using ds::host::PositionPersistence;
using ds::host::read_text_file;
using ds::host::spec_from_pixels;
using ds::host::WorkArea;
using ds::host::write_text_file;
using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::CapabilityMatrix;
using ds::kernel::DraggableSurface;
using ds::kernel::DragStatus;
using ds::kernel::HitPolicy;
using ds::kernel::InputPolicy;
using ds::kernel::SurfaceLayer;
using ds::kernel::SurfaceLifecycle;
using ds::kernel::SurfaceProfile;
using ds::kernel::Win32KernelBackend;

namespace {

constexpr const char* kSurface = "surface.widget";

SurfaceProfile panel() {
    SurfaceProfile p;
    p.layer = SurfaceLayer::Topmost;
    p.input = InputPolicy::Accepting;
    p.hit = HitPolicy::Solid;
    p.lifecycle = SurfaceLifecycle::Persistent;
    return p;
}

// 典型工作區：1920x1040，工作列在下方（原點仍為 0,0）。
WorkArea typical() { return WorkArea{0, 0, 1920, 1040}; }
// 工作列在上方 / 左側時原點不是 (0,0)——這個情境常被忘記。
WorkArea offset_origin() { return WorkArea{80, 40, 1840, 1000}; }

std::string temp_path(const char* name) {
    char buf[MAX_PATH] = {};
    ::GetTempPathA(MAX_PATH, buf);
    return std::string(buf) + name;
}

}  // namespace

// --- 像素 ↔ AnchorSpec ------------------------------------------------------

// 核心不變式：來回換算必須回到原點（容許 1px 的四捨五入誤差）。
TEST(PositionStore, PixelsRoundTripThroughAnchorSpec) {
    const WorkArea area = typical();
    const int ew = 320, eh = 132;

    for (const auto& p : {std::pair<int, int>{0, 0}, {17, 3}, {640, 480},
                          {1599, 907}, {1920 - ew, 1040 - eh}}) {
        AnchorSpec spec;
        ASSERT_TRUE(spec_from_pixels(p.first, p.second, area, spec))
            << "x=" << p.first << " y=" << p.second;
        int x = 0, y = 0;
        ASSERT_TRUE(pixels_from_spec(spec, area, ew, eh, x, y));
        EXPECT_NEAR(x, p.first, 1) << "x round-trip";
        EXPECT_NEAR(y, p.second, 1) << "y round-trip";
    }
}

// 工作區原點不是 (0,0) 時（工作列在上或在左）也要正確。
TEST(PositionStore, RoundTripHandlesNonZeroWorkAreaOrigin) {
    const WorkArea area = offset_origin();
    AnchorSpec spec;
    ASSERT_TRUE(spec_from_pixels(500, 300, area, spec));
    int x = 0, y = 0;
    ASSERT_TRUE(pixels_from_spec(spec, area, 320, 132, x, y));
    EXPECT_NEAR(x, 500, 1);
    EXPECT_NEAR(y, 300, 1);
}

// 存的是比例不是像素 —— 換螢幕解析度時位置要按比例移動，而不是留在原像素座標。
// 這正是用 AnchorSpec 而非像素持久化的理由。
TEST(PositionStore, StoredPositionScalesWithScreenSize) {
    AnchorSpec spec;
    ASSERT_TRUE(spec_from_pixels(960, 520, typical(), spec));  // 大螢幕的正中央

    const WorkArea smaller{0, 0, 1280, 720};
    int x = 0, y = 0;
    ASSERT_TRUE(pixels_from_spec(spec, smaller, 320, 132, x, y));
    EXPECT_NEAR(x, 640, 1) << "應落在小螢幕的相對同一位置";
    EXPECT_NEAR(y, 360, 1);
}

// 退化的工作區不得產生垃圾資料。
TEST(PositionStore, DegenerateWorkAreaIsRejected) {
    AnchorSpec spec;
    EXPECT_FALSE(spec_from_pixels(10, 10, WorkArea{0, 0, 0, 100}, spec));
    EXPECT_FALSE(spec_from_pixels(10, 10, WorkArea{0, 0, 100, 0}, spec));
    int x = 0, y = 0;
    EXPECT_FALSE(pixels_from_spec(AnchorSpec{}, WorkArea{0, 0, 0, 0}, 10, 10, x, y));
}

// --- 檔案存取 ---------------------------------------------------------------

TEST(PositionStore, TextFileRoundTrips) {
    const std::string p = temp_path("ds_pos_roundtrip.conf");
    ASSERT_TRUE(write_text_file(p, "hello\nworld\n"));
    EXPECT_EQ(read_text_file(p), "hello\nworld\n");
    std::remove(p.c_str());
}

// UTF-8 BOM 必須被去掉——否則使用者用記事本改過設定檔之後，設定會被**靜默丟棄**
// 而退回預設值（CHG-20260803-12 的操作驗收實際踩到；同 K-001 / K-004 的字集家族）。
TEST(PositionStore, ReadStripsUtf8Bom) {
    const std::string p = temp_path("ds_pos_bom.conf");
    const std::string body = "format_version: 1.0\nwidget.controls:\n  locked: true\n";
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out << "\xEF\xBB\xBF" << body;  // 帶 BOM 寫入
    }
    EXPECT_EQ(read_text_file(p), body) << "BOM 必須被去掉，否則第一行解析必失敗";
    std::remove(p.c_str());
}

// 只去掉開頭的 BOM，不得動到內容中其他位元組。
TEST(PositionStore, ReadDoesNotStripNonBomBytes) {
    const std::string p = temp_path("ds_pos_nobom.conf");
    const std::string body = "format_version: 1.0\n";
    ASSERT_TRUE(write_text_file(p, body));
    EXPECT_EQ(read_text_file(p), body);
    std::remove(p.c_str());
}

TEST(PositionStore, MissingFileReadsEmptyNotCrash) {
    EXPECT_EQ(read_text_file(temp_path("ds_pos_does_not_exist.conf")), "");
    EXPECT_EQ(read_text_file(""), "");
    EXPECT_FALSE(write_text_file("", "x"));
}

TEST(PositionStore, DefaultPathIsUnderLocalAppData) {
    const std::string p = default_positions_path();
    ASSERT_FALSE(p.empty()) << "測試環境應有 LOCALAPPDATA";
    EXPECT_NE(p.find("desktop-shell"), std::string::npos);
    EXPECT_NE(p.find("positions.conf"), std::string::npos);
}

// --- 端到端：拖曳 → 記住 → 還原 ----------------------------------------------

// 完整情境：移動視窗 → 記住 → 寫檔 →（模擬重開）→ 還原 → 視窗真的回到原位。
TEST(PositionStore, RememberFlushRestoreReturnsWindowToSamePlace) {
    const std::string path = temp_path("ds_pos_e2e.conf");
    std::remove(path.c_str());

    int remembered_x = 0, remembered_y = 0;
    {
        Win32KernelBackend backend{CapabilityMatrix::defaults()};
        ASSERT_TRUE(backend.init());
        ASSERT_TRUE(backend.create_surface(kSurface, panel()));
        ASSERT_TRUE(backend.set_surface_origin(kSurface, 420, 260));

        DraggableSurface drag{backend};
        PositionPersistence store{backend, drag, path};

        ASSERT_TRUE(store.remember_current(kSurface));
        ASSERT_TRUE(store.flush());
        ASSERT_TRUE(backend.surface_origin(kSurface, remembered_x, remembered_y));
        EXPECT_EQ(remembered_x, 420);
        EXPECT_EQ(remembered_y, 260);
    }

    // 模擬「關掉再開」：全新的後端、全新的狀態機，只有檔案是共同的。
    {
        Win32KernelBackend backend{CapabilityMatrix::defaults()};
        ASSERT_TRUE(backend.init());
        ASSERT_TRUE(backend.create_surface(kSurface, panel()));
        // 新 surface 會落在預設幾何，先確認它**不在**記憶位置上，否則測試沒有鑑別力。
        int x0 = 0, y0 = 0;
        ASSERT_TRUE(backend.surface_origin(kSurface, x0, y0));
        ASSERT_NE(x0, remembered_x) << "預設位置恰好等於記憶位置，本測試將失去鑑別力";

        DraggableSurface drag{backend};
        PositionPersistence store{backend, drag, path};
        ASSERT_TRUE(store.restore(kSurface));

        int x = 0, y = 0;
        ASSERT_TRUE(backend.surface_origin(kSurface, x, y));
        EXPECT_NEAR(x, remembered_x, 1);
        EXPECT_NEAR(y, remembered_y, 1);
    }
    std::remove(path.c_str());
}

// 沒有位置檔時（第一次執行）不得崩潰，也不得亂搬視窗。
TEST(PositionStore, RestoreWithoutFileLeavesWindowAlone) {
    const std::string path = temp_path("ds_pos_absent.conf");
    std::remove(path.c_str());

    Win32KernelBackend backend{CapabilityMatrix::defaults()};
    ASSERT_TRUE(backend.init());
    ASSERT_TRUE(backend.create_surface(kSurface, panel()));
    int before_x = 0, before_y = 0;
    ASSERT_TRUE(backend.surface_origin(kSurface, before_x, before_y));

    DraggableSurface drag{backend};
    PositionPersistence store{backend, drag, path};
    EXPECT_FALSE(store.restore(kSurface));

    int after_x = 0, after_y = 0;
    ASSERT_TRUE(backend.surface_origin(kSurface, after_x, after_y));
    EXPECT_EQ(after_x, before_x) << "沒有記錄就不該動視窗";
    EXPECT_EQ(after_y, before_y);
}

// 位置檔壞掉時：不得套用半套狀態、不得崩潰、不得把視窗搬到奇怪的地方。
TEST(PositionStore, CorruptFileIsRejectedWithoutMovingWindow) {
    const std::string path = temp_path("ds_pos_corrupt.conf");
    ASSERT_TRUE(write_text_file(path, "這不是合法的宣告式設定 {{{ \n anchor: ???"));

    Win32KernelBackend backend{CapabilityMatrix::defaults()};
    ASSERT_TRUE(backend.init());
    ASSERT_TRUE(backend.create_surface(kSurface, panel()));
    int before_x = 0, before_y = 0;
    ASSERT_TRUE(backend.surface_origin(kSurface, before_x, before_y));

    DraggableSurface drag{backend};
    PositionPersistence store{backend, drag, path};
    EXPECT_FALSE(store.restore(kSurface));
    EXPECT_EQ(drag.tracked_count(), 0u) << "壞檔不得留下半套記憶（E1-08 全有或全無）";

    int after_x = 0, after_y = 0;
    ASSERT_TRUE(backend.surface_origin(kSurface, after_x, after_y));
    EXPECT_EQ(after_x, before_x);
    EXPECT_EQ(after_y, before_y);
    std::remove(path.c_str());
}

// 連續拖兩次：記住的必須是最後一次的位置。
TEST(PositionStore, SecondMoveOverwritesFirst) {
    const std::string path = temp_path("ds_pos_twice.conf");
    std::remove(path.c_str());

    Win32KernelBackend backend{CapabilityMatrix::defaults()};
    ASSERT_TRUE(backend.init());
    ASSERT_TRUE(backend.create_surface(kSurface, panel()));
    DraggableSurface drag{backend};
    PositionPersistence store{backend, drag, path};

    ASSERT_TRUE(backend.set_surface_origin(kSurface, 200, 150));
    ASSERT_TRUE(store.remember_current(kSurface));
    ASSERT_TRUE(backend.set_surface_origin(kSurface, 700, 500));
    ASSERT_TRUE(store.remember_current(kSurface));
    ASSERT_TRUE(store.flush());

    Win32KernelBackend b2{CapabilityMatrix::defaults()};
    ASSERT_TRUE(b2.init());
    ASSERT_TRUE(b2.create_surface(kSurface, panel()));
    DraggableSurface d2{b2};
    PositionPersistence s2{b2, d2, path};
    ASSERT_TRUE(s2.restore(kSurface));

    int x = 0, y = 0;
    ASSERT_TRUE(b2.surface_origin(kSurface, x, y));
    EXPECT_NEAR(x, 700, 1);
    EXPECT_NEAR(y, 500, 1);
    std::remove(path.c_str());
}
