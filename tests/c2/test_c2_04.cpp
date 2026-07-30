// tests/c2/test_c2_04.cpp — C2-04 天氣 widget（gtest）
//
// 涵蓋：C1-01 基底組裝、configure()（有效 / 各類無效具名設定）、refresh()（未設定即刷新 /
// transport 為 null / 經 E2-14 注入回應成功取值 / 非 2xx 擷取失敗降級（保留既有資料）/
// JSON 解析失敗 / 必要欄位缺失或型別不符 / icon 選填欄位缺失）、display()（尚無資料時 Invalid /
// E7-10 format() 格式化文字 / E4-01 排版 / E4-02 圖示渲染描述 / 攝氏與華氏單位換算 / 缺 icon
// 時 has_source 為 false）。
#include "weather_widget.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using ds::format::Value;
using ds::kernel::CapabilityMatrix;
using ds::kernel::LayerStack;
using ds::kernel::NullKernelBackend;
using ds::profiles::SkinState;
using ds::render::FixedFontMetrics;
using ds::sysinfo::HttpResponse;
using ds::sysinfo::NullHttpTransport;
using ds::widgets::TemperatureUnit;
using ds::widgets::WeatherStatus;
using ds::widgets::WeatherWidget;

namespace {

const char* const kEndpoint = "https://weather.example/api";

const char* const kValidBody =
    R"({"temp_c": 21.5, "description": "Cloudy", "icon": "cloudy"})";

// 測試固定件：以 defaults() 能力矩陣建構後端 / 圖層堆疊（不需 per-pixel alpha 能力，
// 本 widget 不直接測試 load_skin，只驗證 C1-01 基底已正確組裝）。
struct Fixture {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    std::shared_ptr<NullHttpTransport> transport = std::make_shared<NullHttpTransport>();
    FixedFontMetrics metrics{6.0, 14.0};
};

}  // namespace

// -----------------------------------------------------------------------------
// 建構 / C1-01 基底組裝
// -----------------------------------------------------------------------------

