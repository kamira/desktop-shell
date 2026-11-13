// H1-09 就地編輯裝配 —— 把「編輯 → 寫回 → 存檔 → 還原 → 熱重載 → 重畫」接成一條路
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含平台條件編譯**，`host/edit/` 的
// CMakeLists 也**整份不包 `if(WIN32)`**（照 `host/compose/`（H1-07）與 `host/paint/` 前半
// （H1-06 / H1-08）的先例）。H1-09 的 `units.json` 標 `platform: neutral`，而本單元的內容
// ——編輯設定值、序列化、版本分流、幾何消毒——沒有一行需要 Windows。放在守衛外，ubuntu 的
// **G3**（`-DDS_WERROR=ON`）、**G3b**（註冊數不得下降）、**G3c**（neutral 單元須在 ubuntu
// 的 `ctest -N` 裡看得到）與 **`gate_sanitizers`**（ASan + UBSan + float-cast-overflow）
// 才真的建置並執行它的測試。
//
// ---------------------------------------------------------------------------
// 1. 這一層把哪些上游接起來（全部可讀不可改）
// ---------------------------------------------------------------------------
//   E7-12 設定值寫回        `set_value` / `serialize`  —— 路徑定位、更新、序列化
//   E7-14 圖形化設定與就地編輯 `apply_edit`             —— 編輯前的型別 / 約束驗證
//   E7-15 拖放產生設定項    `generate_config_item`      —— 拖放內容 → 設定項
//   E9-05 佈局存檔與還原    `LayoutState` / `LayoutStore` —— **存檔格式的唯一事實來源**
//   E7-08 設定遷移          `MigrationRegistry`         —— 舊版存檔的升級鏈
//   E7-07 熱重載            `ReloadSource` / `HotReloader` —— 來源變更偵測 + 失敗保舊值
//   H1-06 繪製層分派        `ElementDraw` / `PaintBox` / `ElementRendererTable`
//                                                        —— 換 kind → 換畫法
//
// **本層不自造格式。** 存檔一律由 E9-05 `serialize_layout` 產生，結構是
// `{ format_version, elements: [ { id, type, properties } ] }`，`format_version` 由
// E9-05 的 `kLayoutFormat` 帶上（目前 1.0，與 E7-01 `kSupportedFormat` 同）。
//
// ---------------------------------------------------------------------------
// 2. 讀與寫刻意不對稱 —— 這是驗收標準最硬那一條的落點
// ---------------------------------------------------------------------------
// 驗收標準：「**使用者可直接改動持久化狀態，故錯誤不得讓佈局進入救不回來的狀態。**」
//
// - **寫**：走 E9-05（格式單一事實來源）。
// - **讀**：**不**走 E9-05 `deserialize_layout`。它對局部損壞（某個元件缺 `id`）是
//   `throw`，等於**一個壞元件炸掉整份佈局** —— 使用者手滑刪掉一行，整個桌面就回不來了。
//   本層改用**容忍式讀取**：同一個結構、逐元件判定，壞元件具名丟棄並計數，其餘照常還原。
//
// 兩條路徑不得各自漂移，故以測試釘住：**合法輸入下本層讀出的 `LayoutState` 必須與
// `ds::layout::deserialize_layout` 的結果逐欄相等**（`tests/h1/test_h1_09.cpp`
// `FormatParity` 一組）。任何一方改了結構而另一方沒跟上，那組測試會紅。
//
// 「救不回來」的三道防線：
//   1. 載入失敗 → **保留現有記憶體佈局**（與 E7-07「失敗保舊值」同一條原則）。
//   2. 載入失敗 → **不把壞狀態寫回**；storage 裡的原文原封不動，使用者還能手改。
//   3. `save()` 先序列化、再**自我往返驗證**（序列化 → 以本層讀回 → 逐欄比對），
//      驗證過才 `put()` —— 提交點只有一個，要嘛整份已驗證的文字落地、要嘛什麼都沒變。
//
// 公開 API **一律不 throw**：host 層把例外丟進 UI 迴圈等於整個桌面掛掉。所有失敗一律是
// 具名 enum（`IntakeStatus` / `EditStatus` / `SaveStatus`）＋ `EditStats` 計數器 ＋
// `last_*()` 查詢（照 H1-08 `RingStatus` / `RingGaugeStats` 的做法）。
//
// ---------------------------------------------------------------------------
// 3. `format_version` 的四種分流（驗收標準點名的三種異常都在內）
// ---------------------------------------------------------------------------
// `ds::format::parse()` 自己會擋版本，但不相容一律回一個行號 0 的泛用訊息，分不出
// 「太新 / 太舊 / 缺失」。本層在 parse 之前先**掃一次版本行**（`peek_format_version`，
// 純字串掃描、不解析內容）再分流：
//
//   缺 `format_version`   → `MissingVersion`  拒收。**不猜**版本（猜錯 = 默默改語意）
//   版本無法解析          → `MalformedVersion` 拒收
//   版本比程式新          → `VersionTooNew`   拒收、保留現有佈局、不寫回（實作不降級）
//   版本比程式舊          → 走 **E7-08** 遷移鏈 → `Migrated`
//                           查無路徑 → `MigrationUnavailable`（**不**「結構看起來一樣就收下」）
//
// **舊版文件的取用細節（已知取捨，另載於 CHG）**：E7-01 `parse()` 在版本不相容時直接拒收，
// 所以舊版文件連「讀成 Value 樹」都做不到。本層的作法是：**僅**把版本行的值換成本實作支援
// 的版本以通過 E7-01 的版本閘（`restamp_format_version`，只動那一行、其餘位元組不動），
// 取得結構後，**把原始宣告版本**交給 E7-08 `MigrationRegistry` 驅動遷移鏈。語意轉換全部
// 由註冊的遷移步驟負責，本層一行都不猜。
//
// ---------------------------------------------------------------------------
// 4. DPI 語意：存檔裡的幾何是**邏輯像素**
// ---------------------------------------------------------------------------
// `properties` 的 `x` / `y` / `width` / `height` 一律是**邏輯像素**（與 H1-07
// `ComponentPlacement` 同慣例：原點左上、x 向右、y 向下、widget 內相對）。裝置像素只在
// `resolve_placement(placement, dpi_scale)` 這一步產生，交出 H1-06 的 `PaintBox`
// （半開區間 [left,right) x [top,bottom)）——**不另造第二個像素矩形型別**。
//
// 日後要改這條語意（例如改存裝置像素）**必須走 E7-08 遷移並升 `format_version`**，
// 不得默默改：同一份存檔在新舊實作下會長成不同位置，而那是看得見、改不回來的壞。
//
// ---------------------------------------------------------------------------
// 5. 使用者手改的存檔裡真的會有 NaN —— K-016 / K-002 / float-cast-overflow
// ---------------------------------------------------------------------------
// `x: nan`、`width: -40`、`y: 1e400`、`height: ""` 都是現實輸入，不是假想。本層：
//   - 任何「這個值可不可用」的判定一律**明寫 `std::isfinite(v)`**，不靠某個比較剛好擋住。
//     K-016：`v != 0.0` 對 NaN 回 **true**，「正向比較即 fail-closed」的口訣碰到 `!=`
//     就反過來；最毒的是同一個 `return` 裡混用兩種比較，讀起來像每項都檢查過了。
//   - 夾值一律 `isfinite` 先行，NaN **不得原封通過**（K-002：`v<lo?lo:(v>hi?hi:v)` 對 NaN
//     兩個比較都是 false，於是原封回傳）。
//   - `double → int` 之前先消毒並夾到遠小於 `int` 值域的範圍再 `lround`
//     （`-fsanitize=float-cast-overflow` 會讓一次溢位轉換**當場 FAILED**，不是印訊息）。
#ifndef DS_HOST_EDIT_EDIT_ASSEMBLY_HPP
#define DS_HOST_EDIT_EDIT_ASSEMBLY_HPP

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "document.hpp"       // E7-01：Value / Document / FormatVersion
#include "dropgen.hpp"        // E7-15：DropContent / generate_config_item
#include "editor_model.hpp"   // E7-14：apply_edit / validate_edit
#include "hot_reload.hpp"     // E7-07：ReloadSource / HotReloader
#include "layout_store.hpp"   // E9-05：LayoutState / LayoutStorage / LayoutStore
#include "migration.hpp"      // E7-08：MigrationRegistry / version_less
#include "render_dispatch.hpp"  // H1-06：ElementDraw / PaintBox / ElementRendererTable
#include "widget_compose.hpp"   // H1-07：ComponentPlacement / PixelPoint / PixelSize（宣告相依）
#include "writeback.hpp"      // E7-12：Path / set_value / serialize

