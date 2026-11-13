// H1-07 組件組合裝配 —— widget = 一或多個組件，組件位置以 widget 內相對邏輯像素表達
//
// 本檔**不含任何 win32 呼叫、不 include windows.h、不含任何平台條件編譯**，`host/compose/`
// 整個目錄的 CMakeLists 也**不包 `if(WIN32)`**（照 H1-06 `host/paint/CMakeLists.txt` 的先例）：
// 本單元的內容是純邏輯命題 —— 消費 E9-03 的組合模型、把組件擺到 widget 內的相對像素位置 ——
// 沒有任何一行需要 Windows。放在守衛外，ubuntu 的 **G3**（`-DDS_WERROR=ON`）與
// **`gate_sanitizers`**（ASan + UBSan）才真的建置並執行它的測試；包進守衛內則三道防線會
// 同時放行一個零覆蓋的單元（H1-06 驗收時實測：註冊數 183 → 181、該單元零命中，
// 而 G3 / gate_sanitizers / gate_windows **全綠**；那個洞由 `CHG-20261112-07` 的 G3b 補上）。
//
// ---------------------------------------------------------------------------
// 三個概念
// ---------------------------------------------------------------------------
//   1. **組件（component）= 獨立組合單位**。組件就是 E9-01 的 `ds::package::Package`
//      （manifest + 內容清單），本層不重新定義「組件是什麼」。
//   2. **widget = 一或多個組件**。`WidgetAssembly` 內部持有一個 **E9-03
//      `ds::package::Composition`**：插槽宣告 / 綁定 / 替換 / 清空 / 相容性檢查 /
//      完整性驗證**一律轉呼叫它**，本層不重造任何一條。本層**只多加一件事** ——
//      每個插槽在 widget 內的**像素版面與呈現型態**。
//   3. **呈現型態（`ComponentPresentation`）屬於「組件在這個 widget 裡」，不屬於組件本身**。
//      同一個組件（同一個 `Package`）可同時綁進兩個 widget 的插槽，各自有不同的像素框與
//      不同的具名變體 —— 這是驗收標準「同一組件須能同時出現在兩個 widget 且呈現型態可不同」
//      之所以成立的機制（第二案例見 `media_compose.hpp`，消費 C2-07 媒體控制）。
//
// ---------------------------------------------------------------------------
// 座標系與單位（驗收標準明文）
// ---------------------------------------------------------------------------
// - **原點左上**：`PixelPoint{0,0}` 是 widget 自身的左上角，x 向右遞增、y 向下遞增。
// - **widget 內相對**：組件位置一律相對於它所屬的 widget，**不是螢幕絕對座標**。
//   widget 自己擺在螢幕哪裡由 E1-07 具名錨點 / E1-24 具名圖層決定，本層看不到也不需要。
// - **單位為邏輯像素，隨 DPI 縮放**：`ComponentPlacement` 存的是**邏輯**像素；
//   `resolve()` 時乘上縮放係數得到實際像素。縮放係數**取自 kernel 的 E1-17
//   `DpiInfo::scale_of(screen)`** —— 注意 kernel 交出來的是**具名螢幕的縮放係數（純比值）**，
//   不是像素；這正是下面那條反向斷言要保住的形狀。
//
// ---------------------------------------------------------------------------
// 反向斷言：E4 與 kernel 的公開介面不得出現任何像素型別
// ---------------------------------------------------------------------------
// H1-07（連同 H1-06 的繪製層）是全系統**唯一知道像素的一層**。像素型別往上滲進 E4 / kernel
// 的公開介面，會在相位 3 換平台後端時才爆，而 `backend_guard` 擋不到（它只擋
// `src/kernel/backend/win32|cocoa/` 這類後端路徑，不看型別）。
// 這條斷言的機器化判定在 `pixel_boundary.hpp`（獨立 target，零相依），
// 由 `tests/h1/test_h1_07_pixel_boundary.cpp` 兩個方向各自釘住。
//
// **本層的像素型別（`PixelPoint` / `PixelSize` / `ResolvedComponent` / `WidgetLayout`，
// 以及沿用 H1-06 的 `PaintBox`）只存在於 `host/`，刻意不往上送**：
// 本層對上游的輸入只有 `ds::package::Package` / `ds::package::ComponentSlot`（具名、無幾何）
// 與一個 `double` 縮放係數。
//
// 像素矩形**沿用 H1-06 的 `ds::host::PaintBox`**，不另造第二個：`host/` 裡有兩個長得一樣的
// 像素矩形型別，遲早有人在兩者之間寫轉換函式、然後在某一側漏掉半開區間的慣例。
// `PaintBox` 是半開區間 [left,right) x [top,bottom)，`WidgetLayout` 交出的框可直接餵進
// H1-06 的 `ElementRendererTable::draw()`。
#ifndef DS_HOST_COMPOSE_WIDGET_COMPOSE_HPP
#define DS_HOST_COMPOSE_WIDGET_COMPOSE_HPP

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "composition.hpp"      // E9-03（上游，可讀不可改）：Composition / ComponentSlot /
                                 //   CompatibilityResult / BindResult；並經其傳遞 E9-01 Package。