TEST(WeatherWidget, ConstructedHasAssembledBaseAndDefaults) {
    Fixture f;
    WeatherWidget widget{"widget.weather.home", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    EXPECT_EQ(widget.id(), std::string("widget.weather.home"));
    // C1-01 基底以同一 id 組裝，初始未載入。
    EXPECT_EQ(widget.base().id(), std::string("widget.weather.home"));
    EXPECT_EQ(widget.base().state(), SkinState::Unloaded);

    EXPECT_FALSE(widget.is_configured());
    EXPECT_FALSE(widget.has_data());
    EXPECT_FALSE(widget.render_model().has_data);
}

// -----------------------------------------------------------------------------
// configure（Value:地點/單位）
// -----------------------------------------------------------------------------

TEST(WeatherWidget, ConfigureValidDefaultsToCelsius) {
    Fixture f;
    WeatherWidget widget{"widget.weather.a", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    Value def = Value::map({{"location", Value::string("Taipei")}});
    EXPECT_EQ(widget.configure(def), WeatherStatus::Ok);
    EXPECT_TRUE(widget.is_configured());
    EXPECT_EQ(widget.config().location, std::string("Taipei"));
    EXPECT_EQ(widget.config().unit, TemperatureUnit::Celsius);
}

TEST(WeatherWidget, ConfigureValidWithFahrenheitUnit) {
    Fixture f;
    WeatherWidget widget{"widget.weather.b", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    Value def = Value::map(
        {{"location", Value::string("New York")}, {"unit", Value::string("fahrenheit")}});
    EXPECT_EQ(widget.configure(def), WeatherStatus::Ok);
    EXPECT_EQ(widget.config().unit, TemperatureUnit::Fahrenheit);
}

TEST(WeatherWidget, ConfigureInvalidNonMap) {
    Fixture f;
    WeatherWidget widget{"widget.weather.c", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    EXPECT_EQ(widget.configure(Value::string("not-a-map")), WeatherStatus::Invalid);
    EXPECT_FALSE(widget.is_configured());
}

TEST(WeatherWidget, ConfigureInvalidMissingLocation) {
    Fixture f;
    WeatherWidget widget{"widget.weather.d", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    EXPECT_EQ(widget.configure(Value::map({})), WeatherStatus::Invalid);
    EXPECT_FALSE(widget.is_configured());
}

TEST(WeatherWidget, ConfigureInvalidEmptyLocation) {
    Fixture f;
    WeatherWidget widget{"widget.weather.e", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    Value def = Value::map({{"location", Value::string("")}});
    EXPECT_EQ(widget.configure(def), WeatherStatus::Invalid);
}

TEST(WeatherWidget, ConfigureInvalidUnitName) {
    Fixture f;
    WeatherWidget widget{"widget.weather.f", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};

    Value def =
        Value::map({{"location", Value::string("Taipei")}, {"unit", Value::string("kelvin")}});
    EXPECT_EQ(widget.configure(def), WeatherStatus::Invalid);
    EXPECT_FALSE(widget.is_configured());
}

// -----------------------------------------------------------------------------
// refresh — 經 E2-14 注入回應 + 結構化解析
// -----------------------------------------------------------------------------

TEST(WeatherWidget, RefreshWithoutConfigureIsInvalid) {
    Fixture f;
    WeatherWidget widget{"widget.weather.g", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    EXPECT_EQ(widget.refresh(), WeatherStatus::Invalid);
}

TEST(WeatherWidget, RefreshWithNullTransportFails) {
    NullKernelBackend backend{CapabilityMatrix::defaults()};
    LayerStack layers{CapabilityMatrix::defaults()};
    FixedFontMetrics metrics{6.0, 14.0};
    WeatherWidget widget{"widget.weather.h", backend, layers, nullptr, kEndpoint, metrics};

    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    EXPECT_EQ(widget.refresh(), WeatherStatus::FetchFailed);
    EXPECT_FALSE(widget.has_data());
}

TEST(WeatherWidget, RefreshSuccessParsesInjectedJson) {
    Fixture f;
    WeatherWidget widget{"widget.weather.i", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));

    const std::string url = widget.request_url();
    EXPECT_EQ(url, std::string(kEndpoint) + "?location=Taipei");
    f.transport->set_response(url, HttpResponse::ok(kValidBody));

    EXPECT_EQ(widget.refresh(), WeatherStatus::Ok);
    EXPECT_TRUE(widget.has_data());
    EXPECT_EQ(widget.temperature_celsius(), 21.5);
    EXPECT_EQ(widget.description(), std::string("Cloudy"));
    EXPECT_EQ(widget.icon_code(), std::string("cloudy"));
    EXPECT_EQ(widget.last_status(), 200);
    EXPECT_EQ(f.transport->request_count(), static_cast<std::size_t>(1));
}

TEST(WeatherWidget, RefreshFetchFailedOnNon2xx) {
    Fixture f;
    WeatherWidget widget{"widget.weather.j", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));

    const std::string url = widget.request_url();
    f.transport->set_response(url, HttpResponse::of(500, "server error"));

    EXPECT_EQ(widget.refresh(), WeatherStatus::FetchFailed);
    EXPECT_FALSE(widget.has_data());
    EXPECT_EQ(widget.last_status(), 500);
}

TEST(WeatherWidget, RefreshFetchFailureDegradesButKeepsPriorData) {
    Fixture f;
    WeatherWidget widget{"widget.weather.k", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();

    f.transport->set_response(url, HttpResponse::ok(kValidBody));
    ASSERT_EQ(widget.refresh(), WeatherStatus::Ok);
    ASSERT_TRUE(widget.has_data());

    // 之後端點失效：降級——回 FetchFailed，但既有（上一輪成功取得）的資料不被清除。
    f.transport->set_response(url, HttpResponse::of(503, "unavailable"));
    EXPECT_EQ(widget.refresh(), WeatherStatus::FetchFailed);
    EXPECT_TRUE(widget.has_data());
    EXPECT_EQ(widget.description(), std::string("Cloudy"));
}

TEST(WeatherWidget, RefreshParseFailedOnMalformedJson) {
    Fixture f;
    WeatherWidget widget{"widget.weather.l", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();

    f.transport->set_response(url, HttpResponse::ok("not-json{"));
    EXPECT_EQ(widget.refresh(), WeatherStatus::ParseFailed);
    EXPECT_FALSE(widget.has_data());
}

TEST(WeatherWidget, RefreshParseFailedOnMissingRequiredField) {
    Fixture f;
    WeatherWidget widget{"widget.weather.m", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();

    // 缺 description。
    f.transport->set_response(url, HttpResponse::ok(R"({"temp_c": 10})"));
    EXPECT_EQ(widget.refresh(), WeatherStatus::ParseFailed);
}

TEST(WeatherWidget, RefreshParseFailedOnWrongFieldType) {
    Fixture f;
    WeatherWidget widget{"widget.weather.n", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();

    // temp_c 應為數值，此處為字串。
    f.transport->set_response(url,
                              HttpResponse::ok(R"({"temp_c": "cold", "description": "Foggy"})"));
    EXPECT_EQ(widget.refresh(), WeatherStatus::ParseFailed);
}

TEST(WeatherWidget, RefreshOkWithMissingOptionalIconField) {
    Fixture f;
    WeatherWidget widget{"widget.weather.o", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();

    f.transport->set_response(url, HttpResponse::ok(R"({"temp_c": 5, "description": "Clear"})"));
    EXPECT_EQ(widget.refresh(), WeatherStatus::Ok);
    EXPECT_TRUE(widget.icon_code().empty());
}

// -----------------------------------------------------------------------------
// display — E7-10 格式化 + E4-01 排版 + E4-02 圖示渲染描述
// -----------------------------------------------------------------------------

TEST(WeatherWidget, DisplayBeforeDataIsInvalid) {
    Fixture f;
    WeatherWidget widget{"widget.weather.p", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));

    EXPECT_EQ(widget.display(), WeatherStatus::Invalid);
    EXPECT_FALSE(widget.render_model().has_data);
}

TEST(WeatherWidget, DisplayCelsiusProducesTextIconAndLayout) {
    Fixture f;
    WeatherWidget widget{"widget.weather.q", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();
    f.transport->set_response(url, HttpResponse::ok(kValidBody));
    ASSERT_EQ(widget.refresh(), WeatherStatus::Ok);

    EXPECT_EQ(widget.display(), WeatherStatus::Ok);
    const auto& model = widget.render_model();
    EXPECT_TRUE(model.has_data);
    // 21.5°C 四捨五入為 22（lround 遠離零捨入）。
    EXPECT_NE(model.formatted_text.find("22"), std::string::npos);
    EXPECT_NE(model.formatted_text.find("Cloudy"), std::string::npos);

    // E4-01 排版：非空文字、單行（無換行、無界寬度不換行）。
    EXPECT_FALSE(model.text.lines.empty());
    EXPECT_FALSE(model.text.glyphs.empty());

    // E4-02 圖示渲染描述。
    EXPECT_TRUE(model.icon.has_source);
    EXPECT_EQ(model.icon.source_reference, std::string("icon/cloudy"));
    EXPECT_EQ(model.icon.target, std::string("widget.weather.q.icon"));
}

TEST(WeatherWidget, DisplayFahrenheitConvertsTemperature) {
    Fixture f;
    WeatherWidget widget{"widget.weather.r", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(
        Value::map({{"location", Value::string("NYC")}, {"unit", Value::string("fahrenheit")}}));
    const std::string url = widget.request_url();
    // 0°C = 32°F。
    f.transport->set_response(
        url, HttpResponse::ok(R"({"temp_c": 0, "description": "Snow", "icon": "snow"})"));
    ASSERT_EQ(widget.refresh(), WeatherStatus::Ok);

    EXPECT_EQ(widget.display(), WeatherStatus::Ok);
    const auto& model = widget.render_model();
    EXPECT_NE(model.formatted_text.find("32"), std::string::npos);
    EXPECT_NE(model.formatted_text.find("F"), std::string::npos);
}

TEST(WeatherWidget, DisplayWithoutIconHasNoSource) {
    Fixture f;
    WeatherWidget widget{"widget.weather.s", f.backend, f.layers, f.transport, kEndpoint,
                         f.metrics};
    widget.configure(Value::map({{"location", Value::string("Taipei")}}));
    const std::string url = widget.request_url();
    f.transport->set_response(url, HttpResponse::ok(R"({"temp_c": 5, "description": "Clear"})"));
    ASSERT_EQ(widget.refresh(), WeatherStatus::Ok);

    EXPECT_EQ(widget.display(), WeatherStatus::Ok);
    EXPECT_FALSE(widget.render_model().icon.has_source);
}
