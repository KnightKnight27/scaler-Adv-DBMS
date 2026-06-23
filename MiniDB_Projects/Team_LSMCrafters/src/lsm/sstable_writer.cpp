#include "lsm/sstable_writer.h"
#include <cstdio>

namespace minidb {
namespace {
template <typename T>
void put(std::ostream& os, const T& v) { os.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
}  // namespace

SSTableWriter::SSTableWriter(std::string path)
    : path_(std::move(path)), tmp_path_(path_ + ".tmp") {
  out_.open(tmp_path_, std::ios::binary | std::ios::trunc);
  uint32_t magic   = kSstMagic;
  uint32_t version = 1;
  put(out_, magic);
  put(out_, version);  // header = 8 bytes; data records start at offset 8
}

void SSTableWriter::add(Key key, const ValueEntry& entry) {
  uint64_t offset = static_cast<uint64_t>(out_.tellp());
  if (count_ % kIndexStride == 0) sparse_.emplace_back(key, offset);

  uint8_t  type = static_cast<uint8_t>(entry.type);
  uint32_t vlen = static_cast<uint32_t>(entry.value.size());
  put(out_, key);
  put(out_, entry.seq);
  put(out_, type);
  put(out_, vlen);
  out_.write(entry.value.data(), vlen);

  if (count_ == 0) { min_key_ = max_key_ = key; }
  else             { max_key_ = key; }  // ascending, so the last key is the max
  if (entry.seq > max_seq_) max_seq_ = entry.seq;
  ++count_;
}

SSTableMeta SSTableWriter::finish() {
  uint64_t index_offset = static_cast<uint64_t>(out_.tellp());
  for (const auto& [key, offset] : sparse_) { put(out_, key); put(out_, offset); }

  uint64_t index_count = sparse_.size();
  put(out_, index_offset);
  put(out_, index_count);
  put(out_, count_);
  put(out_, min_key_);
  put(out_, max_key_);
  put(out_, max_seq_);
  uint32_t magic = kSstMagic;
  put(out_, magic);

  uint64_t file_bytes = static_cast<uint64_t>(out_.tellp());
  out_.close();
  std::rename(tmp_path_.c_str(), path_.c_str());  // atomic publish

  return SSTableMeta{path_, min_key_, max_key_, max_seq_, count_, file_bytes};
}

}  // namespace minidb
