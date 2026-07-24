// E7-01 宣告式格式核心 + 版本欄位 — 描述子系統的根契約（平台中立 / engine 層）
//
// 本單元是「描述子系統」的**根契約**：它定義宣告式文件的資料模型（`Value` 多型節點樹）
// 與解析基座（文字 → 文件模型），並讓格式**帶版本欄位**（`format_version = major.minor`）。
// 眾多格式 / 設定單元（含 `E9-02` manifest）都是本格式的應用；manifest 是這個更通用格式的
// 一個特例。依 `docs/ai-guideline.md` §3 Q3：「平台的產品就是它的 API 面」，格式從第一版
// 就得帶版本欄位與相容策略。
//
// 設計原則：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **錯誤可定位、不得靜默失敗**（NFR-04 精神）：任何解析失敗一律回傳帶「行號 + 訊息」的
//     ParseError；絕不安靜吞掉未知 / 錯誤的輸入。
//   - **格式帶版本**：`format_version = major.minor`。相容 iff 主版號相同且次版號 <= 支援上限。
//
// 資料模型（`Value` 多型）：
//   Null / Bool / Number（帶「是否整數」旗標）/ String / List（有序）/ Map（有序、鍵唯一）。
//
// 文字宣告格式（極簡、自足、無外部相依；以縮排表達巢狀）：
//     # '#' 開頭或全空白的行會被忽略
//     format_version: 1.0        # 文件唯一必填欄位；於根層，值為 major.minor
//     name: com.example.hello    # key: value 純量
//     enabled: true              # bool
//     count: 42                  # 整數
//     ratio: 3.14                # 浮點
//     nothing: null              # null
//     quoted: "含: 冒號與\n換行"  # 雙引號字串（支援 \\ \" \n \t 轉義）
//     tags:                      # 清單：子行以 '- ' 開頭
//       - alpha
//       - beta
//     window:                    # 巢狀 map：子行更深縮排
//       width: 800
//       height: 600
//     layers:                    # 清單內含 map：'-' 獨佔一行，其下縮排為 map
//       -
//         name: base
//       -
//         name: overlay
//
// 規則摘要：
//   - 縮排只能用空白（**tab 會被明確拒絕並定位**）；子區塊縮排須嚴格深於父項。
//   - map 條目為 `key: value`；`key:`（值為空）表示巢狀子區塊（map 或 list），若其下無更深
//     縮排則值為 `null`。
//   - 清單項目為 `- 純量` 或獨佔一行的 `-`（其下縮排為巢狀 map / list）。
//   - 純量型別推斷：`null` → Null；`true` / `false` → Bool；合法數字 → Number；`"..."` → 字串
//     （帶轉義）；其餘一律視為裸字串（保留原樣，可含空白）。
//   - 根層必須是 map 且必含 `format_version`；重複 key、非預期縮排、tab 縮排、格式版本無法解析
//     或不相容 —— 全部視為錯誤並回報行號（不得靜默）。
#ifndef DS_ENGINE_E7_01_DOCUMENT_HPP
#define DS_ENGINE_E7_01_DOCUMENT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ds::format {

// -----------------------------------------------------------------------------
// 版本欄位：major.minor
// -----------------------------------------------------------------------------

// 宣告式格式版本。相容策略的基礎（Q3）。
//   - major 不同 = 不相容（結構性破壞）。
//   - minor 用於前向擴充：實作只能理解「不高於自身 minor」的文件。
struct FormatVersion {
    int major = 0;
    int minor = 0;

    bool operator==(const FormatVersion& o) const noexcept {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const FormatVersion& o) const noexcept { return !(*this == o); }
};

// 本實作支援（能理解）的格式版本上限。
// 文件相容 iff：major 相同 且 doc.minor <= kSupportedFormat.minor。
inline constexpr FormatVersion kSupportedFormat{1, 0};

// 判斷某格式版本是否為本實作所能理解。
// 相容 iff：major 相同 且 fmt.minor <= supported.minor。
bool is_format_compatible(const FormatVersion& fmt,
                          const FormatVersion& supported = kSupportedFormat) noexcept;

// -----------------------------------------------------------------------------
// 資料模型：Value 多型節點
// -----------------------------------------------------------------------------

enum class ValueType {
    Null,
    Bool,
    Number,
    String,
    List,
    Map,
};

