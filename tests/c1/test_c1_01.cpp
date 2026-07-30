// tests/c1/test_c1_01.cpp — C1-01 Skin profile（桌面寵物 / 角色皮膚基底 profile）（gtest）
//
// 涵蓋：skin 組裝（建構預設）、load_skin（E7-01 宣告式定義：程式化 Value 與自文字 parse 兩路徑、
// 圖層 / 輸入 / alpha / 位置欄位套用）、E1-03 透明外形（能力閘控 supported / Unsupported 降級 /
// alpha profile 套用）、E1-01 具名圖層（載入後指派、卸載後移除、宣告式圖層覆寫）、E1-02 輸入策略
// （套用 / 後端策略 + 命中對映）、E1-07 anchor 定位（resolve_live 具體佈局）、E1-08 自由拖曳 +
// 位置記憶（begin/drag_to/end 提交、cancel 還原、save/load round-trip）、以及各類無效 skin
// （非 map / 型別錯 / 具名值無效 / 空 id / 重複載入 / 未載入即操作 / Capture 能力不可用回滾）。
#include "skin_profile.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using ds::format::Document;
using ds::format::parse;
using ds::format::ParseResult;
using ds::format::Value;
using ds::kernel::AlphaMode;
using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityDecl;
using ds::kernel::CapabilityMatrix;
using ds::kernel::DragStatus;
using ds::kernel::HitResult;
using ds::kernel::InputPolicy;
using ds::kernel::InputStrategy;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SurfaceLayer;
using ds::profiles::SkinProfile;
using ds::profiles::SkinState;
using ds::profiles::SkinStatus;

namespace {

// 具備 per-pixel alpha 能力 + capture 能力的能力矩陣（供成功 / Capture 路徑）。
CapabilityMatrix full_matrix() {
    std::vector<CapabilityDecl> decls = alpha_capable_matrix().all();
    decls.push_back(CapabilityDecl{"input.capture", "全域指標捕捉", /*optional=*/true,
                                   /*default_available=*/true});
    return CapabilityMatrix(std::move(decls));
}

// 建構一份典型的宣告式 skin 定義（E7-01 Value / Map，等同 Document::root）。
Value make_definition(const std::string& layer, const std::string& input,
                      const std::string& alpha_mode, double opacity, const std::string& anchor,
                      double dx, double dy) {
    return Value::map({
        {"layer", Value::string(layer)},
        {"input", Value::string(input)},
        {"alpha", Value::map({{"mode", Value::string(alpha_mode)},
                              {"opacity", Value::number(opacity)}})},
        {"position", Value::map({{"anchor", Value::string(anchor)},
                                {"dx", Value::number(dx)},
                                {"dy", Value::number(dy)}})},
    });
}

// -----------------------------------------------------------------------------
// 組裝正確 / 建構預設
// -----------------------------------------------------------------------------

TEST(SkinProfile, ConstructedUnloadedWithDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};

