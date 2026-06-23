#ifndef RECOVERY_MGR_H
#define RECOVERY_MGR_H

class Database;

class RecoveryManager {
private:
    Database* db;

public:
    explicit RecoveryManager(Database* db);
    ~RecoveryManager() = default;

    void recover();
    void rollback_transaction(int txn_id);
};

#endif
