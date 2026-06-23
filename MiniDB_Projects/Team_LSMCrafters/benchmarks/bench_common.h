#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include "common/types.h"

namespace minidb {

// Wall-clock timer.
struct Timer {
  std::chrono::steady_clock::time_point start_;
  void   start() { start_ = std::chrono::steady_clock::now(); }
  double seconds() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
  }
};

// N unique keys (0..N-1) in a deterministic random order.
inline std::vector<Key> gen_keys(int n, uint64_t seed) {
  std::vector<Key> keys(n);
  for (int i = 0; i < n; ++i) keys[i] = i;
  std::mt19937_64 rng(seed);
  std::shuffle(keys.begin(), keys.end(), rng);
  return keys;
}

// A fixed-size payload; the key is encoded at the front for sanity checks.
inline Bytes gen_value(Key key, std::size_t row_bytes) {
  Bytes v(row_bytes, 'x');
  std::string s = std::to_string(key);
  for (std::size_t i = 0; i < s.size() && i < row_bytes; ++i) v[i] = s[i];
  return v;
}

// Total bytes on disk for a file or, recursively, a directory.
inline uint64_t path_size(const std::string& path) {
  namespace fs = std::filesystem;
  if (!fs::exists(path)) return 0;
  if (!fs::is_directory(path)) return fs::file_size(path);
  uint64_t total = 0;
  for (const auto& entry : fs::recursive_directory_iterator(path))
    if (entry.is_regular_file()) total += entry.file_size();
  return total;
}

// Appends one measurement per line to a CSV.
struct CsvWriter {
  std::ofstream out;
  explicit CsvWriter(const std::string& path) : out(path) { out << "engine,phase,metric,value\n"; }
  void row(const std::string& engine, const std::string& phase, const std::string& metric, double value) {
    out << engine << ',' << phase << ',' << metric << ',' << value << '\n';
  }
};

}  // namespace minidb