    EXPECT_EQ(skin.state(), SkinState::Unloaded);
    EXPECT_FALSE(skin.is_loaded());
    EXPECT_EQ(skin.id(), "skin.cat");
    EXPECT_EQ(skin.layer(), SurfaceLayer::Normal);
    EXPECT_EQ(skin.layer_name(), std::string("layer.normal"));
    EXPECT_EQ(skin.input_strategy(), InputStrategy::Interactive);
    EXPECT_EQ(skin.alpha().mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(skin.alpha().opacity, 1.0f);
    EXPECT_FALSE(skin.assigned_to_layer());
    EXPECT_TRUE(skin.remembered_position() == nullptr);
}

TEST(SkinProfile, AlphaSupportedReflectsBackendCapability) {
    NullKernelBackend capable{alpha_capable_matrix()};
    NullKernelBackend incapable{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile a{"skin.a", capable, layers};
    SkinProfile b{"skin.b", incapable, layers};
    EXPECT_TRUE(a.alpha_supported());   // E1-03 能力可用
    EXPECT_FALSE(b.alpha_supported());  // 保守 defaults 未宣告 per-pixel alpha
}

// -----------------------------------------------------------------------------
// load_skin — E7-01 宣告式定義（程式化 Value）+ 跨擴充點實體化
// -----------------------------------------------------------------------------

TEST(SkinProfile, LoadSkinAppliesDeclarativeConfigAndRealizes) {
    NullKernelBackend backend{full_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};

    Value def = make_definition("overlay", "click-through", "per-pixel", 0.75, "bottom-right",
                                0.0, 0.0);
    EXPECT_EQ(skin.load_skin(def), SkinStatus::Ok);

    EXPECT_TRUE(skin.is_loaded());
    EXPECT_EQ(skin.state(), SkinState::Loaded);
    // E1-01 圖層
    EXPECT_EQ(skin.layer(), SurfaceLayer::Overlay);
    EXPECT_EQ(skin.layer_name(), std::string("layer.overlay"));
    EXPECT_TRUE(skin.assigned_to_layer());
    // E1-02 輸入策略
    EXPECT_EQ(skin.input_strategy(), InputStrategy::ClickThrough);
    EXPECT_EQ(skin.backend_input_policy(), InputPolicy::PassThrough);
    EXPECT_EQ(skin.hit_result(), HitResult::Transparent);
    // E1-03 透明外形
    EXPECT_EQ(skin.alpha().mode, AlphaMode::PerPixel);
    EXPECT_FLOAT_EQ(skin.alpha().opacity, 0.75f);
    // 後端實體 surface 已建立
    EXPECT_TRUE(backend.has_surface("skin.cat"));
    // E1-08 初始記憶位置
    const AnchorSpec* pos = skin.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::BottomRight);
}

TEST(SkinProfile, LoadSkinFromParsedTextDocument) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.dog", backend, layers};

    // 走完整 E7-01 文字 → parse → Document.root → load_skin。
    const std::string text =
        "format_version: 1.0\n"
        "layer: topmost\n"
        "input: interactive\n"
        "alpha:\n"
        "  mode: per-pixel\n"
        "  opacity: 0.5\n"
        "position:\n"
        "  anchor: top-left\n"
        "  dx: 0.1\n"
        "  dy: 0.2\n";
    ParseResult r = parse(text);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(skin.load_skin(r.document().root), SkinStatus::Ok);

    EXPECT_EQ(skin.layer(), SurfaceLayer::Topmost);
    EXPECT_EQ(skin.input_strategy(), InputStrategy::Interactive);
    EXPECT_FLOAT_EQ(skin.alpha().opacity, 0.5f);
    const AnchorSpec* pos = skin.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::TopLeft);
    EXPECT_FLOAT_EQ(pos->offset.dx, 0.1f);
    EXPECT_FLOAT_EQ(pos->offset.dy, 0.2f);
}

TEST(SkinProfile, LoadSkinEmptyMapUsesDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};

    EXPECT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
    EXPECT_EQ(skin.layer(), SurfaceLayer::Normal);
    EXPECT_EQ(skin.input_strategy(), InputStrategy::Interactive);
    EXPECT_EQ(skin.alpha().mode, AlphaMode::PerPixel);
}

// -----------------------------------------------------------------------------
// E1-03 透明外形 — 能力閘控 / Unsupported 降級
// -----------------------------------------------------------------------------

TEST(SkinProfile, LoadSkinUnsupportedWhenAlphaCapabilityUnavailable) {
    NullKernelBackend backend{CapabilityMatrix::defaults()};  // 無 per-pixel alpha
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};

    EXPECT_EQ(skin.load_skin(Value::map({})), SkinStatus::Unsupported);
    EXPECT_FALSE(skin.is_loaded());
    EXPECT_FALSE(backend.has_surface("skin.cat"));  // 未建立任何 surface（降級路徑）
    EXPECT_FALSE(skin.assigned_to_layer());          // 未觸碰圖層堆疊
}

TEST(SkinProfile, OpaqueAlphaModeApplied) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    Value def = Value::map({{"alpha", Value::map({{"mode", Value::string("opaque")}})}});
    EXPECT_EQ(skin.load_skin(def), SkinStatus::Ok);
    EXPECT_EQ(skin.alpha().mode, AlphaMode::Opaque);
}

// -----------------------------------------------------------------------------
// E1-02 輸入策略 — Capture 能力閘控（NFR-03）+ 回滾
// -----------------------------------------------------------------------------

