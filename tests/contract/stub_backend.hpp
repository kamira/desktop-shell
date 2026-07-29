// E1-25 契約測試組專用 —— 測試本地最小 stub 後端
//
// 僅供跑契約：實作 KernelBackend 抽象介面，讓契約斷言有一個「符合介面的後端」可套用，
// 藉此驗證契約組本身可運行、且契約可套於任一實作。
//
// 刻意**不 link E1-24 null 後端**：維持 E1-25 與 E1-24 的單元獨立（units.json 中兩者
// 僅相依 E1-21、不互相依）。此 stub 與 E1-24 各自獨立實作同一契約——正是契約組的用途：
// 未來 E1-24 null 後端、win32、cocoa 都套同一批斷言驗證其符合契約。
//
// 平台中立：本檔無任何真實系統呼叫、無平台分支；能力矩陣取 E1-21 defaults()。
#ifndef DS_CONTRACT_STUB_BACKEND_HPP
#define DS_CONTRACT_STUB_BACKEND_HPP

#include <unordered_set>

#include "kernel_backend.hpp"

namespace ds::kernel::contract {

// 最小 stub 後端：以純記憶體狀態滿足 KernelBackend 契約。
class StubBackend final : public KernelBackend {
public:
    StubBackend() : caps_(CapabilityMatrix::defaults()) {}

    const CapabilityMatrix& capabilities() const override { return caps_; }
    bool has(const CapabilityId& id) const override { return caps_.has(id); }

    Status initialize() override {
        ready_ = true;  // 冪等：重複 init 維持 ready
        return Status::Ok;
    }
    bool is_ready() const override { return ready_; }
    Status shutdown() override {
        ready_ = false;
        live_.clear();  // 後置條件：釋放所有 surface
        return Status::Ok;  // 冪等：未 init / 重複 shutdown 皆安全
    }

    Status create_surface(const SurfaceProfile& profile, SurfaceHandle& out) override {
        out = kInvalidSurface;               // 失敗時輸出必為無效值（後置條件）
        if (!ready_) return Status::Invalid;  // 前置條件：須已 initialize()
        if (profile.role.empty()) return Status::Invalid;  // 前置條件：role 非空
        const SurfaceHandle h = next_++;
        live_.insert(h);
        out = h;
        return Status::Ok;
    }
    Status destroy_surface(SurfaceHandle handle) override {
        const auto it = live_.find(handle);
        if (it == live_.end()) return Status::Invalid;  // 無效 / 未知 / 已銷毀
        live_.erase(it);
        return Status::Ok;
    }
    bool surface_alive(SurfaceHandle handle) const override {
        return live_.find(handle) != live_.end();
    }

    Status invoke_capability(const CapabilityId& id) override {
        // 錯誤處理契約：不可用（含未宣告）一律 Unsupported、不改任何狀態。
        if (!has(id)) return Status::Unsupported;
        return Status::Ok;
    }

private:
    CapabilityMatrix caps_;
    bool ready_ = false;
    SurfaceHandle next_ = 1;  // 由 1 起，0 保留給 kInvalidSurface
    std::unordered_set<SurfaceHandle> live_;
};

}  // namespace ds::kernel::contract

#endif  // DS_CONTRACT_STUB_BACKEND_HPP
