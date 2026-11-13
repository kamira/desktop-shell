// H1-09 —— 驗收標準最硬的那一條：
//
//   「**使用者可直接改動持久化狀態，故錯誤不得讓佈局進入救不回來的狀態。**」
//
// 所以本檔不是「跑一遍正常流程」，而是**拿壞掉的存檔餵進去看會怎樣**。每一組都要同時釘住
// 三件事：
//   (a) 失敗是**具名**的（分得出是哪一種壞，不是一個泛用的 false）；
//   (b) 失敗後**記憶體裡的佈局原封不動**（不會因為檔案壞掉就什麼都沒了）；
//   (c) 失敗後**storage 裡的原文原封不動**（使用者還改得回來；也沒有被壞狀態覆寫）。
//
// 覆蓋的壞法：截斷 / 亂碼 / 非法縮排、欄位缺失 / 型別不對 / 數值荒謬（NaN / Inf / 1e400 /
// 負尺寸 / 超大座標）、format_version 太新 / 太舊 / 缺失 / 寫壞、`elements` 形狀不對、
// 重複 id、遷移步驟自己壞掉、以及「寫入過程中斷」（本層的提交點只有一個，見 AtomicSave）。
//
// 這一組同時是 **K-016 / K-002**（NaN 比較）與 **float-cast-overflow**（double→int）的
// 回歸測試：`gate_sanitizers` 以 `-fsanitize=address,undefined,float-cast-overflow
// -fno-sanitize-recover=all` 跑本檔，一次溢位的轉換就會讓測試當場 FAILED。
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
using ds::host::edit::IntakeReport;
using ds::host::edit::IntakeStatus;
using ds::host::edit::LogicalPlacement;
using ds::host::edit::SaveStatus;
using ds::host::edit::ingest_layout_text;

LogicalPlacement place(double x, double y, double w, double h) {
    LogicalPlacement p;
    p.x = x;
    p.y = y;
    p.width = w;
    p.height = h;
    return p;
}

// 一份好的存檔，外加一個已經在跑的 session（裡面有一個「現有佈局」）。
struct Fixture {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session{storage};

    std::string good_text;

    Fixture() {
        EXPECT_EQ(session.add_element("good", "bar", place(11, 22, 33, 44)), EditStatus::Ok);
        EXPECT_EQ(session.save("desktop"), SaveStatus::Ok);
        EXPECT_TRUE(storage.get("desktop", good_text));
    }

    std::string stored(const std::string& name = "desktop") const {
        std::string text;
        EXPECT_TRUE(storage.get(name, text));
        return text;
    }

    // 把 `name` 的存檔內容換成 `text`（模擬使用者拿編輯器手改）。
    void tamper(const std::string& text, const std::string& name = "desktop") {
        storage.put(name, text);
    }

    // 現有佈局還在、還是原來那一個。
    void expect_layout_intact() const {
        ASSERT_EQ(session.size(), 1u);
        EXPECT_EQ(session.kind_of("good"), "bar");
        LogicalPlacement p;
        ASSERT_TRUE(session.placement_of("good", p));
        EXPECT_DOUBLE_EQ(p.x, 11.0);
        EXPECT_DOUBLE_EQ(p.y, 22.0);
    }
};

// 走一次「壞存檔 → restore」，並檢查 (a)(b)(c) 三件事。
void expect_rejected(Fixture& fx, const std::string& bad_text, IntakeStatus expected) {
    fx.tamper(bad_text);
    const std::string before = fx.stored();

    EXPECT_EQ(fx.session.restore("desktop"), expected) << fx.session.last_message();
    EXPECT_FALSE(fx.session.last_intake().usable());
    EXPECT_FALSE(fx.session.last_message().empty());  // 說得出原因，不是靜默
    fx.expect_layout_intact();                        // (b) 記憶體佈局沒被毀掉
    EXPECT_EQ(fx.stored(), before);                   // (c) 檔案原文沒被動過
}

// -----------------------------------------------------------------------------
// 1. 存檔檔案損毀
// -----------------------------------------------------------------------------

TEST(CorruptFile, TruncatedMidDocument) {
    Fixture fx;
    const std::string good = fx.stored();
    ASSERT_GT(good.size(), 20u);
    expect_rejected(fx, good.substr(0, good.size() / 2), IntakeStatus::Malformed);
}

TEST(CorruptFile, TruncatedToNothing) {
    Fixture fx;
    expect_rejected(fx, std::string(), IntakeStatus::MissingVersion);
}

TEST(CorruptFile, BinaryGarbage) {
    Fixture fx;
    // 含嵌入 NUL 的二進位垃圾。長度**用 sizeof 算，不用手數**——第一版手寫 26 多算了
    // 5 個位元組，ASan 當場報 global-buffer-overflow（字面值只有 22 個位元組含結尾 NUL）。
    static const char kGarbage[] = "\x01\x02\xff\xfe nonsense \0 bytes";
    expect_rejected(fx, std::string(kGarbage, sizeof(kGarbage) - 1),
                    IntakeStatus::MissingVersion);
}

TEST(CorruptFile, TabIndentIsLocatedNotSilent) {
    Fixture fx;
    // E7-01 明確拒絕 tab 縮排並定位到行；本層把那個行號帶出來。
    expect_rejected(fx,
                    "format_version: 1.0\n"
                    "elements:\n"
                    "\t- a\n",
                    IntakeStatus::Malformed);
    EXPECT_NE(fx.session.last_message().find("行 3"), std::string::npos)
        << fx.session.last_message();
}

TEST(CorruptFile, ElementsIsNotAList) {
    Fixture fx;
    expect_rejected(fx,
                    "format_version: 1.0\n"
                    "elements: 42\n",
                    IntakeStatus::StructureInvalid);
}

// -----------------------------------------------------------------------------
// 2. format_version：太新 / 太舊 / 缺失 / 寫壞
// -----------------------------------------------------------------------------

TEST(VersionGate, MissingVersionIsNotGuessed) {
    Fixture fx;
    expect_rejected(fx,
                    "elements:\n"
                    "  -\n"
                    "    id: a\n"
                    "    type: bar\n",
                    IntakeStatus::MissingVersion);
}

TEST(VersionGate, FutureVersionIsRefusedNotDowngraded) {
    Fixture fx;
    const std::string future =
        "format_version: 1.1\n"
        "elements:\n"
        "  -\n"
        "    id: from_the_future\n"
        "    type: bar\n";
    expect_rejected(fx, future, IntakeStatus::VersionTooNew);
    expect_rejected(fx, "format_version: 2.0\n", IntakeStatus::VersionTooNew);

    // 未來版本的元件**沒有**被讀進來（不是「先收下再說」）。
    EXPECT_FALSE(fx.session.contains("from_the_future"));
}

TEST(VersionGate, OlderVersionWithoutMigrationChainIsRefused) {
    Fixture fx;
    expect_rejected(fx,
                    "format_version: 0.9\n"
                    "elements:\n"
                    "  -\n"
                    "    id: a\n"
                    "    type: bar\n",
                    IntakeStatus::MigrationUnavailable);
}

TEST(VersionGate, MalformedVersionNeverThrows) {
    Fixture fx;
    // 最後一個是重點：E7-01 的 `parse_version` 用 `std::stoi`，位數爆掉時會丟
    // `std::out_of_range`。本層在 parse **之前**就把它判成 MalformedVersion，
    // 所以那段文字根本不會被餵給 `parse()`（對外承諾不 throw）。
    for (const char* bad : {"format_version:\n", "format_version: 1\n",
                            "format_version: x.y\n", "format_version: 1.\n",
                            "format_version: .1\n", "format_version: 1.2.3\n",
                            "format_version: -1.0\n", "format_version: 1.0 # 註解\n",
                            "format_version: 99999999999999999999.0\n",
                            "format_version: 1.99999999999999999999\n"}) {
        SCOPED_TRACE(bad);
        expect_rejected(fx, bad, IntakeStatus::MalformedVersion);
    }
}

