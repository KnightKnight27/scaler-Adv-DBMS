#include "recovery/wal_manager.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "common/serialize.h"

namespace walterdb {

WalManager::WalManager(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
  if (fd_ < 0) {
    throw std::runtime_error("WalManager: cannot open '" + path_ + "': " + std::strerror(errno));
  }
  // Resume the LSN counter past whatever is already on disk.
  for (const LogRecord& r : read_all()) next_lsn_ = r.lsn + 1;
}

WalManager::~WalManager() {
  if (fd_ >= 0) ::close(fd_);
}

lsn_t WalManager::append(LogType type, txn_id_t txn, uint32_t table_id, std::string_view row) {
  std::lock_guard<std::mutex> guard(latch_);
  lsn_t lsn = next_lsn_++;

  ByteWriter body;
  body.put_u64(static_cast<uint64_t>(lsn));
  body.put_u8(static_cast<uint8_t>(type));
  body.put_i64(txn);
  body.put_u32(table_id);
  body.put_string(row);

  // Frame each record with a length prefix so the reader can detect and drop a
  // torn trailing record left by a crash mid-write.
  ByteWriter framed;
  framed.put_u32(static_cast<uint32_t>(body.size()));
  framed.put_bytes(body.str());

  // Append atomically w.r.t. the on-disk log: if write() makes only partial
  // progress (ENOSPC / EINTR), truncate the partial prefix back off so the log
  // stays a clean sequence of whole frames.  Otherwise that orphan prefix would
  // become an INTERIOR torn record once the next append succeeds, and read_all
  // only tolerates a torn record at the very tail -> framing corruption.
  const std::string& bytes = framed.str();
  off_t start = ::lseek(fd_, 0, SEEK_END);  // O_APPEND target offset
  ssize_t n = ::write(fd_, bytes.data(), bytes.size());
  if (n != static_cast<ssize_t>(bytes.size())) {
    if (start >= 0) ::ftruncate(fd_, start);
    throw std::runtime_error("WalManager: short write to log");
  }
  return lsn;
}

lsn_t WalManager::log_begin(txn_id_t txn) { return append(LogType::Begin, txn, 0, {}); }
lsn_t WalManager::log_insert(txn_id_t txn, uint32_t table_id, std::string_view row) {
  return append(LogType::Insert, txn, table_id, row);
}
lsn_t WalManager::log_delete(txn_id_t txn, uint32_t table_id, std::string_view row) {
  return append(LogType::Delete, txn, table_id, row);
}
lsn_t WalManager::log_commit(txn_id_t txn) { return append(LogType::Commit, txn, 0, {}); }
lsn_t WalManager::log_abort(txn_id_t txn) { return append(LogType::Abort, txn, 0, {}); }

void WalManager::sync() {
  std::lock_guard<std::mutex> guard(latch_);
  if (fd_ >= 0) ::fsync(fd_);
}

std::vector<LogRecord> WalManager::read_all() {
  std::vector<LogRecord> out;

  off_t size = ::lseek(fd_, 0, SEEK_END);
  if (size <= 0) return out;
  std::string buf(static_cast<size_t>(size), '\0');
  if (::pread(fd_, buf.data(), buf.size(), 0) != static_cast<ssize_t>(buf.size())) return out;

  size_t pos = 0;
  while (pos + 4 <= buf.size()) {
    uint32_t len = load_u32(buf.data() + pos);
    if (pos + 4 + len > buf.size()) break;  // torn trailing record -> stop
    ByteReader r(std::string_view(buf.data() + pos + 4, len));
    LogRecord rec;
    rec.lsn = static_cast<lsn_t>(r.get_u64());
    rec.type = static_cast<LogType>(r.get_u8());
    rec.txn = r.get_i64();
    rec.table_id = r.get_u32();
    rec.row_image = std::string(r.get_string());
    out.push_back(std::move(rec));
    pos += 4 + len;
  }
  return out;
}

void WalManager::truncate() {
  if (::ftruncate(fd_, 0) != 0) {
    throw std::runtime_error("WalManager: ftruncate failed: " + std::string(std::strerror(errno)));
  }
  next_lsn_ = 0;
}

}  // namespace walterdb
