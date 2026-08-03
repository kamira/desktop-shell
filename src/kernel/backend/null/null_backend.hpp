// E1-24 null 後端參考實作 — kernel 平台後端抽象介面 + null 實作
//
// 本檔宣告兩件事：
//   1. `KernelBackend` —— kernel 平台後端的**抽象介面**。所有後端（相位 1 的 null、
//      相位 2+ 的 win32 / cocoa）皆實作同一介面：surface kernel（視窗）、繪製（paint）、
//      輸入（input）三組平台原語（K1 / K2 / K3），加上經 E1-21 能力矩陣的能力查詢，
//      與 init / shutdown 生命週期。呼叫端只透過此介面對「系統」下達操作，因此與真實
//      平台完全解耦。
//   2. `NullKernelBackend` —— 相位 1（Mac / null 期）的**參考實作**。所有平台操作為
//      no-op 或以**記憶體狀態**忠實記錄，**任何平台都能編譯執行**（不含任何真實 OS
//      呼叫），作為相位 1 的預設後端、契約測試（E1-25）的靶，與其他後端的行為基準。
//
// 相位 1（Mac / null 期）約束：
//   - 不含任何真實平台後端，不真的呼叫 OS；跨平台性由 API 面約束保證，不由語言保證。
//   - **不得出現** `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支或真實 OS API。
//   - null 後端以 E1-21 `CapabilityMatrix` 回報一組**保守能力**；`has()` 為能力閘控
//     入口（NFR-03）。
//
// 硬約束（NFR-02）：本介面**不得出現絕對座標與數字 z-order**。
//   - surface 一律以**具名 SurfaceId** 指涉，不以數字 handle / index 指涉。
//   - 圖層以**具名角色**（`SurfaceLayer`）表達，不以數字層級表達。
//   - 幾何 / 佈局不以像素座標表達；沒有 `set_position(x, y)` 式 API。
#ifndef DS_KERNEL_BACKEND_NULL_NULL_BACKEND_HPP
#define DS_KERNEL_BACKEND_NULL_NULL_BACKEND_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "capability_matrix.hpp"  // E1-21（上游，可讀不可改）

namespace ds::kernel {

// surface 的穩定具名識別碼（如 "surface.panel" / "surface.wallpaper"）。
//
// 刻意用具名字串而非數字 handle / index：NFR-02 禁止以絕對座標 / 數字 z-order 指涉。
// 與 E1-18 的具名指涉慣例一致。
using SurfaceId = std::string;

// 圖層角色 —— 以**具名角色**表達 z-order（NFR-02：不用數字層級）。
//
// 對應需求中「桌布之上、一般視窗之下」等具名層帶；真實後端上線後映射到平台實際圖層。
enum class SurfaceLayer {
    Wallpaper,    // 桌布層（最底）
    BelowNormal,  // 一般視窗之下
    Normal,       // 一般視窗
    Overlay,      // 浮層（一般視窗之上）
    Topmost,      // 最上層
};

// 輸入策略 —— surface 如何參與輸入（「不搶焦點的面板」對「獨占焦點的 modal」共存的關鍵）。
enum class InputPolicy {
    Modal,        // 獨占焦點
    Accepting,    // 接受輸入但不獨占焦點
    PassThrough,  // 穿透：點擊穿過，本 surface 不吃輸入
};

// 命中策略 —— surface 對命中測試的回應。
enum class HitPolicy {
    Solid,        // 實心：命中落在本 surface
    Transparent,  // 命中穿透：點擊視為落到其後
};

// 生命週期 —— surface 的存續型態。
enum class SurfaceLifecycle {
    Persistent,  // 常駐（桌面角色 / 面板）
    Ephemeral,   // 短暫（召喚式浮層，用畢即棄）
};

// 四參數 surface profile —— surface kernel 的核心洞見：四個產品差異即這四個參數的取值。
//
// 純資料、具名取值、無絕對座標、無數字 z-order。
struct SurfaceProfile {
    SurfaceLayer layer = SurfaceLayer::Normal;
    InputPolicy input = InputPolicy::Accepting;
    HitPolicy hit = HitPolicy::Solid;
    SurfaceLifecycle lifecycle = SurfaceLifecycle::Persistent;
};

// 輸入事件型別 —— 具名分類，不含任何像素座標（NFR-02）。
enum class InputEventType {
    PointerMove,
    PointerDown,
    PointerUp,
    Key,
};

// 單一輸入事件 —— 以具名型別 + 具名目標 surface 表達，不含絕對座標。
struct InputEvent {
    InputEventType type = InputEventType::PointerMove;
    SurfaceId target;  // 具名目標 surface（可為空 = 未指向任何 surface）
};

// ---------------------------------------------------------------------------
// KernelBackend —— kernel 平台後端的抽象介面（platform 層對系統操作的唯一出口）。
//
// 後端家族的共同契約：所有後端實作同一組方法，呼叫端與上層邏輯不因後端而改。相位 1
// 由 NullKernelBackend 承接；真實後端（相位 2+）實作同介面即可。契約測試組（E1-25）
// 僅依賴本介面與 E1-21 能力矩陣，不含任何平台分支。
// ---------------------------------------------------------------------------
class KernelBackend {
public:
    virtual ~KernelBackend() = default;

