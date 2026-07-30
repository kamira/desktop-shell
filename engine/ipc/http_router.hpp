// E10-03 HTTP 端點 — 平台中立契約（相位 1：可注入請求輸入，無真實 socket / listen）
//
// HTTP 端點：讓外部可經 HTTP 與本應用互動的抽象——路由註冊（method + path → handler）、
// 請求解析、回應建構、以及把請求映射到 E10-01 訊息通道 / E6-01 命令。
//
// 相位 1 刻意「平台中立」：**不開任何真實 socket、不 listen、不綁 port**，也沒有任何
// `#ifdef` / win32 / cocoa / libmicrohttpd 之類的傳輸後端。路由與分派邏輯以**可注入的
// 請求輸入**表達——外部把一個 `HttpRequest` 值餵入 `HttpRouter::handle(request) -> HttpResponse`，
// 即完成匹配、參數擷取、分派與回應建構。等真實網路綁定（相位 2）到位時，只需在 socket
// 接受迴圈把位元組解析成 `HttpRequest`、再把 `HttpResponse` 序列化回位元組——路由語意不動。
//
// 本單元屬 engine 層（平台中立純邏輯）：
//   - 無 `#ifdef` / `_WIN32` / `cocoa` 等平台分支，不綁任何真實傳輸。
//   - 請求 / 回應酬載重用 E6-01 的穩定值型別（`ds::command::CommandArgs`），跨模組邊界不變形；
//     故 HTTP 端點可直接把請求映射為 E6-01 具名命令、或封成 E10-01 `Message` 投遞。
//   - 命名空間 `ds::ipc::http`（巢狀於 E10-01 的 `ds::ipc`，一致且不與其符號相撞）。
//   - 404 / 405 / 命令失敗一律回**明確**回應，不靜默、不崩潰。
// 因此可完全以單元測試驗證：路由匹配、路徑參數、請求解析、回應建構、404 / 405、映射到通道 / 命令。
#ifndef DS_IPC_E10_03_HTTP_ROUTER_HPP
#define DS_IPC_E10_03_HTTP_ROUTER_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"       // E6-01：重用穩定值型別 / 具名命令（PUBLIC 相依）
#include "message_channel.hpp"   // E10-01：本機 IPC 訊息通道（PUBLIC 相依）

