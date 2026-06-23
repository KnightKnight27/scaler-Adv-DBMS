#include <cassert>
#include <filesystem>
#include <cstdio>

#include "catalog/catalog_manager.h"
#include "index/index_manager.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/transaction_manager.h"

using namespace minidb;
namespace fs = std::filesystem;

int main() {
    fs::path tmp = fs::temp_directory_path() / "minidb_recovery_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    fs::path walPath = tmp / "wal.log";
    {
        recovery::WAL wal(walPath.string());
        recovery::LogRecord begin{};
        begin.kind = recovery::LogKind::BEGIN;
        begin.txnId = 1;
        assert(wal.append(begin) != INVALID_LSN);

        recovery::LogRecord commit{};
        commit.kind = recovery::LogKind::COMMIT;
        commit.txnId = 1;
        assert(wal.append(commit) != INVALID_LSN);
        assert(wal.flush() == Status::OK);
    }

    recovery::WAL wal(walPath.string());
    int seen = 0;
    assert(wal.read(1, [&](const recovery::LogRecord& r) {
        if (r.txnId == 1) ++seen;
        return true;
    }) == Status::OK);
    assert(seen == 2);

    storage::DiskManager dm((tmp / "minidb.db").string());
    storage::BufferPool bp(&dm, 8);
    catalog::CatalogManager cat(&dm);
    assert(cat.load() == Status::OK);
    index::IndexManager idx(&bp, &cat);
    transaction::TransactionManager txns;
    recovery::RecoveryManager rec(&wal, &bp, &cat, &idx, &txns);
    assert(rec.runAtStartup() == Status::OK);

    std::printf("[OK] WAL round-trip and startup recovery\n");
    return 0;
}
