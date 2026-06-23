#include "engine/lsm/sstable.h"

#include <cstring>
#include <fstream>

#include "common/exception.h"

namespace minidb {

namespace {
template <typename T>
void put_raw(std::ofstream& os, T v) { os.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
template <typename T>
bool get_raw(std::ifstream& is, T& v) { return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T))); }
} // namespace

std::shared_ptr<SSTable> SSTable::create(const std::string& path,
                                         const std::map<std::int64_t, LsmEntry>& entries) {
    std::ofstream os(path, std::ios::binary | std::ios::trunc);
    if (!os) throw DBException("SSTable: cannot create " + path);

    std::map<std::int64_t, std::uint64_t> index;
    BloomFilter bloom(entries.size() ? entries.size() : 1);
    std::uint64_t offset = 0;

    for (const auto& [key, e] : entries) {
        index[key] = offset;
        bloom.add(key);
        put_raw<std::int64_t>(os, key);
        put_raw<std::uint8_t>(os, e.tombstone ? 1 : 0);
        put_raw<std::uint32_t>(os, static_cast<std::uint32_t>(e.row.size()));
        os.write(e.row.data(), static_cast<std::streamsize>(e.row.size()));
        offset += sizeof(std::int64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + e.row.size();
    }
    os.flush();
    return std::shared_ptr<SSTable>(new SSTable(path, std::move(index), std::move(bloom), offset));
}

std::shared_ptr<SSTable> SSTable::open(const std::string& path) {
    std::ifstream is(path, std::ios::binary);
    if (!is) throw DBException("SSTable: cannot open " + path);

    std::map<std::int64_t, std::uint64_t> index;
    std::uint64_t offset = 0;
    std::int64_t key;
    // First pass to size the Bloom filter, then rebuild it.
    std::map<std::int64_t, LsmEntry> tmp;
    while (get_raw(is, key)) {
        std::uint8_t tomb; std::uint32_t len;
        get_raw(is, tomb); get_raw(is, len);
        std::string row(len, '\0');
        if (len) is.read(&row[0], len);
        index[key] = offset;
        tmp[key] = LsmEntry{std::move(row), tomb != 0, 0};
        offset += sizeof(std::int64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + len;
    }
    BloomFilter bloom(tmp.size() ? tmp.size() : 1);
    for (const auto& [k, _] : tmp) bloom.add(k);
    return std::shared_ptr<SSTable>(new SSTable(path, std::move(index), std::move(bloom), offset));
}

bool SSTable::get(std::int64_t key, LsmEntry& out) const {
    if (!bloom_.maybe_contains(key)) return false;  // definitely absent -> skip file
    auto it = index_.find(key);
    if (it == index_.end()) return false;

    std::ifstream is(path_, std::ios::binary);
    is.seekg(static_cast<std::streamoff>(it->second));
    std::int64_t k; std::uint8_t tomb; std::uint32_t len;
    get_raw(is, k); get_raw(is, tomb); get_raw(is, len);
    std::string row(len, '\0');
    if (len) is.read(&row[0], len);
    out = LsmEntry{std::move(row), tomb != 0, 0};
    return true;
}

void SSTable::for_each(const std::function<void(std::int64_t, const LsmEntry&)>& fn) const {
    std::ifstream is(path_, std::ios::binary);
    std::int64_t key;
    while (get_raw(is, key)) {
        std::uint8_t tomb; std::uint32_t len;
        get_raw(is, tomb); get_raw(is, len);
        std::string row(len, '\0');
        if (len) is.read(&row[0], len);
        LsmEntry e{std::move(row), tomb != 0, 0};
        fn(key, e);
    }
}

} // namespace minidb
