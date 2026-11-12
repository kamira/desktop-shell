// tests/h1/test_h1_07.cpp — H1-07 組件組合裝配（gtest）
//
// 涵蓋驗收標準的每一句：
//   1. **host 消費 E9-03 Composition**：插槽宣告 / 綁定 / 替換 / 清空 / 相容性 / 完整性驗證
//      全部經 `ds::package::Composition`，本層不重造（`ConsumesE903` 一組）。
//   2. **組件為獨立組合單位**：組件 = E9-01 `Package`，同一個 `Package` 物件可綁進任意
//      數量的 widget 插槽（`Component` / `SameComponentInTwoWidgets` 兩組）。
//   3. **widget 由一或多個組件組成**：零個組件的 widget 解析回 `EmptyWidget`（不是空版面）。
//   4. **組件位置以 widget 內相對 pixel 表達，原點左上**（`Geometry` 一組）。
//   5. **單位為邏輯像素（隨 DPI 縮放）**，縮放係數取自 kernel 的 E1-17 `DpiInfo`
//      （`Dpi` 一組）。
//   6. **同一組件須能同時出現在兩個 widget 且呈現型態可不同**，第二案例為 **C2-07 媒體控制**
//      （`SameComponentInTwoWidgets` / `MediaCase` 兩組 —— 後者真的建一個
//      `ds::widgets::MediaControlWidget`、`refresh()` 讀 E2-13 指標，再把它的
//      `MediaControlRenderModel` 切成部件框）。
//
// 反向斷言（E4 與 kernel 的公開介面不得出現任何像素型別）在
// `tests/h1/test_h1_07_pixel_boundary.cpp`，獨立的程式與獨立的 `add_test`。
#include "media_compose.hpp"
#include "widget_compose.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using ds::actuators::MediaControlActuator;
using ds::host::ComponentAppearance;
using ds::host::ComponentPlacement;
using ds::host::ComponentPresentation;
using ds::host::ComposeStatus;
using ds::host::MediaPartBox;
using ds::host::PaintBox;
using ds::host::PixelPoint;
using ds::host::PixelSize;
using ds::host::WidgetAssembly;
using ds::host::WidgetLayout;
using ds::kernel::CapabilityMatrix;
using ds::kernel::DpiInfo;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::ScreenDpi;
using ds::metrics::MetricRegistry;
using ds::package::ComponentSlot;
using ds::package::Package;
using ds::sysinfo::MediaMetadata;
using ds::sysinfo::MediaMetadataProvider;
using ds::sysinfo::NullMediaSource;
using ds::sysinfo::PlaybackState;
using ds::widgets::MediaControlStatus;
using ds::widgets::MediaControlWidget;

namespace {

// --- 組件（E9-01 Package）的建構小工具：失敗一律當場 ASSERT，不讓半套資料流進斷言 ---
Package media_component(const std::string& name) {
    Package pkg;
    std::string error;
    const bool ok = ds::host::make_media_control_component(name, pkg, error);
    EXPECT_TRUE(ok) << error;
    return pkg;
}

Package artwork_component(const std::string& name) {
    Package pkg;
    std::string error;
    const bool ok = ds::host::make_artwork_only_component(name, pkg, error);
    EXPECT_TRUE(ok) << error;
    return pkg;
}

ComponentPlacement at(int x, int y, int w, int h, const char* variant) {
    ComponentPlacement p;
    p.origin = PixelPoint{x, y};
    p.size = PixelSize{w, h};
    p.presentation.variant = variant;
    return p;
}

// 一個已綁好、可解析的單組件 widget。
WidgetAssembly one_component_widget(const std::string& widget_id, PixelSize size,
                                    const ComponentPlacement& placement, const Package& component) {
    WidgetAssembly w{widget_id, size};
    EXPECT_TRUE(w.declare(ds::host::media_control_slot("main"), placement).ok());
    EXPECT_TRUE(w.bind("main", component).ok());
    return w;
}

// C2-07 的固定件（依宣告順序初始化；backend 先 init 才能 create_surface —— 同 test_c2_07）。
// **全部按值持有**：K-013 那條「以 const 參考持有 + 按值回傳的工廠 = 懸空參考，而測試是綠的」
// 的形狀，在本檔刻意不出現。`MediaControlWidget` 以參考持有 backend / layers，
// 故兩者必須與 widget 同生命週期 —— 由本結構一起持有。
struct MediaFixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    bool backend_initialized = backend.init();
    LayerStack layers{CapabilityMatrix::defaults()};
    std::shared_ptr<NullMediaSource> source = std::make_shared<NullMediaSource>();
    std::shared_ptr<MetricRegistry> registry = std::make_shared<MetricRegistry>();
    std::shared_ptr<MediaControlActuator> actuator = std::make_shared<MediaControlActuator>();
    MediaMetadataProvider provider{source};

    void publish(const MediaMetadata& meta) {
        source->set_metadata(meta);
        provider.register_metrics(*registry);
    }
};

