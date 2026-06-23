#include "minidb/lsm/lsm_tree.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {

void SSTable::Write(
    const std::filesystem::path &path,
    const std::map<int, std::optional<std::string>> &entries) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("cannot create SSTable");
  const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
  out.write(reinterpret_cast<const char *>(&count), sizeof(count));
  for (const auto &[key, value] : entries) {
    const std::int32_t stored_key = key;
    const std::int32_t length =
        value ? static_cast<std::int32_t>(value->size()) : -1;
    out.write(reinterpret_cast<const char *>(&stored_key), sizeof(stored_key));
    out.write(reinterpret_cast<const char *>(&length), sizeof(length));
    if (value) out.write(value->data(), length);
  }
  out.flush();
  if (!out) throw std::runtime_error("SSTable write failed");
}

std::map<int, std::optional<std::string>> SSTable::Read(
    const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read SSTable");
  std::uint32_t count{};
  in.read(reinterpret_cast<char *>(&count), sizeof(count));
  std::map<int, std::optional<std::string>> entries;
  for (std::uint32_t i = 0; i < count; ++i) {
    std::int32_t key{};
    std::int32_t length{};
    in.read(reinterpret_cast<char *>(&key), sizeof(key));
    in.read(reinterpret_cast<char *>(&length), sizeof(length));
    if (length < 0) {
      entries[key] = std::nullopt;
    } else {
      std::string value(static_cast<std::size_t>(length), '\0');
      in.read(value.data(), length);
      entries[key] = std::move(value);
    }
  }
  if (!in) throw std::runtime_error("corrupt SSTable");
  return entries;
}

LsmTree::LsmTree(std::filesystem::path directory, std::size_t flush_threshold)
    : directory_(std::move(directory)), flush_threshold_(flush_threshold) {
  if (flush_threshold_ == 0) {
    throw std::invalid_argument("flush threshold must be positive");
  }
  std::filesystem::create_directories(directory_);
  LoadManifest();
}

void LsmTree::Put(int key, std::string value) {
  memtable_.Put(key, std::move(value));
  Trace::Log("LSM", "memtable put key " + std::to_string(key));
  if (memtable_.Size() >= flush_threshold_) Flush();
}

void LsmTree::Delete(int key) {
  memtable_.Delete(key);
  Trace::Log("LSM", "memtable tombstone key " + std::to_string(key));
  if (memtable_.Size() >= flush_threshold_) Flush();
}

std::optional<std::string> LsmTree::Get(int key) const {
  if (auto value = memtable_.Get(key)) return *value;
  for (auto it = tables_.rbegin(); it != tables_.rend(); ++it) {
    auto entries = SSTable::Read(*it);
    auto found = entries.find(key);
    if (found != entries.end()) return found->second;
  }
  return std::nullopt;
}

void LsmTree::Flush() {
  if (memtable_.Size() == 0) return;
  const auto filename = "sst_" + std::to_string(next_generation_++) + ".dat";
  const auto path = directory_ / filename;
  SSTable::Write(path, memtable_.Entries());
  tables_.push_back(path);
  memtable_.Clear();
  SaveManifest();
  Trace::Log("LSM", "flushed MemTable to " + filename);
}

void LsmTree::Compact() {
  Flush();
  if (tables_.size() <= 1) return;
  std::map<int, std::optional<std::string>> merged;
  for (const auto &path : tables_) {
    for (auto &[key, value] : SSTable::Read(path)) {
      merged[key] = std::move(value);
    }
  }
  for (auto it = merged.begin(); it != merged.end();) {
    if (!it->second) {
      it = merged.erase(it);
    } else {
      ++it;
    }
  }
  const auto new_path =
      directory_ / ("sst_" + std::to_string(next_generation_++) + ".dat");
  SSTable::Write(new_path, merged);
  const auto old_tables = tables_;
  tables_ = {new_path};
  SaveManifest();
  for (const auto &path : old_tables) std::filesystem::remove(path);
  Trace::Log("LSM", "compacted SSTables into " + new_path.filename().string());
}

void LsmTree::LoadManifest() {
  std::ifstream manifest(directory_ / "MANIFEST");
  std::string filename;
  while (manifest >> filename) {
    const auto path = directory_ / filename;
    if (std::filesystem::exists(path)) tables_.push_back(path);
    const auto first = filename.find('_');
    const auto last = filename.find('.');
    if (first != std::string::npos && last != std::string::npos) {
      next_generation_ =
          std::max(next_generation_,
                   static_cast<std::uint64_t>(
                       std::stoull(filename.substr(first + 1,
                                                   last - first - 1)) +
                       1));
    }
  }
}

void LsmTree::SaveManifest() const {
  const auto temporary = directory_ / "MANIFEST.tmp";
  std::ofstream manifest(temporary, std::ios::trunc);
  for (const auto &path : tables_) manifest << path.filename().string() << '\n';
  manifest.flush();
  if (!manifest) throw std::runtime_error("manifest write failed");
  manifest.close();
  const auto final = directory_ / "MANIFEST";
  std::error_code error;
  std::filesystem::remove(final, error);
  std::filesystem::rename(temporary, final);
}

}  // namespace minidb
