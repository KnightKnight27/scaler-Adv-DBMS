#include "lsm/sstable.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "common/serialize.h"

namespace walterdb {

namespace {
constexpr uint64_t kMagic = 0x5353544231ULL;  // "SSTB1"
constexpr size_t kFooterSize = 5 * 8;          // index_off, bloom_off, minmax_off, count, magic
}  // namespace

void SSTable::write(const std::string& path, const std::vector<SSTEntry>& sorted) {
  ByteWriter data;
  ByteWriter index;
  std::vector<std::pair<std::string, uint64_t>> sparse;
  BloomFilter bloom(sorted.size());

  for (size_t i = 0; i < sorted.size(); ++i) {
    const SSTEntry& e = sorted[i];
    uint64_t off = data.size();
    if (i % SPARSE_INTERVAL == 0) sparse.push_back({e.key, off});
    bloom.add(e.key);
    data.put_string(e.key);
    data.put_u8(e.tombstone ? 1 : 0);
    data.put_string(e.value);
  }

  index.put_u32(static_cast<uint32_t>(sparse.size()));
  for (auto& [k, off] : sparse) {
    index.put_string(k);
    index.put_u64(off);
  }

  std::string bloom_bytes = bloom.serialize();
  ByteWriter bloom_w;
  bloom_w.put_string(bloom_bytes);

  ByteWriter minmax;
  minmax.put_string(sorted.empty() ? std::string_view{} : std::string_view(sorted.front().key));
  minmax.put_string(sorted.empty() ? std::string_view{} : std::string_view(sorted.back().key));

  uint64_t index_off = data.size();
  uint64_t bloom_off = index_off + index.size();
  uint64_t minmax_off = bloom_off + bloom_w.size();

  ByteWriter footer;
  footer.put_u64(index_off);
  footer.put_u64(bloom_off);
  footer.put_u64(minmax_off);
  footer.put_u64(sorted.size());
  footer.put_u64(kMagic);

  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) throw std::runtime_error("SSTable::write open: " + std::string(std::strerror(errno)));
  auto write_all = [&](const std::string& s) {
    if (::write(fd, s.data(), s.size()) != static_cast<ssize_t>(s.size()))
      throw std::runtime_error("SSTable::write short write");
  };
  write_all(data.str());
  write_all(index.str());
  write_all(bloom_w.str());
  write_all(minmax.str());
  write_all(footer.str());
  ::fsync(fd);
  ::close(fd);
}

SSTable::SSTable(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ < 0) throw std::runtime_error("SSTable open: " + std::string(std::strerror(errno)));
  off_t sz = ::lseek(fd_, 0, SEEK_END);
  if (sz < static_cast<off_t>(kFooterSize)) throw std::runtime_error("SSTable: file too small");
  file_size_ = static_cast<uint64_t>(sz);

  std::string footer = read_range(file_size_ - kFooterSize, kFooterSize);
  ByteReader fr(footer);
  uint64_t index_off = fr.get_u64();
  uint64_t bloom_off = fr.get_u64();
  uint64_t minmax_off = fr.get_u64();
  num_entries_ = fr.get_u64();
  if (fr.get_u64() != kMagic) throw std::runtime_error("SSTable: bad magic");
  data_end_ = index_off;

  // Sparse index.
  std::string idx = read_range(index_off, bloom_off - index_off);
  ByteReader ir(idx);
  uint32_t count = ir.get_u32();
  sparse_.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::string k(ir.get_string());
    uint64_t off = ir.get_u64();
    sparse_.push_back({std::move(k), off});
  }

  // Bloom filter.
  std::string bloom_raw = read_range(bloom_off, minmax_off - bloom_off);
  ByteReader br(bloom_raw);
  bloom_ = std::make_unique<BloomFilter>(BloomFilter::deserialize(br.get_string()));

  // Min / max keys.
  std::string mm = read_range(minmax_off, (file_size_ - kFooterSize) - minmax_off);
  ByteReader mr(mm);
  min_key_ = std::string(mr.get_string());
  max_key_ = std::string(mr.get_string());
}

SSTable::~SSTable() {
  if (fd_ >= 0) ::close(fd_);
}

std::string SSTable::read_range(uint64_t off, uint64_t len) const {
  std::string buf(len, '\0');
  if (len == 0) return buf;
  if (::pread(fd_, buf.data(), len, static_cast<off_t>(off)) != static_cast<ssize_t>(len))
    throw std::runtime_error("SSTable: short pread");
  return buf;
}

