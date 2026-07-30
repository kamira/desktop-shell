// E2-14 HTTP 取得與結構化解析 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「一個 HTTP 端點回傳的結構化資料（JSON / 類結構文字）中的某個數值欄位」
// 透過 **E2-01 的 MetricProvider 介面**掛成一個指標，並以 **E2-02 的採集頻率分級**決定
// 「多久重新抓取一次」的節奏。這是「新增指標 = 新增 `MetricProvider`、掛件一行不動」機制
// 的一個具體提供者——它**消費 E2-01 / E2-02 契約、不自造指標模型、不自造排程**。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 傳輸 + 純解析邏輯**：**不真的發網路請求**。真實網路後端
//     （libcurl / socket / 平台 HTTP 堆疊）留待後端相位；本檔一律不含。
//   - 無 `#ifdef`、無 socket、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//   - **HTTP 取得抽象化為可注入的 `HttpTransport`**：提供者只依賴此抽象介面，故換後端
//     （相位 2+ 的真實傳輸）時提供者一行不動；相位 1 只有 `NullHttpTransport`（回注入的
//     固定回應或「無後端」空回應）。
//   - **結構化解析為純邏輯**：自帶輕量 JSON 解析器把回應 body 轉成可查詢的 `JsonValue` 樹
//     （不消費 E7-01 的縮排式宣告格式——那要求 format_version 且非 JSON；本單元面對的是
//     任意 HTTP 端點的 JSON，故自帶最小 JSON 解析較合適，且不引入 depends_on 外的相依）。
//     解析失敗**不靜默吞掉**（NFR-04 精神）：回傳帶位置 + 訊息的錯誤。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id / name / unit / range 由建構時指定（供多個 HTTP 端點各自成為一個指標）
//   - 單一實例（單一端點為單值來源）：
//       value.number = 從解析結果依 `value_path` 取出的數值欄位
//       value.valid  = 是否本輪成功取得數值（非 2xx / 解析失敗 / 路徑不存在或非數值 →
//                      unknown，不把 0 誤當真實讀值）
//       history      = 該數值的環狀歷史（供折線 / sparkline 呈現）
#ifndef DS_MODULES_E2_14_HTTP_FETCH_HPP
#define DS_MODULES_E2_14_HTTP_FETCH_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ===========================================================================
// HTTP 取得抽象（可注入傳輸 + null 後端）
// ===========================================================================

// HTTP 回應：狀態碼 + 標頭 + body（平台中立最小形）。
// `status == 0` 慣例表示「無後端 / 未發出請求」（相位 1 null 傳輸的誠實預設），
// 消費者據此視為「無讀值」，而非把它當成某個真實 HTTP 狀態。
struct HttpResponse {
    // (name, value) 有序標頭清單；名稱比對大小寫不敏感（HTTP 慣例）。
    using Header = std::pair<std::string, std::string>;

    int status = 0;                  // HTTP 狀態碼；0 = 無後端 / 未請求
    std::vector<Header> headers;     // 有序標頭
    std::string body;                // 回應內容（結構化解析的輸入）

    // 2xx 視為成功（200..299）。
    bool is_success() const noexcept { return status >= 200 && status < 300; }

    // 依名稱查標頭（大小寫不敏感）；不存在回 nullptr。回傳指標於本物件存活期間有效。
    const std::string* header(const std::string& name) const;

    // --- 工廠（供 null 傳輸注入 / 測試）---
    // 2xx 回應（預設 200）帶 body。
    static HttpResponse ok(std::string body, int status = 200);
    // 任意狀態 + body。
    static HttpResponse of(int status, std::string body);
    // 「無後端 / 未請求」的空回應（status==0）。
    static HttpResponse none() { return HttpResponse{}; }
};

// ---------------------------------------------------------------------------
// HttpTransport：發出 GET 請求的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台 / 網路後端（相位 2+）實作它以真的發 HTTP GET；相位 1 只有 null 傳輸。
// 提供者只依賴此抽象介面，故換後端時提供者一行不動。
class HttpTransport {
public:
    virtual ~HttpTransport() = default;