// 自成一個巢狀命名空間：`host/paint/`（H1-06 / H1-08）與 `host/compose/`（H1-07）已經在
// `ds::host` 放了 `sanitize_dpi_scale` 等名字，本層若也往 `ds::host` 丟同名自由函式，
// 將來有人把兩個 target 連進同一個程式就是一個不必要的重定義。
namespace ds::host::edit {

// ---------------------------------------------------------------------------
// 屬性鍵（具名字串，與 kind 一樣不走 enum —— 新增屬性不必改本層）
// ---------------------------------------------------------------------------
inline constexpr const char* kPropX = "x";
inline constexpr const char* kPropY = "y";
inline constexpr const char* kPropWidth = "width";
inline constexpr const char* kPropHeight = "height";
inline constexpr const char* kPropFillRatio = "fill_ratio";
inline constexpr const char* kPropLabel = "label";
inline constexpr const char* kPropAvailable = "available";
inline constexpr const char* kPropVertical = "vertical";
inline constexpr const char* kPropStartAngle = "start_angle";
inline constexpr const char* kPropSweep = "sweep";
inline constexpr const char* kPropAngle = "angle";
inline constexpr const char* kPropLengthRatio = "length_ratio";
inline constexpr const char* kPropThicknessRatio = "thickness_ratio";

// ---------------------------------------------------------------------------
// 夾限上界（邏輯像素）。刻意遠小於 int 值域：乘上 DPI 縮放後仍不可能讓 double→int 溢位。
// ---------------------------------------------------------------------------
inline constexpr double kCoordLimit = 1.0e6;    // |x| / |y| 的上界
inline constexpr double kExtentLimit = 1.0e6;   // width / height 的上界（下界為 0）
inline constexpr double kMaxDpiScale = 16.0;    // 縮放上界（16x 已遠超任何現實 DPI）

// ---------------------------------------------------------------------------
// LogicalPlacement —— 一個元件在 widget / 桌面上的位置與大小（**邏輯像素**）
// ---------------------------------------------------------------------------
struct LogicalPlacement {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    // 四個欄位**全部**為有限數。明寫 `std::isfinite`，不靠任何比較順便擋住（K-016）。
    bool finite() const noexcept {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
               std::isfinite(height);
    }

