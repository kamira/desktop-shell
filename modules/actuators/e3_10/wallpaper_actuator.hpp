// E3-10 桌布設定致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「設定桌面桌布」這組副作用（設定指定顯示器 / 所有顯示器的桌布影像路徑、
// 縮放模式 fill / fit / stretch / center、查詢目前桌布），以具名命令掛上 E6-01 命令匯流排
// （`wallpaper.set` / `wallpaper.get`）。呼叫端只需 命令 id + 具名參數即可觸發，
// 不需相依本致動器或任何 OS 桌布 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01 契約
// （與已合併的 E3-05「音量設定」採同一「注入式後端 + null 樣式」範式）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 桌布 API**（不含 NSWorkspace /
//     setDesktopImageURL / SystemParametersInfo / win32 / cocoa 等）。所有桌布狀態交由
//     可抽換的 `WallpaperBackend` 承接；預設 `NullWallpaperBackend` 以純記憶體狀態模擬
//     （每顯示器一份 WallpaperSpec），供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端
//     （win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實桌布 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（路徑 / 模式 / 顯示器參數驗證、經 E6-01 分派、結果回報）與後端解耦：
// 參數合法化（空路徑拒絕、模式字串解析、顯示器範圍檢查、「所有顯示器」展開）在致動器層
// 完成，後端只承接「已合法化」的意圖，因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_10_WALLPAPER_ACTUATOR_HPP
#define DS_ACTUATORS_E3_10_WALLPAPER_ACTUATOR_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 命名加 `wallpaper_` 前綴，避免與同命名空間其他致動器（如 e3_05 的
// `volume_contract_version()`）的版本符號衝突（兩者可能同時連入同一 target）。
const char* wallpaper_contract_version() noexcept;

// 兩個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdWallpaperSet = "wallpaper.set";  // 必填 path；選填 mode / display
inline constexpr const char* kCmdWallpaperGet = "wallpaper.get";  // 選填 display，回目前桌布

// display 參數缺席時的語意哨兵：套用到「所有顯示器」。
// display 為顯示器索引（>= 0 的相對索引，非絕對座標 / z-order）；缺席 → 全部。
inline constexpr int kAllDisplays = -1;

// ---------------------------------------------------------------------------
// ScaleMode — 桌布縮放模式（列舉；不使用數字魔法值，字串 <-> 列舉雙向轉換）。
//
// fill    ：填滿（可能裁切）
// fit     ：等比縮放至完整可見（可能留邊）
// stretch ：拉伸至填滿（可能變形）
// center  ：置中原尺寸（不縮放）
// ---------------------------------------------------------------------------
enum class ScaleMode { Fill, Fit, Stretch, Center };

// 縮放模式 → 穩定字串（供回報 / 診斷 / 序列化）。
inline const char* scale_mode_to_string(ScaleMode mode) noexcept {
    switch (mode) {
        case ScaleMode::Fill:    return "fill";
        case ScaleMode::Fit:     return "fit";
        case ScaleMode::Stretch: return "stretch";
        case ScaleMode::Center:  return "center";
    }
    return "fill";  // 不可達；防禦性預設。
}

// 字串 → 縮放模式；未知字串回 std::nullopt（致動器層據此回報無效模式，不崩潰）。
inline std::optional<ScaleMode> scale_mode_from_string(const std::string& s) {
    if (s == "fill")    return ScaleMode::Fill;
    if (s == "fit")     return ScaleMode::Fit;
    if (s == "stretch") return ScaleMode::Stretch;
    if (s == "center")  return ScaleMode::Center;
    return std::nullopt;
}

// 預設縮放模式（未指定 mode 參數時）。
inline constexpr ScaleMode kDefaultScaleMode = ScaleMode::Fill;

// ---------------------------------------------------------------------------
// WallpaperSpec — 平台中立地描述「一個顯示器要顯示的桌布」。
//
// 不含任何 OS handle / 螢幕幾何 / 絕對座標；只承載「影像路徑 + 縮放模式」。
// 路徑是不透明字串（致動器只驗非空，不解讀檔案系統語意）。
// ---------------------------------------------------------------------------
struct WallpaperSpec {
    std::string path;                        // 桌布影像路徑（不透明；致動器僅驗非空）
    ScaleMode mode = kDefaultScaleMode;      // 縮放模式