TEST(VersionGate, PeekIsPurelyTextualAndFindsRootKeyOnly) {
    ds::format::FormatVersion v{};
    EXPECT_EQ(ds::host::edit::peek_format_version("format_version: 3.7\nx: 1\n", v),
              ds::host::edit::VersionPeek::Found);
    EXPECT_EQ(v.major, 3);
    EXPECT_EQ(v.minor, 7);

    // 縮排過的同名鍵不是根層的版本欄位。
    EXPECT_EQ(ds::host::edit::peek_format_version("a:\n  format_version: 1.0\n", v),
              ds::host::edit::VersionPeek::Missing);
    // 註解行不算。
    EXPECT_EQ(ds::host::edit::peek_format_version("# format_version: 1.0\n", v),
              ds::host::edit::VersionPeek::Missing);
    // 空白 / 行尾換行的變體仍找得到。
    EXPECT_EQ(ds::host::edit::peek_format_version("format_version:   1.0", v),
              ds::host::edit::VersionPeek::Found);
    EXPECT_EQ(v, ds::layout::kLayoutFormat);
}

TEST(VersionGate, RestampTouchesOnlyTheVersionLine) {
    const std::string src =
        "format_version: 0.9\n"
        "elements:\n"
        "  -\n"
        "    id: a   # format_version: 9.9 不該被動到\n";
    const std::string out =
        ds::host::edit::restamp_format_version(src, ds::format::FormatVersion{1, 0});
    EXPECT_EQ(out,
              "format_version: 1.0\n"
              "elements:\n"
              "  -\n"
              "    id: a   # format_version: 9.9 不該被動到\n");
    // 找不到版本行 → 原樣回傳（不無中生有）。
    EXPECT_EQ(ds::host::edit::restamp_format_version("x: 1\n", ds::format::FormatVersion{1, 0}),
              "x: 1\n");
}

TEST(VersionGate, RestampStopsAtTheFirstVersionLine) {
    // 根層出現兩個 format_version 是 E7-01 明文的「重複的 key」錯誤。restamp **只換第一個**，
    // 把重複留在原地交給 E7-01 去報 —— 全部換掉會讓一份非法文件長得更像合法文件。
    const std::string two =
        "format_version: 0.9\n"
        "format_version: 0.8\n"
        "elements:\n";
    EXPECT_EQ(ds::host::edit::restamp_format_version(two, ds::format::FormatVersion{1, 0}),
              "format_version: 1.0\n"
              "format_version: 0.8\n"
              "elements:\n");

    // 而這種文件讀進來就是具名的解析失敗（帶行號），不是「修一修收下」。
    Fixture fx;
    expect_rejected(fx, two, IntakeStatus::Malformed);
    EXPECT_NE(fx.session.last_message().find("format_version"), std::string::npos)
        << fx.session.last_message();
}

TEST(VersionGate, BrokenMigrationStepIsNamedNotSilent) {
    Fixture fx;
    ds::format::MigrationRegistry registry;
    ASSERT_TRUE(registry.add(ds::format::FormatVersion{0, 9}, ds::layout::kLayoutFormat,
                             [](const Value&) -> Value {
                                 throw std::runtime_error("遷移步驟自己炸了");
                             }));
    fx.session.set_migrations(&registry);
    expect_rejected(fx,
                    "format_version: 0.9\n"
                    "elements:\n"
                    "  -\n"
                    "    id: a\n"
                    "    type: bar\n",
                    IntakeStatus::MigrationFailed);
}

TEST(VersionGate, MigrationReturningNonMapIsStructureInvalid) {
    // `parse()` 之後根一定是 Map，但遷移步驟可以回傳任何東西 —— 這條防線走得到。
    ds::format::MigrationRegistry registry;
    ASSERT_TRUE(registry.add(ds::format::FormatVersion{0, 9}, ds::layout::kLayoutFormat,
                             [](const Value&) { return Value::list({}); }));
    const IntakeReport rep = ingest_layout_text(
        "format_version: 0.9\n"
        "elements:\n"
        "  -\n"
        "    id: a\n"
        "    type: bar\n",
        &registry);
    EXPECT_EQ(rep.status, IntakeStatus::StructureInvalid);
    EXPECT_FALSE(rep.usable());
}

// -----------------------------------------------------------------------------
// 3. 欄位缺失 / 型別不對 —— 壞一個元件不得炸掉整份佈局
// -----------------------------------------------------------------------------

TEST(PartialDamage, BadElementIsDroppedTheRestSurvive) {
    const std::string text =
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: keep_me\n"
        "    type: bar\n"
        "    properties:\n"
        "      x: 1\n"
        "  -\n"
        "    type: bar\n"           // 缺 id → 無法定位 → 丟棄
        "  -\n"
        "    id: no_type\n"         // 缺 type → 丟棄
        "  -\n"
        "    id: 42\n"              // id 型別不對 → 丟棄
        "    type: bar\n"
        "  -\n"
        "    id: keep_me_too\n"
        "    type: ring_gauge\n";

    const IntakeReport rep = ingest_layout_text(text);
    EXPECT_EQ(rep.status, IntakeStatus::Repaired);
    EXPECT_TRUE(rep.usable());
    EXPECT_TRUE(rep.degraded());
    EXPECT_EQ(rep.dropped_elements, 3);
    ASSERT_EQ(rep.state.elements.size(), 2u);
    EXPECT_EQ(rep.state.elements[0].id, "keep_me");
    EXPECT_EQ(rep.state.elements[1].id, "keep_me_too");

    // 對比組：E9-05 的嚴格讀取對同一份輸入是整份失敗 —— 這正是本層不直接用它的理由。
    EXPECT_THROW(ds::layout::deserialize_layout(text), std::runtime_error);
}

TEST(PartialDamage, DuplicateIdDropsTheLaterOne) {
    const IntakeReport rep = ingest_layout_text(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: dup\n"
        "    type: bar\n"
        "  -\n"
        "    id: dup\n"
        "    type: ring_gauge\n");
    EXPECT_EQ(rep.status, IntakeStatus::Repaired);
    EXPECT_EQ(rep.dropped_elements, 1);
    ASSERT_EQ(rep.state.elements.size(), 1u);
    EXPECT_EQ(rep.state.elements[0].type, "bar");  // 先到的留下
}

TEST(PartialDamage, PropertiesOfWrongTypeBecomeEmptyMap) {
    const IntakeReport rep = ingest_layout_text(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: a\n"
        "    type: bar\n"
        "    properties: \"not a map\"\n");
    EXPECT_EQ(rep.status, IntakeStatus::Repaired);
    // 整個 properties 區塊被丟掉是**內容遺失**，不是欄位消毒 —— 兩者的差別決定了
    // save() 要不要擋（F4）。修訂 2 把它算進 repaired_fields，於是它被當成自我修復。
    EXPECT_EQ(rep.discarded_properties, 1);
    EXPECT_EQ(rep.repaired_fields, 0);
    EXPECT_EQ(rep.lost_content(), 1);
    EXPECT_FALSE(rep.carries_everything());
    ASSERT_EQ(rep.state.elements.size(), 1u);
    EXPECT_TRUE(rep.state.elements[0].properties.is_map());
    EXPECT_EQ(rep.state.elements[0].properties.size(), 0u);
}