    // 在夾限範圍內（呼叫端須先確認 `finite()`：非有限數在此一律回 false，
    // 因為所有比較對 NaN 都是 false，而 `!(a<b)` 這種反寫會讓 NaN 溜過去）。
    bool within_limits() const noexcept {
        return finite() && x >= -kCoordLimit && x <= kCoordLimit && y >= -kCoordLimit &&
               y <= kCoordLimit && width >= 0.0 && width <= kExtentLimit && height >= 0.0 &&
               height <= kExtentLimit;
    }

    // 真的有東西可畫（有限、在範圍內、且寬高皆為正）。
    bool usable() const noexcept { return within_limits() && width > 0.0 && height > 0.0; }
};

bool operator==(const LogicalPlacement& a, const LogicalPlacement& b) noexcept;
bool operator!=(const LogicalPlacement& a, const LogicalPlacement& b) noexcept;

// ---------------------------------------------------------------------------
// 消毒（NaN 一律不得原封通過 —— K-002）
// ---------------------------------------------------------------------------

// 把 v 夾進 [lo, hi]。**先判有限**再夾：NaN / ±Inf → fallback（fallback 本身非有限則取 lo）。
double clamp_finite(double v, double fallback, double lo, double hi) noexcept;

// 座標消毒：非有限 → 0，否則夾進 [-kCoordLimit, kCoordLimit]。
double sanitize_coord(double v) noexcept;

// 尺寸消毒：非有限 / 負 → 0，否則夾進 [0, kExtentLimit]。
// 負尺寸不翻正（翻正是猜使用者的意圖）；歸 0 → 之後是具名的 `EmptyArea`，看得見。
double sanitize_extent(double v) noexcept;

// DPI 縮放消毒：非有限 / ≤0 → 1.0；過大 → 夾到 kMaxDpiScale。語意與 H1-08 同。
double sanitize_dpi(double scale) noexcept;

// 四個欄位逐一消毒；`repaired` 非 null 時累加**實際被改動**的欄位數（不是「檢查過幾個」）。
LogicalPlacement sanitize_placement(const LogicalPlacement& p, int* repaired = nullptr) noexcept;

// double → 整數像素。**最後一道防線，且是公開的**：先判有限、再夾到遠小於 `int` 值域的
// 範圍、最後才 `lround`。非有限 → 0。
//
// 為什麼公開：它原本是 .cpp 匿名命名空間裡的「防禦性」檢查，而今日所有呼叫端都先過
// `clamp_finite`，於是**拿掉它不會有任何測試紅**（V1.7 的 MX5 存活）。一條沒有斷言保護的
// 防線，等於下一個人可以安靜地刪掉它——而它正是 `-fsanitize=float-cast-overflow` 的最後
// 一道牆。公開 + 直接測它，這條防線才真的被釘住。
int round_to_px(double v) noexcept;

// 以半開區間慣例組出一個 `PaintBox`：右 / 下不得小於左 / 上（反過來就夾平成退化矩形）。
// 同樣是公開的，理由同 `round_to_px`（V1.7 的 MX6：夾限本身已保證順序，故那兩行在
// `resolve_placement` 內部不可達、刪掉沒有測試會紅）。
ds::host::PaintBox normalize_box(int left, int top, int right, int bottom) noexcept;

// 邏輯像素 + 縮放 → 裝置像素矩形（H1-06 `PaintBox`，半開區間）。
// **純函式、不會崩潰、不會產生溢位的 double→int 轉換**：先消毒、再夾限、最後才 lround。
ds::host::PaintBox resolve_placement(const LogicalPlacement& placement, double dpi_scale) noexcept;

// ---------------------------------------------------------------------------
// 與 H1-07 組件組合裝配的橋接（`units.json` 宣告的相依）
// ---------------------------------------------------------------------------
// H1-07 的 `ComponentPlacement` 與本層的 `LogicalPlacement` **語意相同**（邏輯像素、
// 原點左上、widget 內相對），只差表示型別：H1-07 是 `int`（版面一經定案就是整數格位），
// 本層是 `double`（拖曳要累加、存檔要保留使用者拖到的位置，整數會每拖一次就掉一次精度）。
//
// 兩個同語意不同型別的幾何放著不管，遲早有人各自寫一份轉換、然後在某一側漏掉消毒
// ——所以轉換只有這一處，而且有測試釘住（含 NaN / 超限）。
ds::host::ComponentPlacement to_component_placement(
    const LogicalPlacement& placement,
    ds::host::ComponentPresentation presentation = ds::host::ComponentPresentation{});
LogicalPlacement from_component_placement(const ds::host::ComponentPlacement& placement) noexcept;

// ---------------------------------------------------------------------------
// 非有限數值的**深層**偵測（F2：巢狀 Map / List 裡的 NaN 一樣不得進入持久化狀態）
// ---------------------------------------------------------------------------
// 回傳 `root` 內所有非有限數值的路徑（E7-12 路徑語法，如 `cfg.inner`、`arr[0]`）；
// 根自己就是非有限純量時回傳一個空字串元素。空 vector = 乾淨。
std::vector<std::string> non_finite_paths(const ds::format::Value& root);

// 便捷述詞（等價於 `!non_finite_paths(root).empty()`，但不配置路徑字串）。
bool contains_non_finite(const ds::format::Value& root) noexcept;

// ---------------------------------------------------------------------------
// format_version 預掃（純字串掃描，不解析內容）
// ---------------------------------------------------------------------------
enum class VersionPeek {
    Found,      // 找到且可解析為 major.minor
    Missing,    // 根層沒有 format_version 這個鍵
    Malformed,  // 有這個鍵，但值不是 major.minor（含空值、位數荒謬到放不進 int）
};

const char* to_string(VersionPeek peek) noexcept;

// 掃出根層（零縮排）第一個 `format_version` 的值。規則刻意與 E7-01 對齊：
// `#` 開頭與全空白的行略過；值取冒號之後 trim 的整段（E7-01 **不**剝行尾註解）。
// 不使用 `std::stoi`：E7-01 的 `parse_version` 用了，位數爆掉時會丟 `std::out_of_range`，
// 而本層對外承諾不 throw。位數超出 int 一律視為 `Malformed`（且因此不會把那種文字餵給
// `parse()`，順帶擋掉上游那條例外路徑）。
VersionPeek peek_format_version(const std::string& text, ds::format::FormatVersion& out);

// 只把根層 format_version 那一行的值換成 `version`，其餘位元組原封不動。
// 用途見檔頭 §3：讓舊版文件能通過 E7-01 的版本閘以取得**結構**，語意轉換仍交給 E7-08。
// 找不到該行則原樣回傳。
std::string restamp_format_version(const std::string& text,
                                   const ds::format::FormatVersion& version);

// ---------------------------------------------------------------------------
// 載入（容忍式；對外不 throw）
// ---------------------------------------------------------------------------
enum class IntakeStatus {
    Ok,                    // 完全正常
    Repaired,              // 讀進來了，但有欄位被消毒 / 有元件被丟棄（見計數器）
    Migrated,              // 舊版存檔，已由 E7-08 遷移鏈升級（修補數另見計數器）
    NotFound,              // storage 裡沒有這個名字
    MissingVersion,        // 缺 format_version
    MalformedVersion,      // format_version 不是 major.minor
    VersionTooNew,         // 版本比本實作新 —— 不降級
    MigrationUnavailable,  // 版本比本實作舊，但沒有可用的遷移鏈
    MigrationFailed,       // 有遷移鏈但套用失敗
    Malformed,             // E7-01 解析失敗（訊息帶行號）
    StructureInvalid,      // 解析得出來，但根 / elements 的形狀整個不對
};

const char* to_string(IntakeStatus status) noexcept;

struct IntakeReport {
    IntakeStatus status = IntakeStatus::Malformed;
    ds::layout::LayoutState state{};          // 僅 usable() 為 true 時有意義
    std::string message;                      // 人類可讀原因（解析失敗帶行號）
    ds::format::FormatVersion source_version{};  // 文件自己宣告的版本
    bool has_version = false;

