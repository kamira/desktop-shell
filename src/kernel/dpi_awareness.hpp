// E1-17 DPI 感知與混合 DPI — 平台中立介面
//
// 多螢幕環境下，各螢幕的邏輯縮放（scale factor，如 1.0 / 1.5 / 2.0）可能不同
//（混合 DPI）。此單元宣告一組平台中立介面，讓上層以**每螢幕的縮放係數**表達縮放，
// 而非任何絕對像素座標或裝置相關數值。
//
// 相位 1（Mac / null 期）約束：
//   - 只有介面 + 宣告式（null）行為，不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` / `win32` / `cocoa` 等平台分支；跨平台性由 API 面約束
//     保證，不由語言保證。
//   - null 後端回傳宣告的預設縮放（`kDefaultScaleFactor`，1.0）。真實後端上線後由後端
//     以實際 per-monitor DPI 探測覆寫。
//
// 硬約束（NFR-02）：本介面**不得出現絕對座標與數字 z-order**。
//   - 螢幕一律以**具名識別碼**（`ScreenId`）指涉，不以 index / 位置 / (x,y) 指涉。
//   - 縮放一律以**縮放係數**（無單位比值）表達，不以像素尺寸表達。
//   - 沒有 `setPosition(x, y)` 式 API；跨螢幕搬移只由 `relative_scale()` 的比值描述。
#ifndef DS_KERNEL_E1_17_DPI_AWARENESS_HPP
#define DS_KERNEL_E1_17_DPI_AWARENESS_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ds::kernel {

// 螢幕的穩定具名識別碼（如 "screen.primary" / "screen.external-hdmi"）。
//
// 刻意用具名字串而非數字 index 或座標：NFR-02 禁止以絕對座標 / 數字 z-order 指涉螢幕。
// 具名識別碼跨平台一致、與螢幕實體排列位置無關。
using ScreenId = std::string;

// 相位 1（null 期）與未知螢幕的保守預設縮放係數。
//
// 1.0 = 邏輯單位與繪製單位一比一（不縮放），是換平台時最安全的中性值：
// 未知螢幕以此回傳可保證「不會誤放大 / 誤縮小」。
inline constexpr double kDefaultScaleFactor = 1.0;

// 單一螢幕的 DPI 宣告。
//
// 「DPI 拓撲」即一組 ScreenDpi —— 純資料，不含任何平台判斷邏輯，亦不含任何絕對座標。
struct ScreenDpi {
    ScreenId id;               // 穩定具名識別碼
    std::string description;   // 人類可讀說明
    double scale_factor;       // 邏輯縮放係數（> 0，如 1.0 / 1.5 / 2.0）
};

// 多螢幕 DPI 拓撲的查詢介面。
//
// 建構來源有二：
//   - DpiInfo::defaults()：內嵌的預設拓撲（相位 1 的單一資料來源；單螢幕、1.0）。
//   - DpiInfo(screens)：由外部一組宣告建構（供測試，及未來由真實後端以實際探測填入）。
//
// 混合 DPI 由「多個 scale_factor 不同的 ScreenDpi 並存」自然表達，各螢幕彼此獨立；
// 查詢某螢幕的縮放不受其他螢幕影響。
class DpiInfo {
public:
    // 由一組螢幕宣告建構。
    //   - 非正（<= 0）的 scale_factor 一律正規化為 kDefaultScaleFactor（保守：拒絕
    //     不合法縮放，退回中性值，呼叫端永遠安全）。
    //   - 若同一 id 重複，後者覆蓋前者（後定義者為準；供後端覆寫先前宣告）。
    explicit DpiInfo(std::vector<ScreenDpi> screens);

    // 內嵌預設 DPI 拓撲（相位 1 唯一資料來源）：單一具名主螢幕，縮放 kDefaultScaleFactor。
    static DpiInfo defaults();

    // 該螢幕是否存在於拓撲中。
    bool is_known(const ScreenId& id) const;

    // 查詢某螢幕的縮放係數。
    //   - 已知螢幕：回傳其宣告的 scale_factor。
    //   - **未知螢幕：保守回傳 kDefaultScaleFactor（1.0）**，呼叫端因此永不誤縮放。
    double scale_of(const ScreenId& id) const;

    // 兩螢幕間的相對縮放比值 = scale_of(to) / scale_of(from)。
    //
    // 用於描述「同一邏輯內容從 from 螢幕搬到 to 螢幕」時尺寸的縮放倍率 —— 純比值、
    // 無絕對座標，符合 NFR-02。任一端未知則以 kDefaultScaleFactor 計（保守）。
    double relative_scale(const ScreenId& from, const ScreenId& to) const;

    // 是否為混合 DPI：存在至少兩個 scale_factor 不同的螢幕。單螢幕或全同一律回 false。
    bool is_mixed_dpi() const;

    // 查詢單一螢幕宣告；未知回 nullptr。回傳指標於本物件存活期間有效。
    const ScreenDpi* find(const ScreenId& id) const;

    // 全部螢幕（宣告順序）。
    const std::vector<ScreenDpi>& screens() const noexcept { return screens_; }

    // 螢幕數量。
    std::size_t size() const noexcept { return screens_.size(); }

private:
    std::vector<ScreenDpi> screens_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_17_DPI_AWARENESS_HPP
