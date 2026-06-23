#pragma once

#include <cstddef>

namespace minidb {

struct ExecutionMetrics {
    std::size_t tuples_scanned = 0;
    std::size_t tuples_output = 0;
    std::size_t batches_processed = 0;
    std::size_t columnar_vector_bytes = 0;
    bool used_columnar_filter = false;
};

class ExecutionMetricsHolder {
public:
    static void Reset();
    static ExecutionMetrics& Get();

private:
    static ExecutionMetrics metrics_;
};

}  // namespace minidb
