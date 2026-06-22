#pragma once

#include "sstable.h"
#include <queue>
#include <algorithm>

namespace minidb {

// compaction — merges multiple sstables into one, removing tombstones
// uses a min-heap to perform a k-way merge of sorted sstables.
// output is a new sstable.  old sstable files are deleted after a
// successful compaction.

class Compaction {
public:
    // merge `inputs` sstables into a single output sstable at `output_path`.
    // returns true on success.
    static bool compact(const std::vector<SSTable*>& inputs,
                        const std::string& output_path);

private:
    // heap entry for k-way merge.
    struct HeapEntry {
        Key      key;
        Record   record;
        size_t   sstable_idx;   // which input sstable
        uint32_t data_offset;    // offset within that sstable's index

        bool operator>(const HeapEntry& o) const { return key > o.key; }
    };
};

inline bool Compaction::compact(const std::vector<SSTable*>& inputs,
                                 const std::string& output_path) {
    if (inputs.empty()) return false;

    // min-heap seeded with the first entry of each sstable.
    std::priority_queue<HeapEntry, std::vector<HeapEntry>,
                        std::greater<HeapEntry>> heap;

    // track current index positions per sstable.
    std::vector<size_t> positions(inputs.size(), 0);

    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto& idx = inputs[i]->_index;
        if (!idx.empty()) {
            auto [key, rec] = inputs[i]->read_entry_at(idx[0].second);
            heap.push({key, rec, i, idx[0].second});
        }
    }

    // collect merged entries into a vector (in order).
    std::vector<std::pair<Key, Record>> merged;

    // track last key to remove duplicates (keep latest = highest sstable_idx).
    // since we process by key order, when we see the same key, the later
    // entry (from a newer sstable) wins.
    Key last_key;
    bool has_last = false;

    while (!heap.empty()) {
        HeapEntry top = heap.top();
        heap.pop();

        // skip tombstones (empty records)
        if (!top.record.empty()) {
            // de-duplicate: if same key as previous, replace (newer wins)
            if (has_last && top.key == last_key) {
                merged.back().second = top.record;
            } else {
                merged.emplace_back(top.key, top.record);
                last_key = top.key;
                has_last = true;
            }
        } else {
            // tombstone: remove from output (don't add to merged)
            has_last = false;
        }

        // advance the iterator for this sstable and push next entry.
        size_t idx = top.sstable_idx;
        positions[idx]++;
        const auto& sst_idx = inputs[idx]->_index;
        if (positions[idx] < sst_idx.size()) {
            uint32_t next_off = sst_idx[positions[idx]].second;
            auto [nkey, nrec] = inputs[idx]->read_entry_at(next_off);
            heap.push({nkey, nrec, idx, next_off});
        }
    }

    // build output sstable
    if (merged.empty()) {
        // create empty file so it exists
        SSTable empty_out;
        empty_out.build(output_path, merged.begin(), merged.end());
    } else {
        SSTable out;
        out.build(output_path, merged.begin(), merged.end());
    }

    // delete old sstable files
    for (auto* sst : inputs) {
        if (sst) sst->remove_file();
    }

    return true;
}

} // namespace minidb
