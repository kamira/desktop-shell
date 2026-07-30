// E7-04 動態變數與執行期重算 — 反應式變數作用域（平台中立 / engine 層）
//
// 本單元建構於 E7-02 的變數系統（`VariableScope` / `resolve`）與 E7-01 的資料模型
// （`Value`）之上：把「宣告一次、處處引用」的靜態變數，推進為**執行期可變、且相依者
// 自動重算**的反應式變數。屬「描述子系統」的一環——當某個來源值（視窗尺寸、主題色、
// 使用者偏好）在執行期改變時，凡由它衍生出的值（如 `half_width = width / 2`）都應
// 自動重新計算，毋須呼叫端手動追蹤誰依賴誰。
//
// 設計原則（延續 E7-01 / E7-02）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **循環相依不得靜默**（NFR-04 精神）：定義一條會形成循環的相依鏈一律回報帶訊息
//     （含肇因變數名）的 `ReactiveError`；絕不靜默接受、也不無限遞迴 / 堆疊爆掉。
//   - **消費上游、不重造輪子**：值型別沿用 E7-01 的 `Value`；不重造型別模型。
//
// 反應式模型：
//   - **來源變數（source）**：以 `set(name, value)` 註冊 / 更新，值可在執行期任意改變。
//   - **衍生變數（derived）**：以 `define_derived(name, deps, compute)` 定義；其值由一個
//     計算函式依 `deps` 列出的其他變數算出。`compute` 只能透過傳入的 `DerivedInputs`
//     讀取**已宣告的** `deps`（強制相依關係誠實、可追蹤）。
//   - **相依追蹤 + 髒標記傳播 + 拓撲重算**：`set` 一個來源值時，沿相依圖將所有（遞迴）
//     下游衍生變數標記為髒，並以拓撲序（相依先於被依賴）重算——**只重算受影響者**，
//     未受影響的衍生變數維持既有快取、不重算。
//   - **變更通知 / 訂閱**：`on_change(cb)` 註冊回呼；任一變數的快取值在一次傳播中實際
//     改變時，依節點建立序（來源優先）觸發回呼，帶上變更的變數名。
//   - **循環相依偵測**：`define_derived` 於定義當下檢查相依圖；若新相依會使該變數
//     （遞迴）依賴自身 → 回報 `ReactiveError`（不改變作用域狀態）。
//
// 語意細節：
//   - `set` 一個目前為 derived 的名稱 → 錯誤（衍生變數的值由 compute 決定，不可直接設定）。
//   - `define_derived` 的每個 dep 必須**已存在**（已 `set` 或已 `define_derived`）；引用
//     未知變數 → 錯誤（相依圖須完整，便於自底向上建構多層相依鏈）。
//   - `get(name)`：回傳變數目前值；未知變數或來源變數尚未設值 → 錯誤。衍生變數若為髒
//     （延遲求值路徑）則先重算再回傳。
//   - `compute` 內以 `DerivedInputs::get` 讀取未宣告於 `deps` 的名稱 → 契約違反，throw。
#ifndef DS_ENGINE_E7_04_REACTIVE_HPP
#define DS_ENGINE_E7_04_REACTIVE_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "variables.hpp"  // E7-02：ds::format::VariableScope（PUBLIC 帶入 E7-01 的 Value）

namespace ds::format {

class ReactiveScope;  // 前置宣告

// -----------------------------------------------------------------------------
// 衍生計算的輸入視圖
// -----------------------------------------------------------------------------

// 傳給 derived `compute` 的唯讀輸入：只能讀取該衍生變數**宣告過的** deps。
// 讀取未宣告的名稱屬呼叫端契約違反（會使相依追蹤失真）→ throw std::runtime_error。
class DerivedInputs {
public:
    // 取得某個已宣告 dep 的目前值（型別不符 / 未宣告 → throw）。
    const Value& get(const std::string& name) const;
    // 該名稱是否為本衍生變數宣告過的 dep。
    bool has(const std::string& name) const;
    // 便捷：等同 get(name)。
    const Value& operator[](const std::string& name) const { return get(name); }

private:
    friend class ReactiveScope;
    DerivedInputs(const ReactiveScope* scope, const std::vector<std::string>* deps)
        : scope_(scope), deps_(deps) {}
    const ReactiveScope* scope_ = nullptr;
    const std::vector<std::string>* deps_ = nullptr;
};

// 衍生變數的計算函式：由已宣告 deps 的目前值算出本變數的值。
using ComputeFn = std::function<Value(const DerivedInputs&)>;

// 變更通知回呼：帶上值已改變的變數名。
using ChangeCallback = std::function<void(const std::string&)>;

// -----------------------------------------------------------------------------
// 結果 / 錯誤（不靜默失敗）
// -----------------------------------------------------------------------------

// 反應式作用域操作錯誤——循環相依、未知變數、對衍生變數 set、dep 不存在等，
// 一律帶人類可讀訊息；`variable` 為肇因變數名（若適用）。
struct ReactiveError {
    std::string message;   // 人類可讀原因。
    std::string variable;  // 肇因變數名（無關時為空字串）。
};

// 反應式操作結果：成功持 `Value`（get / define_derived 的當前值；set 為新值），
// 失敗持 `ReactiveError`。二者互斥。對齊 E7-02 的 `ResolveResult` 風格。
class ReactiveResult {
public:
    static ReactiveResult success(Value v);
    static ReactiveResult failure(ReactiveError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const Value& value() const { return value_; }         // 僅 ok() 為 true 時有效。
    const ReactiveError& error() const { return error_; }  // 僅 ok() 為 false 時有效。

private:
    ReactiveResult() = default;
    bool ok_ = false;
    Value value_;
    ReactiveError error_;
};

// -----------------------------------------------------------------------------
// 反應式作用域
// -----------------------------------------------------------------------------

// 一個執行期可變、相依者自動重算的變數作用域。
//
// 節點分兩類：來源（source，`set`）與衍生（derived，`define_derived`）。作用域維護
// 相依圖（正向 deps + 反向 dependents）與每節點的快取值 / 髒標記。`set` 一個來源值時，
// 沿反向邊將受影響的下游衍生節點標記為髒，並以拓撲序重算——只重算受影響者，且對每個
// 值實際改變的節點觸發變更通知。
class ReactiveScope {
public:
    ReactiveScope() = default;

