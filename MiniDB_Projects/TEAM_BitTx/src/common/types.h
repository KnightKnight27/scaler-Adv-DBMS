#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

using namespace std;

enum class TypeId : uint8_t { INVALID = 0, BOOLEAN, INTEGER, BIGINT, VARCHAR, DECIMAL };

class Value {
public:
  Value() = default;

  Value(bool v) : typeId_(TypeId::BOOLEAN), boolVal_(v) {}
  Value(int32_t v) : typeId_(TypeId::INTEGER), intVal_(v) {}
  Value(int64_t v) : typeId_(TypeId::BIGINT), bigVal_(v) {}
  Value(const string& v) : typeId_(TypeId::VARCHAR), strVal_(v) {}
  Value(const char* v) : typeId_(TypeId::VARCHAR), strVal_(v) {}

  TypeId GetTypeId() const {
    return typeId_;
  }
  bool IsNull() const {
    return typeId_ == TypeId::INVALID;
  }

  bool GetAsBoolean() const {
    return boolVal_;
  }
  int32_t GetAsInteger() const {
    return intVal_;
  }
  int64_t GetAsBigInt() const {
    return bigVal_;
  }
  const string& GetAsVarchar() const {
    return strVal_;
  }

  string ToString() const;

  void SerializeTo(char* buf) const;
  static Value DeserializeFrom(const char* buf, TypeId type);
  size_t SerializedSize() const;

  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const {
    return !(*this == other);
  }
  bool operator<(const Value& other) const;
  bool operator<=(const Value& other) const {
    return *this < other || *this == other;
  }
  bool operator>(const Value& other) const {
    return !(*this <= other);
  }
  bool operator>=(const Value& other) const {
    return !(*this < other);
  }

private:
  TypeId typeId_ = TypeId::INVALID;
  bool boolVal_ = false;
  int32_t intVal_ = 0;
  int64_t bigVal_ = 0;
  string strVal_;
};

class Column {
public:
  Column(string name, TypeId type, bool nullable = true, bool isPrimaryKey = false)
      : name_(move(name)), type_(type), nullable_(nullable), isPrimaryKey_(isPrimaryKey) {}

  const string& GetName() const {
    return name_;
  }
  TypeId GetType() const {
    return type_;
  }
  bool IsNullable() const {
    return nullable_;
  }
  bool IsPrimaryKey() const {
    return isPrimaryKey_;
  }

  size_t GetFixedLength() const;

private:
  string name_;
  TypeId type_;
  bool nullable_;
  bool isPrimaryKey_;
};

class Schema {
public:
  Schema() = default;
  explicit Schema(const vector<Column>& columns) : columns_(columns) {}

  void AddColumn(const Column& col) {
    columns_.push_back(col);
  }
  size_t GetColumnCount() const {
    return columns_.size();
  }

  const Column& GetColumn(size_t idx) const {
    return columns_[idx];
  }
  const vector<Column>& GetColumns() const {
    return columns_;
  }
  int GetColumnIndex(const string& name) const;

  size_t GetTupleLength() const;
  string ToString() const;

private:
  vector<Column> columns_;
};

const char* TypeIdToString(TypeId type);
TypeId StringToTypeId(const string& s);
size_t GetTypeSize(TypeId type);

} // namespace minidb