// H1-03 拖曳裝配 + 位置持久化
//
// 把三樣既有東西接起來，自己不發明任何狀態機：
//   E1-08 `DraggableSurface` —— 拖曳狀態機、記憶位置、序列化 / 還原（`serialize_positions`
//                               / `load_positions`），位置以 `AnchorSpec` 表達
//   E1-07 `resolve()`        —— AnchorSpec → 具體像素（唯一的佈局邊界）
//   W1-03 win32 後端擴充     —— 真實視窗的位置讀寫與拖曳結束通知
//
// **像素只在這一層出現。** 記憶與持久化用的是 `AnchorSpec`：具名錨點 + **容器尺寸的
// 正規化分數**偏移。因此存下來的位置是**解析度無關**的——換螢幕、改工作列高度之後，
// widget 會落在對應的相對位置，而不是跑到畫面外。這是 NFR-02 的直接紅利，不是額外工。
#ifndef DS_HOST_POSITION_POSITION_STORE_HPP
#define DS_HOST_POSITION_POSITION_STORE_HPP

#include <string>

#include "anchor_model.hpp"       // E1-07（上游）：AnchorSpec / Size / resolve
#include "draggable_surface.hpp"  // E1-08（上游）：DraggableSurface / DragStatus
#include "win32_backend.hpp"      // W1-03：位置讀寫與拖曳通知

namespace ds::host {

// 螢幕工作區（扣掉工作列）。容器座標系的原點不保證是 (0,0)——
// 工作列擺在上方或左側時就不是，故必須連原點一起帶著走。
struct WorkArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// 像素 → AnchorSpec。以 `TopLeft` 為錨點，位置整個表達成正規化偏移，可無損來回。
//
// 為什麼固定用 TopLeft 而不挑最近的錨點：挑錨點會讓「同一個位置」有多種表達，
// round-trip 不再唯一，測試與除錯都會變得沒有依據。邊緣吸附之類的行為屬於更上層的決策。
// 工作區尺寸非正時回 false（不產生垃圾資料）。
bool spec_from_pixels(int x, int y, const WorkArea& area, ds::kernel::AnchorSpec& out);

// AnchorSpec → 像素（委由 E1-07 `resolve`，不自行算幾何）。
// 回傳的是**螢幕絕對座標**（已加回工作區原點）。resolve 失敗或尺寸非正時回 false。
bool pixels_from_spec(const ds::kernel::AnchorSpec& spec, const WorkArea& area,
                      int element_width, int element_height, int& out_x, int& out_y);

// 位置檔的預設路徑：`%LOCALAPPDATA%\desktop-shell\positions.conf`。
// 取不到 LOCALAPPDATA 時回空字串（呼叫端據此跳過持久化，不崩潰、不亂寫檔）。
std::string default_positions_path();

// 讀 / 寫純文字。寫入會自動建立父目錄。失敗回 false / 空字串——
// **持久化失敗絕不能讓 widget 掛掉**，呼叫端一律容忍。
//
// `read_text_file` 會**去掉開頭的 UTF-8 BOM**：設定檔是使用者會用記事本改的東西，
// 而許多 Windows 編輯器預設寫 BOM；不去掉的話解析必失敗，設定被靜默丟棄
// （CHG-20260803-12 的操作驗收實際踩到）。
bool write_text_file(const std::string& path, const std::string& text);
std::string read_text_file(const std::string& path);

// 把「拖曳 → 記住 → 下次還原」串成一條線。
//
// 職責邊界：本類別**不含任何狀態機**，狀態全在 E1-08 `DraggableSurface` 裡；
// 這裡只負責像素換算、檔案存取，以及在對的時機呼叫對的東西。
class PositionPersistence {
public:
    PositionPersistence(ds::kernel::Win32KernelBackend& backend,
                        ds::kernel::DraggableSurface& drag,
                        std::string path)
        : backend_(backend), drag_(drag), path_(std::move(path)) {}

    // 開機還原：讀檔 → E1-08 載入記憶 → 解析為像素 → 真的把視窗搬過去。
    // 回傳是否確實套用了一個已記憶的位置（沒有檔案 / 沒有該 surface 的記錄時回 false，屬正常）。
    bool restore(const ds::kernel::SurfaceId& id);

    // 使用者剛拖完：讀視窗現在的真實位置 → 換算 AnchorSpec → 提交為記憶位置。
    // 走 E1-08 的 begin_drag / drag_to / end_drag，讓狀態機是唯一的真相來源。
    bool remember_current(const ds::kernel::SurfaceId& id);

    // 把所有記憶位置寫回檔案。無路徑時直接回 false（不視為錯誤）。
    bool flush() const;

    const std::string& path() const noexcept { return path_; }

private:
    bool current_work_area(WorkArea& out) const;

    ds::kernel::Win32KernelBackend& backend_;
    ds::kernel::DraggableSurface& drag_;
    std::string path_;
};

}  // namespace ds::host

#endif  // DS_HOST_POSITION_POSITION_STORE_HPP
