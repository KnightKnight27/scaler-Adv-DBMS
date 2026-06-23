#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "index/btree.h"
#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"

#include <cstdio>
#include <string>

namespace {

minidb::RecordId MakeRid(minidb::page_id_t page_id, uint16_t slot_id) {
    minidb::RecordId rid{};
    rid.page_id = page_id;
    rid.slot_id = slot_id;
    return rid;
}

}  // namespace

TEST_CASE("debug delete bisect") {
    constexpr int kDegree = 32;

    int fail_at = -1;
    for (int count : {500, 1000, 2000, 5000, 10000, 20000, 50000}) {
        const std::string path = "/tmp/minidb_debug_btree_" + std::to_string(count) + ".db";
        std::remove(path.c_str());

        minidb::DiskManager count_disk(path);
        minidb::BufferPoolManager count_pool(&count_disk, 512);
        minidb::BTree t(&count_pool, minidb::BTREE_META_PAGE_ID, kDegree);
        for (int i = 0; i < count; ++i) {
            const int64_t key = static_cast<int64_t>(i * 2);
            REQUIRE(t.Insert(key, MakeRid(static_cast<minidb::page_id_t>(i), 0)));
        }

        bool ok = true;
        for (int i = 0; i < count; i += 3) {
            const int64_t key = static_cast<int64_t>(i * 2);
            if (!t.Remove(key)) {
                ok = false;
                fail_at = count;
                MESSAGE("Remove failed at count=", count, " key=", key, " i=", i);
                break;
            }
        }

        if (!ok) {
            break;
        }

        minidb::RecordId rid{};
        for (int i = 0; i < count; ++i) {
            const int64_t key = static_cast<int64_t>(i * 2);
            const bool should_exist = (i % 3) != 0;
            if (t.Search(key, &rid) != should_exist) {
                ok = false;
                fail_at = count;
                MESSAGE("Search mismatch at count=", count, " key=", key,
                        " should_exist=", should_exist);
                break;
            }
        }

        if (!ok) {
            break;
        }
        std::remove(path.c_str());
    }

    CHECK(fail_at == -1);
}
