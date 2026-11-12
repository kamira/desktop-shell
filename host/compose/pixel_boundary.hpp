// H1-07 反向斷言的機器化判定 —— E4 與 kernel 的公開介面不得出現任何像素型別
//
// 平台中立、零相依（只用標準庫），**不包 `if(WIN32)`**：這條斷言在哪個平台上都該成立，
// 而唯一真的會跑它的是 ubuntu 的 G3 與 `gate_sanitizers`。
//
// ---------------------------------------------------------------------------
// 要防的到底是什麼
// ---------------------------------------------------------------------------
// H1-07（連同 H1-06 的繪製層）是全系統**唯一知道像素的一層**。像素型別往上滲進 E4
// （`modules/elements/`）或 kernel（`src/kernel/`）的公開介面，會在相位 3 換平台後端時
// 才爆，而 `backend_guard` 擋不到 —— 它擋的是 `src/kernel/backend/win32|cocoa/` 這類
// **路徑**，不看型別。NFR-02 的正面表述是「具名佈局，非座標」：E4 與 kernel 的幾何一律以
// **比例值**（`float` / `double`，[0,1] 或容器尺寸的分數）或**具名 / 索引值**表達。
//
// ---------------------------------------------------------------------------
// 【關鍵】天真的 grep 會給出假的「已被違反」
// ---------------------------------------------------------------------------
//     grep -rnE '\bpixels?\b|_px\b|Pixel' modules/elements/*.hpp src/kernel/*.hpp
// 排除註解行後仍有命中，全部是：
//     src/kernel/alpha_surface.hpp:  AlphaMode::PerPixel / kPerPixelAlphaCapability
//     modules/elements/{dim_overlay,gradient_fill,histogram,line_chart}.hpp:
//         per-pixel alpha 的能力旗標與 AlphaProfile 預設
// 那些是 **alpha 合成模式的名稱**（逐像素 alpha 通道 vs 整體透明度），
// **不是像素座標／尺寸／偏移的型別**。所以判定必須針對「像素值的幾何型別」，
// 而不是「pixel 這個字」。這一條**兩個方向都容易寫錯**：寫太鬆就誤報紅，
// 寫成只比對不存在的字串就永遠綠。下面三條規則與 §預期基線就是為此設計的。
//
// ---------------------------------------------------------------------------
// 三條互補規則（三條都只看**去註解、去字串/字元字面值**後的程式碼）
// ---------------------------------------------------------------------------
//   **R1 型別名滲透**：`host/` 像素層自有的像素幾何型別名（H1-06 的 `PaintBox`、本單元的
//     `PixelPoint` / `PixelSize` / `ComponentPlacement` / `ResolvedComponent` /
//     `WidgetLayout` / `ComponentAppearance`），以及平台的像素矩形（`RECT` / `POINT` /
//     `SIZE`），不得作為識別字出現在被掃檔案中。這是「往上滲」最直接的形狀。
//     **刻意不收 `Rect` / `Point` / `Size` 這些混合大小寫的通名**：
//     `ds::kernel::ShapeKind::Rect`（具名形狀分類）、`ds::elements::Point`（圖形資料的
//     本地比例座標）、`ds::kernel::Size`（`resolve()` 的輸入，單位由呼叫端決定）都是正當的，
//     把它們收進規則等於製造必然的誤報。
//
//   **R2 像素單位識別字**：識別字拆詞後**同時**含
//       「像素單位詞」{px, pixel, pixels}  **且**  「幾何角色詞」{x, y, left, top, right,
//        bottom, width, height, size, offset, pos, position, coord(s), rect, box, margin,
//        padding, gap, inset, origin, bounds, extent, stride, thickness, radius, diameter,
//        dx, dy, cx, cy}
//     者命中。拆詞同時依底線與 CamelCase 邊界，全部轉小寫。
//     於是 `width_px` / `PixelRect` / `pixel_offset` 命中，而
//     **`PerPixel`（詞 = per, pixel）與 `kPerPixelAlphaCapability`
//     （詞 = k, per, pixel, alpha, capability）都沒有幾何角色詞 → 不命中**。
//     「要求兩類詞同時出現」就是把上述假命中排除掉的那個合取條件。
//
//   **R3 整數型幾何欄位**：宣告的型別含**整數型別** token
//     {int, unsigned, long, short, size_t, ptrdiff_t, int8/16/32/64_t, uint8/16/32/64_t, char,
//      signed}，且**欄位名含幾何角色詞**者命中 —— 因為 NFR-02 之下 E4 / kernel 的幾何是
//     比例值或具名/索引值，**整數型的幾何欄位才是像素的形狀**。
//     三個排除條件（每一條都有具體的現存案例）：
//       - 名稱自帶**非像素單位詞** {line, lines, ratio, fraction, percent, index, indices,
//         count, rows, columns, chars, cells, glyphs, pages, items} → 排除。
//         `ScrollPosition::offset_lines` 是**行數**而不是像素 y（該標頭明寫「行索引，非像素 y」）。
//       - 名稱**尾端底線**（本庫私有成員慣例）→ 排除（非公開介面）。
//       - `bool` 不算整數型：`ScrollPosition::at_top` 是旗標而非座標。
//
// ---------------------------------------------------------------------------
// 預期基線 —— 現況 R3 不是零，這點必須誠實寫明
// ---------------------------------------------------------------------------
// R3 在目前的 main 上恰好命中 **2** 筆，都在
// `modules/elements/image_element.hpp` 的 `ImageDimensions { int width; int height; }`
// （其註解明寫「固有像素寬／高」，且 `ImageRenderModel::source_dimensions` 用了它，
// 確實在 E4 的公開介面上）。
//
// **為什麼它不是這條驗收標準要防的滲透**：那是**來源影像自身資料的固有尺寸** ——
// 一張 PNG 有多少像素，換平台後端不會改變；該標頭也已明寫「非螢幕座標／非擺放位置；NFR-02」。
// 本斷言要防的是 **widget / 螢幕的擺放幾何**（座標／尺寸／偏移）滲上去，那才是相位 3 換後端
// 時會爆的東西。
//
// **處理方式刻意不是「把它從規則裡挖掉」，而是列成具名、附理由的預期基線並斷言
// 「命中集合 == 預期基線」：**
//   - 多出任何一筆（真的滲透）→ **紅**；
//   - 預期基線那兩筆消失（掃描器瞎了：路徑錯、regex 壞、檔案讀不到、拆詞改壞）→ **也紅**。
// 後者就是防「寫成只比對不存在的字串就永遠綠」的那道鎖 —— 一個永遠回空集合的掃描器
// 在這個斷言下是紅的，不是綠的。
#ifndef DS_HOST_COMPOSE_PIXEL_BOUNDARY_HPP
#define DS_HOST_COMPOSE_PIXEL_BOUNDARY_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ds::host::pixel_audit {