TEST(SkinProfile, CaptureStrategyRealizesWhenCapabilityAvailable) {
    NullKernelBackend backend{full_matrix()};  // 含 input.capture
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    Value def = Value::map({{"input", Value::string("capture")}});
    EXPECT_EQ(skin.load_skin(def), SkinStatus::Ok);
    EXPECT_EQ(skin.input_strategy(), InputStrategy::Capture);
    EXPECT_EQ(skin.backend_input_policy(), InputPolicy::Modal);
}

TEST(SkinProfile, CaptureStrategyUnsupportedRollsBack) {
    // alpha 可用但無 input.capture 能力 → set_strategy 失敗 → 全回滾（NFR-03 組裝一致性）。
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    Value def = Value::map({{"input", Value::string("capture")}});
    EXPECT_EQ(skin.load_skin(def), SkinStatus::Unsupported);
    EXPECT_FALSE(skin.is_loaded());
    EXPECT_FALSE(backend.has_surface("skin.cat"));  // 已建立的 alpha surface 被回滾銷毀
    EXPECT_FALSE(skin.assigned_to_layer());          // 圖層指派亦回滾
}

TEST(SkinProfile, InputStrategyMappingsAssembleE1_02) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile inert{"skin.inert", backend, layers};
    EXPECT_EQ(inert.load_skin(Value::map({{"input", Value::string("inert")}})), SkinStatus::Ok);
    EXPECT_EQ(inert.input_strategy(), InputStrategy::Inert);
    EXPECT_EQ(inert.backend_input_policy(), InputPolicy::Accepting);
    EXPECT_EQ(inert.hit_result(), HitResult::Solid);
}

// -----------------------------------------------------------------------------
// E1-01 具名圖層 — 卸載後移除
// -----------------------------------------------------------------------------

TEST(SkinProfile, UnloadTearsDownBackendAndLayer) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};

    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
    ASSERT_TRUE(backend.has_surface("skin.cat"));
    ASSERT_TRUE(skin.assigned_to_layer());

    EXPECT_TRUE(skin.unload());
    EXPECT_EQ(skin.state(), SkinState::Unloaded);
    EXPECT_FALSE(backend.has_surface("skin.cat"));  // 後端 surface 銷毀
    EXPECT_FALSE(skin.assigned_to_layer());          // 圖層指派移除
    EXPECT_FALSE(skin.unload());                      // 重複卸載 no-op
}

TEST(SkinProfile, MultipleSkinsShareLayerStackWithoutInterference) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile cat{"skin.cat", backend, layers};
    SkinProfile dog{"skin.dog", backend, layers};

    ASSERT_EQ(cat.load_skin(Value::map({{"layer", Value::string("overlay")}})), SkinStatus::Ok);
    ASSERT_EQ(dog.load_skin(Value::map({{"layer", Value::string("normal")}})), SkinStatus::Ok);
    EXPECT_TRUE(cat.assigned_to_layer());
    EXPECT_TRUE(dog.assigned_to_layer());
    EXPECT_EQ(layers.size(), 2u);

    EXPECT_TRUE(cat.unload());
    EXPECT_FALSE(cat.assigned_to_layer());
    EXPECT_TRUE(dog.assigned_to_layer());  // dog 不受 cat 卸載干擾
    EXPECT_EQ(layers.size(), 1u);
}

// -----------------------------------------------------------------------------
// E1-07 anchor 定位 — resolve_live 具體佈局
// -----------------------------------------------------------------------------

TEST(SkinProfile, ResolveLiveProducesConcretePlacement) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    // center anchor、無偏移；container 100x100、element 20x20 → 置中 (40,40)。
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);

    ResolvedPlacement out;
    EXPECT_EQ(skin.resolve_live(Size{100.0f, 100.0f}, Size{20.0f, 20.0f}, out), AnchorStatus::Ok);
    EXPECT_FLOAT_EQ(out.x, 40.0f);
    EXPECT_FLOAT_EQ(out.y, 40.0f);
    EXPECT_FLOAT_EQ(out.width, 20.0f);
    EXPECT_FLOAT_EQ(out.height, 20.0f);
}

