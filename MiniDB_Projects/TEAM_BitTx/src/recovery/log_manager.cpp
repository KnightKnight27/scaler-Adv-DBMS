#include "recovery/log_manager.h"

#include <cstdio>
#include <cstring>

namespace minidb {

using namespace std;

LogManager::LogManager(const string& filename) : filename_(filename) {
  ifstream in(filename_, ios::binary | ios::ate);
  if (in) {
    size_t sz = static_cast<size_t>(in.tellg());
    lsn_ = sz;
  }
}

LogManager::~LogManager() = default;

int64_t LogManager::Append(const LogRecord& rec) {
  lock_guard<mutex> lock(mu_);
  string s = SerializeLog(rec);
  ofstream out(filename_, ios::binary | ios::app);
  out.write(s.data(), s.size());
  out.flush();
  int64_t ret = lsn_;
  lsn_ += s.size();
  return ret;
}

vector<LogRecord> LogManager::ReadAll() {
  lock_guard<mutex> lock(mu_);
  vector<LogRecord> result;
  ifstream in(filename_, ios::binary);
  if (!in)
    return result;
  in.seekg(0, ios::end);
  size_t total = static_cast<size_t>(in.tellg());
  in.seekg(0, ios::beg);
  string buf(total, '\0');
  if (total > 0)
    in.read(buf.data(), total);
  size_t pos = 0;
  while (pos + 4 <= buf.size()) {
    int32_t len;
    memcpy(&len, buf.data() + pos, sizeof(int32_t));
    pos += sizeof(int32_t);
    if (len < 0 || pos + static_cast<size_t>(len) > buf.size())
      break;
    string rec(buf, pos, len);
    result.push_back(DeserializeLogBody(rec));
    pos += len;
  }
  return result;
}

void LogManager::Truncate() {
  lock_guard<mutex> lock(mu_);
  ofstream out(filename_, ios::binary | ios::trunc);
  lsn_ = 0;
}

int64_t LogManager::GetLSN() const {
  lock_guard<mutex> lock(mu_);
  return lsn_;
}

} // namespace minidb