TEST(PartialDamage, EmptyElementsListIsAnEmptyLayoutNotAnError) {
    const IntakeReport rep = ingest_layout_text("format_version: 1.0\nelements:\n");
    EXPECT_EQ(rep.status, IntakeStatus::Ok);
    EXPECT_TRUE(rep.state.elements.empty());

    const IntakeReport none = ingest_layout_text("format_version: 1.0\n");
    EXPECT_EQ(none.status, IntakeStatus::Ok);
    EXPECT_TRUE(none.state.elements.empty());
}

// -----------------------------------------------------------------------------
// 4. 數值荒謬：NaN / Inf / 1e400 / 負尺寸 / 超大座標（K-016 / K-002 的回歸）
// -----------------------------------------------------------------------------

TEST(AbsurdNumbers, PersistedNanAndInfinityAreRepairedNotPropagated) {
    const IntakeReport rep = ingest_layout_text(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: a\n"
        "    type: bar\n"
        "    properties:\n"
        "      x: nan\n"          // E7-01 推斷為裸字串 → 幾何欄位型別不對 → 修補為 0
        "      y: inf\n"          // 同上
        "      width: -40\n"      // 負尺寸 → 修補為 0（不翻正，不猜使用者意圖）
        "      height: 1e9\n");   // 合法但超過夾限 → 夾到上界
    EXPECT_EQ(rep.status, IntakeStatus::Repaired);
    EXPECT_EQ(rep.repaired_fields, 4);
    ASSERT_EQ(rep.state.elements.size(), 1u);

    const Value& props = rep.state.elements[0].properties;
    ASSERT_TRUE(props.at("x").is_number());
    EXPECT_TRUE(std::isfinite(props.at("x").as_number()));
    EXPECT_DOUBLE_EQ(props.at("x").as_number(), 0.0);
    EXPECT_DOUBLE_EQ(props.at("y").as_number(), 0.0);
    EXPECT_DOUBLE_EQ(props.at("width").as_number(), 0.0);
    EXPECT_DOUBLE_EQ(props.at("height").as_number(), ds::host::edit::kExtentLimit);
}

// 合法但巨大的字面值（`1e308` 還在 double 值域內）→ 讀得進來，夾到上界。
TEST(AbsurdNumbers, HugeButRepresentableIsClamped) {
    const IntakeReport rep = ingest_layout_text(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: a\n"
        "    type: bar\n"
        "    properties:\n"
        "      x: 1e308\n"
        "      y: -1e308\n");
    EXPECT_EQ(rep.status, IntakeStatus::Repaired);
    EXPECT_EQ(rep.repaired_fields, 2);
    const Value& props = rep.state.elements[0].properties;
    EXPECT_DOUBLE_EQ(props.at("x").as_number(), ds::host::edit::kCoordLimit);
    EXPECT_DOUBLE_EQ(props.at("y").as_number(), -ds::host::edit::kCoordLimit);
}

// **上游的例外路徑被本層關在裡面** —— 這是實際踩到的，不是假想：
// E7-01 `numeric_token` 用 `std::stod`，`1e400` 超出 double 值域 → 丟 `std::out_of_range`，
// 而 `parse()` 的契約是「回傳帶行號的 ParseError」，不是丟例外。使用者在設定檔裡手打一個
// 很大的數就走得到。本層對 `parse()` 的 try/catch 把它變成具名的 `Malformed`，
// 不讓它穿出 host 層（穿出去 = 整個桌面掛掉）。
TEST(AbsurdNumbers, ParserThrowFromOutOfRangeLiteralIsContained) {
    const char* const texts[] = {
        "format_version: 1.0\nelements:\n  -\n    id: a\n    type: bar\n"
        "    properties:\n      x: 1e400\n",
        "format_version: 1.0\nelements:\n  -\n    id: a\n    type: bar\n"
        "    properties:\n      y: -1e400\n",
        // **下溢走的是同一個 `std::stod`**，一樣會丟 std::out_of_range。
        // 補測試時很容易只補上溢那一半（本單元第一版就是）。
        "format_version: 1.0\nelements:\n  -\n    id: a\n    type: bar\n"
        "    properties:\n      x: 1e-400\n",
        "format_version: 1.0\nelements:\n  -\n    id: a\n    type: bar\n"
        "    properties:\n      width: 1e999999\n",
    };
    for (const char* text : texts) {
        SCOPED_TRACE(text);
        IntakeReport rep;
        ASSERT_NO_THROW(rep = ingest_layout_text(text));
        EXPECT_EQ(rep.status, IntakeStatus::Malformed);
        EXPECT_FALSE(rep.message.empty());
        EXPECT_FALSE(rep.usable());
    }

    // 端到端：這種存檔不會讓既有佈局不見。
    Fixture fx;
    expect_rejected(fx,
                    "format_version: 1.0\nelements:\n  -\n    id: a\n    type: bar\n"
                    "    properties:\n      x: 1e400\n",
                    IntakeStatus::Malformed);
}

TEST(AbsurdNumbers, SanitizersNeverLetNanThrough) {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();

    // K-002：`v < lo ? lo : (v > hi ? hi : v)` 對 NaN 兩個比較都是 false → 原封回傳。
    // 本層的 clamp 先判 isfinite，所以 NaN 不可能原封出來。
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(nan, 7.0, 0.0, 10.0), 7.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(inf, 7.0, 0.0, 10.0), 7.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(-inf, 7.0, 0.0, 10.0), 7.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(nan, nan, 2.0, 10.0), 2.0);  // fallback 也壞
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(5.0, 7.0, 0.0, 10.0), 5.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(-5.0, 7.0, 0.0, 10.0), 0.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::clamp_finite(50.0, 7.0, 0.0, 10.0), 10.0);

    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_coord(nan), 0.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_coord(-inf), 0.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_extent(nan), 0.0);
    EXPECT_DOUBLE_EQ(ds::host::edit::sanitize_extent(-1.0), 0.0);

    // K-016：`finite()` 必須明寫 isfinite。若有人改成靠 `!=` 之類的比較，這裡會紅。
    EXPECT_FALSE(place(nan, 0, 1, 1).finite());
    EXPECT_FALSE(place(0, nan, 1, 1).finite());
    EXPECT_FALSE(place(0, 0, nan, 1).finite());
    EXPECT_FALSE(place(0, 0, 1, nan).finite());
    EXPECT_FALSE(place(inf, 0, 1, 1).finite());
    EXPECT_TRUE(place(0, 0, 1, 1).finite());

    EXPECT_FALSE(place(nan, 0, 1, 1).within_limits());
    EXPECT_FALSE(place(0, 0, nan, 1).usable());
    EXPECT_FALSE(place(0, 0, 0, 10).usable());  // 退化尺寸不算可用
}

TEST(AbsurdNumbers, ResolveNeverOverflowsTheDoubleToIntCast) {
    // 這一組是給 `-fsanitize=float-cast-overflow` 跑的：任何一次溢位轉換就是 FAILED。
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const LogicalPlacement hostile[] = {
        place(nan, nan, nan, nan),
        place(inf, -inf, inf, inf),
        place(1.0e308, 1.0e308, 1.0e308, 1.0e308),
        place(-1.0e308, -1.0e308, 1.0e308, 1.0e308),
        place(ds::host::edit::kCoordLimit, ds::host::edit::kCoordLimit,
              ds::host::edit::kExtentLimit, ds::host::edit::kExtentLimit),
    };
    for (const LogicalPlacement& p : hostile) {
        for (const double scale : {nan, inf, -1.0, 0.0, 1.0, 1.25, 1.0e300,
                                   ds::host::edit::kMaxDpiScale}) {
            const ds::host::PaintBox box = ds::host::edit::resolve_placement(p, scale);
            EXPECT_LE(box.left, box.right);   // 半開區間的慣例不得被壞輸入翻掉
            EXPECT_LE(box.top, box.bottom);
        }
    }
}

