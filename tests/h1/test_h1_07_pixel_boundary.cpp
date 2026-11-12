// tests/h1/test_h1_07_pixel_boundary.cpp — H1-07 的**反向斷言**（gtest）
//
// 驗收標準：**E4 與 kernel 的公開介面不得出現任何像素型別** —— H1-07 是全系統唯一知道
// 像素的一層，往上滲會在相位 3 換後端時才爆，而 `backend_guard` 擋不到（它擋路徑，不看型別）。
//
// 這一條**兩個方向都容易寫壞**，所以本檔刻意把兩個方向分開釘住：
//
//   **方向一：它真的紅得起來**（不是空轉的檢查）
//     - `RuleCatches*` 一組：以**合成字串**餵判定規則，證明三條規則各自會命中
//       （含一份「E4 標頭被滲透」的合成樣本：裡面有 `PaintBox` 與 `widget_offset_px`）。
//     - `ControlRealFilesWherePixelsLegitimatelyLive`：對**真實檔案**（`host/paint/` 與
//       `host/compose/` —— 像素正當存在的那兩層）掃出非零命中，證明掃描器在真實輸入上
//       不是瞎的。
//     - `RealHeadersMatchExpectedBaselineExactly` 的斷言形式是**等於**預期基線，
//       所以一個「永遠回空集合」的掃描器在這裡是**紅**的，不是綠的。
//
//   **方向二：它不會假紅**
//     - `RuleDoesNotFlagAlphaCompositionNames`：`AlphaMode::PerPixel` /
//       `kPerPixelAlphaCapability` / `per_pixel_alpha` 一律**不**命中。那些是 alpha 合成
//       模式的名稱（逐像素 alpha 通道 vs 整體透明度），不是像素座標／尺寸型別。
//       天真的 `grep -rnE '\bpixels?\b|_px\b|Pixel'` 在排除註解行後仍會命中它們 ——
//       本規則以「必須同時有像素單位詞**與**幾何角色詞」的合取條件把它們排除。
//     - `RuleDoesNotFlagRatioOrNamedGeometry` / `RuleDoesNotFlagNonPixelUnits`：
//       比例值型幾何（`float x`）、行數（`offset_lines`）、旗標（`bool at_top`）、
//       私有成員（`width_`）都不命中 —— 每一條都對應一個**現存**的上游案例。
//     - `RealHeadersMatchExpectedBaselineExactly` 現況必須綠。
#include "pixel_boundary.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#ifndef DS_H1_07_REPO_ROOT
#error "DS_H1_07_REPO_ROOT 未定義 —— 反向斷言要掃 repo 內的真實標頭，必須由 CMake 傳入 repo 根"
#endif

namespace audit = ds::host::pixel_audit;

namespace {

const std::string& repo_root() {
    static const std::string kRoot = DS_H1_07_REPO_ROOT;
    return kRoot;
}

std::size_t count_rule(const std::vector<audit::Finding>& findings, audit::Rule rule) {
    std::size_t n = 0;
    for (const audit::Finding& f : findings) {
        if (f.rule == rule) ++n;
    }
    return n;
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) {
        out += "\n  " + s;
    }
    return out;
}

}  // namespace

// =============================================================================
// 拆詞 —— 三條規則都建在它上面，所以先把它釘死
// =============================================================================
TEST(PixelAuditWords, SplitsOnUnderscoreAndCamelCase) {
    using V = std::vector<std::string>;
    EXPECT_EQ(audit::split_identifier_words("width_px"), (V{"width", "px"}));
    EXPECT_EQ(audit::split_identifier_words("PixelRect"), (V{"pixel", "rect"}));
    EXPECT_EQ(audit::split_identifier_words("offset_lines"), (V{"offset", "lines"}));
    EXPECT_EQ(audit::split_identifier_words("PerPixel"), (V{"per", "pixel"}));
    EXPECT_EQ(audit::split_identifier_words("kPerPixelAlphaCapability"),
              (V{"k", "per", "pixel", "alpha", "capability"}));
    // 連續大寫後緊跟小寫 → 在最後一個大寫前斷詞（HTTPServer → http | server）。
    EXPECT_EQ(audit::split_identifier_words("HTTPServerX"), (V{"http", "server", "x"}));
    EXPECT_TRUE(audit::split_identifier_words("").empty());
}

