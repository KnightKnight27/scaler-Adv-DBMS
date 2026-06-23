#pragma once
#include <map>
#include <memory>
#include <vector>
#include "lsm/lsm_types.h"
#include "lsm/sstable_reader.h"

namespace minidb {

// A source of (key, entry) pairs in ascending key order.
struct MergeSource {
  virtual bool next(Key& key, ValueEntry& entry) = 0;
  virtual ~MergeSource() = default;
};

// Iterates a MemTable snapshot.
class MemTableSource : public MergeSource {
 public:
  explicit MemTableSource(const std::map<Key, ValueEntry>& table)
      : it_(table.begin()), end_(table.end()) {}
  bool next(Key& key, ValueEntry& entry) override;

 private:
  std::map<Key, ValueEntry>::const_iterator it_, end_;
};

// Iterates one SSTable's data block.
class SSTableSource : public MergeSource {
 public:
  explicit SSTableSource(SSTableReader::Iter it) : it_(std::move(it)) {}
  bool next(Key& key, ValueEntry& entry) override { return it_.next(key, entry); }

 private:
  SSTableReader::Iter it_;
};

// Merges several sorted sources into one ascending stream, keeping only the
// newest version of each key (highest seq). Used both by LsmTable::scan and by
// compaction; passing skip_tombstones=true drops deleted keys.
class MergeIterator {
 public:
  explicit MergeIterator(std::vector<std::unique_ptr<MergeSource>> sources);
  bool next(Key& key, ValueEntry& entry, bool skip_tombstones);

 private:
  struct Head {
    bool       valid = false;
    Key        key   = 0;
    ValueEntry entry;
  };
  void advance(std::size_t i);

  std::vector<std::unique_ptr<MergeSource>> sources_;
  std::vector<Head>                         heads_;
};

}  // namespace minidb
