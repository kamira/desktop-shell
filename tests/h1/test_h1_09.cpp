// H1-09 就地編輯裝配 —— 驗收標準的端到端情境
//
// 本檔釘住的是驗收標準那兩句話，一句一組：
//   1. 「拖動組件 → 存檔 → 重啟 → 位置還原」      → `DragSaveRestartRestore`
//   2. 「換 kind → 熱重載 → 造型改變」              → `KindChangeHotReload`
// 外加：
//   3. 存檔格式沿用 E7-01 並帶 format_version       → `PersistedFormat`
//   4. 讀 / 寫兩條路徑不得漂移（本層 vs E9-05）     → `FormatParity`
//   5. DPI 語意為邏輯像素                            → `LogicalPixelSemantics`
//   6. 真的接上 E7-14 / E7-15 / E7-08               → `EditorModelBridge` / `DropBridge` /
//                                                      `MigrationBridge`
//
// 「重啟」在這裡就是：讓第一個 `EditAssembly` 離開作用域，用**同一個 storage** 再建一個。
// 持久化狀態是兩者之間唯一的通道 —— 這正是驗收標準要問的事。
//
// K-014：本執行檔**不連任何 `e4_*` / `c2_*`**，故 `ds::elements::RenderModel` 的定義數為 0。
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "edit_assembly.hpp"

namespace {

using ds::format::Value;
using ds::host::edit::EditAssembly;
using ds::host::edit::EditStatus;
using ds::host::edit::IntakeStatus;
using ds::host::edit::LogicalPlacement;
using ds::host::edit::SaveStatus;

LogicalPlacement place(double x, double y, double w, double h) {
    LogicalPlacement p;
    p.x = x;
    p.y = y;
    p.width = w;
    p.height = h;
    return p;
}

// 記錄型 PaintSink：把下達的繪製指令錄成字串序列，「造型」因此可被逐筆斷言。
class RecordingSink final : public ds::host::PaintSink {
public:
    void fill_box(const ds::host::PaintBox& box, ds::host::PaintRole role) override {
        ops.push_back(std::string("fill:") + ds::host::to_string(role) + ":" +
                      std::to_string(box.left) + "," + std::to_string(box.top) + "," +
                      std::to_string(box.right) + "," + std::to_string(box.bottom));
    }
    void draw_text(const ds::host::PaintBox&, const std::string& utf8, ds::host::PaintRole role,
                   ds::host::TextAlign) override {
        ops.push_back(std::string("text:") + ds::host::to_string(role) + ":" + utf8);
    }
    void draw_needle(const ds::host::PaintBox&, double angle_degrees, double, double,
                     ds::host::PaintRole role) override {
        ops.push_back(std::string("needle:") + ds::host::to_string(role) + ":" +
                      std::to_string(static_cast<int>(angle_degrees)));
    }

