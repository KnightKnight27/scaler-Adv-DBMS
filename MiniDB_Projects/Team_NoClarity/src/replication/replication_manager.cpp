#include "replication/replication_manager.h"
#include <cstring>
#include <iostream>
#include <vector>

namespace minidb {

inline void InitSockets() {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
        initialized = true;
    }
#endif
}

ReplicationManager::ReplicationManager(const std::string& target_ip, int target_port, ReplicationMode mode, LogManager* log_mgr)
    : client_socket_fd_(INVALID_SOCKET_VAL), mode_(mode), current_role_(NodeRole::PRIMARY),
      replica_online_(false), last_acknowledged_lsn_(0), target_ip_(target_ip), target_port_(target_port),
      log_mgr_(log_mgr), is_running_(false) {
    InitSockets();
}

ReplicationManager::~ReplicationManager() {
    is_running_ = false;
    replica_online_ = false;
    if (client_socket_fd_ != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(client_socket_fd_);
    }
    if (ack_thread_.joinable()) {
        ack_thread_.join();
    }
}

void ReplicationManager::StartBroadcasting() {
    client_socket_fd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (client_socket_fd_ == INVALID_SOCKET_VAL) {
        replica_online_ = false;
        return;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target_port_);
    inet_pton(AF_INET, target_ip_.c_str(), &addr.sin_addr);
    
    int connect_res = connect(client_socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (connect_res < 0) {
        replica_online_ = false;
        CLOSE_SOCKET(client_socket_fd_);
        client_socket_fd_ = INVALID_SOCKET_VAL;
        return;
    }
    
    // 1. Read handshake (max LSN) from the replica
    lsn_t replica_max_lsn = 0;
    int bytes_read = recv(client_socket_fd_, reinterpret_cast<char*>(&replica_max_lsn), sizeof(lsn_t), 0);
    if (bytes_read <= 0) {
        replica_online_ = false;
        CLOSE_SOCKET(client_socket_fd_);
        client_socket_fd_ = INVALID_SOCKET_VAL;
        return;
    }

    replica_online_ = true;
    is_running_ = true;
    ack_thread_ = std::thread(&ReplicationManager::ReceiveAcksLoop, this);

    // 2. Log Catch-up Phase: Replay missed logs
    if (log_mgr_ != nullptr) {
        std::ifstream is(log_mgr_->GetLogFileName(), std::ios::binary);
        if (is.is_open()) {
            LogRecord record;
            while (record.Deserialize(is)) {
                if (record.lsn > replica_max_lsn) {
                    ReplicateLog(record);
                }
            }
            is.close();
        }
    }
}

bool ReplicationManager::ReplicateLog(const LogRecord& record) {
    if (!replica_online_) {
        return false;
    }

    uint16_t slot_id = static_cast<uint16_t>(record.offset);
    uint16_t payload_len = static_cast<uint16_t>(record.after_image.length());
    uint32_t packet_len = 4 + 8 + 4 + 2 + 2 + payload_len;

    std::vector<char> buffer(packet_len);
    char* ptr = buffer.data();

    // 1. PacketLength (4B)
    std::memcpy(ptr, &packet_len, 4); ptr += 4;
    // 2. LSN (8B)
    uint64_t lsn_val = static_cast<uint64_t>(record.lsn);
    std::memcpy(ptr, &lsn_val, 8); ptr += 8;
    // 3. PageID (4B)
    std::memcpy(ptr, &record.page_id, 4); ptr += 4;
    // 4. SlotID (2B)
    std::memcpy(ptr, &slot_id, 2); ptr += 2;
    // 5. PayloadLength (2B)
    std::memcpy(ptr, &payload_len, 2); ptr += 2;
    // 6. AfterImageBytes
    if (payload_len > 0) {
        std::memcpy(ptr, record.after_image.data(), payload_len);
    }

    int bytes_sent = send(client_socket_fd_, buffer.data(), packet_len, 0);
    if (bytes_sent <= 0) {
        replica_online_ = false;
        return false;
    }

    if (mode_ == ReplicationMode::SYNCHRONOUS) {
        std::unique_lock<std::mutex> lock(ack_mutex_);
        auto timeout = std::chrono::milliseconds(500);
        bool status = ack_cv_.wait_for(lock, timeout, [this, record]() {
            return last_acknowledged_lsn_ >= record.lsn || !replica_online_;
        });
        if (!status || !replica_online_) {
            replica_online_ = false;
            return false;
        }
    }
    return true;
}

void ReplicationManager::HandleReplicaTimeout() {
    replica_online_ = false;
    if (client_socket_fd_ != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(client_socket_fd_);
        client_socket_fd_ = INVALID_SOCKET_VAL;
    }
    ack_cv_.notify_all();
}

void ReplicationManager::ReceiveAcksLoop() {
    char ack_buf[4];
    while (is_running_ && replica_online_) {
        int bytes_received = 0;
        while (bytes_received < 4 && is_running_ && replica_online_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_socket_fd_, &read_fds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            
            int select_res = select(client_socket_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
            if (select_res > 0) {
                int res = recv(client_socket_fd_, ack_buf + bytes_received, 4 - bytes_received, 0);
                if (res <= 0) {
                    replica_online_ = false;
                    ack_cv_.notify_all();
                    break;
                }
                bytes_received += res;
            } else if (select_res < 0) {
                replica_online_ = false;
                ack_cv_.notify_all();
                break;
            }
        }
        
        if (bytes_received == 4) {
            lsn_t ack_lsn;
            std::memcpy(&ack_lsn, ack_buf, 4);
            
            {
                std::lock_guard<std::mutex> lock(ack_mutex_);
                if (ack_lsn > last_acknowledged_lsn_) {
                    last_acknowledged_lsn_ = ack_lsn;
                }
            }
            ack_cv_.notify_all();
        }
    }
}

} // namespace minidb
