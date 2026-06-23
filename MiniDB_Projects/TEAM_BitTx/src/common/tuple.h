#pragma once

#include "common/rid.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

using namespace std;

class Tuple {
public:
  Tuple() = default;
  explicit Tuple(const vector<Value>& values) : values_(values) {}
  Tuple(const vector<Value>& values, const RecordId& rid) : values_(values), rid_(rid) {}

  const vector<Value>& GetValues() const {
    return values_;
  }
  vector<Value>& GetValues() {
    return values_;
  }

  const Value& GetValue(size_t idx) const {
    return values_[idx];
  }
  void SetValue(size_t idx, const Value& v) {
    values_[idx] = v;
  }

  size_t GetSize() const {
    return values_.size();
  }

  const RecordId& GetRid() const {
    return rid_;
  }
  void SetRid(const RecordId& rid) {
    rid_ = rid;
  }

  bool IsNull(size_t idx) const {
    return values_[idx].IsNull();
  }

  string ToString() const;

  void SerializeTo(char* buf) const;
  static Tuple DeserializeFrom(const char* buf, const Schema& schema);

  bool operator==(const Tuple& other) const {
    if (values_.size() != other.values_.size())
      return false;
    for (size_t i = 0; i < values_.size(); ++i) {
      if (!(values_[i] == other.values_[i]))
        return false;
    }
    return true;
  }
  bool operator!=(const Tuple& other) const {
    return !(*this == other);
  }

private:
  vector<Value> values_;
  RecordId rid_;
};

} // namespace minidb
