#pragma once

#include "recovery/wal.h"
#include <string>

/**
 * @class Primary
 * @brief Leader node in the Primary-Replica log shipping replication model.
 *
 * Appends committed WAL records into a separate replication.log file.
 */
class Primary {
public:
    Primary() = default;
    ~Primary();

    // Disable copy constructors
    Primary(const Primary&) = delete;
    Primary& operator=(const Primary&) = delete;

    /**
     * @brief Opens the replication log file for write/append operations.
     * @return True on success, false if file cannot be accessed.
     */
    bool open(const std::string& replication_log_path);

    /**
     * @brief Ships new records from the main WAL file to the replication log starting from lsn_from.
     */
    void shipLog(const std::string& wal_path, LSN lsn_from);

    /**
     * @brief Closes the replication log handle.
     */
    void close();

    /**
     * @brief Returns the maximum LSN shipped to the replication stream.
     */
    LSN shippedLSN() const { return shipped_lsn_; }

private:
    int fd_ = -1;              ///< POSIX descriptor to replication log file
    LSN shipped_lsn_ = 0;      ///< High water mark LSN shipped to replica
};
