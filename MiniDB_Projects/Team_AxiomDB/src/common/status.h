#pragma once

#include <string>
#include <utility>

namespace axiomdb {

// ---------------------------------------------------------------------------
// Status: a lightweight, value-type error channel used for *expected* failures
// (key not found, write conflict, out of space, ...).  We reserve C++
// exceptions for genuinely exceptional / programmer-error situations (corrupt
// on-disk format, broken invariants) so that the common control flow stays
// explicit and allocation-light.
// ---------------------------------------------------------------------------

enum class StatusCode {
  Ok,
  NotFound,
  AlreadyExists,
  IoError,
  Corruption,
  InvalidArgument,
  NotSupported,
  OutOfSpace,
  Conflict,    // lock / write-write conflict
  Aborted,     // transaction aborted (e.g. deadlock victim)
};

const char* status_code_name(StatusCode code);

class Status {
 public:
  Status() = default;  // default-constructs to Ok; `return {};` means success

  static Status error(StatusCode code, std::string message) {
    return Status(code, std::move(message));
  }

  // Convenience constructors for the common cases.
  static Status not_found(std::string m = "") { return {StatusCode::NotFound, std::move(m)}; }
  static Status already_exists(std::string m = "") { return {StatusCode::AlreadyExists, std::move(m)}; }
  static Status io_error(std::string m = "") { return {StatusCode::IoError, std::move(m)}; }
  static Status corruption(std::string m = "") { return {StatusCode::Corruption, std::move(m)}; }
  static Status invalid_argument(std::string m = "") { return {StatusCode::InvalidArgument, std::move(m)}; }
  static Status not_supported(std::string m = "") { return {StatusCode::NotSupported, std::move(m)}; }
  static Status out_of_space(std::string m = "") { return {StatusCode::OutOfSpace, std::move(m)}; }
  static Status conflict(std::string m = "") { return {StatusCode::Conflict, std::move(m)}; }
  static Status aborted(std::string m = "") { return {StatusCode::Aborted, std::move(m)}; }

  bool ok() const { return code_ == StatusCode::Ok; }
  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  std::string to_string() const;

 private:
  Status(StatusCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  StatusCode code_ = StatusCode::Ok;
  std::string message_;
};

}  // namespace axiomdb
