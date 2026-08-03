// H1-03 拖曳裝配 + 位置持久化 — 實作
#include "position_store.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ds::host {

using ds::kernel::AnchorSpec;
using ds::kernel::AnchorStatus;
using ds::kernel::DragStatus;
using ds::kernel::ResolvedPlacement;
using ds::kernel::Size;
using ds::kernel::SurfaceId;

bool spec_from_pixels(int x, int y, const WorkArea& area, AnchorSpec& out) {
    if (area.width <= 0 || area.height <= 0) return false;
    out.anchor = ds::kernel::Anchor::TopLeft;
    out.offset.dx = static_cast<float>(x - area.x) / static_cast<float>(area.width);
    out.offset.dy = static_cast<float>(y - area.y) / static_cast<float>(area.height);
    return std::isfinite(out.offset.dx) && std::isfinite(out.offset.dy);
}

bool pixels_from_spec(const AnchorSpec& spec, const WorkArea& area,
                      int element_width, int element_height, int& out_x, int& out_y) {
    if (area.width <= 0 || area.height <= 0) return false;
    const Size container{static_cast<float>(area.width), static_cast<float>(area.height)};
    const Size element{static_cast<float>(element_width), static_cast<float>(element_height)};
    ResolvedPlacement placed;
    if (ds::kernel::resolve(spec, container, element, placed) != AnchorStatus::Ok) return false;
    // resolve 的結果是容器座標系；加回工作區原點才是螢幕絕對座標。
    out_x = area.x + static_cast<int>(std::lround(placed.x));
    out_y = area.y + static_cast<int>(std::lround(placed.y));
    return true;
}

std::string default_positions_path() {
    const char* base = std::getenv("LOCALAPPDATA");
    if (!base || !*base) return std::string();
    return std::string(base) + "\\desktop-shell\\positions.conf";
}

bool write_text_file(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    const std::size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) {
        // 只建一層父目錄即可（路徑形如 <LOCALAPPDATA>/desktop-shell/），
        // 已存在時 CreateDirectory 回 ERROR_ALREADY_EXISTS，視為成功。
        const std::string dir = path.substr(0, slash);
        ::CreateDirectoryA(dir.c_str(), nullptr);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return out.good();
}

std::string read_text_file(const std::string& path) {
    if (path.empty()) return std::string();
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// --- PositionPersistence -----------------------------------------------------

bool PositionPersistence::current_work_area(WorkArea& out) const {
    return backend_.work_area(out.x, out.y, out.width, out.height);
}

bool PositionPersistence::restore(const SurfaceId& id) {
    const std::string text = read_text_file(path_);
    if (text.empty()) return false;  // 第一次執行沒有檔案，屬正常

    // load_positions 是全有或全無：檔案壞掉時不會留半套狀態（E1-08 契約）。
    if (drag_.load_positions(text) != DragStatus::Ok) return false;

    const AnchorSpec* spec = drag_.remembered_position(id);
    if (!spec) return false;  // 檔案裡沒有這個 surface 的記錄

    WorkArea area;
    if (!current_work_area(area)) return false;
    int w = 0, h = 0;
    if (!backend_.surface_size(id, w, h)) return false;

    int x = 0, y = 0;
    if (!pixels_from_spec(*spec, area, w, h, x, y)) return false;
    return backend_.set_surface_origin(id, x, y);
}

bool PositionPersistence::remember_current(const SurfaceId& id) {
    WorkArea area;
    if (!current_work_area(area)) return false;

    int x = 0, y = 0;
    if (!backend_.surface_origin(id, x, y)) return false;

    AnchorSpec spec;
    if (!spec_from_pixels(x, y, area, spec)) return false;

    // 位置的真相來源是 E1-08 的狀態機，不是這裡的區域變數。
    // 尚未註冊時先 set_position 登錄，之後走一次完整的 begin → drag_to → end 提交。
    if (!drag_.is_tracked(id)) {
        if (drag_.set_position(id, spec) != DragStatus::Ok) return false;
        return true;
    }
    if (drag_.begin_drag(id) != DragStatus::Ok) return false;
    if (drag_.drag_to(id, spec) != DragStatus::Ok) {
        drag_.cancel_drag(id);
        return false;
    }
    return drag_.end_drag(id) == DragStatus::Ok;
}

bool PositionPersistence::flush() const {
    if (path_.empty()) return false;
    return write_text_file(path_, drag_.serialize_positions());
}

}  // namespace ds::host
