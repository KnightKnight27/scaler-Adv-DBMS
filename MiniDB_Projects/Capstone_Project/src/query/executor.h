#pragma once

#include "query/parser.h"
#include "query/optimizer.h"
#include "storage/heap_file.h"
#include "index/bplus_tree.h"
#include "transaction/tx_manager.h"
#include "recovery/wal.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <utility>

/**
 * @struct TableEntry
 * @brief Combines table physical heap files and primary indexes.
 */
struct TableEntry {
    std::unique_ptr<HeapFile> heap;    ///< Storage heap allocator layer
    std::unique_ptr<BPlusTree> index;   ///< Corresponding B+ Tree index helper
};

/**
 * @class Executor
 * @brief Dispatcher class receiving ParsedQuery objects to execute them.
 */
class Executor {
public:
    /**
     * @brief Construct an Executor.
     */
    Executor(BufferPool& bp, WAL& wal, TxManager& txm);

    // Disable copy semantics
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    /**
     * @brief Evaluates the command and dispatches it to internal operations.
     */
    void execute(const ParsedQuery& q);

    /**
     * @brief Connects a table's structural elements to the query processor registry.
     */
    void registerTable(const std::string& name, TableEntry entry);

    /**
     * @brief Verifies if a given table exists in the database.
     */
    bool tableExists(const std::string& name) const;

    /**
     * @brief Retrieves pointers to the underlying heap file and index components of a table.
     */
    std::pair<HeapFile*, BPlusTree*> getTablePointers(const std::string& name);

    /**
     * @brief Outputs list of all registered tables.
     */
    void showTables() const;

private:
    void doCreateTable(const ParsedQuery& q);
    void doInsert(const ParsedQuery& q);
    void doSelectAll(const ParsedQuery& q);
    void doSelectKey(const ParsedQuery& q);
    void doSelectRange(const ParsedQuery& q);
    void doJoin(const ParsedQuery& q);
    void doDelete(const ParsedQuery& q);

    void printRecord(const RecordID& rid, const Record& rec) const;

    TableEntry* getTable(const std::string& name);

    void rollback(TxID txid);

    BufferPool& bp_;
    WAL& wal_;
    TxManager& txm_;
    Optimizer optimizer_;

    std::unordered_map<std::string, TableEntry> tables_; ///< Map linking tables to schema components
    TxID current_txid_ = 0;                             ///< Active transaction ID in this session (0 = auto-commit)

    enum class UndoOpType { INSERT, DELETE };

    struct UndoRecord {
        UndoOpType type;
        std::string table_name;
        int32_t key;
        std::string value;
        RecordID rid;
    };

    std::unordered_map<TxID, std::vector<UndoRecord>> undo_buffer_; ///< Tracks transaction changes for rollbacks
};