MediaMetadata playing_song() {
    return MediaMetadata{/*has_media=*/true, PlaybackState::Playing, "Song A", "Artist A",
                         "Album A", /*position=*/10.0, /*duration=*/200.0, "art/cover-a"};
}

std::vector<std::string> part_ids(const std::vector<MediaPartBox>& parts) {
    std::vector<std::string> ids;
    ids.reserve(parts.size());
    for (const MediaPartBox& p : parts) ids.push_back(p.part);
    return ids;
}

bool contains(const PaintBox& outer, const PaintBox& inner) {
    return inner.left >= outer.left && inner.top >= outer.top && inner.right <= outer.right &&
           inner.bottom <= outer.bottom;
}

}  // namespace

// =============================================================================
// 組件 = 獨立組合單位（E9-01 Package）
// =============================================================================
TEST(Component, IsAnE901PackageParsedByUpstream) {
    const Package pkg = media_component("com.example.media.control");
    EXPECT_EQ(pkg.manifest.name, std::string("com.example.media.control"));
    ASSERT_EQ(pkg.entries.size(), 2u);
    EXPECT_EQ(pkg.entries[0].kind, std::string(ds::host::kMediaArtworkKind));
    EXPECT_EQ(pkg.entries[1].kind, std::string(ds::host::kMediaTransportKind));
}

TEST(Component, EmptyNameIsRefusedWithAReasonNotAHalfBuiltPackage) {
    Package pkg;
    std::string error;
    EXPECT_FALSE(ds::host::make_media_control_component("", pkg, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(pkg.manifest.name.empty());
}

// =============================================================================
// widget = 一或多個組件
// =============================================================================
TEST(Assembly, OneComponentWidgetResolves) {
    const Package pkg = media_component("com.example.media.control");
    const WidgetAssembly w =
        one_component_widget("widget.media.home", PixelSize{320, 180},
                             at(12, 20, 296, 140, ds::host::kMediaVariantExpanded), pkg);

    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.0, layout).ok());
    EXPECT_EQ(layout.widget_id, std::string("widget.media.home"));
    ASSERT_EQ(layout.component_count(), 1u);
    EXPECT_EQ(layout.components[0].component_name,
              std::string("com.example.media.control"));
}

TEST(Assembly, MultipleComponentsKeepDeclarationOrderAndEachGetsItsOwnBox) {
    WidgetAssembly w{"widget.dash", PixelSize{200, 120}};
    const Package a = media_component("com.example.a");
    const Package b = media_component("com.example.b");
    const Package c = media_component("com.example.c");

    ASSERT_TRUE(w.declare(ds::host::media_control_slot("top"), at(0, 0, 200, 40, "")).ok());
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("mid"), at(0, 40, 200, 40, "")).ok());
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("bot"), at(0, 80, 200, 40, "")).ok());
    ASSERT_TRUE(w.bind("top", a).ok());
    ASSERT_TRUE(w.bind("mid", b).ok());
    ASSERT_TRUE(w.bind("bot", c).ok());

    EXPECT_EQ(w.slot_ids(), (std::vector<std::string>{"top", "mid", "bot"}));

    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.0, layout).ok());
    ASSERT_EQ(layout.component_count(), 3u);
    EXPECT_EQ(layout.components[0].slot_id, std::string("top"));
    EXPECT_EQ(layout.components[2].slot_id, std::string("bot"));
    // 三個格位相鄰但不重疊（半開區間：40 是上一格的 bottom，也是下一格的 top）。
    EXPECT_FALSE(ds::host::boxes_overlap(layout.components[0].box, layout.components[1].box));
    EXPECT_FALSE(ds::host::boxes_overlap(layout.components[1].box, layout.components[2].box));
    EXPECT_EQ(layout.components[0].box.bottom, layout.components[1].box.top);
}

TEST(Assembly, ZeroComponentWidgetIsEmptyWidgetNotAnEmptyLayout) {
    const WidgetAssembly w{"widget.void", PixelSize{100, 100}};
    WidgetLayout layout;
    layout.widget_id = "untouched";
    const auto r = w.resolve(1.0, layout);
    EXPECT_EQ(r.status, ComposeStatus::EmptyWidget);
    EXPECT_FALSE(r.message.empty());
    EXPECT_EQ(layout.widget_id, std::string("untouched"));  // 失敗不寫出半套版面
}

TEST(Assembly, UnboundSlotMakesResolveIncompleteAndLeavesOutUntouched) {
    WidgetAssembly w{"widget.partial", PixelSize{100, 100}};
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("a"), at(0, 0, 50, 50, "")).ok());
    WidgetLayout layout;
    layout.widget_id = "untouched";
    const auto r = w.resolve(1.0, layout);
    EXPECT_EQ(r.status, ComposeStatus::IncompleteComposition);
    EXPECT_FALSE(r.message.empty());  // E9-03 的 validate() 會指名未綁定的插槽
    EXPECT_EQ(layout.widget_id, std::string("untouched"));
}