#include "dpi_awareness.hpp"    // E1-17（上游，可讀不可改）：DpiInfo / ScreenId / kDefaultScaleFactor
#include "render_dispatch.hpp"  // H1-06（上游，可讀不可改）：PaintBox（像素矩形的單一來源）

namespace ds::host {

// ---------------------------------------------------------------------------
// 邏輯像素的點與尺寸（原點左上、x 向右、y 向下）
// ---------------------------------------------------------------------------
// 刻意是 `int`：像素是可數的離散量。比例值（`float` / `double`）是上游 E4 / kernel 的表達
// 方式，本層把比例**換算成**像素，兩者不混用。
struct PixelPoint {
    int x = 0;
    int y = 0;
};

struct PixelSize {
    int width = 0;
    int height = 0;

    // 無可用面積（寬或高非正）。退化尺寸是「輸入不合法」，由具名狀態回報，不靜默吞掉。
    bool empty() const noexcept { return width <= 0 || height <= 0; }
};

bool operator==(const PixelPoint& a, const PixelPoint& b) noexcept;
bool operator!=(const PixelPoint& a, const PixelPoint& b) noexcept;
bool operator==(const PixelSize& a, const PixelSize& b) noexcept;
bool operator!=(const PixelSize& a, const PixelSize& b) noexcept;

// ---------------------------------------------------------------------------
// DPI 縮放（純函式）
// ---------------------------------------------------------------------------
// 非有限（NaN / inf）或 <= 0 的縮放係數一律當成 `kDefaultScaleFactor`（1.0）。
// 與 E1-17 `DpiInfo` 對未知螢幕的處置同一條保守原則：**寧可不縮放，不可誤縮放**。
// 這同時是 K-002 的形狀 —— `!(s > 0.0)` 而非 `s <= 0.0`：NaN 的任何比較都為 false，
// 寫成 `s <= 0.0` 會讓 NaN 一路落到「係數正常」的分支，最後把版面乘成全 0 或垃圾值，
// 而呼叫端看到的是一個「看起來算過了」的結果。
double sane_scale(double scale) noexcept;

// 邏輯像素 → 實際像素（四捨五入到最近整數；溢位夾限於 int 值域）。
int scale_logical(int logical, double scale) noexcept;

// 邏輯像素的 (原點, 尺寸) → 實際像素的半開區間矩形。
PaintBox scale_box(PixelPoint origin, PixelSize size, double scale) noexcept;

// ---------------------------------------------------------------------------
// 呈現型態 —— 「組件在這個 widget 裡長什麼樣」
// ---------------------------------------------------------------------------
// `variant` 是**具名字串**而非 enum：enum 一加值就得改本層，字串鍵不必 ——
// 與 H1-06 的 `ElementDraw::kind` 同一個理由（新增呈現型態不改平台）。
// `params` 是具名 double 袋子，本層不解釋其語意，只負責原樣帶給認得它的呈現層。
struct ComponentPresentation {
    std::string variant;                  // 空字串 = 預設呈現
    std::map<std::string, double> params; // 該變體的具名參數
    bool visible = true;                  // false = 本 widget 內暫不顯示（仍佔插槽，不是未綁定）

