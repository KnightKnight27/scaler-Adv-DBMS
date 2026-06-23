#include <gtest/gtest.h>
#include <filesystem>
#include "storage/lsm/memtable.h"
#include "storage/lsm/sstable_writer.h"
#include "storage/lsm/sstable_reader.h"
#include "recovery/wal.h"

using namespace minidb;

class StorageEngineTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Clean up any files created during tests
        std::filesystem::remove("test_sstable.sst");
        std::filesystem::remove("test_wal.log");
    }
};

TEST_F(StorageEngineTest, MemTablePutAndGet) {
    MemTable memtable;
    InternalKey key{1, "row_1"};
    Row row{{"1", "Alice"}};

    memtable.Put(key, row);

    Row retrieved_row;
    SearchResult result = memtable.Get(key, &retrieved_row);

    EXPECT_EQ(result, SearchResult::FOUND);
    EXPECT_EQ(retrieved_row.columns[0], "1");
    EXPECT_EQ(retrieved_row.columns[1], "Alice");
}

TEST_F(StorageEngineTest, MemTableTombstoneDeletion) {
    MemTable memtable;
    InternalKey key{1, "row_1"};
    Row row{{"1", "Alice"}};

    memtable.Put(key, row);
    memtable.Delete(key); // Inserts a tombstone

    Row retrieved_row;
    SearchResult result = memtable.Get(key, &retrieved_row);

    // It should explicitly return DELETED, not NOT_FOUND
    EXPECT_EQ(result, SearchResult::DELETED); 
}

TEST_F(StorageEngineTest, SSTableFlushAndRead) {
    MemTable memtable;
    InternalKey key1{1, "row_1"};
    Row row1{{"1", "Alice"}};
    
    InternalKey key2{1, "row_2"};
    Row row2{{"2", "Bob"}};

    memtable.Put(key1, row1);
    memtable.Put(key2, row2);

    // Flush to disk
    std::string file_path = "test_sstable.sst";
    bool write_success = SSTableWriter::WriteSSTable(file_path, memtable.GetAllEntries());
    EXPECT_TRUE(write_success);

    // Read from disk
    Row retrieved_row;
    SearchResult result = SSTableReader::FindKey(file_path, key2, &retrieved_row);
    
    EXPECT_EQ(result, SearchResult::FOUND);
    EXPECT_EQ(retrieved_row.columns[1], "Bob");
}

TEST_F(StorageEngineTest, WALAppend) {
    WAL wal("test_wal.log");
    lsn_t lsn1 = wal.Append(LogRecordType::PUT, "key1", "val1");
    lsn_t lsn2 = wal.Append(LogRecordType::PUT, "key2", "val2");

    EXPECT_EQ(lsn1, 1);
    EXPECT_EQ(lsn2, 2);
    EXPECT_TRUE(std::filesystem::exists("test_wal.log"));
}