// =============================================================================
// 方向一：規則真的會命中（合成輸入）
// =============================================================================
TEST(PixelAuditRule, R1CatchesHostPixelTypeNames) {
    EXPECT_TRUE(audit::is_host_pixel_type_name("PaintBox"));       // H1-06
    EXPECT_TRUE(audit::is_host_pixel_type_name("PixelPoint"));     // H1-07
    EXPECT_TRUE(audit::is_host_pixel_type_name("WidgetLayout"));   // H1-07
    EXPECT_TRUE(audit::is_host_pixel_type_name("RECT"));           // win32
    EXPECT_TRUE(audit::is_host_pixel_type_name("CGRect"));         // cocoa
}

TEST(PixelAuditRule, R2CatchesPixelUnitGeometryIdentifiers) {
    EXPECT_TRUE(audit::is_pixel_geometry_identifier("width_px"));
    EXPECT_TRUE(audit::is_pixel_geometry_identifier("pixel_offset"));
    EXPECT_TRUE(audit::is_pixel_geometry_identifier("PixelRect"));
    EXPECT_TRUE(audit::is_pixel_geometry_identifier("origin_in_pixels"));
    EXPECT_TRUE(audit::is_pixel_geometry_identifier("kThicknessPx"));
}

TEST(PixelAuditRule, R3CatchesIntegralGeometryFields) {
    const std::string leaked =
        "namespace ds::elements {\n"
        "struct SomeElementRenderModel {\n"
        "    int width = 0;\n"
        "    int height = 0;\n"
        "};\n"
        "}\n";
    const auto findings = audit::scan_source("synthetic/leaked.hpp", leaked);
    EXPECT_EQ(count_rule(findings, audit::Rule::IntegralGeometryField), 2u);
}

// 一份「E4 標頭真的被滲透」的合成樣本：三條規則各自該命中。
TEST(PixelAuditRule, CatchesASyntheticE4Leak) {
    const std::string leaked =
        "#include \"render_dispatch.hpp\"\n"
        "namespace ds::elements {\n"
        "struct BadRenderModel {\n"
        "    ds::host::PaintBox bounds;\n"   // R1
        "    int widget_offset_px = 0;\n"    // R2（+ R3）
        "    int left = 0;\n"                // R3
        "};\n"
        "}\n";
    const auto findings = audit::scan_source("synthetic/bad.hpp", leaked);
    EXPECT_GE(count_rule(findings, audit::Rule::HostPixelTypeName), 1u);
    EXPECT_GE(count_rule(findings, audit::Rule::PixelUnitIdentifier), 1u);
    EXPECT_GE(count_rule(findings, audit::Rule::IntegralGeometryField), 2u);
}

// =============================================================================
// 方向二：規則不會假紅
// =============================================================================
// 【這是本檔最重要的一組】`AlphaMode::PerPixel` 這一族是 alpha 合成模式的名稱，
// 不是像素座標／尺寸型別。天真的 grep 會把它們算成違反。
TEST(PixelAuditRule, DoesNotFlagAlphaCompositionNames) {
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("PerPixel"));
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("kPerPixelAlphaCapability"));
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("per_pixel_alpha"));
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("AlphaMode"));
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("AlphaProfile"));
    EXPECT_FALSE(audit::is_pixel_geometry_identifier("alpha_query"));
    EXPECT_FALSE(audit::is_host_pixel_type_name("AlphaMode"));
    EXPECT_FALSE(audit::is_host_pixel_type_name("PerPixel"));
}

