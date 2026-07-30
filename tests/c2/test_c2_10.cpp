// tests/c2/test_c2_10.cpp — C2-10 網頁 widget（gtest）
//
// 涵蓋：組裝建構（掛 C1-01 殼層、預設空狀態）、configure()（合法 url / 非 Map / 缺 url /
// 非字串 url / 空字串 / 缺 scheme / scheme 後無內容 皆 Invalid 且不改動既有狀態；已載入時
// 重新 configure 會先停止舊行程並清空內容）、load()（注入內容模擬載入 / 未 configure()
// 為 NotConfigured / 空注入內容為 Invalid 且轉 Failed / 已存活行程再次 load 直接更新內容不
// 重啟行程）、reload()（重啟行程並回到 Configured 等待重新 load / 行程 handle 因重啟而改變 /
// 未曾成功 load 過為 NotConfigured）、loading_state 轉移（Idle → Configured → Loaded /
// Degraded → Failed 等路徑）、E10-05 能力閘控降級（NFR-03：能力後端存在但不支援 →
// Unsupported / Degraded，不崩潰）、無後端降級（未注入能力後端 → 同樣優雅降級）、
// render_model()（各狀態文字呈現）、to_string() 具名字串（NFR-02）、解構安全（RAII 停止
// 仍在跑的內容行程，不遺留孤兒行程）。
#include "web_widget.hpp"

#include <gtest/gtest.h>

#include <string>

using ds::format::Value;
using ds::ipc::MessageChannel;
using ds::ipc::NullProcessLauncher;
using ds::kernel::AlphaSurfaceService;
using ds::kernel::alpha_capable_matrix;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::profiles::SkinState;
using ds::widgets::is_valid_web_url;
using ds::widgets::LoadingState;
using ds::widgets::to_string;
using ds::widgets::WebWidget;
using ds::widgets::WebWidgetStatus;

namespace {

Value config_with_url(const std::string& url) {
    return Value::map({{"url", Value::string(url)}});
}

// 便利 fixture：能力可用（alpha_capable_matrix + 注入能力後端）。每個測試各自一套，互不干擾。
struct CapableEnv {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    NullProcessLauncher launcher;
    MessageChannel channel;
    AlphaSurfaceService surface{backend};
    WebWidget widget{"widget.web", backend, layers, launcher, channel, &surface};
};

// 能力不可用（保守 defaults() 矩陣，per-pixel alpha 不宣告），但已注入能力後端 —— 用於 NFR-03
// 能力閘控降級測試（服務存在但 supported() == false）。
struct IncapableEnv {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    NullProcessLauncher launcher;
    MessageChannel channel;
    AlphaSurfaceService surface{backend};
    WebWidget widget{"widget.web", backend, layers, launcher, channel, &surface};
};

// 完全未注入能力後端（surface_service == nullptr）—— 用於「無後端降級」測試。
struct NoServiceEnv {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    NullProcessLauncher launcher;
    MessageChannel channel;
    WebWidget widget{"widget.web", backend, layers, launcher, channel};
};

}  // namespace

// -----------------------------------------------------------------------------
// 組裝建構
// -----------------------------------------------------------------------------

TEST(WebWidget, ConstructedIdleWithShellAndNoWidget) {
    CapableEnv env;
    EXPECT_EQ(env.widget.id(), std::string("widget.web"));
    EXPECT_EQ(env.widget.shell().id(), std::string("widget.web"));
    EXPECT_EQ(env.widget.shell().state(), SkinState::Unloaded);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
    EXPECT_TRUE(env.widget.url().empty());
    EXPECT_TRUE(env.widget.content().empty());
    EXPECT_FALSE(env.widget.surface_bridged());
    EXPECT_FALSE(env.widget.host().has_widget());
    EXPECT_TRUE(env.widget.surface_capable());
}

// -----------------------------------------------------------------------------
// is_valid_web_url —— 純函式，供 configure() 內部使用
// -----------------------------------------------------------------------------

TEST(IsValidWebUrl, AcceptsSchemeWithContent) {
    EXPECT_TRUE(is_valid_web_url("https://example.com"));
    EXPECT_TRUE(is_valid_web_url("http://a"));
    EXPECT_TRUE(is_valid_web_url("file:///tmp/x.html"));
}

TEST(IsValidWebUrl, RejectsEmptyNoSchemeOrEmptyAfterScheme) {
    EXPECT_FALSE(is_valid_web_url(""));
    EXPECT_FALSE(is_valid_web_url("example.com"));
    EXPECT_FALSE(is_valid_web_url("://missing-scheme"));
    EXPECT_FALSE(is_valid_web_url("http://"));
}

// -----------------------------------------------------------------------------
// configure()
// -----------------------------------------------------------------------------

TEST(WebWidgetConfigure, ValidUrlSucceedsAndTransitionsToConfigured) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.url(), std::string("https://example.com"));
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Configured);
    EXPECT_TRUE(env.widget.content().empty());
}

