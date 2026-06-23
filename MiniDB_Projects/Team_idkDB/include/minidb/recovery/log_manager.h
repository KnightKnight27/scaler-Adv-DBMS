#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "minidb/common/types.h"

namespace minidb {

struct LogRecord {
  Lsn lsn{};
  TxnId txn_id{};
  LogType type{LogType::Begin};
  std::string table;
  int key{};
  std::string old_value;
  std::string new_value;
};

class LogManager {
 public:
  explicit LogManager(std::filesystem::path path);
  ~LogManager();

  Lsn Append(TxnId txn_id, LogType type, std::string table = {},
             int key = 0, std::string old_value = {},
             std::string new_value = {});
  void Flush();
  std::vector<LogRecord> ReadAll() const;

 private:
  static std::string TypeName(LogType type);
  static LogType ParseType(const std::string &name);

  std::filesystem::path path_;
  mutable std::mutex mutex_;
  std::ofstream output_;
  Lsn next_lsn_{1};
};

}  // namespace minidb