// 哪一條規則命中。
enum class Rule {
    HostPixelTypeName,      // R1
    PixelUnitIdentifier,    // R2
    IntegralGeometryField,  // R3
};

const char* to_string(Rule rule) noexcept;

// 一筆命中。`file` 是**傳進來的標籤**（本庫一律用 repo 相對路徑），`line` 為 1-based。
struct Finding {
    std::string file;
    int line = 0;
    std::string identifier;
    Rule rule = Rule::HostPixelTypeName;
    std::string detail;  // R3 填宣告的型別文字；其餘為空
};

bool operator==(const Finding& a, const Finding& b);

// --- 可單獨測試的判定零件（合成案例直接餵字串，不必有檔案）---

// 去掉 `//` 與 `/* */` 註解、以及字串 / 字元字面值的**內容**。
// **行號完全保留**（被去掉的字元以空白取代、換行原樣保留），所以命中的行號指得回原始檔。
// 註解不看是刻意的：「per-pixel alpha」寫在註解裡不是介面的一部分。
std::string strip_comments_and_literals(const std::string& source);

// 識別字拆詞：先依底線切，再依 CamelCase 邊界切，全部轉小寫。
// `kPerPixelAlphaCapability` → {k, per, pixel, alpha, capability}
// `offset_lines`             → {offset, lines}
// `HTTPServerX`              → {http, server, x}
std::vector<std::string> split_identifier_words(const std::string& identifier);

bool is_host_pixel_type_name(const std::string& identifier);          // R1
bool is_pixel_geometry_identifier(const std::string& identifier);     // R2

// 對一段原始碼文字跑三條規則。`file_label` 原樣填進每筆 Finding 的 `file`。
std::vector<Finding> scan_source(const std::string& file_label, const std::string& source);

// --- 檔案層 ---

// 讀檔。失敗（不存在 / 讀不到）回 false 並填 `error`；**不把失敗當成空字串**——
// 空字串掃出來是零命中，而零命中在本斷言裡會被誤讀成「乾淨」。
bool read_file(const std::string& path, std::string& out, std::string& error);

// 一次掃描的完整帳：掃了哪些檔、讀了幾個位元組、命中什麼、哪些檔讀不到。
// `bytes` / `files` 存在的理由是**反真空**：掃到 0 個檔或 0 個位元組時，
// 「零命中」毫無意義，呼叫端（測試）要能看見並據此判紅。
struct ScanReport {
    std::vector<std::string> files;   // 實際讀到的檔（repo 相對路徑，字典序）
    std::size_t bytes = 0;            // 實際讀到的總位元組
    std::vector<Finding> findings;
    std::vector<std::string> errors;  // 讀不到的檔與原因
};

// 掃一組 repo 相對路徑（相對於 `repo_root`）。
ScanReport scan_files(const std::string& repo_root, const std::vector<std::string>& rel_paths);

// 本斷言的掃描目標：E4 = `modules/elements/*.hpp`、kernel = `src/kernel/*.hpp`
// （**只掃該目錄本身的標頭**，不遞迴進 `src/kernel/backend/`：後端就是知道平台細節的地方，
// 它不是 kernel 的公開介面）。回傳 repo 相對路徑、字典序；目錄列舉失敗回空清單
// （呼叫端由 §反真空 的檔數下界擋下）。
std::vector<std::string> interface_headers(const std::string& repo_root);

// 預期基線的一筆（見檔頭 §預期基線）。
struct BaselineEntry {
    std::string file;
    std::string identifier;
    Rule rule = Rule::IntegralGeometryField;
    std::string justification;  // 為什麼它不算滲透（人類可讀，寫進斷言失敗訊息裡）
};

// 具名、附理由的預期基線。**不是豁免清單**：斷言的形式是「命中集合 == 本清單」。
const std::vector<BaselineEntry>& expected_baseline();

// 把一組命中縮成 (file, identifier, rule) 的字典序清單，供與基線逐項比對。
std::vector<std::string> finding_keys(const std::vector<Finding>& findings);
std::vector<std::string> baseline_keys();

}  // namespace ds::host::pixel_audit

#endif  // DS_HOST_COMPOSE_PIXEL_BOUNDARY_HPP
