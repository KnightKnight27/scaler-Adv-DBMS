#pragma once

#include <functional>
#include <vector>

using namespace std;

#include "common/Config.hpp"
#include "common/Types.hpp"

namespace minidb {

class BatchExecutor {
public:
    using Predicate = function<bool(const Row&)>;

    static RowList filterBatches(const RowList& input, Predicate pred) {
        RowList output;
        for (size_t i = 0; i < input.size(); i += BATCH_SIZE) {
            size_t end = min(i + (size_t)BATCH_SIZE, input.size());
            for (size_t j = i; j < end; ++j)
                if (pred(input[j])) output.push_back(input[j]);
        }
        return output;
    }
};

}  // namespace minidb
