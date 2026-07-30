// E2-23 檔案系統列舉 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：列舉**檔案系統目錄內容**（給定路徑 → 子項目：名稱 / 類型(檔/目錄) / 大小 /
// 修改時間等中繼），並透過 **E2-01 的 MetricProvider 介面**把「某路徑下的項目清單／數量」
// 隨選掛成一個指標。這是「新增指標 = 新增 MetricProvider、掛件一行不動」機制的又一個具體
// 提供者——它**消費 E2-01 契約、不自造指標模型**（沿用 E2-16 已安裝應用列舉的隨選列舉樣式）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null / 假來源**：**絕不**接真實檔案系統——不含 `<filesystem>`
//     實體存取、不 `opendir`、不 `#ifdef`、無 win32 / cocoa 平台分支。換平台一行不動
//     （backend_guard 綠燈）。真實 OS 目錄掃描留待後端相位，本檔一律不含。
//   - 列舉來源為**可注入的 FileSystemSource 抽象**（`list(path)` / `stat(path)`）+ 一個
//     **記憶體內假來源**（NullFileSystemSource，持有記憶體目錄樹）。提供者只依賴此抽象，
//     故換後端時提供者一行不動。
//   - **錯誤不靜默**：無效路徑 / 非目錄 / 權限錯，`list` / `stat` 以明確的 FsError 回報，
//     不以空清單魚目混珠（消費者可分辨「空目錄」與「路徑錯誤」）。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id   = "fs.entries"
//   - name = "Filesystem Entries"
//   - unit = ""（純計數，無單位）
//   - range = at_least(0)（下界 0、上無界）
//   - **可列舉實例 = 目標路徑下的各子項目**：每個子項目一個 MetricInstance，
//     instance_id = 項目名、label = 項目名、value = 存在(1.0) + 類型文字（"file"/"dir"）。
//     於是「清單」由列舉實例取得、「數量」即 `Metric::instance_count()`——直接借用
//     E2-01 的可列舉實例要素，這正是本單元名「檔案系統列舉」的精神。
#ifndef DS_MODULES_E2_23_FS_ENUM_HPP
#define DS_MODULES_E2_23_FS_ENUM_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "metric.hpp"  // E2-01 契約（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// DirEntryType：子項目類型（檔 / 目錄）
// ---------------------------------------------------------------------------
// 相位 1 只區分「檔」與「目錄」兩類；其他（symlink / socket 等）留待後端相位再細分，
// 現階段以 Other 涵蓋，維持 module 層平台中立、最小契約。
enum class DirEntryType {
    File,       // 一般檔案
    Directory,  // 目錄
    Other,      // 其他（現階段不細分）
};

// 類型 → 穩定文字表述（供 MetricInstance 的 value.text 與人類可讀顯示）。
inline const char* to_string(DirEntryType t) {
    switch (t) {
        case DirEntryType::File:      return "file";
        case DirEntryType::Directory: return "dir";
        case DirEntryType::Other:     return "other";
    }
    return "other";
}

// ---------------------------------------------------------------------------
// DirEntry：一個目錄子項目的平台中立描述（名稱 + 類型 + 中繼欄位）
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述：名稱 + 類型 + 大小 + 修改時間。刻意不含任何平台專屬欄位
// （inode / 磁碟區代號 / ACL 等），維持 module 層平台中立。
//   - size：位元組數；目錄慣例為 0（不代表目錄「大小」，僅佔位）。
//   - mtime：修改時間，以 Unix epoch 秒表述（平台中立的整數，不綁任何 OS 時間型別）。
struct DirEntry {
    std::string name;                     // 項目名（不含路徑），如 "readme.txt"
    DirEntryType type = DirEntryType::File;
    std::uint64_t size = 0;               // 位元組；目錄慣例 0
    std::int64_t mtime = 0;               // 修改時間（Unix epoch 秒）；0 = 未知

    bool is_directory() const noexcept { return type == DirEntryType::Directory; }
    bool is_file() const noexcept { return type == DirEntryType::File; }

