#include "lsm/bloom_filter.h"

#include <cmath>

#include "common/serialize.h"

namespace axiomdb {

namespace {
// Two independent 64-bit FNV-1a hashes (different offset bases) of the key.
void hash_pair(std::string_view key, uint64_t& h1, uint64_t& h2) {
  h1 = 1469598103934665603ULL;       // FNV offset basis
  h2 = 1099511628211ULL ^ 0xABCDEF;  // a different seed
  for (unsigned char c : key) {
    h1 = (h1 ^ c) * 1099511628211ULL;
    h2 = (h2 ^ c) * 1099511628211ULL;
    h2 = (h2 << 13) | (h2 >> 51);  // extra mixing so h2 differs from h1
  }
}
}  // namespace

BloomFilter::BloomFilter(size_t expected_keys, double bits_per_key) {
  size_t n = expected_keys ? expected_keys : 1;
  num_bits_ = static_cast<uint64_t>(n * bits_per_key);
  if (num_bits_ < 64) num_bits_ = 64;
  // Optimal k = (m/n) ln 2, clamped to a sane range.
  k_ = static_cast<uint32_t>(bits_per_key * 0.6931471805599453);
  if (k_ < 1) k_ = 1;
  if (k_ > 12) k_ = 12;
  bits_.assign((num_bits_ + 7) / 8, 0);
}

void BloomFilter::add(std::string_view key) {
  uint64_t h1, h2;
  hash_pair(key, h1, h2);
  for (uint32_t i = 0; i < k_; ++i) {
    uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) % num_bits_;
    bits_[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
  }
}

bool BloomFilter::maybe_contains(std::string_view key) const {
  uint64_t h1, h2;
  hash_pair(key, h1, h2);
  for (uint32_t i = 0; i < k_; ++i) {
    uint64_t bit = (h1 + static_cast<uint64_t>(i) * h2) % num_bits_;
    if (!(bits_[bit / 8] & (1u << (bit % 8)))) return false;  // definitely absent
  }
  return true;  // probably present
}

std::string BloomFilter::serialize() const {
  ByteWriter w;
  w.put_u64(num_bits_);
  w.put_u32(k_);
  w.put_string(std::string_view(reinterpret_cast<const char*>(bits_.data()), bits_.size()));
  return w.take();
}

BloomFilter BloomFilter::deserialize(std::string_view bytes) {
  ByteReader r(bytes);
  BloomFilter bf;
  bf.num_bits_ = r.get_u64();
  bf.k_ = r.get_u32();
  std::string_view raw = r.get_string();
  bf.bits_.assign(raw.begin(), raw.end());
  return bf;
}

}  // namespace axiomdb