    // --- 「沒能帶走的內容」：三個互斥的計數器 ---
    // 這三個是**覆寫決定的判據**（見 `carries_everything()`）。分開數而不是合成一個 bool，
    // 是因為它們的成因不同，訊息要說得出是哪一種。
    int dropped_elements = 0;      // 整個元件帶不進來（缺 id / 缺 type / 型別錯 / 重複 id）
    int discarded_properties = 0;  // 整個 properties 區塊型別不對而被丟掉
    int unconsumed_keys = 0;       // 檔案裡有、但本層的資料模型根本帶不走的鍵

    // --- 「修好了的東西」：不算遺失 ---
    // 幾何值被消毒（NaN → 0、負尺寸 → 0、超限 → 夾限）。存回去是**自我修復**，是對的。
    int repaired_fields = 0;

    // 這份報告帶得出一個可用的佈局。
    bool usable() const noexcept {
        return status == IntakeStatus::Ok || status == IntakeStatus::Repaired ||
               status == IntakeStatus::Migrated;
    }

    // 本層**沒能帶走**的內容量。覆寫前要問的就是這個數字。
    int lost_content() const noexcept {
        return dropped_elements + discarded_properties + unconsumed_keys;
    }

    // 讀得出來，**而且**檔案裡的每一塊內容都真的進到了記憶體模型裡。
    //
    // ---------------------------------------------------------------------------
    // 這個述詞就是覆寫決定的判據（修訂 3 / 驗收 F4）
    // ---------------------------------------------------------------------------
    // 修訂 2 的 `save()` 用的是 `usable()`，而 `usable()` 把 `Repaired` 一律當成「可以」。
    // 但 `Repaired` 同時涵蓋兩種語意完全相反的情況：
    //   - 幾何欄位被消毒（`repaired_fields`）→ 存回去是**自我修復**，是對的；
    //   - 整個元件被丟棄（`dropped_elements`）→ 存回去是**把使用者的資料扔掉**。
    // 使用者把一個元件的 `type:` 打成 `typo:`，那個元件連同它的整組設定就此消失，
    // 而 `save()` 回報 `Ok`。那是同一條驗收標準被第二個入口攻破。
    //
    // 所以判據不是「讀不讀得出來」，而是：
    //   **本層在做覆寫決定的那一刻，知不知道自己沒能完整帶走使用者的內容？**
    // 知道（`lost_content() > 0`）就不能靜默覆寫。
    bool carries_everything() const noexcept { return usable() && lost_content() == 0; }

