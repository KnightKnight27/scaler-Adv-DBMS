#pragma once

#include "query/executor.h"
#include "transaction/transaction.h"
#include "recovery/wal.h"
#include "storage/lsm_engine.h"
#include <memory>
#include <string>

namespace minidb {

// database — main entry point for the minidb system
// wires together all components: storage engine, catalog, query executor,
// transaction manager, and recovery.

class Database {
public:
    Database(const std::string& data_dir);

    // --- lifecycle -----------------------------------------------------------
    // initialise the database.  if crash_recover is true, run wal recovery.
    bool init(bool crash_recover = true);

    // gracefully shut down: flush storage, close wal.
    void shutdown();

    // --- sql execution -------------------------------------------------------
    // execute a single sql statement string.  returns a formatted result.
    std::string execute(const std::string& sql);

    // --- transaction management ----------------------------------------------
    TxnID begin_transaction();
    bool  commit_transaction(TxnID txn_id);
    bool  abort_transaction(TxnID txn_id);
    TxnID current_txn() const { return _current_txn; }
    void  set_current_txn(TxnID id) { _current_txn = id; }

    // --- accessors -----------------------------------------------------------
    Catalog&            catalog()        { return _catalog; }
    LSMEngine&          storage()        { return _storage; }
    TransactionManager& txn_manager()    { return _txn_mgr; }
    LockManager&        lock_manager()   { return _lock_mgr; }
    WriteAheadLog&      wal()            { return _wal; }
    QueryExecutor&      executor()       { return _executor; }

private:
    std::string format_result(const QueryExecutor::ExecResult& result);

    std::string         _data_dir;
    LSMEngine           _storage;
    Catalog             _catalog;
    LockManager         _lock_mgr;
    TransactionManager  _txn_mgr;
    WriteAheadLog       _wal;
    QueryExecutor       _executor;
    TxnID               _current_txn = INVALID_TXN;
};

// implementation

inline Database::Database(const std::string& data_dir)
    : _data_dir(data_dir)
    , _storage(data_dir + "/lsm")
    , _txn_mgr(_lock_mgr)
    , _wal(data_dir + "/wal.log")
    , _executor(_storage, _catalog, &_txn_mgr) {

    // wire up wal callback to transaction manager
    _txn_mgr.set_wal_callback(
        [this](TxnID txn_id, TableID table_id, const Key& key,
               const Record& before, const Record& after) {
            _wal.append(LogRecordType::UPDATE, txn_id, table_id,
                        key, before, after);
        });
}

inline bool Database::init(bool crash_recover) {
    // create data directories
    std::filesystem::create_directories(_data_dir);

    // run crash recovery if requested
    if (crash_recover) {
        RecoveryManager recovery(_wal, _storage, _txn_mgr);
        recovery.recover();
    }
    return true;
}

inline void Database::shutdown() {
    _storage.close();
    _storage.flush();
    _wal.close();
}

inline std::string Database::execute(const std::string& sql) {
    Parser parser;
    auto stmt = parser.parse(sql);

    if (stmt.type == Statement::NONE) {
        return "Error: " + parser.error();
    }

    auto result = _executor.execute(stmt, _current_txn);

    if (!result.success) {
        return "Error: " + result.error;
    }

    return format_result(result);
}

inline std::string Database::format_result(const QueryExecutor::ExecResult& result) {
    if (result.column_names.empty()) {
        if (result.affected_rows > 0) {
            return "OK. " + std::to_string(result.affected_rows) + " row(s) affected.";
        }
        return "OK.";
    }

    std::ostringstream oss;

    // column headers
    for (size_t i = 0; i < result.column_names.size(); ++i) {
        if (i > 0) oss << " | ";
        oss << result.column_names[i];
    }
    oss << "\n";
    oss << std::string(result.column_names.size() * 10, '-') << "\n";

    // rows
    for (const auto& row : result.rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i > 0) oss << " | ";
            oss << row[i].to_string();
        }
        oss << "\n";
    }

    oss << "\n" << result.rows.size() << " row(s) returned.";
    return oss.str();
}

inline TxnID Database::begin_transaction() {
    _current_txn = _txn_mgr.begin();
    return _current_txn;
}

inline bool Database::commit_transaction(TxnID txn_id) {
    bool ok = _txn_mgr.commit(txn_id);
    if (ok && txn_id == _current_txn) _current_txn = INVALID_TXN;
    return ok;
}

inline bool Database::abort_transaction(TxnID txn_id) {
    bool ok = _txn_mgr.abort(txn_id);
    if (ok && txn_id == _current_txn) _current_txn = INVALID_TXN;
    return ok;
}

} // namespace minidb
