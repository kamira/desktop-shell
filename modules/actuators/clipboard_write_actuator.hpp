// E3-07 剪貼簿寫入致動器 — 平台中立契約（相位 1：介面 + null 後端）
//
// 語意：把「寫入系統剪貼簿」這組副作用（寫入文字、寫入多資料型別如純文字 / HTML /
// 影像參照、清空剪貼簿），以具名命令掛上 E6-01 命令匯流排
// （`clipboard.write` / `clipboard.clear` / `clipboard.read`）。呼叫端只需 命令 id +
// 具名參數 即可觸發，不需相依本致動器或任何 OS 剪貼簿 API。
//
// 分層 / 相位：本單元屬 modules/actuators（動作層 / 子系統 actuators），消費 E6-01 契約
// （與已合併的 E3-05 音量致動器一致，採「注入式後端 + null 樣式」範式）。
//   - 相位 1（Mac / null 期）：**絕不呼叫真實 OS 剪貼簿 API**（無 NSPasteboard /
//     OpenClipboard / SetClipboardData / cocoa / win32 等）。所有剪貼簿狀態交由可抽換的
//     `ClipboardBackend` 承接；預設 `NullClipboardBackend` 以純記憶體狀態模擬
//     （型別 + 內容字串），供測試 / 診斷驗證，絕不觸碰 OS。相位 2 換上真實後端
//     （win32 / cocoa）時，本致動器與命令契約一行不動。
//   - 無 `#ifdef` / 平台分支 / 真實剪貼簿 API；唯一 `#ifndef` 為 header guard。
//
// 致動器邏輯（型別 / 內容參數驗證、經 E6-01 分派、結果回報）與後端解耦：參數合法化在
// 致動器層完成，後端只承接「已合法化」的意圖（可 read() 供驗證），因此可完全以單元測試驗證。
#ifndef DS_ACTUATORS_E3_07_CLIPBOARD_WRITE_ACTUATOR_HPP
#define DS_ACTUATORS_E3_07_CLIPBOARD_WRITE_ACTUATOR_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "command_bus.hpp"  // E6-01：重用命令匯流排 / 穩定值型別 / 具名命令（PUBLIC 相依）

