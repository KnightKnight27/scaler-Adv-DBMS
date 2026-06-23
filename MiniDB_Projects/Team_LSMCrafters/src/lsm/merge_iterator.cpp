#include "lsm/merge_iterator.h"

namespace minidb {

bool MemTableSource::next(Key& key, ValueEntry& entry) {
  if (it_ == end_) return false;
  key   = it_->first;
  entry = it_->second;
  ++it_;
  return true;
}

MergeIterator::MergeIterator(std::vector<std::unique_ptr<MergeSource>> sources)
    : sources_(std::move(sources)), heads_(sources_.size()) {
  for (std::size_t i = 0; i < sources_.size(); ++i) advance(i);
}

void MergeIterator::advance(std::size_t i) {
  Head h;
  h.valid = sources_[i]->next(h.key, h.entry);
  heads_[i] = std::move(h);
}

bool MergeIterator::next(Key& key, ValueEntry& entry, bool skip_tombstones) {
  while (true) {
    // Find the smallest key among the live heads.
    bool found_min = false;
    Key  min_key   = 0;
    for (const Head& h : heads_) {
      if (h.valid && (!found_min || h.key < min_key)) { min_key = h.key; found_min = true; }
    }
    if (!found_min) return false;

    // Among heads at min_key, the newest (highest seq) wins. Advance them all.
    ValueEntry winner;
    bool have_winner = false;
    for (std::size_t i = 0; i < heads_.size(); ++i) {
      if (heads_[i].valid && heads_[i].key == min_key) {
        if (!have_winner || heads_[i].entry.seq > winner.seq) { winner = heads_[i].entry; have_winner = true; }
        advance(i);
      }
    }

    if (skip_tombstones && winner.type == RecType::Tombstone) continue;
    key   = min_key;
    entry = std::move(winner);
    return true;
  }
}

}  // namespace minidb
