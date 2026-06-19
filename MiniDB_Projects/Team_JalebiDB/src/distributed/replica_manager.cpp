#include "distributed/replica_manager.h"
#include "storage/page.h"
#include "recovery/log_record.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace minidb {

ReplicaManager::ReplicaManager(BufferPoolManager *bpm, LogManager *log_mgr, DiskManager *disk_mgr)
    : bpm_(bpm), log_mgr_(log_mgr), disk_mgr_(disk_mgr) {}

ReplicaManager::~ReplicaManager() {
    Stop();
}

void ReplicaManager::StartPrimary(int port) {
    is_primary_ = true;
    run_threads_ = true;

    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Primary: Failed to create socket");
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        throw std::runtime_error("Primary: Failed to bind to port " + std::to_string(port));
    }

    if (listen(server_fd_, 3) < 0) {
        close(server_fd_);
        server_fd_ = -1;
        throw std::runtime_error("Primary: Failed to listen");
    }

    std::cout << "[Primary] Listening for Replica connection on port " << port << "..." << std::endl;
    worker_thread_ = std::thread(&ReplicaManager::RunPrimarySender, this);
}

void ReplicaManager::StartReplica(const std::string &primary_host, int port) {
    is_primary_ = false;
    run_threads_ = true;

    client_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd_ < 0) {
        throw std::runtime_error("Replica: Failed to create socket");
    }

    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, primary_host.c_str(), &serv_addr.sin_addr) <= 0) {
        close(client_fd_);
        client_fd_ = -1;
        throw std::runtime_error("Replica: Invalid primary address");
    }

    std::cout << "[Replica] Connecting to Primary at " << primary_host << ":" << port << "..." << std::endl;
    if (connect(client_fd_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(client_fd_);
        client_fd_ = -1;
        throw std::runtime_error("Replica: Connection to primary failed");
    }

    is_connected_ = true;
    std::cout << "[Replica] Successfully connected to Primary. Commencing log sync." << std::endl;
    worker_thread_ = std::thread(&ReplicaManager::RunReplicaReceiver, this);
}

void ReplicaManager::Stop() {
    run_threads_ = false;
    
    {
        std::lock_guard<std::mutex> lock(latch_);
        if (client_fd_ >= 0) {
            shutdown(client_fd_, SHUT_RDWR);
            close(client_fd_);
            client_fd_ = -1;
        }
        if (server_fd_ >= 0) {
            shutdown(server_fd_, SHUT_RDWR);
            close(server_fd_);
            server_fd_ = -1;
        }
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    is_connected_ = false;
}

void ReplicaManager::PromoteToPrimary() {
    is_primary_ = true;
    is_connected_ = false;
    std::cout << "\n[Distributed Manager] !!! FAILOVER: REPLICA PROMOTED TO PRIMARY !!!" << std::endl;
    std::cout << "[Distributed Manager] Node is now accepting read-write transactions." << std::endl;
}

void ReplicaManager::RunPrimarySender() {
    sockaddr_in address{};
    int addrlen = sizeof(address);
    
    // Wait for connection
    int fd = accept(server_fd_, (struct sockaddr *)&address, (socklen_t *)&addrlen);
    if (fd < 0) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(latch_);
        client_fd_ = fd;
        is_connected_ = true;
    }
    std::cout << "[Primary] Replica connected. Replicating transaction log stream." << std::endl;

    int last_sent_offset = 0;
    while (run_threads_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        int log_size = disk_mgr_->GetLogFileSize();
        if (log_size > last_sent_offset) {
            int send_size = log_size - last_sent_offset;
            std::vector<char> buffer(send_size);
            disk_mgr_->ReadLog(buffer.data(), send_size, last_sent_offset);

            // Send size prefix
            int n_bytes = send(client_fd_, &send_size, sizeof(int), 0);
            if (n_bytes <= 0) {
                std::cout << "[Primary] Replica disconnected." << std::endl;
                is_connected_ = false;
                break;
            }

            // Send raw log payload
            n_bytes = send(client_fd_, buffer.data(), send_size, 0);
            if (n_bytes <= 0) {
                std::cout << "[Primary] Replica disconnected." << std::endl;
                is_connected_ = false;
                break;
            }

            last_sent_offset += send_size;
        }
    }
}

void ReplicaManager::RunReplicaReceiver() {
    while (run_threads_) {
        int batch_size = 0;
        // Read size prefix
        int n_bytes = recv(client_fd_, &batch_size, sizeof(int), 0);
        if (n_bytes <= 0) {
            std::cout << "[Replica] Connection to Primary lost." << std::endl;
            PromoteToPrimary();
            break;
        }

        // Read log payload
        std::vector<char> buffer(batch_size);
        int total_read = 0;
        while (total_read < batch_size) {
            int read_bytes = recv(client_fd_, buffer.data() + total_read, batch_size - total_read, 0);
            if (read_bytes <= 0) {
                break;
            }
            total_read += read_bytes;
        }

        if (total_read < batch_size) {
            std::cout << "[Replica] Connection to Primary lost." << std::endl;
            PromoteToPrimary();
            break;
        }

        // Replay received logs
        int offset = 0;
        int replay_count = 0;
        while (offset < batch_size) {
            uint32_t record_size;
            std::memcpy(&record_size, buffer.data() + offset, sizeof(uint32_t));
            if (record_size == 0) break;

            LogRecord rec = LogRecord::Deserialize(buffer.data() + offset);
            
            // Replay inserts/deletes to replica's storage
            if (rec.GetType() == LogRecordType::INSERT || rec.GetType() == LogRecordType::DELETE) {
                RID rid = rec.GetRID();
                Page *page = bpm_->FetchPage(rid.page_id);
                if (page != nullptr) {
                    SlottedPage slotted(page);
                    if (rec.GetType() == LogRecordType::INSERT) {
                        slotted.RestoreTuple(rid.slot_id, rec.GetData().data(), rec.GetData().size());
                    } else if (rec.GetType() == LogRecordType::DELETE) {
                        slotted.DeleteTuple(rid.slot_id);
                    }
                    page->SetLSN(rec.GetLSN());
                    bpm_->UnpinPage(rid.page_id, true);
                    replay_count++;
                }
            }

            // Write logs locally to keep replica WAL synced
            log_mgr_->AppendLogRecord(&rec);
            offset += record_size;
        }
        
        if (replay_count > 0) {
            std::cout << "[Replica] Synchronized " << replay_count << " txn log records from Primary." << std::endl;
        }
    }
}

} // namespace minidb
