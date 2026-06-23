#include "minidb/storage/write_ahead_log.hpp"

#include <stdexcept>

namespace minidb {

namespace {
constexpr uint32_t kMagic = 0x4D494E49;  // 'MINI'

void put_u16(std::ostream& os, uint16_t v) {
  os.put(static_cast<char>(v & 0xFF));
  os.put(static_cast<char>((v >> 8) & 0xFF));
}
void put_u32(std::ostream& os, uint32_t v) {
  for (int i = 0; i < 4; ++i) os.put(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void put_u64(std::ostream& os, uint64_t v) {
  for (int i = 0; i < 8; ++i) os.put(static_cast<char>((v >> (8 * i)) & 0xFF));
}
uint16_t read_u16(std::istream& is) {
  uint16_t lo = static_cast<unsigned char>(is.get());
  uint16_t hi = static_cast<unsigned char>(is.get());
  return static_cast<uint16_t>(lo | (hi << 8));
}
uint32_t read_u32(std::istream& is) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i)
    v |= static_cast<uint32_t>(static_cast<unsigned char>(is.get())) << (8 * i);
  return v;
}
uint64_t read_u64(std::istream& is) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i)
    v |= static_cast<uint64_t>(static_cast<unsigned char>(is.get())) << (8 * i);
  return v;
}
}  // namespace

WriteAheadLog::WriteAheadLog(const std::string& path) : path_(path) {
  out_.open(path_, std::ios::binary | std::ios::app);
  if (!out_) throw std::runtime_error("WAL: cannot open " + path_);
  // Continue LSNs past whatever is already on disk.
  std::vector<WalRecord> existing = read_all();
  for (const auto& r : existing)
    if (r.lsn >= next_lsn_) next_lsn_ = r.lsn + 1;
}

WriteAheadLog::~WriteAheadLog() {
  if (out_.is_open()) out_.flush();
}

uint64_t WriteAheadLog::append(const WalRecord& rec) {
  if (!out_) throw std::runtime_error("WAL: stream not open");
  uint64_t lsn = next_lsn_++;
  put_u32(out_, kMagic);
  put_u64(out_, lsn);
  put_u64(out_, rec.txn);
  out_.put(static_cast<char>(static_cast<uint8_t>(rec.type)));
  put_u32(out_, rec.table);
  put_u32(out_, rec.rid.page_id);
  put_u16(out_, rec.rid.slot);
  put_u32(out_, static_cast<uint32_t>(rec.payload.size()));
  if (!rec.payload.empty())
    out_.write(reinterpret_cast<const char*>(rec.payload.data()),
               static_cast<std::streamsize>(rec.payload.size()));
  if (!out_) throw std::runtime_error("WAL: write failed");
  return lsn;
}

void WriteAheadLog::flush() {
  if (out_) out_.flush();
}

std::vector<WalRecord> WriteAheadLog::read_all() {
  std::vector<WalRecord> out;
  std::ifstream in(path_, std::ios::binary);
  if (!in) return out;
  while (in.peek() != EOF) {
    uint32_t magic = read_u32(in);
    if (!in || magic != kMagic) break;
    WalRecord rec;
    rec.lsn = read_u64(in);
    rec.txn = read_u64(in);
    rec.type = static_cast<WalType>(static_cast<uint8_t>(in.get()));
    rec.table = read_u32(in);
    rec.rid.page_id = read_u32(in);
    rec.rid.slot = read_u16(in);
    uint32_t len = read_u32(in);
    rec.payload.resize(len);
    if (len > 0) in.read(reinterpret_cast<char*>(rec.payload.data()), len);
    if (!in) break;
    out.push_back(std::move(rec));
  }
  return out;
}

void WriteAheadLog::truncate() {
  out_.close();
  std::ofstream trunc(path_, std::ios::binary | std::ios::trunc);
  trunc.close();
  out_.open(path_, std::ios::binary | std::ios::app);
  next_lsn_ = 1;
}

}  // namespace minidb
