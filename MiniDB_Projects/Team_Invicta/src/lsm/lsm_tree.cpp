#include "lsm/lsm_tree.h"
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace minidb {

LSMTree::LSMTree(std::string dir, size_t mem_limit, size_t compaction_trigger)
    : dir_(std::move(dir)), mem_limit_(mem_limit), compaction_trigger_(compaction_trigger) {
  fs::create_directories(dir_);
  LoadExisting();
}

void LSMTree::LoadExisting() {
  std::vector<std::pair<uint64_t, std::string>> files;
  for (const auto &ent : fs::directory_iterator(dir_)) {
    std::string stem = ent.path().stem().string();  // e.g. "sst_000003"
    if (stem.rfind("sst_", 0) != 0) continue;
    uint64_t n = std::stoull(stem.substr(4));
    files.emplace_back(n, ent.path().string());
  }
  std::sort(files.begin(), files.end());  // oldest (smallest seq) first
  for (auto &f : files) {
    ssts_.push_back(std::make_unique<SSTable>(f.second));
    seq_ = std::max(seq_, f.first + 1);
  }
}

std::string LSMTree::NextSSTablePath() {
  std::ostringstream os;
  os << dir_ << "/sst_" << std::setw(6) << std::setfill('0') << seq_++ << ".sst";
  return os.str();
}

void LSMTree::Put(int64_t key, const std::string &value) {
  mem_.Put(key, value);
  stats_valid_ = false;
  MaybeFlush();
}

void LSMTree::Delete(int64_t key) {
  mem_.Delete(key);  // tombstone
  stats_valid_ = false;
  MaybeFlush();
}

bool LSMTree::Get(int64_t key, std::string *value) {
  // Newest first: MemTable, then SSTables newest -> oldest. First hit wins.
  LSMEntry entry;
  if (mem_.Get(key, &entry)) {
    if (entry.deleted) return false;
    *value = entry.value;
    return true;
  }
  for (auto it = ssts_.rbegin(); it != ssts_.rend(); ++it) {
    if ((*it)->Get(key, &entry)) {
      if (entry.deleted) return false;
      *value = entry.value;
      return true;
    }
  }
  return false;
}

void LSMTree::MaybeFlush() {
  if (mem_.bytes() >= mem_limit_) FlushMemTable();
}

void LSMTree::FlushMemTable() {
  if (mem_.empty()) return;
  std::string path = NextSSTablePath();
  SSTable::Build(path, mem_.entries());
  mem_.Clear();
  ssts_.push_back(std::make_unique<SSTable>(path));
  if (ssts_.size() >= compaction_trigger_) Compact();
}

void LSMTree::Flush() { FlushMemTable(); }

void LSMTree::Compact() {
  // Merge every SSTable into one, newest version of each key winning, dropping
  // tombstones (size-tiered, full compaction). The MemTable is empty here.
  std::map<int64_t, LSMEntry> merged;
  for (auto &sst : ssts_) {  // oldest -> newest; later writes overwrite
    const auto &idx = sst->index();
    for (size_t i = 0; i < idx.size(); ++i) merged[idx[i].key] = sst->ReadAt(i);
  }
  std::vector<std::string> old_paths;
  for (auto &sst : ssts_) old_paths.push_back(sst->path());

  std::map<int64_t, LSMEntry> live;
  for (auto &kv : merged) {
    if (!kv.second.deleted) live.emplace(kv.first, kv.second);  // drop tombstones
  }

  std::string new_path = NextSSTablePath();
  ssts_.clear();  // close file handles before removing
  SSTable::Build(new_path, live);
  for (const std::string &p : old_paths) {
    std::error_code ec;
    fs::remove(p, ec);
  }
  ssts_.push_back(std::make_unique<SSTable>(new_path));
}

std::vector<std::pair<int64_t, std::string>> LSMTree::ScanAll() {
  // Merge all runs + MemTable, newest wins, skip tombstones.
  std::map<int64_t, LSMEntry> view;
  for (auto &sst : ssts_) {
    const auto &idx = sst->index();
    for (size_t i = 0; i < idx.size(); ++i) view[idx[i].key] = sst->ReadAt(i);
  }
  for (const auto &kv : mem_.entries()) view[kv.first] = kv.second;

  std::vector<std::pair<int64_t, std::string>> out;
  for (auto &kv : view) {
    if (!kv.second.deleted) out.emplace_back(kv.first, kv.second.value);
  }
  return out;
}

std::vector<std::pair<int64_t, std::string>> LSMTree::Range(int64_t low, int64_t high) {
  std::vector<std::pair<int64_t, std::string>> out;
  for (auto &kv : ScanAll()) {
    if (kv.first >= low && kv.first <= high) out.push_back(kv);
  }
  return out;
}

void LSMTree::RefreshStats() {
  auto all = ScanAll();
  live_count_ = all.size();
  has_keys_ = !all.empty();
  if (has_keys_) {
    min_key_ = all.front().first;
    max_key_ = all.back().first;
  }
  stats_valid_ = true;
}

size_t LSMTree::LiveCount() {
  if (!stats_valid_) RefreshStats();
  return live_count_;
}

bool LSMTree::KeyRange(int64_t *min_key, int64_t *max_key) {
  if (!stats_valid_) RefreshStats();
  if (!has_keys_) return false;
  *min_key = min_key_;
  *max_key = max_key_;
  return true;
}

}  // namespace minidb
