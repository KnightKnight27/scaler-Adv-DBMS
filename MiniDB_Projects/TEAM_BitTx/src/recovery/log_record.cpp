#include "recovery/log_record.h"

#include <cstring>

namespace minidb {

using namespace std;

namespace {
void AppendInt(string& s, int64_t v) {
  s.append(reinterpret_cast<const char*>(&v), sizeof(int64_t));
}
void AppendInt32(string& s, int32_t v) {
  s.append(reinterpret_cast<const char*>(&v), sizeof(int32_t));
}
void AppendStr(string& s, const string& v) {
  int32_t len = static_cast<int32_t>(v.size());
  s.append(reinterpret_cast<const char*>(&len), sizeof(int32_t));
  s.append(v);
}
bool ReadInt(const string& s, size_t& pos, int64_t* v) {
  if (pos + sizeof(int64_t) > s.size())
    return false;
  memcpy(v, s.data() + pos, sizeof(int64_t));
  pos += sizeof(int64_t);
  return true;
}
bool ReadInt32(const string& s, size_t& pos, int32_t* v) {
  if (pos + sizeof(int32_t) > s.size())
    return false;
  memcpy(v, s.data() + pos, sizeof(int32_t));
  pos += sizeof(int32_t);
  return true;
}
bool ReadStr(const string& s, size_t& pos, string* v) {
  if (pos + sizeof(int32_t) > s.size())
    return false;
  int32_t len;
  memcpy(&len, s.data() + pos, sizeof(int32_t));
  pos += sizeof(int32_t);
  if (pos + len > s.size())
    return false;
  v->assign(s, pos, len);
  pos += len;
  return true;
}
} // namespace

string SerializeLog(const LogRecord& rec) {
  string body;
  body.push_back(static_cast<char>(rec.type));
  AppendInt(body, rec.txnId);
  AppendInt32(body, rec.pageId);
  AppendInt32(body, rec.slotId);
  AppendStr(body, rec.oldData);
  AppendStr(body, rec.newData);
  AppendInt(body, rec.prevLSN);
  string out;
  int32_t len = static_cast<int32_t>(body.size());
  out.append(reinterpret_cast<const char*>(&len), sizeof(int32_t));
  out.append(body);
  return out;
}

LogRecord DeserializeLogBody(const string& data) {
  LogRecord r;
  size_t pos = 0;
  if (pos >= data.size())
    return r;
  r.type = static_cast<LogRecordType>(static_cast<unsigned char>(data[pos++]));
  ReadInt(data, pos, &r.txnId);
  ReadInt32(data, pos, &r.pageId);
  ReadInt32(data, pos, &r.slotId);
  ReadStr(data, pos, &r.oldData);
  ReadStr(data, pos, &r.newData);
  ReadInt(data, pos, &r.prevLSN);
  return r;
}

int32_t LogFrameSize(const string& framed) {
  if (framed.size() < sizeof(int32_t))
    return -1;
  int32_t len;
  memcpy(&len, framed.data(), sizeof(int32_t));
  return len;
}

} // namespace minidb