// =============================================================================
// 組件位置：widget 內相對 pixel，原點左上
// =============================================================================
TEST(Geometry, OriginIsTopLeftOfTheWidget) {
    const Package pkg = media_component("com.example.x");
    const WidgetAssembly w =
        one_component_widget("w", PixelSize{320, 180}, at(0, 0, 320, 180, ""), pkg);
    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.0, layout).ok());
    EXPECT_EQ(layout.bounds.left, 0);
    EXPECT_EQ(layout.bounds.top, 0);
    EXPECT_EQ(layout.components[0].box.left, 0);
    EXPECT_EQ(layout.components[0].box.top, 0);
    EXPECT_EQ(layout.components[0].box.right, 320);
    EXPECT_EQ(layout.components[0].box.bottom, 180);
}

TEST(Geometry, PlacementIsRelativeToTheWidgetNotTheScreen) {
    const Package pkg = media_component("com.example.x");
    const WidgetAssembly w =
        one_component_widget("w", PixelSize{320, 180}, at(12, 20, 100, 40, ""), pkg);
    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.0, layout).ok());
    const PaintBox& box = layout.components[0].box;
    EXPECT_EQ(box.left, 12);
    EXPECT_EQ(box.top, 20);
    EXPECT_EQ(box.right, 112);   // 12 + 100（半開區間）
    EXPECT_EQ(box.bottom, 60);   // 20 + 40
    EXPECT_EQ(box.width(), 100);
    EXPECT_EQ(box.height(), 40);
}

TEST(Geometry, OutOfBoundsPlacementIsRefusedAndStateIsUnchanged) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    // 右緣超出：100 + 1 > 100
    auto r = w.declare(ds::host::media_control_slot("a"), at(1, 0, 100, 10, ""));
    EXPECT_EQ(r.status, ComposeStatus::OutOfBounds);
    EXPECT_FALSE(r.message.empty());
    // **E9-03 的組合物件完全沒被碰過**（先驗放置、後動組合）。
    EXPECT_FALSE(w.has_slot("a"));
    EXPECT_TRUE(w.composition().slot_ids().empty());

    // 負座標同樣擋下（widget 內相對座標的原點是左上角 (0,0)）。
    r = w.declare(ds::host::media_control_slot("b"), at(-1, 0, 10, 10, ""));
    EXPECT_EQ(r.status, ComposeStatus::OutOfBounds);
    EXPECT_FALSE(w.has_slot("b"));
}

TEST(Geometry, DegenerateSizesAreRefused) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    EXPECT_EQ(w.declare(ds::host::media_control_slot("a"), at(0, 0, 0, 10, "")).status,
              ComposeStatus::DegenerateSize);
    EXPECT_EQ(w.declare(ds::host::media_control_slot("b"), at(0, 0, 10, -5, "")).status,
              ComposeStatus::DegenerateSize);

    // widget 自身退化：declare 與 resolve 都擋。
    WidgetAssembly bad{"bad", PixelSize{0, 100}};
    EXPECT_EQ(bad.declare(ds::host::media_control_slot("a"), at(0, 0, 1, 1, "")).status,
              ComposeStatus::DegenerateSize);
    WidgetLayout layout;
    EXPECT_EQ(bad.resolve(1.0, layout).status, ComposeStatus::EmptyWidget);  // 先沒組件
}

TEST(Geometry, PlaceMovesTheComponentAndRefusesAnIllegalMove) {
    const Package pkg = media_component("com.example.x");
    WidgetAssembly w =
        one_component_widget("w", PixelSize{100, 100}, at(0, 0, 10, 10, ""), pkg);

    ASSERT_TRUE(w.place("main", PixelPoint{20, 30}, PixelSize{40, 50}).ok());
    ASSERT_NE(w.placement("main"), nullptr);
    EXPECT_EQ(w.placement("main")->origin, (PixelPoint{20, 30}));

    // 非法移動 → 舊版面原封不動。
    EXPECT_EQ(w.place("main", PixelPoint{90, 90}, PixelSize{40, 50}).status,
              ComposeStatus::OutOfBounds);
    EXPECT_EQ(w.placement("main")->origin, (PixelPoint{20, 30}));
    EXPECT_EQ(w.placement("main")->size, (PixelSize{40, 50}));

    EXPECT_EQ(w.place("nope", PixelPoint{0, 0}, PixelSize{1, 1}).status,
              ComposeStatus::UnknownSlot);
}

TEST(Geometry, OverlapIsAQueryNotAnError) {
    const PaintBox a{0, 0, 10, 10};
    const PaintBox b{5, 5, 15, 15};
    const PaintBox adjacent{10, 0, 20, 10};
    const PaintBox degenerate{0, 0, 0, 10};
    EXPECT_TRUE(ds::host::boxes_overlap(a, b));
    EXPECT_FALSE(ds::host::boxes_overlap(a, adjacent));  // 半開區間：相鄰不重疊
    EXPECT_FALSE(ds::host::boxes_overlap(a, degenerate));
}