    // 讀進來的東西與檔案裡寫的不完全一樣（降級是**具名且可觀測**的，不是靜默）。
    bool degraded() const noexcept { return lost_content() > 0 || repaired_fields > 0; }
};

// 從宣告式文字讀出佈局。`migrations` 為 null 時，舊版文件一律 `MigrationUnavailable`。
// **不 throw**：任何失敗都走具名 `IntakeStatus`。
IntakeReport ingest_layout_text(const std::string& text,
                                const ds::format::MigrationRegistry* migrations = nullptr);

// ---------------------------------------------------------------------------
// 編輯 / 存檔的具名結果
// ---------------------------------------------------------------------------
enum class EditStatus {
    Ok,
    EmptyId,            // 元件 id 為空
    EmptyKind,          // 元件 kind（= E9-05 的 type）為空
    DuplicateId,        // 已經有同 id 的元件
    UnknownElement,     // 找不到這個 id
    NonFiniteValue,     // 候選值是 NaN / ±Inf —— 不寫進持久化狀態
    OutOfRange,         // 候選值超出夾限
    BadPath,            // E7-12 路徑語法錯 / 無法套用
    RejectedByModel,    // E7-14 的型別 / 約束驗證不過
    UnknownDropContent, // E7-15 認不出這段拖放內容
};

const char* to_string(EditStatus status) noexcept;

enum class SaveStatus {
    Ok,
    EmptyName,                // 名稱為空（E9-05 的契約）
    SerializeFailed,          // E9-05 序列化拒絕目前狀態（如 id / type 為空）
    VerifyFailed,             // 往返自驗不過 —— **不提交**，storage 原封不動
    RefusedUnreadableTarget,  // 目標存檔存在、但本層**讀不出來** → 拒絕覆寫（見下）
    RefusedIncompleteTarget,  // 目標讀得出來，但本層**沒能帶走它的全部內容** → 拒絕覆寫
    RefusedBackupSlotExhausted,  // 明示丟棄，但備份名稱全被占用 → 拒絕（不蓋掉任何東西）
};

const char* to_string(SaveStatus status) noexcept;

// 覆寫既有存檔的策略。
//
// ---------------------------------------------------------------------------
// 為什麼需要這個 enum（本單元第一版的阻擋缺陷，V1.7 的 F1）
// ---------------------------------------------------------------------------
// 第一版的 `save()` 完全不管目標存檔是什麼，一律 `put()` 覆寫。於是這條路會毀資料：
//
//     開機 → restore("desktop") 被拒收（檔案裡多了一個 tab）→ 記憶體佈局是空的
//          → 離開時自動 save("desktop") → **使用者的佈局永久消失**，而 save 回報 Ok
//
// 第一版的辯詞是「載入失敗不寫回，原文原封不動，使用者還改得回來」——前半句對（就那一次
// `restore` 而言），後半句錯：「還改得回來」要求檔案活到使用者去改它，而下一個正常操作
// 就把它刪了。`VersionTooNew` 尤其諷刺：拒收它正是為了保護它（不降級），結果 save 把它刪了。
//
// 相位 1 的後端是 `MemoryLayoutStorage`：沒有版本、沒有備份、沒有 `.bak`，被覆寫就是沒了。
// CHG 第一版用「相位 1 沒有磁碟可以被寫到一半」論證原子性——那對**撕裂寫入**成立，但相位 1
// 真正的資料毀損路徑不是撕裂寫入，是**一次完整、合法、通過往返自驗的寫入，蓋掉本層刻意
// 拒絕讀取的資料**。那一條本層擋得住（`ingest_layout_text` 就在手上），所以本層就該擋。
enum class Overwrite {
    // 預設：目標存在、但本層讀不出來或帶不走它的全部內容 → 具名拒絕，**不提交**。
    Refuse,
    // 明示丟棄：使用者真的要扔掉目標現有的內容。即使如此，**原文仍會先被另存一份**
    // （`<name>` + `kRejectedSuffix`，必要時加序號），所以連「明示丟棄」都不真的丟掉位元組。
    //
    // 修訂 2 原名 `DiscardUnreadable`；修訂 3 之後它同時涵蓋「讀不出來」與「讀不全」
    // 兩種拒絕，舊名字會讓呼叫端以為後者不在保護範圍內，故更名。
    DiscardTargetContent,
};

const char* to_string(Overwrite policy) noexcept;

// 明示丟棄時，原文被另存到的名稱後綴。第一個備份用 `<name>.rejected`，
// 之後依序為 `<name>.rejected.2`、`.3`…… —— **備份永遠不覆寫任何既有的名字**
// （驗收 R8：修訂 2 的備份直接 `put()`，會蓋掉一個剛好同名的、可讀的 profile；
//  那是修訂 2 裡唯一一處「沒先檢查就寫」的地方，與本單元其餘部分的原則不一致）。
inline constexpr const char* kRejectedSuffix = ".rejected";
// 備份槽上限。全部用完 → `RefusedBackupSlotExhausted`，寧可拒絕也不蓋掉任何東西。
inline constexpr int kMaxRejectedBackups = 32;

enum class ReloadOutcome {
    NoSource,   // 沒有接上任何 ReloadSource
    Unchanged,  // 來源未變更（E7-07 的 revision 比對）
    Reloaded,   // 重新讀入成功（造型可能因此改變）
    Rejected,   // 新內容不可用 —— **保留舊佈局**，細節見 last_intake()
};

const char* to_string(ReloadOutcome outcome) noexcept;

// 計數器：讓「有沒有真的發生、發生幾次」可被斷言，而不是只看最後狀態（照 H1-08）。
struct EditStats {
    int edits = 0;
    int edits_rejected = 0;
    int saves = 0;
    int saves_rejected = 0;
    // `save()` 因為「目標讀不出來」而拒絕覆寫的次數（F1 的防線是否真的擋到，可被斷言）。
    int saves_refused_target = 0;
    // `save()` 因為「目標讀得出來、但本層帶不走它的全部內容」而拒絕覆寫的次數（F4）。
    int saves_refused_incomplete = 0;
    // 明示丟棄時，把讀不出來的原文另存為 `<name>.rejected` 的次數。
    int rejected_backups = 0;
    int loads = 0;
    int loads_failed = 0;
    int reloads = 0;
    int reloads_rejected = 0;
    int elements_dropped = 0;
    int fields_repaired = 0;
};

// ---------------------------------------------------------------------------
// 繪製橋接（H1-06）
// ---------------------------------------------------------------------------
// 一個元件解析後的繪製輸入：具名 kind + 裝置像素框。餵進 `ElementRendererTable::draw()` 即可。
struct PlacedDraw {
    std::string id;
    ds::host::ElementDraw draw;
    ds::host::PaintBox box;
};

// 一輪繪製的具名統計（分派結果三種各自計數，不靜默）。
struct PaintReport {
    int drawn = 0;
    int unknown_kind = 0;
    int empty_area = 0;

