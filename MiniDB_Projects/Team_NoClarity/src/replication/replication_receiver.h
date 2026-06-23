#ifndef REPLICATION_RECEIVER_H
#define REPLICATION_RECEIVER_H

#include "common/config.h"
#include "storage/buffer_pool_manager.h"
#include "replication/replication_manager.h"
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

namespace minidb {

/**
 * Replica-side replication listener processing incoming socket connections and log packet streams.
 */
class ReplicationReceiver {
public:
    ReplicationReceiver(int listen_port, BufferPoolManager* bpm);
    ~ReplicationReceiver();
    
    // Spawns background network processing loop
    void StartListening(); 
    
    // Terminates connection listeners
    void StopListening();
    
    // Toggles role to accept client writes
    void PromoteToPrimary(); 

    // Getter helpers for testing
    NodeRole GetRole() const { return role_; }
    bool IsRunning() const { return is_running_; }

private:
    void ListenLoop();
    void ProcessIncomingLogPacket(int client_fd);
    
    int server_socket_fd_;
    std::atomic<bool> is_running_;
    NodeRole role_;
    BufferPoolManager* bpm_;

    int listen_port_;
    std::thread listen_thread_;
    std::vector<int> client_fds_;
    std::mutex client_fds_mutex_;
};

} // namespace minidb

#endif // REPLICATION_RECEIVER_H
