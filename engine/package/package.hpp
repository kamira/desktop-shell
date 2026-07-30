// E9-01 統一套件格式定義 — 一個組件套件的整體結構（格式定義與解析/驗證，engine 層）
//
// 語意：E9-01 定義「一個組件套件」的**整體結構**，由三部分構成：
//   1. manifest（中繼資料）—— 直接複用 E9-02 的 `parse_manifest`，不重造。
//   2. 資源檔佈局 / 內含項目清單（content inventory）—— 套件內各資源的邏輯路徑與類別。
//   3. 結構完整性驗證 —— manifest 有效性 + 內容清單佈局的完整性。
//
// 設計原則（與 E9-02 一致）：
//   - **平台中立、純邏輯**：不含任何 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **錯誤可定位、不得靜默失敗**：解析/驗證失敗一律回傳帶「行號 + 訊息」的錯誤；
//     manifest 區段的錯誤由 E9-02 產生並原樣（含行號）向外傳遞。
//   - **不重造 manifest**：manifest 的欄位、版本、requires/permissions 一律走 E9-02。
//
// 套件描述文字格式（極簡、自足、無外部相依）：
//     # 以 '#' 開頭或全空白的行會被忽略
//     format_version: 1.0            # ┐
//     name: com.example.hello        # │ manifest 區段：逐行 key: value，
//     requires: host.tray_icon       # │ 交由 E9-02 parse_manifest 解析驗證
//     permissions: fs.read           # ┘
//     ---                            # 區段分隔線（單獨一行，trim 後恰為 "---"）
//     asset: icons/tray.png          # ┐ 內容清單區段：逐行 <kind>: <logical_path>，
//     code: main.wasm                # ┘ 描述套件內資源引用與佈局
//   - 分隔線之前 = manifest 區段（原樣交給 E9-02）；分隔線之後 = 內容清單區段。
//   - 無分隔線時：全文視為 manifest 區段，內容清單為空（僅含 manifest 的套件亦屬合法）。
//   - 內容清單每行首個 ':' 之前為 kind、之後為 logical_path；二者去空白後皆不得為空。
//   - logical_path 須為套件內相對路徑：不得以 '/' 開頭、不得含 '..' 區段、不得有空區段；重複路徑報錯。
#ifndef DS_ENGINE_E9_01_PACKAGE_HPP
#define DS_ENGINE_E9_01_PACKAGE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "manifest.hpp"  // E9-02（由 target_link_libraries PUBLIC e9_02 傳遞 include 路徑）

namespace ds::package {

// 套件內單一資源引用（內容清單的一項）。純資料。
struct PackageEntry {
    std::string kind;          // 資源類別（如 "asset" / "code" / "data"；語意對本格式不透明）。
    std::string logical_path;  // 套件內相對邏輯路徑（如 "icons/tray.png"）。
};

// 一個組件套件的整體結構：manifest（E9-02）+ 內含項目清單。純資料。
struct Package {
    Manifest manifest;                     // 複用 E9-02 解析所得；套件的中繼資料。
    std::vector<PackageEntry> entries;     // 內含項目清單 / 資源引用（可為空）。
};

// 解析/驗證結果：成功持有 Package，失敗持有 ParseError（複用 E9-02 的錯誤型別）。二者互斥。
class PackageResult {
public:
    static PackageResult success(Package p);
    static PackageResult failure(ParseError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅在 ok() 為 true 時有效。
    const Package& package() const { return package_; }

    // 僅在 ok() 為 false 時有效。
    const ParseError& error() const { return error_; }

private:
    PackageResult() = default;
    bool ok_ = false;
    Package package_;
    ParseError error_;
};

// 判斷一條套件內邏輯路徑是否結構合法：
// 非空、不以 '/' 開頭（須相對）、不含 '..' 區段（禁止逃逸套件根）、無空區段。
bool is_valid_logical_path(const std::string& path) noexcept;

// 從套件描述文字解析出 Package。見檔首格式說明。
// 解析過程即驗證：manifest 區段交由 E9-02 parse_manifest（錯誤含行號原樣傳出）；
// 內容清單每行須 <kind>: <logical_path> 且 path 結構合法、無重複。任一不符 → failure(帶行號)。
PackageResult parse_package(const std::string& text);

// 對一個「已在記憶體中構成」的 Package 做結構完整性驗證（不涉及來源行號，錯誤 line = 0）：
//   - manifest 有效性：name 非空、format_version 與本實作相容（複用 E9-02 is_format_compatible）。
//   - 內容清單完整性：每項 kind 非空、logical_path 結構合法、且路徑不重複。
PackageResult validate_package(const Package& pkg);

}  // namespace ds::package

#endif  // DS_ENGINE_E9_01_PACKAGE_HPP
