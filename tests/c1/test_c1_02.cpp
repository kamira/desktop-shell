// tests/c1/test_c1_02.cpp — C1-02 立繪 profile（角色立繪 / 桌寵靜態立繪）（gtest）
//
// 涵蓋：組裝（建構預設、alpha_supported 反映後端能力）、load_portrait（E7-01 宣告式定義：
// 程式化 Value 與自文字 parse 兩路徑、圖層 / alpha / 位置 / 表情欄位套用）、E1-03 透明外形
// （能力閘控 Unsupported 降級、opaque 模式）、E1-01 具名圖層（載入後指派 / 卸載後移除 /
// 多立繪共用堆疊互不干擾）、E1-08 自由拖曳 + 位置記憶（begin/drag_to/end 提交、cancel 還原、
// resolve_live、save/load round-trip）、E4-06 具名表情切換（add/switch/remove/current/list、
// 切換更新命中形狀）、E5-14 區域點擊（set_regions + inject_click + on_region_click 回呼帶
// 具名區域 id + 參數、未命中子區域仍正常派發）、以及各類無效輸入（非 map / 型別錯 / 具名值
// 無效 / 空 id / 重複載入 / 重複表情 / initial_expression 無匹配 / 未載入即操作）。
#include "portrait_profile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using ds::elements::ImageDimensions;
using ds::elements::ImageElement;
using ds::elements::ImageStatus;
using ds::elements::MemoryImageSource;
using ds::elements::ScaleMode;

using ds::events::MouseButton;
using ds::events::RegionEvent;
using ds::events::RouteStatus;

using ds::format::Document;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::Value;

using ds::kernel::AlphaMode;
using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::DragStatus;
using ds::kernel::LayerStack;
using ds::kernel::LocalPoint;
using ds::kernel::make_rect;
using ds::kernel::NamedRegionMap;
using ds::kernel::NullKernelBackend;
using ds::kernel::RegionParams;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SurfaceLayer;

using ds::profiles::PortraitProfile;
using ds::profiles::PortraitState;
using ds::profiles::PortraitStatus;

namespace {

// 一份典型的宣告式立繪定義（E7-01 Value / Map，等同 Document::root），含一個具名表情。
Value make_definition(const std::string& layer, const std::string& alpha_mode, double opacity,
                      const std::string& anchor, double dx, double dy) {
    return Value::map({
        {"layer", Value::string(layer)},
        {"alpha", Value::map({{"mode", Value::string(alpha_mode)},
                              {"opacity", Value::number(opacity)}})},
        {"position", Value::map({{"anchor", Value::string(anchor)},
                                {"dx", Value::number(dx)},
                                {"dy", Value::number(dy)}})},
        {"expressions", Value::list({
             Value::map({{"name", Value::string("idle")},
                        {"source", Value::string("res://miku_idle")},
                        {"width", Value::integer(20)},
                        {"height", Value::integer(20)}}),
             Value::map({{"name", Value::string("happy")},
                        {"source", Value::string("res://miku_happy")},
                        {"width", Value::integer(20)},
                        {"height", Value::integer(20)},
                        {"scale", Value::string("fill")}}),
         })},
        {"initial_expression", Value::string("idle")},
    });
}

}  // namespace

// -----------------------------------------------------------------------------
// 組裝正確 / 建構預設
// -----------------------------------------------------------------------------

