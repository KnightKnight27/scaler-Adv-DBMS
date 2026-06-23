#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace minidb {

// A Bloom filter over int64 keys. Used per-SSTable so a point lookup can skip a
// file entirely when the key is definitely absent (cuts read amplification).
// "definitely not present" is always correct; "maybe present" allows a small
// false-positive rate (~1% at the default 10 bits/key, per the LSM lectures).
class BloomFilter {
public:
    explicit BloomFilter(std::size_t expected_keys, int bits_per_key = 10)
        : bits_(std::max<std::size_t>(64, expected_keys * static_cast<std::size_t>(bits_per_key))),
          k_(std::max(1, static_cast<int>(bits_per_key * 0.69))),  // k = ln2 * (m/n)
          data_((bits_ + 7) / 8, 0) {}

    void add(std::int64_t key) {
        std::uint64_t h1 = hash1(key), h2 = hash2(key);
        for (int i = 0; i < k_; ++i) {
            std::uint64_t b = (h1 + static_cast<std::uint64_t>(i) * h2) % bits_;
            data_[b / 8] |= static_cast<std::uint8_t>(1u << (b % 8));
        }
    }

    bool maybe_contains(std::int64_t key) const {
        std::uint64_t h1 = hash1(key), h2 = hash2(key);
        for (int i = 0; i < k_; ++i) {
            std::uint64_t b = (h1 + static_cast<std::uint64_t>(i) * h2) % bits_;
            if (!(data_[b / 8] & (1u << (b % 8)))) return false;  // definitely absent
        }
        return true;  // maybe present
    }

private:
    // Two independent 64-bit mixes (double hashing gives k hashes cheaply).
    static std::uint64_t hash1(std::int64_t key) {
        std::uint64_t x = static_cast<std::uint64_t>(key);
        x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
        return x;
    }
    static std::uint64_t hash2(std::int64_t key) {
        std::uint64_t x = static_cast<std::uint64_t>(key) + 0x9e3779b97f4a7c15ULL;
        x ^= x >> 29; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 32;
        return x | 1;
    }

    std::size_t            bits_;
    int                    k_;
    std::vector<std::uint8_t> data_;
};

} // namespace minidb