// `src/kernel/alpha_surface.hpp` 與 `modules/elements/dim_overlay.hpp` 的真實形狀，
// 原樣貼成合成輸入：零命中。
TEST(PixelAuditRule, AcceptsTheRealAlphaSurfaceShape) {
    const std::string real =
        "namespace ds::kernel {\n"
        "inline constexpr const char* kPerPixelAlphaCapability = \"surface.per_pixel_alpha\";\n"
        "enum class AlphaMode {\n"
        "    PerPixel,\n"
        "    Uniform,\n"
        "};\n"
        "struct AlphaProfile {\n"
        "    AlphaMode mode = AlphaMode::PerPixel;\n"
        "    double opacity = 1.0;\n"
        "};\n"
        "}\n"
        "namespace ds::elements {\n"
        "struct DimRenderModel {\n"
        "    bool alpha_supported = false;\n"
        "    ds::kernel::AlphaProfile composite_{};\n"
        "};\n"
        "}\n";
    const auto findings = audit::scan_source("synthetic/alpha.hpp", real);
    EXPECT_TRUE(findings.empty()) << "假紅：" << join(audit::finding_keys(findings));
}

// NFR-02 之下 E4 / kernel 的幾何是**比例值**，不是像素 —— 比例值型幾何不得命中。
TEST(PixelAuditRule, DoesNotFlagRatioGeometry) {
    const std::string ratios =
        "namespace ds::kernel {\n"
        "struct Offset {\n"
        "    float dx = 0.0f;\n"
        "    float dy = 0.0f;\n"
        "};\n"
        "struct Size {\n"
        "    float width = 0.0f;\n"
        "    float height = 0.0f;\n"
        "};\n"
        "struct ResolvedPlacement {\n"
        "    float x = 0.0f;\n"
        "    float y = 0.0f;\n"
        "};\n"
        "enum class ShapeKind { Rect, RoundedRect, Circle };\n"
        "}\n";
    const auto findings = audit::scan_source("synthetic/ratio.hpp", ratios);
    EXPECT_TRUE(findings.empty()) << "假紅：" << join(audit::finding_keys(findings));
}

// 名稱自帶非像素單位詞（行數）、旗標、私有成員 —— 三者皆有現存的上游案例。
TEST(PixelAuditRule, DoesNotFlagNonPixelUnitsFlagsOrPrivateMembers) {
    const std::string scroll =
        "namespace ds::elements {\n"
        "struct ScrollPosition {\n"
        "    std::size_t offset_lines = 0;\n"      // 行數，不是像素 y
        "    std::size_t max_offset_lines = 0;\n"
        "    double ratio = 0.0;\n"
        "    bool at_top = true;\n"                // 旗標不是座標
        "    bool at_bottom = true;\n"
        "};\n"
        "class ScrollView {\n"
        " private:\n"
        "    std::size_t offset_lines_ = 0;\n"     // 私有成員，非公開介面
        "    int width_ = 0;\n"
        "};\n"
        "}\n";
    const auto findings = audit::scan_source("synthetic/scroll.hpp", scroll);
    EXPECT_TRUE(findings.empty()) << "假紅：" << join(audit::finding_keys(findings));
}

// 註解與字串字面值不算介面；且行號必須保留（命中指得回原始檔）。
TEST(PixelAuditRule, IgnoresCommentsAndLiteralsButKeepsLineNumbers) {
    const std::string src =
        "// line 1: 這行註解提到 PaintBox 與 width_px，都不該命中\n"
        "const char* kDoc = \"PaintBox width_px\";\n"
        "/* line 3\n"
        "   line 4 註解裡的 PaintBox 也不該命中 */\n"
        "ds::host::PaintBox real_leak_on_line_5;\n";
    const auto findings = audit::scan_source("synthetic/comments.hpp", src);
    ASSERT_EQ(findings.size(), 1u) << join(audit::finding_keys(findings));
    EXPECT_EQ(findings[0].identifier, std::string("PaintBox"));
    EXPECT_EQ(findings[0].rule, audit::Rule::HostPixelTypeName);
    EXPECT_EQ(findings[0].line, 5);
}