TEST(PortraitProfile, ConstructedUnloadedWithDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    EXPECT_EQ(portrait.state(), PortraitState::Unloaded);
    EXPECT_FALSE(portrait.is_loaded());
    EXPECT_EQ(portrait.id(), "portrait.miku");
    EXPECT_EQ(portrait.layer(), SurfaceLayer::Normal);
    EXPECT_EQ(portrait.layer_name(), std::string("layer.normal"));
    EXPECT_EQ(portrait.alpha().mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(portrait.alpha().opacity, 1.0f);
    EXPECT_FALSE(portrait.assigned_to_layer());
    EXPECT_TRUE(portrait.remembered_position() == nullptr);
    EXPECT_TRUE(portrait.expressions().empty());
    EXPECT_TRUE(portrait.current_expression().empty());
    EXPECT_TRUE(portrait.current_image() == nullptr);
}

TEST(PortraitProfile, AlphaSupportedReflectsBackendCapability) {
    NullKernelBackend capable{alpha_capable_matrix()};
    NullKernelBackend incapable{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile a{"portrait.a", capable, layers};
    PortraitProfile b{"portrait.b", incapable, layers};
    EXPECT_TRUE(a.alpha_supported());   // E1-03 能力可用
    EXPECT_FALSE(b.alpha_supported());  // 保守 defaults 未宣告 per-pixel alpha
}

// -----------------------------------------------------------------------------
// load_portrait — E7-01 宣告式定義（程式化 Value）+ 跨擴充點實體化
// -----------------------------------------------------------------------------

TEST(PortraitProfile, LoadPortraitAppliesDeclarativeConfigAndRealizes) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    Value def = make_definition("overlay", "per-pixel", 0.75, "bottom-right", 0.0, 0.0);
    EXPECT_EQ(portrait.load_portrait(def), PortraitStatus::Ok);

    EXPECT_TRUE(portrait.is_loaded());
    EXPECT_EQ(portrait.state(), PortraitState::Loaded);
    // E1-01 圖層
    EXPECT_EQ(portrait.layer(), SurfaceLayer::Overlay);
    EXPECT_EQ(portrait.layer_name(), std::string("layer.overlay"));
    EXPECT_TRUE(portrait.assigned_to_layer());
    // E1-03 透明外形
    EXPECT_EQ(portrait.alpha().mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(portrait.alpha().opacity, 0.75f);
    // 後端實體 surface 已建立
    EXPECT_TRUE(backend.has_surface("portrait.miku"));
    // E1-08 初始記憶位置
    const AnchorSpec* pos = portrait.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::BottomRight);
    // E4-06 + E4-02：兩個具名表情已登記，預設切至 initial_expression
    EXPECT_EQ(portrait.expressions().size(), 2u);
    EXPECT_TRUE(portrait.has_expression("idle"));
    EXPECT_TRUE(portrait.has_expression("happy"));
    EXPECT_EQ(portrait.current_expression(), "idle");
    ASSERT_TRUE(portrait.current_image() != nullptr);
    EXPECT_EQ(portrait.current_image()->source_reference(), "res://miku_idle");
    EXPECT_EQ(portrait.current_image()->target(), "portrait.miku");
}

TEST(PortraitProfile, LoadPortraitFromParsedTextDocument) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.dog", backend, layers};

    const std::string text =
        "format_version: 1.0\n"
        "layer: topmost\n"
        "alpha:\n"
        "  mode: per-pixel\n"
        "  opacity: 0.5\n"
        "position:\n"
        "  anchor: top-left\n"
        "  dx: 0.1\n"
        "  dy: 0.2\n";
    ParseResult r = parse(text);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(portrait.load_portrait(r.document().root), PortraitStatus::Ok);

    EXPECT_EQ(portrait.layer(), SurfaceLayer::Topmost);
    EXPECT_FLOAT_EQ(portrait.alpha().opacity, 0.5f);
    const AnchorSpec* pos = portrait.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::TopLeft);
    EXPECT_FLOAT_EQ(pos->offset.dx, 0.1f);
    EXPECT_FLOAT_EQ(pos->offset.dy, 0.2f);
    EXPECT_TRUE(portrait.expressions().empty());  // 無 expressions 欄位：合法，僅外形
}

TEST(PortraitProfile, LoadPortraitEmptyMapUsesDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    EXPECT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(portrait.layer(), SurfaceLayer::Normal);
    EXPECT_EQ(portrait.alpha().mode, AlphaMode::PerPixel);
}

TEST(PortraitProfile, LoadPortraitWithoutInitialExpressionDefaultsToFirst) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    Value def = Value::map({{"expressions",
                             Value::list({Value::map({{"name", Value::string("a")},
                                                     {"source", Value::string("res://a")},
                                                     {"width", Value::integer(10)},
                                                     {"height", Value::integer(10)}}),
                                         Value::map({{"name", Value::string("b")},
                                                     {"source", Value::string("res://b")},
                                                     {"width", Value::integer(10)},
                                                     {"height", Value::integer(10)}})})}});
    ASSERT_EQ(portrait.load_portrait(def), PortraitStatus::Ok);
    EXPECT_EQ(portrait.current_expression(), "a");
}

// -----------------------------------------------------------------------------
// E1-03 透明外形 — 能力閘控 / Unsupported 降級
// -----------------------------------------------------------------------------

