// E5-07 區域內連續拖曳判定 — 介面（engine 層 / 事件，platform 相位 1 = Mac / null 期）
//
// 語意：判定一連串指標移動事件是否構成「在某具名區域內的連續拖曳」——按下（Down）落在具名
// 區域內 → 起拖；隨後移動（Move）持續落在**同一個**具名區域內 → 拖曳中；放開（Up）仍在同一
// 區域內 → 拖曳結束；任一時刻移動離開該區域（或落到別的區域 / 離開整個 surface）→ 拖曳
// 中斷（取消）。本單元**不重新實作任何命中測試 / 區域查詢邏輯**——區域命中判定完全委派已
// 合併的 E5-14 `RegionEventDispatcher`（其再委派 E5-01 `MouseEventRouter` + E1-05
// `NamedRegionMap`），本單元新增的邏輯只有「連續拖曳」這個更高層的狀態機語意。
//
// 相位 1（Mac / null 期）硬約束：
//   - 純邏輯狀態機，無真實視窗系統 / OS / 繪圖 API，不接真實滑鼠（指標事件仍由呼叫端 / 測試
//     以 `feed_down()` / `feed_move()` / `feed_up()` 注入）。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支。
//   - **NFR-02**：座標沿用 E1-04 `LocalPoint`（本地 / 相對），區域一律具名（`region_name`
//     為具名字串），無畫面絕對座標、無數字 z-order / index。
//
// 建於上游之上（已合併，可讀不可改）：
//   - E5-14 `region_event_dispatcher.hpp`：`RegionEventDispatcher` / `RegionEvent` ——
//     具名 surface 命中 + 具名子區域查詢橋接（其再完全委派 E5-01 `MouseEventRouter` +
//     E1-05 `NamedRegionMap`，本單元不重複觸碰）。
//
// 設計要點：`RegionDragRecognizer` 建構時綁定**一個** E5-14 具名 surface + 該 surface 的
// E1-05 `NamedRegionMap` 具名子區域集合（內部私有持有一個專屬的 `RegionEventDispatcher`
// 實例，不與外部共用，故不會與呼叫端其他滑鼠處理邏輯互相干擾）。呼叫端以 `feed_down()` /
// `feed_move()` / `feed_up()`（或統一入口 `feed()`）依序注入指標事件序列，本類別據此驅動
// 拖曳狀態機並以 `subscribe()` 分派拖曳語意事件（Begin / Move / End / Cancel）。
//
// 移動事件的區域命中查詢借用 E5-14 `inject_button(..., MouseAction::Click, position)`——
// 該注入語意「直接命中判定並分派，不讀取或修改多擊追蹤游標」（見 E5-01 文件），故可在
// Down/Up 之間任意次查詢而不擾動 E5-14 內部 router 的按鍵狀態機；Down/Up 本身仍以真正的
// `inject_down()` / `inject_up()` 注入，取得的 `RegionEvent` 與查詢用途相同（是否命中 +
// 命中之具名區域 + 參數），只是額外驅動了（本類別私有、不對外暴露的）內部 router 按鍵狀態。
#ifndef DS_EVENTS_E5_07_REGION_DRAG_RECOGNIZER_HPP
#define DS_EVENTS_E5_07_REGION_DRAG_RECOGNIZER_HPP

#include <cstddef>
#include <functional>
#include <map>
#include <string>

#include "region_event_dispatcher.hpp"  // E5-14（上游，可讀不可改）：RegionEventDispatcher / RegionEvent

