// tests/test_storage.cpp — Catch2 tests for the Storage Engine (Phase 1).
//
// Coverage:
//   1. Page: insert / read / delete / compact
//   2. Tuple serialization round-trip
//   3. HeapFile: multi-page insert / read / delete
//   4. BufferPool: fetch / eviction / hit-miss stats / flush

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/types.h"
#include "storage/page.h"
#include "storage/heap_file.h"
#include "storage/buffer_pool.h"

using namespace minidb;

// ═══════════════════════════════════════════════════════════════
//  1. Page Tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Page: insert, read, delete, compact", "[page]") {
    Page page;
    page.set_page_id(0);

    // ── Insert three tuples ──
    const char* t0 = "hello";
    const char* t1 = "world!!!";
    const char* t2 = "minidb";

    slot_id_t s0 = page.insert_tuple(t0, 5);
    slot_id_t s1 = page.insert_tuple(t1, 8);
    slot_id_t s2 = page.insert_tuple(t2, 6);

    REQUIRE(s0 != INVALID_SLOT_ID);
    REQUIRE(s1 != INVALID_SLOT_ID);
    REQUIRE(s2 != INVALID_SLOT_ID);
    REQUIRE(page.get_slot_count() == 3);

    // ── Read them back ──
    char buf[64];
    uint16_t len = 0;

    REQUIRE(page.get_tuple(s0, buf, &len));
    REQUIRE(len == 5);
    REQUIRE(std::memcmp(buf, t0, 5) == 0);

    REQUIRE(page.get_tuple(s1, buf, &len));
    REQUIRE(len == 8);
    REQUIRE(std::memcmp(buf, t1, 8) == 0);

    REQUIRE(page.get_tuple(s2, buf, &len));
    REQUIRE(len == 6);
    REQUIRE(std::memcmp(buf, t2, 6) == 0);

    // ── Delete middle tuple ──
    REQUIRE(page.delete_tuple(s1));

    // Reading a tombstoned tuple should fail.
    REQUIRE_FALSE(page.get_tuple(s1, buf, &len));

    // The other two should still be readable.
    REQUIRE(page.get_tuple(s0, buf, &len));
    REQUIRE(page.get_tuple(s2, buf, &len));

    // ── Compact ──
    page.compact();

    // After compaction the remaining tuples must still be readable.
    REQUIRE(page.get_tuple(s0, buf, &len));
    REQUIRE(len == 5);
    REQUIRE(std::memcmp(buf, t0, 5) == 0);

    REQUIRE(page.get_tuple(s2, buf, &len));
    REQUIRE(len == 6);
    REQUIRE(std::memcmp(buf, t2, 6) == 0);

    // Tombstoned slot stays tombstoned.
    REQUIRE_FALSE(page.get_tuple(s1, buf, &len));
}

TEST_CASE("Page: reject insert when full", "[page]") {
    Page page;
    page.set_page_id(0);

    // Fill the page with many small tuples until it runs out of space.
    int count = 0;
    char data[32];
    std::memset(data, 'A', sizeof(data));

    while (true) {
        slot_id_t sid = page.insert_tuple(data, sizeof(data));
        if (sid == INVALID_SLOT_ID) break;
        count++;
    }
    // We should have inserted a decent number of tuples.
    REQUIRE(count > 50);  // rough lower bound for 4K page
}

// ═══════════════════════════════════════════════════════════════
//  2. Tuple Serialization Round-Trip
// ═══════════════════════════════════════════════════════════════

TEST_CASE("Tuple serialization round-trip", "[serialization]") {
    Schema schema({
        Column("id",    ColumnType::INT),
        Column("score", ColumnType::FLOAT),
        Column("name",  ColumnType::VARCHAR, 255),
        Column("active",ColumnType::BOOL),
    });

    Tuple original({
        Value(42),
        Value(3.14),
        Value(std::string("MiniDB")),
        Value(true),
    });

    std::vector<char> bytes = serialize_tuple(original, schema);
    Tuple restored = deserialize_tuple(bytes.data(),
                                       static_cast<uint16_t>(bytes.size()),
                                       schema);

    REQUIRE(restored.values.size() == 4);
    REQUIRE(std::get<int>(restored.values[0])         == 42);
    REQUIRE(std::get<double>(restored.values[1])       == 3.14);
    REQUIRE(std::get<std::string>(restored.values[2]) == "MiniDB");
    REQUIRE(std::get<bool>(restored.values[3])        == true);
}

TEST_CASE("Tuple serialization with NULL values", "[serialization]") {
    Schema schema({
        Column("id",   ColumnType::INT),
        Column("name", ColumnType::VARCHAR, 255),
    });

    // Second column is NULL.
    Tuple original({
        Value(7),
        Value(std::monostate{}),
    });

    std::vector<char> bytes = serialize_tuple(original, schema);
    Tuple restored = deserialize_tuple(bytes.data(),
                                       static_cast<uint16_t>(bytes.size()),
                                       schema);

    REQUIRE(std::get<int>(restored.values[0]) == 7);
    REQUIRE(is_null(restored.values[1]));
}

