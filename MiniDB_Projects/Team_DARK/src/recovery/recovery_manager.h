#pragma once

#include "concurrency/lock_manager.h"
#include "recovery/log_manager.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/page.h"
#include "storage/table_heap.h"

#include <unordered_map>

namespace minidb {

struct RecoveryState {
    std::unordered_map<TxID, TxStatus> transactions;
    TxID next_xid = 1;
    page_id_t next_page_id = 0;
    page_id_t insert_page_id = INVALID_PAGE_ID;
};

class RecoveryManager {
public:
    static RecoveryState Recover(DiskManager* disk_manager, BufferPoolManager* buffer_pool,
                                 TableHeap* heap, LogManager* log_manager);
};

}  // namespace minidb
