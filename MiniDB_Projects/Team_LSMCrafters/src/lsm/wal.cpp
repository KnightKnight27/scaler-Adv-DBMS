#include "lsm/wal.h"

namespace minidb {
namespace {
template <typename T>
void put(std::ostream& os, const T& v) { os.write(reinterpret_cast<const char*>(&v), sizeof(T)); }
template <typename T>
bool get(std::istream& is, T& v) { return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T))); }
}  // namespace

LsmWal::LsmWal(std::string path, bool truncate) : path_(std::move(path)) {
  auto mode = std::ios::binary | std::ios::out | (truncate ? std::ios::trunc : std::ios::app);
  out_.open(path_, mode);
}

void LsmWal::append_put(Key key, const Bytes& value, SeqNo seq, bool sync) {
  uint8_t  type = static_cast<uint8_t>(RecType::Put);
  uint32_t vlen = static_cast<uint32_t>(value.size());
  put(out_, type); put(out_, key); put(out_, seq); put(out_, vlen);
  out_.write(value.data(), vlen);
  if (sync) out_.flush();
}

void LsmWal::append_del(Key key, SeqNo seq, bool sync) {
  uint8_t  type = static_cast<uint8_t>(RecType::Tombstone);
  uint32_t vlen = 0;
  put(out_, type); put(out_, key); put(out_, seq); put(out_, vlen);
  if (sync) out_.flush();
}

void LsmWal::reset() {
  out_.close();
  out_.open(path_, std::ios::binary | std::ios::out | std::ios::trunc);
}

SeqNo LsmWal::replay(const std::string& path, MemTable& into) {
  std::ifstream in(path, std::ios::binary);
  SeqNo max_seq = 0;
  uint8_t type;
  while (get(in, type)) {
    Key key; SeqNo seq; uint32_t vlen;
    if (!get(in, key) || !get(in, seq) || !get(in, vlen)) break;
    Bytes value(vlen, '\0');
    if (vlen && !in.read(value.data(), vlen)) break;
    if (static_cast<RecType>(type) == RecType::Put) into.put(key, std::move(value), seq);
    else                                            into.del(key, seq);
    if (seq > max_seq) max_seq = seq;
  }
  return max_seq;
}

}  // namespace minidb