// ═══════════════════════════════════════════════════════════════
//  3. HeapFile Tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("HeapFile: insert, read, delete across pages", "[heapfile]") {
    const std::string path = "/tmp/minidb_test_heapfile_" +
                             std::to_string(std::hash<std::string>{}("heapfile")) +
                             ".db";
    // Ensure clean slate.
    std::filesystem::remove(path);

    {
        HeapFile hf(path);
        REQUIRE(hf.get_num_pages() == 0);

        // Insert enough tuples to span multiple pages.
        // Each tuple is ~200 bytes so ~20 per page.
        std::vector<RecordId> rids;
        char data[200];
        for (int i = 0; i < 60; ++i) {
            std::memset(data, static_cast<char>('A' + (i % 26)), sizeof(data));
            // Stamp the index into the first 4 bytes for later verification.
            std::memcpy(data, &i, sizeof(int));
            RecordId rid = hf.insert_tuple(data, sizeof(data));
            REQUIRE(rid.is_valid());
            rids.push_back(rid);
        }

        REQUIRE(hf.get_num_pages() >= 3);

        // Read them all back and verify.
        for (int i = 0; i < 60; ++i) {
            char buf[200];
            uint16_t len = 0;
            REQUIRE(hf.get_tuple(rids[i], buf, &len));
            REQUIRE(len == 200);
            int stored;
            std::memcpy(&stored, buf, sizeof(int));
            REQUIRE(stored == i);
        }

        // Delete a few and verify.
        REQUIRE(hf.delete_tuple(rids[10]));
        REQUIRE(hf.delete_tuple(rids[30]));
        REQUIRE(hf.delete_tuple(rids[50]));

        char buf[200];
        uint16_t len = 0;
        REQUIRE_FALSE(hf.get_tuple(rids[10], buf, &len));
        REQUIRE_FALSE(hf.get_tuple(rids[30], buf, &len));
        REQUIRE_FALSE(hf.get_tuple(rids[50], buf, &len));

        // Others still readable.
        REQUIRE(hf.get_tuple(rids[0], buf, &len));
        REQUIRE(hf.get_tuple(rids[59], buf, &len));
    }

    // Reopen the file and verify persistence.
    {
        HeapFile hf(path);
        REQUIRE(hf.get_num_pages() >= 3);
    }

    std::filesystem::remove(path);
}

// ═══════════════════════════════════════════════════════════════
//  4. BufferPool Tests
// ═══════════════════════════════════════════════════════════════

TEST_CASE("BufferPool: fetch, eviction, hit/miss, flush", "[bufferpool]") {
    const std::string path = "/tmp/minidb_test_bufferpool_" +
                             std::to_string(std::hash<std::string>{}("bpool")) +
                             ".db";
    std::filesystem::remove(path);

    {
        HeapFile hf(path);

        // Pre-allocate 5 pages and write a marker into each.
        for (int i = 0; i < 5; ++i) {
            page_id_t pid = hf.allocate_page();
            Page page;
            hf.read_page(pid, page);
            page.set_page_id(pid);
            // Write a small marker tuple: 4 bytes containing the page index.
            page.insert_tuple(reinterpret_cast<const char*>(&i), sizeof(int));
            hf.write_page(pid, page);
        }

        // Create a buffer pool with only 3 frames (K=2).
        BufferPool bp(&hf, 3, 2);

        // Fetch pages 0, 1, 2 — all misses.
        Page* p0 = bp.fetch_page(0);
        REQUIRE(p0 != nullptr);
        bp.unpin_page(0, false);

        Page* p1 = bp.fetch_page(1);
        REQUIRE(p1 != nullptr);
        bp.unpin_page(1, false);

        Page* p2 = bp.fetch_page(2);
        REQUIRE(p2 != nullptr);
        bp.unpin_page(2, false);

        REQUIRE(bp.get_miss_count() == 3);
        REQUIRE(bp.get_hit_count()  == 0);

        // Fetch page 0 again — should be a hit.
        p0 = bp.fetch_page(0);
        bp.unpin_page(0, false);
        REQUIRE(bp.get_hit_count() == 1);

        // Fetch pages 3, 4 — forces evictions of the least-recently used.
        Page* p3 = bp.fetch_page(3);
        REQUIRE(p3 != nullptr);
        bp.unpin_page(3, false);

        Page* p4 = bp.fetch_page(4);
        REQUIRE(p4 != nullptr);
        bp.unpin_page(4, false);

        REQUIRE(bp.get_miss_count() == 5);

        // ── Dirty page flush ──
        // Fetch page 3, mark dirty, flush.
        p3 = bp.fetch_page(3);
        // Modify something on the page.
        int marker = 999;
        p3->insert_tuple(reinterpret_cast<const char*>(&marker), sizeof(int));
        bp.unpin_page(3, true);
        bp.flush_page(3);

        // Verify the marker persists by reading from disk directly.
        Page verify;
        hf.read_page(3, verify);
        char tbuf[16];
        uint16_t tlen = 0;
        // Slot 1 should be the new marker (slot 0 was the original).
        REQUIRE(verify.get_tuple(1, tbuf, &tlen));
        int read_marker;
        std::memcpy(&read_marker, tbuf, sizeof(int));
        REQUIRE(read_marker == 999);
    }

    std::filesystem::remove(path);
}

TEST_CASE("BufferPool: eviction throws when all pinned", "[bufferpool]") {
    const std::string path = "/tmp/minidb_test_bp_pinned_" +
                             std::to_string(std::hash<std::string>{}("pinned")) +
                             ".db";
    std::filesystem::remove(path);

    {
        HeapFile hf(path);
        for (int i = 0; i < 4; ++i) {
            hf.allocate_page();
        }

        BufferPool bp(&hf, 2, 2);

        // Pin two pages, don't unpin.
        bp.fetch_page(0);
        bp.fetch_page(1);

        // Fetching a third page should throw — all frames are pinned.
        REQUIRE_THROWS_AS(bp.fetch_page(2), std::runtime_error);
    }

    std::filesystem::remove(path);
}