// =============================================================================
// 真實檔案：掃描目標的枚舉
// =============================================================================
TEST(PixelAuditTargets, EnumeratesE4AndKernelPublicHeaders) {
    const std::vector<std::string> headers = audit::interface_headers(repo_root());
    // 反真空：掃到個位數檔案時「零命中」毫無意義。現況為 22（E4）+ 23（kernel）= 45。
    EXPECT_GE(headers.size(), 40u) << "掃描目標只有 " << headers.size() << " 個檔，疑似枚舉壞了";

    const std::set<std::string> as_set(headers.begin(), headers.end());
    // 逐一點名幾個**必須**在裡面的檔（枚舉若悄悄漏掉某個目錄，這幾行會先紅）。
    EXPECT_EQ(as_set.count("modules/elements/image_element.hpp"), 1u);
    EXPECT_EQ(as_set.count("modules/elements/bar_gauge.hpp"), 1u);
    EXPECT_EQ(as_set.count("modules/elements/scroll_view.hpp"), 1u);
    EXPECT_EQ(as_set.count("src/kernel/alpha_surface.hpp"), 1u);
    EXPECT_EQ(as_set.count("src/kernel/anchor_model.hpp"), 1u);
    EXPECT_EQ(as_set.count("src/kernel/dpi_awareness.hpp"), 1u);

    // **不遞迴進後端**：`src/kernel/backend/` 就是知道平台細節的地方，不是公開介面。
    for (const std::string& h : headers) {
        EXPECT_EQ(h.find("src/kernel/backend/"), std::string::npos) << h;
    }
}

TEST(PixelAuditTargets, ReadsEveryTargetFileAndSeesRealBytes) {
    const audit::ScanReport report =
        audit::scan_files(repo_root(), audit::interface_headers(repo_root()));
    EXPECT_TRUE(report.errors.empty()) << "讀不到檔：" << join(report.errors);
    EXPECT_GE(report.files.size(), 40u);
    // 反真空第二道：真的讀到內容，不是一堆空字串（空檔會被 read_file 當成錯誤，
    // 但這一行額外釘住「總量級」，免得某天掃到一堆 1 位元組的檔還以為乾淨）。
    EXPECT_GT(report.bytes, 100000u) << "只讀到 " << report.bytes << " 位元組";
}

// =============================================================================
// 【核心斷言】E4 與 kernel 的公開介面，命中集合必須恰好等於具名預期基線
// =============================================================================
// 斷言形式是**等於**，不是「小於某個數」：
//   - 多出任何一筆 → 紅（真的有像素型別滲上去了）
//   - 基線那幾筆消失 → 也紅（掃描器瞎了：路徑錯、拆詞改壞、檔案讀不到）
// 後者正是「寫成只比對不存在的字串就永遠綠」的解藥。
TEST(PixelBoundary, E4AndKernelPublicHeadersMatchExpectedBaselineExactly) {
    const audit::ScanReport report =
        audit::scan_files(repo_root(), audit::interface_headers(repo_root()));
    ASSERT_TRUE(report.errors.empty()) << "讀不到檔：" << join(report.errors);

    const std::vector<std::string> got = audit::finding_keys(report.findings);
    const std::vector<std::string> expected = audit::baseline_keys();
    EXPECT_EQ(got, expected) << "實際命中：" << join(got) << "\n預期基線：" << join(expected);
}

// 三條規則各自在真實標頭上的現況，分開釘住（哪一條變了，看得出來是哪一條）。
TEST(PixelBoundary, NoHostPixelTypeNameAndNoPixelUnitIdentifierInE4OrKernel) {
    const audit::ScanReport report =
        audit::scan_files(repo_root(), audit::interface_headers(repo_root()));
    ASSERT_TRUE(report.errors.empty()) << join(report.errors);

    std::vector<std::string> r1_r2;
    for (const audit::Finding& f : report.findings) {
        if (f.rule != audit::Rule::IntegralGeometryField) {
            r1_r2.push_back(f.file + ":" + std::to_string(f.line) + " " + f.identifier + " (" +
                            audit::to_string(f.rule) + ")");
        }
    }
    // R1 / R2 的現況是**零** —— host 的像素型別名與 px 命名的幾何識別字都沒有往上滲。
    EXPECT_TRUE(r1_r2.empty()) << join(r1_r2);
}