    std::vector<std::string> ops;
};

// 兩個造型截然不同的 kind：一個畫方塊、一個畫指針。
ds::host::ElementRendererTable two_kind_table() {
    ds::host::ElementRendererTable table;
    table.register_kind("bar", [](const ds::host::ElementDraw& draw,
                                  const ds::host::PaintBox& area, ds::host::PaintSink& sink) {
        sink.fill_box(area, ds::host::PaintRole::Track);
        ds::host::PaintBox filled = area;
        filled.right = area.left + static_cast<int>((area.right - area.left) * draw.fill_ratio);
        sink.fill_box(filled, ds::host::PaintRole::Fill);
    });
    table.register_kind("ring_gauge", [](const ds::host::ElementDraw& draw,
                                         const ds::host::PaintBox& area,
                                         ds::host::PaintSink& sink) {
        sink.draw_needle(area, draw.angle_degrees, 0.8, 0.05, ds::host::PaintRole::Fill);
    });
    return table;
}

// -----------------------------------------------------------------------------
// 1. 拖動組件 → 存檔 → 重啟 → 位置還原
// -----------------------------------------------------------------------------

TEST(DragSaveRestartRestore, PositionSurvivesRestart) {
    ds::layout::MemoryLayoutStorage storage;

    // --- 第一次「開機」---
    {
        EditAssembly session(storage);
        ASSERT_EQ(session.add_element("clock.main", "bar", place(10, 20, 100, 40)),
                  EditStatus::Ok);
        ASSERT_EQ(session.drag_element("clock.main", 35.0, -5.0), EditStatus::Ok);

        LogicalPlacement after;
        ASSERT_TRUE(session.placement_of("clock.main", after));
        EXPECT_DOUBLE_EQ(after.x, 45.0);
        EXPECT_DOUBLE_EQ(after.y, 15.0);

        ASSERT_EQ(session.save("desktop"), SaveStatus::Ok);
    }  // ← 「關機」：session 不存在了，storage 是唯一留下來的東西

    // --- 第二次「開機」---
    EditAssembly restarted(storage);
    EXPECT_EQ(restarted.size(), 0u);  // 新 session 一開始是空的
    ASSERT_EQ(restarted.restore("desktop"), IntakeStatus::Ok);

    ASSERT_EQ(restarted.size(), 1u);
    EXPECT_EQ(restarted.kind_of("clock.main"), "bar");
    LogicalPlacement restored;
    ASSERT_TRUE(restarted.placement_of("clock.main", restored));
    EXPECT_DOUBLE_EQ(restored.x, 45.0);
    EXPECT_DOUBLE_EQ(restored.y, 15.0);
    EXPECT_DOUBLE_EQ(restored.width, 100.0);
    EXPECT_DOUBLE_EQ(restored.height, 40.0);
    EXPECT_EQ(restarted.last_intake().dropped_elements, 0);
    EXPECT_EQ(restarted.last_intake().repaired_fields, 0);
}

TEST(DragSaveRestartRestore, DragAccumulatesAndIsRejectedWhenNonFinite) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("w", "bar", place(0, 0, 10, 10)), EditStatus::Ok);

    ASSERT_EQ(session.drag_element("w", 5.0, 5.0), EditStatus::Ok);
    ASSERT_EQ(session.drag_element("w", 2.5, -1.0), EditStatus::Ok);
    LogicalPlacement p;
    ASSERT_TRUE(session.placement_of("w", p));
    EXPECT_DOUBLE_EQ(p.x, 7.5);
    EXPECT_DOUBLE_EQ(p.y, 4.0);

    // NaN / Inf 的拖曳一律擋在持久化狀態之外，且**位置不動**。
    EXPECT_EQ(session.drag_element("w", std::nan(""), 0.0), EditStatus::NonFiniteValue);
    EXPECT_EQ(session.drag_element("w", 0.0, std::numeric_limits<double>::infinity()),
              EditStatus::NonFiniteValue);
    ASSERT_TRUE(session.placement_of("w", p));
    EXPECT_DOUBLE_EQ(p.x, 7.5);
    EXPECT_DOUBLE_EQ(p.y, 4.0);

    // 夾限之外也是具名拒絕，不是偷偷夾一夾寫進去。
    EXPECT_EQ(session.drag_element("w", 1.0e300, 0.0), EditStatus::OutOfRange);
    ASSERT_TRUE(session.placement_of("w", p));
    EXPECT_DOUBLE_EQ(p.x, 7.5);

    EXPECT_EQ(session.drag_element("nope", 1.0, 1.0), EditStatus::UnknownElement);
}

