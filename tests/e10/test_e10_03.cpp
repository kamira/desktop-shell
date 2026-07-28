// E10-03 HTTP 端點 — gtest 契約測試。
//
// 覆蓋：路由匹配（method + path）、路徑參數擷取、請求解析（path/query）、回應建構與工廠、
// 404（無相符路徑）/ 405（路徑相符但方法不符，帶 Allow 標頭）、重複註冊不覆蓋、
// 以及把請求映射到 E6-01 命令與 E10-01 訊息通道、契約版本標記。
#include "http_router.hpp"

#include <string>

#include <gtest/gtest.h>

using ds::ipc::http::HttpMethod;
using ds::ipc::http::HttpRequest;
using ds::ipc::http::HttpResponse;
using ds::ipc::http::HttpRouter;
namespace status = ds::ipc::http::status;

using ds::command::CommandArgs;
using ds::command::CommandBus;
using ds::command::CommandResult;
using ds::command::CommandValue;

// ---------------------------------------------------------------------------
// 方法字串互轉
// ---------------------------------------------------------------------------
TEST(HttpMethod, ToAndFromString) {
    EXPECT_STREQ(ds::ipc::http::to_string(HttpMethod::Get), "GET");
    EXPECT_STREQ(ds::ipc::http::to_string(HttpMethod::Delete), "DELETE");
    EXPECT_TRUE(ds::ipc::http::method_from_string("get") == HttpMethod::Get);
    EXPECT_TRUE(ds::ipc::http::method_from_string("POST") == HttpMethod::Post);
    EXPECT_TRUE(ds::ipc::http::method_from_string("wat") == HttpMethod::Unknown);
}

// ---------------------------------------------------------------------------
// 請求解析：path / query
// ---------------------------------------------------------------------------
TEST(HttpRequestParse, PathOnlyNoQuery) {
    HttpRequest req = HttpRequest::parse(HttpMethod::Get, "/widgets/42");
    EXPECT_EQ(req.path, "/widgets/42");
    EXPECT_TRUE(req.query.empty());
}

TEST(HttpRequestParse, SplitsPathAndQuery) {
    HttpRequest req = HttpRequest::parse(HttpMethod::Get, "/widgets/42?verbose=1&tag=a");
    EXPECT_EQ(req.path, "/widgets/42");
    EXPECT_EQ(req.query.size(), 2u);
    ASSERT_TRUE(req.query.get_string("verbose").has_value());
    EXPECT_EQ(*req.query.get_string("verbose"), "1");
    EXPECT_EQ(*req.query.get_string("tag"), "a");
}

TEST(HttpRequestParse, QueryKeyWithoutValueIsEmptyString) {
    HttpRequest req = HttpRequest::parse(HttpMethod::Get, "/x?flag&k=v");
    ASSERT_TRUE(req.query.get_string("flag").has_value());
    EXPECT_EQ(*req.query.get_string("flag"), "");
    EXPECT_EQ(*req.query.get_string("k"), "v");
}

TEST(HttpRequestParse, CarriesBody) {
    HttpRequest req = HttpRequest::parse(HttpMethod::Post, "/things", "payload-bytes");
    EXPECT_EQ(req.body, "payload-bytes");
    EXPECT_TRUE(req.method == HttpMethod::Post);
}

// ---------------------------------------------------------------------------
// 回應建構與工廠
// ---------------------------------------------------------------------------
TEST(HttpResponseBuild, Factories) {
    EXPECT_EQ(HttpResponse::make_ok("hi").status, status::Ok);
    EXPECT_EQ(HttpResponse::make_ok("hi").body, "hi");
    EXPECT_TRUE(HttpResponse::make_ok().ok());
    EXPECT_EQ(HttpResponse::make_no_content().status, status::NoContent);
    EXPECT_FALSE(HttpResponse::make_not_found().ok());
    EXPECT_EQ(HttpResponse::make_not_found().status, status::NotFound);
    EXPECT_EQ(HttpResponse::make_bad_request().status, status::BadRequest);
    EXPECT_EQ(HttpResponse::make_error().status, status::InternalServerError);
}

TEST(HttpResponseBuild, SetHeaderChains) {
    HttpResponse r = HttpResponse::make_ok("body");
    r.set_header("X-A", "1").set_header("X-B", "2");
    EXPECT_EQ(r.headers.size(), 2u);
    EXPECT_EQ(*r.headers.get_string("X-A"), "1");
}