    // 取一個具名參數；不存在則回 fallback（**不靜默新增鍵**）。
    double param(const std::string& name, double fallback) const;
};

bool operator==(const ComponentPresentation& a, const ComponentPresentation& b);
bool operator!=(const ComponentPresentation& a, const ComponentPresentation& b);

// 一個組件在 widget 內的放置：邏輯像素的位置 + 尺寸 + 呈現型態。
struct ComponentPlacement {
    PixelPoint origin;                  // widget 內相對位置（原點左上）
    PixelSize size;                     // 邏輯像素尺寸
    ComponentPresentation presentation;
};

// ---------------------------------------------------------------------------
// 具名結果（失敗一律說出原因，不靜默）
// ---------------------------------------------------------------------------
enum class ComposeStatus {
    Ok,
    EmptySlotId,            // 插槽 id 為空
    DuplicateSlot,          // 插槽 id 在本 widget 內重複
    UnknownSlot,            // 本 widget 沒有這個插槽
    DegenerateSize,         // 組件（或 widget 自身）的寬或高非正
    OutOfBounds,            // 放置超出 widget 的邏輯邊界（含負座標）
    SlotAlreadyBound,       // 首次綁定用 bind、已綁定請用 swap（E9-03 的語意，原樣傳出）
    SlotNotBound,           // 尚未綁定（swap / unbind 的前提不成立）
    Incompatible,           // 元件不符插槽介面（E9-03 `check_compatibility` 的原因原樣傳出）
    EmptyWidget,            // widget 一個組件都沒有 —— 「widget = 一或多個組件」不成立
    IncompleteComposition,  // 有插槽未綁定 / 某綁定已不相容（E9-03 `validate()` 的原因）
};

const char* to_string(ComposeStatus status) noexcept;

// 操作結果：成功，或帶具名狀態 + 人類可讀原因。失敗時**狀態完全不變**（不部分套用）。
struct ComposeResult {
    ComposeStatus status = ComposeStatus::Ok;
    std::string message;  // 失敗時非空；成功時為空

    explicit operator bool() const noexcept { return status == ComposeStatus::Ok; }
    bool ok() const noexcept { return status == ComposeStatus::Ok; }

    static ComposeResult success();
    static ComposeResult failure(ComposeStatus status, std::string why);
};

// ---------------------------------------------------------------------------
// 解析後的版面
// ---------------------------------------------------------------------------
// 一個已綁定組件在 widget 內的實際像素框（已套 DPI 縮放）。
struct ResolvedComponent {
    std::string slot_id;         // E9-03 的插槽 id
    std::string component_name;  // 綁定組件的 E9-01 `manifest.name`（具名，非數字 handle）
    PaintBox box;                // widget 內相對的實際像素框（半開區間，可直接餵給 H1-06）
    ComponentPresentation presentation;
    // 本次解析採用的 DPI 縮放係數（已過 `sane_scale`）。**組件自己的內部版面也要縮放** ——
    // 呈現層（如 `media_compose.hpp`）用邏輯像素寫內部尺度，需要同一個係數才換算得回實際像素；
    // 若不帶著它走，呈現層就只能改用比例值，而比例值在整數像素上會各自湊出不同的捨入結果。
    double scale_factor = 1.0;
};

// 整個 widget 的版面：widget 自身的框 + 每個組件的框（依插槽宣告順序）。
struct WidgetLayout {
    std::string widget_id;
    PaintBox bounds;                             // widget 自身（左上角恆為 (0,0)：widget 內相對）
    double scale_factor = 1.0;                   // 本次解析實際採用的縮放係數（已過 sane_scale）
    std::vector<ResolvedComponent> components;

    // 依插槽 id 找組件；找不到回 nullptr（**不回一個假的空框**）。
    const ResolvedComponent* find(const std::string& slot_id) const noexcept;
    std::size_t component_count() const noexcept { return components.size(); }
};

// 兩個已解析組件的像素框是否重疊（半開區間語意）。
// **重疊不是錯誤**：封面之上疊曲名、按鈕列疊在底圖上都是正當版面，故本函式是查詢而非驗證。
bool boxes_overlap(const PaintBox& a, const PaintBox& b) noexcept;

// ---------------------------------------------------------------------------
// WidgetAssembly —— widget = 一或多個組件
// ---------------------------------------------------------------------------
class WidgetAssembly {
public:
    // `logical_size` 是 widget 自身的邏輯像素尺寸（組件的放置以它為界）。
    // 非正尺寸不在建構時報錯（建構子無法回結果），但 `declare()` 與 `resolve()` 都會以
    // `DegenerateSize` 擋下 —— 失敗被延後到有回傳值的地方，不是被吞掉。
    WidgetAssembly(std::string widget_id, PixelSize logical_size);

    const std::string& id() const noexcept { return widget_id_; }
    PixelSize logical_size() const noexcept { return logical_size_; }

