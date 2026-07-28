// E7-08 設定遷移與版本相容 — 宣告式設定的版本升級鏈（平台中立 / engine 層）
//
// 本單元回答一個問題：當一份宣告式設定文件的 **`format_version` 落後**於實作能理解的
// 版本時，如何把舊版結構安全升級到新版？答案是**遷移鏈**——註冊一連串「從版本 X 升級到
// 版本 Y」的遷移步驟；`migrate()` 依文件的 `FormatVersion` 在這些步驟間找出一條可達目標
// 版本的路徑，依序套用其 `transform`，產出升級後的 `Value` 與新版本標記。
//
// 消費上游（可讀不可改）：
//   - E7-01 `engine/format/e7_01/`：`Value` 多型資料模型、`FormatVersion`（major.minor）、
//     `Document`（version + root）。遷移即對 `Value`（設定樹）做結構轉換。
//   - E9-02 `engine/package/e9_02/`：帶版本欄位的 manifest 格式（同一「格式帶版本」策略的
//     另一個應用）；本單元與其同屬描述子系統的版本相容策略。
//
// 設計原則（承 E7-01 檔首與 NFR-04 精神）：
//   - **平台中立、純邏輯**：無 `#ifdef`、系統呼叫或真實後端。engine 層換平台一行不動。
//   - **不得靜默失敗**：無可用遷移路徑、文件版本比目標新（過新）、遷移鏈斷裂——一律回傳
//     帶明確原因與版本資訊的 `MigrateError`，絕不安靜回退或吞掉。
//   - **冪等的最新版**：文件已是目標版本 → no-op（不套用任何步驟），仍回成功但標記未變更。
//
// 版本語意（與 E7-01 一致）：`FormatVersion = major.minor`，序關係為 (major, minor) 字典序。
// 遷移步驟必為嚴格上升（`from < to`）；`migrate` 只沿註冊邊由低版本往高版本推進，直到抵達
// 目標版本為止。文件版本**高於**目標 = 過新（實作無法降級），明確報錯。
#ifndef DS_ENGINE_E7_08_MIGRATION_HPP
#define DS_ENGINE_E7_08_MIGRATION_HPP

#include <functional>
#include <string>
#include <vector>

#include "document.hpp"  // E7-01：Value / FormatVersion / Document

namespace ds::format {

// -----------------------------------------------------------------------------
// 版本序關係（FormatVersion 僅提供 == / !=；此處補上排序，供路徑推進使用）
// -----------------------------------------------------------------------------

// (major, minor) 字典序小於。
bool version_less(const FormatVersion& a, const FormatVersion& b) noexcept;

// 人類可讀版本字串 "major.minor"（供錯誤訊息）。
std::string version_to_string(const FormatVersion& v);

// -----------------------------------------------------------------------------
// 單一遷移步驟
// -----------------------------------------------------------------------------

// 一個遷移步驟：把設定樹（root `Value`）從 `from` 版本升級到 `to` 版本。
//   - `transform`：純結構轉換（輸入舊版 root、回傳新版 root）。應為純函式、平台中立、
//     不觸碰系統狀態。允許為空（表示結構相容、僅需更版本標記）。
//   - 約束：`from < to`（嚴格上升）；`register_migration` 會驗證並拒絕非法步驟。
struct Migration {
    FormatVersion from;
    FormatVersion to;
    std::function<Value(const Value&)> transform;  // 可為空 = 恆等（僅升版本標記）
};

// -----------------------------------------------------------------------------
// 遷移結果
// -----------------------------------------------------------------------------

enum class MigrateStatus {
    Ok,       // 成功（含「已是目標版本」的 no-op）
    NoPath,   // 找不到由文件版本抵達目標版本的遷移鏈
    TooNew,   // 文件版本比目標版本新（過新）——實作不降級
    Invalid,  // 註冊或呼叫端契約違反（如遷移步驟非嚴格上升）
};

// 遷移失敗描述——一律帶明確原因與涉及的版本（不靜默）。
struct MigrateError {
    MigrateStatus status = MigrateStatus::NoPath;
    std::string message;      // 人類可讀原因
    FormatVersion from;       // 文件原始版本
    FormatVersion target;     // 要求的目標版本
};

// 遷移結果：成功則持有升級後 Value + 結果版本標記；失敗則持有 MigrateError。二者互斥。
class MigrateResult {
public:
    static MigrateResult success(Value value, FormatVersion version, bool changed);
    static MigrateResult failure(MigrateError error);

    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    // 僅在 ok() 為 true 時有效。
    const Value& value() const { return value_; }
    // 升級後的版本標記（成功時等於目標版本）。
    const FormatVersion& version() const noexcept { return version_; }
    // 是否真正套用了至少一個遷移步驟（false = 已是目標版本的 no-op）。
    bool migrated() const noexcept { return changed_; }

    // 僅在 ok() 為 false 時有效。
    const MigrateError& error() const { return error_; }

private:
    MigrateResult() = default;
    bool ok_ = false;
    bool changed_ = false;
    Value value_;
    FormatVersion version_;
    MigrateError error_;
};

// -----------------------------------------------------------------------------
// 遷移登錄：註冊步驟鏈，依版本找路徑並套用
// -----------------------------------------------------------------------------

class MigrationRegistry {
public:
    // 註冊一個遷移步驟。`from` 須嚴格小於 `to`，否則回傳 false 且不登錄（契約違反不靜默）。
    // 允許同一 `from` 有多條外向邊（分支鏈）；重複的 (from,to) 亦允許（先註冊者先被採用）。
    bool register_migration(Migration migration);

    // 便捷註冊：等價於 register_migration({from, to, std::move(fn)})。
    bool add(FormatVersion from, FormatVersion to,
             std::function<Value(const Value&)> fn);

    // 已註冊步驟數。
    std::size_t size() const noexcept { return migrations_.size(); }

    // 是否存在由 `from` 抵達 `target` 的遷移鏈（不套用轉換，僅查可達性）。
    bool has_path(const FormatVersion& from, const FormatVersion& target) const;

    // 把設定樹 `root` 從 `current` 版本升級到 `target` 版本。
    //   - current == target：no-op，回成功（migrated()==false，version()==target）。
    //   - current  < target：沿註冊邊 BFS 找最短鏈；找到則依序套用 transform，回成功
    //     （migrated()==true，version()==target）；找不到 → failure(NoPath)。
    //   - current  > target：文件過新 → failure(TooNew)。
    MigrateResult migrate(const Value& root,
                          const FormatVersion& current,
                          const FormatVersion& target) const;

    // 便捷重載：以 Document 的 format_version 為來源版本、root 為設定樹，升級到 target。
    MigrateResult migrate(const Document& doc, const FormatVersion& target) const;

private:
    // 由 `from` 找出抵達 `target` 的步驟索引序列（BFS 最短）；回傳是否找到。
    bool find_path(const FormatVersion& from, const FormatVersion& target,
                   std::vector<std::size_t>& out_steps) const;

    std::vector<Migration> migrations_;
};

}  // namespace ds::format

#endif  // DS_ENGINE_E7_08_MIGRATION_HPP
