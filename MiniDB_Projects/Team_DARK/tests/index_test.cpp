#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "index/bnode.h"
#include "index/btree.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace {

std::string TempDbPath(const char* suffix) {
    return std::string("/tmp/minidb_index_test_") + suffix + "_" +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + ".db";
}

void RemoveFile(const std::string& path) {
    std::remove(path.c_str());
}

minidb::RecordId MakeRid(minidb::page_id_t page_id, uint16_t slot_id) {
    minidb::RecordId rid{};
    rid.page_id = page_id;
    rid.slot_id = slot_id;
    return rid;
}

}  // namespace

TEST_CASE("BNode packed layout fits within PAGE_SIZE") {
    const int max_degree = minidb::BNodePage::MaxSupportedDegree();
    CHECK(max_degree >= 50);
    CHECK(minidb::BNodePage::FitsOnPage(50, true));
    CHECK(minidb::BNodePage::FitsOnPage(50, false));
    CHECK(sizeof(minidb::RecordId) == 6);
}

TEST_CASE("BTree basic insert search remove") {
    const std::string db_path = TempDbPath("basic");
    RemoveFile(db_path);

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 64);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, 4);

        CHECK(tree.Insert(10, MakeRid(1, 0)));
        CHECK(tree.Insert(20, MakeRid(2, 0)));
        CHECK(tree.Insert(5, MakeRid(3, 1)));

        minidb::RecordId rid{};
        CHECK(tree.Search(10, &rid));
        CHECK(rid.page_id == 1);
        CHECK(rid.slot_id == 0);
        CHECK_FALSE(tree.Search(99, &rid));
        CHECK_FALSE(tree.Insert(10, MakeRid(9, 9)));

        CHECK(tree.Remove(10));
        CHECK_FALSE(tree.Search(10, &rid));
        CHECK_FALSE(tree.Remove(10));
    }

    RemoveFile(db_path);
}

TEST_CASE("BTree 50000 keys insert search delete") {
    const std::string db_path = TempDbPath("50k");
    RemoveFile(db_path);

    constexpr int kCount = 50000;
    constexpr int kDegree = 32;

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 512);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, kDegree);

        for (int i = 0; i < kCount; ++i) {
            const int64_t key = static_cast<int64_t>(i * 2);
            CHECK(tree.Insert(key, MakeRid(static_cast<minidb::page_id_t>(i), static_cast<uint16_t>(i % 100))));
        }

        minidb::RecordId rid{};
        for (int i = 0; i < kCount; ++i) {
            const int64_t key = static_cast<int64_t>(i * 2);
            CHECK(tree.Search(key, &rid));
            CHECK(rid.page_id == static_cast<minidb::page_id_t>(i));
        }

        for (int i = 0; i < kCount; i += 3) {
            const int64_t key = static_cast<int64_t>(i * 2);
            CHECK(tree.Remove(key));
            CHECK_FALSE(tree.Search(key, &rid));
        }

        for (int i = 0; i < kCount; ++i) {
            const int64_t key = static_cast<int64_t>(i * 2);
            const bool should_exist = (i % 3) != 0;
            CHECK(tree.Search(key, &rid) == should_exist);
        }
    }

    RemoveFile(db_path);
}

TEST_CASE("BTree range search returns sorted keys") {
    const std::string db_path = TempDbPath("range");
    RemoveFile(db_path);

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 64);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, 8);

        for (int i = 0; i < 1000; ++i) {
            const int64_t key = static_cast<int64_t>(i * 3);
            CHECK(tree.Insert(key, MakeRid(static_cast<minidb::page_id_t>(i), 0)));
        }

        const auto results = tree.RangeSearch(100, 500);
        CHECK(!results.empty());
        for (std::size_t i = 1; i < results.size(); ++i) {
            CHECK(results[i - 1].first < results[i].first);
        }

        for (const auto& entry : results) {
            CHECK(entry.first >= 100);
            CHECK(entry.first <= 500);
        }

        const auto full = tree.RangeSearch(0, 3000);
        CHECK(static_cast<int>(full.size()) == 1000);
    }

    RemoveFile(db_path);
}

TEST_CASE("BTree height stays logarithmic") {
    const std::string db_path = TempDbPath("height");
    RemoveFile(db_path);

    constexpr int kCount = 50000;
    constexpr int kDegree = 50;

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 512);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, kDegree);

        for (int i = 0; i < kCount; ++i) {
            CHECK(tree.Insert(static_cast<int64_t>(i), MakeRid(1, static_cast<uint16_t>(i % 500))));
        }

        const int height = tree.Height();
        const double expected =
            std::ceil(std::log(static_cast<double>(kCount)) / std::log(static_cast<double>(kDegree))) + 1.0;
        CHECK(height <= static_cast<int>(expected));
        CHECK(height >= 2);
    }

    RemoveFile(db_path);
}

TEST_CASE("BTree optional 100000 keys") {
    const std::string db_path = TempDbPath("100k");
    RemoveFile(db_path);

    constexpr int kCount = 100000;
    constexpr int kDegree = 64;

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 512);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, kDegree);

        for (int i = 0; i < kCount; ++i) {
            CHECK(tree.Insert(static_cast<int64_t>(i), MakeRid(1, static_cast<uint16_t>(i % 1000))));
        }

        minidb::RecordId rid{};
        for (int i = 0; i < kCount; i += 997) {
            CHECK(tree.Search(static_cast<int64_t>(i), &rid));
        }
    }

    RemoveFile(db_path);
}

TEST_CASE("BTree persists across reopen via FlushAllPages") {
    const std::string db_path = TempDbPath("persist");
    RemoveFile(db_path);

    constexpr int kDegree = 16;
    minidb::page_id_t saved_root = minidb::INVALID_PAGE_ID;

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 128);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, kDegree);

        for (int i = 0; i < 5000; ++i) {
            CHECK(tree.Insert(static_cast<int64_t>(i), MakeRid(static_cast<minidb::page_id_t>(i), 0)));
        }

        saved_root = tree.RootPageId();
        pool.FlushAllPages();
    }

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 128);
        minidb::BTree tree(&pool, minidb::BTREE_META_PAGE_ID, kDegree);

        CHECK(tree.RootPageId() == saved_root);

        minidb::RecordId rid{};
        for (int i = 0; i < 5000; ++i) {
            CHECK(tree.Search(static_cast<int64_t>(i), &rid));
            CHECK(rid.page_id == static_cast<minidb::page_id_t>(i));
        }

        CHECK(tree.Remove(1234));
        CHECK_FALSE(tree.Search(1234, &rid));
        CHECK(tree.Search(1235, &rid));
    }

    RemoveFile(db_path);
}
