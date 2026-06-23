#include "lsm/lsm_tree.hpp"

#include <algorithm>
#include <filesystem>

namespace minidb {

namespace fs = std::filesystem;

LSMTree::LSMTree(std::string dir, Options opts) : dir_(std::move(dir)), opts_(opts) {
    fs::create_directories(dir_);
    // Recover any existing SSTables, in id order (oldest -> newest).
    std::vector<std::pair<int, std::string>> found;
    for (auto& e : fs::directory_iterator(dir_)) {
        std::string name = e.path().filename().string();
        if (name.rfind("sst_", 0) == 0 && e.path().extension() == ".sst") {
            int id = std::stoi(name.substr(4, name.find('.') - 4));
            found.emplace_back(id, e.path().string());
        }
    }
    std::sort(found.begin(), found.end());
    for (auto& [id, path] : found) {
        auto sst = std::make_unique<SSTable>(path);
        sst->open();
        sstables_.push_back(std::move(sst));
        next_id_ = std::max(next_id_, id + 1);
    }
}

std::string LSMTree::next_sstable_path() {
    return dir_ + "/sst_" + std::to_string(next_id_++) + ".sst";
}

void LSMTree::put(const std::string& key, const std::string& value) {
    mem_.put(key, value);
    maybe_flush();
}

void LSMTree::del(const std::string& key) {
    mem_.del(key);
    maybe_flush();
}

std::optional<std::string> LSMTree::get(const std::string& key) {
    if (auto e = mem_.get(key)) {
        if (e->tombstone) return std::nullopt;
        return e->value;
    }
    // Newest SSTable wins: iterate back -> front.
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        if (auto e = (*it)->get(key)) {
            if (e->tombstone) return std::nullopt;
            return e->value;
        }
    }
    return std::nullopt;
}

void LSMTree::maybe_flush() {
    if (mem_.size_bytes() >= opts_.memtable_bytes) flush();
}

void LSMTree::flush() {
    if (mem_.empty()) return;
    std::string path = next_sstable_path();
    SSTable::build(path, mem_.entries());
    auto sst = std::make_unique<SSTable>(path);
    sst->open();
    sstables_.push_back(std::move(sst));
    mem_.clear();
    flushes_++;
    maybe_compact();
}

void LSMTree::maybe_compact() {
    if (sstables_.size() > opts_.compaction_trigger) compact();
}

void LSMTree::compact() {
    if (sstables_.size() < 2) return;
    // K-way merge by repeatedly taking the smallest key across all runs; for a
    // key present in multiple runs, the newest (later in sstables_) wins.
    // Simpler correct approach: fold newest -> oldest into a map, first-writer
    // wins (i.e., newest value kept), then drop tombstones.
    std::map<std::string, MemEntry> merged;
    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        for (auto& [k, e] : (*it)->scan()) {
            if (merged.find(k) == merged.end()) merged[k] = e;  // newest seen first
        }
    }
    // Drop tombstones: after full compaction there is nothing older to shadow.
    std::vector<std::pair<std::string, MemEntry>> out;
    for (auto& [k, e] : merged)
        if (!e.tombstone) out.emplace_back(k, e);

    std::string path = next_sstable_path();
    SSTable::build_from_vector(path, out);

    // Remove old SSTable files and replace with the single compacted run.
    for (auto& sst : sstables_) { std::error_code ec; fs::remove(sst->path(), ec); }
    sstables_.clear();
    auto sst = std::make_unique<SSTable>(path);
    sst->open();
    sstables_.push_back(std::move(sst));
    compactions_++;
}

LSMTree::Stats LSMTree::stats() const {
    Stats s;
    s.memtable_entries = mem_.count();
    s.num_sstables = sstables_.size();
    s.flushes = flushes_;
    s.compactions = compactions_;
    return s;
}

}  // namespace minidb
