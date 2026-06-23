#include "engine/lsm/lsm_engine.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

#include "common/exception.h"

namespace minidb {

namespace fs = std::filesystem;

LsmEngine::LsmEngine(std::string dir, std::size_t memtable_limit_bytes, std::size_t compaction_trigger)
    : dir_(std::move(dir)), mem_limit_(memtable_limit_bytes), compaction_trigger_(compaction_trigger) {
    fs::create_directories(dir_);
}

LsmEngine::LsmTable& LsmEngine::require(const std::string& table) {
    auto it = tables_.find(table);
    if (it == tables_.end()) throw DBException("LsmEngine: no such table: " + table);
    return it->second;
}

std::string LsmEngine::sst_path(const std::string& table, int id) const {
    return dir_ + "/" + table + "_" + std::to_string(id) + ".sst";
}

void LsmEngine::create_table(const std::string& table, const Schema&, int) {
    tables_.try_emplace(table);  // schema/pk are not needed by a KV engine
}

bool LsmEngine::put(const std::string& table, std::int64_t key, const std::string& row) {
    LsmTable& t = require(table);
    t.mem.put(key, row, t.seq++);
    if (t.mem.bytes() >= mem_limit_) flush_table(table, t);
    return true;
}

bool LsmEngine::get(const std::string& table, std::int64_t key, std::string& out) {
    LsmTable& t = require(table);
    LsmEntry e;
    if (t.mem.get(key, e)) { if (e.tombstone) return false; out = e.row; return true; }
    for (const auto& sst : t.ssts) {                 // newest -> oldest
        if (sst->get(key, e)) { if (e.tombstone) return false; out = e.row; return true; }
    }
    return false;
}

bool LsmEngine::erase(const std::string& table, std::int64_t key) {
    LsmTable& t = require(table);
    t.mem.del(key, t.seq++);  // tombstone; shadows older versions until compaction
    return true;
}

void LsmEngine::flush_table(const std::string& table, LsmTable& t) {
    if (t.mem.empty()) return;
    auto sst = SSTable::create(sst_path(table, t.next_id++), t.mem.entries());
    t.ssts.insert(t.ssts.begin(), sst);  // newest first
    t.mem.clear();
    if (t.ssts.size() >= compaction_trigger_) compact(table);
}

void LsmEngine::compact(const std::string& table) {
    LsmTable& t = require(table);
    if (t.ssts.empty()) return;

    // Merge oldest -> newest so the newest version of each key wins.
    std::map<std::int64_t, LsmEntry> merged;
    for (auto it = t.ssts.rbegin(); it != t.ssts.rend(); ++it)
        (*it)->for_each([&](std::int64_t k, const LsmEntry& e) { merged[k] = e; });

    // This is a full compaction to a single run, so tombstones can be dropped.
    std::map<std::int64_t, LsmEntry> live;
    for (auto& [k, e] : merged) if (!e.tombstone) live[k] = e;

    auto compacted = SSTable::create(sst_path(table, t.next_id++), live);

    std::vector<std::string> old_paths;
    for (const auto& sst : t.ssts) old_paths.push_back(sst->path());
    t.ssts.assign(1, compacted);
    for (const auto& p : old_paths) std::remove(p.c_str());
}

namespace {
// Cursor over a materialised, key-ordered snapshot (used for scan/range).
class VecCursor : public StorageEngine::Cursor {
public:
    explicit VecCursor(std::vector<std::pair<std::int64_t, std::string>> rows)
        : rows_(std::move(rows)) {}
    bool next(std::int64_t& key, std::string& row) override {
        if (i_ >= rows_.size()) return false;
        key = rows_[i_].first;
        row = rows_[i_].second;
        ++i_;
        return true;
    }
private:
    std::vector<std::pair<std::int64_t, std::string>> rows_;
    std::size_t i_ = 0;
};

// Materialise the live, key-ordered view of a table by merging all sources
// (SSTables oldest->newest, then the MemTable as newest), newest wins, tombstones dropped.
std::vector<std::pair<std::int64_t, std::string>> materialise(
    const std::vector<std::shared_ptr<SSTable>>& ssts, const MemTable& mem,
    std::int64_t lo, std::int64_t hi, bool bounded) {
    std::map<std::int64_t, LsmEntry> merged;
    for (auto it = ssts.rbegin(); it != ssts.rend(); ++it)
        (*it)->for_each([&](std::int64_t k, const LsmEntry& e) { merged[k] = e; });
    for (const auto& [k, e] : mem.entries()) merged[k] = e;

    std::vector<std::pair<std::int64_t, std::string>> out;
    for (auto& [k, e] : merged) {
        if (e.tombstone) continue;
        if (bounded && (k < lo || k > hi)) continue;
        out.emplace_back(k, e.row);
    }
    return out;
}
} // namespace

std::unique_ptr<StorageEngine::Cursor> LsmEngine::scan(const std::string& table) {
    LsmTable& t = require(table);
    return std::make_unique<VecCursor>(materialise(t.ssts, t.mem, 0, 0, /*bounded=*/false));
}

std::unique_ptr<StorageEngine::Cursor> LsmEngine::range(const std::string& table,
                                                        std::int64_t lo, std::int64_t hi) {
    LsmTable& t = require(table);
    return std::make_unique<VecCursor>(materialise(t.ssts, t.mem, lo, hi, /*bounded=*/true));
}

void LsmEngine::flush() {
    for (auto& [name, t] : tables_) flush_table(name, t);
}

EngineStats LsmEngine::stats(const std::string& table) {
    LsmTable& t = require(table);
    EngineStats s;
    for (const auto& sst : t.ssts) s.bytes_on_disk += sst->file_size();
    auto cur = scan(table);
    std::int64_t k; std::string r;
    while (cur->next(k, r)) ++s.live_rows;
    return s;
}

} // namespace minidb