namespace ds::ipc::http {

// 契約版本標記。HTTP 端點為承重抽象（相位 2 換上真實傳輸時，消費者可據此做相容判斷）。定義在 .cpp。
const char* contract_version() noexcept;

// ---------------------------------------------------------------------------
// HttpMethod — HTTP 方法。具名列舉；不使用裸字串比較以避免大小寫 / 拼寫歧義。
// 提供與字串互轉，供相位 2 從請求列解析、或呼叫端以字串註冊。
// ---------------------------------------------------------------------------
enum class HttpMethod { Get, Post, Put, Delete, Patch, Head, Options, Unknown };

inline const char* to_string(HttpMethod m) noexcept {
    switch (m) {
        case HttpMethod::Get:     return "GET";
        case HttpMethod::Post:    return "POST";
        case HttpMethod::Put:     return "PUT";
        case HttpMethod::Delete:  return "DELETE";
        case HttpMethod::Patch:   return "PATCH";
        case HttpMethod::Head:    return "HEAD";
        case HttpMethod::Options: return "OPTIONS";
        case HttpMethod::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

// 從字串解析方法（大小寫不敏感）；無法辨識回 HttpMethod::Unknown（不崩潰）。
inline HttpMethod method_from_string(const std::string& s) {
    std::string up;
    up.reserve(s.size());
    for (char c : s) {
        up.push_back(static_cast<char>((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c));
    }
    if (up == "GET")     return HttpMethod::Get;
    if (up == "POST")    return HttpMethod::Post;
    if (up == "PUT")     return HttpMethod::Put;
    if (up == "DELETE")  return HttpMethod::Delete;
    if (up == "PATCH")   return HttpMethod::Patch;
    if (up == "HEAD")    return HttpMethod::Head;
    if (up == "OPTIONS") return HttpMethod::Options;
    return HttpMethod::Unknown;
}

// ---------------------------------------------------------------------------
// HTTP 狀態碼 — 具名常數（避免核心 API 散落裸數字）。相位 1 只需這幾個承重碼。
// ---------------------------------------------------------------------------
namespace status {
constexpr int Ok                  = 200;
constexpr int NoContent           = 204;
constexpr int BadRequest          = 400;
constexpr int NotFound            = 404;
constexpr int MethodNotAllowed    = 405;
constexpr int InternalServerError = 500;
}  // namespace status

// ---------------------------------------------------------------------------
// HttpRequest — 一則進站請求：方法 + 路徑 + 查詢 + 路徑參數 + 標頭 + 主體。
//
// 相位 1「可注入的請求輸入」：由測試 / 相位 2 的傳輸層建構後餵入 router。查詢與路徑參數
// 皆以 E6-01 `CommandArgs`（穩定值型別具名字典）承載，跨邊界不變形、可直接轉命令參數。
// ---------------------------------------------------------------------------
struct HttpRequest {
    HttpMethod method = HttpMethod::Get;
    std::string path;                       // 資源路徑（不含查詢字串），如 "/widgets/42"
    ds::command::CommandArgs query;         // 查詢字串參數（?a=1&b=2）
    ds::command::CommandArgs params;        // 路徑參數（由 router 於匹配時填入，如 :id → "42"）
    ds::command::CommandArgs headers;       // 請求標頭（具名字典）
    std::string body;                       // 原始主體（相位 2 可為 JSON 等）

    HttpRequest() = default;
    HttpRequest(HttpMethod m, std::string p) : method(m), path(std::move(p)) {}

    // 請求解析：把「請求目標」（可含 ?查詢）解析成 method + path + query。
    // 例：parse(Get, "/widgets/42?verbose=1&tag=a") → path="/widgets/42"，query{verbose:"1",tag:"a"}。
    // 不做百分號解碼（相位 1 純邏輯；相位 2 傳輸層負責 wire 解碼），空值鍵以空字串記。
    static HttpRequest parse(HttpMethod m, const std::string& target, std::string body = {}) {
        HttpRequest req;
        req.method = m;
        req.body = std::move(body);
        const std::size_t q = target.find('?');
        if (q == std::string::npos) {
            req.path = target;
            return req;
        }
        req.path = target.substr(0, q);
        req.query = parse_query(target.substr(q + 1));
        return req;
    }

    // 解析查詢字串（"a=1&b=2&flag"）為具名字典；缺 '=' 的鍵值記為空字串。全部以字串型別存放。
    static ds::command::CommandArgs parse_query(const std::string& qs) {
        ds::command::CommandArgs args;
        std::size_t i = 0;
        while (i < qs.size()) {
            std::size_t amp = qs.find('&', i);
            if (amp == std::string::npos) amp = qs.size();
            const std::string pair = qs.substr(i, amp - i);
            if (!pair.empty()) {
                const std::size_t eq = pair.find('=');
                if (eq == std::string::npos) {
                    args.set(pair, std::string{});
                } else {
                    args.set(pair.substr(0, eq), pair.substr(eq + 1));
                }
            }
            i = amp + 1;
        }
        return args;
    }
};

// ---------------------------------------------------------------------------
// HttpResponse — 回應：狀態碼 + 標頭 + 主體。提供意圖清楚的工廠，回應建構不散落裸值。
// ---------------------------------------------------------------------------
struct HttpResponse {
    int status = status::Ok;
    ds::command::CommandArgs headers;
    std::string body;

    HttpResponse() = default;
    explicit HttpResponse(int s, std::string b = {}) : status(s), body(std::move(b)) {}

    bool ok() const noexcept { return status >= 200 && status < 300; }

    HttpResponse& set_header(std::string key, std::string value) {
        headers.set(std::move(key), std::move(value));
        return *this;
    }

    // ---- 工廠 ----
    static HttpResponse make_ok(std::string body = {}) {
        return HttpResponse{status::Ok, std::move(body)};
    }
    static HttpResponse make_no_content() { return HttpResponse{status::NoContent}; }
    static HttpResponse make_bad_request(std::string body = "bad request") {
        return HttpResponse{status::BadRequest, std::move(body)};
    }
    static HttpResponse make_not_found(const std::string& path = {}) {
        return HttpResponse{status::NotFound, path.empty() ? "not found" : ("not found: " + path)};
    }
    // 405 依 HTTP 規範必須帶 Allow 標頭列出該資源允許的方法（明確、不靜默）。
    static HttpResponse make_method_not_allowed(const std::string& allow) {
        HttpResponse r{status::MethodNotAllowed, "method not allowed"};
        r.set_header("Allow", allow);
        return r;
    }
    static HttpResponse make_error(std::string body = "internal server error") {
        return HttpResponse{status::InternalServerError, std::move(body)};
    }
};

// 路由處理器：接收（已填好路徑參數的）請求，回傳回應。
using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

// ---------------------------------------------------------------------------
// HttpRouter — 路由表與分派器。
//
// 路由以「method + 路徑樣式」註冊。路徑樣式以 '/' 分段，`:name` 段為路徑參數（擷取單段）。
// 契約保證：
//   - route(method, pattern, handler)：pattern 非空、handler 非空、且該 (method, pattern)
//     尚未註冊時成功回 true；否則不變更狀態回 false（**不靜默覆蓋**已註冊者）。
//   - handle(request)：
//       * 有樣式在路徑與方法皆相符 → 填入路徑參數、呼叫其 handler、回傳其回應。
//       * 路徑相符但方法不符 → 405，並帶 Allow 標頭列出該路徑允許的方法（明確）。
//       * 無任何樣式路徑相符 → 404（明確）。
//     全程不崩潰、不丟例外。
// ---------------------------------------------------------------------------
class HttpRouter {
public:
    HttpRouter() = default;

    // 註冊一條路由。回傳是否成功（見上契約；重複 (method,pattern) 不覆蓋）。
    bool route(HttpMethod method, std::string pattern, HttpHandler handler) {
        if (pattern.empty() || !handler || method == HttpMethod::Unknown) return false;
        for (const auto& r : routes_) {
            if (r.method == method && r.pattern == pattern) return false;  // 不覆蓋
        }
        routes_.push_back(Route{method, std::move(pattern), std::move(handler)});
        return true;
    }

    // 便捷多載。
    bool get(std::string pattern, HttpHandler h)  { return route(HttpMethod::Get,    std::move(pattern), std::move(h)); }
    bool post(std::string pattern, HttpHandler h) { return route(HttpMethod::Post,   std::move(pattern), std::move(h)); }
    bool put(std::string pattern, HttpHandler h)  { return route(HttpMethod::Put,    std::move(pattern), std::move(h)); }
    bool del(std::string pattern, HttpHandler h)  { return route(HttpMethod::Delete, std::move(pattern), std::move(h)); }

    // 分派一則請求。永遠回一個明確回應（含 404 / 405）。
    HttpResponse handle(const HttpRequest& request) const {
        bool path_matched = false;
        for (const auto& r : routes_) {
            ds::command::CommandArgs params;  // 每條路由用新字典，match 於相符時填入
            if (match(r.pattern, request.path, params)) {
                path_matched = true;
                if (r.method == request.method) {
                    HttpRequest enriched = request;       // 複製並補上路徑參數
                    enriched.params = std::move(params);
                    return r.handler(enriched);
                }
            }
        }
        if (path_matched) {
            return HttpResponse::make_method_not_allowed(allowed_methods(request.path));
        }
        return HttpResponse::make_not_found(request.path);
    }

    // 是否有任一樣式的路徑相符（不論方法）。供內省。
    bool has_path(const std::string& path) const {
        for (const auto& r : routes_) {
            ds::command::CommandArgs sink;
            if (match(r.pattern, path, sink)) return true;
        }
        return false;
    }

    std::size_t route_count() const noexcept { return routes_.size(); }

    // 逗號分隔列出某路徑允許的方法（依註冊序、不重複）。供 405 Allow 標頭 / 內省。
    std::string allowed_methods(const std::string& path) const {
        std::string out;
        for (const auto& r : routes_) {
            ds::command::CommandArgs sink;
            if (!match(r.pattern, path, sink)) continue;
            const char* m = to_string(r.method);
            if (out.find(m) != std::string::npos) continue;  // 去重
            if (!out.empty()) out += ", ";
            out += m;
        }
        return out;
    }

private:
    struct Route {
        HttpMethod method;
        std::string pattern;
        HttpHandler handler;
    };

    // 以 '/' 切段（保留空段以精確比對段數；忽略最前導 '/' 造成的首個空段一致性由兩邊同規則保證）。
    static std::vector<std::string> split(const std::string& path) {
        std::vector<std::string> segs;
        std::size_t i = 0;
        while (i <= path.size()) {
            std::size_t slash = path.find('/', i);
            if (slash == std::string::npos) slash = path.size();
            segs.push_back(path.substr(i, slash - i));
            i = slash + 1;
        }
        return segs;
    }

    // 樣式與路徑比對：段數相等，字面段須相等，`:name` 段擷取為 params_out[name]。
    // 僅在完全相符（回 true）時，params_out 才含擷取到的路徑參數；呼叫端每次傳入新字典。
    static bool match(const std::string& pattern, const std::string& path,
                      ds::command::CommandArgs& params_out) {
        const std::vector<std::string> ps = split(pattern);
        const std::vector<std::string> xs = split(path);
        if (ps.size() != xs.size()) return false;
        ds::command::CommandArgs captured;
        for (std::size_t i = 0; i < ps.size(); ++i) {
            const std::string& seg = ps[i];
            if (!seg.empty() && seg[0] == ':') {
                captured.set(seg.substr(1), xs[i]);  // 路徑參數擷取
            } else if (seg != xs[i]) {
                return false;
            }
        }
        params_out = std::move(captured);
        return true;
    }

    std::vector<Route> routes_;
};

// ---------------------------------------------------------------------------
// 映射到 E6-01 命令 / E10-01 訊息通道
//
// HTTP 端點的核心價值之一：把進站請求轉為既有的命令 / 訊息，重用命令匯流排與本機 IPC，
// 而非在 HTTP 層另立一套副作用。路徑參數（`req.params`）為最自然的命令參數來源；需要
// 查詢 / 主體者可自行組 `CommandArgs`。以下為薄轉接，均為純邏輯、平台中立。
// ---------------------------------------------------------------------------

// 把請求轉為 E6-01 具名命令（id + 路徑參數）。呼叫端可再交 CommandBus 分派。
inline ds::command::Command to_command(const HttpRequest& req, ds::command::CommandId id) {
    return ds::command::Command{std::move(id), req.params};
}

// 把請求封成 E10-01 `Message`（型別 + 路徑參數酬載），可 send / publish 到 MessageChannel。
inline ds::ipc::Message to_message(const HttpRequest& req, ds::ipc::MessageType type) {
    return ds::ipc::Message{std::move(type), req.params};
}

// 把 E6-01 分派結果映射為 HTTP 回應（明確、不靜默）：
//   Ok → 200（主體取回傳字串值，否則取訊息）；NotFound → 404；Failed → 400（帶失敗訊息）。
inline HttpResponse map_command_result(const ds::command::CommandResult& r) {
    switch (r.status) {
        case ds::command::CommandStatus::Ok: {
            HttpResponse resp = HttpResponse::make_ok();
            if (auto s = r.value.as_string()) resp.body = *s;
            else if (!r.message.empty()) resp.body = r.message;
            return resp;
        }
        case ds::command::CommandStatus::NotFound:
            return HttpResponse::make_not_found();
        case ds::command::CommandStatus::Failed:
            return HttpResponse::make_bad_request(r.message.empty() ? "command failed" : r.message);
    }
    return HttpResponse::make_error();
}

// 直接把請求分派到 CommandBus 上的具名命令，並回映射後的 HTTP 回應。
inline HttpResponse dispatch_request(const ds::command::CommandBus& bus,
                                     const ds::command::CommandId& id,
                                     const HttpRequest& req) {
    return map_command_result(bus.dispatch(id, req.params));
}

// 產生一個「把請求分派到 CommandBus 具名命令」的路由處理器，可直接 route(...) 掛上。
// bus 以參考捕獲（須比 router 長壽）；id 以值捕獲。
inline HttpHandler command_dispatcher(const ds::command::CommandBus& bus,
                                      ds::command::CommandId id) {
    return [&bus, id](const HttpRequest& req) -> HttpResponse {
        return dispatch_request(bus, id, req);
    };
}

}  // namespace ds::ipc::http

#endif  // DS_IPC_E10_03_HTTP_ROUTER_HPP
