#pragma once
#include "execution/executor.h"
#include "storage/buffer_pool.h"

namespace minidb {

class SeqScanExecutor : public Executor {
public:
    SeqScanExecutor(BufferPool* buffer_pool, DiskManager* disk_manager);

    void Init() override;
    bool Next(Tuple* tuple) override;

private:
    BufferPool* buffer_pool_;
    DiskManager* disk_manager_;
    
    PageId current_page_id_;
    int current_slot_;
    int total_pages_;
};

} // namespace minidb
