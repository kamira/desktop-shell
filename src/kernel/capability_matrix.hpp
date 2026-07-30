// E1-21 能力矩陣宣告檔 — 平台中立介面
//
// 宣告「哪些能力可能在某些平台不存在」。這是後續 `has()` 能力閘控（NFR-03）
// 與降級路徑的**單一資料來源**。
//
// 相位 1（Mac / null 期）約束：
//   - 只有介面 + 宣告式（null）行為，不綁任何真實平台後端。
//   - 不得出現 `#ifdef _WIN32` 等平台分支；跨平台性由 API 面約束保證，不由語言保證。
//   - `has()` 回傳宣告的預設可用性；真實後端上線後由後端覆寫實際探測結果。
#ifndef DS_KERNEL_E1_21_CAPABILITY_MATRIX_HPP
#define DS_KERNEL_E1_21_CAPABILITY_MATRIX_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace ds::kernel {

// 能力的穩定識別碼（如 "host.tray_icon"）。跨平台一致，不隨後端而變。
using CapabilityId = std::string;

// 單一能力的宣告。
//
// 「能力矩陣」即一組 CapabilityDecl —— 純資料，不含任何平台判斷邏輯。
struct CapabilityDecl {
    CapabilityId id;          // 穩定識別碼
    std::string description;  // 人類可讀說明
    bool optional;            // true = 可能在某些平台不存在，呼叫端必須先 has() 閘控並備妥降級路徑
    bool default_available;   // 尚無真實後端探測前（相位 1 null 期）的預設可用性
};

// 能力矩陣的載入 / 查詢介面。
//
// 建構來源有二：
//   - CapabilityMatrix::defaults()：內嵌的預設宣告（相位 1 的單一資料來源）。
//   - CapabilityMatrix(decls)：由外部一組宣告建構（供測試，及未來從宣告檔載入）。
class CapabilityMatrix {
public:
    // 由一組宣告建構。若同一 id 重複，後者覆蓋前者（後定義者為準）。
    explicit CapabilityMatrix(std::vector<CapabilityDecl> decls);

    // 內嵌預設能力矩陣（相位 1 唯一資料來源）。
    static CapabilityMatrix defaults();

    // NFR-03 能力閘控的查詢入口：該能力目前是否可用。
    // 相位 1 回傳宣告的 default_available。
    // **未知能力一律回 false**（保守：未宣告即不可用），呼叫端因此永遠安全。
    bool has(const CapabilityId& id) const;

    // 該能力是否被宣告為 optional（可能在某些平台不存在）。未知回 false。
    bool is_optional(const CapabilityId& id) const;

    // 該能力是否存在於宣告表中。
    bool is_declared(const CapabilityId& id) const;

    // 查詢單一宣告；未知回 nullptr。回傳指標於本物件存活期間有效。
    const CapabilityDecl* find(const CapabilityId& id) const;

    // 全部宣告（宣告順序）。
    const std::vector<CapabilityDecl>& all() const noexcept { return decls_; }

    // 宣告數量。
    std::size_t size() const noexcept { return decls_.size(); }

private:
    std::vector<CapabilityDecl> decls_;
};

}  // namespace ds::kernel

#endif  // DS_KERNEL_E1_21_CAPABILITY_MATRIX_HPP