TEST(AbsurdNumbers, HostileFileStillPaints) {
    // 端到端：壞成這樣的存檔讀進來之後，整條繪製路徑仍然走得完（不崩潰、框有限）。
    ds::layout::MemoryLayoutStorage storage;
    storage.put("hostile",
                "format_version: 1.0\n"
                "elements:\n"
                "  -\n"
                "    id: a\n"
                "    type: bar\n"
                "    properties:\n"
                "      x: 1e308\n"
                "      y: -1e308\n"
                "      width: nan\n"
                "      height: -7\n"
                "      fill_ratio: 1e308\n"
                "      angle: nan\n");
    EditAssembly session(storage);
    ASSERT_EQ(session.restore("hostile"), IntakeStatus::Repaired);

    const std::vector<ds::host::edit::PlacedDraw> draws = session.resolve_draws(1.25);
    ASSERT_EQ(draws.size(), 1u);
    EXPECT_LE(draws[0].box.left, draws[0].box.right);
    EXPECT_TRUE(draws[0].box.empty());  // 退化尺寸 → 具名的 EmptyArea，不是垃圾座標
    EXPECT_TRUE(std::isfinite(draws[0].draw.fill_ratio));
    EXPECT_GE(draws[0].draw.fill_ratio, 0.0);
    EXPECT_LE(draws[0].draw.fill_ratio, 1.0);
    EXPECT_TRUE(std::isfinite(draws[0].draw.angle_degrees));
}

// -----------------------------------------------------------------------------
// 5. 寫入：唯一提交點 + 往返自驗（「寫入過程中斷」的落點）
// -----------------------------------------------------------------------------

TEST(AtomicSave, BadStateIsNeverCommitted) {
    Fixture fx;
    const std::string good = fx.stored();

    // 繞過編輯 API（就像別處的程式碼 / 未來的呼叫端塞進一個壞狀態）。
    ds::layout::LayoutState poisoned = fx.session.state();
    poisoned.elements[0].properties =
        ds::format::set_value(poisoned.elements[0].properties, "x",
                              Value::number(std::nan("")));
    fx.session.reset(poisoned);

    EXPECT_EQ(fx.session.dry_run_save(), SaveStatus::VerifyFailed);
    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::VerifyFailed);
    EXPECT_EQ(fx.stored(), good);  // ← 既有存檔一個位元組都沒被改到
    EXPECT_EQ(fx.session.stats().saves_rejected, 1);
    EXPECT_EQ(fx.session.stats().saves, 1);  // 只有 Fixture 那一次成功

    // 而且舊存檔仍然救得回來。
    EditAssembly rescue(fx.storage);
    ASSERT_EQ(rescue.restore("desktop"), IntakeStatus::Ok);
    LogicalPlacement p;
    ASSERT_TRUE(rescue.placement_of("good", p));
    EXPECT_DOUBLE_EQ(p.x, 11.0);
}

TEST(AtomicSave, SerializeFailureLeavesStorageUntouched) {
    Fixture fx;
    const std::string good = fx.stored();

    ds::layout::LayoutState broken = fx.session.state();
    broken.elements[0].id.clear();  // E9-05 的序列化契約：id 不得為空 → throw
    fx.session.reset(broken);

    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::SerializeFailed);
    EXPECT_FALSE(fx.session.last_message().empty());
    EXPECT_EQ(fx.stored(), good);
}

TEST(AtomicSave, EmptyNameIsRefusedBeforeTouchingStorage) {
    Fixture fx;
    EXPECT_EQ(fx.session.save(""), SaveStatus::EmptyName);
    EXPECT_EQ(fx.storage.names().size(), 1u);  // 沒有多出一個空名字的條目
}

TEST(AtomicSave, SavedTextAlwaysReparsesToTheSameState) {
    // 往返自驗的正面：各種值（負數、小數、長字串、轉義字元）存完再讀都一致。
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(-3.25, 0.1, 1e5, 0.0)), EditStatus::Ok);
    ASSERT_EQ(session.edit_property("a", "label", Value::string("含: 冒號\n與換行")),
              EditStatus::Ok);
    ASSERT_EQ(session.edit_property("a", "ratio", Value::number(1.0 / 3.0)), EditStatus::Ok);
    ASSERT_EQ(session.save("rt"), SaveStatus::Ok);

    EditAssembly reopened(storage);
    ASSERT_EQ(reopened.restore("rt"), IntakeStatus::Ok);
    EXPECT_EQ(reopened.state(), session.state());
}

// -----------------------------------------------------------------------------
// 6. 還原 / 重載失敗一律保舊值
// -----------------------------------------------------------------------------

TEST(KeepLastGood, MissingNameIsNamedAndHarmless) {
    Fixture fx;
    EXPECT_EQ(fx.session.restore("no_such_layout"), IntakeStatus::NotFound);
    EXPECT_FALSE(fx.session.last_message().empty());
    fx.expect_layout_intact();
    EXPECT_EQ(fx.session.stats().loads_failed, 1);
}

TEST(KeepLastGood, HotReloadRejectionKeepsTheOldLayout) {
    Fixture fx;
    ds::format::MemorySource source(fx.stored());
    fx.session.attach_source(source);
    ASSERT_EQ(fx.session.poll_reload(), ds::host::edit::ReloadOutcome::Reloaded);
    fx.expect_layout_intact();

    // 使用者在外面把檔案改壞了。
    source.set_content("format_version: 1.0\nelements:\n\t- broken\n");
    EXPECT_EQ(fx.session.poll_reload(), ds::host::edit::ReloadOutcome::Rejected);
    EXPECT_EQ(fx.session.last_intake_status(), IntakeStatus::Malformed);
    fx.expect_layout_intact();                          // 舊佈局還在
    EXPECT_FALSE(fx.session.last_reload_report().empty());  // E7-06 的定位報告拿得到
    EXPECT_EQ(fx.session.stats().reloads_rejected, 1);

    // 改回來之後能恢復。
    source.set_content(fx.stored());
    EXPECT_EQ(fx.session.poll_reload(), ds::host::edit::ReloadOutcome::Reloaded);
    fx.expect_layout_intact();

    fx.session.detach_source();
    EXPECT_EQ(fx.session.poll_reload(), ds::host::edit::ReloadOutcome::NoSource);
}

TEST(KeepLastGood, RepairedLoadIsAdoptedAndCounted) {
    Fixture fx;
    fx.tamper(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: survivor\n"
        "    type: bar\n"
        "    properties:\n"
        "      x: nan\n"
        "  -\n"
        "    type: orphan\n");
    EXPECT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);
    EXPECT_TRUE(fx.session.last_intake().usable());
    EXPECT_EQ(fx.session.stats().elements_dropped, 1);
    EXPECT_EQ(fx.session.stats().fields_repaired, 1);
    EXPECT_EQ(fx.session.stats().loads_failed, 0);  // Repaired 不算失敗
    ASSERT_EQ(fx.session.size(), 1u);
    EXPECT_TRUE(fx.session.contains("survivor"));

    // 降級後的狀態**沒有**被自動寫回去：原文還在，使用者還能手改回來。
    EXPECT_NE(fx.stored().find("orphan"), std::string::npos);
}

// -----------------------------------------------------------------------------
// 7. 拒收之後的 save() 不得把那份檔案毀掉（ACC-20261113-h1_09 的 F1）
// -----------------------------------------------------------------------------
//
// 第一版的洞：`save()` 完全不看目標存檔，一律覆寫。於是這條路會毀資料——
//   開機 → restore 被拒收 → 記憶體佈局是空的 → 離開時自動 save → 使用者的佈局永久消失。
// 而那次 save 回報 `Ok`。使用者沒做錯任何事，只是檔案裡多了一個 tab。
//
// 下面每一組都跨越「拒收 → save」這**兩次**呼叫 —— 第一版 32 支測試裡沒有任何一條跨過
// 這個邊界，所以那個洞一條測試都沒踩到。