// =============================================================================
// 邏輯像素隨 DPI 縮放
// =============================================================================
TEST(Dpi, LogicalPixelsScaleByTheFactor) {
    const Package pkg = media_component("com.example.x");
    const WidgetAssembly w =
        one_component_widget("w", PixelSize{320, 180}, at(12, 20, 100, 40, ""), pkg);

    WidgetLayout at1;
    ASSERT_TRUE(w.resolve(1.0, at1).ok());
    WidgetLayout at2;
    ASSERT_TRUE(w.resolve(2.0, at2).ok());

    EXPECT_EQ(at2.scale_factor, 2.0);
    EXPECT_EQ(at2.bounds.right, 640);
    EXPECT_EQ(at2.bounds.bottom, 360);
    EXPECT_EQ(at2.components[0].box.left, at1.components[0].box.left * 2);
    EXPECT_EQ(at2.components[0].box.right, at1.components[0].box.right * 2);
    EXPECT_EQ(at2.components[0].scale_factor, 2.0);
}

TEST(Dpi, FractionalScaleKeepsAdjacentComponentsEdgeAligned) {
    // 兩個相鄰格位在 1.5 倍下仍必須邊界重合 —— 若右緣用 `left + scale(width)` 算，
    // 這裡就會出現 1px 的縫或重疊。
    WidgetAssembly w{"w", PixelSize{200, 100}};
    const Package a = media_component("com.example.a");
    const Package b = media_component("com.example.b");
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("l"), at(12, 0, 100, 100, "")).ok());
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("r"), at(112, 0, 88, 100, "")).ok());
    ASSERT_TRUE(w.bind("l", a).ok());
    ASSERT_TRUE(w.bind("r", b).ok());

    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.5, layout).ok());
    EXPECT_EQ(layout.components[0].box.left, 18);    // 12 * 1.5
    EXPECT_EQ(layout.components[0].box.right, 168);  // (12+100) * 1.5
    EXPECT_EQ(layout.components[1].box.left, 168);   // 邊界重合，不留縫
    EXPECT_FALSE(ds::host::boxes_overlap(layout.components[0].box, layout.components[1].box));
}

TEST(Dpi, ScaleFactorComesFromTheKernelNamedScreen) {
    const Package pkg = media_component("com.example.x");
    const WidgetAssembly w =
        one_component_widget("w", PixelSize{100, 50}, at(0, 0, 100, 50, ""), pkg);

    // kernel（E1-17）交出來的是**具名螢幕的縮放係數（純比值）**，不是像素。
    const DpiInfo dpi{{ScreenDpi{"screen.main", "main", 1.0},
                       ScreenDpi{"screen.hidpi", "hidpi", 2.0}}};
    WidgetLayout on_main;
    ASSERT_TRUE(w.resolve(dpi, "screen.main", on_main).ok());
    WidgetLayout on_hidpi;
    ASSERT_TRUE(w.resolve(dpi, "screen.hidpi", on_hidpi).ok());
    EXPECT_EQ(on_main.bounds.right, 100);
    EXPECT_EQ(on_hidpi.bounds.right, 200);

    // 未知螢幕：E1-17 自己保守回 kDefaultScaleFactor（1.0），**不誤縮放**。
    WidgetLayout on_unknown;
    ASSERT_TRUE(w.resolve(dpi, "screen.nope", on_unknown).ok());
    EXPECT_EQ(on_unknown.scale_factor, ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(on_unknown.bounds.right, 100);
}

TEST(Dpi, BadScaleFactorsFallBackToOneInsteadOfProducingGarbage) {
    // K-002 的形狀：壞掉的值不得偽裝成一個看起來正常的版面。
    EXPECT_EQ(ds::host::sane_scale(0.0), ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(ds::host::sane_scale(-2.0), ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(ds::host::sane_scale(std::nan("")), ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(ds::host::sane_scale(std::numeric_limits<double>::infinity()),
              ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(ds::host::sane_scale(1.5), 1.5);

    const Package pkg = media_component("com.example.x");
    const WidgetAssembly w =
        one_component_widget("w", PixelSize{100, 50}, at(0, 0, 100, 50, ""), pkg);
    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(std::nan(""), layout).ok());
    EXPECT_EQ(layout.scale_factor, ds::kernel::kDefaultScaleFactor);
    EXPECT_EQ(layout.bounds.right, 100);  // 不是 0、也不是垃圾值
}

// =============================================================================
// 消費 E9-03 Composition（相容性 / 替換 / 列舉 / 具名錯誤皆由上游負責）
// =============================================================================
TEST(ConsumesE903, SlotInterfaceActuallyFiltersAndTheReasonComesFromUpstream) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("full"), at(0, 0, 50, 50, "")).ok());
    ASSERT_TRUE(w.declare(ds::host::artwork_only_slot("art"), at(50, 0, 50, 50, "")).ok());

    const Package art_only = artwork_component("com.example.artwork");
    // 只有封面的組件不符「封面 + 傳輸控制」的插槽介面 → 帶原因拒絕，狀態不變。
    const auto bad = w.bind("full", art_only);
    EXPECT_EQ(bad.status, ComposeStatus::Incompatible);
    EXPECT_FALSE(bad.message.empty());
    EXPECT_FALSE(w.is_bound("full"));

    // 同一個組件對只要求封面的插槽是相容的。
    EXPECT_TRUE(w.bind("art", art_only).ok());
    EXPECT_TRUE(w.is_bound("art"));
    ASSERT_NE(w.bound_component("art"), nullptr);
    EXPECT_EQ(w.bound_component("art")->manifest.name, std::string("com.example.artwork"));
}

TEST(ConsumesE903, BindSwapUnbindFollowUpstreamStateRules) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    const Package a = media_component("com.example.a");
    const Package b = media_component("com.example.b");
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("s"), at(0, 0, 50, 50, "")).ok());

    EXPECT_EQ(w.bind("nope", a).status, ComposeStatus::UnknownSlot);
    EXPECT_EQ(w.swap("s", a).status, ComposeStatus::SlotNotBound);   // 尚未綁定 → 請用 bind
    EXPECT_EQ(w.unbind("s").status, ComposeStatus::SlotNotBound);

    ASSERT_TRUE(w.bind("s", a).ok());
    EXPECT_EQ(w.bind("s", b).status, ComposeStatus::SlotAlreadyBound);  // 已綁定 → 請用 swap
    ASSERT_TRUE(w.swap("s", b).ok());
    EXPECT_EQ(w.bound_component("s")->manifest.name, std::string("com.example.b"));

    ASSERT_TRUE(w.unbind("s").ok());
    EXPECT_FALSE(w.is_bound("s"));
}

