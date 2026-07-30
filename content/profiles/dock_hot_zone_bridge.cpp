// content/profiles/c1_06/dock_hot_zone_bridge.cpp — DockHotZoneBridge 實作
//
// 本檔是本單元**唯一** `#include "edge_hot_zone.hpp"`（E1-16）之處——理由見標頭註解：
// 藉由把實際的上游串接隔離到獨立翻譯單元，避免 E1-02 `input_strategy.hpp` 與 E1-16
// `edge_hot_zone.hpp`（transitively 帶入 E1-04 `hit_test.hpp`）在同一翻譯單元內因
// `ds::kernel::HitResult` 同名不同型別（enum class 對 struct）而編譯失敗。
#include "dock_hot_zone_bridge.hpp"

#include <utility>

#include "edge_hot_zone.hpp"  // E1-16（上游，可讀不可改）：EdgeHotZoneRegistry / EdgeHotZone / ...

namespace ds::profiles {

namespace {

// DockEdge 是否屬於具名集合——鏡射 E1-16 `is_named_zone` 的窮舉 switch 慣例：只列舉具名
// case，未知列舉值（如外部 static_cast 產生的無效邊）落出 switch，回 false（不靜默視為某個
// 具名邊）。
bool is_valid_dock_edge(DockEdge edge) {
    switch (edge) {
        case DockEdge::Left:
        case DockEdge::Right:
        case DockEdge::Top:
        case DockEdge::Bottom:
        case DockEdge::TopLeft:
        case DockEdge::TopRight:
        case DockEdge::BottomLeft:
        case DockEdge::BottomRight:
            return true;
    }
    return false;  // 未知列舉值：無效邊，不靜默。
}

// 僅於 edge 已由 is_valid_dock_edge 確認有效時呼叫——switch 窮舉具名集合，理論上必中一 case。
ds::kernel::EdgeHotZone to_kernel_edge(DockEdge edge) {
    switch (edge) {
        case DockEdge::Left:
            return ds::kernel::EdgeHotZone::Left;
        case DockEdge::Right:
            return ds::kernel::EdgeHotZone::Right;
        case DockEdge::Top:
            return ds::kernel::EdgeHotZone::Top;
        case DockEdge::Bottom:
            return ds::kernel::EdgeHotZone::Bottom;
        case DockEdge::TopLeft:
            return ds::kernel::EdgeHotZone::TopLeft;
        case DockEdge::TopRight:
            return ds::kernel::EdgeHotZone::TopRight;
        case DockEdge::BottomLeft:
            return ds::kernel::EdgeHotZone::BottomLeft;
        case DockEdge::BottomRight:
            return ds::kernel::EdgeHotZone::BottomRight;
    }
    return ds::kernel::EdgeHotZone::Left;  // 不可達（呼叫端已先以 is_valid_dock_edge 把關）。
}

DockEdge from_kernel_edge(ds::kernel::EdgeHotZone edge) {
    switch (edge) {
        case ds::kernel::EdgeHotZone::Left:
            return DockEdge::Left;
        case ds::kernel::EdgeHotZone::Right:
            return DockEdge::Right;
        case ds::kernel::EdgeHotZone::Top:
            return DockEdge::Top;
        case ds::kernel::EdgeHotZone::Bottom:
            return DockEdge::Bottom;
        case ds::kernel::EdgeHotZone::TopLeft:
            return DockEdge::TopLeft;
        case ds::kernel::EdgeHotZone::TopRight:
            return DockEdge::TopRight;
        case ds::kernel::EdgeHotZone::BottomLeft:
            return DockEdge::BottomLeft;
        case ds::kernel::EdgeHotZone::BottomRight:
            return DockEdge::BottomRight;
    }
    return DockEdge::Left;  // 不可達（E1-16 enum 已窮舉）。
}

DockHotZoneStatus from_kernel_status(ds::kernel::HotZoneStatus status) {
    switch (status) {
        case ds::kernel::HotZoneStatus::Ok:
            return DockHotZoneStatus::Ok;
        case ds::kernel::HotZoneStatus::InvalidZone:
            return DockHotZoneStatus::InvalidZone;
        case ds::kernel::HotZoneStatus::InvalidThickness:
            return DockHotZoneStatus::InvalidThickness;
    }
    return DockHotZoneStatus::InvalidZone;  // 不可達（E1-16 enum 已窮舉）。
}

}  // namespace

struct DockHotZoneBridge::Impl {
    ds::kernel::EdgeHotZoneRegistry registry;
};

DockHotZoneBridge::DockHotZoneBridge() : impl_(std::make_unique<Impl>()) {}
DockHotZoneBridge::~DockHotZoneBridge() = default;
DockHotZoneBridge::DockHotZoneBridge(DockHotZoneBridge&&) noexcept = default;
DockHotZoneBridge& DockHotZoneBridge::operator=(DockHotZoneBridge&&) noexcept = default;

DockHotZoneStatus DockHotZoneBridge::register_zone(DockEdge edge, float thickness_ratio,
                                                    std::string action) {
    if (!is_valid_dock_edge(edge)) {
        return DockHotZoneStatus::InvalidZone;  // 提前攔截無效邊，不呼叫 E1-16（報錯不靜默）。
    }
    const ds::kernel::HotZoneStatus status =
        impl_->registry.register_zone(to_kernel_edge(edge), thickness_ratio, std::move(action));
    return from_kernel_status(status);
}

std::optional<DockTriggeredZone> DockHotZoneBridge::test(const DockPoint& point,
                                                          const DockScreenExtent& screen) const {
    const ds::kernel::LocalPoint kp{point.x, point.y};
    const ds::kernel::ScreenExtent ks{screen.width, screen.height};
    const std::optional<ds::kernel::TriggeredHotZone> hit = impl_->registry.test(kp, ks);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    return DockTriggeredZone{from_kernel_edge(hit->zone), hit->action};
}

std::size_t DockHotZoneBridge::size() const { return impl_->registry.size(); }

}  // namespace ds::profiles