TEST(WebWidgetConfigure, NonMapConfigIsInvalidAndDoesNotChangeState) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(Value::string("not-a-map")), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
    EXPECT_TRUE(env.widget.url().empty());
}

TEST(WebWidgetConfigure, MissingUrlKeyIsInvalid) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(Value::map({})), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
}

TEST(WebWidgetConfigure, NonStringUrlIsInvalid) {
    CapableEnv env;
    Value cfg = Value::map({{"url", Value::integer(5)}});
    EXPECT_EQ(env.widget.configure(cfg), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
}

TEST(WebWidgetConfigure, EmptyStringUrlIsInvalid) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(config_with_url("")), WebWidgetStatus::Invalid);
}

TEST(WebWidgetConfigure, UrlWithoutSchemeIsInvalid) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(config_with_url("example.com")), WebWidgetStatus::Invalid);
}

TEST(WebWidgetConfigure, UrlWithSchemeButNoContentIsInvalid) {
    CapableEnv env;
    EXPECT_EQ(env.widget.configure(config_with_url("http://")), WebWidgetStatus::Invalid);
}

TEST(WebWidgetConfigure, ReconfiguringWhileLoadedStopsHostAndClearsContent) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://a.example")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load("<html>a</html>"), WebWidgetStatus::Ok);
    ASSERT_TRUE(env.widget.host().is_alive());

    EXPECT_EQ(env.widget.configure(config_with_url("https://b.example")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.url(), std::string("https://b.example"));
    EXPECT_TRUE(env.widget.content().empty());
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Configured);
    EXPECT_FALSE(env.widget.host().is_alive());
}

// -----------------------------------------------------------------------------
// load()：注入內容模擬一次網頁載入
// -----------------------------------------------------------------------------

TEST(WebWidgetLoad, WithoutConfigureReturnsNotConfigured) {
    CapableEnv env;
    EXPECT_EQ(env.widget.load("<html></html>"), WebWidgetStatus::NotConfigured);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
}

TEST(WebWidgetLoad, EmptyInjectedContentIsInvalidAndTransitionsToFailed) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.load(""), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Failed);
    EXPECT_FALSE(env.widget.host().is_alive());
}

TEST(WebWidgetLoad, ValidContentWithCapableBackendFullyLoadsAndBridgesSurface) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.load("<html>hi</html>"), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Loaded);
    EXPECT_EQ(env.widget.content(), std::string("<html>hi</html>"));
    EXPECT_TRUE(env.widget.surface_bridged());
    EXPECT_TRUE(env.widget.host().is_alive());
    EXPECT_TRUE(env.surface.has_alpha_surface("widget.web.surface"));
}

TEST(WebWidgetLoad, SecondLoadWhileAliveUpdatesContentWithoutRestartingProcess) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load("first"), WebWidgetStatus::Ok);
    const std::string handle_after_first = env.widget.host().process_handle();

    EXPECT_EQ(env.widget.load("second"), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.content(), std::string("second"));
    EXPECT_EQ(env.widget.host().process_handle(), handle_after_first);  // 未重啟行程
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Loaded);
}

// -----------------------------------------------------------------------------
// reload()
// -----------------------------------------------------------------------------

TEST(WebWidgetReload, BeforeAnySuccessfulLoadReturnsNotConfigured) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.reload(), WebWidgetStatus::NotConfigured);
}

TEST(WebWidgetReload, AfterLoadRestartsProcessAndAwaitsFreshContent) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load("first"), WebWidgetStatus::Ok);
    const std::string handle_before_reload = env.widget.host().process_handle();

    EXPECT_EQ(env.widget.reload(), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Configured);
    EXPECT_TRUE(env.widget.content().empty());
    EXPECT_TRUE(env.widget.host().is_alive());
    EXPECT_NE(env.widget.host().process_handle(), handle_before_reload);  // 重啟：新 handle

    EXPECT_EQ(env.widget.load("second"), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.content(), std::string("second"));
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Loaded);
}

// -----------------------------------------------------------------------------
// loading_state 轉移（整體路徑）
// -----------------------------------------------------------------------------

TEST(WebWidgetLoadingState, FullHappyPathTransitions) {
    CapableEnv env;
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Idle);
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Configured);
    ASSERT_EQ(env.widget.load("content"), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Loaded);
    ASSERT_EQ(env.widget.reload(), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Configured);
}

TEST(WebWidgetLoadingState, InvalidLoadTransitionsToFailed) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.load(""), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Failed);
}

// -----------------------------------------------------------------------------
// E10-05 能力閘控降級（NFR-03）：能力後端存在但 supported() == false
// -----------------------------------------------------------------------------

