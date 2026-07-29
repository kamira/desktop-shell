// E2-26 RSS / 標題抓取 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：抓取 **RSS/Atom feed 或網頁標題**的 sysinfo 提供者——透過 **E2-14 的 HTTP 取得機制**
// （可注入 `HttpTransport` + null 傳輸）抓內容，解析出 feed 項目（標題 / 連結 / 時間）或網頁
// `<title>`，並以 **E2-01 的 MetricProvider 介面**暴露（每項目一實例、最新 N 筆）。這是「新增
// 指標 = 新增 `MetricProvider`、掛件一行不動」機制的又一具體提供者——它**消費 E2-01 / E2-14
// 契約、不自造指標模型、不自造 HTTP 傳輸**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **沿用 E2-14 的可注入 `HttpTransport` + `NullHttpTransport`**：**不真的發網路請求**。真實
//     網路後端（libcurl / socket / 平台 HTTP 堆疊）留待後端相位；本檔一律不含。
//   - 無 `#ifdef`、無 socket、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//   - **RSS / Atom / HTML 標題解析為純文字處理**：自帶輕量標記（markup）掃描器把回應 body 轉成
//     結構化的 `FeedDocument`（feed 標題 + 項目清單，或網頁 `<title>`）。解析失敗**不靜默吞掉**
//     （NFR-04 精神）：回帶訊息的錯誤。**空 feed 視為成功（0 項目），malformed 內容視為錯誤。**
//   - 命名空間 `ds::sysinfo` 與既有 sysinfo（E2-14 等）一致。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id / name 由建構時指定（供多個來源各自成為一個指標）。
//   - 多實例（**最新 N 筆**，slot 0 = 最新）：register 時預配置 N 個 slot 實例；每次採集把解析
//     出的前 k 筆填入 slot 0..k-1、其餘 slot 標為 unknown。單一 slot 實例：
//       value.number = slot 序位（0 = 最新）；value.valid = 該 slot 本輪是否有項目
//       value.text   = 項目標題（或網頁 `<title>`）
//   - 連結 / 時間等結構化欄位經 `items()` 存取（`FeedItem{title, link, time}`），供進階消費者
//     （E2-01 實例只承載 number + 可選 text，故完整結構由提供者側暴露，比照 E2-14 `last_document()`）。
#ifndef DS_MODULES_E2_26_FEED_FETCH_HPP
#define DS_MODULES_E2_26_FEED_FETCH_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "http_fetch.hpp"  // E2-14 契約（上游，可讀不可改）：HttpTransport / HttpResponse
#include "metric.hpp"      // E2-01 契約（上游，可讀不可改）：MetricProvider / MetricRegistry

namespace ds::sysinfo {

// ===========================================================================
// 結構化解析：RSS / Atom / HTML → FeedDocument
// ===========================================================================

// 來源型別（由 root 元素 / 內容判定）。
enum class FeedType {
    Unknown,  // 尚未判定 / 解析失敗
    Rss,      // RSS 2.0（<rss> / <rdf:RDF>，項目為 <item>）
    Atom,     // Atom（<feed>，項目為 <entry>）
    Html,     // 一般網頁（僅取 <title>）
};

// 單一 feed 項目（純文字，相位 1 不解析時間為時鐘值——保留原始文字）。
struct FeedItem {
    std::string title;  // 項目標題（已解碼實體、去頭尾空白）
    std::string link;   // 項目連結（RSS: <link> 文字；Atom: <link href="...">）
    std::string time;   // 原始發布 / 更新時間文字（pubDate / published / updated），可空
};

// 解析後的結構化文件。
struct FeedDocument {
    FeedType type = FeedType::Unknown;
    std::string title;             // feed / channel 標題，或網頁 <title>
    std::vector<FeedItem> items;   // 項目清單（RSS <item> / Atom <entry>）；HTML 為空
};

// 解析錯誤 —— 不得靜默失敗（NFR-04 精神）。
struct FeedError {
    std::string message;  // 人類可讀原因（malformed 標記 / 無法辨識內容）。
};

// 解析結果：成功持有 FeedDocument，失敗持有 FeedError。二者互斥。
class FeedParseResult {
public:
    static FeedParseResult success(FeedDocument doc);
    static FeedParseResult failure(FeedError err);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    const FeedDocument& document() const { return doc_; }  // 僅 ok() 為 true 時有效。
    const FeedError& error() const { return error_; }      // 僅 ok() 為 false 時有效。

private:
    FeedParseResult() = default;
    bool ok_ = false;
    FeedDocument doc_;
    FeedError error_;
};

// 解析一段 RSS / Atom / HTML 標記為 `FeedDocument`。純文字處理：
//   - 掃描標記（含註解 / CDATA / 宣告 / 屬性），抽出 feed / channel 標題與各項目
//     （標題 / 連結 / 時間），解碼常見 XML/HTML 實體（&amp; &lt; &gt; &quot; &apos; &#nn; &#xNN;）。
//   - 型別由 root 元素判定（<rss>/<rdf:RDF> → RSS、<feed> → Atom、<html> → HTML），
//     或由項目元素回推（<item> → RSS、<entry> → Atom）。
//   - **空 feed（無項目）為成功**（0 項目）；**malformed 標記（未結束標籤 / 註解 / CDATA、
//     或完全無法辨識的內容）為失敗**（帶訊息，不靜默）。
FeedParseResult parse_feed(const std::string& markup);

// ===========================================================================
// FeedFetchProvider：把「RSS / Atom feed 或網頁標題」掛成指標的 sysinfo 提供者
// ===========================================================================
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標，該指標帶
// **N 個 slot 實例**（最新 N 筆，slot 0 = 最新）。之後每次採集：經注入的 E2-14 `HttpTransport`
// GET url → 若 2xx 則以 `parse_feed()` 解析 body → 取最新 N 筆項目填入 slot（value.text = 標題、
// value.number = slot 序位）、多出的 slot 標為 unknown。連結 / 時間經 `items()` 暴露。消費者
// （掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
//
// 保守設計：transport 為 null、未 register、非 2xx、解析失敗時 sample() 回 false 並把所有 slot
// 標為 unknown（不崩）。空 feed（解析成功但 0 項目）回 true、item_count()==0（與 malformed 區分）。
class FeedFetchProvider : public ds::metrics::MetricProvider {
public:
    // 提供者穩定識別碼前綴（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderPrefix = "sysinfo.feed";
    // 預設暴露的最新項目數（slot 數）。
    static constexpr std::size_t kDefaultMaxItems = 8;

