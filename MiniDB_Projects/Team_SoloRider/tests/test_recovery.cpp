#include <catch2/catch_test_macros.hpp>
#include "recovery/wal.h"
#include "recovery/recovery_manager.h"
#include <filesystem>

using namespace minidb;

TEST_CASE("Recovery: Write-Ahead Log", "[recovery]") {
    std::string log_file = "test_wal.log";
    std::filesystem::remove(log_file);
    
    {
        WriteAheadLog wal(log_file);
        LogRecord r1{0, 1, LogRecordType::BEGIN, {INVALID_PAGE_ID, INVALID_SLOT_ID}, {}};
        LogRecord r2{0, 1, LogRecordType::INSERT, {1, 0}, {'a', 'b', 'c'}};
        LogRecord r3{0, 1, LogRecordType::COMMIT, {INVALID_PAGE_ID, INVALID_SLOT_ID}, {}};
        
        wal.append(r1);
        wal.append(r2);
        wal.append(r3);
    }
    
    {
        WriteAheadLog wal(log_file);
        auto records = wal.read_all();
        REQUIRE(records.size() == 3);
        REQUIRE(records[0].type == LogRecordType::BEGIN);
        REQUIRE(records[1].type == LogRecordType::INSERT);
        REQUIRE(records[1].tuple_data.size() == 3);
        REQUIRE(records[1].tuple_data[0] == 'a');
        REQUIRE(records[2].type == LogRecordType::COMMIT);
    }
    
    std::filesystem::remove(log_file);
}
