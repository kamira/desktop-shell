// W1-04 真實圖示資源 — gtest
//
// 兩條路徑都要驗，而且要能**分辨走了哪一條**：
//   1. 具名 id → .ico 檔路徑 → 真的從檔案載入
//   2. 檔案不存在 → 程式繪製的後備圖示（**不是** Windows 通用圖示）
//
// 第 2 條的區分特別重要：若後備直接用 `IDI_APPLICATION`，
// 「資源沒部署好」與「功能還沒做」在畫面上完全一樣，沒人查得出來。
#include <gtest/gtest.h>

#include <string>

#include "tray_win32.hpp"

using ds::host::Win32TrayBackend;

namespace {

// 系統共用的預設圖示——後備圖示**不得**等於它。
HICON system_default_icon() {
    return ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
}

struct AssetsEnvGuard {
    explicit AssetsEnvGuard(const wchar_t* value) {
        if (const wchar_t* old = ::_wgetenv(L"DESKTOP_SHELL_ASSETS")) previous_ = old;
        ::_wputenv_s(L"DESKTOP_SHELL_ASSETS", value);
    }
    ~AssetsEnvGuard() { ::_wputenv_s(L"DESKTOP_SHELL_ASSETS", previous_.c_str()); }
    std::wstring previous_;
};

std::wstring repo_assets() {
    const std::string s = DS_REPO_ASSETS_DIR;
    return std::wstring(s.begin(), s.end());
}

}  // namespace

// --- 路徑解析（純字串，不碰檔案系統）----------------------------------------

// 具名 id 去掉 `icon.` 前綴後即為套件內邏輯路徑，與 E9 的 `asset: icons/tray.png` 對齊。
TEST(TrayIcon, NamedIdMapsToIconsSubdirectory) {
    AssetsEnvGuard guard(L"C:\\base");
    EXPECT_EQ(Win32TrayBackend::icon_path_for("icon.tray"), L"C:\\base\\icons\\tray.ico");
}

// 沒有 `icon.` 前綴時整個 id 就是葉名（不強制前綴）。
TEST(TrayIcon, IdWithoutPrefixIsUsedVerbatim) {
    AssetsEnvGuard guard(L"C:\\base");
    EXPECT_EQ(Win32TrayBackend::icon_path_for("tray"), L"C:\\base\\icons\\tray.ico");
}

// 空 id / 只有前綴 → 回空字串，不得組出 `icons\.ico` 這種垃圾路徑。
TEST(TrayIcon, EmptyOrPrefixOnlyIdYieldsNoPath) {
    AssetsEnvGuard guard(L"C:\\base");
    EXPECT_TRUE(Win32TrayBackend::icon_path_for("").empty());
    EXPECT_TRUE(Win32TrayBackend::icon_path_for("icon.").empty());
}

// 沒設環境變數時退回執行檔所在目錄下的 assets/。
TEST(TrayIcon, FallsBackToExecutableDirectoryWhenEnvUnset) {
    AssetsEnvGuard guard(L"");
    const std::wstring p = Win32TrayBackend::icon_path_for("icon.tray");
    ASSERT_FALSE(p.empty());
    EXPECT_NE(p.find(L"\\assets\\icons\\tray.ico"), std::wstring::npos) << "路徑尾段不對";
    EXPECT_NE(p.find(L":\\"), std::wstring::npos) << "應為絕對路徑";
}

// --- 實際載入 ---------------------------------------------------------------

// 指到 repo 的 assets/ 時，必須**真的從檔案載入**（而非落到後備）。
TEST(TrayIcon, LoadsRealIconFromFile) {
    AssetsEnvGuard guard(repo_assets().c_str());
    Win32TrayBackend b;
    b.set_icon("icon.tray");
    EXPECT_TRUE(b.icon_from_file()) << "assets/icons/tray.ico 應該讀得到";
}

// 檔案不存在 → 走後備，且後備**不得**是 Windows 通用圖示。
TEST(TrayIcon, MissingFileUsesDrawnFallbackNotSystemIcon) {
    AssetsEnvGuard guard(L"C:\\definitely\\not\\a\\real\\assets\\dir");
    Win32TrayBackend b;
    b.set_icon("icon.tray");
    EXPECT_FALSE(b.icon_from_file()) << "路徑不存在時不該宣稱從檔案載入";
    // 後備必須是自己畫的，不能是系統共用圖示——否則資源沒部署與功能沒做長得一樣。
    EXPECT_NE(b.native_icon(), system_default_icon());
    EXPECT_NE(b.native_icon(), nullptr);
}

// 重複切換圖示不得洩漏 handle：連續換多次後仍可正常加入系統匣。
// （真正的洩漏要靠工具才測得出來，這裡至少釘住「換完還能用」與所有權旗標的一致性。）
TEST(TrayIcon, RepeatedIconSwitchesStayConsistent) {
    Win32TrayBackend b;
    for (int i = 0; i < 5; ++i) {
        AssetsEnvGuard good(repo_assets().c_str());
        b.set_icon("icon.tray");
        EXPECT_TRUE(b.icon_from_file());
    }
    {
        AssetsEnvGuard bad(L"C:\\nope");
        b.set_icon("icon.tray");
        EXPECT_FALSE(b.icon_from_file());
    }
    {
        AssetsEnvGuard good(repo_assets().c_str());
        b.set_icon("icon.tray");
        EXPECT_TRUE(b.icon_from_file());
    }
    b.show();
    EXPECT_TRUE(b.icon_added()) << "多次換圖後仍須能加入系統匣";
    b.hide();
}

// 加入系統匣後再換圖示，走 NIM_MODIFY 而非重新加入。
TEST(TrayIcon, ChangingIconWhileShownKeepsItAdded) {
    AssetsEnvGuard guard(repo_assets().c_str());
    Win32TrayBackend b;
    b.set_icon("icon.tray");
    b.show();
    ASSERT_TRUE(b.icon_added());
    b.set_icon("icon.tray");
    EXPECT_TRUE(b.icon_added());
    b.hide();
}
