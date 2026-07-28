// E7-15 拖放產生設定項 — 把拖放進來的內容轉成設定項（engine 層 / 平台中立）
//
// 本單元是 E7-12「設定值寫回」與 E5-08「系統事件」的**應用**：把一段拖放進來的
// 抽象內容（檔案路徑 / URL / 文字 / 顏色…）依內容類型推斷，**產生對應的設定項**
// （如拖入圖片→背景設定、拖入 URL→連結元件），再經 E7-12 寫入宣告式設定文件。
//
//   拖放內容 ──偵測類型──▶ ConfigItem{ 路徑, 值 } ──E7-12 set_value──▶ 新文件
//
// 相位 1（Mac / null 期）約束（與 E5-08 / E7-12 一致）：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / win32 / cocoa / 系統呼叫 / 真實後端。
//   - **不接真實 OS 拖放**：拖放內容以抽象資料（`DropContent`）表示。相位 1 的內容經由
//     E5-08 的事件分派「手動注入」（見 `subscribe_drops`），相位 2 換真實拖放後端時，
//     偵測 / 產生 / 寫回語意一行不動，後端只需在拖放到達時走同一條分派路徑。
//   - **不靜默失敗**（承 E7-01 / E7-12 NFR-04 精神）：無法辨識的內容以 `DropKind::Unknown`
//     **明確回報**；對 Unknown 產生設定項會 throw `std::runtime_error`，而非安靜產出可疑結果。
#ifndef DS_ENGINE_E7_15_DROPGEN_HPP
#define DS_ENGINE_E7_15_DROPGEN_HPP

#include <functional>
#include <string>
#include <vector>

#include "document.hpp"       // E7-01：Value / Document / FormatVersion（經 e7_12 傳遞）
#include "writeback.hpp"      // E7-12：Path / set_value / serialize（相依 target e7_12）
#include "system_event.hpp"   // E5-08：SystemEvent / SystemEventSource（相依 target e5_08）

namespace ds::format {

// -----------------------------------------------------------------------------
// 拖放內容：平台中立的抽象表示
// -----------------------------------------------------------------------------

// 拖放內容的種類。以內容語意分類（跨平台一致）。
enum class DropKind {
    File,     // 檔案路徑（含圖片；圖片另由 is_image_path 細分對應到背景設定）
    Url,      // 連結（http/https/ftp 之類）
    Text,     // 純文字片段
    Color,    // 顏色（如 #RRGGBB）
    Unknown,  // 無法辨識——明確回報，不靜默猜測
};

// 單一拖放內容。純資料、平台中立——不含任何 OS 原生型別或控制代碼。
//   - kind：內容種類（可由平台的拖放 flavor 明確給定，或由 detect() 自原始字串推斷）。
//   - payload：內容本體（檔案路徑 / URL 文字 / 文字內容 / 顏色字面值）。
struct DropContent {
    DropKind kind = DropKind::Unknown;
    std::string payload;

    // --- 明確工廠：平台已知拖放 flavor 時使用（不經推斷）---
    static DropContent file(std::string path);
    static DropContent url(std::string u);
    static DropContent text(std::string t);
    static DropContent color(std::string c);

    // --- 推斷工廠：只有一段原始字串（如 E5-08 事件 detail）時使用 ---
    // 依 detect_drop_kind 推斷 kind；無法辨識則 kind == Unknown（payload 仍保留原始字串）。
    static DropContent detect(std::string raw);

