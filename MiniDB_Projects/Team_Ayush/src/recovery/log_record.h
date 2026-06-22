#pragma once
#include <cstdint>

namespace minidb {

// Write-ahead log record types. The log is physiological: UPDATE records carry
// both a before-image (for undo) and an after-image (for redo) of a single
// integer "account" cell, which keeps recovery simple to demonstrate.
enum class LogType : int8_t { BEGIN = 1, UPDATE = 2, COMMIT = 3, ABORT = 4 };

struct LogRecord {
  LogType type;
  int32_t txn;
  int32_t idx;     // affected cell (UPDATE only)
  int32_t before;  // before-image (UPDATE only)
  int32_t after;   // after-image  (UPDATE only)
};

}  // namespace minidb
