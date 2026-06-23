#include "wal.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <set>
#include <unordered_set>

namespace minidb {

// little-endian encode/decode helpers
namespace {
void put_u16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(v & 0xFF);
  b.push_back((v >> 8) & 0xFF);
}
void put_i32(std::vector<uint8_t>& b, int32_t v) {
  for (int i = 0; i < 4; i++) b.push_back((v >> (i * 8)) & 0xFF);
}
void put_i64(std::vector<uint8_t>& b, int64_t v) {
  for (int i = 0; i < 8; i++) b.push_back((v >> (i * 8)) & 0xFF);
}
uint16_t get_u16(const uint8_t* p, size_t& o) {
  uint16_t v = p[o] | (p[o + 1] << 8);
  o += 2;
  return v;
}
int32_t get_i32(const uint8_t* p, size_t& o) {
  int32_t v = 0;
  for (int i = 0; i < 4; i++) v |= (static_cast<int32_t>(p[o + i]) << (i * 8));
  o += 4;
  return v;
}
int64_t get_i64(const uint8_t* p, size_t& o) {
  int64_t v = 0;
  for (int i = 0; i < 8; i++) v |= (static_cast<int64_t>(p[o + i]) << (i * 8));
  o += 8;
  return v;
}
}  // namespace

std::vector<uint8_t> LogManager::encode(const LogRecord& r) {
  std::vector<uint8_t> body;
  body.push_back(static_cast<uint8_t>(r.type));
  put_i64(body, r.lsn);
  put_i64(body, r.txn);
  put_i32(body, r.file_id);
  put_i32(body, r.page_id);
  put_i32(body, r.slot);
  put_u16(body, r.old_off);
  put_u16(body, r.old_len);
  body.insert(body.end(), r.old_bytes.begin(), r.old_bytes.end());
  put_u16(body, r.new_off);
  put_u16(body, r.new_len);
  body.insert(body.end(), r.new_bytes.begin(), r.new_bytes.end());

  std::vector<uint8_t> out;
  put_i32(out, static_cast<int32_t>(body.size()));  // length prefix
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

LogManager::LogManager(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd_ < 0) throw DBException("cannot open log file " + path_);
  // seed LSN counters from the existing log so we never reuse an LSN
  auto recs = read_all();
  for (auto& r : recs) {
    if (r.lsn >= next_lsn_) next_lsn_ = r.lsn + 1;
  }
  persisted_lsn_ = next_lsn_ - 1;
  ::lseek(fd_, 0, SEEK_END);
}

LogManager::~LogManager() {
  flush();
  if (fd_ >= 0) ::close(fd_);
}

lsn_t LogManager::append(LogRecord rec) {
  std::lock_guard<std::mutex> lk(latch_);
  rec.lsn = next_lsn_++;
  auto bytes = encode(rec);
  pending_.insert(pending_.end(), bytes.begin(), bytes.end());
  return rec.lsn;
}

lsn_t LogManager::log_begin(txn_id_t txn) {
  LogRecord r;
  r.type = LogType::BEGIN;
  r.txn = txn;
  return append(std::move(r));
}
lsn_t LogManager::log_commit(txn_id_t txn) {
  LogRecord r;
  r.type = LogType::COMMIT;
  r.txn = txn;
  lsn_t l = append(std::move(r));
  if (sync_on_commit_) flush_to(l);  // durable commit fsyncs before returning
  return l;
}
lsn_t LogManager::log_abort(txn_id_t txn) {
  LogRecord r;
  r.type = LogType::ABORT;
  r.txn = txn;
  return append(std::move(r));
}
lsn_t LogManager::log_insert(txn_id_t txn, file_id_t fid, page_id_t pid, slot_id_t slot,
                             uint16_t off, const std::vector<uint8_t>& bytes) {
  LogRecord r;
  r.type = LogType::INSERT;
  r.txn = txn;
  r.file_id = fid;
  r.page_id = pid;
  r.slot = slot;
  r.old_off = 0;
  r.old_len = 0;
  r.new_off = off;
  r.new_len = static_cast<uint16_t>(bytes.size());
  r.new_bytes = bytes;
  return append(std::move(r));
}
lsn_t LogManager::log_delete(txn_id_t txn, file_id_t fid, page_id_t pid, slot_id_t slot,
                             uint16_t off, const std::vector<uint8_t>& bytes) {
  LogRecord r;
  r.type = LogType::DELETE;
  r.txn = txn;
  r.file_id = fid;
  r.page_id = pid;
  r.slot = slot;
  r.old_off = off;
  r.old_len = static_cast<uint16_t>(bytes.size());
  r.old_bytes = bytes;
  r.new_off = 0;
  r.new_len = 0;
  return append(std::move(r));
}

