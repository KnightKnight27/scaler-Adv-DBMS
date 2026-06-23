#pragma once
#include "execution/executor.h"
#include "index/b_plus_tree.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include <memory>

namespace minidb {

class DeleteExecutor : public Executor {
public:
    DeleteExecutor(std::unique_ptr<Executor> child_executor, BPlusTree* tree, LogManager* log_manager,
                   BufferPool* buffer_pool, DiskManager* disk_manager);

    void Init() override;
    bool Next(Tuple* tuple) override;

private:
    std::unique_ptr<Executor> child_executor_;
    BPlusTree* tree_;
    LogManager* log_manager_;
    BufferPool* buffer_pool_;
    DiskManager* disk_manager_;
};

} // namespace minidb
