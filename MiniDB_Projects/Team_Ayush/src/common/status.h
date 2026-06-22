#pragma once
#include <string>

namespace minidb {

// A lightweight result type used instead of exceptions across module
// boundaries. Cheap to return by value; carries an error message when not ok.
class Status {
 public:
  Status() : ok_(true) {}

  bool ok() const { return ok_; }
  const std::string& message() const { return msg_; }

  static Status OK() { return Status(); }
  static Status Error(const std::string& m) {
    Status s; s.ok_ = false; s.msg_ = m; return s;
  }

 private:
  bool ok_;
  std::string msg_;
};

}  // namespace minidb