namespace ds::actuators {

// 擴充點契約版本標記（承重：呼叫端 / 相位 2 後端消費）。定義在 .cpp。
// 前綴 `clipboard_write_` 避免與同命名空間其他致動器（如 e3_05 的
// `volume_contract_version()`）的版本函式衝突。
const char* clipboard_write_contract_version() noexcept;

// 三個具名命令 id（穩定、可讀字串，不使用數字 opcode；與 E6-01 CommandId 取捨一致）。
inline constexpr const char* kCmdClipboardWrite = "clipboard.write";  // 必填 content(string)；選填 type
inline constexpr const char* kCmdClipboardClear = "clipboard.clear";  // 無參數，清空剪貼簿
inline constexpr const char* kCmdClipboardRead  = "clipboard.read";   // 無參數，回目前內容（供驗證）

// ---------------------------------------------------------------------------
// ClipboardContentType — 剪貼簿內容的資料型別（平台中立）。
//
// 不含任何 OS UTI / 格式碼；只承載抽象型別分類。相位 2 真實後端負責把此抽象型別
// 映射到平台格式（NSPasteboardType / CF_TEXT 等），本層不關心。
// ---------------------------------------------------------------------------
enum class ClipboardContentType {
    Empty,      // 空剪貼簿（清空後 / 尚未寫入）
    PlainText,  // 純文字
    Html,       // HTML 片段
    ImageRef,   // 影像參照（路徑 / 識別碼字串，非影像位元組本身）
};

// 型別 ↔ 穩定字串（用於命令參數 `type` 與結果訊息 / 診斷）。
inline const char* clipboard_type_name(ClipboardContentType t) noexcept {
    switch (t) {
        case ClipboardContentType::Empty:     return "empty";
        case ClipboardContentType::PlainText: return "text";
        case ClipboardContentType::Html:      return "html";
        case ClipboardContentType::ImageRef:  return "image";
    }
    return "empty";
}

// 由 `type` 參數字串解析型別；未知字串回 std::nullopt（致動器據此回 Failed）。
// 注意：`empty` 不是可寫入的型別（清空由 clipboard.clear 命令表達），故不接受。
inline std::optional<ClipboardContentType> clipboard_type_from_string(const std::string& s) {
    if (s == "text")  return ClipboardContentType::PlainText;
    if (s == "html")  return ClipboardContentType::Html;
    if (s == "image") return ClipboardContentType::ImageRef;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// ClipboardData — 平台中立地描述一次剪貼簿內容。
//
// 不含任何 OS handle / 格式碼；只承載「型別 + 內容字串」。Empty 型別時 content 為空字串。
// ---------------------------------------------------------------------------
struct ClipboardData {
    ClipboardContentType type = ClipboardContentType::Empty;
    std::string content{};  // 內容字串；影像型別時為影像參照（路徑 / 識別碼），非位元組

    bool empty() const noexcept { return type == ClipboardContentType::Empty; }

    bool operator==(const ClipboardData& o) const noexcept {
        return type == o.type && content == o.content;
    }
    bool operator!=(const ClipboardData& o) const noexcept { return !(*this == o); }

    // 工廠：清空狀態（型別 Empty、內容空）。
    static ClipboardData empty_data() { return ClipboardData{ClipboardContentType::Empty, {}}; }
};

// ---------------------------------------------------------------------------
// ClipboardBackend — 執行實際剪貼簿副作用的抽象後端。
//
// 相位 1 僅提供 NullClipboardBackend；相位 2 由平台後端以真實剪貼簿 API 實作。
// 介面刻意最小：寫入 / 清空 / 讀取三組原語，致動器層以此組合出 write/clear/read 命令。
// 契約保證：傳入 write(data) 的 data 已由致動器合法化（型別合法、content 非空）。
// read() 供驗證 / 診斷 / 相位 2 回讀。
// ---------------------------------------------------------------------------
class ClipboardBackend {
public:
    virtual ~ClipboardBackend() = default;

    // 寫入剪貼簿（呼叫端保證 data 已合法化：型別非 Empty、content 非空）。
    virtual void write(const ClipboardData& data) = 0;
    // 清空剪貼簿。
    virtual void clear() = 0;
    // 讀取目前剪貼簿內容（供驗證 / 診斷）。空時回 Empty 型別 + 空字串。
    virtual ClipboardData read() const = 0;
};

// ---------------------------------------------------------------------------
// NullClipboardBackend — 相位 1 預設後端：不觸碰 OS，以純記憶體狀態模擬。
//
// 讓致動器在無真實平台後端時仍可完整跑通（寫入 → read 一致、清空後為 Empty），
// 並讓測試 / 診斷驗證狀態一致性。初值可注入（預設空剪貼簿）。
// ---------------------------------------------------------------------------
class NullClipboardBackend : public ClipboardBackend {
public:
    NullClipboardBackend() = default;
    explicit NullClipboardBackend(ClipboardData initial) : data_(std::move(initial)) {}

    void write(const ClipboardData& data) override { data_ = data; }
    void clear() override { data_ = ClipboardData::empty_data(); }
    ClipboardData read() const override { return data_; }

    // 內省：完整狀態（供測試 / 診斷）。
    const ClipboardData& state() const noexcept { return data_; }

private:
    ClipboardData data_{};  // 記憶體模擬的剪貼簿狀態（預設 Empty）
};

// ---------------------------------------------------------------------------
// ClipboardWriteActuator — 把三個具名命令掛上 E6-01 命令匯流排的剪貼簿寫入致動器。
//
// 建構時綁定一個 ClipboardBackend（相位 1 為 NullClipboardBackend）。register_on(bus)
// 將 clipboard.write / clipboard.clear / clipboard.read 註冊到匯流排；呼叫端之後只需
// bus.dispatch("clipboard.write", args) 即可觸發，完全不需相依本型別。
//
// 命令參數契約（皆以 E6-01 CommandArgs 承載，必填參數以 has()/get_string 保護）：
//   - clipboard.write：必填 `content`（string，非空）；選填 `type`（string：
//     "text"/"html"/"image"，預設 "text"）。缺 / 型別錯 / 空 content / 無效 type
//     → Failed。成功回 value=寫入內容、message=型別名。
//   - clipboard.clear：無參數。清空剪貼簿；回 Ok（message="cleared"）。
//   - clipboard.read ：無參數。回目前內容（value=內容字串，message=型別名）。
// 缺 / 型別錯 / 空 / 無效的必填參數 → 回 CommandResult{Failed}（不崩潰、不丟例外、
// 不改後端狀態）。
// ---------------------------------------------------------------------------
class ClipboardWriteActuator {
public:
    explicit ClipboardWriteActuator(std::shared_ptr<ClipboardBackend> backend)
        : backend_(std::move(backend)) {}

    // 便捷建構：預設綁 NullClipboardBackend（相位 1）。
    ClipboardWriteActuator() : backend_(std::make_shared<NullClipboardBackend>()) {}

    // 綁定的後端（可為 null 檢查用）。
    const std::shared_ptr<ClipboardBackend>& backend() const noexcept { return backend_; }

    // 將三個具名命令註冊到匯流排。全部成功才回 true；任一失敗（如 id 已被占用）
    // 則回滾已註冊者並回 false（不留半掛狀態，不遮蔽既有致動器）。無後端一律回 false。
    bool register_on(ds::command::CommandBus& bus) {
        if (!backend_) return false;
        auto self = this;
        const bool ok_write = bus.register_command(
            kCmdClipboardWrite, [self](const ds::command::CommandArgs& a) {
                return self->handle_write(a);
            });
        const bool ok_clear = bus.register_command(
            kCmdClipboardClear, [self](const ds::command::CommandArgs& a) {
                return self->handle_clear(a);
            });
        const bool ok_read = bus.register_command(
            kCmdClipboardRead, [self](const ds::command::CommandArgs& a) {
                return self->handle_read(a);
            });
        if (ok_write && ok_clear && ok_read) return true;
        // 回滾：只移除本次成功掛上的。
        if (ok_write) bus.unregister(kCmdClipboardWrite);
        if (ok_clear) bus.unregister(kCmdClipboardClear);
        if (ok_read) bus.unregister(kCmdClipboardRead);
        return false;
    }

    // 從匯流排移除三個具名命令。回傳確有移除的數量（0..3）。
    std::size_t unregister_from(ds::command::CommandBus& bus) {
        std::size_t n = 0;
        n += bus.unregister(kCmdClipboardWrite) ? 1 : 0;
        n += bus.unregister(kCmdClipboardClear) ? 1 : 0;
        n += bus.unregister(kCmdClipboardRead) ? 1 : 0;
        return n;
    }

    // ---- 處理器（亦可直接呼叫，方便測試不經匯流排也能驗證語意）----

    ds::command::CommandResult handle_write(const ds::command::CommandArgs& args) {
        if (!backend_) return no_backend();
        // 必填 content（string）。
        if (!args.has("content")) {
            return ds::command::CommandResult::make_failed("clipboard.write: missing 'content'");
        }
        const auto content = args.get_string("content");
        if (!content) {
            return ds::command::CommandResult::make_failed(
                "clipboard.write: 'content' must be a string");
        }
        if (content->empty()) {
            return ds::command::CommandResult::make_failed(
                "clipboard.write: 'content' must not be empty (use clipboard.clear to empty)");
        }
        // 選填 type（string，預設 text）。存在則型別須為 string 且值合法。
        ClipboardContentType type = ClipboardContentType::PlainText;
        if (args.has("type")) {
            const auto type_str = args.get_string("type");
            if (!type_str) {
                return ds::command::CommandResult::make_failed(
                    "clipboard.write: 'type' must be a string");
            }
            const auto parsed = clipboard_type_from_string(*type_str);
            if (!parsed) {
                return ds::command::CommandResult::make_failed(
                    "clipboard.write: invalid 'type' (expected text/html/image)");
            }
            type = *parsed;
        }
        backend_->write(ClipboardData{type, *content});
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{*content}, clipboard_type_name(type));
    }

    ds::command::CommandResult handle_clear(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        backend_->clear();
        return ds::command::CommandResult::make_ok(ds::command::CommandValue{}, "cleared");
    }

    ds::command::CommandResult handle_read(const ds::command::CommandArgs&) {
        if (!backend_) return no_backend();
        const ClipboardData data = backend_->read();
        return ds::command::CommandResult::make_ok(
            ds::command::CommandValue{data.content}, clipboard_type_name(data.type));
    }

    // 便捷查詢：目前完整剪貼簿內容（型別 + 內容）。不經匯流排，供呼叫端 / 測試內省。
    ClipboardData current_data() const {
        if (!backend_) return ClipboardData::empty_data();
        return backend_->read();
    }

private:
    static ds::command::CommandResult no_backend() {
        return ds::command::CommandResult::make_failed("clipboard: no backend bound");
    }

    std::shared_ptr<ClipboardBackend> backend_;
};

}  // namespace ds::actuators

#endif  // DS_ACTUATORS_E3_07_CLIPBOARD_WRITE_ACTUATOR_HPP
