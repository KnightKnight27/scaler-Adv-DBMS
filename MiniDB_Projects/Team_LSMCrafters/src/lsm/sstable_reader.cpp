#include "lsm/sstable_reader.h"
#include <algorithm>
#include <stdexcept>

namespace minidb {
namespace {

template <typename T>
bool get_pod(std::istream& is, T& v) {
  return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

// Reads one data record at the stream's current position.
bool read_record(std::istream& in, Key& key, ValueEntry& entry) {
  uint8_t  type;
  uint32_t vlen;
  if (!get_pod(in, key) || !get_pod(in, entry.seq) || !get_pod(in, type) || !get_pod(in, vlen))
    return false;
  entry.type = static_cast<RecType>(type);
  entry.value.resize(vlen);
  return vlen == 0 || static_cast<bool>(in.read(entry.value.data(), vlen));
}

}  // namespace

SSTableReader::SSTableReader(std::string path) {
  in_.open(path, std::ios::binary);
  if (!in_.is_open()) throw std::runtime_error("SSTableReader: cannot open " + path);

  in_.seekg(0, std::ios::end);
  uint64_t file_bytes = static_cast<uint64_t>(in_.tellg());
  in_.seekg(file_bytes - kSstFooterBytes, std::ios::beg);

  uint64_t index_count;
  uint32_t magic;
  get_pod(in_, index_offset_);
  get_pod(in_, index_count);
  get_pod(in_, meta_.count);
  get_pod(in_, meta_.min_key);
  get_pod(in_, meta_.max_key);
  get_pod(in_, meta_.max_seq);
  get_pod(in_, magic);
  if (magic != kSstMagic) throw std::runtime_error("SSTableReader: bad footer magic in " + path);

  meta_.path       = std::move(path);
  meta_.file_bytes = file_bytes;

  in_.seekg(index_offset_, std::ios::beg);
  sparse_.reserve(index_count);
  for (uint64_t i = 0; i < index_count; ++i) {
    Key k;
    uint64_t off;
    get_pod(in_, k);
    get_pod(in_, off);
    sparse_.emplace_back(k, off);
  }
}

std::optional<ValueEntry> SSTableReader::get(Key key) const {
  if (key < meta_.min_key || key > meta_.max_key) return std::nullopt;

  // Largest sparse key <= key gives the starting offset; default to data start.
  uint64_t offset = 8;  // after the 8-byte header
  auto it = std::upper_bound(sparse_.begin(), sparse_.end(), key,
                             [](Key k, const std::pair<Key, uint64_t>& e) { return k < e.first; });
  if (it != sparse_.begin()) offset = std::prev(it)->second;

  in_.clear();
  in_.seekg(offset, std::ios::beg);
  Key k;
  ValueEntry entry;
  while (static_cast<uint64_t>(in_.tellg()) < index_offset_ && read_record(in_, k, entry)) {
    if (k == key) return entry;
    if (k > key) break;
  }
  return std::nullopt;
}

SSTableReader::Iter::Iter(const std::string& path, uint64_t data_end)
    : in_(path, std::ios::binary), data_end_(data_end) {
  in_.seekg(8, std::ios::beg);  // skip the header
}

bool SSTableReader::Iter::next(Key& key, ValueEntry& entry) {
  if (static_cast<uint64_t>(in_.tellg()) >= data_end_) return false;
  return read_record(in_, key, entry);
}

}  // namespace minidb
