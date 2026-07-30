// E3-06 亮度設定致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「螢幕亮度設定」這組副作用（設定亮度 0–100、相對增減、查詢目前亮度，
// 皆可指定顯示器索引以支援多顯示器），以具名命令掛上 E6-01 命令匯流排
// （`brightness.set` / `brightness.get` / `brightness.adjust`）。呼叫端只需
// 命令 id + 具名參數即可觸發，不需相依本致動器或任何 OS 亮度 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01 契約
// （與已合併的 E3-05 音量設定、E3-02 一致的「注入式後端 + null 樣式」範式）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 亮度 API**（無 IOKit / DisplayServices /
//     IODisplay / cocoa / win32 等）。所有亮度狀態交由可抽換的 `BrightnessBackend` 承接；
//     預設 `NullBrightnessBackend` 以純記憶體狀態（每顯示器索引一個亮度值）模擬，供
//     測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端（win32 / cocoa）時，本致動器
//     與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實亮度 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（範圍夾限 0–100、顯示器索引驗證、經 E6-01 分派、結果回報）與後端解耦：
// 夾限與參數驗證在致動器層完成，後端只承接「已合法化」的意圖（顯示器索引 + 已夾限亮度），
// 因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_06_BRIGHTNESS_ACTUATOR_HPP
#define DS_ACTUATORS_E3_06_BRIGHTNESS_ACTUATOR_HPP

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 命名加 `brightness_` 前綴，避免與同命名空間 ds::actuators 其他致動器（如 e3_05 的
// volume_contract_version、e3_02 的 contract_version）符號衝突（可能同時連入同一 target）。
const char* brightness_contract_version() noexcept;

// 三個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdBrightnessSet    = "brightness.set";     // 必填 level（int 0–100），選填 display
inline constexpr const char* kCmdBrightnessGet    = "brightness.get";     // 選填 display，回目前亮度
inline constexpr const char* kCmdBrightnessAdjust = "brightness.adjust";  // 必填 delta（int，可負），選填 display

// 亮度合法範圍（相對值域，非絕對座標 / z-order）：0 = 最暗，100 = 最亮。
inline constexpr int kBrightnessMin = 0;
inline constexpr int kBrightnessMax = 100;

// 預設顯示器索引（未指定 `display` 參數時採用）。索引為相對序號、非絕對座標。
inline constexpr int kDisplayDefault = 0;

// 把任意整數夾限至 [kBrightnessMin, kBrightnessMax]。致動器層唯一的「合法化」點。
inline int clamp_brightness(int level) noexcept {
    return std::max(kBrightnessMin, std::min(kBrightnessMax, level));
}

// ---------------------------------------------------------------------------
// BrightnessState — 平台中立地描述一次亮度查詢的結果。
//
// 不含任何 OS handle / 裝置語意；只承載「顯示器索引 + 目前亮度位階」。
// ---------------------------------------------------------------------------
struct BrightnessState {
    int display = kDisplayDefault;  // 顯示器索引，恆 >= 0
    int level = 0;                  // 目前亮度位階，恆在 [0,100]