TEST(WebWidgetCapabilityGating, UnsupportedCapabilityDegradesGracefully) {
    IncapableEnv env;
    ASSERT_FALSE(env.surface.supported());
    ASSERT_FALSE(env.widget.surface_capable());

    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.load("content"), WebWidgetStatus::Unsupported);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Degraded);
    EXPECT_EQ(env.widget.content(), std::string("content"));  // 內容仍透過注入式後端載入
    EXPECT_FALSE(env.widget.surface_bridged());
    EXPECT_TRUE(env.widget.host().is_alive());  // 行程本身仍正常啟動，只是未橋接畫面 surface
    EXPECT_FALSE(env.surface.has_alpha_surface("widget.web.surface"));
}

// -----------------------------------------------------------------------------
// 無後端降級：完全未注入能力後端（surface_service == nullptr）
// -----------------------------------------------------------------------------

TEST(WebWidgetCapabilityGating, NoServiceInjectedDegradesGracefully) {
    NoServiceEnv env;
    ASSERT_FALSE(env.widget.surface_capable());

    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.load("content"), WebWidgetStatus::Unsupported);
    EXPECT_EQ(env.widget.loading_state(), LoadingState::Degraded);
    EXPECT_EQ(env.widget.content(), std::string("content"));
    EXPECT_FALSE(env.widget.surface_bridged());
    EXPECT_TRUE(env.widget.host().is_alive());
}

// -----------------------------------------------------------------------------
// render_model()
// -----------------------------------------------------------------------------

TEST(WebWidgetRenderModel, IdleStateHasNoUrlPlaceholder) {
    CapableEnv env;
    EXPECT_EQ(env.widget.render_model(), std::string("[idle] (no url configured)"));
}

TEST(WebWidgetRenderModel, ConfiguredStateShowsUrlOnly) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.render_model(), std::string("[configured] https://example.com"));
}

TEST(WebWidgetRenderModel, LoadedStateShowsUrlAndContent) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load("hello"), WebWidgetStatus::Ok);
    EXPECT_EQ(env.widget.render_model(), std::string("[loaded] https://example.com\nhello"));
}

TEST(WebWidgetRenderModel, DegradedStateShowsUrlAndContent) {
    IncapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load("hello"), WebWidgetStatus::Unsupported);
    EXPECT_EQ(env.widget.render_model(), std::string("[degraded] https://example.com\nhello"));
}

TEST(WebWidgetRenderModel, FailedStateShowsUrlWithoutContent) {
    CapableEnv env;
    ASSERT_EQ(env.widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
    ASSERT_EQ(env.widget.load(""), WebWidgetStatus::Invalid);
    EXPECT_EQ(env.widget.render_model(), std::string("[failed] https://example.com"));
}

// -----------------------------------------------------------------------------
// to_string()（NFR-02：具名字串）
// -----------------------------------------------------------------------------

TEST(ToStringLoadingState, CoversAllValues) {
    EXPECT_STREQ(to_string(LoadingState::Idle), "idle");
    EXPECT_STREQ(to_string(LoadingState::Configured), "configured");
    EXPECT_STREQ(to_string(LoadingState::Loading), "loading");
    EXPECT_STREQ(to_string(LoadingState::Loaded), "loaded");
    EXPECT_STREQ(to_string(LoadingState::Degraded), "degraded");
    EXPECT_STREQ(to_string(LoadingState::Failed), "failed");
}

TEST(ToStringWebWidgetStatus, CoversAllValues) {
    EXPECT_STREQ(to_string(WebWidgetStatus::Ok), "ok");
    EXPECT_STREQ(to_string(WebWidgetStatus::Invalid), "invalid");
    EXPECT_STREQ(to_string(WebWidgetStatus::Unsupported), "unsupported");
    EXPECT_STREQ(to_string(WebWidgetStatus::NotConfigured), "not_configured");
    EXPECT_STREQ(to_string(WebWidgetStatus::HostError), "host_error");
}

// -----------------------------------------------------------------------------
// 解構安全：仍在跑的內容行程於 widget 銷毀時會被強制停止（不遺留孤兒行程）。
// -----------------------------------------------------------------------------

TEST(WebWidgetDestruction, StopsRunningHostProcessOnDestroy) {
    NullKernelBackend backend{alpha_capable_matrix()};
    LayerStack layers{CapabilityMatrix::defaults()};
    NullProcessLauncher launcher;
    MessageChannel channel;
    AlphaSurfaceService surface{backend};
    std::string handle;
    {
        WebWidget widget("widget.web", backend, layers, launcher, channel, &surface);
        ASSERT_EQ(widget.configure(config_with_url("https://example.com")), WebWidgetStatus::Ok);
        ASSERT_EQ(widget.load("hi"), WebWidgetStatus::Ok);
        handle = widget.host().process_handle();
        ASSERT_TRUE(launcher.is_alive(handle));
    }
    EXPECT_FALSE(launcher.is_alive(handle));
    EXPECT_FALSE(surface.has_alpha_surface("widget.web.surface"));
}