void LogManager::flush_to(lsn_t lsn) {
  std::lock_guard<std::mutex> lk(latch_);
  if (lsn <= persisted_lsn_ || pending_.empty()) return;
  // write() can do a partial write, so loop until it's all on disk or the
  // framing ends up half-written
  size_t written = 0;
  while (written < pending_.size()) {
    ssize_t n = ::write(fd_, pending_.data() + written, pending_.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      throw DBException("log write failed");
    }
    written += static_cast<size_t>(n);
  }
  ::fsync(fd_);
  pending_.clear();
  persisted_lsn_ = next_lsn_ - 1;
}

void LogManager::flush() { flush_to(next_lsn_); }

std::vector<LogRecord> LogManager::read_all() {
  std::vector<LogRecord> out;
  off_t sz = ::lseek(fd_, 0, SEEK_END);
  if (sz <= 0) return out;
  std::vector<uint8_t> buf(sz);
  ssize_t n = ::pread(fd_, buf.data(), sz, 0);
  if (n <= 0) return out;
  // fixed header is 37 bytes with zero-length payloads
  constexpr int kMinBody = 37;
  size_t o = 0;
  while (o + 4 <= static_cast<size_t>(n)) {
    size_t len_pos = o;
    int32_t rlen = get_i32(buf.data(), len_pos);
    // stop on a torn or corrupt tail
    if (rlen < kMinBody || len_pos + rlen > static_cast<size_t>(n)) break;
    size_t body_end = len_pos + rlen;
    size_t p = len_pos;
    LogRecord r;
    r.type = static_cast<LogType>(buf[p++]);
    r.lsn = get_i64(buf.data(), p);
    r.txn = get_i64(buf.data(), p);
    r.file_id = get_i32(buf.data(), p);
    r.page_id = get_i32(buf.data(), p);
    r.slot = get_i32(buf.data(), p);
    r.old_off = get_u16(buf.data(), p);
    r.old_len = get_u16(buf.data(), p);
    // keep payloads inside this record's body
    if (p + r.old_len + 4 > body_end) break;
    r.old_bytes.assign(buf.begin() + p, buf.begin() + p + r.old_len);
    p += r.old_len;
    r.new_off = get_u16(buf.data(), p);
    r.new_len = get_u16(buf.data(), p);
    if (p + r.new_len > body_end) break;
    r.new_bytes.assign(buf.begin() + p, buf.begin() + p + r.new_len);
    p += r.new_len;
    out.push_back(std::move(r));
    o = body_end;
  }
  ::lseek(fd_, 0, SEEK_END);
  return out;
}

size_t RecoveryManager::recover() {
  auto records = log_->read_all();
  if (records.empty()) return 0;

  // analysis: which txns committed
  std::unordered_set<txn_id_t> committed;
  for (auto& r : records)
    if (r.type == LogType::COMMIT) committed.insert(r.txn);

  auto is_heap = [](LogType t) { return t == LogType::INSERT || t == LogType::DELETE; };

  // redo: repeat history, reapply changes not yet on the page (page_lsn < lsn)
  for (auto& r : records) {
    if (!is_heap(r.type)) continue;
    Page* pg = bpool_->fetch_page(r.file_id, r.page_id);
    char* d = pg->data();
    if (slotted::get_lsn(d) < r.lsn) {
      slotted::apply_slot(d, r.slot, r.new_off, r.new_len,
                          r.new_len ? r.new_bytes.data() : nullptr);
      slotted::set_lsn(d, r.lsn);
      bpool_->unpin_page(r.file_id, r.page_id, true);
    } else {
      bpool_->unpin_page(r.file_id, r.page_id, false);
    }
  }

  // undo losers: walk backward reverting txns that never committed
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    LogRecord& r = *it;
    if (!is_heap(r.type)) continue;
    if (committed.count(r.txn)) continue;
    Page* pg = bpool_->fetch_page(r.file_id, r.page_id);
    char* d = pg->data();
    slotted::apply_slot(d, r.slot, r.old_off, r.old_len,
                        r.old_len ? r.old_bytes.data() : nullptr);
    slotted::set_lsn(d, r.lsn);
    bpool_->unpin_page(r.file_id, r.page_id, true);
  }

  bpool_->flush_all();
  return records.size();
}

}  // namespace minidb