SSTable::Lookup SSTable::get(std::string_view key, std::string* out) const {
  if (num_entries_ == 0) return Lookup::Absent;
  if (key < std::string_view(min_key_) || key > std::string_view(max_key_)) return Lookup::Absent;
  if (!bloom_->maybe_contains(key)) return Lookup::Absent;

  // Find the bounded run to scan: [start, end) between consecutive sparse keys.
  // upper_bound on sparse keys gives the first sparse key strictly greater.
  size_t j = std::upper_bound(sparse_.begin(), sparse_.end(), key,
                              [](std::string_view k, const std::pair<std::string, uint64_t>& e) {
                                return k < std::string_view(e.first);
                              }) -
             sparse_.begin();
  uint64_t start = (j == 0) ? 0 : sparse_[j - 1].second;
  uint64_t end = (j < sparse_.size()) ? sparse_[j].second : data_end_;

  std::string buf = read_range(start, end - start);
  size_t pos = 0;
  while (pos < buf.size()) {
    uint32_t klen = load_u32(buf.data() + pos); pos += 4;
    std::string_view k(buf.data() + pos, klen); pos += klen;
    uint8_t flag = static_cast<uint8_t>(buf[pos]); pos += 1;
    uint32_t vlen = load_u32(buf.data() + pos); pos += 4;
    std::string_view v(buf.data() + pos, vlen); pos += vlen;
    if (k == key) {
      if (flag) return Lookup::Tombstone;
      if (out) out->assign(v.begin(), v.end());
      return Lookup::Found;
    }
    if (k > key) break;  // sorted: passed where the key would be
  }
  return Lookup::Absent;
}

void SSTable::collect_range(std::string_view lo, std::string_view hi,
                            std::vector<SSTEntry>& out) const {
  if (num_entries_ == 0) return;
  if (!hi.empty() && hi <= std::string_view(min_key_)) return;  // range entirely below this table
  if (!lo.empty() && lo > std::string_view(max_key_)) return;   // range entirely above

  auto key_less = [](const std::pair<std::string, uint64_t>& e, std::string_view k) {
    return std::string_view(e.first) < k;
  };

  // Start at the sparse block that could contain `lo`.
  uint64_t start = 0;
  if (!lo.empty()) {
    size_t j = std::lower_bound(sparse_.begin(), sparse_.end(), lo, key_less) - sparse_.begin();
    // lower_bound gives the first sparse key >= lo; back up one block to catch
    // keys equal to or just below an indexed boundary.
    if (j > 0 && (j == sparse_.size() || std::string_view(sparse_[j].first) != lo)) --j;
    start = sparse_[j].second;
  }
  // End at the first sparse block whose key is >= hi.
  uint64_t end = data_end_;
  if (!hi.empty()) {
    size_t j = std::lower_bound(sparse_.begin(), sparse_.end(), hi, key_less) - sparse_.begin();
    if (j < sparse_.size()) end = sparse_[j].second;
  }

  std::string buf = read_range(start, end - start);
  size_t pos = 0;
  while (pos < buf.size()) {
    uint32_t klen = load_u32(buf.data() + pos); pos += 4;
    std::string_view k(buf.data() + pos, klen); pos += klen;
    uint8_t flag = static_cast<uint8_t>(buf[pos]); pos += 1;
    uint32_t vlen = load_u32(buf.data() + pos); pos += 4;
    std::string_view v(buf.data() + pos, vlen); pos += vlen;
    if (!lo.empty() && k < lo) continue;
    if (!hi.empty() && k >= hi) break;
    out.push_back({std::string(k), std::string(v), flag != 0});
  }
}

SSTable::Iterator SSTable::iterator() const {
  return Iterator(read_range(0, data_end_));
}

SSTable::Iterator::Iterator(std::string data) : data_(std::move(data)) { parse(); }

void SSTable::Iterator::parse() {
  if (pos_ >= data_.size()) { valid_ = false; return; }
  uint32_t klen = load_u32(data_.data() + pos_); pos_ += 4;
  key_ = std::string_view(data_.data() + pos_, klen); pos_ += klen;
  tombstone_ = data_[pos_] != 0; pos_ += 1;
  uint32_t vlen = load_u32(data_.data() + pos_); pos_ += 4;
  value_ = std::string_view(data_.data() + pos_, vlen); pos_ += vlen;
  valid_ = true;
}

}  // namespace walterdb
