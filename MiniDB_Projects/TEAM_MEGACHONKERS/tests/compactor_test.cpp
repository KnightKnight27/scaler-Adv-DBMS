#include <gtest/gtest.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "storage/lsm/compactor.h"
#include "storage/lsm/sstable_writer.h"
#include "storage/lsm/sstable_reader.h"
#include "common/types.h"

using namespace minidb;

class CompactorTest : public ::testing::Test {
protected:
    std::vector<std::string> test_files = {
        "test_sst_1.sst", 
        "test_sst_2.sst", 
        "test_sst_3.sst", 
        "merged_output.sst"
    };

    void TearDown() override {
        // Clean up any files created during tests to ensure isolated runs
        for (const auto& file : test_files) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    }
};

TEST_F(CompactorTest, MergeAndPurgeTombstones) {
    // 1. Create SSTable 1 (Oldest Data)
    std::map<std::string, std::string> data1 = {
        {"key1", "val1_old"},
        {"key2", "val2_old"},
        {"key3", "val3_active"}
    };
    SSTableWriter::WriteSSTable(test_files[0], data1);

    // 2. Create SSTable 2 (Middle Data)
    std::map<std::string, std::string> data2 = {
        {"key1", "val1_new"}, // Overwrites key1
        {"key2", ""}          // Tombstones (deletes) key2
    };
    SSTableWriter::WriteSSTable(test_files[1], data2);

    // 3. Create SSTable 3 (Newest Data)
    std::map<std::string, std::string> data3 = {
        {"key4", "val4_newest"} // Brand new key
    };
    SSTableWriter::WriteSSTable(test_files[2], data3);

    // 4. Run the Compactor
    // We pass them in chronological order: [Oldest, Middle, Newest]
    std::vector<std::string> inputs = {test_files[0], test_files[1], test_files[2]};
    std::string output = test_files[3];
    
    bool success = Compactor::Compact(inputs, output);
    EXPECT_TRUE(success);

    // 5. Verify the atomic cleanup successfully deleted the old input files
    EXPECT_FALSE(std::filesystem::exists(test_files[0]));
    EXPECT_FALSE(std::filesystem::exists(test_files[1]));
    EXPECT_FALSE(std::filesystem::exists(test_files[2]));

    // 6. Verify the new unified output file exists
    EXPECT_TRUE(std::filesystem::exists(output));

    // 7. Read the output file and verify the merged data constraints
    auto merged_data = SSTableReader::ReadAll(output);
    
    // Constraint 1: key1 should have the updated value from the newer SSTable
    EXPECT_EQ(merged_data["key1"], "val1_new");
    
    // Constraint 2: key2 should be completely purged from the map (not even an empty string)
    EXPECT_TRUE(merged_data.find("key2") == merged_data.end());
    
    // Constraint 3: key3 should be untouched from the oldest file
    EXPECT_EQ(merged_data["key3"], "val3_active");
    
    // Constraint 4: key4 should be present from the newest file
    EXPECT_EQ(merged_data["key4"], "val4_newest");
    
    // Final Constraint: The output SSTable should contain exactly 3 keys
    EXPECT_EQ(merged_data.size(), 3);
}