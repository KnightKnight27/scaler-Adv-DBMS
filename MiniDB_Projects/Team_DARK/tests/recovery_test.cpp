#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include "concurrency/transaction_manager.h"

#include <cstdio>
#include <ctime>
#include <string>

namespace {

std::string TempDbPath(const char* suffix) {
    return std::string("/tmp/minidb_recovery_") + suffix + "_" +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + ".db";
}

std::string LogPathForDb(const std::string& db_path) {
    return db_path.substr(0, db_path.size() - 3) + ".log";
}

void RemoveFiles(const std::string& db_path) {
    std::remove(db_path.c_str());
    std::remove(LogPathForDb(db_path).c_str());
}

}  // namespace

TEST_CASE("Committed transaction survives recovery restart") {
    const std::string db_path = TempDbPath("committed");
    RemoveFiles(db_path);

    {
        minidb::TransactionManager tm(db_path);
        const minidb::TxID tx = tm.Begin();
        tm.Insert(tx, "user:1", "alice");
        tm.Commit(tx);
        tm.FlushRecoveryState();
    }

    minidb::TransactionManager restarted(db_path);
    const minidb::TxID reader = restarted.Begin();
    const auto value = restarted.Read(reader, "user:1");
    REQUIRE(value.has_value());
    CHECK(*value == "alice");
    restarted.Commit(reader);

    RemoveFiles(db_path);
}

TEST_CASE("Uncommitted transaction is undone after recovery") {
    const std::string db_path = TempDbPath("uncommitted");
    RemoveFiles(db_path);

    minidb::TxID committed_tx = 0;
    {
        minidb::TransactionManager tm(db_path);
        committed_tx = tm.Begin();
        tm.Insert(committed_tx, "anchor", "stable");
        tm.Commit(committed_tx);
        tm.FlushRecoveryState();

        const minidb::TxID uncommitted = tm.Begin();
        tm.Insert(uncommitted, "ghost", "should_disappear");
        tm.FlushRecoveryState();
    }

    minidb::TransactionManager restarted(db_path);
    const minidb::TxID reader = restarted.Begin();

    const auto anchor = restarted.Read(reader, "anchor");
    REQUIRE(anchor.has_value());
    CHECK(*anchor == "stable");

    const auto ghost = restarted.Read(reader, "ghost");
    CHECK_FALSE(ghost.has_value());

    CHECK(restarted.IsCommitted(committed_tx));
    restarted.Commit(reader);

    RemoveFiles(db_path);
}

TEST_CASE("Aborted transaction changes are rolled back after recovery") {
    const std::string db_path = TempDbPath("aborted");
    RemoveFiles(db_path);

    {
        minidb::TransactionManager tm(db_path);
        const minidb::TxID setup = tm.Begin();
        tm.Insert(setup, "balance", "100");
        tm.Commit(setup);

        const minidb::TxID aborted = tm.Begin();
        tm.Update(aborted, "balance", "999");
        tm.Abort(aborted);
        tm.FlushRecoveryState();
    }

    minidb::TransactionManager restarted(db_path);
    const minidb::TxID reader = restarted.Begin();
    const auto value = restarted.Read(reader, "balance");
    REQUIRE(value.has_value());
    CHECK(*value == "100");
    restarted.Commit(reader);

    RemoveFiles(db_path);
}

TEST_CASE("No-log restart preserves flushed heap pages") {
    const std::string db_path = TempDbPath("no_log_restart");
    const std::string log_path = LogPathForDb(db_path);
    RemoveFiles(db_path);

    {
        minidb::TransactionManager tm(db_path, log_path);
        const minidb::TxID tx = tm.Begin();
        tm.Insert(tx, "persist:1", "value_one");
        tm.Commit(tx);
        tm.FlushRecoveryState();
        std::remove(log_path.c_str());
    }

    minidb::TransactionManager restarted(db_path, log_path);
    const minidb::TxID reader = restarted.Begin();
    const auto value = restarted.Read(reader, "persist:1");
    REQUIRE(value.has_value());
    CHECK(*value == "value_one");
    restarted.Commit(reader);

    char* page_data = restarted.GetBufferPoolManager()->FetchPage(0);
    minidb::Page page(page_data);
    CHECK(page.GetHeader().slot_count >= 1);
    restarted.GetBufferPoolManager()->UnpinPage(0);

    RemoveFiles(db_path);
}

TEST_CASE("Force-WAL commit persists before restart") {
    const std::string db_path = TempDbPath("force_wal");
    RemoveFiles(db_path);

    minidb::TxID tx1 = 0;
    minidb::TxID tx2 = 0;
    {
        minidb::TransactionManager tm(db_path);
        tx1 = tm.Begin();
        tm.Insert(tx1, "k1", "v1");
        tm.Commit(tx1);

        tx2 = tm.Begin();
        tm.Insert(tx2, "k2", "v2");
        tm.Commit(tx2);
    }

    minidb::TransactionManager restarted(db_path);
    const minidb::TxID reader = restarted.Begin();
    CHECK(restarted.Read(reader, "k1") == std::optional<std::string>("v1"));
    CHECK(restarted.Read(reader, "k2") == std::optional<std::string>("v2"));
    CHECK(restarted.IsCommitted(tx1));
    CHECK(restarted.IsCommitted(tx2));
    restarted.Commit(reader);

    RemoveFiles(db_path);
}

TEST_CASE("Recovery successfully handles zeroed heap pages by reinitializing them") {
    const std::string db_path = TempDbPath("zeroed_page");
    RemoveFiles(db_path);

    {
        minidb::TransactionManager tm(db_path);
        const minidb::TxID tx = tm.Begin();
        tm.Insert(tx, "k1", "v1");
        tm.Commit(tx);
        tm.FlushRecoveryState();
    }

    // Zero-out page 0 on disk to simulate un-flushed page before crash
    std::FILE* f = std::fopen(db_path.c_str(), "r+b");
    REQUIRE(f != nullptr);
    std::vector<char> zeros(minidb::PAGE_SIZE, 0);
    std::fwrite(zeros.data(), 1, minidb::PAGE_SIZE, f);
    std::fclose(f);

    // Restart, which will trigger recovery and reinitialize the page during redo
    minidb::TransactionManager restarted(db_path);
    const minidb::TxID reader = restarted.Begin();
    const auto value = restarted.Read(reader, "k1");
    REQUIRE(value.has_value());
    CHECK(*value == "v1");
    restarted.Commit(reader);

    RemoveFiles(db_path);
}