TEST(PortraitProfile, LoadPortraitUnsupportedWhenAlphaCapabilityUnavailable) {
    NullKernelBackend backend{CapabilityMatrix::defaults()};  // 無 per-pixel alpha
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    EXPECT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Unsupported);
    EXPECT_FALSE(portrait.is_loaded());
    EXPECT_FALSE(backend.has_surface("portrait.miku"));  // 未建立任何 surface（降級路徑）
    EXPECT_FALSE(portrait.assigned_to_layer());            // 未觸碰圖層堆疊
    EXPECT_TRUE(portrait.expressions().empty());           // 未觸碰表情登錄
}

TEST(PortraitProfile, OpaqueAlphaModeApplied) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    Value def = Value::map({{"alpha", Value::map({{"mode", Value::string("opaque")}})}});
    EXPECT_EQ(portrait.load_portrait(def), PortraitStatus::Ok);
    EXPECT_EQ(portrait.alpha().mode, AlphaMode::Opaque);
}

// -----------------------------------------------------------------------------
// E1-01 具名圖層 — 卸載後移除 / 多立繪共用互不干擾
// -----------------------------------------------------------------------------

TEST(PortraitProfile, UnloadTearsDownBackendAndLayer) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);
    ASSERT_TRUE(backend.has_surface("portrait.miku"));
    ASSERT_TRUE(portrait.assigned_to_layer());
    ASSERT_FALSE(portrait.expressions().empty());

    EXPECT_TRUE(portrait.unload());
    EXPECT_EQ(portrait.state(), PortraitState::Unloaded);
    EXPECT_FALSE(backend.has_surface("portrait.miku"));  // 後端 surface 銷毀
    EXPECT_FALSE(portrait.assigned_to_layer());            // 圖層指派移除
    EXPECT_TRUE(portrait.expressions().empty());           // 表情登錄清空
    EXPECT_TRUE(portrait.current_expression().empty());
    EXPECT_FALSE(portrait.has_regions());
    EXPECT_FALSE(portrait.unload());  // 重複卸載 no-op
}

TEST(PortraitProfile, MultiplePortraitsShareLayerStackWithoutInterference) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile miku{"portrait.miku", backend, layers};
    PortraitProfile rin{"portrait.rin", backend, layers};

    ASSERT_EQ(miku.load_portrait(Value::map({{"layer", Value::string("overlay")}})),
              PortraitStatus::Ok);
    ASSERT_EQ(rin.load_portrait(Value::map({{"layer", Value::string("normal")}})),
              PortraitStatus::Ok);
    EXPECT_TRUE(miku.assigned_to_layer());
    EXPECT_TRUE(rin.assigned_to_layer());
    EXPECT_EQ(layers.size(), 2u);

    EXPECT_TRUE(miku.unload());
    EXPECT_FALSE(miku.assigned_to_layer());
    EXPECT_TRUE(rin.assigned_to_layer());  // rin 不受 miku 卸載干擾
    EXPECT_EQ(layers.size(), 1u);
}

// -----------------------------------------------------------------------------
// E1-08 自由拖曳 + 位置記憶（含 resolve_live）
// -----------------------------------------------------------------------------

TEST(PortraitProfile, ResolveLiveProducesConcretePlacement) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    ResolvedPlacement out;
    EXPECT_EQ(portrait.resolve_live(Size{100.0f, 100.0f}, Size{20.0f, 20.0f}, out),
              AnchorStatus::Ok);
    EXPECT_FLOAT_EQ(out.x, 40.0f);
    EXPECT_FLOAT_EQ(out.y, 40.0f);
}

TEST(PortraitProfile, ResolveLiveInvalidWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ResolvedPlacement out;
    EXPECT_EQ(portrait.resolve_live(Size{100.0f, 100.0f}, Size{20.0f, 20.0f}, out),
              AnchorStatus::Invalid);
}

TEST(PortraitProfile, PlaceUpdatesRememberedPosition) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    AnchorSpec spec;
    spec.anchor = Anchor::TopRight;
    spec.offset.dx = -0.05f;
    EXPECT_EQ(portrait.place(spec), DragStatus::Ok);
    const AnchorSpec* pos = portrait.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::TopRight);
    EXPECT_FLOAT_EQ(pos->offset.dx, -0.05f);
}

TEST(PortraitProfile, PlaceInvalidWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    EXPECT_EQ(portrait.place(AnchorSpec{}), DragStatus::Invalid);
}

