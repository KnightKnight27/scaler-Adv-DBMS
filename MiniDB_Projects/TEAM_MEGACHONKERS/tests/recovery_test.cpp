#include <gtest/gtest.h>
#include <filesystem>
#include "catalog/catalog.h"
#include "recovery/recovery_manager.h"

using namespace minidb;

class RecoveryTest : public ::testing::Test {
protected:
    Catalog catalog;

    void TearDown() override {
        // Clean up log files after the test
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            if (entry.path().extension() == ".log") {
                std::filesystem::remove(entry.path());
            }
        }
    }
};

TEST_F(RecoveryTest, CrashAndRecover) {
    Schema schema({
        Column("id", TypeId::INTEGER, 4), 
        Column("name", TypeId::VARCHAR, 255)
    });
    TableMetadata* table = catalog.CreateTable("users", schema);

    InternalKey key1{table->oid, "1"};
    InternalKey key2{table->oid, "2"};
    Row row1{{"1", "Alice"}};
    Row row2{{"2", "Bob"}};

    // 1. Simulate Normal Database Operation (Writes to WAL & MemTable)
    table->wal->Append(LogRecordType::PUT, key1.Encode(), row1.Serialize());
    table->memtable->Put(key1, row1);

    table->wal->Append(LogRecordType::PUT, key2.Encode(), row2.Serialize());
    table->memtable->Put(key2, row2);

    // Delete row 1
    table->wal->Append(LogRecordType::DELETE_TOMBSTONE, key1.Encode(), "");
    table->memtable->Delete(key1);

    // Flush to disk
    table->wal->Flush();

    // 2. SIMULATE SYSTEM CRASH! 
    table->memtable->Clear();
    
    std::string original_wal = "wal_" + std::to_string(table->oid) + ".log";
    
    // In a real crash, the OS forcefully reclaims all open file descriptors.
    // We simulate this cleanly by destroying the WAL object. No OS file copying hacks.
    table->wal.reset(); 

    // Verify memory is genuinely empty
    Row dummy;
    EXPECT_EQ(table->memtable->Get(key2, &dummy), SearchResult::NOT_FOUND);

    // 3. Execute Crash Recovery on the exact same log file
    RecoveryManager::ReplayWAL(original_wal, table);

    // 4. Verify the database perfectly reconstructed its state
    Row recovered_row;
    
    // Alice was deleted before the crash, so she should still be a Tombstone
    EXPECT_EQ(table->memtable->Get(key1, &recovered_row), SearchResult::DELETED);
    
    // Bob should be successfully recovered
    ASSERT_EQ(table->memtable->Get(key2, &recovered_row), SearchResult::FOUND);
    EXPECT_EQ(recovered_row.columns[1], "Bob");
}