TEST(ConsumesE903, DuplicateAndEmptySlotIdsAreRefused) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("s"), at(0, 0, 10, 10, "")).ok());
    EXPECT_EQ(w.declare(ds::host::media_control_slot("s"), at(10, 0, 10, 10, "")).status,
              ComposeStatus::DuplicateSlot);
    EXPECT_EQ(w.declare(ds::host::media_control_slot(""), at(20, 0, 10, 10, "")).status,
              ComposeStatus::EmptySlotId);
    EXPECT_EQ(w.slot_count(), 1u);
}

TEST(ConsumesE903, CandidateEnumerationGoesStraightToTheUpstreamComposition) {
    WidgetAssembly w{"w", PixelSize{100, 100}};
    ASSERT_TRUE(w.declare(ds::host::media_control_slot("s"), at(0, 0, 10, 10, "")).ok());

    const std::vector<Package> candidates = {artwork_component("com.example.art"),
                                             media_component("com.example.media"),
                                             artwork_component("com.example.art2")};
    // 本層不代理一份相容性邏輯：直接問 E9-03 的組合物件。
    const std::vector<std::size_t> ok = w.composition().compatible_candidates("s", candidates);
    ASSERT_EQ(ok.size(), 1u);
    EXPECT_EQ(ok[0], 1u);
}

TEST(ConsumesE903, UnknownSlotQueriesReturnNullptrNotAFakeEmptyValue) {
    const WidgetAssembly w{"w", PixelSize{100, 100}};
    EXPECT_EQ(w.placement("nope"), nullptr);
    EXPECT_EQ(w.bound_component("nope"), nullptr);
    EXPECT_FALSE(w.has_slot("nope"));
    EXPECT_FALSE(w.is_bound("nope"));
}

// =============================================================================
// 【驗收標準核心】同一組件同時出現在兩個 widget，且呈現型態可不同
// =============================================================================
TEST(SameComponentInTwoWidgets, OneComponentTwoWidgetsTwoPresentations) {
    const Package shared = media_component("com.example.media.control");

    // widget 一：桌面上的大面板，expanded。
    WidgetAssembly home{"widget.media.home", PixelSize{320, 180}};
    ASSERT_TRUE(home.declare(ds::host::media_control_slot("now_playing"),
                             at(12, 12, 296, 120, ds::host::kMediaVariantExpanded))
                    .ok());
    ASSERT_TRUE(home.bind("now_playing", shared).ok());

    // widget 二：停駐列上的細條，compact + 自訂具名參數。
    WidgetAssembly strip{"widget.dock.strip", PixelSize{240, 48}};
    ComponentPlacement compact = at(4, 4, 232, 40, ds::host::kMediaVariantCompact);
    compact.presentation.params["button_px"] = 16.0;
    ASSERT_TRUE(strip.declare(ds::host::media_control_slot("media"), compact).ok());
    ASSERT_TRUE(strip.bind("media", shared).ok());  // **同一個組件物件**

    const std::vector<const WidgetAssembly*> book = {&home, &strip};
    const std::vector<ComponentAppearance> seen =
        ds::host::appearances_of("com.example.media.control", book, 1.0);

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0].widget_id, std::string("widget.media.home"));
    EXPECT_EQ(seen[1].widget_id, std::string("widget.dock.strip"));
    EXPECT_EQ(seen[0].slot_id, std::string("now_playing"));
    EXPECT_EQ(seen[1].slot_id, std::string("media"));

    // 呈現型態不同：具名變體不同、像素框不同。
    EXPECT_NE(seen[0].presentation.variant, seen[1].presentation.variant);
    EXPECT_TRUE(ds::host::presentation_differs(seen[0], seen[1]));
    EXPECT_NE(seen[0].box, seen[1].box);

    // 不存在的組件名回空清單（不回一筆假的現身）。
    EXPECT_TRUE(ds::host::appearances_of("com.example.nope", book, 1.0).empty());
}