    // 後端的穩定名稱（"null" / 未來 "cocoa" / "win32"）—— 供診斷與契約測試識別。
    virtual std::string name() const = 0;

    // --- 生命週期 ---
    // 初始化後端；成功回 true。可重入：已初始化再呼叫仍回 true（冪等）。
    virtual bool init() = 0;
    // 關閉後端並釋放其資源（含所有 surface）。冪等：未初始化再呼叫不崩潰。
    virtual void shutdown() = 0;
    // 目前是否處於已初始化狀態。
    virtual bool is_initialized() const = 0;

    // --- 能力查詢（NFR-03，經 E1-21 能力矩陣）---
    // 本後端回報的能力矩陣（單一資料來源）。
    virtual const CapabilityMatrix& capabilities() const = 0;
    // 能力閘控入口：該能力於本後端是否可用。等價於 capabilities().has(id)。
    // **未知能力一律回 false**（保守），呼叫端因此永遠安全。
    virtual bool has(const CapabilityId& id) const = 0;

    // --- K1 surface kernel（視窗）---
    // 以具名 id + 四參數 profile 建立一個 surface。
    //
    // **前置條件：後端須已 `init()`**。未初始化的後端不該能開出視窗——真實後端在
    // `init()` 才註冊視窗類別 / 建立平台資源，這是物理限制而非實作偏好。
    // 未初始化、id 為空、或 id 已存在則回 false（保守）。
    //
    // （此前置條件於 CHG-20260803-11 寫明並對齊：原本只有 win32 遵守，null 不遵守，
    //   而舊契約從未跑過真實後端，故分歧長期未被發現——見知識庫 K-003 / K-007。）
    virtual bool create_surface(const SurfaceId& id, const SurfaceProfile& profile) = 0;
    // 銷毀具名 surface；回傳是否確有銷毀（未知 id 回 false，不崩潰）。
    virtual bool destroy_surface(const SurfaceId& id) = 0;
    // 該具名 surface 是否存在。
    virtual bool has_surface(const SurfaceId& id) const = 0;
    // 顯示 / 隱藏具名 surface；未知 id 回 false（不崩潰）。
    virtual bool show_surface(const SurfaceId& id) = 0;
    virtual bool hide_surface(const SurfaceId& id) = 0;
    // 該具名 surface 目前是否可見；未知 id 回 false（保守）。
    virtual bool is_visible(const SurfaceId& id) const = 0;
    // 查詢某具名 surface 的四參數 profile；未知 id 回 nullptr。指標於該 surface 存活期間有效。
    virtual const SurfaceProfile* surface_profile(const SurfaceId& id) const = 0;
    // 目前存在的 surface 數量。
    virtual std::size_t surface_count() const = 0;