// ---------------------------------------------------------------------------
// 路由匹配（method + path）
// ---------------------------------------------------------------------------
TEST(HttpRouterMatch, StaticPathMethodMatch) {
    HttpRouter router;
    EXPECT_TRUE(router.get("/health", [](const HttpRequest&) {
        return HttpResponse::make_ok("ok");
    }));
    EXPECT_EQ(router.route_count(), 1u);

    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Get, "/health"});
    EXPECT_EQ(resp.status, status::Ok);
    EXPECT_EQ(resp.body, "ok");
}

TEST(HttpRouterMatch, MethodDiscriminates) {
    HttpRouter router;
    router.get("/r", [](const HttpRequest&) { return HttpResponse::make_ok("GET"); });
    router.post("/r", [](const HttpRequest&) { return HttpResponse::make_ok("POST"); });

    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/r"}).body, "GET");
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Post, "/r"}).body, "POST");
}

TEST(HttpRouterMatch, RejectsEmptyPatternNullHandlerUnknownMethod) {
    HttpRouter router;
    EXPECT_FALSE(router.get("", [](const HttpRequest&) { return HttpResponse::make_ok(); }));
    EXPECT_FALSE(router.get("/x", nullptr));
    EXPECT_FALSE(router.route(HttpMethod::Unknown, "/x",
                              [](const HttpRequest&) { return HttpResponse::make_ok(); }));
    EXPECT_EQ(router.route_count(), 0u);
}

TEST(HttpRouterMatch, DuplicateRouteNotOverwritten) {
    HttpRouter router;
    EXPECT_TRUE(router.get("/x", [](const HttpRequest&) { return HttpResponse::make_ok("first"); }));
    EXPECT_FALSE(router.get("/x", [](const HttpRequest&) { return HttpResponse::make_ok("second"); }));
    EXPECT_EQ(router.route_count(), 1u);
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/x"}).body, "first");
}

// ---------------------------------------------------------------------------
// 路徑參數擷取
// ---------------------------------------------------------------------------
TEST(HttpRouterParams, CapturesSinglePathParam) {
    HttpRouter router;
    router.get("/widgets/:id", [](const HttpRequest& req) {
        auto id = req.params.get_string("id");
        return HttpResponse::make_ok(id.value_or("<none>"));
    });
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/widgets/42"}).body, "42");
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/widgets/abc"}).body, "abc");
}

TEST(HttpRouterParams, CapturesMultiplePathParams) {
    HttpRouter router;
    router.get("/u/:uid/posts/:pid", [](const HttpRequest& req) {
        return HttpResponse::make_ok(*req.params.get_string("uid") + "/" +
                                     *req.params.get_string("pid"));
    });
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/u/7/posts/99"}).body, "7/99");
}

TEST(HttpRouterParams, SegmentCountMustMatch) {
    HttpRouter router;
    router.get("/widgets/:id", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    // 段數不符（少一段 / 多一段）→ 不相符 → 404
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/widgets"}).status, status::NotFound);
    EXPECT_EQ(router.handle(HttpRequest{HttpMethod::Get, "/widgets/42/extra"}).status, status::NotFound);
}

// ---------------------------------------------------------------------------
// 404 / 405 明確回應
// ---------------------------------------------------------------------------
TEST(HttpRouterErrors, NotFoundWhenNoPathMatches) {
    HttpRouter router;
    router.get("/known", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Get, "/unknown"});
    EXPECT_EQ(resp.status, status::NotFound);
    EXPECT_FALSE(resp.ok());
}

TEST(HttpRouterErrors, MethodNotAllowedWhenPathMatchesButMethodDoesNot) {
    HttpRouter router;
    router.get("/r", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    router.put("/r", [](const HttpRequest&) { return HttpResponse::make_ok(); });

    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Post, "/r"});
    EXPECT_EQ(resp.status, status::MethodNotAllowed);
    // 405 必帶 Allow 標頭，列出該路徑允許的方法
    ASSERT_TRUE(resp.headers.get_string("Allow").has_value());
    const std::string allow = *resp.headers.get_string("Allow");
    EXPECT_TRUE(allow.find("GET") != std::string::npos);
    EXPECT_TRUE(allow.find("PUT") != std::string::npos);
}

TEST(HttpRouterErrors, AllowHeaderMatchesParamPath) {
    HttpRouter router;
    router.get("/widgets/:id", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Delete, "/widgets/5"});
    EXPECT_EQ(resp.status, status::MethodNotAllowed);
    EXPECT_EQ(*resp.headers.get_string("Allow"), "GET");
}

