// E5-07 區域內連續拖曳判定 — 實作
//
// 純狀態機邏輯：內部持有一個私有的 E5-14 `RegionEventDispatcher` 實例，完全委派具名 surface
// 命中 + 具名子區域查詢；本單元新增的狀態只有「目前是否拖曳中 + 拖曳所在的具名區域」。
// 此檔不含任何平台分支或真實滑鼠後端。
#include "region_drag_recognizer.hpp"

#include <utility>
#include <vector>

namespace ds::events {

RegionDragRecognizer::RegionDragRecognizer(ds::kernel::HitSurface surface,
                                           ds::kernel::NamedRegionMap regions)
    : surface_(surface.id) {
    // 先取 id（下方 set_surfaces 會 move surface），再把 surface 移交內部 dispatcher。
    dispatcher_.set_surfaces({std::move(surface)});
    dispatcher_.set_regions(surface_, std::move(regions));
    // 訂閱本類別私有 dispatcher 的橋接事件：inject_* / query() 呼叫時同步寫入 last_event_，
    // 供本類別狀態機讀取（不對外暴露這張訂閱表）。
    dispatcher_.subscribe(surface_, [this](const RegionEvent& event) {
        last_event_ = event;
        last_valid_ = true;
    });
}

SubscriptionId RegionDragRecognizer::subscribe(DragListener listener) {
    if (!listener) {
        return 0;  // 無效訂閱：不佔用代號
    }
    const SubscriptionId id = next_id_++;
    listeners_.emplace(id, std::move(listener));
    return id;
}

bool RegionDragRecognizer::unsubscribe(SubscriptionId id) {
    return listeners_.erase(id) > 0;
}

std::size_t RegionDragRecognizer::listener_count() const {
    return listeners_.size();
}

void RegionDragRecognizer::feed(PointerAction action, const ds::kernel::LocalPoint& position) {
    switch (action) {
        case PointerAction::Down:
            handle_down(position);
            return;
        case PointerAction::Move:
            handle_move(position);
            return;
        case PointerAction::Up:
            handle_up(position);
            return;
    }
}

void RegionDragRecognizer::feed_down(const ds::kernel::LocalPoint& position) {
    handle_down(position);
}

void RegionDragRecognizer::feed_move(const ds::kernel::LocalPoint& position) {
    handle_move(position);
}

void RegionDragRecognizer::feed_up(const ds::kernel::LocalPoint& position) {
    handle_up(position);
}

RegionEvent RegionDragRecognizer::query(const ds::kernel::LocalPoint& position) {
    last_valid_ = false;
    // MouseAction::Click：直接命中判定並分派，不讀取 / 修改多擊追蹤游標（見 E5-01 文件），
    // 故可在 Down/Up 之間任意次查詢而不擾動內部 router 的按鍵狀態機。
    dispatcher_.inject_button(MouseButton::Left, MouseAction::Click, position);
    return last_valid_ ? last_event_ : RegionEvent{};
}

void RegionDragRecognizer::handle_down(const ds::kernel::LocalPoint& position) {
    if (state_ == State::Dragging) {
        return;  // 狀態機不亂序：已在拖曳中，忽略多餘的 Down（不重新起拖）
    }

    last_valid_ = false;
    dispatcher_.inject_down(MouseButton::Left, position);
    const RegionEvent event = last_valid_ ? last_event_ : RegionEvent{};

    if (!event.region_hit) {
        return;  // 未在具名區域內按下：不起拖（no-op，狀態維持 Idle）
    }

    state_ = State::Dragging;
    active_region_ = event.region_name;
    active_params_ = event.region_params;
    dispatch(DragEventKind::Begin, position);
}

void RegionDragRecognizer::handle_move(const ds::kernel::LocalPoint& position) {
    if (state_ != State::Dragging) {
        return;  // 狀態機不亂序：非拖曳中忽略移動
    }

    const RegionEvent event = query(position);
    if (event.region_hit && event.region_name == active_region_) {
        active_params_ = event.region_params;
        dispatch(DragEventKind::Move, position);
        return;
    }

    // 中途離開該具名區域（含落到別的區域 / 離開整個 surface）→ 中斷拖曳。
    dispatch(DragEventKind::Cancel, position);
    state_ = State::Idle;
    active_region_.clear();
}

void RegionDragRecognizer::handle_up(const ds::kernel::LocalPoint& position) {
    if (state_ != State::Dragging) {
        return;  // 狀態機不亂序：非拖曳中忽略放開
    }

    last_valid_ = false;
    dispatcher_.inject_up(MouseButton::Left, position);
    const RegionEvent event = last_valid_ ? last_event_ : RegionEvent{};

    if (event.region_hit && event.region_name == active_region_) {
        dispatch(DragEventKind::End, position);
    } else {
        dispatch(DragEventKind::Cancel, position);
    }
    state_ = State::Idle;
    active_region_.clear();
}

void RegionDragRecognizer::dispatch(DragEventKind kind, const ds::kernel::LocalPoint& position) {
    DragEvent event;
    event.kind = kind;
    event.surface = surface_;
    event.region_name = active_region_;
    event.position = position;
    event.region_params = active_params_;

    // 分派前一律取快照：listener 於回呼中訂閱 / 取消訂閱不影響本輪、避免疊代中改容器的
    // UB（與 E5-01 `MouseEventRouter` / E5-14 `RegionEventDispatcher` 同慣例）。
    std::vector<DragListener> snapshot;
    snapshot.reserve(listeners_.size());
    for (const auto& kv : listeners_) {
        snapshot.push_back(kv.second);
    }
    for (const auto& listener : snapshot) {
        listener(event);
    }
}

}  // namespace ds::events
