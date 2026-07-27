// E9-07 拖放安裝 — 實作（平台中立、純邏輯，無任何平台分支）。
#include "drop_install.hpp"

#include <string>
#include <utility>

namespace ds::package {

// ---- MemoryInstallSource ----------------------------------------------------

MemoryInstallSource MemoryInstallSource::with_text(std::string label, std::string text) {
    MemoryInstallSource s;
    s.label_ = std::move(label);
    s.available_ = true;
    s.text_ = std::move(text);
    return s;
}

MemoryInstallSource MemoryInstallSource::unavailable(std::string label) {
    MemoryInstallSource s;
    s.label_ = std::move(label);
    s.available_ = false;
    return s;
}

SourceContent MemoryInstallSource::open() const {
    SourceContent c;
    c.available = available_;
    if (available_) {
        c.text = text_;
    }
    return c;
}

// ---- InstallRegistry --------------------------------------------------------

bool InstallRegistry::contains(const std::string& name) const {
    return by_name_.find(name) != by_name_.end();
}

bool InstallRegistry::add(const Package& pkg) {
    // 以 manifest.name 為鍵；同名已存在則不覆寫（重複安裝由呼叫端明確處理）。
    return by_name_.emplace(pkg.manifest.name, pkg).second;
}

const Package* InstallRegistry::find(const std::string& name) const {
    const auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &it->second;
}

// ---- DropInstaller ----------------------------------------------------------

DropInstaller::DropInstaller(InstallRegistry& registry,
                             ds::events::SystemEventSource& system_events)
    : registry_(registry), system_events_(system_events) {
    // 訂閱 E5-08 系統事件以維護安裝閘門。實際呼叫 E5-08 的分派實作。
    sub_ = system_events_.subscribe(
        [this](const ds::events::SystemEvent& ev) { on_system_event(ev); });
}

DropInstaller::~DropInstaller() {
    if (sub_ != 0) {
        system_events_.unsubscribe(sub_);
    }
}

void DropInstaller::set_lifecycle_listener(InstallLifecycleListener listener) {
    lifecycle_ = std::move(listener);
}

void DropInstaller::on_system_event(const ds::events::SystemEvent& ev) {
    using ds::events::SystemEventType;
    switch (ev.type) {
        case SystemEventType::SessionLocked:
        case SystemEventType::SystemSleep:
            gate_open_ = false;  // session 不在場 / 系統睡眠：拒絕拖放安裝。
            break;
        case SystemEventType::SessionUnlocked:
        case SystemEventType::SystemWake:
            gate_open_ = true;  // 重新開啟閘門。
            break;
        case SystemEventType::DisplayChanged:
        case SystemEventType::PowerStatusChanged:
            // 與安裝閘門無關：不改變狀態。
            break;
    }
}

void DropInstaller::report(InstallStage stage, bool ok, const std::string& message,
                           const std::string& name) {
    if (lifecycle_) {
        lifecycle_(InstallLifecycleEvent{stage, ok, message, name});
    }
}

InstallResult DropInstaller::fail(InstallStage stage, std::string message, std::string name) {
    report(stage, /*ok=*/false, message, name);
    InstallResult r;
    r.status = InstallStatus::Failed;
    r.stage = stage;
    r.package_name = std::move(name);
    r.message = std::move(message);
    return r;
}

InstallResult DropInstaller::handle_drop(const DropEvent& drop) {
    // 階段 1：接收。格式不正的拖放（無來源）即為無效，明確報錯。
    if (drop.source == nullptr) {
        return fail(InstallStage::Received, "無效拖放：來源為空");
    }
    report(InstallStage::Received, /*ok=*/true, "已接收拖放：" + drop.source->label(), {});

    // 閘門檢查：session 鎖定 / 系統睡眠時拒絕安裝（不靜默丟棄）。
    if (!gate_open_) {
        return fail(InstallStage::Received,
                    "安裝已封鎖：session 鎖定或系統睡眠中，暫不接受拖放安裝");
    }

    // 階段 2：解析來源。無法讀取的來源 = 無效來源，明確報錯。
    const SourceContent content = drop.source->open();
    if (!content.available) {
        return fail(InstallStage::Resolved, "無效來源：無法讀取 " + drop.source->label());
    }
    report(InstallStage::Resolved, /*ok=*/true, "來源已解析：" + drop.source->label(), {});

    // 階段 3：解析為 E9-01 套件（解析即驗證，含 E9-02 格式相容性檢查——不相容套件於此報錯）。
    const PackageResult parsed = parse_package(content.text);
    if (!parsed.ok()) {
        const ParseError& e = parsed.error();
        std::string msg = "套件解析失敗";
        if (e.line != 0) {
            msg += "（第 " + std::to_string(e.line) + " 行）";
        }
        msg += "：" + e.message;
        return fail(InstallStage::Parsed, std::move(msg));
    }
    const Package& pkg = parsed.package();
    report(InstallStage::Parsed, /*ok=*/true, "套件已解析", pkg.manifest.name);

    // 階段 4：結構完整性驗證（E9-01 validate_package；對已構成 Package 的獨立防線）。
    const PackageResult validated = validate_package(pkg);
    if (!validated.ok()) {
        return fail(InstallStage::Validated, "套件驗證失敗：" + validated.error().message,
                    pkg.manifest.name);
    }
    report(InstallStage::Validated, /*ok=*/true, "套件驗證通過", pkg.manifest.name);

    // 階段 5：登錄至記憶體登錄。重複安裝（同名已存在）明確報錯、不覆寫。
    if (registry_.contains(pkg.manifest.name)) {
        return fail(InstallStage::Registered, "套件已安裝：" + pkg.manifest.name,
                    pkg.manifest.name);
    }
    if (!registry_.add(pkg)) {
        // 理論上 contains 已擋下；防禦性處理，仍明確報錯不靜默。
        return fail(InstallStage::Registered, "登錄失敗：" + pkg.manifest.name,
                    pkg.manifest.name);
    }
    report(InstallStage::Registered, /*ok=*/true, "安裝完成", pkg.manifest.name);

    InstallResult r;
    r.status = InstallStatus::Success;
    r.stage = InstallStage::Registered;
    r.package_name = pkg.manifest.name;
    r.message = "安裝成功：" + pkg.manifest.name;
    return r;
}

}  // namespace ds::package
