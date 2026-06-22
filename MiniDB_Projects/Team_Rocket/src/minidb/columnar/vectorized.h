#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace minidb {

constexpr int VECTOR_BATCH = 1024;

inline bool cmp_op(const std::string& op, int64_t a, int64_t b) {
    if (op == "=") return a == b;
    if (op == "!=") return a != b;
    if (op == "<") return a < b;
    if (op == ">") return a > b;
    if (op == "<=") return a <= b;
    if (op == ">=") return a >= b;
    return true;
}

// Sum `values`, optionally keeping only rows where `filter[i] op threshold`.
// Work proceeds in fixed-size batches: each pass over a batch is a tight loop
// over contiguous memory, which is what makes vectorised execution fast.
inline int64_t vectorized_sum(const std::vector<int64_t>& values,
                              const std::vector<int64_t>& filter, const std::string& op,
                              int64_t threshold, bool has_filter) {
    int64_t sum = 0;
    size_t n = values.size();
    for (size_t base = 0; base < n; base += VECTOR_BATCH) {
        size_t end = base + VECTOR_BATCH < n ? base + VECTOR_BATCH : n;
        if (!has_filter) {
            for (size_t i = base; i < end; ++i) sum += values[i];
        } else {
            for (size_t i = base; i < end; ++i)
                if (cmp_op(op, filter[i], threshold)) sum += values[i];
        }
    }
    return sum;
}

}  // namespace minidb
