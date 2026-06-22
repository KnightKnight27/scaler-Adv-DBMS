#include "lsm/sstable.h"
#include <fstream>
#include <algorithm>
#include <filesystem>

void SSTable::Write(const std::string& filename, const std::vector<SSEntry>& entries) {
    std::filesystem::path p(filename);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

    std::ofstream out(filename, std::ios::binary);
    int count = entries.size();
    out.write((char*)&count, 4);

    for (auto& e : entries) {
        out.write((char*)&e.key, 4);
        uint8_t del = e.deleted ? 1 : 0;
        out.write((char*)&del, 1);
        int dlen = e.data.size();
        out.write((char*)&dlen, 4);
        if (dlen > 0) out.write(e.data.data(), dlen);
    }
}

SSTable::SSTable(const std::string& filename) : path_(filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) return;

    int count;
    in.read((char*)&count, 4);

    for (int i = 0; i < count; i++) {
        SSEntry e;
        in.read((char*)&e.key, 4);
        uint8_t del; in.read((char*)&del, 1);
        e.deleted = (del != 0);
        int dlen; in.read((char*)&dlen, 4);
        if (dlen > 0) {
            e.data.resize(dlen);
            in.read(e.data.data(), dlen);
        }
        entries_.push_back(e);
    }
}

const SSEntry* SSTable::Get(int key) const {
    // Binary search (entries are sorted by key)
    int lo = 0, hi = entries_.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (entries_[mid].key == key) return &entries_[mid];
        if (entries_[mid].key < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

void SSTable::Merge(const std::vector<SSTable*>& tables, const std::string& output_file) {
    // Merge-sort all entries. If duplicate keys, newer table (lower index) wins.
    std::vector<std::pair<int, const SSEntry*>> all;  // (table_idx, entry)
    for (int t = 0; t < (int)tables.size(); t++) {
        for (auto& e : tables[t]->GetEntries()) {
            all.push_back({t, &e});
        }
    }

    // Sort by key, then by table index (lower = newer = wins)
    std::sort(all.begin(), all.end(), [](auto& a, auto& b) {
        if (a.second->key != b.second->key) return a.second->key < b.second->key;
        return a.first < b.first;
    });

    // Deduplicate: keep first occurrence of each key
    std::vector<SSEntry> merged;
    int prev_key = -1;
    for (auto& [_, entry] : all) {
        if (entry->key == prev_key) continue;
        if (!entry->deleted) {  // drop tombstones during compaction
            merged.push_back(*entry);
        }
        prev_key = entry->key;
    }

    Write(output_file, merged);
}
