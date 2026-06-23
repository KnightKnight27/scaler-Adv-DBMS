#include "minidb/recovery/log_manager.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {

LogManager::LogManager(std::filesystem::path path) : path_(std::move(path)) {
  if (path_.has_parent_path()) std::filesystem::create_directories(path_.parent_path());
  for (const auto &record : ReadAll()) {
    next_lsn_ = std::max(next_lsn_, record.lsn + 1);
  }
  output_.open(path_, std::ios::app);
  if (!output_) throw std::runtime_error("cannot open WAL: " + path_.string());
}

LogManager::~LogManager() { Flush(); }

std::string LogManager::TypeName(LogType type) {
  switch (type) {
    case LogType::Begin: return "BEGIN";
    case LogType::Insert: return "INSERT";
    case LogType::Delete: return "DELETE";
    case LogType::Commit: return "COMMIT";
    case LogType::Abort: return "ABORT";
  }
  throw std::runtime_error("unknown log type");
}

LogType LogManager::ParseType(const std::string &name) {
  if (name == "BEGIN") return LogType::Begin;
  if (name == "INSERT") return LogType::Insert;
  if (name == "DELETE") return LogType::Delete;
  if (name == "COMMIT") return LogType::Commit;
  if (name == "ABORT") return LogType::Abort;
  throw std::runtime_error("invalid WAL record type");
}

Lsn LogManager::Append(TxnId txn_id, LogType type, std::string table, int key,
                       std::string old_value, std::string new_value) {
  std::scoped_lock lock(mutex_);
  const Lsn lsn = next_lsn_++;
  output_ << lsn << ' ' << txn_id << ' ' << TypeName(type) << ' '
          << std::quoted(table) << ' ' << key << ' ' << std::quoted(old_value)
          << ' ' << std::quoted(new_value) << '\n';
  if (!output_) throw std::runtime_error("WAL append failed");
  Trace::Log("WAL", "LSN " + std::to_string(lsn) + " " + TypeName(type) +
                        " T" + std::to_string(txn_id));
  return lsn;
}

void LogManager::Flush() {
  std::scoped_lock lock(mutex_);
  if (output_.is_open()) output_.flush();
}

std::vector<LogRecord> LogManager::ReadAll() const {
  std::scoped_lock lock(mutex_);
  std::ifstream input(path_);
  std::vector<LogRecord> records;
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream row(line);
    LogRecord record;
    std::string type;
    if (row >> record.lsn >> record.txn_id >> type >> std::quoted(record.table) >>
            record.key >> std::quoted(record.old_value)) {
      if (!(row >> std::quoted(record.new_value))) {
        // Backward compatibility with the original 6-field WAL:
        // INSERT stored the new value; DELETE stored the old value.
        record.new_value.clear();
        if (type == "INSERT") {
          record.new_value = record.old_value;
          record.old_value.clear();
        }
      }
      record.type = ParseType(type);
      records.push_back(std::move(record));
    }
    // An incomplete final record is ignored.
  }
  return records;
}

}  // namespace minidb
