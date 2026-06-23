#include <catch2/catch_test_macros.hpp>
#include "storage_lsm/memtable.h"
#include "storage_lsm/sstable.h"
#include "storage_lsm/compaction.h"
#include <filesystem>

using namespace minidb;

TEST_CASE("LSM: MemTable to SSTable", "[lsm]") {
    MemTable mem;
    mem.put(1, "val1");
    mem.put(2, "val2");
    
    std::string sst_path = "test_sst.db";
    SSTable::write_from_memtable(sst_path, mem);
    
    std::string val;
    REQUIRE(SSTable::get(sst_path, 1, val));
    REQUIRE(val == "val1");
    
    REQUIRE(SSTable::get(sst_path, 2, val));
    REQUIRE(val == "val2");
    
    REQUIRE(!SSTable::get(sst_path, 3, val));
    
    std::filesystem::remove(sst_path);
}

TEST_CASE("LSM: Compaction", "[lsm]") {
    MemTable m1, m2;
    m1.put(1, "A");
    m1.put(2, "B");
    m2.put(2, "C"); // Overrides B
    m2.put(3, "D");
    
    std::string s1 = "t1.db", s2 = "t2.db", s_out = "t_out.db";
    SSTable::write_from_memtable(s1, m1);
    SSTable::write_from_memtable(s2, m2);
    
    Compaction::compact(s1, s2, s_out);
    
    std::string val;
    REQUIRE(SSTable::get(s_out, 1, val)); REQUIRE(val == "A");
    REQUIRE(SSTable::get(s_out, 2, val)); REQUIRE(val == "C");
    REQUIRE(SSTable::get(s_out, 3, val)); REQUIRE(val == "D");
    
    std::filesystem::remove(s1);
    std::filesystem::remove(s2);
    std::filesystem::remove(s_out);
}
