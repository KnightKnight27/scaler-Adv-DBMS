#include "lsm/sstable.h"
#include <stdexcept>

namespace minidb {

void SSTable::Build(const std::string &path, const std::map<int64_t, LSMEntry> &entries) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) throw std::runtime_error("SSTable::Build cannot open " + path);

  int64_t count = static_cast<int64_t>(entries.size());
  out.write(reinterpret_cast<char *>(&count), 8);

  BloomFilter bloom = BloomFilter::ForN(entries.size());
  for (const auto &kv : entries) {
    int64_t key = kv.first;
    uint8_t deleted = kv.second.deleted ? 1 : 0;
    uint32_t vlen = static_cast<uint32_t>(kv.second.value.size());
    out.write(reinterpret_cast<char *>(&key), 8);
    out.write(reinterpret_cast<char *>(&deleted), 1);
    out.write(reinterpret_cast<char *>(&vlen), 4);
    out.write(kv.second.value.data(), vlen);
    bloom.Add(key);
  }

  std::string blob = bloom.Serialize();
  out.write(blob.data(), blob.size());
  int64_t bloom_size = static_cast<int64_t>(blob.size());
  out.write(reinterpret_cast<char *>(&bloom_size), 8);
  out.flush();
}

SSTable::SSTable(const std::string &path) : path_(path) {
  in_.open(path, std::ios::binary);
  if (!in_.is_open()) throw std::runtime_error("SSTable cannot open " + path);

  // Load the Bloom filter from the trailer.
  in_.seekg(0, std::ios::end);
  std::streamoff file_size = in_.tellg();
  in_.seekg(file_size - 8);
  int64_t bloom_size = 0;
  in_.read(reinterpret_cast<char *>(&bloom_size), 8);
  std::streamoff bloom_off = file_size - 8 - bloom_size;
  in_.seekg(bloom_off);
  std::string blob(static_cast<size_t>(bloom_size), '\0');
  in_.read(&blob[0], bloom_size);
  bloom_ = BloomFilter::Deserialize(blob);

  // Scan the data section to build the in-memory key index.
  in_.seekg(0);
  int64_t count = 0;
  in_.read(reinterpret_cast<char *>(&count), 8);
  for (int64_t i = 0; i < count; ++i) {
    IndexEntry e;
    uint8_t deleted;
    in_.read(reinterpret_cast<char *>(&e.key), 8);
    in_.read(reinterpret_cast<char *>(&deleted), 1);
    in_.read(reinterpret_cast<char *>(&e.vlen), 4);
    e.deleted = (deleted != 0);
    e.value_off = in_.tellg();
    index_.push_back(e);
    in_.seekg(e.vlen, std::ios::cur);  // skip the value bytes
  }
}

LSMEntry SSTable::ReadAt(size_t i) {
  const IndexEntry &e = index_[i];
  LSMEntry out;
  out.deleted = e.deleted;
  if (e.vlen > 0) {
    out.value.resize(e.vlen);
    in_.seekg(e.value_off);
    in_.read(&out.value[0], e.vlen);
  }
  return out;
}

bool SSTable::Get(int64_t key, LSMEntry *out) {
  if (!bloom_.Maybe(key)) return false;  // Bloom filter says definitely absent
  // Binary search the sorted key index.
  size_t lo = 0, hi = index_.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (index_[mid].key < key) lo = mid + 1;
    else hi = mid;
  }
  if (lo < index_.size() && index_[lo].key == key) {
    *out = ReadAt(lo);
    return true;
  }
  return false;  // Bloom filter false positive
}

}  // namespace minidb
