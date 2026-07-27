// E2-13 媒體播放中繼資料 — 實作（module 層 / 子系統 sysinfo）
//
// 純邏輯：查詢可注入來源、以 E2-01 記憶體內實作把每個中繼資料欄位建成一個實例、
// 掛上註冊表，並支援 sample() 重新查詢更新（進度推入歷史）。
// 無 `#ifdef`、無系統呼叫、無真實媒體 API——換平台一行不動。
#include "media_metadata.hpp"

#include <memory>

namespace ds::sysinfo {

// ---------------------------------------------------------------------------
// PlaybackState 輔助
// ---------------------------------------------------------------------------
int state_rank(PlaybackState state) noexcept {
    switch (state) {
        case PlaybackState::Stopped: return 0;
        case PlaybackState::Paused: return 1;
        case PlaybackState::Playing: return 2;
    }
    return 0;  // 不可達；保守回停止位階
}

const char* to_string(PlaybackState state) noexcept {
    switch (state) {
        case PlaybackState::Stopped: return "stopped";
        case PlaybackState::Paused: return "paused";
        case PlaybackState::Playing: return "playing";
    }
    return "stopped";  // 不可達；保守
}

// ---------------------------------------------------------------------------
// MediaMetadataProvider
// ---------------------------------------------------------------------------
void MediaMetadataProvider::register_metrics(ds::metrics::MetricRegistry& registry) {
    // 沿用 E2-01 的記憶體內實作，不自造指標模型。
    // 異質中繼資料欄位集：無統一單位、無單一值域（unbounded）。
    metric_ = std::make_shared<ds::metrics::InMemoryMetric>(
        kMetricId, kMetricName, /*unit=*/"", ds::metrics::MetricRange::unbounded());

    // 固定欄位集（決定性列舉順序）。每欄一個可列舉實例：
    //   instance_id = 欄位鍵、label = 顯示名。
    //   狀態 / 文字 / 封面欄位無時序歷史（capacity=0）；進度欄位保留歷史環，
    //   配合 E2-02 週期採集鋪成進度序列。
    inst_state_ = &metric_->add_instance(kFieldState, "Playback State", /*history=*/0);
    inst_title_ = &metric_->add_instance(kFieldTitle, "Title", /*history=*/0);
    inst_artist_ = &metric_->add_instance(kFieldArtist, "Artist", /*history=*/0);
    inst_album_ = &metric_->add_instance(kFieldAlbum, "Album", /*history=*/0);
    inst_position_ = &metric_->add_instance(kFieldPosition, "Position", position_history_);
    inst_duration_ = &metric_->add_instance(kFieldDuration, "Duration", /*history=*/0);
    inst_artwork_ = &metric_->add_instance(kFieldArtwork, "Artwork", /*history=*/0);

    // 以目前快照填初值。初建亦把有效進度推入歷史（與後續採集路徑一致）。
    apply(current(), /*to_history=*/true);

    // 掛上註冊表；重複 id 由註冊表保守拒絕（回 false，此處不覆寫既有）。
    registry.register_metric(metric_);
}

void MediaMetadataProvider::sample() {
    if (!metric_) return;  // 尚未 register_metrics：無指標可更新
    // 重新查詢來源、把新快照寫入各實例；進度值推入歷史（採集路徑）。
    apply(current(), /*to_history=*/true);
}

void MediaMetadataProvider::apply(const MediaMetadata& meta, bool to_history) {
    using ds::metrics::MetricValue;

    // 狀態欄位：恆有效（含「無播放」= stopped）。數值維度 = 位階、文字 = 穩定字串。
    inst_state_->set_value(
        MetricValue::of(static_cast<double>(state_rank(meta.state)), to_string(meta.state)));

    // 「無播放」時，其餘欄位以 valid==false（未知）誠實表達，不塞假值。
    if (!meta.has_media) {
        inst_title_->set_value(MetricValue::unknown());
        inst_artist_->set_value(MetricValue::unknown());
        inst_album_->set_value(MetricValue::unknown());
        inst_duration_->set_value(MetricValue::unknown());
        inst_artwork_->set_value(MetricValue::unknown());
        // 進度：設為未知且**不**推入歷史（無讀值不污染序列）。
        inst_position_->set_value(MetricValue::unknown());
        return;
    }

    // 有媒體工作階段：文字欄位承載文字值（number 維持 0，消費者讀 text）。
    inst_title_->set_value(MetricValue::of(0.0, meta.title));
    inst_artist_->set_value(MetricValue::of(0.0, meta.artist));
    inst_album_->set_value(MetricValue::of(0.0, meta.album));
    inst_artwork_->set_value(MetricValue::of(0.0, meta.artwork_ref));
    // 總長：數值維度（秒）。相對靜態，不入歷史。
    inst_duration_->set_value(MetricValue::of(meta.duration_seconds));

    // 進度：數值維度（秒）。採集路徑推入歷史（update 於 valid 值才推）。
    const MetricValue pos = MetricValue::of(meta.position_seconds);
    if (to_history) {
        inst_position_->update(pos);
    } else {
        inst_position_->set_value(pos);
    }
}

}  // namespace ds::sysinfo