TEST(SameComponentInTwoWidgets, PresentationCanBeChangedPerWidgetWithoutTouchingTheBinding) {
    const Package shared = media_component("com.example.media.control");
    WidgetAssembly w =
        one_component_widget("w", PixelSize{200, 100},
                             at(0, 0, 200, 100, ds::host::kMediaVariantExpanded), shared);

    ComponentPresentation next;
    next.variant = ds::host::kMediaVariantCompact;
    next.params["pad_px"] = 8.0;
    ASSERT_TRUE(w.present("main", next).ok());

    // 綁定與版面都沒被動到，只有呈現型態換了。
    ASSERT_NE(w.bound_component("main"), nullptr);
    EXPECT_EQ(w.bound_component("main")->manifest.name,
              std::string("com.example.media.control"));
    EXPECT_EQ(w.placement("main")->origin, (PixelPoint{0, 0}));
    EXPECT_EQ(w.placement("main")->size, (PixelSize{200, 100}));
    EXPECT_EQ(w.placement("main")->presentation.variant,
              std::string(ds::host::kMediaVariantCompact));
    EXPECT_EQ(w.placement("main")->presentation.param("pad_px", -1.0), 8.0);
    EXPECT_EQ(w.placement("main")->presentation.param("absent", -1.0), -1.0);

    EXPECT_EQ(w.present("nope", next).status, ComposeStatus::UnknownSlot);
}

// =============================================================================
// 第二案例：C2-07 媒體控制（真的建一個 widget、真的 refresh()）
// =============================================================================
TEST(MediaCase, C207RenderModelDrivesTheComponentParts) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media.home", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);
    ASSERT_TRUE(c207.has_media());
    ASSERT_TRUE(c207.render_model().artwork.has_source);

    const Package shared = media_component("com.example.media.control");
    const WidgetAssembly home =
        one_component_widget("widget.media.home", PixelSize{320, 180},
                             at(12, 12, 296, 120, ds::host::kMediaVariantExpanded), shared);
    WidgetLayout layout;
    ASSERT_TRUE(home.resolve(1.0, layout).ok());
    ASSERT_EQ(layout.component_count(), 1u);

    const std::vector<MediaPartBox> parts =
        ds::host::media_part_boxes(c207.render_model(), layout.components[0]);

    // expanded：封面 + 四顆按鈕 + 曲名 + 演出者。
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartArtwork), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartTitle), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartArtist), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartButtonPlay), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartButtonPause), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartButtonPrev), nullptr);
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartButtonNext), nullptr);

    // **每一個部件框都在該組件的框之內**（不畫到框外）。
    for (const MediaPartBox& p : parts) {
        EXPECT_TRUE(contains(layout.components[0].box, p.box)) << p.part;
        EXPECT_FALSE(p.box.empty()) << p.part;
    }
}

