// E9-10 已安裝套件登錄的持久化 — 契約測試（GoogleTest）
//
// 本測試組的重心是驗收標準那句『安裝 → 不載入 → 重啟 → 仍列得出該套件』，
// 其中**重啟**是重點（CHG-20260829-05 停點：記憶體登錄過不了）。
//
// 「重啟」在相位 1 的模擬機制（誠實說明，不誇大）：
//   後端（`MemoryInstallStorage`）是**相位 1 的替身**，其生命週期由呼叫端控制。本測試把後端
//   放在**外層 scope**，把登錄物件 `InstalledPackageStore` 放在**內層 scope**；內層結束後登錄
//   物件已解構，再以**新的** store 指向**同一個**存活的後端——這就是重啟後的狀態。
//   **刻意不寫**「對同一個還活著的 store 再查一次」那種寫法：它證明不了任何事。
//
// 涵蓋：序列化走 E7-12 / 產出為 E7-01 可再解析（含 format_version）、round-trip 不變式、
// 空登錄與空容器的已知限制、套件名含 ':' 仍安全（因其為值而非 map 鍵）、損毀文字明確報錯、
// write-through 落盤、重複登錄不覆寫、forget、字典序列舉、
// 以及三個獨立的「重啟」案例 + 與 E9-07 `DropInstaller` 的實際整合路徑。
// 平台中立：不含任何平台分支、不接真實檔案 I/O。
#include "install_store.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "system_event.hpp"  // E5-08：NullSystemEventSource（供 E9-07 DropInstaller 建構）

namespace {

using ds::events::NullSystemEventSource;
using ds::package::deserialize_install_records;
using ds::package::DropEvent;
using ds::package::DropInstaller;
using ds::package::InstalledPackageStore;
using ds::package::InstallRecord;
using ds::package::InstallRegistry;
using ds::package::kInstallRegistryFormat;
using ds::package::MemoryInstallStorage;
using ds::package::mirror_into;
using ds::package::Package;
using ds::package::PackageEntry;
using ds::package::parse_package;
using ds::package::serialize_install_records;
using ds::package::SourceContent;

// 一份最小合法套件描述（manifest + 內容清單），name 可注入。與 test_e9_07 同形。
std::string valid_package_text(const std::string& name) {
    return "format_version: 1.0\n"
           "name: " + name + "\n"
           "version: 0.1.0\n"
           "description: 範例模組\n"
           "requires: host.tray_icon, host.global_hotkey\n"
           "permissions: fs.read\n"
           "---\n"
           "asset: icons/tray.png\n"
           "code: main.wasm\n";
}

// 以 E9-01 解析出一個 Package（測試前置，不是被測對象）。
Package make_package(const std::string& name) {
    const auto r = parse_package(valid_package_text(name));
    if (!r.ok()) {
        throw std::runtime_error("測試前置失敗：套件描述無法解析 —— " + r.error().message);
    }
    return r.package();
}

// 僅含 manifest 的套件（內含項目清單為空）——用於釘住 E7-01 空容器限制的處理。
Package make_manifest_only_package(const std::string& name) {
    const auto r = parse_package("format_version: 1.0\nname: " + name + "\n");
    if (!r.ok()) {
        throw std::runtime_error("測試前置失敗：" + r.error().message);
    }
    return r.package();
}

// 一筆紀錄。
InstallRecord make_record(const std::string& name) {
    InstallRecord rec;
    rec.package = make_package(name);
    return rec;
}

// 計數型拖放來源：記錄 `open()`（= 讀取 / 載入套件本體描述）被呼叫幾次。
// 這是「不載入」那一步的**可證據化**工具：重啟後列舉已安裝套件時，計數不得增加。
class CountingInstallSource : public ds::package::InstallSource {
public:
    CountingInstallSource(std::string label, std::string text)
        : label_(std::move(label)), text_(std::move(text)) {}

    std::string label() const override { return label_; }

    SourceContent open() const override {
        ++opens_;
        SourceContent c;
        c.available = true;
        c.text = text_;
        return c;
    }

