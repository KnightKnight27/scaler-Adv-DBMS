#pragma once
#include <cstdint>

namespace minidb {

struct TableStats {
    int32_t num_tuples_{0};
    int32_t min_pk_{0};
    int32_t max_pk_{0};

    void Update(int32_t pk) {
        if (num_tuples_ == 0) {
            min_pk_ = pk;
            max_pk_ = pk;
        } else {
            if (pk < min_pk_) min_pk_ = pk;
            if (pk > max_pk_) max_pk_ = pk;
        }
        num_tuples_++;
    }
};

} // namespace minidb