TEST(HttpRouterIntrospect, HasPathAndAllowedMethods) {
    HttpRouter router;
    router.get("/a/:x", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    router.post("/a/:x", [](const HttpRequest&) { return HttpResponse::make_ok(); });
    EXPECT_TRUE(router.has_path("/a/1"));
    EXPECT_FALSE(router.has_path("/a"));
    EXPECT_EQ(router.allowed_methods("/a/1"), "GET, POST");
}

// ---------------------------------------------------------------------------
// 映射到 E6-01 命令
// ---------------------------------------------------------------------------
TEST(HttpMapCommand, ToCommandUsesPathParams) {
    HttpRequest req{HttpMethod::Get, "/widgets/42"};
    req.params.set("id", CommandValue{std::string{"42"}});
    ds::command::Command cmd = ds::ipc::http::to_command(req, "widget.get");
    EXPECT_EQ(cmd.id, "widget.get");
    EXPECT_EQ(*cmd.args.get_string("id"), "42");
}

TEST(HttpMapCommand, DispatchRequestOkMapsTo200) {
    CommandBus bus;
    bus.register_command("widget.get", [](const CommandArgs& args) {
        auto id = args.get_string("id").value_or("?");
        return CommandResult::make_ok(CommandValue{std::string{"widget-"} + id});
    });

    HttpRouter router;
    router.get("/widgets/:id", ds::ipc::http::command_dispatcher(bus, "widget.get"));

    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Get, "/widgets/9"});
    EXPECT_EQ(resp.status, status::Ok);
    EXPECT_EQ(resp.body, "widget-9");
}

TEST(HttpMapCommand, UnknownCommandMapsTo404) {
    CommandBus bus;  // 無註冊任何命令
    HttpResponse resp = ds::ipc::http::dispatch_request(bus, "nope",
                                                        HttpRequest{HttpMethod::Get, "/x"});
    EXPECT_EQ(resp.status, status::NotFound);
}

TEST(HttpMapCommand, FailedCommandMapsTo400WithMessage) {
    CommandBus bus;
    bus.register_command("do.it", [](const CommandArgs&) {
        return CommandResult::make_failed("nope-reason");
    });
    HttpResponse resp = ds::ipc::http::dispatch_request(bus, "do.it",
                                                        HttpRequest{HttpMethod::Post, "/do"});
    EXPECT_EQ(resp.status, status::BadRequest);
    EXPECT_EQ(resp.body, "nope-reason");
}

// ---------------------------------------------------------------------------
// 映射到 E10-01 訊息通道
// ---------------------------------------------------------------------------
TEST(HttpMapChannel, ToMessageCarriesParamsAndRoundTripsThroughQueue) {
    HttpRequest req{HttpMethod::Post, "/widgets/7/refresh"};
    req.params.set("id", CommandValue{std::string{"7"}});

    ds::ipc::Message msg = ds::ipc::http::to_message(req, "widget.refresh");
    EXPECT_EQ(msg.type, "widget.refresh");
    EXPECT_EQ(*msg.payload.get_string("id"), "7");

    // 經 E10-01 MessageChannel 點對點佇列 round-trip
    ds::ipc::MessageChannel ch;
    ch.send(msg);
    auto got = ch.receive();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->type, "widget.refresh");
    EXPECT_EQ(*got->payload.get_string("id"), "7");
}

TEST(HttpMapChannel, RouteHandlerPublishesToChannel) {
    ds::ipc::MessageChannel ch;
    int delivered = 0;
    ch.subscribe("widget.refresh", [&delivered](const ds::ipc::Message&) { ++delivered; });

    HttpRouter router;
    router.post("/widgets/:id/refresh", [&ch](const HttpRequest& req) {
        ch.publish(ds::ipc::http::to_message(req, "widget.refresh"));
        return HttpResponse::make_no_content();
    });

    HttpResponse resp = router.handle(HttpRequest{HttpMethod::Post, "/widgets/3/refresh"});
    EXPECT_EQ(resp.status, status::NoContent);
    EXPECT_EQ(delivered, 1);
}

// ---------------------------------------------------------------------------
// 契約版本標記
// ---------------------------------------------------------------------------
TEST(HttpContract, VersionTag) {
    EXPECT_EQ(std::string(ds::ipc::http::contract_version()), "e10_03/1.0.0");
}