    bool operator==(const WallpaperSpec& o) const noexcept {
        return path == o.path && mode == o.mode;
    }
    bool operator!=(const WallpaperSpec& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// WallpaperBackend — 執行實際桌布副作用的抽象後端。
//
// 相位 1 僅提供 NullWallpaperBackend；相位 2 由平台後端以真實桌布 API 實作。
// 介面刻意最小：display_count / set(display, spec) / get(display) 三組原語，致動器層以此
// 組合出「單一顯示器 / 所有顯示器」設定與查詢。
// 契約保證：傳入 set() 的 spec.path 已由致動器驗為非空、mode 已合法化（後端可信任）；
//           傳入的 display 已由致動器驗在 [0, display_count) 範圍內。
// ---------------------------------------------------------------------------
class WallpaperBackend {
public:
    virtual ~WallpaperBackend() = default;

    // 顯示器數量（>= 1）。致動器據此展開「所有顯示器」與檢查 display 範圍。
    virtual int display_count() const = 0;
    // 設定指定顯示器的桌布（呼叫端保證 display 合法、spec 已合法化）。
    virtual void set(int display, const WallpaperSpec& spec) = 0;
    // 查詢指定顯示器目前桌布；未曾設定回 std::nullopt。
    virtual std::optional<WallpaperSpec> get(int display) const = 0;
};

// ---------------------------------------------------------------------------
// NullWallpaperBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（設定 → 查詢一致、多顯示器 / 全部、縮放模式），
// 並讓測試 / 診斷驗證狀態一致性。顯示器數量可注入（預設 1）。
// ---------------------------------------------------------------------------
class NullWallpaperBackend : public WallpaperBackend {
public:
    NullWallpaperBackend() = default;
    // 顯示器數至少 1（<= 0 一律夾為 1，避免無顯示器的退化狀態）。
    explicit NullWallpaperBackend(int display_count)
        : display_count_(display_count > 0 ? display_count : 1) {}

    int display_count() const override { return display_count_; }

    void set(int display, const WallpaperSpec& spec) override {
        specs_[display] = spec;
    }

    std::optional<WallpaperSpec> get(int display) const override {
        auto it = specs_.find(display);
        if (it == specs_.end()) return std::nullopt;
        return it->second;
    }

    // 內省：已設定桌布的顯示器數（供測試 / 診斷）。
    std::size_t set_count() const noexcept { return specs_.size(); }

private:
    int display_count_ = 1;                     // 記憶體模擬的顯示器數量（預設 1）
    std::map<int, WallpaperSpec> specs_;        // display 索引 → 桌布規格（有序、決定性）
};

// ---------------------------------------------------------------------------
// WallpaperActuator — 把兩個具名命令掛上 E6-01 命令匯流排的桌布致動器。
//
// 建構時綁定一個 WallpaperBackend（相位 1 為 NullWallpaperBackend）。register_on(bus) 將
// wallpaper.set / wallpaper.get 註冊到匯流排；呼叫端之後只需 bus.dispatch("wallpaper.set", args)
// 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填 / 選填參數以 has()/get_* 保護）：
//   - wallpaper.set：必填 `path`（string，非空）；選填 `mode`（string，fill/fit/stretch/center，
//     預設 fill；未知字串 → Failed）；選填 `display`（int，>= 0；缺席 → 所有顯示器；
//     超出 [0, display_count) → Failed）。成功回 value = 實際設定的顯示器數（int）。
//   - wallpaper.get：選填 `display`（int，預設 0；超出範圍 → Failed）。回 value = 桌布路徑
//     （string；未設定回 Null），message 帶縮放模式（未設定為 "unset"）。
// 缺 / 型別錯 / 無效值的參數 → 回 CommandResult{Failed}（不崩潰、不改後端狀態、不丟例外）。
// ---------------------------------------------------------------------------
class WallpaperActuator {
public:
    explicit WallpaperActuator(std::shared_ptr<WallpaperBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullWallpaperBackend（相位 1，單一顯示器）。
    WallpaperActuator() : backend_(std::make_shared<NullWallpaperBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<WallpaperBackend>& backend() const noexcept { return backend_; }

    // 將兩個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_set = bus.register_command(
            kCmdWallpaperSet, [self](const ds::command::CommandArgs& a) {
                return self->handle_set(a);
            });
        const bool ok_get = bus.register_command(
            kCmdWallpaperGet, [self](const ds::command::CommandArgs& a) {
                return self->handle_get(a);
            });
        if (ok_set && ok_get) return true;
        // 回滾：只移除本次成功掛上的。
        if (ok_set) bus.unregister(kCmdWallpaperSet);
        if (ok_get) bus.unregister(kCmdWallpaperGet);
        return false;
    }

    // 從匯流排移除兩個具名命令。回傳確有移除的數量（0..2）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdWallpaperSet) ? 1 : 0;
        n += bus.unregister(kCmdWallpaperGet) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_set(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();

        // 必填 path：缺席 / 型別錯 / 空字串 → Failed（不改後端狀態）。
        if (!args.has("path")) {
            return ds::command::CommandResult::make_failed("wallpaper.set: missing 'path'");
        }
        const auto path = args.get_string("path");
        if (!path) {
            return ds::command::CommandResult::make_failed(
                "wallpaper.set: 'path' must be a string");
        }
        if (path->empty()) {
            return ds::command::CommandResult::make_failed(
                "wallpaper.set: 'path' must not be empty");
        }

        // 選填 mode：缺席 → 預設 fill；型別錯 / 未知字串 → Failed。
        ScaleMode mode = kDefaultScaleMode;
        if (args.has("mode")) {
            const auto mode_str = args.get_string("mode");
            if (!mode_str) {
                return ds::command::CommandResult::make_failed(
                    "wallpaper.set: 'mode' must be a string");
            }
            const auto parsed = scale_mode_from_string(*mode_str);
            if (!parsed) {
                return ds::command::CommandResult::make_failed(
                    "wallpaper.set: invalid 'mode' (expected fill/fit/stretch/center)");
            }
            mode = *parsed;
        }

        // 選填 display：缺席 → 所有顯示器；型別錯 → Failed；超出範圍 → Failed。
        int display = kAllDisplays;
        if (args.has("display")) {
            const auto d = args.get_int("display");
            if (!d) {
                return ds::command::CommandResult::make_failed(
                    "wallpaper.set: 'display' must be an integer");
            }
            display = static_cast<int>(*d);
            if (display < 0 || display >= backend_->display_count()) {
                return ds::command::CommandResult::make_failed(
                    "wallpaper.set: 'display' out of range");
            }
        }

        const WallpaperSpec spec{*path, mode};
        int affected = 0;
        if (display == kAllDisplays) {
            const int n = backend_->display_count();
            for (int i = 0; i < n; ++i) {
                backend_->set(i, spec);
                ++affected;
            }
        } else {
            backend_->set(display, spec);
            affected = 1;
        }

        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{affected},
            std::string("mode=") + scale_mode_to_string(mode));
    }

    ds::command::CommandResult handle_get(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();

        // 選填 display：缺席 → 0（主顯示器）；型別錯 → Failed；超出範圍 → Failed。
        int display = 0;
        if (args.has("display")) {
            const auto d = args.get_int("display");
            if (!d) {
                return ds::command::CommandResult::make_failed(
                    "wallpaper.get: 'display' must be an integer");
            }
            display = static_cast<int>(*d);
        }
        if (display < 0 || display >= backend_->display_count()) {
            return ds::command::CommandResult::make_failed(
                "wallpaper.get: 'display' out of range");
        }

        const auto spec = backend_->get(display);
        if (!spec) {
            // 未設定：Ok + Null 值 + "unset"（查詢不算失敗）。
            return ds::command::CommandResult::make_ok(ds::command::CommandValue{}, "unset");
        }
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{spec->path}, scale_mode_to_string(spec->mode));
    }

    // 便捷查詢：指定顯示器目前桌布規格（不經匯流排，供呼叫端 / 測試內省）。
    // 無後端 / 未設定回 std::nullopt。
    std::optional<WallpaperSpec> current_spec(int display = 0) const {
        if (!backend_) return std::nullopt;
        if (display < 0 || display >= backend_->display_count()) return std::nullopt;
        return backend_->get(display);
    }

private:
    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("wallpaper: no backend bound");
    }

    std::shared_ptr<WallpaperBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_10_WALLPAPER_ACTUATOR_HPP