TEST(DragSaveRestartRestore, AssemblyContract) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    EXPECT_EQ(session.add_element("", "bar", place(0, 0, 1, 1)), EditStatus::EmptyId);
    EXPECT_EQ(session.add_element("a", "", place(0, 0, 1, 1)), EditStatus::EmptyKind);
    EXPECT_EQ(session.add_element("a", "bar", place(0, 0, 1, 1)), EditStatus::Ok);
    EXPECT_EQ(session.add_element("a", "bar", place(0, 0, 1, 1)), EditStatus::DuplicateId);
    EXPECT_EQ(session.add_element("b", "bar", place(std::nan(""), 0, 1, 1)),
              EditStatus::NonFiniteValue);
    EXPECT_EQ(session.add_element("b", "bar", place(1.0e300, 0, 1, 1)), EditStatus::OutOfRange);
    EXPECT_EQ(session.size(), 1u);
    EXPECT_TRUE(session.contains("a"));
    EXPECT_EQ(session.remove_element("a"), EditStatus::Ok);
    EXPECT_EQ(session.remove_element("a"), EditStatus::UnknownElement);
    EXPECT_EQ(session.size(), 0u);
}

// -----------------------------------------------------------------------------
// 2. 換 kind → 熱重載 → 造型改變
// -----------------------------------------------------------------------------

// 先證明「換 kind 真的換畫法」這件事本身成立（不經熱重載）。
TEST(KindChangeHotReload, KindDecidesShape) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("g", "bar", place(0, 0, 100, 20)), EditStatus::Ok);

    const ds::host::ElementRendererTable table = two_kind_table();

    RecordingSink before;
    ds::host::edit::PaintReport r1 =
        ds::host::edit::paint_layout(session.resolve_draws(1.0), table, before);
    EXPECT_EQ(r1.drawn, 1);
    ASSERT_EQ(before.ops.size(), 2u);
    EXPECT_EQ(before.ops[0].rfind("fill:", 0), 0u);

    ASSERT_EQ(session.set_kind("g", "ring_gauge"), EditStatus::Ok);

    RecordingSink after;
    ds::host::edit::PaintReport r2 =
        ds::host::edit::paint_layout(session.resolve_draws(1.0), table, after);
    EXPECT_EQ(r2.drawn, 1);
    ASSERT_EQ(after.ops.size(), 1u);
    EXPECT_EQ(after.ops[0].rfind("needle:", 0), 0u);

    EXPECT_NE(before.ops, after.ops);  // ← 「造型改變」的那條斷言
}

// 驗收標準的那一句：**經過熱重載**換 kind，造型隨之改變。
TEST(KindChangeHotReload, ShapeChangesAfterHotReload) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly author(storage);
    ASSERT_EQ(author.add_element("g", "bar", place(0, 0, 100, 20)), EditStatus::Ok);
    ASSERT_EQ(author.edit_property("g", "fill_ratio", Value::number(0.5)), EditStatus::Ok);
    ASSERT_EQ(author.save("live"), SaveStatus::Ok);

    std::string text;
    ASSERT_TRUE(storage.get("live", text));

    // 外部來源（E7-07）：相位 1 的「檔案」就是這個可注入的 MemorySource。
    ds::format::MemorySource source(text);
    EditAssembly runtime(storage);
    runtime.attach_source(source);

    ASSERT_EQ(runtime.poll_reload(), ds::host::edit::ReloadOutcome::Reloaded);
    EXPECT_EQ(runtime.kind_of("g"), "bar");

    const ds::host::ElementRendererTable table = two_kind_table();
    RecordingSink before;
    ds::host::edit::paint_layout(runtime.resolve_draws(1.0), table, before);

    // 來源沒變 → 不重載（E7-07 的 revision 比對），造型當然也不會變。
    EXPECT_EQ(runtime.poll_reload(), ds::host::edit::ReloadOutcome::Unchanged);

    // 使用者在外部把 kind 改掉（就地編輯的另一種入口：直接改設定檔）。
    std::string changed = text;
    const std::size_t at = changed.find("type: bar");
    ASSERT_NE(at, std::string::npos);
    changed.replace(at, std::string("type: bar").size(), "type: ring_gauge");
    source.set_content(changed);

    ASSERT_EQ(runtime.poll_reload(), ds::host::edit::ReloadOutcome::Reloaded);
    EXPECT_EQ(runtime.kind_of("g"), "ring_gauge");

    RecordingSink after;
    ds::host::edit::paint_layout(runtime.resolve_draws(1.0), table, after);

    ASSERT_FALSE(before.ops.empty());
    ASSERT_FALSE(after.ops.empty());
    EXPECT_EQ(before.ops[0].rfind("fill:", 0), 0u);
    EXPECT_EQ(after.ops[0].rfind("needle:", 0), 0u);
    EXPECT_NE(before.ops, after.ops);  // ← 「造型改變」
    EXPECT_EQ(runtime.stats().reloads, 2);
}

