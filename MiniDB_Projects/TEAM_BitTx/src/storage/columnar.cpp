#include "storage/columnar.h"

#include "common/tuple.h"

#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>

namespace minidb {

ColumnarFile::ColumnarFile(const string& filename, const Schema& schema)
    : filename_(filename), schema_(schema), numRows_(0), dirty_(false) {
  std::ifstream in(filename_, std::ios::binary);
  if (!in.good()) {
    // Initialize empty column vectors for new file.
    intCols_.assign(schema_.GetColumnCount(), {});
    bigCols_.assign(schema_.GetColumnCount(), {});
    boolCols_.assign(schema_.GetColumnCount(), {});
    dirty_ = true;
    return;
  }
  in.read(reinterpret_cast<char*>(&numRows_), sizeof(int32_t));
  int32_t nCols;
  in.read(reinterpret_cast<char*>(&nCols), sizeof(int32_t));
  if (nCols != schema_.GetColumnCount()) {
    throw std::runtime_error("columnar schema mismatch");
  }
  std::vector<TypeId> types(nCols);
  for (int32_t c = 0; c < nCols; ++c) {
    uint8_t t;
    in.read(reinterpret_cast<char*>(&t), sizeof(uint8_t));
    types[c] = static_cast<TypeId>(t);
  }
  intCols_.assign(nCols, {});
  bigCols_.assign(nCols, {});
  boolCols_.assign(nCols, {});
  for (int32_t c = 0; c < nCols; ++c) {
    int32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(int32_t));
    for (int32_t i = 0; i < count; ++i) {
      switch (types[c]) {
      case TypeId::INTEGER: {
        int32_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(int32_t));
        intCols_[c].push_back(v);
        break;
      }
      case TypeId::BIGINT: {
        int64_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(int64_t));
        bigCols_[c].push_back(v);
        break;
      }
      case TypeId::BOOLEAN: {
        uint8_t v;
        in.read(reinterpret_cast<char*>(&v), sizeof(uint8_t));
        boolCols_[c].push_back(v != 0);
        break;
      }
      default: {
        int32_t len;
        in.read(reinterpret_cast<char*>(&len), sizeof(int32_t));
        in.seekg(len, std::ios::cur);
        break;
      }
      }
    }
  }
  dirty_ = false;
}

ColumnarFile::~ColumnarFile() {
  if (dirty_) {
    Flush();
  }
}

void ColumnarFile::AppendRow(const Tuple& t) {
  assert(t.GetSize() == schema_.GetColumnCount());
  for (size_t i = 0; i < t.GetSize(); ++i) {
    const Value& v = t.GetValue(i);
    switch (schema_.GetColumn(i).GetType()) {
    case TypeId::INTEGER:
      intCols_[i].push_back(v.GetAsInteger());
      break;
    case TypeId::BIGINT:
      bigCols_[i].push_back(v.GetAsBigInt());
      break;
    case TypeId::BOOLEAN:
      boolCols_[i].push_back(v.GetAsBoolean());
      break;
    default:
      break; // VARCHAR not supported yet
    }
  }
  ++numRows_;
  dirty_ = true;
}

void ColumnarFile::Flush() const {
  if (!dirty_)
    return;
  std::ofstream out(filename_, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(&numRows_), sizeof(int32_t));
  int32_t nCols = schema_.GetColumnCount();
  out.write(reinterpret_cast<const char*>(&nCols), sizeof(int32_t));
  for (int32_t c = 0; c < nCols; ++c) {
    uint8_t t = static_cast<uint8_t>(schema_.GetColumn(c).GetType());
    out.write(reinterpret_cast<const char*>(&t), sizeof(uint8_t));
  }
  for (int32_t c = 0; c < nCols; ++c) {
    TypeId t = schema_.GetColumn(c).GetType();
    int32_t count = 0;
    switch (t) {
    case TypeId::INTEGER:
      count = static_cast<int32_t>(intCols_[c].size());
      break;
    case TypeId::BIGINT:
      count = static_cast<int32_t>(bigCols_[c].size());
      break;
    case TypeId::BOOLEAN:
      count = static_cast<int32_t>(boolCols_[c].size());
      break;
    default:
      count = 0;
      break;
    }
    out.write(reinterpret_cast<const char*>(&count), sizeof(int32_t));
    for (int32_t i = 0; i < count; ++i) {
      switch (t) {
      case TypeId::INTEGER: {
        int32_t v = intCols_[c][i];
        out.write(reinterpret_cast<const char*>(&v), sizeof(int32_t));
        break;
      }
      case TypeId::BIGINT: {
        int64_t v = bigCols_[c][i];
        out.write(reinterpret_cast<const char*>(&v), sizeof(int64_t));
        break;
      }
      case TypeId::BOOLEAN: {
        uint8_t v = boolCols_[c][i] ? 1 : 0;
        out.write(reinterpret_cast<const char*>(&v), sizeof(uint8_t));
        break;
      }
      default:
        break;
      }
    }
  }
  out.close();
  dirty_ = false;
}

std::vector<Tuple> ColumnarFile::Scan() const {
  std::vector<Tuple> rows;
  rows.reserve(numRows_);
  for (int32_t r = 0; r < numRows_; ++r) {
    std::vector<Value> vals;
    vals.reserve(schema_.GetColumnCount());
    for (size_t c = 0; c < schema_.GetColumnCount(); ++c) {
      switch (schema_.GetColumn(c).GetType()) {
      case TypeId::INTEGER:
        vals.push_back(Value(intCols_[c][r]));
        break;
      case TypeId::BIGINT:
        vals.push_back(Value(bigCols_[c][r]));
        break;
      case TypeId::BOOLEAN:
        vals.push_back(Value(boolCols_[c][r]));
        break;
      default:
        vals.push_back(Value(static_cast<int32_t>(0)));
        break;
      }
    }
    rows.emplace_back(vals);
  }
  return rows;
}

int32_t ColumnarFile::NumRows() const {
  return numRows_;
}

const std::vector<int32_t>& ColumnarFile::IntColumn(size_t idx) const {
  return intCols_[idx];
}

const std::vector<int64_t>& ColumnarFile::BigIntColumn(size_t idx) const {
  return bigCols_[idx];
}

const std::vector<bool>& ColumnarFile::BoolColumn(size_t idx) const {
  return boolCols_[idx];
}

} // namespace minidb