    // 對 url 發一個 GET，回傳回應。決定性後端（如 null 傳輸）同一狀態多次呼叫結果一致。
    virtual HttpResponse get(const std::string& url) const = 0;

protected:
    HttpTransport() = default;
    HttpTransport(const HttpTransport&) = default;
    HttpTransport& operator=(const HttpTransport&) = default;
};

// ---------------------------------------------------------------------------
// NullHttpTransport：相位 1 的 null 傳輸
// ---------------------------------------------------------------------------
// **不發任何真實網路請求**。可對特定 url 注入固定回應供測試 / 假感測器；未注入的 url
// 回傳「無後端」空回應（status==0）——Mac / null 期的誠實預設。真實傳輸留待後端相位，
// 本類永不含 socket / 網路呼叫。
//
// 計數用途（診斷 / 測試）：記錄請求次數與最後請求的 url（get() 為 const，故以 mutable
// 記錄，不影響邏輯決定性）。
class NullHttpTransport : public HttpTransport {
public:
    NullHttpTransport() = default;

    // 對特定 url 注入 / 覆寫固定回應。
    void set_response(const std::string& url, HttpResponse resp) {
        responses_[url] = std::move(resp);
    }
    // 設定未注入 url 的預設回應（預設為「無後端」空回應）。
    void set_default(HttpResponse resp) { default_ = std::move(resp); }
    // 清空所有注入（回到全「無後端」狀態）。
    void clear() {
        responses_.clear();
        default_ = HttpResponse::none();
    }

    // 已發出的請求次數。
    std::size_t request_count() const noexcept { return request_count_; }
    // 最後一次請求的 url（未曾請求為空）。
    const std::string& last_url() const noexcept { return last_url_; }

    // 回注入的固定回應；未注入該 url 則回預設（無後端）。不發任何真實請求。
    HttpResponse get(const std::string& url) const override;

private:
    std::unordered_map<std::string, HttpResponse> responses_;
    HttpResponse default_ = HttpResponse::none();
    mutable std::size_t request_count_ = 0;
    mutable std::string last_url_;
};

// ===========================================================================
// 結構化解析：輕量 JSON → 可查詢的 JsonValue 樹
// ===========================================================================

enum class JsonType { Null, Bool, Number, String, Array, Object };

// 結構化資料的多型節點：純量以值語意持有；容器（Array / Object）遞迴持有子節點。
// Object 保留插入順序且以有序成員清單持有。錯誤型別存取（如對 Bool 呼叫 as_string）
// 為呼叫端契約違反，會 throw std::runtime_error——明確失敗而非回可疑預設值（NFR-04 精神）。
// 存取前請先以 is_*() 查詢型別。
class JsonValue {
public:
    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;  // 預設為 Null。

    // --- 工廠 ---
    static JsonValue null_value();
    static JsonValue boolean(bool b);
    static JsonValue number(double v, bool integral = false);
    static JsonValue integer(std::int64_t v);
    static JsonValue string(std::string s);
    static JsonValue array(std::vector<JsonValue> items);
    static JsonValue object(std::vector<Member> members);

    // --- 型別查詢 ---
    JsonType type() const noexcept { return type_; }
    bool is_null() const noexcept { return type_ == JsonType::Null; }
    bool is_bool() const noexcept { return type_ == JsonType::Bool; }
    bool is_number() const noexcept { return type_ == JsonType::Number; }
    bool is_string() const noexcept { return type_ == JsonType::String; }
    bool is_array() const noexcept { return type_ == JsonType::Array; }
    bool is_object() const noexcept { return type_ == JsonType::Object; }
    // 該 Number 是否來自整數字面值（無小數點 / 指數）。非 Number 恆為 false。
    bool is_integer() const noexcept { return type_ == JsonType::Number && integral_; }

    // --- 純量存取（型別不符 → throw std::runtime_error）---
    bool as_bool() const;
    double as_number() const;
    std::int64_t as_int() const;  // Number 以整數截斷取出。
    const std::string& as_string() const;

    // --- 容器存取（型別不符 → throw）---
    const std::vector<JsonValue>& as_array() const;
    const std::vector<Member>& as_object() const;

