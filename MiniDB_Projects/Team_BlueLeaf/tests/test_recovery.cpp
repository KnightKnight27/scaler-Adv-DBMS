// M5 recovery test: a simulated crash (no checkpoint) followed by reopen must
// keep committed rows and drop uncommitted ones.
#include <cstdio>
#include <iostream>
#include <string>

#include "catalog/catalog.h"
#include "catalog/record.h"
#include "engine/rowstore_engine.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"

using namespace minidb;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

// The engine stack for a database, recovering from the WAL on construction.
struct DB {
    DiskManager    disk;
    BufferPool     pool;
    Catalog        cat;
    RowStoreEngine engine;
    LogManager     log;
    explicit DB(const std::string& p)
        : disk(p), pool(64, &disk), cat(&pool, p + ".cat"),
          engine(&cat, &pool, &disk), log(p + ".wal") {
        RecoveryManager::recover(&engine, log);
    }
};

int main() {
    const std::string path = "test_recovery.db";
    auto cleanup = [&] {
        std::remove(path.c_str());
        std::remove((path + ".cat").c_str());
        std::remove((path + ".wal").c_str());
    };
    cleanup();

    Schema s({{"id", ValueType::INT, 8}, {"v", ValueType::INT, 8}});
    auto enc = [&](std::int64_t id, std::int64_t v) {
        return Record::serialize(s, {id, v});
    };

    {
        DB d(path);
        d.engine.create_table("t", s, 0);
        d.engine.flush(); d.log.truncate();  // checkpoint the (empty) DDL

        // committed transaction 1: insert 1 and 2
        d.engine.put("t", 1, enc(1, 10));
        d.log.append(LogRecord{LogType::PUT, 1, "t", 1, enc(1, 10)});
        d.engine.put("t", 2, enc(2, 20));
        d.log.append(LogRecord{LogType::PUT, 1, "t", 2, enc(2, 20)});
        d.log.append(LogRecord{LogType::COMMIT, 1, "", 0, ""});
        d.log.flush();  // committed + durable in the WAL

        // uncommitted transaction 2: insert 3, no COMMIT
        d.engine.put("t", 3, enc(3, 30));
        d.log.append(LogRecord{LogType::PUT, 2, "t", 3, enc(3, 30)});
        d.log.flush();

        // crash: leave scope WITHOUT checkpoint -> dirty data pages are lost
    }

    {
        DB d(path);  // recovers from WAL
        std::string out;
        CHECK(d.engine.get("t", 1, out));   // committed -> survived (redo)
        CHECK(d.engine.get("t", 2, out));
        CHECK(!d.engine.get("t", 3, out));  // uncommitted -> rolled back
    }

    cleanup();
    if (g_failures == 0) { std::cout << "ALL RECOVERY TESTS PASSED\n"; return 0; }
    std::cerr << g_failures << " CHECK(s) FAILED\n";
    return 1;
}
