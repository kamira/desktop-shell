// E7-08 設定遷移與版本相容 — 實作（平台中立 / engine 層）
//
// 見 migration.hpp 檔首。核心：以註冊的遷移步驟為有向邊（節點 = FormatVersion），
// 從文件版本 BFS 找出抵達目標版本的最短鏈，依序套用 transform。無路徑 / 過新 → 明確報錯。

#include "migration.hpp"

#include <deque>
#include <string>

namespace ds::format {

// -----------------------------------------------------------------------------
// 版本序關係
// -----------------------------------------------------------------------------

bool version_less(const FormatVersion& a, const FormatVersion& b) noexcept {
    if (a.major != b.major) return a.major < b.major;
    return a.minor < b.minor;
}

std::string version_to_string(const FormatVersion& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor);
}

// -----------------------------------------------------------------------------
// MigrateResult
// -----------------------------------------------------------------------------

MigrateResult MigrateResult::success(Value value, FormatVersion version, bool changed) {
    MigrateResult r;
    r.ok_ = true;
    r.changed_ = changed;
    r.value_ = std::move(value);
    r.version_ = version;
    return r;
}

MigrateResult MigrateResult::failure(MigrateError error) {
    MigrateResult r;
    r.ok_ = false;
    r.changed_ = false;
    r.error_ = std::move(error);
    return r;
}

// -----------------------------------------------------------------------------
// MigrationRegistry：註冊
// -----------------------------------------------------------------------------

bool MigrationRegistry::register_migration(Migration migration) {
    // 契約：步驟必為嚴格上升（from < to）。非法則拒絕，不靜默登錄。
    if (!version_less(migration.from, migration.to)) {
        return false;
    }
    migrations_.push_back(std::move(migration));
    return true;
}

bool MigrationRegistry::add(FormatVersion from, FormatVersion to,
                            std::function<Value(const Value&)> fn) {
    return register_migration(Migration{from, to, std::move(fn)});
}

// -----------------------------------------------------------------------------
// 路徑搜尋（BFS 最短鏈）
// -----------------------------------------------------------------------------

bool MigrationRegistry::find_path(const FormatVersion& from,
                                  const FormatVersion& target,
                                  std::vector<std::size_t>& out_steps) const {
    out_steps.clear();
    if (from == target) {
        return true;  // 空鏈：已在目標版本。
    }

    // BFS：以「已抵達的版本」為 frontier；記錄抵達每個版本的前驅步驟以回溯路徑。
    struct Trace {
        FormatVersion version;  // 抵達的版本
        std::size_t step;       // 用來抵達此版本的 migrations_ 索引
        int prev;               // visited 中前驅節點索引；-1 = 起點
    };
    std::vector<Trace> visited;
    std::deque<int> queue;  // visited 索引

    visited.push_back(Trace{from, static_cast<std::size_t>(-1), -1});
    queue.push_back(0);

    auto seen = [&visited](const FormatVersion& v) -> bool {
        for (const auto& t : visited) {
            if (t.version == v) return true;
        }
        return false;
    };

    while (!queue.empty()) {
        int cur = queue.front();
        queue.pop_front();
        const FormatVersion here = visited[static_cast<std::size_t>(cur)].version;

        // 掃描所有由 here 出發的外向邊（依註冊順序，確保決定性）。
        for (std::size_t i = 0; i < migrations_.size(); ++i) {
            if (!(migrations_[i].from == here)) continue;
            const FormatVersion& next = migrations_[i].to;

            if (next == target) {
                // 回溯：target <- ... <- from。
                std::vector<std::size_t> rev;
                rev.push_back(i);
                int p = cur;
                while (p != -1 && visited[static_cast<std::size_t>(p)].prev != -1) {
                    rev.push_back(visited[static_cast<std::size_t>(p)].step);
                    p = visited[static_cast<std::size_t>(p)].prev;
                }
                out_steps.assign(rev.rbegin(), rev.rend());
                return true;
            }

            if (seen(next)) continue;
            visited.push_back(Trace{next, i, cur});
            queue.push_back(static_cast<int>(visited.size()) - 1);
        }
    }
    return false;
}

bool MigrationRegistry::has_path(const FormatVersion& from,
                                 const FormatVersion& target) const {
    if (version_less(target, from)) return false;  // 目標比來源舊：不可達（不降級）。
    std::vector<std::size_t> steps;
    return find_path(from, target, steps);
}

// -----------------------------------------------------------------------------
// migrate
// -----------------------------------------------------------------------------

MigrateResult MigrationRegistry::migrate(const Value& root,
                                         const FormatVersion& current,
                                         const FormatVersion& target) const {
    // 1) 已是目標版本：no-op（不套用任何步驟）。
    if (current == target) {
        return MigrateResult::success(root, target, /*changed=*/false);
    }

    // 2) 文件版本比目標新：過新，實作不降級，明確報錯。
    if (version_less(target, current)) {
        MigrateError e;
        e.status = MigrateStatus::TooNew;
        e.from = current;
        e.target = target;
        e.message = "文件版本 " + version_to_string(current) + " 比目標版本 " +
                    version_to_string(target) + " 新（過新）；本實作無法降級遷移";
        return MigrateResult::failure(std::move(e));
    }

    // 3) current < target：找出遷移鏈。
    std::vector<std::size_t> steps;
    if (!find_path(current, target, steps)) {
        MigrateError e;
        e.status = MigrateStatus::NoPath;
        e.from = current;
        e.target = target;
        e.message = "找不到由版本 " + version_to_string(current) + " 升級到版本 " +
                    version_to_string(target) + " 的遷移路徑";
        return MigrateResult::failure(std::move(e));
    }

    // 4) 依序套用每個步驟的 transform（空 transform = 恆等，僅升版本標記）。
    Value acc = root;
    for (std::size_t idx : steps) {
        const Migration& m = migrations_[idx];
        if (m.transform) {
            acc = m.transform(acc);
        }
    }
    return MigrateResult::success(std::move(acc), target, /*changed=*/true);
}

MigrateResult MigrationRegistry::migrate(const Document& doc,
                                         const FormatVersion& target) const {
    return migrate(doc.root, doc.format_version, target);
}

}  // namespace ds::format
