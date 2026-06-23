#pragma once

#include <cstdint>
#include <string>

namespace minidb {

using namespace std;

enum class LogRecordType : uint8_t {
  INVALID = 0,
  BEGIN,
  COMMIT,
  ABORT,
  INSERT,
  UPDATE,
  DELETE,
  CLR
};

struct LogRecord {
  LogRecordType type = LogRecordType::INVALID;
  int64_t txnId = -1;
  int32_t pageId = -1;
  int32_t slotId = -1;
  string oldData;
  string newData;
  int64_t prevLSN = -1;
};

string SerializeLog(const LogRecord& rec);
LogRecord DeserializeLogBody(const string& body);
int32_t LogFrameSize(const string& framed);

} // namespace minidb