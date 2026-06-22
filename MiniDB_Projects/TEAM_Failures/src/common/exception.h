// ============================================================================
// exception.h  --  One simple exception type used throughout MiniDB.
//
// Rather than inventing a large hierarchy, we use a single DBException carrying
// a human-readable message plus a category.  The REPL catches it and prints the
// message instead of crashing, so a bad SQL statement never takes down the DB.
// ============================================================================
#pragma once

#include <stdexcept>
#include <string>
using namespace std;

namespace minidb {

enum class ErrorType {
  kParse,       // malformed SQL
  kBinder,      // refers to a table/column that does not exist
  kExecution,   // runtime failure while running a query
  kStorage,     // disk / buffer-pool problem
  kTransaction, // aborted txn, deadlock, lock failure
  kInternal     // a bug / unexpected state
};

class DBException : public runtime_error {
 public:
  DBException(ErrorType type, const string &msg)
      : runtime_error(msg), type_(type) {}

  ErrorType type() const { return type_; }

 private:
  ErrorType type_;
};

// Convenience constructors so call sites read nicely:  throw ParseError("...").
inline DBException ParseError(const string &m)   { return {ErrorType::kParse, m}; }
inline DBException BinderError(const string &m)  { return {ErrorType::kBinder, m}; }
inline DBException ExecError(const string &m)    { return {ErrorType::kExecution, m}; }
inline DBException StorageError(const string &m) { return {ErrorType::kStorage, m}; }
inline DBException TxnError(const string &m)     { return {ErrorType::kTransaction, m}; }
inline DBException InternalError(const string &m){ return {ErrorType::kInternal, m}; }

}  // namespace minidb