TEST(MediaCase, TheSameComponentLooksDifferentInTheTwoWidgets) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);

    const Package shared = media_component("com.example.media.control");

    WidgetAssembly home{"widget.media.home", PixelSize{320, 180}};
    ASSERT_TRUE(home.declare(ds::host::media_control_slot("now_playing"),
                             at(12, 12, 296, 120, ds::host::kMediaVariantExpanded))
                    .ok());
    ASSERT_TRUE(home.bind("now_playing", shared).ok());

    WidgetAssembly strip{"widget.dock.strip", PixelSize{240, 48}};
    ASSERT_TRUE(strip.declare(ds::host::media_control_slot("media"),
                              at(4, 4, 232, 40, ds::host::kMediaVariantCompact))
                    .ok());
    ASSERT_TRUE(strip.bind("media", shared).ok());

    WidgetLayout home_layout;
    ASSERT_TRUE(home.resolve(1.0, home_layout).ok());
    WidgetLayout strip_layout;
    ASSERT_TRUE(strip.resolve(1.0, strip_layout).ok());

    const auto expanded =
        ds::host::media_part_boxes(c207.render_model(), home_layout.components[0]);
    const auto compact =
        ds::host::media_part_boxes(c207.render_model(), strip_layout.components[0]);

    // **同一個組件、同一份 C2-07 渲染描述，兩個 widget 的呈現型態不同**：
    // expanded 有封面與四顆按鈕；compact 無封面、只有一顆按鈕。
    EXPECT_EQ(expanded.size(), 7u) << testing::PrintToString(part_ids(expanded));
    EXPECT_EQ(compact.size(), 2u) << testing::PrintToString(part_ids(compact));
    EXPECT_NE(ds::host::find_part(expanded, ds::host::kMediaPartArtwork), nullptr);
    EXPECT_EQ(ds::host::find_part(compact, ds::host::kMediaPartArtwork), nullptr);
    EXPECT_EQ(ds::host::find_part(compact, ds::host::kMediaPartButtonPrev), nullptr);
    EXPECT_EQ(ds::host::find_part(compact, ds::host::kMediaPartButtonNext), nullptr);
    // 播放中 → compact 那一顆是 pause。
    EXPECT_NE(ds::host::find_part(compact, ds::host::kMediaPartButtonPause), nullptr);
    EXPECT_EQ(ds::host::find_part(compact, ds::host::kMediaPartButtonPlay), nullptr);

    for (const MediaPartBox& p : compact) {
        EXPECT_TRUE(contains(strip_layout.components[0].box, p.box)) << p.part;
    }
}

TEST(MediaCase, CompactButtonFollowsThePlaybackState) {
    MediaFixture f;
    MediaMetadata paused = playing_song();
    paused.state = PlaybackState::Paused;
    f.publish(paused);
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);
    ASSERT_EQ(c207.render_model().playback, PlaybackState::Paused);

    const Package shared = media_component("com.example.media.control");
    const WidgetAssembly strip =
        one_component_widget("widget.dock.strip", PixelSize{240, 48},
                             at(4, 4, 232, 40, ds::host::kMediaVariantCompact), shared);
    WidgetLayout layout;
    ASSERT_TRUE(strip.resolve(1.0, layout).ok());

    const auto parts = ds::host::media_part_boxes(c207.render_model(), layout.components[0]);
    // 暫停中 → 那一顆是 play（不是 pause）。
    EXPECT_NE(ds::host::find_part(parts, ds::host::kMediaPartButtonPlay), nullptr);
    EXPECT_EQ(ds::host::find_part(parts, ds::host::kMediaPartButtonPause), nullptr);
}

TEST(MediaCase, NoMediaMeansNoTextPartsInsteadOfEmptyBoxes) {
    MediaFixture f;  // 不 publish：registry 上沒有 "media.nowplaying" 指標 → 降級為無媒體
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);
    ASSERT_FALSE(c207.has_media());
    ASSERT_FALSE(c207.render_model().artwork.has_source);

    const Package shared = media_component("com.example.media.control");
    const WidgetAssembly home =
        one_component_widget("w", PixelSize{320, 180},
                             at(12, 12, 296, 120, ds::host::kMediaVariantExpanded), shared);
    WidgetLayout layout;
    ASSERT_TRUE(home.resolve(1.0, layout).ok());

    const auto parts = ds::host::media_part_boxes(c207.render_model(), layout.components[0]);
    // 無封面來源 → 沒有 artwork 框；無媒體 → 沒有文字框。按鈕仍在（控制面板本身還存在）。
    EXPECT_EQ(ds::host::find_part(parts, ds::host::kMediaPartArtwork), nullptr);
    EXPECT_EQ(ds::host::find_part(parts, ds::host::kMediaPartTitle), nullptr);
    EXPECT_EQ(ds::host::find_part(parts, ds::host::kMediaPartArtist), nullptr);
    EXPECT_EQ(parts.size(), 4u) << testing::PrintToString(part_ids(parts));
}

TEST(MediaCase, PartBoxesScaleWithDpiToo) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);

    const Package shared = media_component("com.example.media.control");
    const WidgetAssembly home =
        one_component_widget("w", PixelSize{320, 180},
                             at(12, 12, 296, 120, ds::host::kMediaVariantExpanded), shared);

    WidgetLayout at1;
    ASSERT_TRUE(home.resolve(1.0, at1).ok());
    WidgetLayout at2;
    ASSERT_TRUE(home.resolve(2.0, at2).ok());

    const auto p1 = ds::host::media_part_boxes(c207.render_model(), at1.components[0]);
    const auto p2 = ds::host::media_part_boxes(c207.render_model(), at2.components[0]);
    ASSERT_EQ(p1.size(), p2.size());
    const MediaPartBox* a1 = ds::host::find_part(p1, ds::host::kMediaPartArtwork);
    const MediaPartBox* a2 = ds::host::find_part(p2, ds::host::kMediaPartArtwork);
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(a2, nullptr);
    // 封面邊長在 2 倍下必須明顯變大（內距與按鈕尺度也一起縮放，故不是剛好 2 倍）。
    EXPECT_GT(a2->box.width(), a1->box.width() * 3 / 2);
    for (const MediaPartBox& p : p2) {
        EXPECT_TRUE(contains(at2.components[0].box, p.box)) << p.part;
    }
}

