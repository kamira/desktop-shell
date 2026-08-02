// W1-01 win32 kernel 後端 — 相位 2（Windows 期）的真實平台後端
//
// 實作 E1-24 宣告的 `ds::kernel::KernelBackend` 抽象介面（上游，可讀不可改）。
// 呼叫端與上層邏輯不因後端而改：null 後端在記憶體裡記錄的每一件事，本後端改為真的對
// Windows 下達操作。
//
// 契約對映（具名概念 → win32 原語）：
//   SurfaceId              → 一個 HWND（具名字串為鍵，永不對外暴露數字 handle，NFR-02）
//   SurfaceLayer::Topmost  → SetWindowPos(..., HWND_TOPMOST, ...)
//   SurfaceLayer::Wallpaper/BelowNormal → HWND_BOTTOM
//   InputPolicy::PassThrough → WS_EX_TRANSPARENT | WS_EX_LAYERED（點擊穿透）
//   InputPolicy::Modal/Accepting → 移除 WS_EX_TRANSPARENT
//   HitPolicy::Transparent → 同 PassThrough 的命中語意
//   show/hide              → ShowWindow(SW_SHOWNOACTIVATE / SW_HIDE)
//   poll_input             → PeekMessage 迴圈，轉譯為具名 InputEvent
//
// NFR-02（介面不得出現絕對座標與數字 z-order）如何守住：
//   **本檔對外的介面一個座標都沒有。** CreateWindowEx 必須要有 x/y/w/h，那是平台實作的
//   內部細節——由本後端自行選定預設幾何（工作區右上角），不經由 API 傳入、不對外暴露。
//   圖層一律以具名 `SurfaceLayer` 表達，不接受數字層級。
//
// win32 專屬擴充（**不屬於 KernelBackend 契約**）：
//   `hwnd_for()` / `pump()` 是非虛擬的額外成員，僅供同樣是 win32 專屬的 host 層使用。
//   為什麼需要它：kernel 契約刻意**沒有繪製 API**（繪製必然涉及座標，放進契約會直接違反
//   NFR-02），所以「把 render_model 畫到這個 surface 上」這件事只能由 host 拿到原生視窗自己做。
//   跨平台的上層邏輯不會碰這兩個方法；碰它們的程式碼本來就只能在 Windows 上跑。
//
// 建置：本單元僅於 Windows 建置（見同目錄 CMakeLists.txt 的 if(WIN32) 守衛），
// ubuntu CI 完全不會編到它——這是 G3 在加上 windows runner 之前不紅的原因，
// 同時也意味著**本後端的正確性目前不在任何閘門的守備範圍內**（見 CHG-20260803-03 已知限制）。
#ifndef DS_KERNEL_BACKEND_WIN32_WIN32_BACKEND_HPP
#define DS_KERNEL_BACKEND_WIN32_WIN32_BACKEND_HPP

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // 否則 windows.h 的 min/max 巨集會撞 std::min/std::max（見 K-001 的鄰居問題）
#endif
#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include "null_backend.hpp"  // E1-24：KernelBackend 介面 + 具名列舉（上游，可讀不可改）

namespace ds::kernel {

// win32 真實後端。所有平台操作真的下達給 Windows；狀態以 HWND 為準，不另維護影子狀態。
class Win32KernelBackend final : public KernelBackend {
public:
    explicit Win32KernelBackend(CapabilityMatrix caps = CapabilityMatrix::defaults());
    ~Win32KernelBackend() override;

    Win32KernelBackend(const Win32KernelBackend&) = delete;
    Win32KernelBackend& operator=(const Win32KernelBackend&) = delete;

    std::string name() const override { return "win32"; }

    // --- 生命週期 ---
    bool init() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    // --- 能力查詢（NFR-03）---
    const CapabilityMatrix& capabilities() const override { return caps_; }
    bool has(const CapabilityId& id) const override { return caps_.has(id); }

    // --- K1 surface kernel ---
    bool create_surface(const SurfaceId& id, const SurfaceProfile& profile) override;
    bool destroy_surface(const SurfaceId& id) override;
    bool has_surface(const SurfaceId& id) const override;
    bool show_surface(const SurfaceId& id) override;
    bool hide_surface(const SurfaceId& id) override;
    bool is_visible(const SurfaceId& id) const override;
    const SurfaceProfile* surface_profile(const SurfaceId& id) const override;
    std::size_t surface_count() const override { return surfaces_.size(); }

    // --- K2 繪製 ---
    // 契約只有 frame 括號，沒有繪製原語（見檔頭 NFR-02 說明）。
    // begin_frame 標記該 surface 進入一個 frame；實際像素由 host 經 hwnd_for() 自行畫。
    bool begin_frame(const SurfaceId& id) override;
    bool end_frame(const SurfaceId& id) override;

    // --- K3 輸入 ---
    bool set_input_policy(const SurfaceId& id, InputPolicy policy) override;
    std::vector<InputEvent> poll_input() override;

    // --- win32 專屬擴充（非契約的一部分，僅供 win32 host 使用）---
    // 取得具名 surface 的原生視窗控制碼；未知 id 回 nullptr。
    HWND hwnd_for(const SurfaceId& id) const;
    // 抽送一輪視窗訊息並回報是否收到結束請求（WM_CLOSE / WM_QUIT）。
    // poll_input() 內部也會抽訊息；host 若只想推進訊息迴圈而不取事件可用本方法。
    bool pump();
    // 是否已收到結束請求（使用者關掉視窗）。
    bool quit_requested() const noexcept { return quit_requested_; }

private:
    struct SurfaceRecord {
        HWND hwnd = nullptr;
        SurfaceProfile profile;
        bool in_frame = false;
        std::size_t completed_frames = 0;
    };

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    bool ensure_window_class();
    void apply_layer(HWND hwnd, SurfaceLayer layer) const;
    void apply_input_policy(HWND hwnd, InputPolicy input, HitPolicy hit) const;

    SurfaceRecord* find(const SurfaceId& id);
    const SurfaceRecord* find(const SurfaceId& id) const;
    // 由 HWND 反查具名 id（訊息回呼要把事件標回具名目標）；找不到回空字串。
    SurfaceId id_for_hwnd(HWND hwnd) const;

    CapabilityMatrix caps_;
    // 具名鍵配對記錄，順序即建立順序（永不以數字 index 對外暴露，NFR-02）。
    std::vector<std::pair<SurfaceId, SurfaceRecord>> surfaces_;
    std::vector<InputEvent> pending_;  // wnd_proc 收集、poll_input 取走
    bool initialized_ = false;
    bool class_registered_ = false;
    bool quit_requested_ = false;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_BACKEND_WIN32_WIN32_BACKEND_HPP