// 預期基線本身不得被偷偷清空 / 清掉理由 —— 否則「等於基線」會退化成「等於空集合」，
// 而那又會被一個瞎掉的掃描器滿足。
TEST(PixelBoundary, BaselineIsNonEmptyAndEveryEntryIsJustified) {
    const auto& baseline = audit::expected_baseline();
    ASSERT_FALSE(baseline.empty());
    for (const audit::BaselineEntry& b : baseline) {
        EXPECT_FALSE(b.file.empty());
        EXPECT_FALSE(b.identifier.empty());
        EXPECT_FALSE(b.justification.empty()) << b.file << " / " << b.identifier;
    }
    // 現況：只有 ImageDimensions 的兩個固有像素尺寸欄位（來源影像自身的資料，
    // 換平台後端不會改變；非 widget / 螢幕的擺放幾何）。
    EXPECT_EQ(baseline.size(), 2u);
    for (const audit::BaselineEntry& b : baseline) {
        EXPECT_EQ(b.file, std::string("modules/elements/image_element.hpp"));
        EXPECT_EQ(b.rule, audit::Rule::IntegralGeometryField);
    }
}

// =============================================================================
// 對照組：像素正當存在的那兩層，掃描器必須看得見
// =============================================================================
// 這一組證明「零命中」是關於**被掃的內容**而不是關於掃描器壞了。
TEST(PixelBoundary, ControlRealFilesWherePixelsLegitimatelyLive) {
    const std::vector<std::string> host_files = {
        "host/paint/render_dispatch.hpp",   // H1-06：PaintBox + int left/top/right/bottom
        "host/compose/widget_compose.hpp",  // H1-07：PixelPoint / PixelSize + int x/y/width/height
    };
    const audit::ScanReport report = audit::scan_files(repo_root(), host_files);
    ASSERT_TRUE(report.errors.empty()) << join(report.errors);
    ASSERT_EQ(report.files.size(), 2u);

    for (const std::string& f : host_files) {
        std::size_t r1 = 0;
        std::size_t r3 = 0;
        for (const audit::Finding& hit : report.findings) {
            if (hit.file != f) continue;
            if (hit.rule == audit::Rule::HostPixelTypeName) ++r1;
            if (hit.rule == audit::Rule::IntegralGeometryField) ++r3;
        }
        EXPECT_GT(r1, 0u) << f << " 掃不到任何 host 像素型別名 —— 掃描器可能瞎了";
        EXPECT_GT(r3, 0u) << f << " 掃不到任何整數型幾何欄位 —— 掃描器可能瞎了";
    }
}

// 本單元**消費**的 engine 層標頭也不得帶 host 的像素型別（像素只在 host）。
// 不在驗收標準明文內，但同一條界線的延伸，順手釘住。
TEST(PixelBoundary, EngineCompositionHeadersCarryNoHostPixelTypes) {
    const audit::ScanReport report =
        audit::scan_files(repo_root(), {"engine/package/composition.hpp",
                                        "engine/package/package.hpp",
                                        "engine/package/manifest.hpp"});
    ASSERT_TRUE(report.errors.empty()) << join(report.errors);
    ASSERT_EQ(report.files.size(), 3u);
    EXPECT_EQ(count_rule(report.findings, audit::Rule::HostPixelTypeName), 0u);
    EXPECT_EQ(count_rule(report.findings, audit::Rule::PixelUnitIdentifier), 0u);
}

// 讀不到的檔必須**明確報錯**，不得被當成「空檔 → 零命中 → 乾淨」。
TEST(PixelAuditFiles, MissingOrEmptyFileIsAnErrorNotACleanResult) {
    const audit::ScanReport report =
        audit::scan_files(repo_root(), {"modules/elements/this_file_does_not_exist.hpp"});
    EXPECT_TRUE(report.files.empty());
    EXPECT_EQ(report.bytes, 0u);
    EXPECT_EQ(report.findings.size(), 0u);
    ASSERT_EQ(report.errors.size(), 1u);  // ← 零命中但**有錯誤**：呼叫端必須據此判紅
}
