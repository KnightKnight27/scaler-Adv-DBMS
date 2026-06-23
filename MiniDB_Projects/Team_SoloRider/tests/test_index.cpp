#include <catch2/catch_test_macros.hpp>
#include "index/bplus_tree.h"
#include <vector>

using namespace minidb;

TEST_CASE("BPlusTree: Insert and Search", "[bplus_tree]") {
    BPlusTree tree(4);
    
    for (int i = 1; i <= 20; i++) {
        REQUIRE(tree.insert(i, {static_cast<page_id_t>(i), 0}));
    }
    
    for (int i = 1; i <= 20; i++) {
        RecordId rid = tree.search(i);
        REQUIRE(rid.page_id == static_cast<page_id_t>(i));
        REQUIRE(rid.slot_id == 0);
    }
    
    // Search non-existent
    RecordId rid = tree.search(99);
    REQUIRE(rid.page_id == INVALID_PAGE_ID);
    
    // Duplicate insert
    REQUIRE(tree.insert(10, {10, 0}) == false);
}

TEST_CASE("BPlusTree: Insert Descending", "[bplus_tree]") {
    BPlusTree tree(4);
    
    for (int i = 20; i >= 1; i--) {
        REQUIRE(tree.insert(i, {static_cast<page_id_t>(i), 0}));
    }
    
    for (int i = 1; i <= 20; i++) {
        RecordId rid = tree.search(i);
        REQUIRE(rid.page_id == static_cast<page_id_t>(i));
    }
}

TEST_CASE("BPlusTree: Delete Keys", "[bplus_tree]") {
    BPlusTree tree(4);
    
    for (int i = 1; i <= 20; i++) {
        tree.insert(i, {static_cast<page_id_t>(i), 0});
    }
    
    REQUIRE(tree.remove(5));
    REQUIRE(tree.remove(10));
    REQUIRE(tree.remove(15));
    
    REQUIRE(tree.search(5).page_id == INVALID_PAGE_ID);
    REQUIRE(tree.search(10).page_id == INVALID_PAGE_ID);
    REQUIRE(tree.search(15).page_id == INVALID_PAGE_ID);
    
    // Remaining still found
    REQUIRE(tree.search(4).page_id == 4);
    REQUIRE(tree.search(11).page_id == 11);
}

TEST_CASE("BPlusTree: Range Scan", "[bplus_tree]") {
    BPlusTree tree(4);
    
    for (int i = 1; i <= 100; i++) {
        tree.insert(i, {static_cast<page_id_t>(i), 0});
    }
    
    std::vector<RecordId> results = tree.range_scan(25, 50);
    REQUIRE(results.size() == 26);
    
    for (size_t i = 0; i < results.size(); i++) {
        REQUIRE(results[i].page_id == 25 + i);
    }
}

TEST_CASE("BPlusTree: Delete All", "[bplus_tree]") {
    BPlusTree tree(4);
    
    for (int i = 1; i <= 10; i++) {
        tree.insert(i, {static_cast<page_id_t>(i), 0});
    }
    
    for (int i = 1; i <= 10; i++) {
        REQUIRE(tree.remove(i));
    }
    
    REQUIRE(tree.is_empty());
}