    int opens() const noexcept { return opens_; }

private:
    std::string label_;
    std::string text_;
    mutable int opens_ = 0;
};

// =============================================================================
// 1. 序列化 / 反序列化：沿用 E7-01 文字格式 + E7-12 寫回，不發明新格式
// =============================================================================

// 產出必須是 E7-01 `parse()` 能再吃回去的合法輸入，且帶本格式自身的 format_version。
TEST(E9_10_Format, SerializedTextIsAValidE7_01DocumentCarryingFormatVersion) {
    const std::string text = serialize_install_records({make_record("com.example.hello")});

    const auto parsed = ds::format::parse(text);
    ASSERT_TRUE(parsed.ok()) << "行 " << parsed.error().line << "：" << parsed.error().message;
    EXPECT_EQ(parsed.document().format_version, kInstallRegistryFormat);
    ASSERT_TRUE(parsed.document().root.is_map());
    EXPECT_TRUE(parsed.document().root.contains("packages"));

    // 首行就是 E7-12 `serialize` 寫出的版本欄位。
    EXPECT_EQ(text.rfind("format_version: 1.0", 0), 0u) << text;
}

// round-trip 不變式：紀錄 → 文字 → 紀錄，內容一致。
TEST(E9_10_Format, RecordsRoundTripThroughDeclarativeText) {
    const std::vector<InstallRecord> original{make_record("com.example.alpha"),
                                              make_record("com.example.beta")};

    const std::vector<InstallRecord> restored =
        deserialize_install_records(serialize_install_records(original));

    ASSERT_EQ(restored.size(), original.size());
    EXPECT_EQ(restored[0], original[0]);
    EXPECT_EQ(restored[1], original[1]);
}

// E7-12 已知限制：空容器序列化後再解析會塌成 Null。
// 本實作把「缺鍵 / Null」一律視為空清單，故空登錄仍 round-trip 為空登錄（不是錯誤）。
TEST(E9_10_Format, EmptyRegistryRoundTripsAsEmptyRatherThanFailing) {
    const std::string text = serialize_install_records({});
    EXPECT_TRUE(deserialize_install_records(text).empty());
}

// 同一個限制在 entries / requires / permissions 這三個清單上也成立。
TEST(E9_10_Format, ManifestOnlyPackageRoundTripsWithEmptyLists) {
    const InstallRecord rec{make_manifest_only_package("com.example.bare")};

    const auto restored = deserialize_install_records(serialize_install_records({rec}));
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored[0], rec);
    EXPECT_TRUE(restored[0].package.entries.empty());
    EXPECT_TRUE(restored[0].package.manifest.required_capabilities.empty());
    EXPECT_TRUE(restored[0].package.manifest.permissions.empty());
}

// 設計決定的驗證：套件名放在**值**的位置（非 map 鍵），故可安全承載 E7-12 的「map 鍵不支援
// 引號」所禁止的字元（如 ':'）。值由 E7-12 自動引號化 / 轉義，型別與內容不漂移。
TEST(E9_10_Format, PackageNameWithColonSurvivesBecauseItIsAValueNotAMapKey) {
    Package pkg = make_manifest_only_package("com.example.plain");
    pkg.manifest.name = "weird: name";  // E9-02 對 name 只要求非空，未禁止 ':'。

    const auto restored = deserialize_install_records(serialize_install_records({{pkg}}));
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored[0].package_id(), "weird: name");
}

// 不靜默失敗：損毀文字 / 缺必要欄位 / 文件內重名 一律明確拋錯。
TEST(E9_10_Format, CorruptTextThrowsInsteadOfSilentlyYieldingNothing) {
    EXPECT_THROW(deserialize_install_records("format_version: 1.0\n\tbad: indent\n"),
                 std::runtime_error);
    EXPECT_THROW(deserialize_install_records("name: no_format_version\n"), std::runtime_error);
}

TEST(E9_10_Format, PackagesWithWrongShapeOrMissingNameThrow) {
    // packages 非清單。
    EXPECT_THROW(deserialize_install_records("format_version: 1.0\npackages: 3\n"),
                 std::runtime_error);
    // 項目缺 name。
    EXPECT_THROW(deserialize_install_records("format_version: 1.0\n"
                                            "packages:\n"
                                            "  -\n"
                                            "    format_major: 1\n"
                                            "    format_minor: 0\n"),
                 std::runtime_error);
}