    int total() const noexcept { return drawn + unknown_kind + empty_area; }
};

// 依序分派一批 PlacedDraw。`table` 查不到 kind → 計入 `unknown_kind`（元件不會憑空消失，
// 它是「畫不出來」且說得出來）。
PaintReport paint_layout(const std::vector<PlacedDraw>& placed,
                         const ds::host::ElementRendererTable& table, ds::host::PaintSink& sink);

// ---------------------------------------------------------------------------
// EditAssembly —— 就地編輯裝配本體
// ---------------------------------------------------------------------------
//
// **生命週期（K-013）**：`storage` 與（若有）`ReloadSource` / `MigrationRegistry` 皆以
// **參考 / 指標**持有，必須活得比本物件久。本物件不接管任何一者的生命週期，
// 也不以 `const&` 去綁按值回傳的臨時物件。
//
// 「重啟」在測試裡就是：丟掉這個物件，用**同一個 storage** 再建一個，然後 `restore(name)`。
class EditAssembly {
public:
    explicit EditAssembly(ds::layout::LayoutStorage& storage);

    // --- 裝配 ---
    EditStatus add_element(const std::string& id, const std::string& kind,
                           const LogicalPlacement& placement);
    EditStatus remove_element(const std::string& id);
    bool contains(const std::string& id) const;
    std::vector<std::string> ids() const;
    std::size_t size() const noexcept { return state_.elements.size(); }