TEST(PortraitProfile, DragCommitUpdatesMemory) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    EXPECT_EQ(portrait.begin_drag(), DragStatus::Ok);
    EXPECT_TRUE(portrait.is_dragging());
    EXPECT_EQ(portrait.begin_drag(), DragStatus::AlreadyDragging);

    AnchorSpec target;
    target.anchor = Anchor::BottomLeft;
    EXPECT_EQ(portrait.drag_to(target), DragStatus::Ok);
    const AnchorSpec* live = portrait.live_position();
    ASSERT_TRUE(live != nullptr);
    EXPECT_EQ(live->anchor, Anchor::BottomLeft);
    EXPECT_EQ(portrait.remembered_position()->anchor, Anchor::Center);  // 尚未提交

    EXPECT_EQ(portrait.end_drag(), DragStatus::Ok);
    EXPECT_FALSE(portrait.is_dragging());
    EXPECT_EQ(portrait.remembered_position()->anchor, Anchor::BottomLeft);  // 放開後記住
}

TEST(PortraitProfile, DragCancelRestoresMemory) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    ASSERT_EQ(portrait.begin_drag(), DragStatus::Ok);
    AnchorSpec target;
    target.anchor = Anchor::TopRight;
    ASSERT_EQ(portrait.drag_to(target), DragStatus::Ok);
    EXPECT_EQ(portrait.cancel_drag(), DragStatus::Ok);
    EXPECT_FALSE(portrait.is_dragging());
    EXPECT_EQ(portrait.remembered_position()->anchor, Anchor::Center);
}

TEST(PortraitProfile, DragOpsNotDraggingWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    EXPECT_EQ(portrait.begin_drag(), DragStatus::Invalid);
    EXPECT_EQ(portrait.drag_to(AnchorSpec{}), DragStatus::NotDragging);
    EXPECT_EQ(portrait.end_drag(), DragStatus::NotDragging);
    EXPECT_EQ(portrait.cancel_drag(), DragStatus::NotDragging);
}

TEST(PortraitProfile, CannotPlaceWhileDragging) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    ASSERT_EQ(portrait.begin_drag(), DragStatus::Ok);
    EXPECT_EQ(portrait.place(AnchorSpec{}), DragStatus::Invalid);
}

TEST(PortraitProfile, SaveAndLoadPositionRoundTrip) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile src{"portrait.miku", backend, layers};
    ASSERT_EQ(src.load_portrait(make_definition("overlay", "per-pixel", 1.0, "bottom-right",
                                                0.03, 0.07)),
              PortraitStatus::Ok);

    const std::string saved = src.save_position();
    EXPECT_NE(saved.find("portrait.miku"), std::string::npos);
    EXPECT_NE(saved.find("bottom-right"), std::string::npos);

    NullKernelBackend backend2{alpha_capable_matrix()};
    LayerStack layers2{CapabilityMatrix::defaults()};
    PortraitProfile dst{"portrait.miku", backend2, layers2};
    EXPECT_EQ(dst.load_position(saved), DragStatus::Ok);
    const AnchorSpec* pos = dst.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::BottomRight);
    EXPECT_FLOAT_EQ(pos->offset.dx, 0.03f);
    EXPECT_FLOAT_EQ(pos->offset.dy, 0.07f);
}

TEST(PortraitProfile, LoadPositionInvalidTextRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    EXPECT_EQ(portrait.load_position("not a valid document without version"), DragStatus::Invalid);
}

// -----------------------------------------------------------------------------
// E4-06 具名表情切換（+ E4-02 圖片渲染描述）
// -----------------------------------------------------------------------------

TEST(PortraitProfile, AddAndSwitchExpressionProgrammatically) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    EXPECT_TRUE(portrait.current_expression().empty());

    MemoryImageSource idle_src("res://idle", ImageDimensions{30, 40});
    ImageElement idle;
    ASSERT_EQ(idle.set_source(idle_src), ImageStatus::Ok);
    EXPECT_TRUE(portrait.add_expression("idle", idle));

    MemoryImageSource happy_src("res://happy", ImageDimensions{30, 40});
    ImageElement happy;
    ASSERT_EQ(happy.set_source(happy_src), ImageStatus::Ok);
    EXPECT_TRUE(portrait.add_expression("happy", happy));

    EXPECT_EQ(portrait.expressions().size(), 2u);
    EXPECT_TRUE(portrait.current_expression().empty());  // 新增不自動切換

    EXPECT_TRUE(portrait.switch_expression("happy"));
    EXPECT_EQ(portrait.current_expression(), "happy");
    ASSERT_TRUE(portrait.current_image() != nullptr);
    EXPECT_EQ(portrait.current_image()->source_reference(), "res://happy");

    EXPECT_TRUE(portrait.switch_expression("idle"));
    EXPECT_EQ(portrait.current_expression(), "idle");
}

