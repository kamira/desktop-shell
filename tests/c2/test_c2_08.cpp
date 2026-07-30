// tests/c2/test_c2_08.cpp — C2-08 筆記 widget（gtest）
//
// 涵蓋：組裝（建構預設、id）、C1-01 基底載入（能力可用 → Ok + 圖層 / 位置實體化；能力不可用
// → Unsupported 降級，透傳 SkinStatus）、定位 / 拖曳透傳（place / begin_drag / drag_to /
// end_drag / cancel_drag / is_dragging）、E4-15 編輯（set_text / insert / backspace /
// erase_forward）、E4-01 顯示（render_model 排版與游標）、E7-12 內容持久化
// save()→load() round-trip（一般文字 / 空筆記 / 多行 / 含需轉義字元）、無效輸入不靜默
// （無法解析的文字 / text 欄位型別錯）且不變動既有內容、NoteStatus 具名字串。
#include "note_widget.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::format::Value;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::Anchor;
using ds::kernel::AnchorSpec;
using ds::kernel::CapabilityMatrix;
using ds::kernel::DragStatus;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::kernel::SurfaceLayer;
using ds::profiles::SkinState;
using ds::profiles::SkinStatus;
using ds::render::FixedFontMetrics;
using ds::widgets::NoteStatus;
using ds::widgets::NoteWidget;
using ds::widgets::to_string;

namespace {

// 等寬字型：每字元 advance=10、行高=20（與上游 E4-01 / E4-15 測試同構）。
FixedFontMetrics MakeMetrics() { return FixedFontMetrics(10.0, 20.0); }

// -----------------------------------------------------------------------------
// 組裝 / 建構預設
// -----------------------------------------------------------------------------

TEST(NoteWidget, ConstructedWithIdAndEmptyUnloadedDefaults) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    EXPECT_EQ(note.id(), "note.todo");
    EXPECT_FALSE(note.is_base_loaded());
    EXPECT_EQ(note.base_state(), SkinState::Unloaded);
    EXPECT_EQ(note.text(), "");
    EXPECT_EQ(note.length(), 0u);
}

// -----------------------------------------------------------------------------
// C1-01 基底 — 載入 / 卸載 / 能力閘控降級（全數透傳 SkinProfile）
// -----------------------------------------------------------------------------

TEST(NoteWidget, LoadBaseAssemblesLayerAndPositionWhenCapable) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    Value def = Value::map({
        {"layer", Value::string("overlay")},
        {"position", Value::map({{"anchor", Value::string("top-left")}})},
    });
    EXPECT_EQ(note.load_base(def), SkinStatus::Ok);
    EXPECT_TRUE(note.is_base_loaded());
    EXPECT_EQ(note.base_state(), SkinState::Loaded);
    EXPECT_TRUE(backend.has_surface("note.todo"));

    EXPECT_TRUE(note.unload_base());
    EXPECT_FALSE(note.is_base_loaded());
    EXPECT_FALSE(backend.has_surface("note.todo"));
}

TEST(NoteWidget, LoadBaseUnsupportedWhenAlphaCapabilityUnavailable) {
    NullKernelBackend backend{CapabilityMatrix::defaults()};  // 無 per-pixel alpha
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    EXPECT_EQ(note.load_base(Value::map({})), SkinStatus::Unsupported);
    EXPECT_FALSE(note.is_base_loaded());
    EXPECT_FALSE(backend.has_surface("note.todo"));  // 未建立任何 surface（降級路徑）
}

// -----------------------------------------------------------------------------
// 定位 / 拖曳 — 透傳 C1-01（E1-07 / E1-08）
// -----------------------------------------------------------------------------

TEST(NoteWidget, PlaceAndDragDelegateToBase) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};
    ASSERT_EQ(note.load_base(Value::map({})), SkinStatus::Ok);

    AnchorSpec spec;
    spec.anchor = Anchor::TopRight;
    EXPECT_EQ(note.place(spec), DragStatus::Ok);

    EXPECT_EQ(note.begin_drag(), DragStatus::Ok);
    EXPECT_TRUE(note.is_dragging());
    AnchorSpec target;
    target.anchor = Anchor::BottomLeft;
    EXPECT_EQ(note.drag_to(target), DragStatus::Ok);
    EXPECT_EQ(note.end_drag(), DragStatus::Ok);
    EXPECT_FALSE(note.is_dragging());

    EXPECT_EQ(note.begin_drag(), DragStatus::Ok);
    EXPECT_EQ(note.cancel_drag(), DragStatus::Ok);
    EXPECT_FALSE(note.is_dragging());
}

TEST(NoteWidget, PlaceInvalidWhenBaseNotLoaded) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    EXPECT_EQ(note.place(AnchorSpec{}), DragStatus::Invalid);
    EXPECT_EQ(note.begin_drag(), DragStatus::Invalid);
}

// -----------------------------------------------------------------------------
// 編輯 — 透傳 E4-15
// -----------------------------------------------------------------------------

TEST(NoteWidget, SetTextAndInsertEditContent) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    note.set_text("buy milk");
    EXPECT_EQ(note.text(), "buy milk");
    EXPECT_EQ(note.length(), 8u);

    note.insert("!");
    EXPECT_EQ(note.text(), "buy milk!");
}

