#include "lsm/lsm_tree.h"

namespace minidb {

LSMTree::LSMTree(string dir, size_t memtable_limit, int compaction_trigger)
    : dir_(move(dir)), memtable_limit_(memtable_limit),
      compaction_trigger_(compaction_trigger) {}

// --- Writes just update the in-memory memtable (fast, no disk I/O). ----------
void LSMTree::put(int64_t key, const string &value) {
  memtable_[key] = {key, value, false};
  maybeFlush();
}

void LSMTree::deleteKey(int64_t key) {
  memtable_[key] = {key, "", true};   // write a tombstone (logical delete)
  maybeFlush();
}

// --- Reads check memtable first, then SSTables newest -> oldest. -------------
// The first place a key is found wins, because newer data shadows older data.
bool LSMTree::get(int64_t key, string *out) const {
  auto it = memtable_.find(key);
  if (it != memtable_.end()) {
    if (it->second.tombstone) return false;   // deleted
    *out = it->second.value;
    return true;
  }
  // search SSTables from newest to oldest.
  for (auto sit = sstables_.rbegin(); sit != sstables_.rend(); ++sit) {
    const auto &e = sit->entries;
    // Binary search this sorted SSTable.
    int lo = 0, hi = static_cast<int>(e.size()) - 1, found = -1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      if (e[mid].key == key) { found = mid; break; }
      if (e[mid].key < key) lo = mid + 1; else hi = mid - 1;
    }
    if (found >= 0) {
      if (e[found].tombstone) return false;    // deleted in a newer SSTable
      *out = e[found].value;
      return true;
    }
  }
  return false;
}

void LSMTree::maybeFlush() {
  if (memtable_.size() >= memtable_limit_) flush();
}

// --- flush: write the sorted memtable out as one immutable SSTable. ----------
void LSMTree::flush() {
  if (memtable_.empty()) return;
  vector<LSMEntry> entries;
  entries.reserve(memtable_.size());
  for (auto &[k, e] : memtable_) entries.push_back(e);  // map iteration is sorted

  int seq = next_seq_++;
  writeSSTableFile(seq, entries);
  memtable_.clear();
  stats_.flushes++;

  // Too many SSTables?  Merge them so reads do not have to scan a long list.
  if (static_cast<int>(sstables_.size()) >= compaction_trigger_) compact();
}

void LSMTree::writeSSTableFile(int seq, const vector<LSMEntry> &entries) {
  string path = dir_ + "/sst_" + to_string(seq) + ".dat";
  ofstream out(path, ios::binary | ios::trunc);
  int32_t n = static_cast<int32_t>(entries.size());
  out.write((char *)&n, 4);
  for (auto &e : entries) {
    out.write((char *)&e.key, 8);
    uint8_t tomb = e.tombstone ? 1 : 0;
    out.write((char *)&tomb, 1);
    int32_t vlen = static_cast<int32_t>(e.value.size());
    out.write((char *)&vlen, 4);
    out.write(e.value.data(), vlen);
  }
  out.flush();
  size_t bytes = 4 + entries.size() * 13;   // approx fixed part
  for (auto &e : entries) bytes += e.value.size();

  sstables_.push_back({seq, entries, path, bytes});
}

// --- Compaction: k-way merge of all SSTables into a single new one. ----------
// We process from OLDEST to NEWEST into a sorted map, so newer values overwrite
// older ones for the same key.  Tombstones are then physically dropped, because
// after a full compaction there is nothing older left for them to shadow.
void LSMTree::compact() {
  if (sstables_.size() <= 1) return;

  map<int64_t, LSMEntry> merged;   // sorted; last writer (newest) wins
  for (auto &sst : sstables_)           // sstables_ is in ascending seq (oldest first)
    for (auto &e : sst.entries) merged[e.key] = e;

  vector<LSMEntry> result;
  for (auto &[k, e] : merged)
    if (!e.tombstone) result.push_back(e);   // drop deleted keys

  // remove old SSTable files from disk.
  for (auto &sst : sstables_) remove(sst.path.c_str());
  sstables_.clear();

  int seq = next_seq_++;
  writeSSTableFile(seq, result);
  stats_.compactions++;
}

LSMStats LSMTree::stats() const {
  LSMStats s = stats_;
  s.num_sstables = static_cast<int>(sstables_.size());
  s.memtable_entries = memtable_.size();
  s.sstable_entries = 0;
  s.disk_bytes = 0;
  for (auto &sst : sstables_) { s.sstable_entries += sst.entries.size(); s.disk_bytes += sst.bytes; }
  return s;
}

}  // namespace minidb
