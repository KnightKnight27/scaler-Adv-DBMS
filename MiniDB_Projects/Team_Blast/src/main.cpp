#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <fcntl.h>
#include <unistd.h>

#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "index/bplus_tree.h"
#include "query/parser.h"
#include "query/executor.h"
#include "transaction/lock_manager.h"
#include "transaction/tx_manager.h"
#include "recovery/wal.h"
#include "recovery/recovery.h"
#include "replication/primary.h"
#include "replication/replica.h"

// ─── Database file paths ──────────────────────────────────────────────────────
constexpr const char* DB_FILE  = "minidb.db";
constexpr const char* WAL_FILE = "minidb.wal";
constexpr const char* REPL_LOG = "minidb_replication.log";

constexpr const char* REPLICA_DB_FILE  = "minidb_replica.db";
constexpr const char* REPLICA_WAL_FILE = "minidb_replica.wal";

// ─── printBanner ─────────────────────────────────────────────────────────────

void printBanner() {
    std::cout << R"(
 ███╗   ███╗██╗███╗   ██╗██╗██████╗ ██████╗
 ████╗ ████║██║████╗  ██║██║██╔══██╗██╔══██╗
 ██╔████╔██║██║██╔██╗ ██║██║██║  ██║██████╔╝
 ██║╚██╔╝██║██║██║╚██╗██║██║██║  ██║██╔══██╗
 ██║ ╚═╝ ██║██║██║ ╚████║██║██████╔╝██████╔╝
 ╚═╝     ╚═╝╚═╝╚═╝  ╚═══╝╚═╝╚═════╝ ╚═════╝

 Advanced DBMS Capstone Project — Team Blast
 Type HELP for available commands. Type QUIT to exit.
 ─────────────────────────────────────────────────
)";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    printBanner();

    bool is_replica = false;
    if (argc > 1 && std::string(argv[1]) == "--replica") {
        is_replica = true;
    }

    const char* db_path = is_replica ? REPLICA_DB_FILE : DB_FILE;
    const char* wal_path = is_replica ? REPLICA_WAL_FILE : WAL_FILE;

    // ── 1. Open or create the database file ──────────────────────────────────
    DiskManager disk;
    bool db_exists = disk.open(db_path);
    if (!db_exists) {
        std::cout << "[MiniDB] Creating new database: " << db_path << "\n";
        if (!disk.create(db_path)) {
            std::cerr << "[MiniDB] FATAL: cannot create database file.\n";
            return 1;
        }
    } else {
        std::cout << "[MiniDB] Opened existing database: " << db_path
                  << " (" << disk.pageCount() << " pages)\n";
    }

    // ── 2. Initialize buffer pool ─────────────────────────────────────────────
    BufferPool bp(disk);

    // ── 3. Initialize transaction layer ──────────────────────────────────────
    LockManager lm;
    TxManager   txm(lm);

    // ── 4. Open WAL ───────────────────────────────────────────────────────────
    WAL wal;
    if (!wal.open(wal_path)) {
        std::cerr << "[MiniDB] FATAL: cannot open WAL file.\n";
        return 1;
    }

    // ── 5. Initialize executor ────────────────────────────────────────────────
    Executor executor(bp, wal, txm);

    // ── 6. Run crash recovery from WAL ────────────────────────────────────────
    if (db_exists) {
        std::cout << "[MiniDB] Running crash recovery...\n";
        auto result = Recovery::runRedo(wal_path,
            [&](const std::string& table_name)
                -> std::pair<HeapFile*, BPlusTree*>
            {
                // Create table if not present (recovery mode)
                if (!executor.tableExists(table_name)) {
                    ParsedQuery create_q;
                    create_q.type   = CmdType::CREATE_TABLE;
                    create_q.table1 = table_name;
                    create_q.valid  = true;
                    executor.execute(create_q);
                }

                // Retrieve the actual table pointers from executor
                return executor.getTablePointers(table_name);
            }
        );
        Recovery::printResult(result);
    }

    // ── 7. Initialize replication (Primary or Replica side) ───────────────────
    Primary primary;
    Replica replica;
    std::atomic<bool> running{true};
    std::thread poll_thread;

    if (!is_replica) {
        primary.open(REPL_LOG);
    } else {
        std::cout << "[Replica] Starting replica node polling " << REPL_LOG << "...\n";
        // Create replica log if it doesn't exist to prevent open errors on replica startup
        int log_fd = ::open(REPL_LOG, O_CREAT | O_WRONLY, 0644);
        if (log_fd >= 0) ::close(log_fd);

        if (!replica.open(REPL_LOG, [&](const std::string& table_name) -> std::pair<HeapFile*, BPlusTree*> {
            if (!executor.tableExists(table_name)) {
                ParsedQuery create_q;
                create_q.type   = CmdType::CREATE_TABLE;
                create_q.table1 = table_name;
                create_q.valid  = true;
                executor.execute(create_q);
            }
            return executor.getTablePointers(table_name);
        })) {
            std::cerr << "[Replica] WARNING: replication log not available yet. Retrying in background.\n";
        }

        poll_thread = std::thread([&]() {
            while (running) {
                if (replica.replicaLSN() == 0) {
                    // Try to reopen if it wasn't available at startup
                    replica.open(REPL_LOG, [&](const std::string& table_name) -> std::pair<HeapFile*, BPlusTree*> {
                        if (!executor.tableExists(table_name)) {
                            ParsedQuery create_q;
                            create_q.type   = CmdType::CREATE_TABLE;
                            create_q.table1 = table_name;
                            create_q.valid  = true;
                            executor.execute(create_q);
                        }
                        return executor.getTablePointers(table_name);
                    });
                }
                replica.applyNewRecords();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });
    }

    // ── 8. REPL ───────────────────────────────────────────────────────────────
    Parser parser;
    std::string line;

    while (true) {
        std::cout << (is_replica ? "minidb-replica> " : "minidb> ");
        if (!std::getline(std::cin, line)) break;

        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        size_t end = line.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) line = line.substr(0, end + 1);

        if (line.empty()) continue;

        ParsedQuery q = parser.parse(line);

        if (q.type == CmdType::QUIT) {
            if (!is_replica) {
                // Ship any remaining WAL records to the replica before exiting
                primary.shipLog(wal_path, primary.shippedLSN());
            }
            break;
        }

        if (is_replica) {
            // Block write commands
            if (q.type == CmdType::INSERT || q.type == CmdType::DELETE_KEY ||
                q.type == CmdType::BEGIN || q.type == CmdType::COMMIT || q.type == CmdType::ABORT) {
                std::cout << "ERROR: Replica is read-only. All writes must go to the Primary.\n";
                continue;
            }
        }

        executor.execute(q);

        if (!is_replica) {
            // After every committed write, ship new WAL records to replica
            primary.shipLog(wal_path, primary.shippedLSN());
        }
    }

    // ── 9. Cleanup ────────────────────────────────────────────────────────────
    running = false;
    if (poll_thread.joinable()) {
        poll_thread.join();
    }

    bp.flushAll();
    wal.close();
    if (!is_replica) {
        primary.close();
    } else {
        replica.close();
    }
    disk.close();

    std::cout << (is_replica ? "[Replica] Database closed. Goodbye.\n" : "[MiniDB] Database closed. Goodbye.\n");
    return 0;
}