TEST(KindChangeHotReload, UnknownKindIsNamedNotSilent) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("x", "no_such_kind", place(0, 0, 10, 10)), EditStatus::Ok);
    const ds::host::ElementRendererTable table = two_kind_table();
    RecordingSink sink;
    const ds::host::edit::PaintReport r =
        ds::host::edit::paint_layout(session.resolve_draws(1.0), table, sink);
    EXPECT_EQ(r.unknown_kind, 1);
    EXPECT_EQ(r.drawn, 0);
    EXPECT_EQ(r.total(), 1);  // 元件沒有憑空消失：它是「畫不出來」且說得出來
    EXPECT_TRUE(sink.ops.empty());
}

TEST(KindChangeHotReload, PollWithoutSourceIsNamed) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    EXPECT_EQ(session.poll_reload(), ds::host::edit::ReloadOutcome::NoSource);
    EXPECT_FALSE(session.has_source());
}

// -----------------------------------------------------------------------------
// 3. 存檔格式沿用 E7-01 並帶 format_version
// -----------------------------------------------------------------------------

TEST(PersistedFormat, CarriesFormatVersionAndReparses) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(1, 2, 3, 4)), EditStatus::Ok);
    ASSERT_EQ(session.save("p"), SaveStatus::Ok);

    std::string text;
    ASSERT_TRUE(storage.get("p", text));
    EXPECT_EQ(text.rfind("format_version: ", 0), 0u);  // 首行就是版本欄位

    // 版本與 E9-05 宣告的存檔格式版本一致（本層不自己訂一個）。
    ds::format::FormatVersion v{};
    ASSERT_EQ(ds::host::edit::peek_format_version(text, v),
              ds::host::edit::VersionPeek::Found);
    EXPECT_EQ(v, ds::layout::kLayoutFormat);

    // 產出即 E7-01 `parse()` 能再解析的合法輸入。
    const ds::format::ParseResult re = ds::format::parse(text);
    ASSERT_TRUE(re.ok());
    EXPECT_EQ(re.document().format_version, ds::layout::kLayoutFormat);
}

// 讀（本層容忍式）與寫（E9-05）兩條路徑不得各自漂移。
TEST(FormatParity, TolerantReaderAgreesWithE905OnValidInput) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(1, 2, 3, 4)), EditStatus::Ok);
    ASSERT_EQ(session.add_element("b", "ring_gauge", place(-5.5, 6.25, 30, 30)),
              EditStatus::Ok);
    ASSERT_EQ(session.edit_property("b", "label", Value::string("CPU: 50%")), EditStatus::Ok);
    ASSERT_EQ(session.edit_property("b", "nested.deep", Value::number(7)), EditStatus::Ok);
    ASSERT_EQ(session.save("parity"), SaveStatus::Ok);

    std::string text;
    ASSERT_TRUE(storage.get("parity", text));

    const ds::host::edit::IntakeReport mine = ds::host::edit::ingest_layout_text(text);
    ASSERT_EQ(mine.status, IntakeStatus::Ok);
    const ds::layout::LayoutState theirs = ds::layout::deserialize_layout(text);

    EXPECT_EQ(mine.state, theirs);                       // 逐欄相等（E9-05 的深層比較）
    EXPECT_EQ(mine.state.elements.size(), theirs.elements.size());
    EXPECT_EQ(mine.state, session.state());              // 也等於記憶體裡的狀態
}

// -----------------------------------------------------------------------------
// 5. DPI：存檔裡是邏輯像素，裝置像素只在 resolve 這一步出現
// -----------------------------------------------------------------------------

