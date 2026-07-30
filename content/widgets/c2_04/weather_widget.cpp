// content/widgets/c2_04/weather_widget.cpp — C2-04 天氣 widget 實作
#include "weather_widget.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace ds::widgets {

namespace {

using ds::format::EvalResult;
using ds::format::Value;
using ds::sysinfo::HttpResponse;
using ds::sysinfo::JsonParseResult;
using ds::sysinfo::JsonPath;
using ds::sysinfo::JsonValue;
using ds::sysinfo::PathSeg;

// 攝氏 → 顯示單位換算。
double convert_temperature(double celsius, TemperatureUnit unit) {
    if (unit == TemperatureUnit::Fahrenheit) {
        return celsius * 9.0 / 5.0 + 32.0;
    }
    return celsius;
}

char unit_letter(TemperatureUnit unit) {
    return unit == TemperatureUnit::Fahrenheit ? 'F' : 'C';
}

}  // namespace

const char* to_string(TemperatureUnit u) noexcept {
    switch (u) {
        case TemperatureUnit::Celsius:
            return "celsius";
        case TemperatureUnit::Fahrenheit:
            return "fahrenheit";
    }
    return "celsius";
}

const char* to_string(WeatherStatus s) noexcept {
    switch (s) {
        case WeatherStatus::Ok:
            return "Ok";
        case WeatherStatus::Invalid:
            return "Invalid";
        case WeatherStatus::FetchFailed:
            return "FetchFailed";
        case WeatherStatus::ParseFailed:
            return "ParseFailed";
    }
    return "Invalid";
}

WeatherWidget::WeatherWidget(std::string id, ds::kernel::KernelBackend& backend,
                              ds::kernel::LayerStack& layers,
                              std::shared_ptr<ds::sysinfo::HttpTransport> transport,
                              std::string endpoint_url,
                              const ds::render::FontMetrics& metrics)
    : id_(std::move(id)),
      base_(id_, backend, layers),
      transport_(std::move(transport)),
      endpoint_url_(std::move(endpoint_url)),
      metrics_(metrics),
      layout_(metrics_, id_ + ".text") {}

WeatherStatus WeatherWidget::configure(const ds::format::Value& definition) {
    if (!definition.is_map()) {
        return WeatherStatus::Invalid;
    }

    const Value* location = definition.find("location");
    if (location == nullptr || !location->is_string() || location->as_string().empty()) {
        return WeatherStatus::Invalid;
    }

    TemperatureUnit unit = TemperatureUnit::Celsius;
    if (const Value* unit_field = definition.find("unit")) {
        if (!unit_field->is_string()) {
            return WeatherStatus::Invalid;
        }
        const std::string& u = unit_field->as_string();
        if (u == "celsius") {
            unit = TemperatureUnit::Celsius;
        } else if (u == "fahrenheit") {
            unit = TemperatureUnit::Fahrenheit;
        } else {
            return WeatherStatus::Invalid;
        }
    }

    config_.location = location->as_string();
    config_.unit = unit;
    configured_ = true;
    return WeatherStatus::Ok;
}

std::string WeatherWidget::request_url() const {
    std::vector<Value> parts{
        Value::string(endpoint_url_),
        Value::string("?location="),
        Value::string(config_.location),
    };
    EvalResult joined = ds::format::strings::concat(parts);
    if (joined.ok() && joined.value().is_string()) {
        return joined.value().as_string();
    }
    // 不應發生（三個引數皆為字串）；保守回退為端點基底 URL，不崩、不靜默吞掉呼叫。
    return endpoint_url_;
}

WeatherStatus WeatherWidget::apply_json(const JsonValue& doc) {
    if (!doc.is_object()) {
        return WeatherStatus::ParseFailed;
    }

    const JsonValue* temp = ds::sysinfo::seek(doc, JsonPath{PathSeg::field("temp_c")});
    if (temp == nullptr || !temp->is_number()) {
        return WeatherStatus::ParseFailed;
    }

    const JsonValue* desc = ds::sysinfo::seek(doc, JsonPath{PathSeg::field("description")});
    if (desc == nullptr || !desc->is_string()) {
        return WeatherStatus::ParseFailed;
    }

    std::string icon;
    if (const JsonValue* icon_field = ds::sysinfo::seek(doc, JsonPath{PathSeg::field("icon")})) {
        if (icon_field->is_string()) {
            icon = icon_field->as_string();
        } else if (!icon_field->is_null()) {
            return WeatherStatus::ParseFailed;
        }
    }

    temp_celsius_ = temp->as_number();
    description_ = desc->as_string();
    icon_code_ = icon;
    has_data_ = true;
    return WeatherStatus::Ok;
}

WeatherStatus WeatherWidget::refresh() {
    if (!configured_) {
        return WeatherStatus::Invalid;
    }
    if (!transport_) {
        last_status_ = 0;
        return WeatherStatus::FetchFailed;
    }

    const std::string url = request_url();
    const HttpResponse resp = transport_->get(url);
    last_status_ = resp.status;
    if (!resp.is_success()) {
        return WeatherStatus::FetchFailed;
    }

    JsonParseResult parsed = ds::sysinfo::parse_json(resp.body);
    if (!parsed.ok()) {
        return WeatherStatus::ParseFailed;
    }

    return apply_json(parsed.value());
}

WeatherStatus WeatherWidget::display() {
    if (!has_data_) {
        return WeatherStatus::Invalid;
    }

    const double shown = convert_temperature(temp_celsius_, config_.unit);
    const long rounded = std::lround(shown);
    const std::string temp_str = std::to_string(rounded);
    const std::string unit_str(1, unit_letter(config_.unit));

    std::vector<Value> args{
        Value::string(temp_str),
        Value::string(unit_str),
        Value::string(description_),
    };
    EvalResult formatted =
        ds::format::strings::format(Value::string("{0}\xC2\xB0{1} {2}"), args);
    if (!formatted.ok() || !formatted.value().is_string()) {
        return WeatherStatus::Invalid;
    }
    model_.formatted_text = formatted.value().as_string();

    ds::render::LayoutConstraints constraints;
    model_.text = layout_.layout(model_.formatted_text, constraints);

    ds::elements::ImageElement icon_element;
    if (!icon_code_.empty()) {
        ds::elements::MemoryImageSource source(
            "icon/" + icon_code_,
            ds::elements::ImageDimensions{kIconSize, kIconSize});
        icon_element.set_source(source);
        icon_element.set_target(id_ + ".icon");
    }
    model_.icon = icon_element.render_model();
    model_.has_data = true;

    return WeatherStatus::Ok;
}

}  // namespace ds::widgets