    // --- 就地編輯 ---
    // 拖動：位移量為**邏輯像素**。dx / dy 非有限 → `NonFiniteValue`，**不寫入**。
    EditStatus drag_element(const std::string& id, double dx, double dy);
    // 直接指定位置與大小（非有限 / 超限 → 具名拒絕，不消毒後硬寫）。
    EditStatus set_placement(const std::string& id, const LogicalPlacement& placement);
    // 取出位置（已消毒）。找不到 id → false，`out` 不動。
    bool placement_of(const std::string& id, LogicalPlacement& out) const;

    // 換 kind（E9-05 的 `type`）→ 下一次 `resolve_draws()` 就會分派到另一個 renderer。
    EditStatus set_kind(const std::string& id, const std::string& kind);
    std::string kind_of(const std::string& id) const;  // 找不到 → 空字串

    // 就地編輯任一設定欄位：路徑已存在 → 走 **E7-14** `apply_edit`（型別 / 約束驗證）；
    // 路徑不存在 → 走 **E7-12** `set_value` 新增。
    //
    // 非有限數值一律先擋下，且檢查是**深層的**：候選值是 Map / List 時會遞迴檢查裡面每一個
    // 數值。修訂 1 只查 `value.is_number()`，於是 `{inner: NaN}` 這種巢狀值直接通過公開 API
    // 進了持久化狀態（驗收 F2）。
    //
    // 修訂 3 起這道守衛放在**共用的 `write_property()`**，而不是掛在本函式上
    // （驗收 R10）：`apply_drop()` 也走 `write_property`，掛在這裡等於漏掉它。
    // 今日 E7-15 只產生字串值所以不可達，但 F2 的教訓本來就是「守衛放錯位置」，
    // 修它的時候不該又放錯一次。
    EditStatus edit_property(const std::string& id, const std::string& path,
                             const ds::format::Value& value);
    // 取出某個屬性（找不到 → false）。
    bool property_of(const std::string& id, const std::string& path,
                     ds::format::Value& out) const;

    // 移除某個屬性（map 鍵；路徑不存在 / 非 map 鍵 → 具名失敗）。
    // 存在的理由：`reset()` 塞進來的壞狀態要有辦法清掉，而不是只能整個元件丟掉。
    EditStatus remove_property(const std::string& id, const std::string& path);

    // 拖放產生設定項（**E7-15**）：`generate_config_item` 決定寫到哪個路徑、寫什麼。
    EditStatus apply_drop(const std::string& id, const ds::format::DropContent& drop);

    // --- 存檔 / 還原（E9-05；帶 format_version）---
    //
    // `save()` 在提交之前會做兩件事：
    //   1. 序列化目前狀態並**往返自驗**（序列化 → 以本層讀回 → 逐欄比對）；
    //   2. **把目標存檔讀一次**，問 `IntakeReport::carries_everything()`——
    //      讀不出來 → `RefusedUnreadableTarget`；讀得出來但帶不走全部內容 →
    //      `RefusedIncompleteTarget`。兩者都可用 `Overwrite::DiscardTargetContent` 明示跨過，
    //      而跨過時原文一定先被備份。
    // 兩者皆通過才 `storage_.put()`。提交點仍然只有一個。
    //
    // **只保護同名目標**（驗收 R7）：本層看的是 `name` 這一份存檔。
    // 「還原 A 失敗、接著存到 B」不在保護範圍內——本層分不出那是誤存，
    // 還是使用者刻意從空白建一份新的 B。那需要呼叫端契約，不是本層單方面能決定的。
    SaveStatus save(const std::string& name, Overwrite policy = Overwrite::Refuse);
    IntakeStatus restore(const std::string& name);

    // 存檔但不提交，只看會不會過 —— 給呼叫端做「存得起來嗎」的預檢。
    // `name` 為空 → 只檢查記憶體狀態；非空 → 連目標是否可安全覆寫也一起檢查。
    // **與 `save()` 用同一個述詞**，不得給出比 `save()` 寬鬆的答案。
    SaveStatus dry_run_save(const std::string& name = std::string()) const;