TEST(SkinProfile, ResolveLiveInvalidWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ResolvedPlacement out;
    EXPECT_EQ(skin.resolve_live(Size{100.0f, 100.0f}, Size{20.0f, 20.0f}, out),
              AnchorStatus::Invalid);
}

// -----------------------------------------------------------------------------
// place — E1-08 set_position
// -----------------------------------------------------------------------------

TEST(SkinProfile, PlaceUpdatesRememberedPosition) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);

    AnchorSpec spec;
    spec.anchor = Anchor::TopRight;
    spec.offset.dx = -0.05f;
    EXPECT_EQ(skin.place(spec), DragStatus::Ok);
    const AnchorSpec* pos = skin.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::TopRight);
    EXPECT_FLOAT_EQ(pos->offset.dx, -0.05f);
}

TEST(SkinProfile, PlaceInvalidWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    EXPECT_EQ(skin.place(AnchorSpec{}), DragStatus::Invalid);
}

// -----------------------------------------------------------------------------
// E1-08 自由拖曳 + 位置記憶
// -----------------------------------------------------------------------------

TEST(SkinProfile, DragCommitUpdatesMemory) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);

    EXPECT_EQ(skin.begin_drag(), DragStatus::Ok);
    EXPECT_TRUE(skin.is_dragging());
    EXPECT_EQ(skin.begin_drag(), DragStatus::AlreadyDragging);  // 重複 begin

    AnchorSpec target;
    target.anchor = Anchor::BottomLeft;
    EXPECT_EQ(skin.drag_to(target), DragStatus::Ok);
    // 拖曳中 live 反映 pending，committed 尚未更新
    const AnchorSpec* live = skin.live_position();
    ASSERT_TRUE(live != nullptr);
    EXPECT_EQ(live->anchor, Anchor::BottomLeft);
    EXPECT_EQ(skin.remembered_position()->anchor, Anchor::Center);  // 尚未提交

    EXPECT_EQ(skin.end_drag(), DragStatus::Ok);
    EXPECT_FALSE(skin.is_dragging());
    EXPECT_EQ(skin.remembered_position()->anchor, Anchor::BottomLeft);  // 放開後記住
}

TEST(SkinProfile, DragCancelRestoresMemory) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);

    ASSERT_EQ(skin.begin_drag(), DragStatus::Ok);
    AnchorSpec target;
    target.anchor = Anchor::TopRight;
    ASSERT_EQ(skin.drag_to(target), DragStatus::Ok);
    EXPECT_EQ(skin.cancel_drag(), DragStatus::Ok);
    EXPECT_FALSE(skin.is_dragging());
    EXPECT_EQ(skin.remembered_position()->anchor, Anchor::Center);  // committed 不變（還原）
}

TEST(SkinProfile, DragOpsNotDraggingWhenNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    EXPECT_EQ(skin.begin_drag(), DragStatus::Invalid);
    EXPECT_EQ(skin.drag_to(AnchorSpec{}), DragStatus::NotDragging);
    EXPECT_EQ(skin.end_drag(), DragStatus::NotDragging);
    EXPECT_EQ(skin.cancel_drag(), DragStatus::NotDragging);
}

TEST(SkinProfile, CannotPlaceWhileDragging) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
    ASSERT_EQ(skin.begin_drag(), DragStatus::Ok);
    EXPECT_EQ(skin.place(AnchorSpec{}), DragStatus::Invalid);  // 拖曳期間不可外部改位置
}

// -----------------------------------------------------------------------------
// save_position / load_position — E7-01 文字 round-trip
// -----------------------------------------------------------------------------

TEST(SkinProfile, SaveAndLoadPositionRoundTrip) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile src{"skin.cat", backend, layers};
    ASSERT_EQ(src.load_skin(make_definition("overlay", "interactive", "per-pixel", 1.0,
                                             "bottom-right", 0.03, 0.07)),
              SkinStatus::Ok);

    const std::string saved = src.save_position();
    EXPECT_NE(saved.find("skin.cat"), std::string::npos);
    EXPECT_NE(saved.find("bottom-right"), std::string::npos);

    // 還原到另一個新 profile（純資料回填，可在 load_skin 前先做）。
    NullKernelBackend backend2{alpha_capable_matrix()};
    LayerStack layers2{CapabilityMatrix::defaults()};
    SkinProfile dst{"skin.cat", backend2, layers2};
    EXPECT_EQ(dst.load_position(saved), DragStatus::Ok);
    const AnchorSpec* pos = dst.remembered_position();
    ASSERT_TRUE(pos != nullptr);
    EXPECT_EQ(pos->anchor, Anchor::BottomRight);
    EXPECT_FLOAT_EQ(pos->offset.dx, 0.03f);
    EXPECT_FLOAT_EQ(pos->offset.dy, 0.07f);
}