// 宣告式文件的核心資料模型：一個多型節點。
//
// 純量以值語意持有；容器（List / Map）遞迴持有子 Value。Map 保留插入順序且鍵唯一。
// 錯誤型別存取（如對 Bool 呼叫 as_string）為呼叫端契約違反，會 throw std::runtime_error
// —— 明確失敗而非回傳可疑的預設值（NFR-04 精神）。存取前請先以 is_*() 查詢型別。
class Value {
public:
    using Member = std::pair<std::string, Value>;

    Value() = default;  // 預設為 Null。

    // --- 工廠（供解析器與程式化建構 / 測試使用）---
    static Value null();
    static Value boolean(bool b);
    static Value number(double v, bool integral = false);
    static Value integer(std::int64_t v);
    static Value string(std::string s);
    static Value list(std::vector<Value> items);
    static Value map(std::vector<Member> members);

    // --- 型別查詢 ---
    ValueType type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == ValueType::Null; }
    bool is_bool() const noexcept { return type_ == ValueType::Bool; }
    bool is_number() const noexcept { return type_ == ValueType::Number; }
    bool is_string() const noexcept { return type_ == ValueType::String; }
    bool is_list() const noexcept { return type_ == ValueType::List; }
    bool is_map() const noexcept { return type_ == ValueType::Map; }
    // 該 Number 是否來自整數字面值（無小數點 / 指數）。非 Number 恆為 false。
    bool is_integer() const noexcept { return type_ == ValueType::Number && integral_; }

    // --- 純量存取（型別不符 → throw std::runtime_error）---
    bool as_bool() const;
    double as_number() const;
    std::int64_t as_int() const;  // Number 以整數截斷取出。
    const std::string& as_string() const;

    // --- 容器存取 ---
    const std::vector<Value>& as_list() const;    // 型別須為 List。
    const std::vector<Member>& as_map() const;     // 型別須為 Map（有序）。

    // List / Map 的元素數（其他型別 → throw）。
    std::size_t size() const;

    // --- Map 便捷查詢（型別須為 Map）---
    bool contains(const std::string& key) const;
    const Value* find(const std::string& key) const;  // 不存在回 nullptr。
    const Value& at(const std::string& key) const;     // 不存在 → throw。
    std::vector<std::string> keys() const;

    // 深層相等（型別 + 內容遞迴比較；Map 比較保序）。便於測試。
    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }

private:
    ValueType type_ = ValueType::Null;
    bool bool_ = false;
    double num_ = 0.0;
    bool integral_ = false;
    std::string str_;
    std::vector<Value> list_;
    std::vector<Member> map_;
};

// -----------------------------------------------------------------------------
// 文件：版本 + 內容根
// -----------------------------------------------------------------------------

// 一份解析後的宣告式文件。
//   - format_version：自根層 `format_version` 欄位取出的型別化版本（已驗相容）。
//   - root：其餘內容組成的根 Map（**不含** format_version 本身，使 root 為純內容）。
struct Document {
    FormatVersion format_version;
    Value root;  // 恆為 Map 型別。
};

// -----------------------------------------------------------------------------
// 解析：文字 → 文件（錯誤可定位）
// -----------------------------------------------------------------------------

// 解析 / 驗證錯誤 —— 一律可定位到來源行（不得靜默失敗）。
struct ParseError {
    std::size_t line = 0;   // 1-based 來源行號；0 = 非特定行（如缺必填欄位、版本不相容）。
    std::string message;    // 人類可讀原因。
};

// 解析結果：成功則持有 Document，失敗則持有 ParseError。二者互斥。
class ParseResult {
public:
    static ParseResult success(Document d);
    static ParseResult failure(ParseError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅在 ok() 為 true 時有效。
    const Document& document() const { return document_; }

    // 僅在 ok() 為 false 時有效。
    const ParseError& error() const { return error_; }

private:
    ParseResult() = default;
    bool ok_ = false;
    Document document_;
    ParseError error_;
};

// 從文字宣告解析一份文件。見檔首格式說明。
// 解析過程即驗證：縮排一致（禁 tab）、無重複 key、根層含可解析且相容的 format_version、
// 純量型別可推斷。任一不符 → failure(帶行號)。
ParseResult parse(const std::string& text);

// 解析純量 token 為 Value（供進階呼叫端 / 測試直接使用同一套型別推斷）。
// 規則見檔首。不會失敗於「非數字」——非 null/bool/數字/引號字串者一律回裸字串。
// 例外：格式錯誤的引號字串會回傳 false（out 不動）。
bool parse_scalar(const std::string& token, Value& out);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_01_DOCUMENT_HPP