    // Array / Object 的元素數（其他型別 → throw）。
    std::size_t size() const;

    // --- Object 便捷查詢（型別須為 Object）---
    bool contains(const std::string& key) const;
    const JsonValue* find(const std::string& key) const;  // 不存在回 nullptr。
    const JsonValue& at(const std::string& key) const;     // 不存在 → throw。
    std::vector<std::string> keys() const;

    // 深層相等（型別 + 內容遞迴比較；Object 比較保序）。便於測試。
    bool operator==(const JsonValue& o) const;
    bool operator!=(const JsonValue& o) const { return !(*this == o); }

private:
    JsonType type_ = JsonType::Null;
    bool bool_ = false;
    double num_ = 0.0;
    bool integral_ = false;
    std::string str_;
    std::vector<JsonValue> arr_;
    std::vector<Member> obj_;
};

// ---------------------------------------------------------------------------
// JsonPath：從結構化樹取值的路徑（欄位鍵 / 陣列索引段）
// ---------------------------------------------------------------------------
// 一段路徑要嘛是「物件欄位鍵」要嘛是「陣列索引」，避免以裸字串表達時鍵與索引的歧義。
struct PathSeg {
    bool is_index = false;
    std::string key;         // is_index==false 時有效
    std::size_t index = 0;   // is_index==true 時有效

    static PathSeg field(std::string k) { return PathSeg{false, std::move(k), 0}; }
    static PathSeg elem(std::size_t i) { return PathSeg{true, {}, i}; }
};
using JsonPath = std::vector<PathSeg>;

// 依路徑走訪一棵 JsonValue 樹；任一段不匹配（鍵不存在 / 索引越界 / 型別不符）回 nullptr。
// 空路徑回 &root 本身。回傳指標於 root 存活期間有效。
const JsonValue* seek(const JsonValue& root, const JsonPath& path);

// ---------------------------------------------------------------------------
// JSON 解析：文字 → JsonValue（錯誤可定位、不靜默）
// ---------------------------------------------------------------------------

// 解析錯誤 —— 一律可定位到來源位置（不得靜默失敗）。
struct JsonError {
    std::size_t offset = 0;   // 0-based 字元位移（錯誤處）。
    std::string message;      // 人類可讀原因。
};

// 解析結果：成功持有 JsonValue，失敗持有 JsonError。二者互斥。
class JsonParseResult {
public:
    static JsonParseResult success(JsonValue v);
    static JsonParseResult failure(JsonError e);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅在 ok() 為 true 時有效。
    const JsonValue& value() const { return value_; }

    // 僅在 ok() 為 false 時有效。
    const JsonError& error() const { return error_; }

private:
    JsonParseResult() = default;
    bool ok_ = false;
    JsonValue value_;
    JsonError error_;
};

// 解析一段 JSON 文字為 JsonValue 樹。支援 object / array / string（含 \\ \" \/ \b \f \n
// \r \t \uXXXX 轉義）/ number（整數與浮點，帶 is_integer 旗標）/ true / false / null，
// 以及其間的空白。尾隨非空白內容視為錯誤。任一不符 → failure(帶位移 + 訊息)。
JsonParseResult parse_json(const std::string& text);

// ===========================================================================
// HttpFetchProvider：把「HTTP 端點的某數值欄位」掛成指標的 sysinfo 提供者
// ===========================================================================
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標。之後每次
// 採集迴圈到期，呼叫 sample()：經注入的 `HttpTransport` GET url → 若 2xx 則解析 body 為
// JsonValue 樹 → 依 value_path 取出數值欄位 → 更新該實例的 value 與歷史。消費者（掛件）
// 只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別。
//
// 與 E2-02 的關係：HTTP 取得涉及網路、成本高且變動相對慢，建議採集分級為 SamplingTier::Low
// （不高頻重抓，呼應 NFR-01 idle 門檻）。register_demand() 便利地把本指標以 Low 級登記進
// E2-02 排程器（除頻合併沿用 E2-02，不自造排程）。
class HttpFetchProvider : public ds::metrics::MetricProvider {
public:
    // 提供者穩定識別碼前綴（供診斷 / 去重 / 溯源）。實際 provider_id 附上指標 id 以區辨
    // 多個 HTTP 端點提供者。
    static constexpr const char* kProviderPrefix = "sysinfo.http";
    // 建議採集分級（E2-02）：網路取得成本高、變動慢，歸低頻。
    static constexpr ds::metrics::SamplingTier kSuggestedTier =
        ds::metrics::SamplingTier::Low;
    // 數值歷史環的預設容量。
    static constexpr std::size_t kDefaultHistoryCapacity = 32;

