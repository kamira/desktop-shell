// E1-19 顯示器熱插拔復原 — 實作
//
// 僅含平台中立的拓撲簽章導出邏輯（管理器本體為標頭內樣板）。此檔不含任何平台分支、
// 真實顯示器 API 或絕對座標。
#include "layout_recovery.hpp"

#include <algorithm>

namespace ds::kernel {

std::string topology_key(const ScreenRegistry& topology) {
    // 僅以具名 ScreenId 構成簽章（NFR-02：無座標 / index / z-order）。
    // 先取具名列舉，排序使簽章與後端列舉順序無關——「拔除後重接同一組螢幕」不論
    // 以何順序列舉皆導出相同簽章，故拓撲恢復可被辨識並據以還原對應組態佈局。
    std::vector<ScreenId> ids = topology.ids();
    std::sort(ids.begin(), ids.end());

    std::string key;
    for (const auto& id : ids) {
        key += id;
        key += '|';  // 具名識別碼間的分隔符；純具名、非座標。
    }
    return key;  // 空拓撲 → 空字串。
}

}  // namespace ds::kernel
