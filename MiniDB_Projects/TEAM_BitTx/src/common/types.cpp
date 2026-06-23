#include "common/types.h"

#include <cstring>
#include <sstream>

namespace minidb {

using namespace std;

const char* TypeIdToString(TypeId type) {
  switch (type) {
  case TypeId::BOOLEAN:
    return "BOOLEAN";
  case TypeId::INTEGER:
    return "INTEGER";
  case TypeId::BIGINT:
    return "BIGINT";
  case TypeId::VARCHAR:
    return "VARCHAR";
  case TypeId::DECIMAL:
    return "DECIMAL";
  default:
    return "INVALID";
  }
}

TypeId StringToTypeId(const string& s) {
  if (s == "BOOLEAN" || s == "bool")
    return TypeId::BOOLEAN;
  if (s == "INTEGER" || s == "int" || s == "INT")
    return TypeId::INTEGER;
  if (s == "BIGINT" || s == "long")
    return TypeId::BIGINT;
  if (s == "VARCHAR" || s == "string" || s == "TEXT")
    return TypeId::VARCHAR;
  if (s == "DECIMAL" || s == "double")
    return TypeId::DECIMAL;
  return TypeId::INVALID;
}

size_t GetTypeSize(TypeId type) {
  switch (type) {
  case TypeId::BOOLEAN:
    return sizeof(bool);
  case TypeId::INTEGER:
    return sizeof(int32_t);
  case TypeId::BIGINT:
    return sizeof(int64_t);
  case TypeId::VARCHAR:
    return 0;
  case TypeId::DECIMAL:
    return sizeof(double);
  default:
    return 0;
  }
}

size_t Column::GetFixedLength() const {
  return GetTypeSize(type_);
}

string Value::ToString() const {
  ostringstream oss;
  switch (typeId_) {
  case TypeId::BOOLEAN:
    return boolVal_ ? "true" : "false";
  case TypeId::INTEGER:
    oss << intVal_;
    return oss.str();
  case TypeId::BIGINT:
    oss << bigVal_;
    return oss.str();
  case TypeId::VARCHAR:
    return strVal_;
  default:
    return "NULL";
  }
}

bool Value::operator==(const Value& other) const {
  if (typeId_ != other.typeId_)
    return false;
  switch (typeId_) {
  case TypeId::BOOLEAN:
    return boolVal_ == other.boolVal_;
  case TypeId::INTEGER:
    return intVal_ == other.intVal_;
  case TypeId::BIGINT:
    return bigVal_ == other.bigVal_;
  case TypeId::VARCHAR:
    return strVal_ == other.strVal_;
  default:
    return true;
  }
}

bool Value::operator<(const Value& other) const {
  if (typeId_ != other.typeId_) {
    return typeId_ < other.typeId_;
  }
  switch (typeId_) {
  case TypeId::BOOLEAN:
    return boolVal_ < other.boolVal_;
  case TypeId::INTEGER:
    return intVal_ < other.intVal_;
  case TypeId::BIGINT:
    return bigVal_ < other.bigVal_;
  case TypeId::VARCHAR:
    return strVal_ < other.strVal_;
  default:
    return false;
  }
}

int Schema::GetColumnIndex(const string& name) const {
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].GetName() == name)
      return static_cast<int>(i);
  }
  return -1;
}

size_t Schema::GetTupleLength() const {
  size_t total = 0;
  for (const auto& col : columns_) {
    total += col.GetFixedLength();
  }
  return total;
}

string Schema::ToString() const {
  ostringstream oss;
  oss << "[";
  for (size_t i = 0; i < columns_.size(); ++i) {
    if (i > 0)
      oss << ", ";
    oss << columns_[i].GetName() << ":" << TypeIdToString(columns_[i].GetType());
  }
  oss << "]";
  return oss.str();
}

void Value::SerializeTo(char* buf) const {
  memcpy(buf, &typeId_, sizeof(TypeId));
  buf += sizeof(TypeId);
  switch (typeId_) {
  case TypeId::BOOLEAN:
    memcpy(buf, &boolVal_, sizeof(bool));
    break;
  case TypeId::INTEGER:
    memcpy(buf, &intVal_, sizeof(int32_t));
    break;
  case TypeId::BIGINT:
    memcpy(buf, &bigVal_, sizeof(int64_t));
    break;
  case TypeId::VARCHAR: {
    int32_t len = static_cast<int32_t>(strVal_.size());
    memcpy(buf, &len, sizeof(int32_t));
    buf += sizeof(int32_t);
    memcpy(buf, strVal_.data(), len);
    break;
  }
  default:
    break;
  }
}

Value Value::DeserializeFrom(const char* buf, TypeId type) {
  TypeId actualType;
  memcpy(&actualType, buf, sizeof(TypeId));
  buf += sizeof(TypeId);
  Value v;
  switch (actualType) {
  case TypeId::BOOLEAN:
    memcpy(&v.boolVal_, buf, sizeof(bool));
    v.typeId_ = TypeId::BOOLEAN;
    break;
  case TypeId::INTEGER:
    memcpy(&v.intVal_, buf, sizeof(int32_t));
    v.typeId_ = TypeId::INTEGER;
    break;
  case TypeId::BIGINT:
    memcpy(&v.bigVal_, buf, sizeof(int64_t));
    v.typeId_ = TypeId::BIGINT;
    break;
  case TypeId::VARCHAR: {
    int32_t len;
    memcpy(&len, buf, sizeof(int32_t));
    buf += sizeof(int32_t);
    v.strVal_.assign(buf, len);
    v.typeId_ = TypeId::VARCHAR;
    break;
  }
  default:
    v.typeId_ = TypeId::INVALID;
    break;
  }
  return v;
}

size_t Value::SerializedSize() const {
  switch (typeId_) {
  case TypeId::BOOLEAN:
    return sizeof(TypeId) + sizeof(bool);
  case TypeId::INTEGER:
    return sizeof(TypeId) + sizeof(int32_t);
  case TypeId::BIGINT:
    return sizeof(TypeId) + sizeof(int64_t);
  case TypeId::VARCHAR:
    return sizeof(TypeId) + sizeof(int32_t) + strVal_.size();
  default:
    return sizeof(TypeId);
  }
}

} // namespace minidb