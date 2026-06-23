#ifndef REPLICATION_MANAGER_H
#define REPLICATION_MANAGER_H

#include "common/config.h"
#include "recovery/log_manager.h"
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSE_SOCKET(s) closesocket(s)
#define INVALID_SOCKET_VAL -1
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
using socket_t = int;
#define CLOSE_SOCKET(s) close(s)
#define INVALID_SOCKET_VAL -1
#endif

namespace minidb {

// Enums representing the replication mode and the node's current cluster role
enum class ReplicationMode { SYNCHRONOUS, ASYNCHRONOUS };
enum class NodeRole { PRIMARY, REPLICA };

/**
 * Primary-side replication coordinator managing replication sockets and streaming WAL log packets.
 */
class ReplicationManager {
public:
    ReplicationManager(const std::string& target_ip, int target_port, ReplicationMode mode);
    ~ReplicationManager();
    
    void StartBroadcasting();
    bool ReplicateLog(const LogRecord& record); // Invoked by Primary LogManager on write path
    void HandleReplicaTimeout();

    // Getter helpers for testing
    ReplicationMode GetMode() const { return mode_; }
    NodeRole GetRole() const { return current_role_; }
    bool IsReplicaOnline() const { return replica_online_; }
    lsn_t GetLastAcknowledgedLSN() const { return last_acknowledged_lsn_; }

private:
    void ReceiveAcksLoop();

    int client_socket_fd_;
    ReplicationMode mode_;
    NodeRole current_role_;
    std::atomic<bool> replica_online_;
    std::atomic<lsn_t> last_acknowledged_lsn_;

    std::string target_ip_;
    int target_port_;
    
    std::mutex ack_mutex_;
    std::condition_variable ack_cv_;
    std::thread ack_thread_;
    std::atomic<bool> is_running_;
};

} // namespace minidb

#endif // REPLICATION_MANAGER_H
