// E2-13 媒體播放中繼資料 — sysinfo 提供者（module 層 / 子系統 sysinfo）
//
// 語意：把「目前媒體播放狀態的中繼資料」（曲名／演出者／專輯／播放狀態
// playing/paused/stopped／進度／總長／封面參照）透過 **E2-01 的 MetricProvider 介面**
// 掛成一個指標，並以 **E2-02 的採集頻率分級** 決定輪詢節奏。這是「新增指標 = 新增
// MetricProvider、掛件一行不動」機制的又一個具體提供者——它**消費 E2-01 契約、不自造
// 指標模型**，並沿用 E2-02 的分級語意（媒體進度會動，宜週期採集而非隨選）。
//
// 分層約束（module 層 / 相位 1 = Mac / null 期）：
//   - **只寫平台中立介面 + null 來源**：不接任何真實系統媒體 API。真實後端
//     （MediaRemote / MPNowPlayingInfoCenter / SMTC 等）留待後端相位，本檔一律不含。
//   - 無 `#ifdef`、無系統呼叫、無平台分支——換平台一行不動（backend_guard 綠燈）。
//   - 可注入的媒體來源 `MediaSource`（抽象）+ null/假來源 `NullMediaSource`
//     （回注入的固定中繼資料，或「無播放」），故可完全單元測試。
//
// 指標形狀（完全沿用 E2-01 六要素，不新增模型）：
//   - id    = "media.nowplaying"
//   - name  = "Now Playing"
//   - unit  = ""（異質欄位集：狀態/文字/秒數混雜，無統一單位）
//   - range = unbounded（欄位異質，無單一值域；進度對總長的比例由消費者自算）
//   - **可列舉實例 = 各中繼資料欄位**：狀態 / 曲名 / 演出者 / 專輯 / 進度 / 總長 / 封面，
//     每欄一個 MetricInstance（instance_id = 欄位鍵、label = 顯示名）。欄位集固定且
//     決定性，「無播放」時非狀態欄位以 valid==false（未知）誠實表達，而非塞假值。
//     進度欄位保留時序歷史（history_capacity>0），配合 E2-02 週期採集可鋪成進度序列；
//     其餘欄位無歷史（capacity==0）。
#ifndef DS_MODULES_E2_13_MEDIA_METADATA_HPP
#define DS_MODULES_E2_13_MEDIA_METADATA_HPP

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "metric.hpp"    // E2-01 契約（上游，可讀不可改）
#include "sampling.hpp"  // E2-02 採集頻率分級（上游，可讀不可改）

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// PlaybackState：播放狀態（三態）
// ---------------------------------------------------------------------------
// 跨平台一致的最小播放狀態集。數值位階（state_rank）供 E2-01 的 MetricValue 數值維度
// 承載，文字（to_string）供顯示 / 列舉表述。
enum class PlaybackState {
    Stopped,  // 停止（含「無播放」）
    Paused,   // 暫停
    Playing,  // 播放中
};

// 狀態數值位階：Stopped=0 / Paused=1 / Playing=2。供 MetricValue 的數值維度承載，
// 讓消費者不必解析文字也能區分狀態高低（如「是否正在播放」= rank>=2）。
int state_rank(PlaybackState state) noexcept;

// 診斷 / 顯示用穩定字串（"stopped" / "paused" / "playing"）。
const char* to_string(PlaybackState state) noexcept;

// ---------------------------------------------------------------------------
// MediaMetadata：目前媒體播放狀態的平台中立中繼資料
// ---------------------------------------------------------------------------
// 跨平台一致的最小描述。`has_media==false` = 「無播放」（沒有任何媒體工作階段）：
// 此時 state 視為 Stopped、其餘文字/秒數欄位無意義（消費者應視為未知）。
// 刻意不含任何平台專屬欄位（無 bundle id、無平台控制代碼），維持 module 層平台中立。
struct MediaMetadata {
    bool has_media = false;                        // 是否有媒體工作階段（false = 無播放）
    PlaybackState state = PlaybackState::Stopped;  // 播放狀態
    std::string title;                             // 曲名
    std::string artist;                            // 演出者
    std::string album;                             // 專輯
    double position_seconds = 0.0;                 // 進度（秒）
    double duration_seconds = 0.0;                 // 總長（秒）
    std::string artwork_ref;                       // 封面參照（URI / 快取鍵；非影像資料本身）

