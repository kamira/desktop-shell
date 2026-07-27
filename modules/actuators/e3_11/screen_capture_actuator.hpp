// E3-11 螢幕擷取致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「螢幕截圖」這個副作用（全螢幕 / 指定區域 / 指定視窗擷取，輸出影像參照或
// 存檔路徑），以具名命令掛上 E6-01 命令匯流排（`screen.capture.full` /
// `screen.capture.region` / `screen.capture.window`）。呼叫端只需 命令 id + 具名參數
// 即可觸發，不需相依本致動器或任何 OS 擷取 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層），消費 E6-01 契約。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實螢幕擷取 API**（無 CGWindowListCreateImage /
//     `#ifdef` / cocoa / win32）。所有擷取請求交由可抽換的 `ScreenCaptureBackend` 承接，
//     預設 `NullScreenCaptureBackend` 只回一張「注入的假影像」（並記錄請求）供測試 / 診斷驗證，
//     絕不觸碰螢幕。相位 2 換上真實後端（win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實擷取呼叫；唯一 `#ifndef` 為 header guard。
//
// 因此可完全以單元測試驗證：命令註冊到匯流排、dispatch 觸發後端、全螢幕 / 區域 / 視窗
// 參數傳遞、null 後端回假影像、無效區域回結構化失敗（不崩潰）、結果尺寸回報。
#ifndef DS_ACTUATORS_E3_11_SCREEN_CAPTURE_ACTUATOR_HPP
#define DS_ACTUATORS_E3_11_SCREEN_CAPTURE_ACTUATOR_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// **前綴命名**避免與同命名空間其他致動器（如 E3-02 的 contract_version）符號衝突。
const char* screen_capture_contract_version() noexcept;

// 三個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdCaptureFullScreen = "screen.capture.full";
inline constexpr const char* kCmdCaptureRegion     = "screen.capture.region";
inline constexpr const char* kCmdCaptureWindow     = "screen.capture.window";

// ---------------------------------------------------------------------------
// CaptureKind / CaptureRegion / CaptureSpec — 平台中立地描述一次「螢幕擷取」意圖。
//
// 不含任何 OS handle / 螢幕座標語意假設；只承載呼叫端表達的目標與參數，讓相位 2 的
// 真實後端（或相位 1 的 null 後端）自行決定如何實現。
// ---------------------------------------------------------------------------
enum class CaptureKind {
    FullScreen,  // 全螢幕擷取（display = 選用顯示器索引）
    Region,      // 指定矩形區域擷取（region = x/y/width/height）
    Window,      // 指定視窗擷取（window = 視窗 id / 標題）
};

// 擷取區域（平台中立矩形）。width/height 必須為正才有效。
struct CaptureRegion {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;

    // 有效區域：寬高皆為正（x/y 可為負以支援多螢幕虛擬座標，由後端解讀）。
    bool valid() const noexcept { return width > 0 && height > 0; }

    bool operator==(const CaptureRegion& o) const noexcept {
        return x == o.x && y == o.y && width == o.width && height == o.height;
    }
    bool operator!=(const CaptureRegion& o) const noexcept { return !(*this == o); }
};

struct CaptureSpec {
    CaptureKind kind = CaptureKind::FullScreen;
    std::int64_t display = 0;    // FullScreen：顯示器索引（預設 0 = 主螢幕）
    CaptureRegion region;        // 僅 Region 使用
    std::string window;          // 僅 Window 使用：視窗 id / 標題
    std::string save_path;       // 選用：非空表示輸出到檔案路徑；空表示回記憶體影像參照

