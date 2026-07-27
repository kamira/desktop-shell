// E2-23 檔案系統列舉 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：以記憶體目錄樹表述來源、切分 POSIX 風格路徑、沿樹導覽、以 E2-01 記憶體內實作
// 把每個子項目建成一個實例、掛上註冊表。**絕不接真實檔案系統**——無 `<filesystem>` 實體
// 存取、無 `opendir`、無 `#ifdef`、無平台分支，換平台一行不動。
#include "fs_enum.hpp"

#include <memory>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// 路徑切分：以 '/' 分段，忽略空段（開頭 / 尾端 / 連續斜線）。
// "" 與 "/" → 空 vector（指根）。
// ---------------------------------------------------------------------------
std::vector<std::string> NullFileSystemSource::split(const std::string& path) {
    std::vector<std::string> segs;
    std::string cur;
    for (char c : path) {
        if (c == '/') {
            if (!cur.empty()) {
                segs.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) segs.push_back(cur);
    return segs;
}

// 沿樹尋（可建）目標節點：走訪每一段，缺則建成目錄。回目標節點參照。
NullFileSystemSource::Node& NullFileSystemSource::ensure(const std::vector<std::string>& segs) {
    Node* cur = &root_;
    for (const auto& seg : segs) {
        // 中間段一律視為目錄；[] 缺則插入預設節點（預設 type == Directory）。
        Node& child = cur->children[seg];
        cur = &child;
    }
    return *cur;
}

// 沿樹尋唯讀節點；任一段缺失回 nullptr。空 segs 指根。
const NullFileSystemSource::Node* NullFileSystemSource::find(const std::string& path) const {
    const Node* cur = &root_;
    for (const auto& seg : split(path)) {
        auto it = cur->children.find(seg);
        if (it == cur->children.end()) return nullptr;
        cur = &it->second;
    }
    return cur;
}

NullFileSystemSource& NullFileSystemSource::add_file(const std::string& path,
                                                     std::uint64_t size, std::int64_t mtime) {
    auto segs = split(path);
    // 空路徑無法命名一個檔案（根恆為目錄）：保守忽略。
    if (segs.empty()) return *this;
    Node& node = ensure(segs);
    node.type = DirEntryType::File;
    node.size = size;
    node.mtime = mtime;
    node.children.clear();  // 檔案無子項目
    return *this;
}

NullFileSystemSource& NullFileSystemSource::add_dir(const std::string& path, std::int64_t mtime) {
    auto segs = split(path);
    Node& node = ensure(segs);  // 空 segs → 根本身
    node.type = DirEntryType::Directory;
    node.mtime = mtime;
    return *this;
}

NullFileSystemSource& NullFileSystemSource::set_permission_denied(const std::string& path,
                                                                  bool denied) {
    Node& node = ensure(split(path));
    node.permission_denied = denied;
    return *this;
}

void NullFileSystemSource::clear() {
    root_ = Node{};  // 重置為空根目錄
}

// ---------------------------------------------------------------------------
// list：列舉目錄內容。錯誤不靜默——以明確 FsError 回報。
// ---------------------------------------------------------------------------
ListResult NullFileSystemSource::list(const std::string& path) const {
    const Node* node = find(path);
    if (node == nullptr) return {FsError::NotFound, {}};
    if (node->permission_denied) return {FsError::PermissionDenied, {}};
    // 對「檔案」呼叫 list 是錯誤（非目錄），不可魚目混珠回空清單。
    if (node->type != DirEntryType::Directory) return {FsError::NotADirectory, {}};

    ListResult result;  // error 預設 None
    // map 疊代序為 name 字典序 → 決定性列舉順序。
    for (const auto& [name, child] : node->children) {
        DirEntry e;
        e.name = name;
        e.type = child.type;
        e.size = child.size;
        e.mtime = child.mtime;
        result.entries.push_back(std::move(e));
    }
    return result;
}

// ---------------------------------------------------------------------------
// stat：查單一路徑中繼。錯誤不靜默。
// ---------------------------------------------------------------------------
StatResult NullFileSystemSource::stat(const std::string& path) const {
    const Node* node = find(path);
    if (node == nullptr) return {FsError::NotFound, {}};
    if (node->permission_denied) return {FsError::PermissionDenied, {}};

    DirEntry e;
    // 名稱 = 路徑最末段；根（空段）以 "/" 表述。
    auto segs = split(path);
    e.name = segs.empty() ? "/" : segs.back();
    e.type = node->type;
    e.size = node->size;
    e.mtime = node->mtime;
    return {FsError::None, std::move(e)};
}

// ---------------------------------------------------------------------------
// FileSystemEnumProvider：把某路徑下的目錄項目掛成 E2-01 指標。
// ---------------------------------------------------------------------------
void FileSystemEnumProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 純計數：無單位、下界 0、上無界。
    auto metric = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::at_least(0));

    if (source_) {
        ListResult listed = source_->list(path_);
        last_error_ = listed.error;  // 明確記錄（None = 成功），不靜默
        if (listed.ok()) {
            for (const auto& entry : listed.entries) {
                // 每個子項目 = 一個可列舉實例：
                //   instance_id = label = 項目名；項目無時序歷史，故 history_capacity = 0。
                auto& inst = metric->add_instance(entry.name, entry.name, /*history_capacity=*/0);
                // value：存在(1.0) + 類型文字（"file"/"dir"）。用 set_value（不推歷史）。
                inst.set_value(ds::metrics::MetricValue::of(1.0, to_string(entry.type)));
            }
        }
        // 列舉錯誤（無效路徑 / 權限）→ 不加實例；指標仍掛上（空），last_error_ 已明確記錄。
    } else {
        // source 為 null：保守視為「路徑不可達」，明確回報、不崩。
        last_error_ = FsError::NotFound;
    }

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(std::move(metric));
}

}  // namespace ds::sysinfo
