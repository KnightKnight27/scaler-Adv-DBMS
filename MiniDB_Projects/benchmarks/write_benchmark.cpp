#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "minidb/common/types.h"
#include "minidb/common/trace.h"
#include "minidb/db/database.h"
#include "minidb/lsm/lsm_tree.h"

namespace {

struct Timing {
  long long micros{};
};

struct StorageStats {
  std::uintmax_t logical_bytes{};
  std::uintmax_t physical_bytes{};
  double amplification{};
};

std::filesystem::path FreshTemp(const std::string &name) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
  std::filesystem::create_directories(path);
  return path;
}

template <typename Fn>
Timing Time(Fn &&fn) {
  const auto start = std::chrono::steady_clock::now();
  fn();
  const auto end = std::chrono::steady_clock::now();
  return {std::chrono::duration_cast<std::chrono::microseconds>(end - start)
              .count()};
}

std::uintmax_t FileSizeIfExists(const std::filesystem::path &path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return 0;
  return std::filesystem::file_size(path, ec);
}

std::uintmax_t RecursiveSize(const std::filesystem::path &directory,
                             bool sstables_only) {
  std::uintmax_t bytes = 0;
  std::error_code ec;
  if (!std::filesystem::exists(directory, ec)) return 0;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    if (sstables_only && entry.path().extension() != ".dat") continue;
    bytes += FileSizeIfExists(entry.path());
  }
  return bytes;
}

std::uintmax_t LogicalBytes(const std::vector<std::string> &values) {
  std::uintmax_t bytes = 0;
  for (const auto &value : values) {
    bytes += sizeof(std::int32_t) + value.size();
  }
  return bytes;
}

std::vector<int> RandomKeys(int record_count, int lookup_count) {
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, record_count - 1);
  std::vector<int> keys;
  keys.reserve(static_cast<std::size_t>(lookup_count));
  for (int i = 0; i < lookup_count; ++i) keys.push_back(dist(rng));
  return keys;
}

double PerOpMicros(Timing timing, int operations) {
  return static_cast<double>(timing.micros) / static_cast<double>(operations);
}

double OpsPerSecond(Timing timing, int operations) {
  if (timing.micros == 0) return static_cast<double>(operations);
  return static_cast<double>(operations) * 1'000'000.0 /
         static_cast<double>(timing.micros);
}

StorageStats Amplification(std::uintmax_t logical, std::uintmax_t physical) {
  return {logical, physical,
          logical == 0 ? 0.0
                       : static_cast<double>(physical) /
                             static_cast<double>(logical)};
}

void PrintStorage(const std::string &name, const StorageStats &stats) {
  std::cout << std::left << std::setw(12) << name << " logical="
            << stats.logical_bytes << " bytes, physical="
            << stats.physical_bytes << " bytes, amplification=" << std::fixed
            << std::setprecision(2) << stats.amplification << "x\n";
}

}  // namespace

int main() {
  minidb::Trace::SetEnabled(false);
  constexpr int kRecords = 5000;
  constexpr int kLookups = 10000;

  std::vector<std::string> values;
  values.reserve(kRecords);
  for (int i = 0; i < kRecords; ++i) {
    values.push_back("value_" + std::to_string(i));
  }
  const auto lookup_keys = RandomKeys(kRecords, kLookups);
  const auto logical_bytes = LogicalBytes(values);

  const auto btree_dir = FreshTemp("minidb_btree_bench");
  const auto lsm_dir = FreshTemp("minidb_lsm_bench");

  std::size_t btree_rows = 0;
  minidb::Database db(btree_dir);
  const auto btree_write = Time([&] {
    for (int i = 0; i < kRecords; ++i) {
      auto result = db.Execute("INSERT users " + std::to_string(i) + " " +
                               values[static_cast<std::size_t>(i)]);
      if (!result.ok) throw std::runtime_error(result.message);
    }
    btree_rows = db.RowCount("users");
    db.Flush();
  });

  minidb::LsmTree lsm(lsm_dir, 512);
  std::size_t sstable_count = 0;
  const auto lsm_write = Time([&] {
    for (int i = 0; i < kRecords; ++i) {
      lsm.Put(i, values[static_cast<std::size_t>(i)]);
    }
    lsm.Flush();
    lsm.Compact();
    sstable_count = lsm.SSTableCount();
  });

  int btree_hits = 0;
  const auto btree_read = Time([&] {
    for (int key : lookup_keys) {
      auto record = db.Get("users", key);
      if (record) ++btree_hits;
    }
  });

  int lsm_hits = 0;
  const auto lsm_read = Time([&] {
    for (int key : lookup_keys) {
      auto value = lsm.Get(key);
      if (value) ++lsm_hits;
    }
  });

  // Heap+BTree currently persists heap pages and WAL/catalog files; the B+ tree
  // is intentionally in-memory and rebuilt from the heap, so persisted index
  // file bytes are zero by design.
  const auto btree_physical = RecursiveSize(btree_dir, false);
  const auto lsm_physical = RecursiveSize(lsm_dir, true);

  std::cout << "MiniDB Track C benchmark\n";
  std::cout << "records=" << kRecords << ", lookups=" << kLookups << "\n\n";

  std::cout << "Write throughput\n";
  std::cout << std::left << std::setw(12) << "Heap+BTree" << btree_write.micros / 1000.0
            << " ms, " << std::fixed << std::setprecision(2)
            << OpsPerSecond(btree_write, kRecords) << " writes/sec, rows="
            << btree_rows << "\n";
  std::cout << std::left << std::setw(12) << "LSM Tree" << lsm_write.micros / 1000.0
            << " ms, " << std::fixed << std::setprecision(2)
            << OpsPerSecond(lsm_write, kRecords) << " writes/sec, sstables="
            << sstable_count << "\n\n";

  std::cout << "Read latency\n";
  std::cout << std::left << std::setw(12) << "Heap+BTree"
            << "avg=" << std::fixed << std::setprecision(3)
            << PerOpMicros(btree_read, kLookups) << " us, "
            << OpsPerSecond(btree_read, kLookups) << " queries/sec, hits="
            << btree_hits << "\n";
  std::cout << std::left << std::setw(12) << "LSM Tree"
            << "avg=" << std::fixed << std::setprecision(3)
            << PerOpMicros(lsm_read, kLookups) << " us, "
            << OpsPerSecond(lsm_read, kLookups) << " queries/sec, hits="
            << lsm_hits << "\n\n";

  std::cout << "Storage amplification\n";
  PrintStorage("Heap+BTree", Amplification(logical_bytes, btree_physical));
  PrintStorage("LSM Tree", Amplification(logical_bytes, lsm_physical));
  return 0;
}