    // 建構一個 RSS / 標題抓取提供者。
    //   transport  注入的 HTTP 傳輸（沿用 E2-14；相位 1 為 NullHttpTransport）。nullptr 時
    //              sample() 保守視為「無後端」（不崩、所有 slot 保持 unknown）。
    //   metric_id / metric_name  指標身分（沿用 E2-01；供多來源各成一個指標）。
    //   url        要 GET 的 feed / 網頁端點。
    //   max_items  暴露的最新項目數（slot 數，≥ 1）；解析出更多項目時只取最新 N 筆。
    FeedFetchProvider(std::shared_ptr<HttpTransport> transport,
                      ds::metrics::MetricId metric_id, std::string metric_name,
                      std::string url, std::size_t max_items = kDefaultMaxItems);

    std::string provider_id() const override { return provider_id_; }

    // 對註冊表掛上指標（N 個 slot 實例，初始皆 unknown，表「尚未採集」）。
    // 掛上後由 sample() 驅動更新。重複 id 由註冊表保守拒絕（掛上失敗則不保留參照）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 採集一次：GET url →（2xx）`parse_feed()` → 取最新 N 筆填入 slot、其餘 unknown。
    // 回傳本次是否**成功抓取並解析**（true 亦含「空 feed」情形，此時 item_count()==0）。
    // 下列情形回 false 且把所有 slot 設為 unknown：未 register、transport 為 null、非 2xx、
    // 解析失敗（malformed）。診斷可經 last_*() 讀取。
    bool sample();

    // --- 診斷（反映最後一次 sample()）---
    int last_status() const noexcept { return last_status_; }            // 最後回應狀態碼（未採集為 0）。
    bool last_parse_ok() const noexcept { return last_parse_ok_; }       // 最後 body 是否成功解析。
    const FeedError& last_error() const noexcept { return last_error_; } // 最後解析錯誤（僅解析失敗時有意義）。
    const FeedDocument& last_document() const noexcept { return last_doc_; }  // 最後成功解析的文件（供查 feed 標題 / 型別）。

    // 本輪填入的項目（最新 N 筆，slot 0 = 最新；含 title / link / time）。供進階消費者取連結 / 時間。
    const std::vector<FeedItem>& items() const noexcept { return items_; }
    // 本輪填入的項目數（≤ max_items）。空 feed / 未採集為 0。
    std::size_t item_count() const noexcept { return items_.size(); }
    // slot 數（= max_items）。
    std::size_t max_items() const noexcept { return max_items_; }

    // 已掛上的指標（未 register 前為 nullptr）。
    std::shared_ptr<ds::metrics::Metric> metric() const { return metric_; }

private:
    // 把所有 slot 標為 unknown（未採集 / 失敗 / 多出的 slot）。
    void clear_slots();

    std::shared_ptr<HttpTransport> transport_;
    ds::metrics::MetricId metric_id_;
    std::string metric_name_;
    std::string url_;
    std::size_t max_items_;
    std::string provider_id_;

    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    // slot 實例弱參照（由 metric_ 持有）。slots_[0] = 最新項目 slot。
    std::vector<ds::metrics::InMemoryMetricInstance*> slots_;

    // 本輪填入的項目（結構化，供 items() 暴露連結 / 時間）。
    std::vector<FeedItem> items_;

    // 診斷狀態
    int last_status_ = 0;
    bool last_parse_ok_ = false;
    FeedError last_error_;
    FeedDocument last_doc_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_26_FEED_FETCH_HPP