    // 覆寫 `name` 會不會丟掉使用者的內容。
    //   - storage 裡沒有這個名字 → true（沒有東西會被毀掉）。
    //   - 讀得出來**且**本層帶得走它的全部內容 → true。
    //   - 讀不出來，或讀得出來但有元件 / properties / 鍵帶不走 → **false**。
    // `report` 非 null 時寫入完整的 `IntakeReport`（狀態 + 三個遺失計數器），
    // 讓呼叫端能對使用者說清楚「會失去什麼」。
    //
    // 修訂 2 原名 `target_is_readable()`，述詞是 `usable()`——它在「元件被丟棄」的那一刻
    // 回 `true`，呼叫端照契約問了、得到綠燈、然後資料沒了（驗收 F4）。
    // 名字與述詞一起改掉：要問的從來不是「讀不讀得出來」，而是「覆寫安不安全」。
    bool target_is_safe_to_overwrite(const std::string& name,
                                     IntakeReport* report = nullptr) const;

    // --- 遷移鏈（E7-08）---
    // 註冊給載入路徑用的遷移登錄。以**指標**持有，呼叫端負責存活期；null = 解除。
    void set_migrations(const ds::format::MigrationRegistry* migrations) noexcept {
        migrations_ = migrations;
    }

    // --- 熱重載（E7-07）---
    // 接上來源；來源必須活得比本物件久（K-013）。重複呼叫會換掉前一個。
    void attach_source(ds::format::ReloadSource& source);
    void detach_source() noexcept;
    bool has_source() const noexcept { return source_ != nullptr; }
    // 來源變更才重新讀入。失敗一律**保留舊佈局**（E7-07 同一條原則）並回 `Rejected`。
    ReloadOutcome poll_reload();
    // 最近一次重載失敗時，E7-07 / E7-06 產生的定位到行的報告（無失敗 → 空字串）。
    const std::string& last_reload_report() const noexcept { return last_reload_report_; }

    // --- 繪製（H1-06）---
    std::vector<PlacedDraw> resolve_draws(double dpi_scale) const;

    // --- 可觀測性 ---
    const ds::layout::LayoutState& state() const noexcept { return state_; }
    // 直接換掉整份佈局（供呼叫端從別處取得狀態；**不**消毒，故另有 save() 的往返自驗把關）。
    void reset(const ds::layout::LayoutState& state);
    const EditStats& stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = EditStats{}; }
    EditStatus last_edit_status() const noexcept { return last_edit_; }
    IntakeStatus last_intake_status() const noexcept { return last_intake_.status; }
    const IntakeReport& last_intake() const noexcept { return last_intake_; }
    SaveStatus last_save_status() const noexcept { return last_save_; }
    // 最近一次失敗的人類可讀原因（成功時為空）。
    const std::string& last_message() const noexcept { return last_message_; }

private:
    ds::layout::LayoutElement* find(const std::string& id);
    const ds::layout::LayoutElement* find(const std::string& id) const;
    // 共用的寫入路徑：先擋非有限數值（深層），再依路徑是否存在走 E7-14 apply_edit
    // 或 E7-12 set_value。**所有**寫入屬性的公開 API 都經過這裡。
    EditStatus write_property(ds::layout::LayoutElement& element, const std::string& path,
                              const ds::format::Value& value, std::string& message);
    EditStatus note_edit(EditStatus status, std::string message);
    // 採用一份載入報告：可用則換掉佈局，否則**保留現有佈局**。計數器由呼叫端各自加。
    void adopt(IntakeReport report);
    // 序列化目前狀態並往返自驗；通過才把文字寫進 `text`。
    SaveStatus serialize_verified(std::string& text, std::string& message) const;
    // 「為什麼不讓你覆寫」的人類可讀說明（說得出是哪一種、少了什麼、怎麼跨過去）。
    static std::string describe_unsafe_target(const std::string& name,
                                              const IntakeReport& target);
    // 找一個沒被占用的備份名稱（`<name>.rejected`、`.rejected.2`…）。
    // 全部占用 → 回 false（呼叫端轉為 `RefusedBackupSlotExhausted`）。
    bool free_backup_name(const std::string& name, std::string& out) const;
    // 往返前後第一個對不上的地方（人類可讀；`VerifyFailed` 的訊息用）。
    static std::string describe_mismatch(const ds::layout::LayoutState& before,
                                         const ds::layout::LayoutState& after);

    // 寫入一律經 E9-05 `serialize_layout`（`LayoutStore::save` 用的就是它），
    // 格式不在本層另開一份；提交點是 `storage_.put()`，見 `save()` 的往返自驗。
    ds::layout::LayoutStorage& storage_;
    ds::layout::LayoutState state_{};
    const ds::format::MigrationRegistry* migrations_ = nullptr;
    ds::format::ReloadSource* source_ = nullptr;
    std::unique_ptr<ds::format::HotReloader> reloader_;
    std::string last_reload_report_;
    EditStats stats_{};
    EditStatus last_edit_ = EditStatus::Ok;
    SaveStatus last_save_ = SaveStatus::Ok;
    IntakeReport last_intake_{};
    std::string last_message_;
};

}  // namespace ds::host::edit

#endif  // DS_HOST_EDIT_EDIT_ASSEMBLY_HPP