TEST(PortraitProfile, AddExpressionRejectsDuplicateEmptyOrSourceless) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);

    MemoryImageSource src("res://idle", ImageDimensions{10, 10});
    ImageElement idle;
    ASSERT_EQ(idle.set_source(src), ImageStatus::Ok);
    EXPECT_TRUE(portrait.add_expression("idle", idle));
    EXPECT_FALSE(portrait.add_expression("idle", idle));   // 重複具名表情
    EXPECT_FALSE(portrait.add_expression("", idle));       // 空 name
    EXPECT_FALSE(portrait.add_expression("blank", ImageElement{}));  // 無來源
    EXPECT_EQ(portrait.expressions().size(), 1u);
}

TEST(PortraitProfile, ExpressionOpsFalseWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    MemoryImageSource src("res://idle", ImageDimensions{10, 10});
    ImageElement idle;
    ASSERT_EQ(idle.set_source(src), ImageStatus::Ok);
    EXPECT_FALSE(portrait.add_expression("idle", idle));
    EXPECT_FALSE(portrait.switch_expression("idle"));
    EXPECT_FALSE(portrait.remove_expression("idle"));
}

TEST(PortraitProfile, RemoveCurrentExpressionClearsCurrent) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);
    ASSERT_EQ(portrait.current_expression(), "idle");

    EXPECT_TRUE(portrait.remove_expression("idle"));
    EXPECT_TRUE(portrait.current_expression().empty());
    EXPECT_FALSE(portrait.has_expression("idle"));
    EXPECT_TRUE(portrait.current_image() == nullptr);
    EXPECT_FALSE(portrait.remove_expression("idle"));  // 未知名：不崩潰
}

TEST(PortraitProfile, SwitchExpressionUnknownNameRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    EXPECT_FALSE(portrait.switch_expression("no-such-expression"));
    EXPECT_TRUE(portrait.current_expression().empty());
}

// -----------------------------------------------------------------------------
// E5-14 區域點擊 — 命中具名子區域 / 未命中仍正常派發
// -----------------------------------------------------------------------------

TEST(PortraitProfile, RegionClickCarriesNamedRegionIdAndParams) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);  // idle 表情 20x20，已切為目前表情 → 命中形狀已是 20x20

    // Shape::Rect 恆以本地原點 (0,0) 為左上角，故兩個矩形無法互不重疊地各據一角；改以
    // 「body 鋪滿全身、head 是疊在其上的一個小圓」表達（E1-05 慣例：重疊時後加入者為上）。
    NamedRegionMap regions;
    RegionParams body_params;
    body_params["part"] = std::string("body");
    ASSERT_TRUE(regions.add_region("body", make_rect(20.0f, 20.0f), body_params));  // 先加入：鋪底
    RegionParams head_params;
    head_params["part"] = std::string("head");
    ASSERT_TRUE(regions.add_region("head", ds::kernel::make_circle(LocalPoint{5.0f, 5.0f}, 4.0f),
                                    head_params));  // 後加入：疊在 body 之上，命中優先
    EXPECT_TRUE(portrait.set_regions(std::move(regions)));
    EXPECT_TRUE(portrait.has_regions());

    std::vector<RegionEvent> received;
    portrait.on_region_click([&](const RegionEvent& e) { received.push_back(e); });

    // (5,5) 同時落在 body 矩形與 head 圓形內；head 後加入 → 命中優先（E1-05 加入序慣例）。
    // inject_click() 於同位置合成 Down + Up + Click 三個事件（E5-01 慣例），故取最後一筆斷言
    // （與 E5-14 `OverlappingRegionsLaterAddedWins` 同慣例）。
    const RouteStatus st = portrait.inject_click(LocalPoint{5.0f, 5.0f});
    EXPECT_EQ(st, RouteStatus::Hit);
    ASSERT_FALSE(received.empty());
    EXPECT_EQ(received.back().mouse.target, "portrait.miku");
    EXPECT_TRUE(received.back().region_hit);
    EXPECT_EQ(received.back().region_name, "head");
    ASSERT_TRUE(received.back().region_params.count("part"));
    EXPECT_EQ(std::get<std::string>(received.back().region_params.at("part")), "head");
}

