// E6-02 動作註冊表與前綴/模糊分派 — 平台中立契約（擴充點 3「動作」之查詢層）
//
// E6-01 提供命令匯流排（具名 id → 處理器）。本單元在其上建「動作註冊表」：
// 一個可查詢的已知命令索引，讓使用者以不完整 / 有錯字的輸入找到最相近命令，
// 再經 E6-01 CommandBus 實際分派。核心能力：
//   - 精確比對：輸入 == 命令 id。
//   - 前綴比對：輸入為 id 之前綴（如 `vol` → `volume.set`）。
//   - 子字串比對：輸入出現在 id 之中（如 `volume` → `audio.volume.set`）。
//   - 模糊 / 近似比對：允許少量錯字（編輯距離），仍找到最相近命令。
//   - 相關度排序：精確 > 前綴 > 子字串 > 模糊；同類再依完成度 / 距離細排。
//   - 歧義處理：多個並列最佳候選時，回候選清單而非亂猜。
//   - 找不到 / 歧義**明確回報**（NotFound / Ambiguous），絕不靜默、絕不崩潰。
//
// 本單元屬 engine 層（平台中立純邏輯）：無 `#ifdef`、無平台分支、無真實副作用。
// 註冊表本身只維護「有哪些命令 id」（可手動 add，或自 CommandBus 同步）；真正的
// 處理器與執行仍由 E6-01 CommandBus 承擔。dispatch_best 接收一個 CommandBus，
// 以解析出的最佳候選觸發之，讓查詢層與執行層鬆耦合、可各自測試。
#ifndef DS_COMMAND_E6_02_ACTION_REGISTRY_HPP
#define DS_COMMAND_E6_02_ACTION_REGISTRY_HPP

#include "command_bus.hpp"  // 上游 E6-01：ds::command::{CommandBus, CommandArgs, CommandResult, CommandId}

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace ds::command {

// 擴充點 3「動作」查詢層契約版本標記（實體符號定義在 .cpp）。
const char* action_registry_contract_version() noexcept;

// ---------------------------------------------------------------------------
// MatchKind — 一次比對命中的種類。用於相關度分級（值越大越相關）。
// ---------------------------------------------------------------------------
enum class MatchKind {
    Fuzzy = 0,      // 近似（編輯距離容錯）
    Substring = 1,  // 輸入為 id 之子字串（非前綴）
    Prefix = 2,     // 輸入為 id 之前綴
    Exact = 3,      // 輸入 == id
};

// ---------------------------------------------------------------------------
// Match — resolve() 回傳的單一候選。
//
// score 為整數相關度（越大越相關），刻意用整數以便歧義判斷做精確相等比較，
// 不受浮點誤差影響。排序：score 由大到小；同分再依 id 字典序（決定性）。
// ---------------------------------------------------------------------------
struct Match {
    CommandId id;                 // 命中的命令 id
    MatchKind kind = MatchKind::Fuzzy;
    int score = 0;                // 相關度（越大越相關）
    int distance = 0;             // 模糊比對的編輯距離（非模糊為 0）
    std::string title;            // 選用：註冊時附帶的顯示標題（僅供呈現，不影響比對）

    // 相關度嚴格優先序：score 大者在前，同分 id 字典序在前。
    bool better_than(const Match& o) const {
        if (score != o.score) return score > o.score;
        return id < o.id;
    }
};

// ---------------------------------------------------------------------------
// ResolveStatus / DispatchOutcome — dispatch_best 的結構化結果。
//
// 找不到（NotFound）與歧義（Ambiguous）皆**明確回報且不分派**；只有唯一最佳
// 候選時才經 CommandBus 執行（Dispatched），並帶回其 CommandResult。
// ---------------------------------------------------------------------------
enum class ResolveStatus {
    Dispatched,  // 唯一最佳候選已經 CommandBus 分派；result / chosen 有效
    NotFound,    // 查詢無任何候選；candidates 空
    Ambiguous,   // 多個並列最佳候選；candidates 列出，未分派
};

struct DispatchOutcome {
    ResolveStatus status = ResolveStatus::NotFound;
    CommandId chosen{};                 // Dispatched 時實際觸發的 id
    CommandResult result{};             // Dispatched 時 CommandBus 的回傳
    std::vector<Match> candidates{};    // Ambiguous 時的並列候選（相關度序）

    bool dispatched() const noexcept { return status == ResolveStatus::Dispatched; }
    bool ambiguous() const noexcept { return status == ResolveStatus::Ambiguous; }
    bool not_found() const noexcept { return status == ResolveStatus::NotFound; }
};

// ---------------------------------------------------------------------------
// 內部：Levenshtein 編輯距離（插入 / 刪除 / 取代各計 1）。兩列滾動陣列，O(n*m)。
// header-inline 純函式，無狀態、無平台相依。
// ---------------------------------------------------------------------------
namespace detail {

inline int edit_distance(const std::string& a, const std::string& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0) return static_cast<int>(m);
    if (m == 0) return static_cast<int>(n);

    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);

    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1,        // 刪除
                               cur[j - 1] + 1,     // 插入
                               prev[j - 1] + cost  // 取代 / 相符
                              });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