    // --- K2 繪製（paint）---
    // 於具名 surface 開一個繪製 frame；未知 id、或該 surface 已在 frame 中則回 false。
    virtual bool begin_frame(const SurfaceId& id) = 0;
    // 結束具名 surface 目前的繪製 frame；未曾 begin 則回 false。
    virtual bool end_frame(const SurfaceId& id) = 0;

    // --- K3 輸入（input）---
    // 更新具名 surface 的輸入策略；未知 id 回 false。
    virtual bool set_input_policy(const SurfaceId& id, InputPolicy policy) = 0;
    // 抽取一批待處理輸入事件。null 後端無真實輸入來源，永遠回空。
    virtual std::vector<InputEvent> poll_input() = 0;
};

// ---------------------------------------------------------------------------
// NullKernelBackend —— 相位 1（Mac / null 期）的參考後端。
//
// 不觸碰任何真實 OS（無 NSWindow / CreateWindowEx / 任何平台 API）；改為在**記憶體**中
// 忠實維護 surface 表（每 surface 保存四參數 profile、可見狀態、frame 計數）與各操作
// 的呼叫計數，供契約測試斷言「狀態一致」。以 E1-21 能力矩陣回報一組保守能力（預設即
// `CapabilityMatrix::defaults()`：保證存在的基礎能力可用、可選能力一律不可用）。
// ---------------------------------------------------------------------------
class NullKernelBackend final : public KernelBackend {
public:
    // 預設以 E1-21 內嵌預設能力矩陣建構（保守）；亦可注入自訂矩陣（供測試 / 未來後端覆用）。
    explicit NullKernelBackend(CapabilityMatrix caps = CapabilityMatrix::defaults());

    std::string name() const override { return "null"; }

    // --- 生命週期 ---
    bool init() override;
    void shutdown() override;
    bool is_initialized() const override { return initialized_; }

    // --- 能力查詢（經 E1-21）---
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
    bool begin_frame(const SurfaceId& id) override;
    bool end_frame(const SurfaceId& id) override;

    // --- K3 輸入 ---
    bool set_input_policy(const SurfaceId& id, InputPolicy policy) override;
    std::vector<InputEvent> poll_input() override;

    // --- 記錄狀態（供契約測試斷言；非介面的一部分）---
    std::size_t init_calls() const noexcept { return init_calls_; }
    std::size_t shutdown_calls() const noexcept { return shutdown_calls_; }
    std::size_t poll_input_calls() const noexcept { return poll_input_calls_; }
    // 某具名 surface 已完成（begin+end 配對）的 frame 數；未知 id 回 0。
    std::size_t completed_frames(const SurfaceId& id) const;
    // 某具名 surface 目前是否正處於一個未結束的 frame 中；未知 id 回 false。
    bool in_frame(const SurfaceId& id) const;

private:
    // 記憶體 surface 記錄 —— 純資料，無平台 handle。
    struct SurfaceRecord {
        SurfaceProfile profile;
        bool visible = false;
        bool in_frame = false;
        std::size_t completed_frames = 0;
    };

    // 以具名鍵線性尋找（surface 數量小）。const 與非 const 兩版。
    SurfaceRecord* find(const SurfaceId& id);
    const SurfaceRecord* find(const SurfaceId& id) const;

    CapabilityMatrix caps_;
    // 以具名鍵配對記錄，順序即建立順序（永不以數字 index 對外暴露）。
    std::vector<std::pair<SurfaceId, SurfaceRecord>> surfaces_;
    bool initialized_ = false;
    std::size_t init_calls_ = 0;
    std::size_t shutdown_calls_ = 0;
    std::size_t poll_input_calls_ = 0;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_BACKEND_NULL_NULL_BACKEND_HPP