TEST(E9_10_Format, DuplicatePackageIdIsRejectedOnBothDirections) {
    EXPECT_THROW(serialize_install_records({make_record("com.example.dup"),
                                            make_record("com.example.dup")}),
                 std::runtime_error);

    const std::string one = serialize_install_records({make_record("com.example.dup")});
    // 把單筆文件的 packages 清單手動複製成兩筆同名 → 反序列化須拋錯。
    const std::string two = one + one.substr(one.find("  -\n"));
    EXPECT_THROW(deserialize_install_records(two), std::runtime_error);
}

// =============================================================================
// 2. 登錄門面：write-through、不覆寫、移除、列舉
// =============================================================================

TEST(E9_10_Store, RecordIsWrittenThroughToTheBackendImmediately) {
    MemoryInstallStorage storage;
    InstalledPackageStore store(storage);

    EXPECT_FALSE(storage.has("installed"));
    ASSERT_TRUE(store.record(make_package("com.example.hello")));

    // 後端此刻已有可再解析的登錄文字——不存在「忘了存檔」的窗口。
    ASSERT_TRUE(storage.has("installed"));
    const auto records = deserialize_install_records(store.serialized());
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].package_id(), "com.example.hello");
}

TEST(E9_10_Store, DuplicateRecordDoesNotOverwriteAndLeavesBackendUnchanged) {
    MemoryInstallStorage storage;
    InstalledPackageStore store(storage);
    ASSERT_TRUE(store.record(make_package("com.example.hello")));
    const std::string before = store.serialized();

    EXPECT_FALSE(store.record(make_package("com.example.hello")));
    EXPECT_EQ(store.size(), 1u);
    EXPECT_EQ(store.serialized(), before);
}

TEST(E9_10_Store, ForgetRemovesTheRecordAndPersistsTheRemoval) {
    MemoryInstallStorage storage;
    {
        InstalledPackageStore store(storage);
        store.record(make_package("com.example.hello"));
        store.record(make_package("com.example.other"));
        EXPECT_TRUE(store.forget("com.example.hello"));
        EXPECT_FALSE(store.forget("com.example.hello"));  // 第二次查無 → false，不拋錯。
    }
    // 重啟後移除仍然成立（移除也落盤了）。
    const InstalledPackageStore after(storage);
    EXPECT_FALSE(after.contains("com.example.hello"));
    EXPECT_TRUE(after.contains("com.example.other"));
}

TEST(E9_10_Store, ListIsLexicographicAndStable) {
    MemoryInstallStorage storage;
    InstalledPackageStore store(storage);
    store.record(make_package("com.example.zeta"));
    store.record(make_package("com.example.alpha"));
    store.record(make_package("com.example.mid"));

    EXPECT_EQ(store.list(), (std::vector<std::string>{"com.example.alpha", "com.example.mid",
                                                      "com.example.zeta"}));
}

// 初次啟動（後端尚無 slot）是合法空登錄，不是錯誤。
TEST(E9_10_Store, FirstStartOnAnEmptyBackendIsNotAnError) {
    MemoryInstallStorage storage;
    const InstalledPackageStore store(storage);
    EXPECT_EQ(store.size(), 0u);
    EXPECT_TRUE(store.list().empty());
}

// 但後端「有內容卻損毀」必須拋錯——靜默當成空登錄會悄悄遺失所有安裝紀錄。
TEST(E9_10_Store, CorruptBackendContentThrowsRatherThanLosingEveryInstall) {
    MemoryInstallStorage storage;
    storage.put("installed", "format_version: 1.0\npackages:\n  -\n    name:\n");
    EXPECT_THROW(InstalledPackageStore store(storage), std::runtime_error);
}

// =============================================================================
// 3. 驗收重點：安裝 → 不載入 → **重啟** → 仍列得出該套件
//    三個獨立案例，各自證明一件事。
// =============================================================================

// 【重啟 1／3】登錄物件被解構後重新建構，後端存活 → 仍列得出。
//
// 結構刻意如此：storage 在外層 scope；store 在內層 scope，內層結束即解構。
// 之後建立的是一個**全新的** store 物件，它只能從後端取得資訊。
TEST(E9_10_Restart, InstalledPackageIsStillListedAfterTheStoreObjectIsDestroyedAndRebuilt) {
    MemoryInstallStorage storage;  // ← 後端存活於外層 scope（相位 1 的「跨重啟」媒介）

    {
        InstalledPackageStore store_before_restart(storage);
        ASSERT_TRUE(store_before_restart.record(make_package("com.example.hello")));
        ASSERT_TRUE(store_before_restart.contains("com.example.hello"));
    }  // ← store 解構：此行之後，記憶體登錄已經不存在了

    // 全新的登錄物件，指向同一個後端 —— 這就是「重啟後」。
    const InstalledPackageStore store_after_restart(storage);
    EXPECT_EQ(store_after_restart.size(), 1u);
    EXPECT_EQ(store_after_restart.list(), (std::vector<std::string>{"com.example.hello"}));
    EXPECT_TRUE(store_after_restart.contains("com.example.hello"));
}