// 五種拒收分類各自對應的壞檔案（內容本身由 API 產生的合法文字改出來，不是手編的）。
struct BadFile {
    const char* label;
    IntakeStatus expected;
    std::string text;
};

std::vector<BadFile> bad_files(const std::string& good) {
    std::vector<BadFile> out;

    std::string tabbed = good;
    const std::size_t el = tabbed.find("elements:");
    EXPECT_NE(el, std::string::npos);
    tabbed.insert(el, "\t");
    out.push_back({"多打一個 tab 縮排", IntakeStatus::Malformed, tabbed});

    std::string no_version = good;
    const std::size_t nl = no_version.find('\n');
    EXPECT_NE(nl, std::string::npos);
    no_version.erase(0, nl + 1);
    out.push_back({"不小心刪掉版本行", IntakeStatus::MissingVersion, no_version});

    std::string comma = good;
    comma.replace(comma.find("format_version: 1.0"), 19, "format_version: 1,0");
    out.push_back({"版本打成 1,0", IntakeStatus::MalformedVersion, comma});

    std::string future = good;
    future.replace(future.find("format_version: 1.0"), 19, "format_version: 2.0");
    out.push_back({"新版程式寫的 2.0", IntakeStatus::VersionTooNew, future});

    std::string old_one = good;
    old_one.replace(old_one.find("format_version: 1.0"), 19, "format_version: 0.9");
    out.push_back({"舊版且無遷移鏈", IntakeStatus::MigrationUnavailable, old_one});

    return out;
}

TEST(OverwriteGuard, SaveRefusesToClobberAnUnreadableLayout) {
    for (const BadFile& bad : bad_files(Fixture().stored())) {
        SCOPED_TRACE(bad.label);
        Fixture fx;
        fx.tamper(bad.text);

        // 開機 → 還原失敗（既有佈局保留，檔案未動 —— 第一版就做到的部分）
        EXPECT_EQ(fx.session.restore("desktop"), bad.expected) << fx.session.last_message();
        EXPECT_EQ(fx.stored(), bad.text);

        // ← 這一次呼叫就是第一版毀掉使用者資料的地方
        EXPECT_EQ(fx.session.save("desktop"), SaveStatus::RefusedUnreadableTarget);
        EXPECT_EQ(fx.stored(), bad.text);  // 一個位元組都沒被改到
        EXPECT_NE(fx.session.last_message().find("desktop"), std::string::npos);
        EXPECT_NE(fx.session.last_message().find(ds::host::edit::to_string(bad.expected)),
                  std::string::npos)
            << fx.session.last_message();
        EXPECT_EQ(fx.session.stats().saves_refused_target, 1);
        EXPECT_EQ(fx.session.stats().rejected_backups, 0);

        // 而且使用者真的救得回來：把檔案修好，佈局就回來了。
        fx.tamper(fx.good_text);
        ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Ok);
        LogicalPlacement p;
        ASSERT_TRUE(fx.session.placement_of("good", p));
        EXPECT_DOUBLE_EQ(p.x, 11.0);
    }
}

// V1.7 的原始重現：**從來沒有 restore 成功過**，記憶體佈局是空的，然後自動存檔。
// 守衛刻意做在「提交的當下重讀目標」，而不是查「上一次 restore 的結果」——
// 後者擋不住這一條，而這一條毀的資料一模一樣。
TEST(OverwriteGuard, RefusesEvenWhenRestoreWasNeverCalled) {
    ds::layout::MemoryLayoutStorage storage;
    const std::string user_file =
        "format_version: 2.0\n"            // 新版程式寫的
        "elements:\n"
        "  -\n"
        "    id: cpu\n"
        "    type: ring_gauge\n";
    storage.put("desktop", user_file);

    EditAssembly app(storage);             // 開機，記憶體是空的
    EXPECT_EQ(app.size(), 0u);
    EXPECT_EQ(app.save("desktop"), SaveStatus::RefusedUnreadableTarget);  // 離開時自動存檔

    std::string after;
    ASSERT_TRUE(storage.get("desktop", after));
    EXPECT_EQ(after, user_file);
    EXPECT_NE(after.find("cpu"), std::string::npos);  // 元件還在
}

TEST(OverwriteGuard, ReadableTargetsAreOverwrittenAsBefore) {
    Fixture fx;  // 目標是自己剛存的合法檔案
    ASSERT_EQ(fx.session.add_element("second", "bar", place(1, 2, 3, 4)), EditStatus::Ok);
    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::Ok);
    EXPECT_NE(fx.stored().find("second"), std::string::npos);
    EXPECT_EQ(fx.session.stats().saves_refused_target, 0);

    // storage 裡沒有這個名字 → 沒有東西會被毀掉，照存。
    EXPECT_EQ(fx.session.save("brand_new"), SaveStatus::Ok);
    EXPECT_TRUE(fx.storage.has("brand_new"));
}

// **只有欄位被消毒、零內容遺失**的 `Repaired` 才可以覆寫：那種情況下使用者的資料
// 真的全部都在記憶體裡，存回去是**自我修復**（把 NaN 寫回成 0）。
//
// 修訂 2 的這支測試用的是「第二個元件缺 id」，斷言 save 回 Ok，註解寫
// 「使用者的資料已經在記憶體裡了」——**對那個輸入是假的**：被丟棄的元件不在記憶體裡，
// 正因為不在，覆寫才會毀掉它。那條測試把 F4 這個洞釘成了「正確行為」。
TEST(OverwriteGuard, FieldRepairOnlyTargetIsStillOverwritable) {
    Fixture fx;
    fx.tamper(
        "format_version: 1.0\n"
        "elements:\n"
        "  -\n"
        "    id: good\n"
        "    type: bar\n"
        "    properties:\n"
        "      x: nan\n");  // 幾何被消毒；沒有任何元件 / 鍵被丟掉
    ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);
    ASSERT_GT(fx.session.last_intake().repaired_fields, 0);
    ASSERT_EQ(fx.session.last_intake().lost_content(), 0);
    ASSERT_TRUE(fx.session.last_intake().carries_everything());

    EXPECT_TRUE(fx.session.target_is_safe_to_overwrite("desktop"));
    EXPECT_EQ(fx.session.dry_run_save("desktop"), SaveStatus::Ok);
    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::Ok);
    EXPECT_EQ(fx.session.stats().saves_refused_target, 0);
    EXPECT_EQ(fx.session.stats().saves_refused_incomplete, 0);
    // 自我修復：寫回去的是消毒後的值。
    EXPECT_EQ(ds::host::edit::ingest_layout_text(fx.stored()).status, IntakeStatus::Ok);
}

// -----------------------------------------------------------------------------
// 7b. 部分丟棄之後的 save() 一樣不得靜默覆寫（ACC 的 F4）
// -----------------------------------------------------------------------------
//
// F1 修的是「整份讀不出來」。F4 是**同一條驗收標準的第二個入口**：檔案讀得出來，
// 但本層丟掉了其中一塊——使用者把一個元件的 `type:` 打成 `typo:`，那個元件連同它的
// 整組設定就此消失，而修訂 2 的 `save()` 回報 `Ok`、沒有備份、兩個預檢 API 還都給綠燈。
//
// 判據因此從「讀不讀得出來」（`usable()`）改成
// 「**本層有沒有完整帶走檔案裡的內容**」（`carries_everything()`）。