    bool operator==(const DropContent& o) const noexcept {
        return kind == o.kind && payload == o.payload;
    }
    bool operator!=(const DropContent& o) const noexcept { return !(*this == o); }
};

// 自一段原始拖放字串推斷內容種類。
//   規則（依序）：空 / 僅空白 或 含控制位元組（非 \t / \n）→ Unknown（不靜默猜測）；
//   `#` 後接 3/4/6/8 位十六進位 → Color；`http:// https:// ftp://` 前綴 → Url；
//   絕對路徑（`/` 開頭）/ `file://` / 具檔名副檔名且無空白 → File；其餘可列印文字 → Text。
DropKind detect_drop_kind(const std::string& raw);

// 判斷一個檔案路徑是否指向圖片（副檔名比對，大小寫不敏感）。
// 支援：png jpg jpeg gif bmp webp svg tiff tif ico。
bool is_image_path(const std::string& path);

// -----------------------------------------------------------------------------
// 設定項：拖放內容轉成的「該寫進設定文件哪裡、寫什麼」
// -----------------------------------------------------------------------------

// 一個待寫入的設定項：E7-12 的字串路徑 + 要設定的 Value。
struct ConfigItem {
    std::string path;   // E7-12 parse_path 語法（如 "background.image"、"link.url"）。
    Value value;        // 要設定的值。

    bool operator==(const ConfigItem& o) const {
        return path == o.path && value == o.value;
    }
    bool operator!=(const ConfigItem& o) const { return !(*this == o); }
};

// 依拖放內容類型產生對應的設定項（內容→設定映射的單一事實來源）：
//   - File 且為圖片 → 背景設定：path "background.image"，值為檔案路徑字串。
//   - File 非圖片   → 一般檔案：path "file.path"，值為檔案路徑字串。
//   - Url           → 連結元件：path "link.url"，值為 URL 字串。
//   - Color         → 顏色設定：path "color"，值為顏色字串。
//   - Text          → 文字設定：path "text"，值為文字字串。
//   - Unknown       → **throw std::runtime_error**（無法辨識，明確失敗不靜默）。
ConfigItem generate_config_item(const DropContent& drop);

// -----------------------------------------------------------------------------
// 套用：把拖放內容經 E7-12 寫入設定文件（純函式，回傳新樹 / 新文件）
// -----------------------------------------------------------------------------

// 對根 Value（須為 Map）套用一筆拖放：產生設定項後以 E7-12 set_value 寫入，回傳**新** Value。
//   Unknown 內容 → throw（同 generate_config_item）。路徑套用之契約違反沿用 E7-12 語意（throw）。
Value apply_drop(const Value& root, const DropContent& drop);

// 對整份文件套用一筆拖放：於 doc.root 寫入設定項，回傳版本不變、root 更新的新文件。
Document apply_drop(const Document& doc, const DropContent& drop);

// 對整份文件依序套用多筆拖放（多項拖放）：逐筆 set_value 疊加，回傳新文件。
//   任一筆為 Unknown → throw（不靜默略過）；已套用的前序筆數不回捲（呼叫端可自行以副本重試）。
Document apply_drops(const Document& doc, const std::vector<DropContent>& drops);

// -----------------------------------------------------------------------------
// 與 E5-08 系統事件整合：把事件分派過來的拖放內容轉成設定項
// -----------------------------------------------------------------------------
//
// 相位 1 的拖放內容經由 E5-08 的事件分派「注入」：事件的 `detail` 欄位攜帶抽象拖放
// 內容（一段字串），本單元以 detect() 推斷其種類。相位 2 的真實拖放後端可沿同一
// 分派路徑送出攜帶內容的事件，本單元的偵測 / 產生 / 寫回語意一行不動。
// （E5-08 目前的事件種類未含專屬拖放型別；此橋接以 detail 承載內容，與其 null 後端
//  「事件由注入手動觸發」的相位 1 模型一致。）

// 自一個 E5-08 系統事件擷取拖放內容：以事件 detail 為原始字串推斷。
DropContent drop_from_system_event(const ds::events::SystemEvent& event);

// 訂閱一個 E5-08 事件來源，把每個到達事件轉成 DropContent 後交給 handler。
// 回傳 E5-08 的訂閱代號（可用其 unsubscribe 解除）；handler 為空則回傳 0（無效訂閱）。
ds::events::SubscriptionId subscribe_drops(
    ds::events::SystemEventSource& source,
    std::function<void(const DropContent&)> handler);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_15_DROPGEN_HPP