// 【重啟 2／3】「不載入」可證：重啟後列舉，套件來源的 `open()` 計數不增加。
//
// 安裝走 E9-07 `DropInstaller` 的真實路徑（它會 open() 來源一次）。重啟後的列舉完全不碰來源，
// 故計數停在 1 —— 光靠持久化紀錄就列得出該套件。
TEST(E9_10_Restart, ListingAfterRestartNeverReopensThePackageSource) {
    MemoryInstallStorage storage;
    CountingInstallSource source("com.example.hello.pkg", valid_package_text("com.example.hello"));

    {
        NullSystemEventSource sys;
        InstallRegistry memory_registry;  // E9-07 的記憶體安裝目標
        DropInstaller installer(memory_registry, sys);

        DropEvent drop;
        drop.source = &source;
        drop.detail = "dropped onto tray";

        const auto r = installer.handle_drop(drop);
        ASSERT_TRUE(r.ok()) << r.message;
        ASSERT_EQ(source.opens(), 1) << "安裝本身應讀取來源恰好一次";

        // E9-10：把 E9-07 剛登錄的套件中繼資料持久化。
        const Package* installed = memory_registry.find("com.example.hello");
        ASSERT_NE(installed, nullptr);
        InstalledPackageStore store(storage);
        ASSERT_TRUE(store.record(*installed));
    }  // ← installer / memory_registry / store 全部解構

    const int opens_before_restart = source.opens();

    // 重啟：全新 store，只有後端可用。
    const InstalledPackageStore store_after_restart(storage);
    const std::vector<std::string> listed = store_after_restart.list();

    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed[0], "com.example.hello");
    // 這一行是「不載入」的證據：列舉過程完全沒有再去讀 / 載入套件本體。
    EXPECT_EQ(source.opens(), opens_before_restart);
    EXPECT_EQ(source.opens(), 1);
}

// 【重啟 3／3】重啟後可把一個**全新的** E9-07 `InstallRegistry` 回填，
// 且整個過程中**不存在任何 `InstallSource`**（來源物件已在重啟前銷毀）。
TEST(E9_10_Restart, FreshInstallRegistryIsRehydratedFromStorageWithoutAnySource) {
    MemoryInstallStorage storage;

    {
        // 來源與安裝器都只活在這個 scope 裡。
        auto source = std::make_unique<CountingInstallSource>(
            "com.example.hello.pkg", valid_package_text("com.example.hello"));
        NullSystemEventSource sys;
        InstallRegistry memory_registry;
        DropInstaller installer(memory_registry, sys);

        DropEvent drop;
        drop.source = source.get();
        ASSERT_TRUE(installer.handle_drop(drop).ok());

        InstalledPackageStore store(storage);
        ASSERT_TRUE(store.record(*memory_registry.find("com.example.hello")));
    }  // ← 來源物件在此銷毀：之後任何「載入套件本體」都已物理上不可能

    // 重啟：全新 store + 全新且空白的 InstallRegistry。
    const InstalledPackageStore store_after_restart(storage);
    InstallRegistry fresh_registry;
    ASSERT_EQ(fresh_registry.size(), 0u);

    EXPECT_EQ(mirror_into(store_after_restart, fresh_registry), 1u);
    EXPECT_EQ(fresh_registry.size(), 1u);
    EXPECT_TRUE(fresh_registry.contains("com.example.hello"));

    // 再回填一次不重複計數（E9-07 add 對同名不覆寫）。
    EXPECT_EQ(mirror_into(store_after_restart, fresh_registry), 0u);
    EXPECT_EQ(fresh_registry.size(), 1u);
}

