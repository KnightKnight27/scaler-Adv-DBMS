#pragma once
#include "server/parser.h"
#include "execution/executor.h"
#include "index/b_plus_tree.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "optimizer/table_stats.h"
#include "recovery/log_manager.h"
#include <memory>
#include <string>

namespace minidb {

class Optimizer {
public:
    Optimizer(BufferPool* buffer_pool, DiskManager* disk_manager, BPlusTree* tree, TableStats* stats, LogManager* log_manager);

    // Creates the optimal execution plan based on the parsed statement
    std::unique_ptr<Executor> Optimize(const SQLStatement& stmt);

private:
    BufferPool* buffer_pool_;
    DiskManager* disk_manager_;
    BPlusTree* tree_;
    TableStats* stats_;
    LogManager* log_manager_;
};

} // namespace minidb
