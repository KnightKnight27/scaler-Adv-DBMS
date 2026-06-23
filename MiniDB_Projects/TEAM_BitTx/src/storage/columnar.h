#pragma once

#include "common/tuple.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

// Columnar file layout: each column is stored as a contiguous array of values
// in its own file. File format:
//
// header: [numRows:int32][numCols:int32][colType:uint8] * numCols
// then per-column fixed-width payloads (count:int32 + values).
class ColumnarFile {
public:
  ColumnarFile(const string& filename, const Schema& schema);
  ~ColumnarFile();

  void AppendRow(const Tuple& t);
  std::vector<Tuple> Scan() const;
  int32_t NumRows() const;
  const std::vector<int32_t>& IntColumn(size_t idx) const;
  const std::vector<int64_t>& BigIntColumn(size_t idx) const;
  const std::vector<bool>& BoolColumn(size_t idx) const;

private:
  void Flush() const;

  string filename_;
  Schema schema_;
  int32_t numRows_;
  mutable bool dirty_;
  mutable std::vector<std::vector<int32_t>> intCols_;
  mutable std::vector<std::vector<int64_t>> bigCols_;
  mutable std::vector<std::vector<bool>> boolCols_;
};

} // namespace minidb