    bool operator==(const DirEntry& o) const {
        return name == o.name && type == o.type && size == o.size && mtime == o.mtime;
    }
    bool operator!=(const DirEntry& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// FsError：檔案系統操作的明確錯誤碼（不靜默）
// ---------------------------------------------------------------------------
// list / stat 以此回報失敗，讓消費者能分辨「空目錄」與「路徑錯誤」，而非把錯誤吞成空清單。
enum class FsError {
    None,             // 成功
    NotFound,         // 路徑不存在
    NotADirectory,    // 對「檔案」呼叫 list（該路徑非目錄）
    PermissionDenied, // 權限不足（模擬：來源標記為拒絕）
};

// 錯誤碼 → 穩定文字（供診斷 / 顯示）。
inline const char* to_string(FsError e) {
    switch (e) {
        case FsError::None:             return "none";
        case FsError::NotFound:         return "not_found";
        case FsError::NotADirectory:    return "not_a_directory";
        case FsError::PermissionDenied: return "permission_denied";
    }
    return "none";
}

// ---------------------------------------------------------------------------
// ListResult / StatResult：帶明確狀態的回傳（錯誤不靜默）
// ---------------------------------------------------------------------------
// ok() == true 時 error == None 且資料有效；否則 error 指出原因、資料為空/預設。
struct ListResult {
    FsError error = FsError::None;
    std::vector<DirEntry> entries;  // 僅 error==None 時有意義（決定性順序）
    bool ok() const noexcept { return error == FsError::None; }
};

struct StatResult {
    FsError error = FsError::None;
    DirEntry entry;                 // 僅 error==None 時有意義
    bool ok() const noexcept { return error == FsError::None; }
};

// ---------------------------------------------------------------------------
// FileSystemSource：列舉檔案系統目錄內容的抽象後端（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以掃描真實檔案系統；相位 1 只有記憶體內假來源。
// 提供者只依賴此抽象介面，故換後端時提供者一行不動。
//   - list(path)：列舉目錄內容 → ListResult（成功回子項目、失敗回明確 FsError）。
//   - stat(path)：查單一路徑中繼 → StatResult（成功回 DirEntry、失敗回明確 FsError）。
class FileSystemSource {
public:
    virtual ~FileSystemSource() = default;

    // 列舉 path 下的子項目（順序即列舉順序，決定性）。路徑錯誤以 FsError 回報。
    virtual ListResult list(const std::string& path) const = 0;

    // 查 path 本身的中繼（名稱 / 類型 / 大小 / 修改時間）。路徑錯誤以 FsError 回報。
    virtual StatResult stat(const std::string& path) const = 0;

protected:
    FileSystemSource() = default;
    FileSystemSource(const FileSystemSource&) = default;
    FileSystemSource& operator=(const FileSystemSource&) = default;
};

// ---------------------------------------------------------------------------
// NullFileSystemSource：相位 1 的記憶體內假來源（記憶體目錄樹）
// ---------------------------------------------------------------------------
// **絕不接真實檔案系統**。持有一棵記憶體目錄樹供測試與假感測器情境：以 `add_file` /
// `add_dir` 建樹（中間目錄自動建立），`set_permission_denied` 模擬權限錯。
// 路徑語意：POSIX 風格、以 '/' 分段；開頭 '/' 為根、尾端 '/' 忽略、"" 與 "/" 皆指根。
// 真實掃描留待後端相位——本類永不含平台呼叫、永不含 `<filesystem>` 實體存取。
class NullFileSystemSource : public FileSystemSource {
public:
    NullFileSystemSource() = default;

    // 加入一個檔案（含中間目錄自動建立）。回 *this 供鏈式呼叫。
    NullFileSystemSource& add_file(const std::string& path, std::uint64_t size,
                                   std::int64_t mtime = 0);
    // 加入一個目錄（含中間目錄自動建立）。
    NullFileSystemSource& add_dir(const std::string& path, std::int64_t mtime = 0);

    // 標記 / 解除某路徑為「權限拒絕」：對其 list / stat 回 PermissionDenied。
    NullFileSystemSource& set_permission_denied(const std::string& path, bool denied = true);

    // 清空整棵樹（回到只有空根目錄的狀態）。
    void clear();

    ListResult list(const std::string& path) const override;
    StatResult stat(const std::string& path) const override;

private:
    // 記憶體目錄樹節點。目錄以 name→子節點的有序 map 持有（決定性列舉順序）。
    struct Node {
        DirEntryType type = DirEntryType::Directory;
        std::uint64_t size = 0;
        std::int64_t mtime = 0;
        bool permission_denied = false;
        std::map<std::string, Node> children;  // 僅目錄使用；map 保證字典序決定性
    };

    Node root_;  // 根恆為目錄

    // 把路徑切成非空分段（忽略空段 / 尾端斜線）。
    static std::vector<std::string> split(const std::string& path);
    // 沿樹尋節點；找不到回 nullptr。
    const Node* find(const std::string& path) const;
    // 沿樹尋（可建）節點：中間段一律視為目錄、按需建立。回目標節點參照。
    Node& ensure(const std::vector<std::string>& segs);
};

// ---------------------------------------------------------------------------
// FileSystemEnumProvider：把「某路徑下的目錄項目清單／數量」掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內對目標路徑呼叫來源 `list()`，
// 每個子項目建成一個可列舉 MetricInstance（清單 = 列舉實例、數量 = 實例數）。消費者
// （掛件）只透過 E2-01 的 MetricRegistry / Metric 介面走訪，完全不觸及本型別與檔案系統。
//
// 隨選語意（沿用 E2-16 樣式）：提供者於註冊當下對「建構時指定之路徑」列舉一次。列舉錯誤
// （無效路徑 / 權限）不崩：仍掛上指標（instance_count()==0），並以 last_error() 明確回報。
class FileSystemEnumProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "fs.entries";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.fs";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Filesystem Entries";

    // 以一個檔案系統來源 + 目標路徑建構。source 為 null 時，提供者仍會掛上一個「空」指標
    // （instance_count()==0，last_error()==NotFound），保守而不崩。
    FileSystemEnumProvider(std::shared_ptr<FileSystemSource> source, std::string path)
        : source_(std::move(source)), path_(std::move(path)) {}

    std::string provider_id() const override { return kProviderId; }

    // 目標列舉路徑。
    const std::string& path() const noexcept { return path_; }

    // 上次 register_metrics() 之列舉結果錯誤碼（None = 成功）。明確可查，不靜默。
    FsError last_error() const noexcept { return last_error_; }

    // 對註冊表掛上 "fs.entries" 指標：列舉目標路徑、每個子項目建一個實例。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

private:
    std::shared_ptr<FileSystemSource> source_;
    std::string path_;
    FsError last_error_ = FsError::None;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_23_FS_ENUM_HPP
