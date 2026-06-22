#include "common/status.h"

namespace axiomdb {

const char* status_code_name(StatusCode code) {
  switch (code) {
    case StatusCode::Ok: return "Ok";
    case StatusCode::NotFound: return "NotFound";
    case StatusCode::AlreadyExists: return "AlreadyExists";
    case StatusCode::IoError: return "IoError";
    case StatusCode::Corruption: return "Corruption";
    case StatusCode::InvalidArgument: return "InvalidArgument";
    case StatusCode::NotSupported: return "NotSupported";
    case StatusCode::OutOfSpace: return "OutOfSpace";
    case StatusCode::Conflict: return "Conflict";
    case StatusCode::Aborted: return "Aborted";
  }
  return "Unknown";
}

std::string Status::to_string() const {
  std::string out = status_code_name(code_);
  if (!message_.empty()) {
    out += ": ";
    out += message_;
  }
  return out;
}

}  // namespace axiomdb
