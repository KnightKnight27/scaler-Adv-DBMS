#include "lsm/sstable.hpp"

#include <cstring>

namespace minidb {

namespace {
void write_u32(std::ofstream& o, uint32_t v) { o.write(reinterpret_cast<char*>(&v), 4); }
void write_u64(std::ofstream& o, uint64_t v) { o.write(reinterpret_cast<char*>(&v), 8); }
void write_entry(std::ofstream& o, const std::string& key, const MemEntry& e) {
    write_u32(o, static_cast<uint32_t>(key.size()));
    o.write(key.data(), key.size());
    uint8_t tomb = e.tombstone ? 1 : 0;
    o.write(reinterpret_cast<char*>(&tomb), 1);
    write_u32(o, static_cast<uint32_t>(e.value.size()));
    o.write(e.value.data(), e.value.size());
}
}  // namespace

void SSTable::build(const std::string& path,
                    const std::map<std::string, MemEntry>& entries) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    write_u64(o, static_cast<uint64_t>(entries.size()));
    for (auto& [k, e] : entries) write_entry(o, k, e);
    o.flush();
}

void SSTable::build_from_vector(
    const std::string& path,
    const std::vector<std::pair<std::string, MemEntry>>& entries) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    write_u64(o, static_cast<uint64_t>(entries.size()));
    for (auto& [k, e] : entries) write_entry(o, k, e);
    o.flush();
}

void SSTable::open() {
    index_.clear();
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) return;
    uint64_t count;
    in.read(reinterpret_cast<char*>(&count), 8);
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t offset = static_cast<uint64_t>(in.tellg());
        uint32_t klen;
        in.read(reinterpret_cast<char*>(&klen), 4);
        std::string key(klen, '\0');
        in.read(key.data(), klen);
        uint8_t tomb;
        in.read(reinterpret_cast<char*>(&tomb), 1);
        uint32_t vlen;
        in.read(reinterpret_cast<char*>(&vlen), 4);
        in.seekg(vlen, std::ios::cur);  // skip value; we only index offsets
        index_[key] = offset;
    }
}

std::ifstream& SSTable::reader() {
    if (!reader_) reader_ = std::make_unique<std::ifstream>(path_, std::ios::binary);
    return *reader_;
}

std::optional<MemEntry> SSTable::get(const std::string& key) {
    auto it = index_.find(key);
    if (it == index_.end()) return std::nullopt;
    std::ifstream& in = reader();
    in.clear();
    in.seekg(static_cast<std::streamoff>(it->second), std::ios::beg);
    uint32_t klen;
    in.read(reinterpret_cast<char*>(&klen), 4);
    in.seekg(klen, std::ios::cur);  // skip key (we already matched it)
    uint8_t tomb;
    in.read(reinterpret_cast<char*>(&tomb), 1);
    uint32_t vlen;
    in.read(reinterpret_cast<char*>(&vlen), 4);
    std::string val(vlen, '\0');
    in.read(val.data(), vlen);
    return MemEntry{val, tomb != 0};
}

std::vector<std::pair<std::string, MemEntry>> SSTable::scan() {
    std::vector<std::pair<std::string, MemEntry>> out;
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) return out;
    uint64_t count;
    in.read(reinterpret_cast<char*>(&count), 8);
    for (uint64_t i = 0; i < count; ++i) {
        uint32_t klen;
        in.read(reinterpret_cast<char*>(&klen), 4);
        std::string key(klen, '\0');
        in.read(key.data(), klen);
        uint8_t tomb;
        in.read(reinterpret_cast<char*>(&tomb), 1);
        uint32_t vlen;
        in.read(reinterpret_cast<char*>(&vlen), 4);
        std::string val(vlen, '\0');
        in.read(val.data(), vlen);
        out.emplace_back(std::move(key), MemEntry{std::move(val), tomb != 0});
    }
    return out;
}

}  // namespace minidb
