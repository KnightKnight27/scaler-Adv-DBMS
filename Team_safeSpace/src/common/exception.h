#pragma once

#include <stdexcept>
#include <string>

namespace minidb {

// Single exception type for all engine-level errors. A `kind` lets callers
// distinguish e.g. a parse error from a transaction abort without a class
// hierarchy. The REPL prints `what()`; the executor catches AbortException to
// roll a transaction back.
enum class ErrorKind { kIO, kParse, kBinder, kExecution, kTransaction, kAbort, kNotImplemented };

class Exception : public std::runtime_error {
 public:
  Exception(ErrorKind kind, const std::string &msg)
      : std::runtime_error(Prefix(kind) + msg), kind_(kind) {}

  ErrorKind kind() const { return kind_; }

 private:
  static const char *Prefix(ErrorKind k) {
    switch (k) {
      case ErrorKind::kIO: return "[IO] ";
      case ErrorKind::kParse: return "[Parse] ";
      case ErrorKind::kBinder: return "[Binder] ";
      case ErrorKind::kExecution: return "[Execution] ";
      case ErrorKind::kTransaction: return "[Txn] ";
      case ErrorKind::kAbort: return "[Abort] ";
      case ErrorKind::kNotImplemented: return "[NotImplemented] ";
    }
    return "";
  }
  ErrorKind kind_;
};

}  // namespace minidb
