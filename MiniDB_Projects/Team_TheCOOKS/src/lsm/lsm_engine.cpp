#include "lsm/lsm_engine.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>

#include "common/serialize.h"

namespace fs = std::filesystem;

namespace walterdb {

namespace {
// A simple vector-backed cursor over an already-merged, tombstone-filtered range.
class VectorIterator : public KVIterator {
 public:
  explicit VectorIterator(std::vector<std::pair<std::string, std::string>> items)
      : items_(std::move(items)) {}
  bool valid() const override { return pos_ < items_.size(); }
  void next() override { ++pos_; }
  std::string_view key() const override { return items_[pos_].first; }
  std::string_view value() const override { return items_[pos_].second; }

 private:
  std::vector<std::pair<std::string, std::string>> items_;
  size_t pos_ = 0;
};

bool in_range(std::string_view k, std::string_view lo, std::string_view hi) {
  if (!lo.empty() && k < lo) return false;
  if (!hi.empty() && k >= hi) return false;
  return true;
}
}  // namespace

LSMEngine::LSMEngine(std::string base_path, size_t threshold)
    : base_path_(std::move(base_path)), threshold_(threshold) {
  fs::path p(base_path_);
  dir_ = p.has_parent_path() ? p.parent_path().string() : ".";
  basename_ = p.filename().string();

  load_sstables();

  std::string wal = base_path_ + ".lsm-wal";
  wal_fd_ = ::open(wal.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
  replay_wal();
}

LSMEngine::~LSMEngine() {
  if (!memtable_.empty()) flush_memtable();
  if (wal_fd_ >= 0) ::close(wal_fd_);
}

std::string LSMEngine::sstable_path(uint64_t seq) const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "-%06llu.sst", static_cast<unsigned long long>(seq));
  return base_path_ + buf;
}

void LSMEngine::load_sstables() {
  std::string prefix = basename_ + "-";
  std::vector<std::pair<uint64_t, std::string>> found;
  if (fs::exists(dir_)) {
    for (const auto& e : fs::directory_iterator(dir_)) {
      std::string fn = e.path().filename().string();
      if (fn.rfind(prefix, 0) != 0 || e.path().extension() != ".sst") continue;
      uint64_t seq = std::strtoull(fn.substr(prefix.size()).c_str(), nullptr, 10);
      found.push_back({seq, e.path().string()});
    }
  }
  std::sort(found.begin(), found.end());  // ascending seq = oldest first
  for (auto& [seq, path] : found) {
    sstables_.insert(sstables_.begin(), std::make_unique<SSTable>(path));  // newest at front
    next_seq_ = std::max(next_seq_, seq + 1);
  }
}

void LSMEngine::replay_wal() {
  off_t size = ::lseek(wal_fd_, 0, SEEK_END);
  if (size <= 0) return;
  std::string buf(static_cast<size_t>(size), '\0');
  if (::pread(wal_fd_, buf.data(), buf.size(), 0) != static_cast<ssize_t>(buf.size())) return;

  size_t pos = 0;
  while (pos + 4 <= buf.size()) {
    uint32_t len = load_u32(buf.data() + pos);
    if (pos + 4 + len > buf.size()) break;  // torn tail
    ByteReader r(std::string_view(buf.data() + pos + 4, len));
    uint8_t op = r.get_u8();
    std::string_view key = r.get_string();
    if (op == 0) {
      memtable_.put(key, r.get_string());
    } else {
      memtable_.remove(key);
    }
    pos += 4 + len;
  }
}

void LSMEngine::wal_append(uint8_t op, std::string_view key, std::string_view value) {
  ByteWriter body;
  body.put_u8(op);
  body.put_string(key);
  if (op == 0) body.put_string(value);
  ByteWriter framed;
  framed.put_u32(static_cast<uint32_t>(body.size()));
  framed.put_bytes(body.str());
  const std::string& bytes = framed.str();
  [[maybe_unused]] ssize_t n = ::write(wal_fd_, bytes.data(), bytes.size());
  bytes_written_ += bytes.size();
}

Status LSMEngine::put(std::string_view key, std::string_view value) {
  wal_append(0, key, value);
  memtable_.put(key, value);
  if (memtable_.size_bytes() >= threshold_) flush_memtable();
  return {};
}

Status LSMEngine::remove(std::string_view key) {
  wal_append(1, key, {});
  memtable_.remove(key);
  if (memtable_.size_bytes() >= threshold_) flush_memtable();
  return {};
}

