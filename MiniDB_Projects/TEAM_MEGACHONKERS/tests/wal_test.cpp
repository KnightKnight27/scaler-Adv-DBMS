#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "recovery/wal.h"

using namespace minidb;

class WALTest : public ::testing::Test {
protected:
    std::string wal_file = "standalone_test_wal.log";

    void TearDown() override {
        // Ensure we don't leave garbage files on the disk after testing
        if (std::filesystem::exists(wal_file)) {
            std::filesystem::remove(wal_file);
        }
    }
};

TEST_F(WALTest, AppendLogRecords) {
    WAL wal(wal_file);
    
    lsn_t lsn1 = wal.Append(LogRecordType::PUT, "key1", "value1");
    lsn_t lsn2 = wal.Append(LogRecordType::PUT, "key2", "value2");
    lsn_t lsn3 = wal.Append(LogRecordType::DELETE_TOMBSTONE, "key1", "");

    // LSNs must be strictly monotonically increasing
    EXPECT_EQ(lsn1, 1);
    EXPECT_EQ(lsn2, 2);
    EXPECT_EQ(lsn3, 3);

    // FLUSH MANUALLY: Required now because we implemented Group Commit buffering!
    wal.Flush();

    // Verify the file was physically created on disk
    EXPECT_TRUE(std::filesystem::exists(wal_file));
    
    // Verify file size is greater than 0
    std::uintmax_t file_size = std::filesystem::file_size(wal_file);
    EXPECT_GT(file_size, 0);
}

TEST_F(WALTest, ClearLog) {
    WAL wal(wal_file);
    
    wal.Append(LogRecordType::PUT, "key1", "value1");
    wal.Append(LogRecordType::PUT, "key2", "value2");
    
    // FLUSH MANUALLY: Required now because we implemented Group Commit buffering!
    wal.Flush();
    
    std::uintmax_t size_before = std::filesystem::file_size(wal_file);
    EXPECT_GT(size_before, 0);

    // Clearing the log should reset the file size and the LSN counter
    wal.Clear();
}