#ifndef REPLICATION_H
#define REPLICATION_H

#include "node.h"
#include <vector>

class ReplicationManager {
private:
    Node* primary;
    std::vector<Node*> replicas;

public:
    explicit ReplicationManager(Node* primary);
    ~ReplicationManager() = default;

    void add_replica(Node* replica);
    void replicate();
};

#endif