std::optional<std::string> LSMEngine::get(std::string_view key) {
  std::string val;
  switch (memtable_.get(key, &val)) {
    case MemTable::Lookup::Found: return val;
    case MemTable::Lookup::Tombstone: return std::nullopt;
    case MemTable::Lookup::Absent: break;
  }
  for (const auto& sst : sstables_) {  // newest -> oldest
    switch (sst->get(key, &val)) {
      case SSTable::Lookup::Found: return val;
      case SSTable::Lookup::Tombstone: return std::nullopt;
      case SSTable::Lookup::Absent: break;
    }
  }
  return std::nullopt;
}

std::unique_ptr<KVIterator> LSMEngine::scan(std::string_view lo, std::string_view hi) {
  // Merge all sources oldest -> newest so newer values overwrite older ones,
  // then drop tombstones.  std::map keeps the result sorted by key.  Each
  // SSTable only reads the blocks covering [lo, hi) via its sparse index.
  std::map<std::string, std::pair<std::string, bool>, std::less<>> merged;
  std::vector<SSTEntry> range_buf;
  for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {  // oldest first
    range_buf.clear();
    (*it)->collect_range(lo, hi, range_buf);
    for (auto& e : range_buf) merged[e.key] = {std::move(e.value), e.tombstone};
  }
  for (const auto& [k, e] : memtable_.entries()) {  // memtable is newest
    if (in_range(k, lo, hi)) merged[k] = {e.value, e.tombstone};
  }

  std::vector<std::pair<std::string, std::string>> out;
  for (auto& [k, e] : merged)
    if (!e.second) out.emplace_back(k, e.first);
  return std::make_unique<VectorIterator>(std::move(out));
}

void LSMEngine::flush() {
  if (!memtable_.empty()) flush_memtable();
}

void LSMEngine::flush_memtable() {
  std::vector<SSTEntry> entries;
  entries.reserve(memtable_.count());
  for (const auto& [k, e] : memtable_.entries())
    entries.push_back({k, e.value, e.tombstone});

  std::string path = sstable_path(next_seq_++);
  SSTable::write(path, entries);
  sstables_.insert(sstables_.begin(), std::make_unique<SSTable>(path));
  bytes_written_ += sstables_.front()->file_size();

  memtable_.clear();
  ::ftruncate(wal_fd_, 0);  // memtable now durable in the SSTable
  ::lseek(wal_fd_, 0, SEEK_SET);

  if (sstables_.size() >= compaction_threshold_) compact();
}

void LSMEngine::compact() {
  if (sstables_.size() < 2) return;

  // k-way merge: at each step take the smallest key across all SSTables; among
  // ties the newest (lowest index) wins; tombstones are dropped (this is a full
  // compaction, so there is no older data they need to shadow).
  std::vector<SSTable::Iterator> its;
  for (auto& sst : sstables_) its.push_back(sst->iterator());

  std::vector<SSTEntry> merged;
  for (;;) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(its.size()); ++i) {
      if (!its[i].valid()) continue;
      if (best == -1 || its[i].key() < its[best].key()) best = i;
    }
    if (best == -1) break;
    std::string_view key = its[best].key();

    // best is the newest holder of this key (lowest index); record it, then
    // advance every iterator currently sitting on this key.
    bool tomb = its[best].tombstone();
    std::string value(its[best].value());
    for (auto& it : its)
      if (it.valid() && it.key() == key) it.next();
    if (!tomb) merged.push_back({std::string(key), std::move(value), false});
  }

  std::string path = sstable_path(next_seq_++);
  SSTable::write(path, merged);

  // Replace all old SSTables with the single compacted one, deleting their files.
  std::vector<std::string> old_paths;
  for (auto& sst : sstables_) old_paths.push_back(sst->path());
  sstables_.clear();
  sstables_.push_back(std::make_unique<SSTable>(path));
  bytes_written_ += sstables_.back()->file_size();
  for (const std::string& p : old_paths) ::unlink(p.c_str());
}

uint64_t LSMEngine::disk_size() const {
  uint64_t total = 0;
  for (const auto& sst : sstables_) total += sst->file_size();
  off_t wal = ::lseek(wal_fd_, 0, SEEK_END);
  if (wal > 0) total += static_cast<uint64_t>(wal);
  return total;
}

}  // namespace walterdb
