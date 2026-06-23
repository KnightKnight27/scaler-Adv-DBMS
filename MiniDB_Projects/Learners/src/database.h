#ifndef DATABASE_H
#define DATABASE_H

#include "storage/page_manager.h"
#include "storage/buffer_pool.h"
#include "tx/lock_manager.h"
#include "tx/transaction.h"
#include "recovery/wal.h"
#include "recovery/recovery_mgr.h"
#include "index/bplus_tree.h"
#include "query/parser.h"
#include "query/optimizer.h"
#include "query/executor.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "compat.h"

class Database {
public:
    std::string db_dir;
    PageManager page_manager;
    BufferPool buffer_pool;
    LockManager lock_manager;
    std::unique_ptr<WAL> wal;
    std::unique_ptr<RecoveryManager> recovery_mgr;

    std::unordered_map<std::string, std::unique_ptr<BPlusTree>> indexes;
    std::unordered_map<std::string, TableStats> table_stats;
    CostBasedOptimizer optimizer;

    int next_txn_id{1};
    Mutex txn_mu;

    explicit Database(const std::string& db_dir, bool use_wal = true);
    ~Database();

    void rebuild_indexes();
    void rollback_transaction(int txn_id);

    Transaction* begin_transaction();
    
    int execute_update(const std::string& sql, Transaction* txn = nullptr);
    std::vector<Record> execute_query(const std::string& sql, Transaction* txn = nullptr);
};

#endif
