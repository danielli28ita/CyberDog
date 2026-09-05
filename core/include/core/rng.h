// 确定性随机数。纯 C++。
//
// SplitMix64：实现短、无状态、同一种子结果确定。不用 <random> 的分布，
// 因为不同标准库实现的分布结果可能不同，存档里的种子换台机器就对不上。

#pragma once

#include <cstdint>

namespace pet {

struct Rng {
    std::uint64_t state = 0;

    explicit Rng(std::uint64_t seed = 0) : state(seed) {}

    std::uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // [0,1)。取高 24 位，低位随机性较差。
    float unit() { return static_cast<float>(next() >> 40) / 16777216.0f; }

    // [lo,hi)
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }

    // 以概率 p 返回 true
    bool chance(float p) { return unit() < p; }
};

}  // namespace pet
