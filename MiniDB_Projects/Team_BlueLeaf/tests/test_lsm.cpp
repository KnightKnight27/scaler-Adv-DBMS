// M5 test: the LSM engine — put/get/overwrite/delete, flushes to SSTables,
// scans, and compaction (which must preserve correctness while reclaiming space).
#include <filesystem>
#include <iostream>
#include <string>

#include "catalog/schema.h"
#include "engine/lsm/lsm_engine.h"

using namespace minidb;
namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    const std::string dir = "test_lsm_dir";
    fs::remove_all(dir);

    // Small memtable + low compaction trigger so flushes and compaction actually happen.
    LsmEngine eng(dir, /*memtable_limit=*/4096, /*compaction_trigger=*/4);
    eng.create_table("t", Schema(std::vector<Column>{}), 0);

    const int N = 5000;
    std::string payload(40, 'x');
    for (int i = 1; i <= N; ++i) eng.put("t", i, payload + std::to_string(i));

    // every key is found with its value
    bool all_found = true;
    for (int i = 1; i <= N; ++i) {
        std::string out;
        if (!eng.get("t", i, out) || out != payload + std::to_string(i)) { all_found = false; break; }
    }
    CHECK(all_found);

    // overwrite: newest write wins
    eng.put("t", 100, "OVERWRITTEN");
    std::string out;
    CHECK(eng.get("t", 100, out) && out == "OVERWRITTEN");

    // delete: tombstone hides the key
    eng.erase("t", 200);
    CHECK(!eng.get("t", 200, out));

    // scan returns the live set in key order
    auto count_scan = [&] {
        auto cur = eng.scan("t");
        std::int64_t k; std::string r; std::int64_t prev = 0; int n = 0; bool ordered = true;
        while (cur->next(k, r)) { if (k <= prev) ordered = false; prev = k; ++n; }
        CHECK(ordered);
        return n;
    };
    CHECK(count_scan() == N - 1);  // one key deleted

    // range scan
    {
        auto cur = eng.range("t", 10, 20);
        std::int64_t k; std::string r; int n = 0;
        while (cur->next(k, r)) ++n;
        CHECK(n == 11);  // 10..20 inclusive
    }

    // Create many dead versions (overwrite keys 1000..2000, avoiding 100/200),
    // then show compaction reclaims that space while preserving correctness.
    std::string payload2(60, 'y');
    for (int i = 1000; i <= 2000; ++i) eng.put("t", i, payload2 + std::to_string(i));
    eng.flush();                       // all data now on disk (incl. dead versions)
    EngineStats before = eng.stats("t");
    eng.compact("t");                  // merge runs, drop dead versions + the tombstone
    EngineStats after = eng.stats("t");

    CHECK(after.live_rows == static_cast<std::uint64_t>(N - 1));   // 200 still deleted
    CHECK(eng.get("t", 100, out) && out == "OVERWRITTEN");        // earlier overwrite survives
    CHECK(eng.get("t", 1500, out) && out == payload2 + "1500");   // newest version wins
    CHECK(!eng.get("t", 200, out));                               // still deleted after compaction
    CHECK(after.bytes_on_disk < before.bytes_on_disk);            // dead versions reclaimed

    fs::remove_all(dir);
    if (g_failures == 0) {
        std::cout << "ALL LSM TESTS PASSED (N=" << N << ", on-disk before/after compaction = "
                  << before.bytes_on_disk << "/" << after.bytes_on_disk << " bytes)\n";
        return 0;
    }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
