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

// 邏輯像素 + 縮放 → 裝置像素矩形（H1-06 `PaintBox`，半開區間）。
// **純函式、不會崩潰、不會產生溢位的 double→int 轉換**：先消毒、再夾限、最後才 lround。
ds::host::PaintBox resolve_placement(const LogicalPlacement& placement, double dpi_scale) noexcept;

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
    int dropped_elements = 0;                 // 被具名丟棄的元件數
    int repaired_fields = 0;                  // 被消毒的欄位數

    // 這份報告帶得出一個可用的佈局。
    bool usable() const noexcept {
        return status == IntakeStatus::Ok || status == IntakeStatus::Repaired ||
               status == IntakeStatus::Migrated;
    }
    // 讀進來的東西與檔案裡寫的不完全一樣（降級是**具名且可觀測**的，不是靜默）。
    bool degraded() const noexcept { return dropped_elements > 0 || repaired_fields > 0; }
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
    EmptyName,        // 名稱為空（E9-05 的契約）
    SerializeFailed,  // E9-05 序列化拒絕目前狀態（如 id / type 為空）
    VerifyFailed,     // 往返自驗不過 —— **不提交**，storage 原封不動
};

const char* to_string(SaveStatus status) noexcept;

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
    // 路徑不存在 → 走 **E7-12** `set_value` 新增。數值為非有限一律先擋下（不進持久化狀態）。
    EditStatus edit_property(const std::string& id, const std::string& path,
                             const ds::format::Value& value);
    // 取出某個屬性（找不到 → false）。
    bool property_of(const std::string& id, const std::string& path,
                     ds::format::Value& out) const;

    // 拖放產生設定項（**E7-15**）：`generate_config_item` 決定寫到哪個路徑、寫什麼。
    EditStatus apply_drop(const std::string& id, const ds::format::DropContent& drop);

    // --- 存檔 / 還原（E9-05；帶 format_version）---
    SaveStatus save(const std::string& name);
    IntakeStatus restore(const std::string& name);
    // 存檔但不提交，只看會不會過 —— 給呼叫端做「存得起來嗎」的預檢。
    SaveStatus dry_run_save() const;

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
    // 共用的寫入路徑：路徑存在 → E7-14 apply_edit；不存在 → E7-12 set_value。
    EditStatus write_property(ds::layout::LayoutElement& element, const std::string& path,
                              const ds::format::Value& value, std::string& message);
    EditStatus note_edit(EditStatus status, std::string message);
    // 採用一份載入報告：可用則換掉佈局，否則**保留現有佈局**。計數器由呼叫端各自加。
    void adopt(IntakeReport report);
    // 序列化目前狀態並往返自驗；通過才把文字寫進 `text`。
    SaveStatus serialize_verified(std::string& text, std::string& message) const;

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
