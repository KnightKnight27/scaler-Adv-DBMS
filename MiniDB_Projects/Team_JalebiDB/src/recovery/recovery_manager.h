#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"

namespace minidb {

class RecoveryManager {
public:
    RecoveryManager(DiskManager *disk_manager, BufferPoolManager *buffer_pool_manager);
    ~RecoveryManager() = default;

    // Run the three phases of ARIES recovery
    void RunRecovery();

private:
    DiskManager *disk_manager_;
    BufferPoolManager *buffer_pool_manager_;
};

} // namespace minidb
