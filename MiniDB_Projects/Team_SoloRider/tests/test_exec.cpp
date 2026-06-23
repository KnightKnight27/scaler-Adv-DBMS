#include <catch2/catch_test_macros.hpp>
#include "exec/seq_scan.h"
#include "exec/index_scan.h"
#include "exec/filter.h"
#include "exec/projection.h"
#include "exec/nested_loop_join.h"
#include <filesystem>

using namespace minidb;

TEST_CASE("Execution Engine: Scan and Filter", "[exec]") {
    std::string test_file = "test_exec_heap.db";
    std::filesystem::remove(test_file);
    HeapFile hf(test_file);
    
    Schema schema({
        Column("id", ColumnType::INT),
        Column("age", ColumnType::INT)
    });
    
    auto t1 = serialize_tuple(Tuple({1, 20}), schema);
    hf.insert_tuple(t1.data(), t1.size());
    auto t2 = serialize_tuple(Tuple({2, 25}), schema);
    hf.insert_tuple(t2.data(), t2.size());
    auto t3 = serialize_tuple(Tuple({3, 30}), schema);
    hf.insert_tuple(t3.data(), t3.size());

    auto scan = std::make_unique<SeqScan>(&hf, schema);
    
    auto col = std::make_unique<ColumnRef>("", "age");
    auto lit = std::make_unique<Literal>(25);
    auto bin = std::make_unique<BinaryExpr>(std::move(col), "=", std::move(lit));
    
    auto filter = std::make_unique<Filter>(std::move(scan), bin.get());
    
    filter->Open();
    Tuple out;
    REQUIRE(filter->Next(out) == true);
    REQUIRE(std::get<int>(out.get_value(0)) == 2);
    REQUIRE(filter->Next(out) == false);
    filter->Close();
    
    std::filesystem::remove(test_file);
}