TEST(PortraitProfile, RegionClickMissDispatchesWithoutRegionInfo) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);

    std::vector<RegionEvent> received;
    portrait.on_region_click([&](const RegionEvent& e) { received.push_back(e); });

    // 未登記任何子區域：命中 surface 本體，但不帶區域資訊。inject_click() 合成 Down+Up+Click
    // 三個事件，每一筆皆應不帶區域資訊。
    const RouteStatus st = portrait.inject_click(LocalPoint{2.0f, 2.0f});
    EXPECT_EQ(st, RouteStatus::Hit);
    ASSERT_FALSE(received.empty());
    for (const auto& e : received) {
        EXPECT_FALSE(e.region_hit);
        EXPECT_TRUE(e.region_name.empty());
    }
}

TEST(PortraitProfile, RegionClickOutsideSurfaceIsNoHit) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);  // 20x20 命中形狀

    const RouteStatus st = portrait.inject_click(LocalPoint{999.0f, 999.0f});
    EXPECT_EQ(st, RouteStatus::NoHit);
}

TEST(PortraitProfile, SwitchExpressionRefreshesHitShapeBounds) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);  // 尚無表情 → 1x1 佔位形狀

    // 尚無表情：預設 1x1 命中形狀，(5,5) 落於形狀外 → NoHit。
    EXPECT_EQ(portrait.inject_click(LocalPoint{5.0f, 5.0f}), RouteStatus::NoHit);

    MemoryImageSource src("res://idle", ImageDimensions{50, 60});
    ImageElement idle;
    ASSERT_EQ(idle.set_source(src), ImageStatus::Ok);
    ASSERT_TRUE(portrait.add_expression("idle", idle));
    ASSERT_TRUE(portrait.switch_expression("idle"));

    // 切換後命中形狀已擴為 50x60 → (5,5) 落於形狀內 → Hit。
    EXPECT_EQ(portrait.inject_click(LocalPoint{5.0f, 5.0f}), RouteStatus::Hit);
}

TEST(PortraitProfile, UnsubscribeRegionClickStopsDelivery) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(make_definition("normal", "per-pixel", 1.0, "center", 0, 0)),
              PortraitStatus::Ok);

    int count = 0;
    const auto sub = portrait.on_region_click([&](const RegionEvent&) { ++count; });
    // 單次 inject_click() 合成 Down+Up+Click 三個事件（E5-01 慣例）。
    portrait.inject_click(LocalPoint{1.0f, 1.0f});
    const int count_after_first_click = count;
    EXPECT_TRUE(count_after_first_click > 0);
    EXPECT_TRUE(portrait.unsubscribe_region_click(sub));
    portrait.inject_click(LocalPoint{1.0f, 1.0f});
    EXPECT_EQ(count, count_after_first_click);  // 取消訂閱後不再收到
    EXPECT_FALSE(portrait.unsubscribe_region_click(sub));  // 重複取消 no-op
}

// -----------------------------------------------------------------------------
// 無效輸入 — 結構化拒絕，不靜默
// -----------------------------------------------------------------------------

TEST(PortraitProfile, LoadPortraitRejectsNonMap) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    EXPECT_EQ(portrait.load_portrait(Value::string("nope")), PortraitStatus::Invalid);
    EXPECT_FALSE(portrait.is_loaded());
    EXPECT_FALSE(backend.has_surface("portrait.miku"));
}

TEST(PortraitProfile, LoadPortraitRejectsInvalidNamedValues) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    // 無效圖層名
    EXPECT_EQ(portrait.load_portrait(Value::map({{"layer", Value::string("nowhere")}})),
              PortraitStatus::Invalid);
    // 無效 anchor 名
    EXPECT_EQ(portrait.load_portrait(Value::map(
                  {{"position", Value::map({{"anchor", Value::string("middle")}})}})),
              PortraitStatus::Invalid);
    // 型別錯：layer 應為字串
    EXPECT_EQ(portrait.load_portrait(Value::map({{"layer", Value::number(3)}})),
              PortraitStatus::Invalid);
    // 型別錯：opacity 應為數字
    EXPECT_EQ(portrait.load_portrait(
                  Value::map({{"alpha", Value::map({{"opacity", Value::string("x")}})}})),
              PortraitStatus::Invalid);
    // 型別錯：alpha 應為 map
    EXPECT_EQ(portrait.load_portrait(Value::map({{"alpha", Value::string("x")}})),
              PortraitStatus::Invalid);
    // 型別錯：expressions 應為 list
    EXPECT_EQ(portrait.load_portrait(Value::map({{"expressions", Value::string("x")}})),
              PortraitStatus::Invalid);
    EXPECT_FALSE(portrait.is_loaded());  // 全程未改狀態
    EXPECT_FALSE(backend.has_surface("portrait.miku"));
}