    bool operator==(const BrightnessState& o) const noexcept {
        return display == o.display && level == o.level;
    }
    bool operator!=(const BrightnessState& o) const noexcept { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// BrightnessBackend — 執行實際亮度副作用的抽象後端。
//
// 相位 1 僅提供 NullBrightnessBackend；相位 2 由平台後端以真實亮度 API 實作。
// 介面刻意最小：以顯示器索引定址的 設定 / 查詢 兩組原語，致動器層以此組合出
// set/get/adjust。契約保證：傳入 set(display, level) 的 level 已由致動器夾限至 [0,100]，
// display 已驗證為 >= 0（後端可信任）。
// ---------------------------------------------------------------------------
class BrightnessBackend {
public:
    virtual ~BrightnessBackend() = default;

    // 設定指定顯示器的亮度位階（呼叫端保證 display >= 0、level 已夾限至 [0,100]）。
    virtual void set(int display, int level) = 0;
    // 查詢指定顯示器目前亮度位階（呼叫端保證 display >= 0）。
    virtual int get(int display) const = 0;
};

// ---------------------------------------------------------------------------
// NullBrightnessBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 每個顯示器索引各自維護一個亮度值（std::map，遍歷決定性）；未曾設定過的顯示器回
// 可注入的預設亮度（預設 50）。讓致動器在無真實平台後端時仍可完整跑通（設定 → 查詢
// 一致、多顯示器互不干擾、增減），並讓測試 / 診斷驗證狀態一致性。
// ---------------------------------------------------------------------------
class NullBrightnessBackend : public BrightnessBackend {
public:
    NullBrightnessBackend() = default;
    // 初值可注入：未曾設定過的顯示器回此預設亮度（自夾限至 [0,100]）。
    explicit NullBrightnessBackend(int default_level)
        : default_level_(clamp_brightness(default_level)) {}

    void set(int display, int level) override { levels_[display] = clamp_brightness(level); }

    int get(int display) const override {
        auto it = levels_.find(display);
        return it == levels_.end() ? default_level_ : it->second;
    }

    // 內省：某顯示器的完整狀態（供測試 / 診斷）。
    BrightnessState state(int display = kDisplayDefault) const {
        return BrightnessState{display, get(display)};
    }

    // 內省：已明確設定過的顯示器索引（有序）。未設定過者不列入（回預設）。
    std::vector<int> known_displays() const {
        std::vector<int> ids;
        ids.reserve(levels_.size());
        for (const auto& kv : levels_) ids.push_back(kv.first);
        return ids;  // std::map 已排序
    }

    // 未設定顯示器的預設亮度。
    int default_level() const noexcept { return default_level_; }

private:
    int default_level_ = 50;      // 未曾設定過的顯示器回此值（預設 50）
    std::map<int, int> levels_;   // 顯示器索引 → 記憶體模擬的亮度位階
};

// ---------------------------------------------------------------------------
// BrightnessActuator — 把三個具名命令掛上 E6-01 命令匯流排的亮度致動器。
//
// 建構時綁定一個 BrightnessBackend（相位 1 為 NullBrightnessBackend）。register_on(bus)
// 將 brightness.set / brightness.get / brightness.adjust 註冊到匯流排；呼叫端之後只需
// bus.dispatch("brightness.set", args) 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_int 保護）：
//   - brightness.set   ：必填 `level`（int）；選填 `display`（int，預設 0）。
//                        level 超出 [0,100] 自動夾限（不失敗）。回夾限後亮度。
//   - brightness.get   ：選填 `display`（int，預設 0）。回該顯示器目前亮度（int）。
//   - brightness.adjust：必填 `delta`（int，可負）；選填 `display`（int，預設 0）。
//                        套用後夾限至 [0,100]。回夾限後亮度。
// 缺 / 型別錯的必填參數，或 `display` 型別錯 / 為負 → 回 CommandResult{Failed}
// （不崩潰、不丟例外、不改後端狀態）。所有成功結果的 value 皆為夾限後的目前亮度（int），
// 訊息帶顯示器索引，供呼叫端 / 測試驗證。
// ---------------------------------------------------------------------------
class BrightnessActuator {
public:
    explicit BrightnessActuator(std::shared_ptr<BrightnessBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullBrightnessBackend（相位 1）。
    BrightnessActuator() : backend_(std::make_shared<NullBrightnessBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<BrightnessBackend>& backend() const noexcept { return backend_; }

    // 將三個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_set = bus.register_command(
            kCmdBrightnessSet, [self](const ds::command::CommandArgs& a) {
                return self->handle_set(a);
            });
        const bool ok_get = bus.register_command(
            kCmdBrightnessGet, [self](const ds::command::CommandArgs& a) {
                return self->handle_get(a);
            });
        const bool ok_adjust = bus.register_command(
            kCmdBrightnessAdjust, [self](const ds::command::CommandArgs& a) {
                return self->handle_adjust(a);
            });
        if (ok_set && ok_get && ok_adjust) return true;
        // 回滾：只移除本次成功掛上的。
        if (ok_set) bus.unregister(kCmdBrightnessSet);
        if (ok_get) bus.unregister(kCmdBrightnessGet);
        if (ok_adjust) bus.unregister(kCmdBrightnessAdjust);
        return false;
    }

    // 從匯流排移除三個具名命令。回傳確有移除的數量（0..3）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdBrightnessSet) ? 1 : 0;
        n += bus.unregister(kCmdBrightnessGet) ? 1 : 0;
        n += bus.unregister(kCmdBrightnessAdjust) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_set(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        const DisplayResolve dr = resolve_display(args);
        if (!dr.ok) return ds::command::CommandResult::make_failed("brightness.set: " + dr.error);
        if (!args.has("level")) {
            return ds::command::CommandResult::make_failed("brightness.set: missing 'level'");
        }
        const auto level = args.get_int("level");
        if (!level) {
            return ds::command::CommandResult::make_failed(
                "brightness.set: 'level' must be an integer");
        }
        const int clamped = clamp_brightness(static_cast<int>(*level));
        backend_->set(dr.display, clamped);
        return ok_with_level(dr.display, clamped);
    }

    ds::command::CommandResult handle_get(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        const DisplayResolve dr = resolve_display(args);
        if (!dr.ok) return ds::command::CommandResult::make_failed("brightness.get: " + dr.error);
        return ok_with_level(dr.display, clamp_brightness(backend_->get(dr.display)));
    }

    ds::command::CommandResult handle_adjust(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        const DisplayResolve dr = resolve_display(args);
        if (!dr.ok) return ds::command::CommandResult::make_failed("brightness.adjust: " + dr.error);
        if (!args.has("delta")) {
            return ds::command::CommandResult::make_failed("brightness.adjust: missing 'delta'");
        }
        const auto delta = args.get_int("delta");
        if (!delta) {
            return ds::command::CommandResult::make_failed(
                "brightness.adjust: 'delta' must be an integer");
        }
        // 以 int64 相加後夾限，避免 int 溢位；結果落回 [0,100]。
        const std::int64_t next =
            static_cast<std::int64_t>(clamp_brightness(backend_->get(dr.display))) + *delta;
        const int clamped = clamp_brightness(next > kBrightnessMax ? kBrightnessMax
                                             : next < kBrightnessMin ? kBrightnessMin
                                                                     : static_cast<int>(next));
        backend_->set(dr.display, clamped);
        return ok_with_level(dr.display, clamped);
    }

    // 便捷查詢：某顯示器目前完整狀態（顯示器索引 + 亮度）。不經匯流排，供呼叫端 / 測試內省。
    // display < 0 或無後端回預設 BrightnessState{}（不觸碰後端）。
    BrightnessState current_state(int display = kDisplayDefault) const {
        if (!backend_ || display < 0) return BrightnessState{};
        return BrightnessState{display, clamp_brightness(backend_->get(display))};
    }

private:
    // 顯示器索引解析結果：ok 為真時 display 有效；否則 error 帶原因（不含命令前綴）。
    struct DisplayResolve {
        bool ok = true;
        int display = kDisplayDefault;
        std::string error{};
    };

    // 從 args 解析 `display`：缺省用預設 0；型別錯或為負則回錯（致動器層驗證，不下推後端）。
    static DisplayResolve resolve_display(const ds::command::CommandArgs& args) {
        if (!args.has("display")) return DisplayResolve{true, kDisplayDefault, {}};
        const auto d = args.get_int("display");
        if (!d) return DisplayResolve{false, 0, "'display' must be an integer"};
        if (*d < 0) return DisplayResolve{false, 0, "'display' must be >= 0"};
        return DisplayResolve{true, static_cast<int>(*d), {}};
    }

    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("brightness: no backend bound");
    }

    // 成功結果統一帶「夾限後的目前亮度」為回傳值，訊息帶顯示器索引供診斷。
    static ds::command::CommandResult ok_with_level(int display, int level) {
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{level},
            "display=" + std::to_string(display));
    }

    std::shared_ptr<BrightnessBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_06_BRIGHTNESS_ACTUATOR_HPP