    // 建構一個 HTTP 取得提供者。
    //   transport       注入的 HTTP 傳輸（相位 1 為 NullHttpTransport）。nullptr 時 sample()
    //                   保守視為「無後端」（不崩、value 保持 unknown）。
    //   metric_id/name  指標身分（沿用 E2-01；供多端點各成一個指標）。
    //   url             要 GET 的端點。
    //   value_path      從解析後 JSON 樹取數值欄位的路徑（空路徑 = 根節點本身須為數值）。
    //   unit/range      指標單位與值域（沿用 E2-01；預設無單位、無界）。
    //   history_capacity 數值歷史環容量。
    HttpFetchProvider(std::shared_ptr<HttpTransport> transport,
                      ds::metrics::MetricId metric_id, std::string metric_name,
                      std::string url, JsonPath value_path,
                      std::string unit = "",
                      ds::metrics::MetricRange range = ds::metrics::MetricRange::unbounded(),
                      std::size_t history_capacity = kDefaultHistoryCapacity);

    std::string provider_id() const override { return provider_id_; }

    // 對註冊表掛上指標（單一實例、初始 value 為 unknown，表「尚未採集」）。
    // 掛上後由 sample() 驅動更新。重複 id 由註冊表保守拒絕（掛上失敗則不保留參照）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 採集一次：GET url → （2xx）解析 body → 依 value_path 取數值 → 更新實例 value 與歷史。
    // 回傳本次是否成功取得**有效數值**（true = value 已更新為 valid 數值）。
    // 下列情形回 false 且把 value 設為 unknown（不污染歷史）：未 register、transport 為 null、
    // 非 2xx、解析失敗、路徑不存在或非數值。診斷可經 last_*() 讀取。
    bool sample();

    // 便利：以 E2-02 排程器把本指標以建議分級（Low）登記為一筆需求，回傳票根。
    // 除頻合併與到期判定沿用 E2-02，本提供者不自造排程。
    ds::metrics::DemandId register_demand(ds::metrics::SamplingScheduler& scheduler) const {
        return scheduler.add_demand(metric_id_, kSuggestedTier);
    }

    // --- 診斷（反映最後一次 sample()）---
    // 最後一次回應狀態碼（未採集為 0）。
    int last_status() const noexcept { return last_status_; }
    // 最後一次 body 是否成功解析為 JSON（非 2xx 或未採集為 false）。
    bool last_parse_ok() const noexcept { return last_parse_ok_; }
    // 最後一次解析錯誤（僅在 last_parse_ok()==false 且曾嘗試解析時有意義）。
    const JsonError& last_error() const noexcept { return last_error_; }
    // 最後一次成功解析的結構化樹（未成功解析為 Null）。供進階消費者查詢其他欄位。
    const JsonValue& last_document() const noexcept { return last_doc_; }

    // 已掛上的指標（未 register 前為 nullptr）。
    std::shared_ptr<ds::metrics::Metric> metric() const { return metric_; }

private:
    std::shared_ptr<HttpTransport> transport_;
    ds::metrics::MetricId metric_id_;
    std::string metric_name_;
    std::string url_;
    JsonPath value_path_;
    std::string unit_;
    ds::metrics::MetricRange range_;
    std::size_t history_capacity_;
    std::string provider_id_;

    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* instance_ = nullptr;  // 由 metric_ 持有，僅弱參照

    // 診斷狀態
    int last_status_ = 0;
    bool last_parse_ok_ = false;
    JsonError last_error_;
    JsonValue last_doc_;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_14_HTTP_FETCH_HPP
