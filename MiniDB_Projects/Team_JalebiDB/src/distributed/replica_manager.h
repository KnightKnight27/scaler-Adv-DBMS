#pragma once

#include "common/config.h"
#include "common/types.h"
#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

namespace minidb {

class ReplicaManager {
public:
    ReplicaManager(BufferPoolManager *bpm, LogManager *log_mgr, DiskManager *disk_mgr);
    ~ReplicaManager();

    // Start in Primary mode (listens for replica connections)
    void StartPrimary(int port);

    // Start in Replica mode (connects to primary and replays logs)
    void StartReplica(const std::string &primary_host, int port);

    // Stop replication threads and close sockets
    void Stop();

    // Promote this replica to primary
    void PromoteToPrimary();

    bool IsPrimary() const { return is_primary_; }
    bool IsConnected() const { return is_connected_; }

private:
    void RunPrimarySender();
    void RunReplicaReceiver();

    BufferPoolManager *bpm_;
    LogManager *log_mgr_;
    DiskManager *disk_mgr_;

    std::atomic<bool> is_primary_{true};
    std::atomic<bool> is_connected_{false};
    std::atomic<bool> run_threads_{false};

    int server_fd_{-1};
    int client_fd_{-1};

    std::thread worker_thread_;
    std::mutex latch_;
};

} // namespace minidb
