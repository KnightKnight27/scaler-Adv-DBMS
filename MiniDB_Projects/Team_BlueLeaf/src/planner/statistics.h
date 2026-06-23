#pragma once

#include <cstdint>
#include <string>

#include "engine/storage_engine.h"

namespace minidb {

// Minimal table statistics the optimizer uses for selectivity estimation. In a
// production system these are collected by ANALYZE and persisted; here we gather
// them with a quick scan on demand (documented simplification).
struct TableStats {
    std::uint64_t row_count = 0;
    std::int64_t  key_min   = 0;
    std::int64_t  key_max   = 0;
    bool          empty     = true;
};

TableStats gather_stats(StorageEngine* engine, const std::string& table);

} // namespace minidb
