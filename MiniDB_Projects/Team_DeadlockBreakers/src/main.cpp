#include "catalog/catalog.h"
#include "demo/demo_runner.h"
#include "execution/executor.h"
#include "optimizer/optimizer.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/txn_manager.h"

#include <filesystem>
#include <iostream>

using namespace minidb;

int main() {
    std::cout << "MiniDB - ADBMS capstone\nRun demo or open existing data? [demo/manual]: ";
    std::string mode;
    std::getline(std::cin, mode);
    bool demo = mode != "manual";
    if (demo) {
        for (const char* file : {"students.db", "courses.db", "wal.log",
                                 "recovered_students.db", "recovered_courses.db"}) {
            std::filesystem::remove(file);
        }
    }

    DiskManager student_disk("students.db");
    DiskManager course_disk("courses.db");
    BufferPoolManager student_pool(BUFFER_POOL_SIZE, &student_disk);
    BufferPoolManager course_pool(BUFFER_POOL_SIZE, &course_disk);
    Catalog catalog;
    catalog.CreateTable("students", StudentSchema(), &student_pool);
    catalog.CreateTable("courses", CourseSchema(), &course_pool);

    WALManager wal("wal.log");
    LockManager locks;
    TransactionManager txns(&locks, &wal);
    Optimizer optimizer;
    Executor executor(&catalog, &optimizer, &txns);

    try {
        if (demo) RunDemo(executor, catalog, txns, locks, wal);
        RunTerminal(executor, catalog);
    } catch (const std::exception& error) {
        std::cerr << "MiniDB error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
