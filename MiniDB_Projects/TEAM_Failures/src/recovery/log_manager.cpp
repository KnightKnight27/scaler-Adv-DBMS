#include "recovery/log_manager.h"

namespace minidb {

LogManager::LogManager(const string &wal_file) : wal_file_(wal_file) {
  // First find the highest LSN already on disk so new records continue past it.
  auto existing = readAll();
  for (auto &r : existing) next_lsn_ = max(next_lsn_, r.lsn + 1);
  // Open for append (we never overwrite old log records).
  out_.open(wal_file_, ios::out | ios::app | ios::binary);
}

LogManager::~LogManager() {
  flush();
  if (out_.is_open()) out_.close();
}

lsn_t LogManager::append(LogRecord rec) {
  lock_guard<mutex> g(latch_);
  rec.lsn = next_lsn_++;
  buffer_ += rec.serialize();
  return rec.lsn;
}

void LogManager::flush() {
  lock_guard<mutex> g(latch_);
  if (buffer_.empty()) return;
  out_.write(buffer_.data(), buffer_.size());
  out_.flush();        // force the OS to persist it
  buffer_.clear();
}

vector<LogRecord> LogManager::readAll() {
  vector<LogRecord> recs;
  ifstream in(wal_file_, ios::in | ios::binary);
  if (!in.is_open()) return recs;
  string data((istreambuf_iterator<char>(in)),
                   istreambuf_iterator<char>());
  size_t off = 0;
  LogRecord r;
  while (LogRecord::deserialize(data, &off, &r)) recs.push_back(r);
  return recs;   // a torn final record is simply ignored
}

}  // namespace minidb
