#pragma once
#include "execution/executor.h"
#include "index/b_plus_tree.h"
#include "storage/buffer_pool.h"

namespace minidb {

class IndexScanExecutor : public Executor {
public:
    // Initializes index scan for a specific equality key
    IndexScanExecutor(BPlusTree* tree, BufferPool* buffer_pool, int32_t search_key);

    void Init() override;
    bool Next(Tuple* tuple) override;

private:
    BPlusTree* tree_;
    BufferPool* buffer_pool_;
    int32_t search_key_;
    bool done_;
};

} // namespace minidb
