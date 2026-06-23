#pragma once

#include "recovery/log_record.h"

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace minidb {

using namespace std;

class LogManager {
public:
  explicit LogManager(const string& filename);
  ~LogManager();

  int64_t Append(const LogRecord& rec);
  vector<LogRecord> ReadAll();
  void Truncate();
  int64_t GetLSN() const;

private:
  string filename_;
  mutable mutex mu_;
  int64_t lsn_ = 0;
};

} // namespace minidb