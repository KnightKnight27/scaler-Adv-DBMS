#include "lsm/lsm_tree.h"
#include "common/types.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>

namespace minidb {

LSMTree::LSMTree(const std::string& prefix, size_t memtable_limit,
                 size_t compaction_trigger)
    : prefix_(prefix), memtable_limit_(memtable_limit),
      compaction_trigger_(compaction_trigger) {
  LoadManifest();
}

static std::string SstPath(const std::string& prefix, int64_t seq) {
  return prefix + "_" + std::to_string(seq) + ".sst";
}

void LSMTree::Put(int64_t key, const std::string& value) {
  mem_.Put(key, value);
  MaybeFlush();
}

void LSMTree::Delete(int64_t key) {
  mem_.Delete(key);
  MaybeFlush();
}

void LSMTree::MaybeFlush() {
  if (mem_.Count() >= memtable_limit_) Flush();
}

bool LSMTree::Get(int64_t key, std::string* out) {
  LsmValue v;
  // Newest-first: memtable shadows SSTables; newer SSTables shadow older.
  if (mem_.Get(key, &v)) {
    if (v.tombstone) return false;
    *out = v.data;
    return true;
  }
  for (auto it = ssts_.rbegin(); it != ssts_.rend(); ++it) {
    if ((*it)->Get(key, &v)) {
      if (v.tombstone) return false;
      *out = v.data;
      return true;
    }
  }
  return false;
}

std::vector<std::pair<int64_t, std::string>> LSMTree::Scan(int64_t low, int64_t high) {
  // Merge all sources oldest->newest so newer versions overwrite older.
  std::map<int64_t, LsmValue> merged;
  for (auto& sst : ssts_) {
    for (auto& [k, v] : sst->ReadAll())
      if (k >= low && k <= high) merged[k] = v;
  }
  for (auto& [k, v] : mem_.Entries())
    if (k >= low && k <= high) merged[k] = v;

  std::vector<std::pair<int64_t, std::string>> out;
  for (auto& [k, v] : merged)
    if (!v.tombstone) out.emplace_back(k, v.data);
  return out;
}

void LSMTree::Flush() {
  if (mem_.Empty()) return;
  int64_t seq = next_seq_++;
  std::string path = SstPath(prefix_, seq);
  std::vector<std::pair<int64_t, LsmValue>> entries(mem_.Entries().begin(),
                                                    mem_.Entries().end());
  live_bytes_ += mem_.Bytes();
  SSTable::Write(path, entries);
  mem_.Clear();
  ssts_.push_back(std::make_unique<SSTable>(path));
  flushes_++;
  WriteManifest();
  if (ssts_.size() >= compaction_trigger_) Compact();
}

void LSMTree::Compact() {
  if (ssts_.size() < 2) return;
  // K-way merge of every run; oldest first so newer overwrites older.
  std::map<int64_t, LsmValue> merged;
  for (auto& sst : ssts_)
    for (auto& [k, v] : sst->ReadAll()) merged[k] = v;

  // Major compaction collapses to one run: tombstones can be dropped because
  // there are no older runs left for them to shadow.
  std::vector<std::pair<int64_t, LsmValue>> out;
  for (auto& [k, v] : merged)
    if (!v.tombstone) out.push_back({k, v});

  int64_t seq = next_seq_++;
  std::string path = SstPath(prefix_, seq);
  SSTable::Write(path, out);

  // Remove the old runs from disk and from memory.
  for (auto& sst : ssts_) std::remove(sst->Path().c_str());
  ssts_.clear();
  ssts_.push_back(std::make_unique<SSTable>(path));
  compactions_++;
  WriteManifest();
}

size_t LSMTree::DiskBytes() const {
  size_t total = 0;
  for (auto& sst : ssts_) total += sst->FileBytes();
  return total;
}

void LSMTree::WriteManifest() {
  std::ofstream out(prefix_ + ".manifest", std::ios::trunc);
  out << next_seq_ << "\n" << ssts_.size() << "\n";
  for (auto& sst : ssts_) out << sst->Path() << "\n";
}

void LSMTree::LoadManifest() {
  std::ifstream in(prefix_ + ".manifest");
  if (!in.is_open()) return;
  size_t n = 0;
  in >> next_seq_ >> n;
  in.ignore();
  for (size_t i = 0; i < n; i++) {
    std::string path;
    std::getline(in, path);
    if (!path.empty()) ssts_.push_back(std::make_unique<SSTable>(path));
  }
}

}  // namespace minidb
