#pragma once
#include "execution/executor.h"
#include "index/b_plus_tree.h"
#include "recovery/log_manager.h"
#include "optimizer/table_stats.h"
#include <string>

#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

namespace minidb {

class InsertExecutor : public Executor {
public:
    InsertExecutor(BPlusTree* tree, LogManager* log_manager, TableStats* stats, BufferPool* buffer_pool, DiskManager* disk_manager, int32_t id, const std::string& name);

    void Init() override;
    bool Next(Tuple* tuple) override;

private:
    BPlusTree* tree_;
    LogManager* log_manager_;
    TableStats* stats_;
    BufferPool* buffer_pool_;
    DiskManager* disk_manager_;
    int32_t id_;
    std::string name_;
    bool inserted_;
};

} // namespace minidb
