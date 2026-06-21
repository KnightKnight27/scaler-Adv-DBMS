#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace walterdb {

// ---------------------------------------------------------------------------
// BloomFilter -- a per-SSTable probabilistic membership filter.  maybe_contains
// returns false only if the key is DEFINITELY absent, letting a point lookup
// skip reading an SSTable entirely when the key isn't there.  This is the main
// lever that keeps LSM read latency reasonable as un-compacted SSTables pile up.
//
// Implementation: a bit array with k hash positions derived by double hashing
// (h1 + i*h2) from two independent 64-bit hashes of the key -- the standard
// Kirsch-Mitzenmacher trick, so we compute two hashes, not k.
// ---------------------------------------------------------------------------
class BloomFilter {
 public:
  // Sized for `expected_keys` at ~`bits_per_key` bits each (10 -> ~1% FP rate).
  explicit BloomFilter(size_t expected_keys, double bits_per_key = 10.0);

  void add(std::string_view key);
  bool maybe_contains(std::string_view key) const;

  std::string serialize() const;
  static BloomFilter deserialize(std::string_view bytes);

 private:
  BloomFilter() = default;  // for deserialize
  std::vector<uint8_t> bits_;
  uint64_t num_bits_ = 0;
  uint32_t k_ = 0;
};

}  // namespace walterdb
