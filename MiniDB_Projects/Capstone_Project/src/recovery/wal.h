#pragma once

#include "common/types.h"
#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>
#include <atomic>

/**
 * @enum WALRecordType
 * @brief Enum defining the classification of transactions recorded in the WAL.
 */
enum class WALRecordType : uint8_t {
    BEGIN = 1,
    INSERT = 2,
    DELETE = 3,
    COMMIT = 4,
    ABORT = 5
};

/**
 * @struct WALRecord
 * @brief Fixed-size binary entry appended sequentially to the WAL file.
 *
 * Footprint: 8B + 8B + 1B + 32B + 4B + 256B = 309 Bytes.
 */
struct WALRecord {
    LSN lsn = 0;              ///< Log Sequence Number
    TxID txid = 0;            ///< Transaction identifier
    WALRecordType type = WALRecordType::BEGIN; ///< Operation category
    char table_name[MAX_TABLE_NAME_LEN] = {};  ///< Target table name
    int32_t key = 0;          ///< Record key payload
    char value[WAL_MAX_VALUE_SIZE] = {};       ///< Record value string payload
};

constexpr size_t WAL_RECORD_SIZE = sizeof(WALRecord);

/**
 * @class WAL
 * @brief Appends transactions logs sequentially before physical pages update.
 */
class WAL {
public:
    WAL() = default;
    ~WAL();

    // Disable copy behaviors
    WAL(const WAL&) = delete;
    WAL& operator=(const WAL&) = delete;

    /**
     * @brief Opens the log file at target path.
     * @return True on success, false if file cannot be initialized.
     */
    bool open(const std::string& path);

    /**
     * @brief Closes the WAL handle.
     */
    void close();

    /**
     * @brief Writes a BEGIN transaction log record.
     */
    LSN logBegin(TxID txid);

    /**
     * @brief Writes an INSERT log record.
     */
    LSN logInsert(TxID txid, const std::string& table, int32_t key, const std::string& value);

    /**
     * @brief Writes a DELETE log record.
     */
    LSN logDelete(TxID txid, const std::string& table, int32_t key);

    /**
     * @brief Writes a COMMIT log record and flushes the buffer to disk immediately.
     */
    LSN logCommit(TxID txid);

    /**
     * @brief Writes an ABORT transaction log record.
     */
    LSN logAbort(TxID txid);

    /**
     * @brief Flushes all buffered log modifications to block storage device (fsync).
     */
    void flush();

    /**
     * @brief Returns current maximum written Log Sequence Number.
     */
    LSN currentLSN() const { return current_lsn_.load(); }

    /**
     * @brief Returns the WAL file path.
     */
    const std::string& path() const { return path_; }

private:
    LSN appendRecord(const WALRecord& rec);

    std::string path_;
    int fd_ = -1;                     ///< POSIX descriptor to log file
    std::atomic<LSN> current_lsn_{0}; ///< Highest written LSN offset tracker
    std::mutex write_mutex_;          ///< Mutex locking concurrent write accesses
};