TEST(LogicalPixelSemantics, ResolveScalesLogicalToDevice) {
    LogicalPlacement p = place(10, 20, 100, 40);

    const ds::host::PaintBox at1 = ds::host::edit::resolve_placement(p, 1.0);
    EXPECT_EQ(at1.left, 10);
    EXPECT_EQ(at1.top, 20);
    EXPECT_EQ(at1.right, 110);
    EXPECT_EQ(at1.bottom, 60);

    const ds::host::PaintBox at125 = ds::host::edit::resolve_placement(p, 1.25);
    EXPECT_EQ(at125.left, 13);   // round(12.5)
    EXPECT_EQ(at125.top, 25);
    EXPECT_EQ(at125.right, 138);  // round(137.5)
    EXPECT_EQ(at125.bottom, 75);

    const ds::host::PaintBox at2 = ds::host::edit::resolve_placement(p, 2.0);
    EXPECT_EQ(at2.left, 20);
    EXPECT_EQ(at2.right, 220);
}

TEST(LogicalPixelSemantics, PersistedGeometryIsScaleIndependent) {
    // 同一份存檔在不同 DPI 下還原出**相同的邏輯像素**；變的只有 resolve 的結果。
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(10, 20, 100, 40)), EditStatus::Ok);
    ASSERT_EQ(session.save("dpi"), SaveStatus::Ok);

    EditAssembly reopened(storage);
    ASSERT_EQ(reopened.restore("dpi"), IntakeStatus::Ok);
    LogicalPlacement p;
    ASSERT_TRUE(reopened.placement_of("a", p));
    EXPECT_DOUBLE_EQ(p.x, 10.0);  // 存檔裡不會因為當時的 DPI 而變成 12.5

    EXPECT_EQ(reopened.resolve_draws(1.0)[0].box.left, 10);
    EXPECT_EQ(reopened.resolve_draws(2.0)[0].box.left, 20);
}

TEST(LogicalPixelSemantics, AbsurdScaleIsSanitized) {
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_dpi(std::nan("")), 1.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_dpi(0.0), 1.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_dpi(-2.0), 1.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_dpi(1.0e9), ds::host::edit::kMaxDpiScale);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_dpi(1.5), 1.5);
}

// -----------------------------------------------------------------------------
// 6. 真的接上 E7-14 / E7-15 / E7-08
// -----------------------------------------------------------------------------

TEST(EditorModelBridge, ExistingFieldGoesThroughE714Validation) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(0, 0, 10, 10)), EditStatus::Ok);

    // `x` 已存在且是數值 → E7-14 推斷為數值欄位，塞字串進去會被擋下。
    EXPECT_EQ(session.edit_property("a", "x", Value::string("left")),
              EditStatus::RejectedByModel);
    LogicalPlacement p;
    ASSERT_TRUE(session.placement_of("a", p));
    EXPECT_DOUBLE_EQ(p.x, 0.0);  // 被擋下 = 沒有寫進去

    // 新欄位（路徑不存在）走 E7-12 新增，型別自由。
    EXPECT_EQ(session.edit_property("a", "label", Value::string("CPU")), EditStatus::Ok);
    Value out;
    ASSERT_TRUE(session.property_of("a", "label", out));
    ASSERT_TRUE(out.is_string());
    EXPECT_EQ(out.as_string(), "CPU");

    // 既有字串欄位改成布林 → 一樣被 E7-14 擋下。
    EXPECT_EQ(session.edit_property("a", "label", Value::boolean(true)),
              EditStatus::RejectedByModel);

    // NaN 連 E7-14 都不必問，本層先擋（不得進入持久化狀態）。
    EXPECT_EQ(session.edit_property("a", "x", Value::number(std::nan(""))),
              EditStatus::NonFiniteValue);
    // 空路徑 = 整個 properties（一個 Map）；拿數值去取代它 → E7-14 擋下。
    EXPECT_EQ(session.edit_property("a", "", Value::number(1.0)),
              EditStatus::RejectedByModel);
    // 語法真的錯的路徑（未閉合的 '['）→ E7-12 parse_path 擋下。
    EXPECT_EQ(session.edit_property("a", "bad[", Value::number(1.0)), EditStatus::BadPath);
    EXPECT_EQ(session.edit_property("nope", "x", Value::number(1.0)),
              EditStatus::UnknownElement);
}

