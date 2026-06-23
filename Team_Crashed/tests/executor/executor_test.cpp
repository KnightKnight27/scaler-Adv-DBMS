#include <cassert>
#include <filesystem>
#include <cstdio>

#include "catalog/catalog_manager.h"
#include "executor/query_engine.h"
#include "index/index_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;
namespace fs = std::filesystem;

int main() {
    fs::path tmp = fs::temp_directory_path() / "minidb_executor_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    storage::DiskManager dm((tmp / "minidb.db").string());
    storage::BufferPool bp(&dm, 32);
    catalog::CatalogManager cat(&dm);
    assert(cat.load() == Status::OK);
    index::IndexManager idx(&bp, &cat);
    transaction::TransactionManager txns;
    recovery::WAL wal((tmp / "wal.log").string());
    recovery::RecoveryManager rec(&wal, &bp, &cat, &idx, &txns);
    executor::QueryEngine engine(&bp, &cat, &idx, &txns, &rec);

    assert(engine.executeUpdate(
        "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(16), age INT)") == Status::OK);
    assert(engine.executeUpdate("INSERT INTO users VALUES (1, 'alice', 30), (2, 'bob', 25)") == Status::OK);

    auto rows = engine.execute("SELECT name FROM users WHERE id = 2");
    assert(rows.size() == 1);
    assert(rows[0].values.size() == 1);
    assert(rows[0].values[0].tag == executor::Value::Tag::STRING);
    assert(rows[0].values[0].s == "bob");

    assert(engine.executeUpdate("DELETE FROM users WHERE id = 1") == Status::OK);
    rows = engine.execute("SELECT id FROM users WHERE id = 1");
    assert(rows.empty());

    std::printf("[OK] executor SQL create/insert/select/delete\n");
    return 0;
}