namespace ds::events {

// 指標事件動作（具名，非平台原生事件碼）——供 `feed()` 統一入口使用。
enum class PointerAction { Down, Move, Up };

// 拖曳語意事件種類。
enum class DragEventKind {
    Begin,   // 起拖：Down 落在具名區域內
    Move,    // 拖曳中：Move 仍落在起拖時的同一具名區域內
    End,     // 拖曳結束：Up 仍落在起拖時的同一具名區域內
    Cancel,  // 拖曳中斷：中途離開該具名區域（或落到別的區域 / 離開整個 surface）、
             // 或 Up 時已不在該具名區域內
};

// 單一拖曳語意事件。純資料、平台中立、本地座標（NFR-02）。
//
// `region_name` / `region_params`：起拖時命中之具名區域 id + 參數；Move 事件會以當次查詢
// 結果更新 `region_params`（區域參數理論上不隨查詢改變，但保留每次的最新快照，不假設不變）。
// Cancel 事件的 `region_name` / `region_params` 為**中斷前**（離開前）最後仍在區域內時的值。
struct DragEvent {
    DragEventKind kind = DragEventKind::Begin;
    ds::kernel::SurfaceId surface;            // 綁定之具名 surface（E5-14）
    std::string region_name;                  // 具名區域 id（見上方欄位說明）
    ds::kernel::LocalPoint position{};         // 觸發本事件的本地座標
    ds::kernel::RegionParams region_params;   // 區域參數快照
};

// 拖曳事件回呼型別。
using DragListener = std::function<void(const DragEvent&)>;

// ---------------------------------------------------------------------------
// RegionDragRecognizer —— 區域內連續拖曳判定。
//
// 綁定一個 E5-14 具名 surface + 其 E1-05 具名子區域集合，將一連串 `feed_down()` /
// `feed_move()` / `feed_up()` 指標事件序列判定為「連續拖曳」狀態轉移，分派
// Begin / Move / End / Cancel 語意事件給訂閱者。
//
// 狀態機（不亂序，見下方各方法說明）：
//   Idle --Down(落在區域內)--> Dragging --Move(仍在同區域)--> Dragging
//   Dragging --Up(仍在同區域)--> Idle（分派 End）
//   Dragging --Move/Up(離開該區域)--> Idle（分派 Cancel）
//   Idle 狀態下的 Move / Up 為 no-op（忽略，不分派任何事件）。
//   Dragging 狀態下重複 Down 為 no-op（忽略，不重新起拖 / 不分派）。
//
// 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的 UB
// （與 E5-01 `MouseEventRouter` / E5-14 `RegionEventDispatcher` 同慣例）。
// ---------------------------------------------------------------------------
class RegionDragRecognizer {
public:
    // 綁定一個具名 surface（`surface`）+ 該 surface 的具名子區域集合（`regions`，移交持有）。
    // 內部另建一個私有的 E5-14 `RegionEventDispatcher` 實例專供本辨識器使用。
    RegionDragRecognizer(ds::kernel::HitSurface surface, ds::kernel::NamedRegionMap regions);

    // 訂閱拖曳語意事件。`listener` 為空 → 回 0（無效訂閱，不佔用代號）。
    SubscriptionId subscribe(DragListener listener);

    // 取消訂閱。回傳是否確有移除；未知 id 為 no-op，回 false。
    bool unsubscribe(SubscriptionId id);

    // 目前訂閱者數。
    std::size_t listener_count() const;

    // 綁定之具名 surface。
    const ds::kernel::SurfaceId& surface() const noexcept { return surface_; }

    // 目前是否處於拖曳中。
    bool dragging() const noexcept { return state_ == State::Dragging; }

    // 拖曳中時目前所在之具名區域；未拖曳時為空字串。
    const std::string& active_region() const noexcept { return active_region_; }

    // 統一注入入口：依 `action` 分派給 `feed_down()` / `feed_move()` / `feed_up()`。
    void feed(PointerAction action, const ds::kernel::LocalPoint& position);

    // 注入一次按下。落在具名區域內 → 起拖（分派 Begin）；否則不起拖（no-op，狀態維持
    // Idle）；若已在拖曳中（Dragging），視為狀態機不亂序保護，忽略本次（no-op）。
    void feed_down(const ds::kernel::LocalPoint& position);

    // 注入一次移動。非拖曳中（Idle）→ 忽略（no-op）。拖曳中：仍落在起拖時的同一具名區域
    // 內 → 分派 Move；離開該區域（含落到別的區域 / 離開整個 surface）→ 中斷拖曳、分派
    // Cancel、狀態轉回 Idle。
    void feed_move(const ds::kernel::LocalPoint& position);

    // 注入一次放開。非拖曳中（Idle）→ 忽略（no-op）。拖曳中：仍落在同一具名區域內 →
    // 分派 End；已離開該區域 → 分派 Cancel。兩種情形皆狀態轉回 Idle。
    void feed_up(const ds::kernel::LocalPoint& position);

private:
    enum class State { Idle, Dragging };

    void handle_down(const ds::kernel::LocalPoint& position);
    void handle_move(const ds::kernel::LocalPoint& position);
    void handle_up(const ds::kernel::LocalPoint& position);

    // 無狀態區域命中查詢：借用 E5-14 `inject_button(..., MouseAction::Click, position)`，
    // 不讀取 / 修改內部 router 的多擊追蹤游標。回傳命中之 `RegionEvent`；未命中任何具名
    // surface（NoHit / Invalid）時回傳結構化空值（`region_hit=false`）。
    RegionEvent query(const ds::kernel::LocalPoint& position);

    // 分派拖曳語意事件給目前訂閱者（依訂閱順序，分派前取快照）。
    void dispatch(DragEventKind kind, const ds::kernel::LocalPoint& position);

    RegionEventDispatcher dispatcher_;  // 私有專屬（不與外部共用），完全委派 E5-14
    ds::kernel::SurfaceId surface_;
    State state_ = State::Idle;
    std::string active_region_;
    ds::kernel::RegionParams active_params_;

    std::map<SubscriptionId, DragListener> listeners_;
    SubscriptionId next_id_ = 1;

    // 供內部橋接回呼同步寫入本次查詢 / 注入結果的暫存（`dispatcher_` 的分派是同步呼叫，
    // 故 inject_* / query() 呼叫返回時已可讀取最新值）。
    RegionEvent last_event_;
    bool last_valid_ = false;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_07_REGION_DRAG_RECOGNIZER_HPP