TEST(DropBridge, DropGeneratesConfigItemViaE715) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(0, 0, 10, 10)), EditStatus::Ok);

    // 圖片 → E7-15 映到 background.image。
    ASSERT_EQ(session.apply_drop("a", ds::format::DropContent::file("/tmp/wall.png")),
              EditStatus::Ok);
    Value out;
    ASSERT_TRUE(session.property_of("a", "background.image", out));
    ASSERT_TRUE(out.is_string());
    EXPECT_EQ(out.as_string(), "/tmp/wall.png");

    // URL → link.url。
    ASSERT_EQ(session.apply_drop("a", ds::format::DropContent::url("https://example.com")),
              EditStatus::Ok);
    ASSERT_TRUE(session.property_of("a", "link.url", out));
    EXPECT_EQ(out.as_string(), "https://example.com");

    // 認不出來的內容 → 具名拒絕，不靜默猜測。
    EXPECT_EQ(session.apply_drop("a", ds::format::DropContent::detect(std::string("\x01"))),
              EditStatus::UnknownDropContent);
    EXPECT_EQ(session.apply_drop("nope", ds::format::DropContent::url("https://e.com")),
              EditStatus::UnknownElement);

    // 拖放產生的設定項也要能存進去、再讀回來。
    ASSERT_EQ(session.save("dropped"), SaveStatus::Ok);
    EditAssembly reopened(storage);
    ASSERT_EQ(reopened.restore("dropped"), IntakeStatus::Ok);
    ASSERT_TRUE(reopened.property_of("a", "background.image", out));
    EXPECT_EQ(out.as_string(), "/tmp/wall.png");
}

// -----------------------------------------------------------------------------
// 7. 與 H1-07 的橋接（`units.json` 宣告的相依；ACC-20261113-h1_09 的 R3）
// -----------------------------------------------------------------------------
//
// H1-07 的 `ComponentPlacement` 與本層的 `LogicalPlacement` **語意相同**（邏輯像素、
// 原點左上、widget 內相對），只差表示型別（`int` vs `double`）。第一版既沒連 H1-07、
// 也沒消費它的型別，等於宣告了相依卻各自長了一套幾何。轉換現在只有這一處，而且
// 這一組測試釘住它的消毒行為 —— 否則日後兩邊一漂移，沒有人會發現。

TEST(ComposeBridge, LogicalAndComponentPlacementRoundTrip) {
    const LogicalPlacement src = place(12, 34, 56, 78);
    const ds::host::ComponentPlacement mid = ds::host::edit::to_component_placement(src);
    EXPECT_EQ(mid.origin.x, 12);
    EXPECT_EQ(mid.origin.y, 34);
    EXPECT_EQ(mid.size.width, 56);
    EXPECT_EQ(mid.size.height, 78);
    EXPECT_FALSE(mid.size.empty());

    const LogicalPlacement back = ds::host::edit::from_component_placement(mid);
    EXPECT_EQ(back, src);  // 整數值原樣往返
}

TEST(ComposeBridge, FractionalLogicalPixelsRoundOnTheWayToH107) {
    // 本層保留小數（拖曳要累加，整數會每拖一次掉一次精度）；H1-07 的版面是整數格位。
    // 轉換是**四捨五入**，而且只發生在這一處。
    const ds::host::ComponentPlacement p =
        ds::host::edit::to_component_placement(place(12.4, 12.5, 0.5, 1.49));
    EXPECT_EQ(p.origin.x, 12);
    EXPECT_EQ(p.origin.y, 13);
    EXPECT_EQ(p.size.width, 1);
    EXPECT_EQ(p.size.height, 1);
}

