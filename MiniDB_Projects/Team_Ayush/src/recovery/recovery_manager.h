#pragma once
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>
#include "recovery/log_record.h"

namespace minidb {

// ARIES-lite crash recovery over a tiny data file whose page 0 holds an array
// of int32 "accounts". On restart we repeat history (redo every logged update)
// and then roll back the effects of transactions that never committed (undo).
class RecoveryManager {
 public:
  // Direct data access (page 0, cell `idx`).
  static int32_t ReadCell(const std::string& data_path, int32_t idx);
  static void    WriteCell(const std::string& data_path, int32_t idx, int32_t val);

  // Run recovery against the data file using `recs`. Writes a narrated trace to
  // `out` and returns the recovered values of cells [0, num_cells).
  static std::vector<int32_t> Recover(const std::string& data_path,
                                      const std::vector<LogRecord>& recs,
                                      int num_cells, std::ostream& out);
};

}  // namespace minidb