// 兩個元件，第二個被使用者改壞；被丟掉的那個帶著 `tune: 777` 當作可驗的指紋。
std::string two_elements_second_broken(const std::string& breakage) {
    return "format_version: 1.0\n"
           "elements:\n"
           "  -\n"
           "    id: cpu\n"
           "    type: bar\n"
           "  -\n" +
           breakage +
           "    properties:\n"
           "      tune: 777\n";
}

TEST(OverwriteGuard, SaveRefusesWhenTheTargetWasOnlyPartiallyCarried) {
    struct Case {
        const char* label;
        std::string breakage;
        int dropped;
    };
    const std::vector<Case> cases = {
        {"第二個元件缺 id", "    type: ring_gauge\n", 1},
        {"第二個元件缺 type", "    id: tuner\n", 1},
        {"兩個元件 id 重複", "    id: cpu\n    type: ring_gauge\n", 1},
        {"第二個元件 id 型別錯", "    id: 42\n    type: ring_gauge\n", 1},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.label);
        Fixture fx;
        const std::string user_file = two_elements_second_broken(c.breakage);
        fx.tamper(user_file);

        // 開機 → 還原：讀得出來，但丟掉了一個元件。
        ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);
        EXPECT_EQ(fx.session.last_intake().dropped_elements, c.dropped);
        EXPECT_FALSE(fx.session.last_intake().carries_everything());
        ASSERT_EQ(fx.session.size(), 1u);  // tune 那一個不在記憶體裡

        // 預檢必須看得到警訊（修訂 2 這兩行都回綠燈）。
        EXPECT_FALSE(fx.session.target_is_safe_to_overwrite("desktop"));
        EXPECT_EQ(fx.session.dry_run_save("desktop"), SaveStatus::RefusedIncompleteTarget);

        // ← 修訂 2 在這裡回 Ok 並讓 tune: 777 永久消失
        EXPECT_EQ(fx.session.save("desktop"), SaveStatus::RefusedIncompleteTarget);
        EXPECT_EQ(fx.stored(), user_file);                     // 位元組原封不動
        EXPECT_NE(fx.stored().find("777"), std::string::npos);  // 指紋還在
        EXPECT_EQ(fx.session.stats().saves_refused_incomplete, 1);
        EXPECT_EQ(fx.session.stats().saves_refused_target, 0);  // 這不是「讀不出來」
        EXPECT_EQ(fx.session.stats().rejected_backups, 0);

        // 訊息要說得出少了什麼。
        EXPECT_NE(fx.session.last_message().find("被丟棄的元件"), std::string::npos)
            << fx.session.last_message();
    }
}

// 另外兩種「帶不走」：整個 properties 區塊型別不對、以及模型根本沒有的鍵。
TEST(OverwriteGuard, SaveRefusesWhenPropertiesOrUnknownKeysCannotBeCarried) {
    {
        Fixture fx;
        const std::string f =
            "format_version: 1.0\n"
            "elements:\n"
            "  -\n"
            "    id: cpu\n"
            "    type: bar\n"
            "    properties: \"使用者打成了一段字串 tune=777\"\n";
        fx.tamper(f);
        ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);
        EXPECT_EQ(fx.session.last_intake().discarded_properties, 1);
        EXPECT_EQ(fx.session.save("desktop"), SaveStatus::RefusedIncompleteTarget);
        EXPECT_EQ(fx.stored(), f);
        EXPECT_NE(fx.session.last_message().find("properties"), std::string::npos);
    }
    {
        // 根層與元件層的未知鍵：本層的資料模型帶不走，`serialize_layout` 不會寫出來，
        // 所以覆寫 = 刪掉它們。
        Fixture fx;
        const std::string f =
            "format_version: 1.0\n"
            "wallpaper: /home/u/bg.png\n"   // 根層未知鍵
            "elements:\n"
            "  -\n"
            "    id: cpu\n"
            "    type: bar\n"
            "    note: 使用者自己加的註記\n";  // 元件層未知鍵
        fx.tamper(f);
        ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);
        EXPECT_EQ(fx.session.last_intake().unconsumed_keys, 2);
        EXPECT_EQ(fx.session.last_intake().dropped_elements, 0);
        EXPECT_EQ(fx.session.save("desktop"), SaveStatus::RefusedIncompleteTarget);
        EXPECT_EQ(fx.stored(), f);
        EXPECT_NE(fx.stored().find("wallpaper"), std::string::npos);
        EXPECT_NE(fx.session.last_message().find("帶不走的鍵"), std::string::npos);
    }
}

// 逃生門對「部分丟棄」一樣要通，而且一樣要留下備份。
TEST(OverwriteGuard, ExplicitDiscardWorksForPartiallyCarriedTargetsToo) {
    Fixture fx;
    const std::string user_file = two_elements_second_broken("    type: ring_gauge\n");
    fx.tamper(user_file);
    ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Repaired);

    EXPECT_EQ(fx.session.save("desktop", ds::host::edit::Overwrite::DiscardTargetContent),
              SaveStatus::Ok);
    EXPECT_EQ(fx.session.stats().rejected_backups, 1);

    std::string kept;
    ASSERT_TRUE(fx.storage.get(std::string("desktop") + ds::host::edit::kRejectedSuffix, kept));
    EXPECT_EQ(kept, user_file);
    EXPECT_NE(kept.find("777"), std::string::npos);  // 被丟掉的那一塊救得回來
}

TEST(OverwriteGuard, ExplicitDiscardStillKeepsTheOriginalBytes) {
    Fixture fx;
    const std::string original = fx.stored();
    std::string broken = original;
    broken.insert(broken.find("elements:"), "\t");
    fx.tamper(broken);
    ASSERT_EQ(fx.session.restore("desktop"), IntakeStatus::Malformed);

    // 使用者看到提示後明示「我要丟掉那份壞檔案」。
    EXPECT_EQ(fx.session.save("desktop", ds::host::edit::Overwrite::DiscardTargetContent),
              SaveStatus::Ok);
    EXPECT_EQ(fx.session.stats().rejected_backups, 1);

    // 新檔案可讀。
    EXPECT_NE(fx.stored(), broken);
    EXPECT_EQ(ds::host::edit::ingest_layout_text(fx.stored()).status, IntakeStatus::Ok);

    // 而原文**仍然在**：連「明示丟棄」都不真的丟掉位元組。
    std::string kept;
    ASSERT_TRUE(fx.storage.get(std::string("desktop") + ds::host::edit::kRejectedSuffix, kept));
    EXPECT_EQ(kept, broken);
}

// 備份**永遠不覆寫任何既有的名字**（ACC 的 R8）。
// 修訂 2 的備份是直接 `put(name + ".rejected", ...)`，那是整個修訂裡唯一一處
// 「沒先檢查就寫」——剛好有一份同名的可讀 profile 就會被它蓋掉。
TEST(OverwriteGuard, BackupNeverClobbersAnExistingName) {
    Fixture fx;
    const std::string backup_name = std::string("desktop") + ds::host::edit::kRejectedSuffix;

    // 使用者自己存了一份叫 `desktop.rejected` 的佈局（名字難看，但那是他的資料）。
    ASSERT_EQ(fx.session.save(backup_name), SaveStatus::Ok);
    const std::string precious = fx.stored(backup_name);

    std::string broken = fx.good_text;
    broken.insert(broken.find("elements:"), "\t");
    fx.tamper(broken);
    ASSERT_EQ(fx.session.save("desktop", ds::host::edit::Overwrite::DiscardTargetContent),
              SaveStatus::Ok);

    EXPECT_EQ(fx.stored(backup_name), precious);  // 他的資料沒被蓋掉
    std::string second;
    ASSERT_TRUE(fx.storage.get(backup_name + ".2", second));  // 備份改落在下一個空位
    EXPECT_EQ(second, broken);
}

