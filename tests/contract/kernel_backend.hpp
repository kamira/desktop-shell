// E1-25 kernel 後端抽象介面 — 契約測試組專用（平台中立）
//
// 這是「所有 kernel 後端都必須實作並通過契約」的抽象介面定義。相位 1（Mac/null 期）
// 尚無任何真實後端合併進 repo，且此介面本身為**純抽象、無現成實作**——因此依單元規格，
// 介面與其最小 stub 由契約測試組自帶（見 stub_backend.hpp），不 link E1-24 null 後端，
// 以維持 E1-25 與 E1-24 的單元獨立（units.json 中兩者僅相依 E1-21、不互相依）。
//
// 平台中立硬約束：本檔**不得**出現 `sys.platform` / `#ifdef _WIN32` / `__APPLE__` /
// `win32` / `cocoa` 等平台分支。任一符合此介面的後端（null / 未來 win32 / cocoa）
// 都套用同一組契約斷言（見 test_contract_backend.cpp）。
//
// 硬約束（NFR-02）：介面不得出現絕對座標與數字 z-order —— surface 一律以**具名角色**
// （SurfaceProfile.role）表達，不以像素尺寸 / (x,y) / 數字層級表達。
// 硬約束（NFR-03）：能力閘控查詢一律經 has()；未宣告能力保守回不可用。
#ifndef DS_CONTRACT_KERNEL_BACKEND_HPP
#define DS_CONTRACT_KERNEL_BACKEND_HPP

#include <string>

#include "capability_matrix.hpp"  // E1-21：CapabilityMatrix / CapabilityId

namespace ds::kernel::contract {

// 後端操作的統一結果碼（平台中立、跨後端一致）。
//
// 契約要求所有後端以相同語意回報，讓上層無需平台分支即可處理各後端結果。
enum class Status {
    Ok,           // 操作成功
    Unsupported,  // 該能力於此後端不可用（能力閘控應先擋；此為錯誤處理契約的第二層保險）
    Invalid,      // 前置條件不滿足（如未初始化、空 role、無效 / 未知 handle）
};

// surface 的宣告式建立參數（平台中立）。
//
// 以**具名角色**表達（如 "surface.character" / "surface.launcher" / "surface.overlay"），
// 刻意不含像素尺寸 / 絕對座標 / 數字 z-order（NFR-02）。
struct SurfaceProfile {
    std::string role;  // 具名角色識別碼；契約要求非空
};

// surface 的不透明控制碼。kInvalidSurface(0) 保留給「無效 / 未建立」。
using SurfaceHandle = unsigned long long;
inline constexpr SurfaceHandle kInvalidSurface = 0;

// kernel 後端抽象介面 —— 所有後端共同契約。
//
// 純虛介面；不含任何平台分支。契約測試對「任一符合此介面的後端」跑同一批斷言，
// 因此新增後端（null/win32/cocoa）無需改契約，只需實作本介面並註冊一個 factory。
class KernelBackend {
public:
    virtual ~KernelBackend() = default;

    // --- 能力查詢契約（NFR-03）---
    // 後端的能力矩陣：能力查詢的單一資料來源。
    virtual const CapabilityMatrix& capabilities() const = 0;
    // 能力閘控查詢入口。契約要求：須與 capabilities().has(id) 完全一致；
    // 未宣告能力保守回 false。
    virtual bool has(const CapabilityId& id) const = 0;

    // --- 生命週期契約 ---
    // 初始化後端。契約要求冪等：重複呼叫仍回 Ok，狀態不變壞。
    virtual Status initialize() = 0;
    // 就緒狀態：initialize() 後為 true、shutdown() 後為 false、初始未 init 為 false。
    virtual bool is_ready() const = 0;
    // 關閉後端並釋放所有 surface。契約要求冪等：未初始化亦可安全呼叫、可重複呼叫。
    virtual Status shutdown() = 0;

    // --- surface 基元契約 ---
    // 前置條件：後端須已 initialize()，否則回 Invalid 且 out 必為 kInvalidSurface；
    // profile.role 非空，否則回 Invalid。
    // 後置條件：成功回 Ok 且 out 為有效（非 kInvalidSurface）handle、surface_alive(out)==true。
    virtual Status create_surface(const SurfaceProfile& profile, SurfaceHandle& out) = 0;
    // 銷毀 surface。無效 / 未知 / 已銷毀 handle 回 Invalid（不崩潰）；成功回 Ok。
    virtual Status destroy_surface(SurfaceHandle handle) = 0;
    // 該 handle 是否為目前存活的 surface。無效 / 未知 handle 回 false。
    virtual bool surface_alive(SurfaceHandle handle) const = 0;

    // --- 錯誤處理契約（能力閘控操作範例）---
    // 對某能力執行的操作：has(id)==false（含未宣告能力）時回 Unsupported、且不改任何狀態；
    // has(id)==true 時回 Ok。示範「未 has() 保護的操作也不會崩潰」。
    virtual Status invoke_capability(const CapabilityId& id) = 0;
};

}  // namespace ds::kernel::contract

#endif  // DS_CONTRACT_KERNEL_BACKEND_HPP
