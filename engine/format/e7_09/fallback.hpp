// E7-09 缺席模組的降級解析 — 引用缺席模組時的優雅降級（平台中立 / engine 層）
//
// 描述子系統的宣告式文件（E7-01 `Value` 樹）常引用「模組」。當設定引用了一個
// **當前不存在 / 未安裝的模組**時，硬失敗會讓整份文件無法使用。本單元提供**優雅降級解析**：
//   - 偵測文件中引用了哪些缺席模組；
//   - 以**停用佔位節點**取代該模組所擁有的區塊（保留 module id，明確標記 disabled + 原因）；
//   - 產出一份仍可用的降級後 `Value`，外加一份「缺席模組 / 降級決策」清單供上層顯示。
//
// 降級決策**必須可見、不得靜默**（NFR-04 精神）：每一次以佔位取代都會產生一筆 `DegradeNote`
// （帶模組 id、樹中位置、人類可讀訊息），且所有缺席模組 id 會去重收進 `missing`。
//
// 模組引用慣例：一個 Map 節點若含字串鍵 `module`（見 `kModuleKey`），代表該區塊「由該模組擁有」。
// 是否存在由**可注入的 `ModuleAvailability`** 判定——可用明確集合（`ModuleSet`）、任意函式
// （`FnAvailability`），或由 E9-02 已安裝 manifest 清單建立（`available_from_manifests`）。
//
// 設計原則：
//   - **平台中立、純邏輯**：無任何 `#ifdef` / 系統呼叫 / 真實後端。engine 層換平台一行不動。
//   - **能力閘控有 `has()` 保護**（NFR-03 精神）：一律先 `avail.has(id)` 才視模組為存在。
//   - **不含絕對座標 / 數字 z-order**（NFR-02）。
#ifndef DS_ENGINE_E7_09_FALLBACK_HPP
#define DS_ENGINE_E7_09_FALLBACK_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "document.hpp"  // E7-01：ds::format::Value（可讀不可改的上游根契約）
#include "manifest.hpp"  // E9-02：ds::package::Manifest（模組清單來源）

namespace ds::format {

// 模組引用慣例欄位：Map 若含此字串鍵，該區塊即「由該 module id 擁有」。
inline constexpr const char* kModuleKey = "module";

// -----------------------------------------------------------------------------
// 可注入的模組可用性判定
// -----------------------------------------------------------------------------

// 判定某模組 id 目前是否存在 / 已安裝。以介面呈現，便於注入（測試 / 不同來源）。
class ModuleAvailability {
public:
    virtual ~ModuleAvailability() = default;
    virtual bool has(const std::string& module_id) const = 0;
};

// 以明確 id 集合為後盾的可用性（保序、去重）。
class ModuleSet : public ModuleAvailability {
public:
    ModuleSet() = default;
    explicit ModuleSet(std::vector<std::string> ids);

    void add(std::string id);
    bool has(const std::string& module_id) const override;
    std::size_t size() const noexcept { return ids_.size(); }

private:
    std::vector<std::string> ids_;
};

// 以任意函式為後盾的可用性——供呼叫端 / 測試注入任意判定邏輯。
class FnAvailability : public ModuleAvailability {
public:
    explicit FnAvailability(std::function<bool(const std::string&)> fn);
    bool has(const std::string& module_id) const override;

private:
    std::function<bool(const std::string&)> fn_;
};

// 由「已安裝的 E9-02 manifest 清單」建立可用集合：模組存在 iff 有一份 manifest 的 name 相符。
// 空 name 的 manifest 會被略過。這是與套件子系統（E9-02）的整合點。
ModuleSet available_from_manifests(const std::vector<ds::package::Manifest>& installed);

// -----------------------------------------------------------------------------
// 降級決策與結果（可見、不靜默）
// -----------------------------------------------------------------------------

// 單一降級決策——每一次「以佔位取代 / 偵測到缺席引用」都留下一筆，供上層顯示。
struct DegradeNote {
    std::string module_id;  // 被引用但缺席的模組 id。
    std::string path;       // 於 Value 樹中的位置（如 "root.layers[1]"）。
    std::string message;    // 人類可讀的降級決策。
};

// 降級解析結果。`value` 恆為可用（缺席區塊已換成停用佔位）；決策全數可見。
struct DegradeResult {
    Value value;                       // 降級後仍可用的 Value（缺席區塊 → 停用佔位）。
    std::vector<std::string> missing;  // 被引用的缺席模組 id（去重、保首見序）。
    std::vector<DegradeNote> notes;    // 逐次的降級決策（含巢狀）。

    // 是否發生任何降級（等價於 notes 非空）。
    bool degraded() const noexcept { return !notes.empty(); }
};

// 缺席模組的降級解析。
//   - 走訪 `doc`（E7-01 `Value` 樹，通常為 Document.root）。
//   - 引用**可用**模組的區塊：原樣保留，並**繼續遞迴**處理其巢狀引用。
//   - 引用**缺席**模組的區塊：以停用佔位節點取代（保留 module id + `disabled: true` + 原因），
//     並仍掃描其子樹以完整回報巢狀缺席引用（僅回報，不入值）。
//   - 一律先 `avail.has(id)` 才視模組為存在（能力閘控保護）。
// 回傳降級後的 `value` + 缺席清單 + 決策清單；不硬失敗、不靜默。
DegradeResult resolve_with_fallback(const Value& doc, const ModuleAvailability& avail);

}  // namespace ds::format

#endif  // DS_ENGINE_E7_09_FALLBACK_HPP
