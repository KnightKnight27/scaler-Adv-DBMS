#pragma once
#include <cstdint>
#include <vector>

namespace minidb {

// A small Bloom filter over int64 keys. Lets an SSTable skip a lookup when the
// key is definitely absent (no false negatives).
class BloomFilter {
 public:
  void Reset(size_t expected) {
    size_t bits = expected * kBitsPerKey;
    if (bits < 64) bits = 64;
    nbits_ = bits;
    words_.assign((nbits_ + 63) / 64, 0);
  }

  void Add(int64_t key) {
    uint64_t h = Mix(static_cast<uint64_t>(key));
    uint64_t delta = (h >> 33) | (h << 31);  // second hash via rotation
    for (int i = 0; i < kHashes; i++) {
      Set(h % nbits_);
      h += delta;
    }
  }

  bool MaybeContains(int64_t key) const {
    if (nbits_ == 0) return true;
    uint64_t h = Mix(static_cast<uint64_t>(key));
    uint64_t delta = (h >> 33) | (h << 31);
    for (int i = 0; i < kHashes; i++) {
      if (!Get(h % nbits_)) return false;
      h += delta;
    }
    return true;
  }

 private:
  static constexpr int kBitsPerKey = 10;  // ~1% false positives at k=7
  static constexpr int kHashes = 7;

  static uint64_t Mix(uint64_t x) {  // splitmix64 finalizer
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
  }
  void Set(uint64_t i) { words_[i >> 6] |= (1ULL << (i & 63)); }
  bool Get(uint64_t i) const { return words_[i >> 6] & (1ULL << (i & 63)); }

  size_t nbits_ = 0;
  std::vector<uint64_t> words_;
};

}  // namespace minidb