    bool operator==(const CaptureSpec& o) const {
        return kind == o.kind && display == o.display && region == o.region &&
               window == o.window && save_path == o.save_path;
    }
    bool operator!=(const CaptureSpec& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// CaptureResult — 一次擷取的結果（平台中立）。
//
// 承載結果尺寸與「影像參照 或 存檔路徑」二擇一：save_path 有給則輸出到檔案（path 非空），
// 否則回記憶體影像參照（image_ref 非空）。相位 2 真實後端與相位 1 null 後端回同一形狀。
// ---------------------------------------------------------------------------
struct CaptureResult {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::string image_ref;   // 記憶體影像參照（不透明 handle / id）；輸出到檔案時為空
    std::string path;        // 存檔路徑；回記憶體參照時為空

    bool ok() const noexcept { return width > 0 && height > 0; }

    bool operator==(const CaptureResult& o) const {
        return width == o.width && height == o.height &&
               image_ref == o.image_ref && path == o.path;
    }
    bool operator!=(const CaptureResult& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// ScreenCaptureBackend — 執行實際擷取的抽象後端。
//
// 相位 1 僅提供 NullScreenCaptureBackend；相位 2 由平台後端實作 capture() 真的擷取螢幕。
// capture() 回 CaptureResult（尺寸 + image_ref / path），讓致動器組出一致的 E6-01 結果。
// ---------------------------------------------------------------------------
class ScreenCaptureBackend {
public:
    virtual ~ScreenCaptureBackend() = default;
    virtual CaptureResult capture(const CaptureSpec& spec) = 0;
};

// ---------------------------------------------------------------------------
// NullScreenCaptureBackend — 相位 1 預設後端：不觸碰螢幕，只回「注入的假影像」並記錄請求。
//
// 讓致動器在無真實平台後端時仍可完整跑通（註冊 → 分派 → 回假影像），並讓測試 / 診斷
// 驗證「呼叫端到底請求了什麼、拿回什麼尺寸」。行為：
//   - Region：結果尺寸 = 請求區域寬高（後端遵循請求區域）。
//   - FullScreen / Window：結果尺寸 = 注入的假影像尺寸（null 不知真實螢幕大小）。
//   - save_path 有給 → 結果帶 path（null 不真的寫檔）；否則帶注入的 image_ref。
// **絕不呼叫任何真實擷取 API。**
// ---------------------------------------------------------------------------
class NullScreenCaptureBackend : public ScreenCaptureBackend {
public:
    NullScreenCaptureBackend() = default;

    // 注入假影像（尺寸 + 參照），供 FullScreen / Window 擷取回報。
    NullScreenCaptureBackend(std::int64_t fake_width, std::int64_t fake_height,
                             std::string fake_ref)
        : fake_width_(fake_width),
          fake_height_(fake_height),
          fake_ref_(std::move(fake_ref)) {}

    CaptureResult capture(const CaptureSpec& spec) override {
        specs_.push_back(spec);
        CaptureResult r;
        if (spec.kind == CaptureKind::Region) {
            // 遵循請求區域尺寸（呼叫端已於致動器層驗證區域有效）。
            r.width = spec.region.width;
            r.height = spec.region.height;
        } else {
            // 全螢幕 / 視窗：回注入的假影像尺寸。
            r.width = fake_width_;
            r.height = fake_height_;
        }
        if (!spec.save_path.empty()) {
            r.path = spec.save_path;   // null：不真的寫檔，只回報「將存於此」。
        } else {
            r.image_ref = fake_ref_;   // 回記憶體假影像參照。
        }
        results_.push_back(r);
        return r;
    }

    // 覆寫注入的假影像（FullScreen / Window 用）。
    void set_fake_image(std::int64_t width, std::int64_t height, std::string ref) {
        fake_width_ = width;
        fake_height_ = height;
        fake_ref_ = std::move(ref);
    }

    // 內省：已記錄的請求 / 結果（依發生序）。供測試 / 診斷。
    const std::vector<CaptureSpec>& specs() const noexcept { return specs_; }
    const std::vector<CaptureResult>& results() const noexcept { return results_; }
    std::size_t count() const noexcept { return specs_.size(); }
    bool empty() const noexcept { return specs_.empty(); }
    void clear() noexcept {
        specs_.clear();
        results_.clear();
    }

    // 最近一次請求 / 結果（無記錄回 nullptr）。
    const CaptureSpec* last_spec() const noexcept {
        return specs_.empty() ? nullptr : &specs_.back();
    }
    const CaptureResult* last_result() const noexcept {
        return results_.empty() ? nullptr : &results_.back();
    }

private:
    std::int64_t fake_width_ = 1920;
    std::int64_t fake_height_ = 1080;
    std::string fake_ref_ = "null-screen-capture";
    std::vector<CaptureSpec> specs_;
    std::vector<CaptureResult> results_;
};

// ---------------------------------------------------------------------------
// ScreenCaptureActuator — 把三個具名命令掛上 E6-01 命令匯流排的致動器。
//
// 建構時綁定一個 ScreenCaptureBackend（相位 1 為 NullScreenCaptureBackend）。
// register_on(bus) 將 screen.capture.full / .region / .window 註冊到匯流排；呼叫端之後
// 只需 bus.dispatch("screen.capture.region", args) 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/getter 保護）：
//   - screen.capture.full  ：選用 `display`（int，顯示器索引，預設 0）；選用 `path`（str，存檔路徑）。
//   - screen.capture.region：必填 `x` `y` `width` `height`（int）；`width`/`height` 須為正；選用 `path`。
//   - screen.capture.window：必填 `window`（str，視窗 id / 標題）；選用 `path`。
// 缺必填參數 / 型別不符 / 無效區域 → 回 CommandResult{Failed}（不崩潰、不丟例外、不記錄）。
// 成功 → 回 CommandResult{Ok}，value = image_ref 或 path，message 含結果尺寸（WxH）。
// ---------------------------------------------------------------------------
class ScreenCaptureActuator {
public:
    explicit ScreenCaptureActuator(std::shared_ptr<ScreenCaptureBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullScreenCaptureBackend（相位 1）。
    ScreenCaptureActuator()
        : backend_(std::make_shared<NullScreenCaptureBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<ScreenCaptureBackend>& backend() const noexcept { return backend_; }

    // 將三個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）則回滾
    // 已註冊者並回 false（不留半掛狀態、不遮蔽既有其他致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_full = bus.register_command(
            kCmdCaptureFullScreen, [self](const ds::command::CommandArgs& a) {
                return self->handle_capture_full(a);
            });
        const bool ok_region = bus.register_command(
            kCmdCaptureRegion, [self](const ds::command::CommandArgs& a) {
                return self->handle_capture_region(a);
            });
        const bool ok_window = bus.register_command(
            kCmdCaptureWindow, [self](const ds::command::CommandArgs& a) {
                return self->handle_capture_window(a);
            });
        if (ok_full && ok_region && ok_window) return true;
        // 回滾：只移除本次成功掛上的。
        if (ok_full) bus.unregister(kCmdCaptureFullScreen);
        if (ok_region) bus.unregister(kCmdCaptureRegion);
        if (ok_window) bus.unregister(kCmdCaptureWindow);
        return false;
    }

    // 從匯流排移除三個具名命令。回傳確有移除的數量（0..3）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdCaptureFullScreen) ? 1 : 0;
        n += bus.unregister(kCmdCaptureRegion) ? 1 : 0;
        n += bus.unregister(kCmdCaptureWindow) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_capture_full(const ds::command::CommandArgs& args) {
        CaptureSpec spec;
        spec.kind = CaptureKind::FullScreen;
        if (args.has("display")) {
            const auto d = args.get_int("display");
            if (!d) {
                return ds::command::CommandResult::make_failed(
                    "screen.capture.full: 'display' must be an integer");
            }
            if (*d < 0) {
                return ds::command::CommandResult::make_failed(
                    "screen.capture.full: 'display' must be >= 0");
            }
            spec.display = *d;
        }
        apply_optional_path(args, spec);
        return dispatch_to_backend(spec);
    }

    ds::command::CommandResult handle_capture_region(const ds::command::CommandArgs& args) {
        static constexpr const char* keys[] = {"x", "y", "width", "height"};
        std::int64_t vals[4];
        for (int i = 0; i < 4; ++i) {
            if (!args.has(keys[i])) {
                return ds::command::CommandResult::make_failed(
                    std::string{"screen.capture.region: missing '"} + keys[i] + "'");
            }
            const auto v = args.get_int(keys[i]);
            if (!v) {
                return ds::command::CommandResult::make_failed(
                    std::string{"screen.capture.region: '"} + keys[i] + "' must be an integer");
            }
            vals[i] = *v;
        }
        CaptureSpec spec;
        spec.kind = CaptureKind::Region;
        spec.region = CaptureRegion{vals[0], vals[1], vals[2], vals[3]};
        if (!spec.region.valid()) {
            return ds::command::CommandResult::make_failed(
                "screen.capture.region: invalid region (width/height must be positive)");
        }
        apply_optional_path(args, spec);
        return dispatch_to_backend(spec);
    }

    ds::command::CommandResult handle_capture_window(const ds::command::CommandArgs& args) {
        if (!args.has("window")) {
            return ds::command::CommandResult::make_failed("screen.capture.window: missing 'window'");
        }
        const auto window = args.get_string("window");
        if (!window || window->empty()) {
            return ds::command::CommandResult::make_failed(
                "screen.capture.window: 'window' must be a non-empty string");
        }
        CaptureSpec spec;
        spec.kind = CaptureKind::Window;
        spec.window = *window;
        apply_optional_path(args, spec);
        return dispatch_to_backend(spec);
    }

private:
    // 選用 `path` 參數：若存在且為字串則設為 save_path（型別不符則忽略，維持記憶體參照）。
    static void apply_optional_path(const ds::command::CommandArgs& args, CaptureSpec& spec) {
        if (args.has("path")) {
            if (const auto p = args.get_string("path")) spec.save_path = *p;
        }
    }

    ds::command::CommandResult dispatch_to_backend(const CaptureSpec& spec) {
        if (!backend_) {
            return ds::command::CommandResult::make_failed("no backend bound");
        }
        const CaptureResult r = backend_->capture(spec);
        if (!r.ok()) {
            return ds::command::CommandResult::make_failed("capture failed: empty result");
        }
        // 結果尺寸回報：message 含 WxH；value 帶影像參照或存檔路徑（二擇一）。
        const std::string dims = std::to_string(r.width) + "x" + std::to_string(r.height);
        if (!r.path.empty()) {
            return ds::command::CommandResult::make_ok(
                ds::command::CommandValue{r.path}, "captured " + dims + " -> " + r.path);
        }
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{r.image_ref}, "captured " + dims + " (image_ref)");
    }

    std::shared_ptr<ScreenCaptureBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_11_SCREEN_CAPTURE_ACTUATOR_HPP