TEST(PortraitProfile, LoadPortraitRejectsMalformedExpressions) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};

    // 缺 source
    EXPECT_EQ(portrait.load_portrait(Value::map(
                  {{"expressions", Value::list({Value::map({{"name", Value::string("idle")},
                                                            {"width", Value::integer(10)},
                                                            {"height", Value::integer(10)}})})}})),
              PortraitStatus::Invalid);
    // 非正尺寸
    EXPECT_EQ(portrait.load_portrait(Value::map(
                  {{"expressions",
                   Value::list({Value::map({{"name", Value::string("idle")},
                                            {"source", Value::string("res://idle")},
                                            {"width", Value::integer(0)},
                                            {"height", Value::integer(10)}})})}})),
              PortraitStatus::Invalid);
    // 重複具名表情 id
    EXPECT_EQ(
        portrait.load_portrait(Value::map(
            {{"expressions",
             Value::list({Value::map({{"name", Value::string("idle")},
                                      {"source", Value::string("res://a")},
                                      {"width", Value::integer(10)},
                                      {"height", Value::integer(10)}}),
                         Value::map({{"name", Value::string("idle")},
                                    {"source", Value::string("res://b")},
                                    {"width", Value::integer(10)},
                                    {"height", Value::integer(10)}})})}})),
        PortraitStatus::Invalid);
    // 無效縮放模式名
    EXPECT_EQ(portrait.load_portrait(Value::map(
                  {{"expressions",
                   Value::list({Value::map({{"name", Value::string("idle")},
                                            {"source", Value::string("res://a")},
                                            {"width", Value::integer(10)},
                                            {"height", Value::integer(10)},
                                            {"scale", Value::string("zoom")}})})}})),
              PortraitStatus::Invalid);
    // initial_expression 無匹配
    EXPECT_EQ(
        portrait.load_portrait(Value::map(
            {{"expressions",
             Value::list({Value::map({{"name", Value::string("idle")},
                                      {"source", Value::string("res://a")},
                                      {"width", Value::integer(10)},
                                      {"height", Value::integer(10)}})})},
            {"initial_expression", Value::string("no-such")}})),
        PortraitStatus::Invalid);

    EXPECT_FALSE(portrait.is_loaded());
    EXPECT_TRUE(portrait.expressions().empty());
}

TEST(PortraitProfile, LoadPortraitRejectsEmptyId) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"", backend, layers};
    EXPECT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Invalid);
}

TEST(PortraitProfile, LoadPortraitRejectsWhenAlreadyLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    PortraitProfile portrait{"portrait.miku", backend, layers};
    ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
    EXPECT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::AlreadyLoaded);
    EXPECT_TRUE(portrait.unload());
    EXPECT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);  // 卸載後可再載入
}

TEST(PortraitProfile, DestructorUnloadsWhileLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    {
        PortraitProfile portrait{"portrait.miku", backend, layers};
        ASSERT_EQ(portrait.load_portrait(Value::map({})), PortraitStatus::Ok);
        ASSERT_TRUE(backend.has_surface("portrait.miku"));
    }
    EXPECT_FALSE(backend.has_surface("portrait.miku"));
    EXPECT_FALSE(layers.contains("portrait.miku"));
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(PortraitProfile, StatusAndStateToString) {
    EXPECT_STREQ(ds::profiles::to_string(PortraitState::Loaded), "Loaded");
    EXPECT_STREQ(ds::profiles::to_string(PortraitState::Unloaded), "Unloaded");
    EXPECT_STREQ(ds::profiles::to_string(PortraitStatus::Ok), "Ok");
    EXPECT_STREQ(ds::profiles::to_string(PortraitStatus::Unsupported), "Unsupported");
    EXPECT_STREQ(ds::profiles::to_string(PortraitStatus::Invalid), "Invalid");
    EXPECT_STREQ(ds::profiles::to_string(PortraitStatus::AlreadyLoaded), "AlreadyLoaded");
}
