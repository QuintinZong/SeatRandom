/* 随机算法：通过获取系统时间和随机设备的随机数来生成一个基础种子，然后通过对基础种子、工作表名称和索引进行哈希运算来派生出工作表种子。
使用派生出的工作表种子来初始化一个Mersenne Twister 64位随机数生成器（std::mt19937_64），以便在工作表中生成随机数。
*/
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>

namespace seatrandom {

inline uint64_t stableHash(std::string const& text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

inline uint64_t splitMix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

inline uint64_t makeBaseSeed() {
    uint64_t clock = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t random =
        (static_cast<uint64_t>(std::random_device{}()) << 32) ^
        std::random_device{}();
    return splitMix64(clock ^ random);
}

inline uint64_t deriveSheetSeed(uint64_t baseSeed, std::string const& sheetName, size_t index) {
    return splitMix64(baseSeed ^ stableHash(sheetName) ^
                      splitMix64(static_cast<uint64_t>(index)));
}

inline std::mt19937_64 makeSheetRng(uint64_t sheetSeed) {
    return std::mt19937_64(sheetSeed);
}

}