TEST(MediaCase, InvisiblePresentationYieldsNoPartsAndUnknownVariantFallsBackToExpanded) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);

    const Package shared = media_component("com.example.media.control");
    WidgetAssembly w = one_component_widget(
        "w", PixelSize{320, 180}, at(12, 12, 296, 120, "no-such-variant"), shared);

    WidgetLayout layout;
    ASSERT_TRUE(w.resolve(1.0, layout).ok());
    // 未知變體 → 歸到 expanded（不靜默變成空白）。
    EXPECT_EQ(ds::host::media_part_boxes(c207.render_model(), layout.components[0]).size(), 7u);

    ComponentPresentation hidden;
    hidden.variant = ds::host::kMediaVariantExpanded;
    hidden.visible = false;
    ASSERT_TRUE(w.present("main", hidden).ok());
    WidgetLayout hidden_layout;
    ASSERT_TRUE(w.resolve(1.0, hidden_layout).ok());
    // 不顯示 → 零部件，但插槽仍是綁定的（不是未綁定）。
    EXPECT_TRUE(
        ds::host::media_part_boxes(c207.render_model(), hidden_layout.components[0]).empty());
    EXPECT_TRUE(w.is_bound("main"));
}

TEST(MediaCase, NamedPresentationParamsChangeTheGeometry) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);

    const Package shared = media_component("com.example.media.control");
    WidgetAssembly w = one_component_widget(
        "w", PixelSize{320, 180}, at(0, 0, 320, 180, ds::host::kMediaVariantCompact), shared);

    WidgetLayout before;
    ASSERT_TRUE(w.resolve(1.0, before).ok());
    const auto small = ds::host::media_part_boxes(c207.render_model(), before.components[0]);

    ComponentPresentation bigger;
    bigger.variant = ds::host::kMediaVariantCompact;
    bigger.params["button_px"] = 60.0;
    ASSERT_TRUE(w.present("main", bigger).ok());
    WidgetLayout after;
    ASSERT_TRUE(w.resolve(1.0, after).ok());
    const auto large = ds::host::media_part_boxes(c207.render_model(), after.components[0]);

    const MediaPartBox* b_small = ds::host::find_part(small, ds::host::kMediaPartButtonPause);
    const MediaPartBox* b_large = ds::host::find_part(large, ds::host::kMediaPartButtonPause);
    ASSERT_NE(b_small, nullptr);
    ASSERT_NE(b_large, nullptr);
    EXPECT_GT(b_large->box.width(), b_small->box.width());

    // 負 / NaN 的具名參數不得變成負尺寸（夾限於 0，不是 UB、也不是負框）。
    ComponentPresentation nonsense;
    nonsense.variant = ds::host::kMediaVariantCompact;
    nonsense.params["button_px"] = -40.0;
    nonsense.params["pad_px"] = std::nan("");
    ASSERT_TRUE(w.present("main", nonsense).ok());
    WidgetLayout weird;
    ASSERT_TRUE(w.resolve(1.0, weird).ok());
    for (const MediaPartBox& p : ds::host::media_part_boxes(c207.render_model(),
                                                            weird.components[0])) {
        EXPECT_FALSE(p.box.empty()) << p.part;
        EXPECT_TRUE(contains(weird.components[0].box, p.box)) << p.part;
    }
}

TEST(MediaCase, DegenerateComponentBoxYieldsNoParts) {
    MediaFixture f;
    f.publish(playing_song());
    MediaControlWidget c207{"widget.media", f.backend, f.layers, f.registry, f.actuator};
    ASSERT_EQ(c207.refresh(), MediaControlStatus::Ok);

    ds::host::ResolvedComponent degenerate;
    degenerate.slot_id = "s";
    degenerate.component_name = "com.example.media.control";
    degenerate.box = PaintBox{10, 10, 10, 20};  // 寬度 0
    EXPECT_TRUE(ds::host::media_part_boxes(c207.render_model(), degenerate).empty());
}

// =============================================================================
// 具名狀態字串（診斷訊息不得是空指標 / 空字串）
// =============================================================================
TEST(Diagnostics, EveryComposeStatusHasAName) {
    const ComposeStatus all[] = {
        ComposeStatus::Ok,          ComposeStatus::EmptySlotId,
        ComposeStatus::DuplicateSlot, ComposeStatus::UnknownSlot,
        ComposeStatus::DegenerateSize, ComposeStatus::OutOfBounds,
        ComposeStatus::SlotAlreadyBound, ComposeStatus::SlotNotBound,
        ComposeStatus::Incompatible, ComposeStatus::EmptyWidget,
        ComposeStatus::IncompleteComposition,
    };
    for (const ComposeStatus s : all) {
        const char* name = ds::host::to_string(s);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::string(name).size(), 0u);
    }
}
