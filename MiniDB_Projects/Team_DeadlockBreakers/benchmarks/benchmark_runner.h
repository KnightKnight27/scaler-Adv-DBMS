#pragma once

#include "common/types.h"
#include <vector>

namespace minidb::bench {

struct Result {
    size_t rows = 0;
    long long batch_wall_us = 0;
    long long row_wall_us = 0;
    long long batch_cpu_us = 0;
    long long row_cpu_us = 0;
    size_t matches = 0;
};

Result RunFilterBenchmark(const std::vector<Row>& data, const Schema& schema);

} // namespace minidb::bench
