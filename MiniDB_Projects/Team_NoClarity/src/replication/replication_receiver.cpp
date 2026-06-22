#include "replication/replication_receiver.h"
#include "storage/slotted_page.h"
#include <cstring>
#include <vector>
#include <iostream>

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

ReplicationReceiver::ReplicationReceiver(int listen_port, BufferPoolManager* bpm)
    : server_socket_fd_(INVALID_SOCKET_VAL), is_running_(false), role_(NodeRole::REPLICA),
      bpm_(bpm), listen_port_(listen_port) {
    InitSockets();
}

ReplicationReceiver::~ReplicationReceiver() {
    StopListening();
}

void ReplicationReceiver::StartListening() {
    server_socket_fd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (server_socket_fd_ == INVALID_SOCKET_VAL) {
        return;
    }
    
    int opt = 1;
    setsockopt(server_socket_fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port_);
    
    if (bind(server_socket_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        CLOSE_SOCKET(server_socket_fd_);
        server_socket_fd_ = INVALID_SOCKET_VAL;
        return;
    }
    
    if (listen(server_socket_fd_, 5) < 0) {
        CLOSE_SOCKET(server_socket_fd_);
        server_socket_fd_ = INVALID_SOCKET_VAL;
        return;
    }
    
    is_running_ = true;
    listen_thread_ = std::thread(&ReplicationReceiver::ListenLoop, this);
}

void ReplicationReceiver::StopListening() {
    is_running_ = false;
    if (server_socket_fd_ != INVALID_SOCKET_VAL) {
        CLOSE_SOCKET(server_socket_fd_);
        server_socket_fd_ = INVALID_SOCKET_VAL;
    }
    
    {
        std::lock_guard<std::mutex> lock(client_fds_mutex_);
        for (int fd : client_fds_) {
            CLOSE_SOCKET(fd);
        }
        client_fds_.clear();
    }
    
    if (listen_thread_.joinable()) {
        listen_thread_.join();
    }
}

void ReplicationReceiver::PromoteToPrimary() {
    role_ = NodeRole::PRIMARY;
}

void ReplicationReceiver::ListenLoop() {
    while (is_running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_socket_fd_, &read_fds);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int select_res = select(server_socket_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (select_res > 0) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = static_cast<int>(accept(server_socket_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len));
            if (client_fd != INVALID_SOCKET_VAL) {
                {
                    std::lock_guard<std::mutex> lock(client_fds_mutex_);
                    client_fds_.push_back(client_fd);
                }
                std::thread worker(&ReplicationReceiver::ProcessIncomingLogPacket, this, client_fd);
                worker.detach();
            }
        }
    }
}

void ReplicationReceiver::ProcessIncomingLogPacket(int client_fd) {
    while (is_running_) {
        char header_buf[4];
        int bytes_received = 0;
        while (bytes_received < 4 && is_running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_fd, &read_fds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            int select_res = select(client_fd + 1, &read_fds, nullptr, nullptr, &tv);
            if (select_res > 0) {
                int res = recv(client_fd, header_buf + bytes_received, 4 - bytes_received, 0);
                if (res <= 0) {
                    break;
                }
                bytes_received += res;
            } else if (select_res < 0) {
                break;
            }
        }
        
        if (bytes_received < 4) {
            break;
        }
        
        uint32_t packet_len;
        std::memcpy(&packet_len, header_buf, 4);
        
        std::vector<char> packet_data(packet_len - 4);
        bytes_received = 0;
        int target_len = packet_len - 4;
        while (bytes_received < target_len && is_running_) {
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(client_fd, &read_fds);
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            int select_res = select(client_fd + 1, &read_fds, nullptr, nullptr, &tv);
            if (select_res > 0) {
                int res = recv(client_fd, packet_data.data() + bytes_received, target_len - bytes_received, 0);
                if (res <= 0) {
                    break;
                }
                bytes_received += res;
            } else if (select_res < 0) {
                break;
            }
        }
        
        if (bytes_received < target_len) {
            break;
        }
        
        const char* ptr = packet_data.data();
        
        uint64_t lsn_val;
        std::memcpy(&lsn_val, ptr, 8); ptr += 8;
        lsn_t record_lsn = static_cast<lsn_t>(lsn_val);
        
        page_id_t page_id;
        std::memcpy(&page_id, ptr, 4); ptr += 4;
        
        uint16_t slot_id;
        std::memcpy(&slot_id, ptr, 2); ptr += 2;
        
        uint16_t payload_len;
        std::memcpy(&payload_len, ptr, 2); ptr += 2;
        
        std::string after_image(ptr, payload_len);
        
        Page* page = bpm_->FetchPage(page_id);
        if (page != nullptr) {
            page->WLock();
            char* page_data = page->GetData();
            
            uint16_t slot_count = SlottedPage::GetSlotCount(page_data);
            if (slot_id >= slot_count) {
                for (uint16_t i = slot_count; i <= slot_id; ++i) {
                    SlottedPage::SetSlotCount(page_data, i + 1);
                    SlottedPage::Slot* slots = SlottedPage::GetSlotArray(page_data);
                    slots[i].offset = SlottedPage::TOMBSTONE;
                    slots[i].length = 0;
                }
            }
            
            SlottedPage::Slot* slots = SlottedPage::GetSlotArray(page_data);
            slots[slot_id].offset = SlottedPage::TOMBSTONE;
            slots[slot_id].length = 0;
            
            SlottedPage::CompactPage(page_data);
            
            uint16_t fsp = SlottedPage::GetFreeSpacePointer(page_data);
            uint16_t new_fsp = fsp - payload_len;
            std::memcpy(page_data + new_fsp, after_image.data(), payload_len);
            SlottedPage::SetFreeSpacePointer(page_data, new_fsp);
            
            slots = SlottedPage::GetSlotArray(page_data);
            slots[slot_id].offset = new_fsp;
            slots[slot_id].length = payload_len;
            
            page->SetPageLSN(record_lsn);
            
            page->WUnlock();
            bpm_->UnpinPage(page_id, true);
        }
        
        send(client_fd, reinterpret_cast<const char*>(&record_lsn), 4, 0);
    }
    CLOSE_SOCKET(client_fd);
}

} // namespace minidb
