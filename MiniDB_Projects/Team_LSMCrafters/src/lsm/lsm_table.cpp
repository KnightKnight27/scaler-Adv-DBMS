#include "lsm/lsm_table.h"
#include <algorithm>
#include <filesystem>
#include "lsm/compactor.h"
#include "lsm/merge_iterator.h"
#include "lsm/sstable_writer.h"

namespace minidb {
namespace {

std::string ensure_dir(std::string dir) {
  std::filesystem::create_directories(dir);
  return dir;
}

// Cursor over the merged view of MemTable + SSTables, skipping tombstones. When
// ranged, it returns only keys in [lo, hi] (and stops early, since keys arrive
// in ascending order).
class LsmScanCursor : public RowCursor {
 public:
  LsmScanCursor(MergeIterator merge, Key lo, Key hi, bool ranged)
      : merge_(std::move(merge)), lo_(lo), hi_(hi), ranged_(ranged) {}

  bool next(Key& out_key, Bytes& out_value) override {
    Key key;
    ValueEntry entry;
    while (merge_.next(key, entry, /*skip_tombstones=*/true)) {
      if (ranged_ && key < lo_) continue;
      if (ranged_ && key > hi_) return false;
      out_key   = key;
      out_value = std::move(entry.value);
      return true;
    }
    return false;
  }

 private:
  MergeIterator merge_;
  Key           lo_, hi_;
  bool          ranged_;
};

}  // namespace

LsmTable::LsmTable(std::string dir, LsmOptions options)
    : dir_(ensure_dir(std::move(dir))),
      opt_(options),
      manifest_(dir_ + "/MANIFEST"),
      wal_(dir_ + "/wal.log", /*truncate=*/false) {
  manifest_.load();
  next_seq_  = manifest_.next_seq;
  next_file_ = manifest_.next_file;
  for (const std::string& path : manifest_.sstables)
    sstables_.push_back(std::make_unique<SSTableReader>(path));
  // Recover any unflushed writes from the WAL into the active MemTable.
  SeqNo wal_max = LsmWal::replay(dir_ + "/wal.log", active_);
  next_seq_ = std::max(next_seq_, wal_max + 1);
}

void LsmTable::note_key(Key key) {
  if (stats_.row_count == 0) stats_.min_key = stats_.max_key = key;
  else { stats_.min_key = std::min(stats_.min_key, key); stats_.max_key = std::max(stats_.max_key, key); }
  stats_.row_count += 1;  // approximate (ignores overwrites/deletes)
}

std::string LsmTable::new_sstable_path() {
  return dir_ + "/sst_" + std::to_string(next_file_++) + ".dat";
}

void LsmTable::insert(Key key, const Bytes& value) {
  SeqNo seq = next_seq();
  wal_.append_put(key, value, seq, opt_.sync_wal);
  active_.put(key, value, seq);
  note_key(key);
  maybe_flush();
}

void LsmTable::erase(Key key) {
  SeqNo seq = next_seq();
  wal_.append_del(key, seq, opt_.sync_wal);
  active_.del(key, seq);
  maybe_flush();
}

std::optional<Bytes> LsmTable::get(Key key) {
  if (auto e = active_.get(key))
    return e->type == RecType::Tombstone ? std::nullopt : std::optional<Bytes>(e->value);
  // SSTables newest first: the first hit wins.
  for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
    if (auto e = (*it)->get(key))
      return e->type == RecType::Tombstone ? std::nullopt : std::optional<Bytes>(e->value);
  }
  return std::nullopt;
}

std::vector<std::unique_ptr<MergeSource>> LsmTable::make_sources() {
  std::vector<std::unique_ptr<MergeSource>> sources;
  sources.push_back(std::make_unique<MemTableSource>(active_.entries()));
  for (auto& reader : sstables_)
    sources.push_back(std::make_unique<SSTableSource>(reader->iterate()));
  return sources;
}

std::unique_ptr<RowCursor> LsmTable::scan() {
  return std::make_unique<LsmScanCursor>(MergeIterator(make_sources()), 0, 0, false);
}

std::unique_ptr<RowCursor> LsmTable::index_range(Key lo, Key hi) {
  return std::make_unique<LsmScanCursor>(MergeIterator(make_sources()), lo, hi, true);
}

void LsmTable::flush() { flush_active(); }

void LsmTable::flush_active() {
  if (active_.empty()) return;
  std::string path = new_sstable_path();
  SSTableWriter writer(path);
  for (const auto& [key, entry] : active_.entries()) writer.add(key, entry);
  writer.finish();

  sstables_.push_back(std::make_unique<SSTableReader>(path));
  active_.clear();
  wal_.reset();  // the MemTable is now durable as an SSTable

  manifest_.sstables.push_back(path);
  manifest_.next_seq  = next_seq_;
  manifest_.next_file = next_file_;
  manifest_.save();
}

void LsmTable::maybe_flush() {
  if (active_.approx_bytes() < opt_.memtable_threshold) return;
  flush_active();
  maybe_compact();
}

void LsmTable::maybe_compact() {
  if (static_cast<int>(sstables_.size()) >= opt_.l0_trigger) force_compact();
}

void LsmTable::force_compact() {
  if (sstables_.size() < 2) return;

  std::vector<SSTableReader*> inputs;
  std::vector<std::string>    old_paths;
  for (auto& reader : sstables_) { inputs.push_back(reader.get()); old_paths.push_back(reader->meta().path); }

  std::string out = new_sstable_path();
  Compactor::compact(inputs, out);

  sstables_.clear();  // close readers before deleting their files
  for (const std::string& p : old_paths) std::filesystem::remove(p);

  sstables_.push_back(std::make_unique<SSTableReader>(out));
  manifest_.sstables  = {out};
  manifest_.next_seq  = next_seq_;
  manifest_.next_file = next_file_;
  manifest_.save();
}

uint64_t LsmTable::disk_bytes() const {
  uint64_t total = 0;
  for (const auto& reader : sstables_) total += reader->meta().file_bytes;
  return total;
}

}  // namespace minidb