// 依 '.' 切出 id 的各段（命名空間片段）。用於模糊比對時比對單一段，
// 讓 `volme` 能對上 `volume.set`（對整串距離大，對段 "volume" 距離僅 1）。
inline std::vector<std::string> segments(const std::string& id) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : id) {
        if (c == '.') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// ActionRegistry — 已註冊命令的可查詢索引 + 前綴 / 模糊分派。
//
// 只維護「有哪些命令 id」（及選用標題）。可手動 add()，或以 sync_from(bus) 自
// E6-01 CommandBus 匯入既有命令。resolve() 依相關度回候選；dispatch_best() 取
// 唯一最佳候選經傳入的 CommandBus 觸發。歧義 / 找不到明確回報，不亂猜、不崩潰。
// ---------------------------------------------------------------------------
class ActionRegistry {
public:
    ActionRegistry() = default;

    // 加入 / 覆寫一個動作 id（可附顯示標題）。空 id 回 false 不變更狀態。
    // 標題僅供呈現，不參與比對。回傳是否新加入（既有 id 更新標題回 false）。
    bool add(CommandId id, std::string title = {}) {
        if (id.empty()) return false;
        const bool is_new = actions_.find(id) == actions_.end();
        actions_[std::move(id)] = std::move(title);
        return is_new;
    }

    // 自 E6-01 CommandBus 同步：把 bus 目前所有已註冊命令 id 併入註冊表。
    // 回傳新加入的數量（既有者不重複計）。
    std::size_t sync_from(const CommandBus& bus) {
        std::size_t added = 0;
        for (const auto& id : bus.command_ids()) {
            if (add(id)) ++added;
        }
        return added;
    }

    // 移除一個動作 id。回傳是否確有移除。
    bool remove(const CommandId& id) { return actions_.erase(id) > 0; }

    bool contains(const CommandId& id) const {
        return actions_.find(id) != actions_.end();
    }
    std::size_t size() const noexcept { return actions_.size(); }
    bool empty() const noexcept { return actions_.empty(); }

    // 列舉所有已註冊 id（字典序，決定性）。
    std::vector<CommandId> ids() const {
        std::vector<CommandId> out;
        out.reserve(actions_.size());
        for (const auto& kv : actions_) out.push_back(kv.first);
        return out;  // std::map 已排序
    }

    // 模糊比對容許的最大編輯距離（預設 2）。設 0 等同關閉模糊比對。
    void set_fuzzy_max_distance(int d) { fuzzy_max_distance_ = d < 0 ? 0 : d; }
    int fuzzy_max_distance() const noexcept { return fuzzy_max_distance_; }

    // 解析查詢 → 依相關度排序的候選清單（最相關在前）。無命中回空。
    // 空查詢一律回空（不對「全部」亂展開）。
    std::vector<Match> resolve(const std::string& query) const {
        std::vector<Match> out;
        if (query.empty()) return out;

        for (const auto& kv : actions_) {
            Match m;
            if (classify(query, kv.first, m)) {
                m.title = kv.second;
                out.push_back(std::move(m));
            }
        }

        std::sort(out.begin(), out.end(),
                  [](const Match& a, const Match& b) { return a.better_than(b); });
        return out;
    }

    // 便捷：只回最佳候選（無命中回 false）。歧義時仍回排序後第一個，
    // 呼叫端若需嚴謹歧義處理應用 resolve() 或 dispatch_best()。
    bool best(const std::string& query, Match& out) const {
        auto matches = resolve(query);
        if (matches.empty()) return false;
        out = matches.front();
        return true;
    }

    // 以查詢解析出的**唯一**最佳候選，經傳入的 E6-01 CommandBus 分派。
    //   - 無候選            → {NotFound}，不分派。
    //   - 多個並列最佳候選  → {Ambiguous, candidates=並列者}，不分派（不亂猜）。
    //   - 唯一最佳候選      → 經 bus 分派，回 {Dispatched, chosen, result}。
    // 注意：若唯一候選 id 未在 bus 註冊（註冊表與 bus 漂移），result 會是
    // CommandBus 自身的結構化 NotFound——如實回報，不靜默。
    DispatchOutcome dispatch_best(const CommandBus& bus,
                                  const std::string& query,
                                  const CommandArgs& args = {}) const {
        DispatchOutcome outcome;
        auto matches = resolve(query);

        if (matches.empty()) {
            outcome.status = ResolveStatus::NotFound;
            return outcome;
        }

        // matches 已排序；並列最佳 = 與 front 同分者。>1 即歧義。
        const int top = matches.front().score;
        std::size_t tied = 0;
        for (const auto& m : matches) {
            if (m.score == top) ++tied; else break;
        }
        if (tied > 1) {
            outcome.status = ResolveStatus::Ambiguous;
            outcome.candidates.assign(matches.begin(), matches.begin() + static_cast<std::ptrdiff_t>(tied));
            return outcome;
        }

        outcome.status = ResolveStatus::Dispatched;
        outcome.chosen = matches.front().id;
        outcome.result = bus.dispatch(outcome.chosen, args);
        return outcome;
    }

private:
    // 判定 query 對某 id 的命中種類與相關度分數。命中回 true 並填 out。
    // 相關度以整數編碼，種類權重拉開量級，種類內再以完成度 / 距離細排，
    // 確保「精確 > 前綴 > 子字串 > 模糊」且同種類同完成度者精確同分（→ 歧義可辨）。
    bool classify(const std::string& query, const std::string& id, Match& out) const {
        out.id = id;

        // 精確：唯一、最高分（權重 300000，量級遠高於其他種類）。
        if (query == id) {
            out.kind = MatchKind::Exact;
            out.score = 300000;
            out.distance = 0;
            return true;
        }

        // 前綴：id 以 query 起頭。完成度 = query 長 / id 長（越接近整串越高），
        // 故較短的完成（更貼近輸入）排前。權重 200000。
        if (id.size() > query.size() && id.compare(0, query.size(), query) == 0) {
            out.kind = MatchKind::Prefix;
            out.score = 200000 + ratio_score(query.size(), id.size());
            out.distance = 0;
            return true;
        }

        // 子字串：query 出現在 id 之中（非前綴）。權重 100000。
        if (id.find(query) != std::string::npos) {
            out.kind = MatchKind::Substring;
            out.score = 100000 + ratio_score(query.size(), id.size());
            out.distance = 0;
            return true;
        }

        // 模糊：對整串 id 與各命名空間段取最小編輯距離；<= 上限才算命中。
        if (fuzzy_max_distance_ > 0) {
            int dist = detail::edit_distance(query, id);
            for (const auto& seg : detail::segments(id)) {
                dist = std::min(dist, detail::edit_distance(query, seg));
            }
            if (dist > 0 && dist <= fuzzy_max_distance_) {
                out.kind = MatchKind::Fuzzy;
                // 距離越小分數越高；同距離再以完成度細排。權重 0（最低種類）。
                out.score = (fuzzy_max_distance_ - dist + 1) * 1000
                            + ratio_score(query.size(), id.size());
                out.distance = dist;
                return true;
            }
        }

        return false;
    }

    // 完成度分數：query 佔 id 的比例映到 0..1000（整數，決定性）。
    static int ratio_score(std::size_t q, std::size_t total) {
        if (total == 0) return 0;
        return static_cast<int>((q * 1000) / total);
    }

    std::map<CommandId, std::string> actions_;  // id → 顯示標題（有序，決定性）
    int fuzzy_max_distance_ = 2;
};

}  // namespace ds::command

#endif  // DS_COMMAND_E6_02_ACTION_REGISTRY_HPP