    // 「無播放」：沒有任何媒體工作階段。這是 null 來源的誠實預設。
    static MediaMetadata none() { return MediaMetadata{}; }

    bool operator==(const MediaMetadata& o) const {
        return has_media == o.has_media && state == o.state && title == o.title &&
               artist == o.artist && album == o.album &&
               position_seconds == o.position_seconds &&
               duration_seconds == o.duration_seconds && artwork_ref == o.artwork_ref;
    }
    bool operator!=(const MediaMetadata& o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// MediaSource：查詢目前媒體中繼資料的抽象來源（平台中立契約）
// ---------------------------------------------------------------------------
// 真實平台後端（相位 2+）實作它以查詢系統目前的媒體工作階段；相位 1 只有 null 來源。
// 提供者只依賴此抽象介面，故換後端時提供者一行不動。
class MediaSource {
public:
    virtual ~MediaSource() = default;

    // 取得目前媒體播放狀態的快照。無媒體工作階段時回 MediaMetadata::none()。
    virtual MediaMetadata snapshot() const = 0;

protected:
    MediaSource() = default;
    MediaSource(const MediaSource&) = default;
    MediaSource& operator=(const MediaSource&) = default;
};

// ---------------------------------------------------------------------------
// NullMediaSource：相位 1 的 null / 假來源
// ---------------------------------------------------------------------------
// **不接任何真實系統媒體 API**。預設回「無播放」（Mac / null 期的誠實預設）；可注入
// 固定中繼資料供測試與假感測器情境，並提供便利變動子（set_state / set_position）以
// 模擬狀態轉換與進度更新。真實查詢留待後端相位——本類永不含平台呼叫。
class NullMediaSource : public MediaSource {
public:
    NullMediaSource() = default;
    explicit NullMediaSource(MediaMetadata meta) : meta_(std::move(meta)) {}

    // 注入 / 覆寫整份中繼資料。
    void set_metadata(MediaMetadata meta) { meta_ = std::move(meta); }
    // 回到「無播放」預設。
    void clear() { meta_ = MediaMetadata::none(); }

    // 便利變動子（供狀態轉換 / 進度更新測試）：
    void set_state(PlaybackState state) { meta_.state = state; }
    void set_position(double seconds) { meta_.position_seconds = seconds; }

    // 目前注入的中繼資料（唯讀）。
    const MediaMetadata& metadata() const noexcept { return meta_; }
    bool has_media() const noexcept { return meta_.has_media; }

    // 回目前注入的中繼資料快照（決定性）。
    MediaMetadata snapshot() const override { return meta_; }

private:
    MediaMetadata meta_ = MediaMetadata::none();
};

// ---------------------------------------------------------------------------
// MediaMetadataProvider：把媒體播放中繼資料掛成指標的 sysinfo 提供者
// ---------------------------------------------------------------------------
// 實作 **E2-01 的 MetricProvider**。register_metrics() 內向註冊表掛上單一指標
// "media.nowplaying"，其可列舉實例即各中繼資料欄位（狀態/曲名/演出者/專輯/進度/總長/封面）。
// 因媒體進度會隨時間變動，本提供者建議以 **E2-02 的週期分級**採集：呼叫端把 metric_id
// 與 sampling_tier() 登記到 SamplingScheduler，於排程器判定該採集時呼叫 sample() 重新
// 查詢來源並更新各實例（進度值推入歷史）。消費者（掛件）只透過 E2-01 的
// MetricRegistry / Metric 介面走訪，完全不觸及本型別。
class MediaMetadataProvider : public ds::metrics::MetricProvider {
public:
    // 本提供者掛上的指標識別碼。
    static constexpr const char* kMetricId = "media.nowplaying";
    // 提供者穩定識別碼（供診斷 / 去重 / 溯源）。
    static constexpr const char* kProviderId = "sysinfo.media";
    // 指標顯示名。
    static constexpr const char* kMetricName = "Now Playing";

    // 各欄位實例的穩定鍵（命名同 E2-01 風格；供消費者以 find_instance 尋值）。
    static constexpr const char* kFieldState = "media.state";
    static constexpr const char* kFieldTitle = "media.title";
    static constexpr const char* kFieldArtist = "media.artist";
    static constexpr const char* kFieldAlbum = "media.album";
    static constexpr const char* kFieldPosition = "media.position";
    static constexpr const char* kFieldDuration = "media.duration";
    static constexpr const char* kFieldArtwork = "media.artwork";

    // 進度欄位歷史環的預設容量（配合 E2-02 週期採集鋪成進度序列）。
    static constexpr std::size_t kDefaultPositionHistory = 64;
    // 建議採集分級：媒體進度變動屬常規頻率（非高頻動畫、亦非隨選靜態）。
    static constexpr ds::metrics::SamplingTier kDefaultTier =
        ds::metrics::SamplingTier::Normal;

    // 以一個媒體來源建構。source 為 null 時，提供者仍會掛上一個指標，且各欄位以
    // 「無播放」語意呈現（保守而不崩）。position_history 為進度欄位歷史環容量。
    explicit MediaMetadataProvider(std::shared_ptr<MediaSource> source,
                                   std::size_t position_history = kDefaultPositionHistory,
                                   ds::metrics::SamplingTier tier = kDefaultTier)
        : source_(std::move(source)), position_history_(position_history), tier_(tier) {}

    std::string provider_id() const override { return kProviderId; }

    // 本提供者建議的 E2-02 採集分級（供呼叫端 add_demand 用）。
    ds::metrics::SamplingTier sampling_tier() const noexcept { return tier_; }

    // 對註冊表掛上 "media.nowplaying" 指標：建立固定欄位集、以目前快照填初值，並保留
    // 指標參照供日後 sample() 更新。重複 id 由註冊表保守拒絕（不覆寫既有）。
    void register_metrics(ds::metrics::MetricRegistry& registry) override;

    // 重新查詢來源快照並更新各實例值（進度值推入歷史）。呼叫端在 E2-02 排程器判定
    // 本指標該採集時呼叫。register_metrics 尚未呼叫（無指標）時為 no-op。
    void sample();

private:
    // 依一份中繼資料把值寫入各實例。to_history 為真時進度欄位推入歷史（採集路徑），
    // 為假時只設值不動歷史（初建路徑外的保守用途）。
    void apply(const MediaMetadata& meta, bool to_history);

    // 目前快照：source_ 為 null 時視為「無播放」。
    MediaMetadata current() const {
        return source_ ? source_->snapshot() : MediaMetadata::none();
    }

    std::shared_ptr<MediaSource> source_;
    std::size_t position_history_;
    ds::metrics::SamplingTier tier_;

    // register_metrics 後持有，供 sample() 更新（與 registry 共享同一物件，
    // 故更新對消費者可見）。非擁有指標指向 metric_ 內的實例（其壽命由 metric_ 保證）。
    std::shared_ptr<ds::metrics::InMemoryMetric> metric_;
    ds::metrics::InMemoryMetricInstance* inst_state_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_title_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_artist_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_album_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_position_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_duration_ = nullptr;
    ds::metrics::InMemoryMetricInstance* inst_artwork_ = nullptr;
};

}  // namespace ds::sysinfo

#endif  // DS_MODULES_E2_13_MEDIA_METADATA_HPP