    // --- 註冊 / 更新 ---

    // 設定一個來源變數（不存在則建立，存在則更新其值）。
    // 若 name 目前為衍生變數 → 失敗（不可直接設定衍生變數）。
    // 成功時觸發受影響變數的重算與變更通知，回傳新值。
    ReactiveResult set(const std::string& name, Value value);

    // 定義一個衍生變數：其值由 compute 依 deps 的目前值算出。
    //   - 每個 dep 必須已存在；引用未知變數 → 失敗。
    //   - 新相依若使 name（遞迴）依賴自身 → 循環相依失敗（不改變作用域狀態）。
    //   - name 已存在且為衍生變數 → 以新 deps / compute 重定義（同樣做循環檢查）。
    //   - name 已存在且為來源變數 → 失敗（型別衝突；直接拒絕）。
    // 成功時立即求值一次並回傳其初始值，且對受影響下游重算 / 通知。
    ReactiveResult define_derived(const std::string& name,
                                  std::vector<std::string> deps,
                                  ComputeFn compute);

    // --- 查詢 ---

    // 取得變數目前值。未知變數 / 來源變數尚未設值 → 失敗。
    // 衍生變數若為髒則先重算（延遲求值路徑）。
    ReactiveResult get(const std::string& name) const;

    // 是否已註冊該變數（來源或衍生皆算）。
    bool has(const std::string& name) const;
    // 該變數是否為衍生變數（未知則為 false）。
    bool is_derived(const std::string& name) const;
    // 所有已註冊變數名（依建立序）。
    std::vector<std::string> names() const;
    // 已註冊變數數。
    std::size_t size() const noexcept { return order_.size(); }

    // --- 訂閱 ---

    // 註冊變更通知回呼；任一變數值在一次傳播中實際改變時被呼叫（帶變更變數名）。
    void on_change(ChangeCallback cb);

private:
    enum class Kind { Source, Derived };

    struct Node {
        std::string name;
        Kind kind = Kind::Source;
        Value value;                          // 快取值。
        bool has_value = false;               // 來源是否已設值 / 衍生是否已算過。
        bool dirty = false;                   // 需重算（僅對衍生有意義）。
        std::vector<std::string> deps;        // 正向：本節點依賴誰（衍生用）。
        std::vector<std::string> dependents;  // 反向：誰依賴本節點。
        ComputeFn compute;                    // 衍生的計算函式。
    };

    friend class DerivedInputs;

    // 內部以受控例外承載 ReactiveError，於公開邊界捕捉轉 failure（沿用 E7-02 手法）。
    struct ReactiveException {
        ReactiveError err;
    };

    Node* find_node(const std::string& name);
    const Node* find_node(const std::string& name) const;

    // 循環偵測：以「name 的 adjacency = new_deps」檢查任一 new_dep 能否（遞迴）抵達 name。
    bool would_create_cycle(const std::string& name,
                            const std::vector<std::string>& new_deps) const;
    bool reaches(const std::string& from, const std::string& target,
                 const std::string& override_name,
                 const std::vector<std::string>& override_deps,
                 std::vector<std::string>& visiting) const;

    // 將 root 的所有（遞迴）下游衍生節點標記為髒。
    void mark_dependents_dirty(const std::string& root);

    // 確保節點為最新：若髒（或未算過）則遞迴求值其 deps 後重算。
    // changed 收集本次實際改變值的變數名（供之後依建立序去重通知）。
    const Value& ensure_computed(Node& node, std::vector<std::string>& changed);

    // 對 changed 依建立序去重並觸發回呼。
    void notify(const std::vector<std::string>& changed);

    std::vector<Node> nodes_;                     // 節點池（僅新增不刪除，索引穩定）。
    std::vector<std::string> order_;              // 建立序（決定通知 / names 順序）。
    std::vector<ChangeCallback> callbacks_;       // 訂閱者。
    mutable std::vector<std::string> computing_;  // 執行期重算的「進行中」堆疊（防禦性循環偵測）。
};

}  // namespace ds::format

#endif  // DS_ENGINE_E7_04_REACTIVE_HPP