TEST(OverwriteGuard, BackupSlotExhaustionIsNamedAndWritesNothing) {
    Fixture fx;
    const std::string backup_name = std::string("desktop") + ds::host::edit::kRejectedSuffix;
    fx.storage.put(backup_name, "occupied");
    for (int i = 2; i <= ds::host::edit::kMaxRejectedBackups; ++i) {
        fx.storage.put(backup_name + "." + std::to_string(i), "occupied");
    }
    const std::size_t before = fx.storage.names().size();

    std::string broken = fx.good_text;
    broken.insert(broken.find("elements:"), "\t");
    fx.tamper(broken);

    EXPECT_EQ(fx.session.save("desktop", ds::host::edit::Overwrite::DiscardTargetContent),
              SaveStatus::RefusedBackupSlotExhausted);
    EXPECT_EQ(fx.stored(), broken);                      // 目標沒被覆寫
    EXPECT_EQ(fx.storage.names().size(), before);        // 也沒有多出任何名字
    EXPECT_EQ(fx.session.stats().rejected_backups, 0);
}

TEST(OverwriteGuard, DiscardOnAReadableTargetMakesNoBackup) {
    Fixture fx;
    EXPECT_EQ(fx.session.save("desktop", ds::host::edit::Overwrite::DiscardTargetContent),
              SaveStatus::Ok);
    EXPECT_EQ(fx.session.stats().rejected_backups, 0);
    EXPECT_FALSE(fx.storage.has(std::string("desktop") + ds::host::edit::kRejectedSuffix));
}

TEST(OverwriteGuard, CallersCanAskBeforeSaving) {
    Fixture fx;
    ds::host::edit::IntakeReport why;

    EXPECT_TRUE(fx.session.target_is_safe_to_overwrite("desktop", &why));
    EXPECT_EQ(why.status, IntakeStatus::Ok);

    EXPECT_TRUE(fx.session.target_is_safe_to_overwrite("never_saved", &why));
    EXPECT_EQ(why.status, IntakeStatus::NotFound);  // 沒有東西會被毀掉

    std::string future = fx.stored();
    future.replace(future.find("format_version: 1.0"), 19, "format_version: 9.9");
    fx.tamper(future);
    EXPECT_FALSE(fx.session.target_is_safe_to_overwrite("desktop", &why));
    EXPECT_EQ(why.status, IntakeStatus::VersionTooNew);

    // 預檢看得到同一個拒絕；而只檢查記憶體狀態（不給名字）仍然是 Ok。
    EXPECT_EQ(fx.session.dry_run_save("desktop"), SaveStatus::RefusedUnreadableTarget);
    EXPECT_EQ(fx.session.dry_run_save(), SaveStatus::Ok);
    EXPECT_EQ(fx.stored(), future);  // 預檢不碰 storage
}

// -----------------------------------------------------------------------------
// 8. 非有限數值的守衛必須是**深層**的（ACC 的 F2）
// -----------------------------------------------------------------------------

TEST(DeepNonFiniteGuard, NestedNanCannotEnterThroughEditProperty) {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(0, 0, 10, 10)), EditStatus::Ok);

    // 第一版只查 `value.is_number()`，所以下面這兩個都會 Ok 並把 NaN / Inf 寫進持久化狀態。
    EXPECT_EQ(session.edit_property("a", "cfg",
                                    Value::map({{"inner", Value::number(nan)}})),
              EditStatus::NonFiniteValue);
    EXPECT_EQ(session.edit_property("a", "arr", Value::list({Value::number(inf)})),
              EditStatus::NonFiniteValue);
    EXPECT_EQ(session.edit_property(
                  "a", "deep",
                  Value::map({{"list", Value::list({Value::map(
                                           {{"x", Value::number(-inf)}})})}})),
              EditStatus::NonFiniteValue);

    // 訊息要指名道姓到路徑，呼叫端才不必猜。
    EXPECT_NE(session.last_message().find("list[0].x"), std::string::npos)
        << session.last_message();

    // 什麼都沒寫進去。
    Value out;
    EXPECT_FALSE(session.property_of("a", "cfg", out));
    EXPECT_FALSE(session.property_of("a", "arr", out));
    EXPECT_FALSE(session.property_of("a", "deep", out));
    EXPECT_EQ(session.save("x"), SaveStatus::Ok);  // 狀態仍然乾淨

    // 有限數的巢狀值照常可寫（守衛不是一律擋掉容器）。
    EXPECT_EQ(session.edit_property("a", "cfg", Value::map({{"inner", Value::number(1.5)}})),
              EditStatus::Ok);
    ASSERT_TRUE(session.property_of("a", "cfg.inner", out));
    EXPECT_DOUBLE_EQ(out.as_number(), 1.5);
}

TEST(DeepNonFiniteGuard, PathsAreReportedForEveryOffender) {
    const double nan = std::nan("");
    EXPECT_TRUE(ds::host::edit::non_finite_paths(Value::number(nan)).size() == 1u);
    EXPECT_EQ(ds::host::edit::non_finite_paths(Value::number(nan))[0], "");  // 根自己

    const Value tree = Value::map({{"ok", Value::number(1.0)},
                                   {"bad", Value::number(nan)},
                                   {"nest", Value::map({{"deep", Value::number(nan)}})},
                                   {"arr", Value::list({Value::number(0.0),
                                                        Value::number(nan)})},
                                   {"text", Value::string("nan")}});
    const std::vector<std::string> paths = ds::host::edit::non_finite_paths(tree);
    ASSERT_EQ(paths.size(), 3u);
    EXPECT_EQ(paths[0], "bad");
    EXPECT_EQ(paths[1], "nest.deep");
    EXPECT_EQ(paths[2], "arr[1]");
    EXPECT_TRUE(ds::host::edit::contains_non_finite(tree));
    EXPECT_FALSE(ds::host::edit::contains_non_finite(
        Value::map({{"a", Value::number(1.0)}, {"b", Value::string("inf")}})));
}

// `reset()` 仍然是不消毒的後門（刻意的），但失敗訊息必須說得出是哪一欄。
TEST(DeepNonFiniteGuard, VerifyFailedNamesTheOffendingFields) {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    Fixture fx;
    const std::string good = fx.stored();

    ds::layout::LayoutState poisoned = fx.session.state();
    poisoned.elements[0].properties = ds::format::set_value(
        poisoned.elements[0].properties, "cfg", Value::map({{"inner", Value::number(nan)}}));
    poisoned.elements[0].properties = ds::format::set_value(
        poisoned.elements[0].properties, "arr", Value::list({Value::number(inf)}));
    fx.session.reset(poisoned);

    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::VerifyFailed);
    EXPECT_EQ(fx.stored(), good);  // storage 未被動

    // 一次列出**全部**路徑 —— 第一版只說「往返自驗不通過（Ok）：」，修完一個才發現還有下一個。
    const std::string& msg = fx.session.last_message();
    EXPECT_NE(msg.find("'good'.properties.cfg.inner"), std::string::npos) << msg;
    EXPECT_NE(msg.find("'good'.properties.arr[0]"), std::string::npos) << msg;

    // 而且清得掉：照訊息給的路徑把它們移除，就存得回去了。
    ASSERT_EQ(fx.session.remove_property("good", "cfg"), EditStatus::Ok);
    ASSERT_EQ(fx.session.remove_property("good", "arr"), EditStatus::Ok);
    EXPECT_EQ(fx.session.save("desktop"), SaveStatus::Ok);
}

