#include "planner/statistics.h"

namespace minidb {

TableStats gather_stats(StorageEngine* engine, const std::string& table) {
    TableStats s;
    auto cur = engine->scan(table);
    std::int64_t key;
    std::string  row;
    while (cur->next(key, row)) {
        if (s.empty) { s.key_min = s.key_max = key; s.empty = false; }
        else { if (key < s.key_min) s.key_min = key; if (key > s.key_max) s.key_max = key; }
        ++s.row_count;
    }
    return s;
}

} // namespace minidb
