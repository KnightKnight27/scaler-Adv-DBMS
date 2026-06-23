#pragma once

#include "recovery/wal.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>
#include <utility>

/**
 * @class Replica
 * @brief Follower node in the Primary-Replica replication model.
 *
 * Reads replication.log streams, buffers pending transactions, and replays
 * committed modifications to keep database replicas in sync.
 */
class Replica {
public:
    /**
     * @brief Locates heap storage and index structures by table name.
     */
    using TableProvider = std::function<std::pair<HeapFile*, BPlusTree*>(const std::string& table_name)>;

    Replica() = default;
    ~Replica();

    // Disable copy constructors
    Replica(const Replica&) = delete;
    Replica& operator=(const Replica&) = delete;

    /**
     * @brief Opens the replication log and configures the table provider callbacks.
     */
    bool open(const std::string& replication_log_path, TableProvider provider);

    /**
     * @brief Polls replication logs and replays committed record modifications.
     * @return Count of applied records in this loop.
     */
    int applyNewRecords();

    /**
     * @brief Returns current maximum applied LSN.
     */
    LSN replicaLSN() const { return replica_lsn_; }

    /**
     * @brief Closes log resources.
     */
    void close();

    /**
     * @brief Prints diagnostic replication sync status.
     */
    void printStatus(LSN primary_lsn) const;

private:
    int fd_ = -1;                     ///< POSIX descriptor to replication log file
    LSN replica_lsn_ = 0;             ///< Current maximum LSN applied on follower
    off_t read_offset_ = 0;           ///< Log byte offset pointer
    TableProvider table_provider_;    ///< Table provider callback reference

    std::unordered_set<TxID> committed_txids_; ///< Set tracking committed TxIDs
    std::unordered_map<TxID, std::vector<WALRecord>> pending_records_; ///< Buffers uncommitted row edits
};