// `describe_mismatch` 的 properties 分支（ACC 的 R11：修 F3 的同一次提交又新增了一行
// 沒有斷言保護的診斷碼）。觸發方式用的是 **E7-12 自己載明的已知限制**：空容器序列化後
// 再解析會塌成 null，所以一個空 Map 的屬性不 round-trip。
TEST(DeepNonFiniteGuard, VerifyFailureNamesTheElementWhosePropertiesDidNotRoundTrip) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(0, 0, 10, 10)), EditStatus::Ok);
    ASSERT_EQ(session.edit_property("a", "cfg", Value::map({})), EditStatus::Ok);

    EXPECT_EQ(session.save("x"), SaveStatus::VerifyFailed);
    EXPECT_FALSE(storage.has("x"));  // 沒提交
    const std::string& msg = session.last_message();
    EXPECT_NE(msg.find("往返自驗不通過"), std::string::npos) << msg;
    EXPECT_NE(msg.find("元件 'a' 的 properties 往返後不一致"), std::string::npos) << msg;

    // 清掉那個空容器就存得回去了。
    ASSERT_EQ(session.remove_property("a", "cfg"), EditStatus::Ok);
    EXPECT_EQ(session.save("x"), SaveStatus::Ok);
}

TEST(DeepNonFiniteGuard, RemovePropertyContract) {
    ds::layout::MemoryLayoutStorage storage;
    EditAssembly session(storage);
    ASSERT_EQ(session.add_element("a", "bar", place(0, 0, 10, 10)), EditStatus::Ok);
    ASSERT_EQ(session.edit_property("a", "cfg.inner", Value::number(1.0)), EditStatus::Ok);

    EXPECT_EQ(session.remove_property("nope", "x"), EditStatus::UnknownElement);
    EXPECT_EQ(session.remove_property("a", "not_there"), EditStatus::BadPath);
    EXPECT_EQ(session.remove_property("a", ""), EditStatus::BadPath);
    EXPECT_EQ(session.remove_property("a", "bad["), EditStatus::BadPath);

    Value out;
    ASSERT_EQ(session.remove_property("a", "cfg.inner"), EditStatus::Ok);
    EXPECT_FALSE(session.property_of("a", "cfg.inner", out));
    ASSERT_TRUE(session.property_of("a", "cfg", out));  // 父節點還在，只少了那個鍵
    EXPECT_EQ(out.size(), 0u);
    ASSERT_TRUE(session.property_of("a", "x", out));    // 其他鍵沒被波及
}

// -----------------------------------------------------------------------------
// 9. 兩個像素原語自己要被測到（ACC 的 F3：有防線、沒有斷言保護）
// -----------------------------------------------------------------------------
//
// 這兩個函式原本藏在 .cpp 的匿名命名空間裡，而所有呼叫端都先過 `clamp_finite`，
// 於是「拿掉它們裡面的防線」不會有任何測試紅（MX5 / MX6 存活）。公開 + 直接測，
// 它們才真的被釘住。

TEST(PixelPrimitives, RoundToPxNeverEmitsAnOverflowingCast) {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();

    EXPECT_EQ(ds::host::edit::round_to_px(nan), 0);   // 非有限 → 0（不是垃圾整數）
    EXPECT_EQ(ds::host::edit::round_to_px(inf), 0);
    EXPECT_EQ(ds::host::edit::round_to_px(-inf), 0);

    // 夾限：超出範圍的有限數**不得**直接轉型（float-cast-overflow 會當場 FAILED）。
    EXPECT_EQ(ds::host::edit::round_to_px(1.0e308), ds::host::edit::round_to_px(1.0e300));
    EXPECT_GT(ds::host::edit::round_to_px(1.0e308), 0);
    EXPECT_LT(ds::host::edit::round_to_px(-1.0e308), 0);

    // 正常範圍內就是四捨五入。
    EXPECT_EQ(ds::host::edit::round_to_px(0.0), 0);
    EXPECT_EQ(ds::host::edit::round_to_px(12.4), 12);
    EXPECT_EQ(ds::host::edit::round_to_px(12.5), 13);
    EXPECT_EQ(ds::host::edit::round_to_px(-12.5), -13);
}

// 右緣必須由「**邏輯座標的和**再縮放」算出，不是「各自縮放再相加」。
//
// 這兩組是 V1.8 構造出來的、真值剛好貼在 `lround` 的 .5 邊界上的案例：兩種算法差的是
// **整整 1 個像素**，不是 ULP。修訂 2 的 CHG 把這條差異稱為「等價突變」是用詞錯誤——
// 等價突變的意思是「不可能被任何測試殺死」，而它顯然殺得死，只是當時沒人去構造輸入。
TEST(PixelPrimitives, RightEdgeIsTheScaledSumNotTheSumOfScaledParts) {
    struct Case {
        double x;
        double w;
        double scale;
    };
    const Case cases[] = {
        {-311820.55261569237, 376612.82534296508, 1.1},
        {-451682.21715042065, 779001.21715042065, 1.5},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(std::to_string(c.x) + " + " + std::to_string(c.w) + " @" +
                     std::to_string(c.scale));
        const int correct = ds::host::edit::round_to_px((c.x + c.w) * c.scale);
        const int parts = ds::host::edit::round_to_px(c.x * c.scale + c.w * c.scale);
        ASSERT_NE(correct, parts) << "這組輸入沒有落在取整邊界上，測不到東西";

        const ds::host::PaintBox box =
            ds::host::edit::resolve_placement(place(c.x, 0.0, c.w, 1.0), c.scale);
        EXPECT_EQ(box.right, correct);
    }
}

TEST(PixelPrimitives, NormalizeBoxKeepsTheHalfOpenConvention) {
    const ds::host::PaintBox ok = ds::host::edit::normalize_box(10, 20, 30, 40);
    EXPECT_EQ(ok.left, 10);
    EXPECT_EQ(ok.right, 30);
    EXPECT_EQ(ok.width(), 20);

    // 反過來的框會被夾平成退化矩形，而不是交出一個負寬度。
    const ds::host::PaintBox flipped = ds::host::edit::normalize_box(30, 40, 10, 20);
    EXPECT_EQ(flipped.left, 30);
    EXPECT_EQ(flipped.right, 30);
    EXPECT_EQ(flipped.width(), 0);
    EXPECT_EQ(flipped.height(), 0);
    EXPECT_TRUE(flipped.empty());
    EXPECT_GE(flipped.right, flipped.left);
    EXPECT_GE(flipped.bottom, flipped.top);
}

TEST(KeepLastGood, StatusStringsAreAllDistinct) {
    // 具名狀態要真的「具名」—— 兩個狀態共用一個字串等於沒分。
    const IntakeStatus all[] = {
        IntakeStatus::Ok,           IntakeStatus::Repaired,
        IntakeStatus::Migrated,     IntakeStatus::NotFound,
        IntakeStatus::MissingVersion, IntakeStatus::MalformedVersion,
        IntakeStatus::VersionTooNew, IntakeStatus::MigrationUnavailable,
        IntakeStatus::MigrationFailed, IntakeStatus::Malformed,
        IntakeStatus::StructureInvalid};
    for (std::size_t i = 0; i < std::size(all); ++i) {
        for (std::size_t j = i + 1; j < std::size(all); ++j) {
            EXPECT_STRNE(ds::host::edit::to_string(all[i]), ds::host::edit::to_string(all[j]));
        }
    }
    EXPECT_STREQ(ds::host::edit::to_string(EditStatus::NonFiniteValue), "NonFiniteValue");
    EXPECT_STREQ(ds::host::edit::to_string(SaveStatus::VerifyFailed), "VerifyFailed");
    EXPECT_STREQ(ds::host::edit::to_string(ds::host::edit::ReloadOutcome::Rejected),
                 "Rejected");
    EXPECT_STREQ(ds::host::edit::to_string(ds::host::edit::VersionPeek::Missing), "Missing");
}

}  // namespace
