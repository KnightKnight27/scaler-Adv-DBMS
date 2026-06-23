#pragma once
#include <string>
#include <stdexcept>

class ReplicationNode {
private:
    int replica_port;
    std::string replica_ip;
    int server_fd; // Used when acting as replica

public:
    // Initialize Node
    ReplicationNode(const std::string& ip, int port);
    ~ReplicationNode();
    
    // Primary Node Action: Send WAL to replica synchronously
    // Returns false if replica is dead or times out
    bool sendLogToReplica(const std::string& sql_statement);

    // Replica Node Action: Start listening for incoming WAL records
    // Takes a callback function to execute the SQL in the DB engine
    void startReplicaServer(void (*execute_sql_callback)(const std::string&));

    // Shut down the replica server socket
    void stopReplicaServer();
};
