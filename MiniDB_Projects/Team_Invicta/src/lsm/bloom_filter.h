#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace minidb {

// A classic Bloom filter over int64 keys. Each SSTable carries one so a point
// lookup can skip files that definitely do not contain a key (no false
// negatives; a small false-positive rate). Uses double hashing:
//   bit_i = (h1 + i*h2) mod m,  for i in [0, k).
class BloomFilter {
 public:
  BloomFilter() = default;
  BloomFilter(size_t num_bits, int k)
      : m_(num_bits ? num_bits : 1), k_(k > 0 ? k : 1), bits_((m_ + 7) / 8, 0) {}

  // Size a filter for ~n keys at ~1% false positives (m ≈ 10n bits, k = 7).
  static BloomFilter ForN(size_t n) {
    size_t m = (n < 1 ? 1 : n) * 10;
    return BloomFilter(m, 7);
  }

  void Add(int64_t key) {
    uint64_t h1 = Hash(key, 0x9E3779B97F4A7C15ULL);
    uint64_t h2 = Hash(key, 0xC2B2AE3D27D4EB4FULL) | 1ULL;
    for (int i = 0; i < k_; ++i) SetBit((h1 + static_cast<uint64_t>(i) * h2) % m_);
  }

  bool Maybe(int64_t key) const {
    uint64_t h1 = Hash(key, 0x9E3779B97F4A7C15ULL);
    uint64_t h2 = Hash(key, 0xC2B2AE3D27D4EB4FULL) | 1ULL;
    for (int i = 0; i < k_; ++i) {
      if (!GetBit((h1 + static_cast<uint64_t>(i) * h2) % m_)) return false;
    }
    return true;
  }

  // Wire format: m(8) | k(4) | raw bit bytes.
  std::string Serialize() const {
    std::string out;
    uint64_t m = m_;
    int32_t k = k_;
    out.append(reinterpret_cast<const char *>(&m), 8);
    out.append(reinterpret_cast<const char *>(&k), 4);
    out.append(reinterpret_cast<const char *>(bits_.data()), bits_.size());
    return out;
  }

  static BloomFilter Deserialize(const std::string &s) {
    uint64_t m;
    int32_t k;
    std::memcpy(&m, s.data(), 8);
    std::memcpy(&k, s.data() + 8, 4);
    BloomFilter bf(m, k);
    size_t nbytes = (m + 7) / 8;
    std::memcpy(bf.bits_.data(), s.data() + 12, nbytes);
    return bf;
  }

 private:
  void SetBit(uint64_t i) { bits_[i >> 3] |= static_cast<uint8_t>(1u << (i & 7)); }
  bool GetBit(uint64_t i) const { return (bits_[i >> 3] >> (i & 7)) & 1u; }

  static uint64_t Hash(int64_t key, uint64_t seed) {
    uint64_t x = static_cast<uint64_t>(key) ^ seed;  // splitmix64
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
  }

  size_t               m_{1};
  int                  k_{1};
  std::vector<uint8_t> bits_{std::vector<uint8_t>(1, 0)};
};

}  // namespace minidb
