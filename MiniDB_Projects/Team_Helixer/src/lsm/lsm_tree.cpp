#include "lsm/lsm_tree.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace minidb {
namespace {
// Serialize a sorted run: [u32 count]{ [i32 key][u8 tombstone][u32 len][bytes] }
std::vector<char> encode_run(const std::vector<LSMEntry> &es) {
    std::vector<char> b;
    auto put = [&](const void *p, size_t n) {
        const char *c = (const char *)p; b.insert(b.end(), c, c + n);
    };
    uint32_t cnt = (uint32_t)es.size();
    put(&cnt, 4);
    for (auto &e : es) {
        put(&e.key, 4);
        uint8_t t = e.tombstone ? 1 : 0; put(&t, 1);
        uint32_t len = (uint32_t)e.value.size(); put(&len, 4);
        put(e.value.data(), len);
    }
    return b;
}
} // namespace

SSTable SSTable::create(const std::string &path, const std::vector<LSMEntry> &sorted) {
    std::vector<char> bytes = encode_run(sorted);
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw std::runtime_error("SSTable: cannot create " + path);
    // One sequential write — the LSM flush is a streaming append, not random I/O.
    ssize_t n = ::write(fd, bytes.data(), bytes.size());
    ::close(fd);
    if (n != (ssize_t)bytes.size()) throw std::runtime_error("SSTable: short write");

    SSTable t;
    t.path_ = path;
    t.entries_ = sorted;
    t.bytes_ = bytes.size();
    return t;
}

SSTable SSTable::open(const std::string &path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw std::runtime_error("SSTable: cannot open " + path);
    struct stat st; ::fstat(fd, &st);
    std::vector<char> bytes(st.st_size);
    ssize_t got = ::read(fd, bytes.data(), bytes.size());
    ::close(fd);
    (void)got;

    SSTable t; t.path_ = path; t.bytes_ = bytes.size();
    const char *p = bytes.data();
    uint32_t cnt; std::memcpy(&cnt, p, 4); p += 4;
    t.entries_.reserve(cnt);
    for (uint32_t i = 0; i < cnt; ++i) {
        LSMEntry e;
        std::memcpy(&e.key, p, 4); p += 4;
        uint8_t tomb; std::memcpy(&tomb, p, 1); p += 1; e.tombstone = tomb != 0;
        uint32_t len; std::memcpy(&len, p, 4); p += 4;
        e.value.assign(p, p + len); p += len;
        t.entries_.push_back(std::move(e));
    }
    return t;
}

bool SSTable::get(int32_t key, LSMEntry *out) const {
    // Entries are sorted by key -> binary search.
    int lo = 0, hi = (int)entries_.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (entries_[mid].key == key) { if (out) *out = entries_[mid]; return true; }
        if (entries_[mid].key < key) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

LSMTree::LSMTree(const std::string &dir, size_t memtable_limit)
    : dir_(dir), limit_(memtable_limit) {
    ::mkdir(dir_.c_str(), 0755); // ok if it already exists
}

void LSMTree::put(int32_t key, const std::string &value) {
    mem_[key] = {key, false, value};
    if (mem_.size() >= limit_) flush();
}

void LSMTree::remove(int32_t key) {
    mem_[key] = {key, true, ""}; // tombstone
    if (mem_.size() >= limit_) flush();
}

bool LSMTree::get(int32_t key, std::string *out) {
    // 1) MemTable (most recent).
    auto it = mem_.find(key);
    if (it != mem_.end()) {
        if (it->second.tombstone) return false;
        if (out) *out = it->second.value;
        return true;
    }
    // 2) SSTables, newest -> oldest. First hit wins.
    for (auto rit = ssts_.rbegin(); rit != ssts_.rend(); ++rit) {
        LSMEntry e;
        if (rit->get(key, &e)) {
            if (e.tombstone) return false;
            if (out) *out = e.value;
            return true;
        }
    }
    return false;
}

void LSMTree::flush() {
    if (mem_.empty()) return;
    std::vector<LSMEntry> run;
    run.reserve(mem_.size());
    for (auto &kv : mem_) run.push_back(kv.second); // std::map iterates sorted
    std::string path = dir_ + "/sst_" + std::to_string(next_id_++) + ".dat";
    ssts_.push_back(SSTable::create(path, run));
    mem_.clear();
}

void LSMTree::compact() {
    flush(); // fold the MemTable in first
    if (ssts_.size() <= 1) return;

    // Merge oldest -> newest so newer values overwrite older ones.
    std::map<int32_t, LSMEntry> merged;
    for (auto &sst : ssts_)
        for (auto &e : sst.entries()) merged[e.key] = e;

    // Full compaction: drop tombstones (no older data remains to mask).
    std::vector<LSMEntry> run;
    for (auto &kv : merged)
        if (!kv.second.tombstone) run.push_back(kv.second);

    // Remove the old SSTable files, then write the single compacted run.
    for (auto &sst : ssts_) ::remove(sst.path().c_str());
    ssts_.clear();
    std::string path = dir_ + "/sst_" + std::to_string(next_id_++) + ".dat";
    ssts_.push_back(SSTable::create(path, run));
}

size_t LSMTree::total_disk_bytes() const {
    size_t total = 0;
    for (auto &sst : ssts_) total += sst.disk_bytes();
    return total;
}

} // namespace minidb
