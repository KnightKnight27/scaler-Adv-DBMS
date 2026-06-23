#include "replication.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <sys/time.h>

ReplicationNode::ReplicationNode(const std::string& ip, int port)
    : replica_port(port), replica_ip(ip), server_fd(-1) {
}

ReplicationNode::~ReplicationNode() {
    stopReplicaServer();
}

void ReplicationNode::stopReplicaServer() {
    if (server_fd != -1) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }
}

bool ReplicationNode::sendLogToReplica(const std::string& sql_statement) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(replica_port);

    if (inet_pton(AF_INET, replica_ip.c_str(), &serv_addr.sin_addr) <= 0) {
        close(sock);
        return false;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return false;
    }

    // Set a 2-second timeout for receiving the ACK
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        close(sock);
        return false;
    }

    // Send the SQL statement
    if (send(sock, sql_statement.c_str(), sql_statement.length(), 0) < 0) {
        close(sock);
        return false;
    }

    // Wait for ACK
    char buffer[1024] = {0};
    int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (bytes_read <= 0) {
        // Timeout or connection closed by peer
        close(sock);
        return false;
    }

    close(sock);
    std::string ack(buffer, bytes_read);
    return (ack == "OK");
}

void ReplicationNode::startReplicaServer(void (*execute_sql_callback)(const std::string&)) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("Replication Error: Failed to create server socket");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        close(server_fd);
        server_fd = -1;
        throw std::runtime_error("Replication Error: setsockopt failed");
    }

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(replica_port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        server_fd = -1;
        throw std::runtime_error("Replication Error: bind failed");
    }

    if (listen(server_fd, 3) < 0) {
        close(server_fd);
        server_fd = -1;
        throw std::runtime_error("Replication Error: listen failed");
    }

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    while (true) {
        int new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (new_socket < 0) {
            // Server socket was stopped
            break;
        }

        char buffer[1024] = {0};
        int valread = recv(new_socket, buffer, sizeof(buffer) - 1, 0);
        if (valread > 0) {
            std::string incoming_sql(buffer, valread);
            
            try {
                execute_sql_callback(incoming_sql);
                std::string ack = "OK";
                send(new_socket, ack.c_str(), ack.length(), 0);
            } catch (const std::exception& e) {
                std::string err = "ERR: " + std::string(e.what());
                send(new_socket, err.c_str(), err.length(), 0);
            }
        }
        close(new_socket);
    }
}