    // 宣告一個插槽並同時給它在 widget 內的放置。
    // 轉呼叫 E9-03 `Composition::add_slot`（id 空 / 重複由它判定，原因原樣傳出）。
    // **先驗放置、後動組合**：放置不合法時 E9-03 的組合完全沒被碰過。
    ComposeResult declare(const ds::package::ComponentSlot& slot,
                          const ComponentPlacement& placement);

    // 綁定 / 替換 / 清空 —— 全部轉呼叫 E9-03（相容性檢查不重造）。
    ComposeResult bind(const std::string& slot_id, const ds::package::Package& component);
    ComposeResult swap(const std::string& slot_id, const ds::package::Package& component);
    ComposeResult unbind(const std::string& slot_id);

    // 只改像素版面（不動綁定）。
    ComposeResult place(const std::string& slot_id, PixelPoint origin, PixelSize size);
    // 只改呈現型態（不動綁定、不動版面）—— 同一組件在兩個 widget 呈現不同的入口。
    ComposeResult present(const std::string& slot_id, const ComponentPresentation& presentation);

    // --- 查詢 ---
    // E9-03 的組合物件本體：相容性列舉 / 插槽定義 / 綁定查詢一律直接走它，本層不代理一份。
    const ds::package::Composition& composition() const noexcept { return composition_; }

    bool has_slot(const std::string& slot_id) const noexcept;
    bool is_bound(const std::string& slot_id) const noexcept;
    const ds::package::Package* bound_component(const std::string& slot_id) const noexcept;
    const ComponentPlacement* placement(const std::string& slot_id) const noexcept;
    std::vector<std::string> slot_ids() const;  // 依宣告順序
    std::size_t slot_count() const noexcept { return slots_.size(); }

    // --- 解析成像素版面 ---
    // 前置條件（任一不成立即具名失敗，**不產出半套版面**）：
    //   - 至少一個插槽（`EmptyWidget`：widget = 一或**多**個組件，零個不是 widget）
    //   - widget 自身尺寸為正（`DegenerateSize`）
    //   - E9-03 `Composition::validate()` 通過（`IncompleteComposition`，原因原樣傳出）
    ComposeResult resolve(double scale_factor, WidgetLayout& out) const;

    // 縮放係數取自 E1-17 的具名螢幕。未知螢幕 → E1-17 自己保守回 `kDefaultScaleFactor`。
    ComposeResult resolve(const ds::kernel::DpiInfo& dpi, const ds::kernel::ScreenId& screen,
                          WidgetLayout& out) const;

private:
    std::size_t index_of(const std::string& slot_id) const noexcept;
    // 放置是否落在 widget 的邏輯邊界內（含退化尺寸與負座標）。
    ComposeResult check_placement(const ComponentPlacement& placement) const;

    struct SlotPlacement {
        std::string id;
        ComponentPlacement placement;
    };

    std::string widget_id_;
    PixelSize logical_size_;
    ds::package::Composition composition_;  // E9-03：組合語意的唯一來源
    std::vector<SlotPlacement> slots_;      // 宣告順序（與 composition_ 的插槽一一對應）
};

// ---------------------------------------------------------------------------
// 跨 widget：同一組件的多次現身
// ---------------------------------------------------------------------------
// 一次「現身」= 某個組件出現在某個 widget 的某個插槽，帶該處的像素框與呈現型態。
struct ComponentAppearance {
    std::string widget_id;
    std::string slot_id;
    PaintBox box;
    ComponentPresentation presentation;
};

// 具名組件（E9-01 `manifest.name`）在給定的一組 widget 內的所有現身，依 widget 的給定順序。
// 解析失敗的 widget 直接跳過（它的失敗由它自己的 `resolve()` 回報，本函式不複述、也不假造）。
// nullptr 的 widget 跳過（防呼叫端誤傳，不崩）。
std::vector<ComponentAppearance> appearances_of(const std::string& component_name,
                                                const std::vector<const WidgetAssembly*>& widgets,
                                                double scale_factor);

// 兩次現身的**呈現型態是否不同**：具名變體 / 具名參數 / 可見性 / 像素框任一不同即為不同。
// 這是驗收標準「呈現型態可不同」的機器判準。
bool presentation_differs(const ComponentAppearance& a, const ComponentAppearance& b);

}  // namespace ds::host

#endif  // DS_HOST_COMPOSE_WIDGET_COMPOSE_HPP
