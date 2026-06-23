#include "common/tuple.h"

#include "common/types.h"

#include <cstring>
#include <sstream>

namespace minidb {

using namespace std;

string Tuple::ToString() const {
  ostringstream oss;
  oss << "(";
  for (size_t i = 0; i < values_.size(); ++i) {
    if (i > 0)
      oss << ", ";
    oss << values_[i].ToString();
  }
  oss << ")";
  return oss.str();
}

void Tuple::SerializeTo(char* buf) const {
  int32_t n = static_cast<int32_t>(values_.size());
  memcpy(buf, &n, sizeof(int32_t));
  buf += sizeof(int32_t);
  for (const Value& v : values_) {
    v.SerializeTo(buf);
    buf += v.SerializedSize();
  }
}

Tuple Tuple::DeserializeFrom(const char* buf, const Schema& schema) {
  int32_t n;
  memcpy(&n, buf, sizeof(int32_t));
  buf += sizeof(int32_t);
  // debug removed
  vector<Value> values;
  values.reserve(n);
  for (int i = 0; i < n; ++i) {
    const Column& col = schema.GetColumns()[i];
    // debug removed
    values.push_back(Value::DeserializeFrom(buf, col.GetType()));
    buf += values.back().SerializedSize();
  }
  return Tuple(values);
}

} // namespace minidb