TEST(NoteWidget, BackspaceAndEraseForwardEditContent) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    note.set_text("abc");  // 游標於末端(3)
    note.backspace();
    EXPECT_EQ(note.text(), "ab");

    note.set_text("abc");
    note.backspace();
    note.backspace();
    note.backspace();
    note.erase_forward();  // 游標於起點(0)，無內容可向前刪：no-op
    EXPECT_EQ(note.text(), "");
}

TEST(NoteWidget, SetTextInvalidUtf8Throws) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    const std::string bad(1, static_cast<char>(0xFF));
    EXPECT_THROW(note.set_text(bad), std::invalid_argument);
}

// -----------------------------------------------------------------------------
// 顯示 — E4-01（經 E4-15 render_model）
// -----------------------------------------------------------------------------

TEST(NoteWidget, RenderModelProducesLayoutMatchingContent) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};
    note.set_text("hi");

    const auto model = note.render_model();
    ASSERT_EQ(model.layout.lines.size(), 1u);
    ASSERT_EQ(model.layout.glyphs.size(), 2u);
    EXPECT_EQ(model.layout.glyphs[0].codepoint, static_cast<ds::render::CodePoint>('h'));
    EXPECT_EQ(model.layout.glyphs[1].codepoint, static_cast<ds::render::CodePoint>('i'));
    EXPECT_EQ(model.cursor.index, 2u);  // set_text 後游標於末端
}

TEST(NoteWidget, RenderModelEmptyNoteHasNoLines) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};

    const auto model = note.render_model();
    EXPECT_TRUE(model.layout.lines.empty());
    EXPECT_FALSE(model.has_selection);
}

// -----------------------------------------------------------------------------
// 內容持久化 — E7-12（經 E7-01）save()→load() round-trip
// -----------------------------------------------------------------------------

TEST(NoteWidget, SaveThenLoadRoundTripsSimpleText) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget src{"note.todo", backend, layers, metrics};
    src.set_text("buy milk");

    const std::string saved = src.save();
    EXPECT_NE(saved.find("format_version"), std::string::npos);
    EXPECT_NE(saved.find("buy milk"), std::string::npos);

    NoteWidget dst{"note.todo", backend, layers, metrics};
    EXPECT_EQ(dst.load(saved), NoteStatus::Ok);
    EXPECT_EQ(dst.text(), "buy milk");
}

TEST(NoteWidget, SaveThenLoadRoundTripsEmptyNote) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget src{"note.todo", backend, layers, metrics};  // 內容預設為空

    const std::string saved = src.save();

    NoteWidget dst{"note.todo", backend, layers, metrics};
    dst.set_text("stale");  // 先塞入舊內容，驗證 load() 會以序列化內容取代（含清空）
    EXPECT_EQ(dst.load(saved), NoteStatus::Ok);
    EXPECT_EQ(dst.text(), "");
}

TEST(NoteWidget, SaveThenLoadRoundTripsMultilineNote) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget src{"note.todo", backend, layers, metrics};
    src.set_text("line one\nline two\nline three");

    const std::string saved = src.save();

    NoteWidget dst{"note.todo", backend, layers, metrics};
    EXPECT_EQ(dst.load(saved), NoteStatus::Ok);
    EXPECT_EQ(dst.text(), "line one\nline two\nline three");
}

TEST(NoteWidget, SaveThenLoadRoundTripsTextNeedingEscaping) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget src{"note.todo", backend, layers, metrics};
    src.set_text("say \"hi\": use a\\b then\ttab");

    const std::string saved = src.save();

    NoteWidget dst{"note.todo", backend, layers, metrics};
    EXPECT_EQ(dst.load(saved), NoteStatus::Ok);
    EXPECT_EQ(dst.text(), "say \"hi\": use a\\b then\ttab");
}

// -----------------------------------------------------------------------------
// 無效輸入 — 不靜默、不變動既有內容
// -----------------------------------------------------------------------------

TEST(NoteWidget, LoadUnparsableTextRejectedContentUnchanged) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};
    note.set_text("keep me");

    EXPECT_EQ(note.load("not a valid document without version"), NoteStatus::Invalid);
    EXPECT_EQ(note.text(), "keep me");  // 內容不變
}

TEST(NoteWidget, LoadTextFieldWrongTypeRejectedContentUnchanged) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};
    note.set_text("keep me");

    const std::string text_field_is_number =
        "format_version: 1.0\n"
        "text: 42\n";
    EXPECT_EQ(note.load(text_field_is_number), NoteStatus::Invalid);
    EXPECT_EQ(note.text(), "keep me");
}

TEST(NoteWidget, LoadMissingTextFieldYieldsEmptyNote) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics = MakeMetrics();
    NoteWidget note{"note.todo", backend, layers, metrics};
    note.set_text("keep me");

    const std::string no_text_field = "format_version: 1.0\nother: 1\n";
    EXPECT_EQ(note.load(no_text_field), NoteStatus::Ok);
    EXPECT_EQ(note.text(), "");  // 缺席 text 欄位視為空筆記，非錯誤
}

// -----------------------------------------------------------------------------
// 具名結果字串（NFR-02）
// -----------------------------------------------------------------------------

TEST(NoteWidget, ToStringNamesAreStable) {
    EXPECT_STREQ(to_string(NoteStatus::Ok), "note.ok");
    EXPECT_STREQ(to_string(NoteStatus::Invalid), "note.invalid");
}

}  // namespace
