// E9-02 套件/模組 manifest — 格式定義與解析（平台中立 / engine 層）
//
// 依 `docs/ai-guideline.md` §3 Q3：manifest 具 `requires`/`permissions`，
// 且**格式帶版本欄位**（`format_version`）。此檔定義 manifest 的資料結構、
// 文字宣告格式的解析，以及相容性/必填欄位的驗證。
//
// 設計原則：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **錯誤可定位、不得靜默失敗**：解析失敗一律回傳帶「行號 + 訊息」的 ParseError；
//     絕不安靜吞掉未知欄位或格式錯誤。
//   - **格式帶版本**：`format_version = major.minor`。主版號相容性 + 未來欄位以次版號前向擴充。
//
// 文字宣告格式（極簡、自足、無外部相依）：
//     # 以 '#' 開頭或全空白的行會被忽略
//     format_version: 1.0
//     name: com.example.hello
//     version: 0.1.0
//     description: 範例模組
//     requires: host.tray_icon, host.global_hotkey
//     permissions: fs.read, net.connect
//   - 每行 `key: value`，首個 ':' 之後為 value（value 內可含 ':'）。
//   - `requires` / `permissions` 為逗號分隔清單；空項會被略過。
//   - 重複 key、未知 key、缺 ':'、格式版本無法解析 —— 全部視為錯誤並回報行號。
#ifndef DS_ENGINE_E9_02_MANIFEST_HPP
#define DS_ENGINE_E9_02_MANIFEST_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ds::package {

// manifest 格式版本：major.minor。
//   - major 不同 = 不相容（結構性破壞）。
//   - minor 用於前向擴充：host 只能理解「不高於自身 minor」的 manifest。
struct FormatVersion {
    int major = 0;
    int minor = 0;

    bool operator==(const FormatVersion& o) const noexcept {
        return major == o.major && minor == o.minor;
    }
    bool operator!=(const FormatVersion& o) const noexcept { return !(*this == o); }
};

// 本實作支援（能理解）的 manifest 格式版本上限。
// manifest 相容 iff：major 相同 且 manifest.minor <= kSupportedFormat.minor。
inline constexpr FormatVersion kSupportedFormat{1, 0};

// 解析後的 manifest。純資料；欄位語意見下。
struct Manifest {
    FormatVersion format_version;                // 必填。格式版本欄位（Q3）。
    std::string name;                            // 必填。套件/模組穩定識別（如 "com.example.hello"）。
    std::string version;                         // 選填。套件自身版本字串（對本格式不透明）。
    std::string description;                     // 選填。人類可讀說明。
    std::vector<std::string> required_capabilities;  // 文字 key = `requires`：所需能力 id 清單。
    std::vector<std::string> permissions;            // 文字 key = `permissions`：所需權限清單。
    // 註：C++ 成員名為 required_capabilities 而非 requires —— requires 為 C++20 保留字，
    //     避免未來提升標準時破裂。manifest 文字格式的 key 仍是 `requires`（見 Q3 契約）。
};

// 解析/驗證錯誤 —— 一律可定位到來源行（不得靜默失敗）。
struct ParseError {
    std::size_t line = 0;   // 1-based 來源行號；0 = 非特定行（如缺必填欄位、版本不相容）。
    std::string message;    // 人類可讀原因。
};

// 解析結果：成功則持有 Manifest，失敗則持有 ParseError。二者互斥。
class ParseResult {
public:
    static ParseResult success(Manifest m);
    static ParseResult failure(ParseError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅在 ok() 為 true 時有效。
    const Manifest& manifest() const { return manifest_; }

    // 僅在 ok() 為 false 時有效。
    const ParseError& error() const { return error_; }

private:
    ParseResult() = default;
    bool ok_ = false;
    Manifest manifest_;
    ParseError error_;
};

// 從文字宣告解析 manifest。見檔首格式說明。
// 解析過程即驗證：格式版本可解析、必填欄位齊備（format_version + name）、
// 無重複/未知 key、格式版本相容。任一不符 → failure(帶行號)。
ParseResult parse_manifest(const std::string& text);

// 判斷某 manifest 格式版本是否為本實作所能理解。
// 相容 iff：major 相同 且 fmt.minor <= supported.minor。
bool is_format_compatible(const FormatVersion& fmt,
                          const FormatVersion& supported = kSupportedFormat) noexcept;

}  // namespace ds::package

#endif  // DS_ENGINE_E9_02_MANIFEST_HPP