TEST(SkinProfile, LoadPositionInvalidTextRejected) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    EXPECT_EQ(skin.load_position("not a valid document without version"), DragStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 無效 skin — 結構化拒絕，不靜默
// -----------------------------------------------------------------------------

TEST(SkinProfile, LoadSkinRejectsNonMap) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    EXPECT_EQ(skin.load_skin(Value::string("nope")), SkinStatus::Invalid);
    EXPECT_FALSE(skin.is_loaded());
    EXPECT_FALSE(backend.has_surface("skin.cat"));
}

TEST(SkinProfile, LoadSkinRejectsInvalidNamedValues) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    // 無效圖層名
    EXPECT_EQ(skin.load_skin(Value::map({{"layer", Value::string("nowhere")}})),
              SkinStatus::Invalid);
    // 無效輸入策略名
    EXPECT_EQ(skin.load_skin(Value::map({{"input", Value::string("bogus")}})),
              SkinStatus::Invalid);
    // 無效 anchor 名
    EXPECT_EQ(skin.load_skin(Value::map({{"position",
                                          Value::map({{"anchor", Value::string("middle")}})}})),
              SkinStatus::Invalid);
    // 型別錯：layer 應為字串
    EXPECT_EQ(skin.load_skin(Value::map({{"layer", Value::number(3)}})), SkinStatus::Invalid);
    // 型別錯：opacity 應為數字
    EXPECT_EQ(skin.load_skin(
                  Value::map({{"alpha", Value::map({{"opacity", Value::string("x")}})}})),
              SkinStatus::Invalid);
    // 型別錯：alpha 應為 map
    EXPECT_EQ(skin.load_skin(Value::map({{"alpha", Value::string("x")}})), SkinStatus::Invalid);
    EXPECT_FALSE(skin.is_loaded());  // 全程未改狀態
    EXPECT_FALSE(backend.has_surface("skin.cat"));
}

TEST(SkinProfile, LoadSkinRejectsEmptyId) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"", backend, layers};
    EXPECT_EQ(skin.load_skin(Value::map({})), SkinStatus::Invalid);
}

TEST(SkinProfile, LoadSkinRejectsWhenAlreadyLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    SkinProfile skin{"skin.cat", backend, layers};
    ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
    EXPECT_EQ(skin.load_skin(Value::map({})), SkinStatus::AlreadyLoaded);
    // 卸載後可再載入
    EXPECT_TRUE(skin.unload());
    EXPECT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
}

TEST(SkinProfile, DestructorUnloadsWhileLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    {
        SkinProfile skin{"skin.cat", backend, layers};
        ASSERT_EQ(skin.load_skin(Value::map({})), SkinStatus::Ok);
        ASSERT_TRUE(backend.has_surface("skin.cat"));
    }
    // 解構後不留殘留後端 / 圖層狀態。
    EXPECT_FALSE(backend.has_surface("skin.cat"));
    EXPECT_FALSE(layers.contains("skin.cat"));
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(SkinProfile, StatusAndStateToString) {
    EXPECT_STREQ(ds::profiles::to_string(SkinState::Loaded), "Loaded");
    EXPECT_STREQ(ds::profiles::to_string(SkinState::Unloaded), "Unloaded");
    EXPECT_STREQ(ds::profiles::to_string(SkinStatus::Ok), "Ok");
    EXPECT_STREQ(ds::profiles::to_string(SkinStatus::Unsupported), "Unsupported");
    EXPECT_STREQ(ds::profiles::to_string(SkinStatus::AlreadyLoaded), "AlreadyLoaded");
}

}  // namespace
