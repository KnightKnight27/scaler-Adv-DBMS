#include "lsm/sstable.h"
#include "common/types.h"
#include <cstdio>
#include <cstring>

namespace minidb {

size_t SSTable::Write(const std::string& path,
                      const std::vector<std::pair<int64_t, LsmValue>>& sorted) {
  std::FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) throw DBError("SSTable::Write: cannot open " + path);
  size_t bytes = 0;
  for (auto& [key, val] : sorted) {
    uint8_t tomb = val.tombstone ? 1 : 0;
    int32_t vlen = static_cast<int32_t>(val.data.size());
    std::fwrite(&key, 8, 1, fp);
    std::fwrite(&tomb, 1, 1, fp);
    std::fwrite(&vlen, 4, 1, fp);
    if (vlen) std::fwrite(val.data.data(), 1, vlen, fp);
    bytes += 13 + vlen;
  }
  std::fflush(fp);
  std::fclose(fp);
  return bytes;
}

SSTable::SSTable(const std::string& path) : path_(path) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) throw DBError("SSTable: cannot open " + path);
  // First pass: count records to size the Bloom filter.
  std::vector<std::pair<int64_t, long>> offsets;
  long off = 0;
  while (true) {
    int64_t key;
    if (std::fread(&key, 8, 1, fp) != 1) break;
    uint8_t tomb; int32_t vlen;
    std::fread(&tomb, 1, 1, fp);
    std::fread(&vlen, 4, 1, fp);
    std::fseek(fp, vlen, SEEK_CUR);
    offsets.emplace_back(key, off);
    off += 13 + vlen;
  }
  file_bytes_ = static_cast<size_t>(off);
  bloom_.Reset(offsets.size());
  for (auto& [key, o] : offsets) {
    index_[key] = o;       // later (== newer within this run) wins for dup keys
    bloom_.Add(key);
  }
  std::fclose(fp);
}

bool SSTable::Get(int64_t key, LsmValue* out) const {
  if (!bloom_.MaybeContains(key)) return false;  // definitely absent
  auto it = index_.find(key);
  if (it == index_.end()) return false;
  std::FILE* fp = std::fopen(path_.c_str(), "rb");
  if (!fp) return false;
  std::fseek(fp, it->second, SEEK_SET);
  int64_t k; uint8_t tomb; int32_t vlen;
  std::fread(&k, 8, 1, fp);
  std::fread(&tomb, 1, 1, fp);
  std::fread(&vlen, 4, 1, fp);
  std::string data(vlen, '\0');
  if (vlen) std::fread(&data[0], 1, vlen, fp);
  std::fclose(fp);
  out->tombstone = tomb != 0;
  out->data = std::move(data);
  return true;
}

std::vector<std::pair<int64_t, LsmValue>> SSTable::ReadAll() const {
  std::vector<std::pair<int64_t, LsmValue>> out;
  std::FILE* fp = std::fopen(path_.c_str(), "rb");
  if (!fp) return out;
  while (true) {
    int64_t key;
    if (std::fread(&key, 8, 1, fp) != 1) break;
    uint8_t tomb; int32_t vlen;
    std::fread(&tomb, 1, 1, fp);
    std::fread(&vlen, 4, 1, fp);
    std::string data(vlen, '\0');
    if (vlen) std::fread(&data[0], 1, vlen, fp);
    out.push_back({key, LsmValue{std::move(data), tomb != 0}});
  }
  std::fclose(fp);
  return out;
}

}  // namespace minidb