TEST(ComposeBridge, HostileGeometryIsSanitizedBeforeCrossingOver) {
    // 壞幾何不得帶著 NaN / 超限值跨進 H1-07（那一側全是 int，轉型本身就是 UB 風險）。
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const ds::host::ComponentPlacement p =
        ds::host::edit::to_component_placement(place(nan, inf, -5.0, 1.0e308));
    EXPECT_EQ(p.origin.x, 0);
    EXPECT_EQ(p.origin.y, 0);
    EXPECT_EQ(p.size.width, 0);  // 負尺寸歸 0，不翻正
    EXPECT_EQ(p.size.height, ds::host::edit::round_to_px(ds::host::edit::kExtentLimit));
    EXPECT_TRUE(p.size.empty());
}

TEST(ComposeBridge, PresentationIsCarriedThroughUntouched) {
    ds::host::ComponentPresentation pres;
    pres.variant = "compact";
    pres.params["opacity"] = 0.5;
    pres.visible = false;

    const ds::host::ComponentPlacement p =
        ds::host::edit::to_component_placement(place(1, 2, 3, 4), pres);
    EXPECT_EQ(p.presentation.variant, "compact");
    EXPECT_DOUBLE_EQ(p.presentation.param("opacity", -1.0), 0.5);
    EXPECT_FALSE(p.presentation.visible);
}

TEST(ComposeBridge, ElementGeometryReachesH107ThroughTheBridge) {
    // 端到端：存檔裡的幾何 → 本層讀回 → 橋接 → H1-07 的 `scale_box` 算出裝置像素，
    // 結果必須與本層 `resolve_placement` **逐一相等**。
    //
    // 這條等式不是巧合：兩層的右 / 下緣都是「**邏輯座標的和**再縮放」
    // （H1-07 `scale_box` 寫明不用 `left + scale(width)`，以免相鄰組件在非整數縮放下
    // 出現 1px 的縫；本層 `resolve_placement` 算的是 `(x + width) * scale`），
    // 且兩邊都是四捨五入。任何一側改掉這個組合方式，下面就會紅。
    struct Case {
        LogicalPlacement p;
        double scale;
    };
    const Case cases[] = {
        {place(10, 20, 100, 40), 1.0},   {place(10, 20, 100, 40), 1.25},
        {place(10, 20, 100, 40), 1.5},   {place(12, 7, 101, 33), 1.25},
        {place(0, 0, 1, 1), 2.0},        {place(-30, -15, 60, 20), 1.25},
        {place(3, 3, 0, 0), 1.75},
        // 下面這兩組是刻意挑的「兩邊各自四捨五入會對不上」的例子：
        // (1+1)*1.5 = 3.0 → 3，但 round(1*1.5) + round(1*1.5) = 2 + 2 = **4**。
        // 任何一側改成「先各自取整再相加」，這兩組就會紅 —— 而那正是 H1-07 `scale_box`
        // 的註解明文要避免的相鄰組件 1px 縫隙。
        {place(1, 1, 1, 1), 1.5},        {place(2, 2, 3, 3), 1.5},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::to_string(c.p.x) + "," + std::to_string(c.p.y) + " @" +
                     std::to_string(c.scale));
        ds::layout::MemoryLayoutStorage storage;
        EditAssembly session(storage);
        ASSERT_EQ(session.add_element("w", "bar", c.p), EditStatus::Ok);
        ASSERT_EQ(session.save("x"), SaveStatus::Ok);

        EditAssembly reopened(storage);
        ASSERT_EQ(reopened.restore("x"), IntakeStatus::Ok);
        LogicalPlacement p;
        ASSERT_TRUE(reopened.placement_of("w", p));
        EXPECT_EQ(p, c.p);  // 存檔往返不改幾何

        const ds::host::ComponentPlacement cp = ds::host::edit::to_component_placement(p);
        const ds::host::PaintBox via_h1_07 = ds::host::scale_box(cp.origin, cp.size, c.scale);
        const ds::host::PaintBox via_h1_09 = ds::host::edit::resolve_placement(p, c.scale);
        EXPECT_EQ(via_h1_07.left, via_h1_09.left);
        EXPECT_EQ(via_h1_07.top, via_h1_09.top);
        EXPECT_EQ(via_h1_07.right, via_h1_09.right);
        EXPECT_EQ(via_h1_07.bottom, via_h1_09.bottom);
    }
}