// 重啟後拿到的不只是名字：manifest 中繼資料與內含項目清單都還在
//（下游 E7-16 設定分頁 / E9-11 權限狀態要的就是這些，且不需要載入套件）。
TEST(E9_10_Restart, ManifestMetadataAndEntryInventorySurviveRestart) {
    MemoryInstallStorage storage;
    {
        InstalledPackageStore store(storage);
        store.record(make_package("com.example.hello"));
    }

    const InstalledPackageStore after(storage);
    const Package* pkg = after.find("com.example.hello");
    ASSERT_NE(pkg, nullptr);

    EXPECT_EQ(pkg->manifest.name, "com.example.hello");
    EXPECT_EQ(pkg->manifest.format_version.major, 1);
    EXPECT_EQ(pkg->manifest.format_version.minor, 0);
    EXPECT_EQ(pkg->manifest.version, "0.1.0");
    EXPECT_EQ(pkg->manifest.description, "範例模組");
    EXPECT_EQ(pkg->manifest.required_capabilities,
              (std::vector<std::string>{"host.tray_icon", "host.global_hotkey"}));
    EXPECT_EQ(pkg->manifest.permissions, (std::vector<std::string>{"fs.read"}));

    ASSERT_EQ(pkg->entries.size(), 2u);
    EXPECT_EQ(pkg->entries[0].kind, "asset");
    EXPECT_EQ(pkg->entries[0].logical_path, "icons/tray.png");
    EXPECT_EQ(pkg->entries[1].kind, "code");
    EXPECT_EQ(pkg->entries[1].logical_path, "main.wasm");

    // find() 未存在者回 nullptr（不靜默造出空套件）。
    EXPECT_EQ(after.find("com.example.absent"), nullptr);
}

// 多個套件、多次重啟都穩定（登錄不隨重啟衰減）。
TEST(E9_10_Restart, MultiplePackagesSurviveRepeatedRestarts) {
    MemoryInstallStorage storage;
    {
        InstalledPackageStore s(storage);
        s.record(make_package("com.example.alpha"));
        s.record(make_package("com.example.beta"));
    }
    {
        InstalledPackageStore s(storage);  // 第一次重啟
        EXPECT_EQ(s.size(), 2u);
        s.record(make_package("com.example.gamma"));
    }
    const InstalledPackageStore s(storage);  // 第二次重啟
    EXPECT_EQ(s.list(), (std::vector<std::string>{"com.example.alpha", "com.example.beta",
                                                  "com.example.gamma"}));
}

// 多 slot：同一後端可承載多份互不干擾的登錄（後端介面的 names() 可列舉）。
TEST(E9_10_Store, SeparateSlotsAreIndependentOnTheSameBackend) {
    MemoryInstallStorage storage;
    {
        InstalledPackageStore a(storage, "profile.a");
        a.record(make_package("com.example.alpha"));
        InstalledPackageStore b(storage, "profile.b");
        b.record(make_package("com.example.beta"));
    }
    EXPECT_EQ(storage.names(), (std::vector<std::string>{"profile.a", "profile.b"}));
    EXPECT_EQ(InstalledPackageStore(storage, "profile.a").list(),
              (std::vector<std::string>{"com.example.alpha"}));
    EXPECT_EQ(InstalledPackageStore(storage, "profile.b").list(),
              (std::vector<std::string>{"com.example.beta"}));
}

TEST(E9_10_Store, EmptySlotNameIsRejected) {
    MemoryInstallStorage storage;
    EXPECT_THROW(InstalledPackageStore(storage, ""), std::runtime_error);
}

// reload()：後端被外部改寫後可重讀（與重啟同一條路徑，不同入口）。
TEST(E9_10_Store, ReloadPicksUpExternalBackendChanges) {
    MemoryInstallStorage storage;
    InstalledPackageStore store(storage);
    store.record(make_package("com.example.hello"));

    storage.put("installed", serialize_install_records({make_record("com.example.other")}));
    EXPECT_TRUE(store.contains("com.example.hello"));  // 尚未重讀
    store.reload();
    EXPECT_FALSE(store.contains("com.example.hello"));
    EXPECT_TRUE(store.contains("com.example.other"));
}

// 空名套件不得登錄（不靜默接受一筆無法定址的紀錄）。
TEST(E9_10_Store, RecordingAPackageWithEmptyNameThrows) {
    MemoryInstallStorage storage;
    InstalledPackageStore store(storage);
    Package pkg = make_manifest_only_package("com.example.plain");
    pkg.manifest.name.clear();
    EXPECT_THROW(store.record(pkg), std::runtime_error);
}

}  // namespace
