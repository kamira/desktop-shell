// E5-09 機率排程器 — 可注入隨機來源
//
// 機率觸發需要亂數，但為了「測試決定性可重現」，本單元**不使用全域 rand()**，
// 而是把亂數抽象成一個可注入 / 可替換的來源介面 `RandomSource`。
//   - 測試可注入固定種子的 PRNG，或一個回傳預定序列的假來源，斷言確定的觸發結果。
//   - 產品可注入同一個固定種子（或以真實熵播種）的 PRNG。
//
// 平台中立純邏輯：不綁任何 OS 熵源、無平台分支。
#ifndef DS_EVENTS_E5_09_RANDOM_SOURCE_HPP
#define DS_EVENTS_E5_09_RANDOM_SOURCE_HPP

#include <cstdint>

namespace ds::events {

// 亂數來源介面：每次 next_unit() 回傳一個 [0,1) 的均勻亂數。
// 機率排程器只依賴此介面——換 PRNG / 換假來源皆不動排程邏輯。
class RandomSource {
public:
    virtual ~RandomSource() = default;

    // 下一個均勻亂數，值域 [0,1)。
    virtual double next_unit() = 0;
};

// 預設實作：SplitMix64 PRNG。以 64-bit 種子播種，序列完全由種子決定 —— 決定性可重現。
// 不依賴任何全域狀態或 OS 熵；同一種子在任何平台產生相同序列。
class SeededRandomSource : public RandomSource {
public:
    explicit SeededRandomSource(std::uint64_t seed = 0) noexcept : state_(seed) {}

    double next_unit() noexcept override {
        // SplitMix64：混合後取高 53 bits 映射到 [0,1)，避免低位偏差。
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);  // 2^53
    }

private:
    std::uint64_t state_;
};

}  // namespace ds::events

#endif  // DS_EVENTS_E5_09_RANDOM_SOURCE_HPP