// 舊版存檔：E7-08 遷移鏈真的被套用（而不是「結構看起來一樣就收下」）。
TEST(MigrationBridge, OldVersionGoesThroughE708) {
    // 0.9 的假想舊格式：座標叫 pos_x / pos_y。
    const std::string old_text =
        "format_version: 0.9\n"
        "elements:\n"
        "  -\n"
        "    id: a\n"
        "    type: bar\n"
        "    properties:\n"
        "      pos_x: 12\n"
        "      pos_y: 34\n"
        "      width: 50\n"
        "      height: 60\n";

    // 沒有遷移鏈 → 具名拒收（**不**沿用舊語意硬讀）。
    const ds::host::edit::IntakeReport bare = ds::host::edit::ingest_layout_text(old_text);
    EXPECT_EQ(bare.status, IntakeStatus::MigrationUnavailable);
    EXPECT_EQ(bare.source_version, (ds::format::FormatVersion{0, 9}));

    // 註冊 0.9 → 1.0：把 pos_x / pos_y 改名為 x / y。
    ds::format::MigrationRegistry registry;
    ASSERT_TRUE(registry.add(ds::format::FormatVersion{0, 9}, ds::layout::kLayoutFormat,
                             [](const Value& root) {
                                 // **K-013 的現場**：`out` 在迴圈裡被整個指派掉，任何指進
                                 // `out` 的指標 / 參考當場懸空。故一律從**不變的 `root`**
                                 // 讀、只往 `out` 寫。（第一版寫成讀 `out`，ASan 會抓。）
                                 Value out = root;
                                 const Value* elems = root.find("elements");
                                 if (elems == nullptr || !elems->is_list()) {
                                     return out;
                                 }
                                 const std::size_t count = elems->as_list().size();
                                 for (std::size_t i = 0; i < count; ++i) {
                                     const Value& item = elems->as_list()[i];
                                     const Value* props = item.find("properties");
                                     if (props == nullptr || !props->is_map()) {
                                         continue;
                                     }
                                     const std::string base =
                                         "elements[" + std::to_string(i) + "].properties.";
                                     const Value* px = props->find("pos_x");
                                     const Value* py = props->find("pos_y");
                                     if (px != nullptr) {
                                         out = ds::format::set_value(out, base + "x", *px);
                                     }
                                     if (py != nullptr) {
                                         out = ds::format::set_value(out, base + "y", *py);
                                     }
                                 }
                                 return out;
                             }));

    const ds::host::edit::IntakeReport migrated =
        ds::host::edit::ingest_layout_text(old_text, &registry);
    ASSERT_EQ(migrated.status, IntakeStatus::Migrated);
    ASSERT_EQ(migrated.state.elements.size(), 1u);
    const Value& props = migrated.state.elements[0].properties;
    ASSERT_NE(props.find("x"), nullptr);
    EXPECT_DOUBLE_EQ(props.at("x").as_number(), 12.0);
    EXPECT_DOUBLE_EQ(props.at("y").as_number(), 34.0);

    // 同一份舊存檔，經 EditAssembly 還原後位置正確。
    ds::layout::MemoryLayoutStorage storage;
    storage.put("legacy", old_text);
    EditAssembly session(storage);
    session.set_migrations(&registry);
    ASSERT_EQ(session.restore("legacy"), IntakeStatus::Migrated);
    LogicalPlacement p;
    ASSERT_TRUE(session.placement_of("a", p));
    EXPECT_DOUBLE_EQ(p.x, 12.0);
    EXPECT_DOUBLE_EQ(p.y, 34.0);
}

}  // namespace
