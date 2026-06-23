#include "demo/demo_runner.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "recovery/recovery_manager.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "transaction/txn_manager.h"

#include <filesystem>
#include <iostream>
#include <thread>

namespace minidb {

Schema StudentSchema() {
    return {{"id", ColumnType::INT}, {"name", ColumnType::STRING},
            {"age", ColumnType::INT}, {"gpa", ColumnType::DOUBLE}};
}

Schema CourseSchema() {
    return {{"id", ColumnType::INT}, {"student_id", ColumnType::INT},
            {"course_name", ColumnType::STRING}};
}

static void PrintRows(const std::vector<Row>& rows) {
    for (const Row& row : rows) {
        std::cout << "  ";
        for (const Value& value : row) {
            if (std::holds_alternative<int64_t>(value))
                std::cout << std::get<int64_t>(value);
            else if (std::holds_alternative<double>(value))
                std::cout << std::get<double>(value);
            else
                std::cout << std::get<std::string>(value);
            std::cout << "  ";
        }
        std::cout << '\n';
    }
    std::cout << "  (" << rows.size() << " rows)\n";
}

static void DemoDeadlock(TransactionManager& txns, LockManager& locks) {
    std::cout << "\n[Deadlock demo]\n";
    Transaction* first = txns.Begin();
    Transaction* second = txns.Begin();
    txn_id_t first_id = first->GetTxnId(), second_id = second->GetTxnId();
    locks.LockExclusive(first, "students");
    locks.LockExclusive(second, "courses");
    bool first_cross = true, second_cross = true;
    std::thread a([&] { first_cross = locks.LockExclusive(first, "courses"); });
    std::thread b([&] { second_cross = locks.LockExclusive(second, "students"); });
    a.join(); b.join();
    std::cout << "  TXN " << first_id << " cross-lock: "
              << (first_cross ? "granted" : "timed out") << '\n';
    std::cout << "  TXN " << second_id << " cross-lock: "
              << (second_cross ? "granted" : "timed out") << '\n';
    txns.Abort(first);
    txns.Abort(second);
}

static void DemoRecovery(WALManager& wal) {
    std::cout << "\n[Crash recovery demo]\n";
    std::filesystem::remove("recovered_students.db");
    std::filesystem::remove("recovered_courses.db");
    DiskManager student_disk("recovered_students.db");
    DiskManager course_disk("recovered_courses.db");
    BufferPoolManager student_pool(16, &student_disk);
    BufferPoolManager course_pool(16, &course_disk);
    Catalog recovered;
    recovered.CreateTable("students", StudentSchema(), &student_pool);
    recovered.CreateTable("courses", CourseSchema(), &course_pool);
    RecoveryResult result = RecoveryManager(&recovered, &wal).Recover();
    std::cout << "  REDO applied: " << result.redone
              << ", uncommitted ignored: " << result.ignored << '\n';
    std::cout << "  Recovered students: "
              << recovered.GetTable("students")->GetTupleCount() << '\n';
    std::cout << "  Recovered courses: "
              << recovered.GetTable("courses")->GetTupleCount() << '\n';
}

void RunDemo(Executor& executor, Catalog&, TransactionManager& txns,
             LockManager& locks, WALManager& wal) {
    std::cout << "\n[Transactional inserts]\n";
    Transaction* load = txns.Begin();
    for (const char* sql : {
        "INSERT INTO students VALUES (1, 'Alice', 22, 3.8)",
        "INSERT INTO students VALUES (2, 'Bob', 23, 3.5)",
        "INSERT INTO students VALUES (3, 'Charlie', 21, 3.9)",
        "INSERT INTO students VALUES (4, 'Diana', 24, 3.2)",
        "INSERT INTO students VALUES (5, 'Eve', 22, 3.7)",
        "INSERT INTO students VALUES (6, 'Frank', 25, 2.9)",
        "INSERT INTO students VALUES (7, 'Grace', 21, 3.6)",
        "INSERT INTO students VALUES (8, 'Hank', 23, 3.1)"}) {
        executor.Execute(sql, load);
    }
    for (const char* sql : {
        "INSERT INTO courses VALUES (1, 1, 'Databases')",
        "INSERT INTO courses VALUES (2, 1, 'Algorithms')",
        "INSERT INTO courses VALUES (3, 2, 'Databases')"}) {
        executor.Execute(sql, load);
    }
    txns.Commit(load);

    std::cout << "\n[Index and WHERE]\n";
    PrintRows(executor.Execute("SELECT * FROM students WHERE id = 3"));
    std::cout << "\n[JOIN]\n";
    PrintRows(executor.Execute(
        "SELECT * FROM students JOIN courses ON students.id = courses.student_id"));

    Transaction* aborted = txns.Begin();
    executor.Execute("INSERT INTO students VALUES (99, 'NotCommitted', 20, 2.0)",
                     aborted);
    txns.Abort(aborted);
    std::cout << "\n[Abort demo] Student 99 rows: "
              << executor.Execute("SELECT * FROM students WHERE id = 99").size() << '\n';

    executor.Execute("DELETE FROM students WHERE id = 4");
    DemoDeadlock(txns, locks);
    DemoRecovery(wal);
    std::cout << "\nDemo complete.\n";
}

void RunTerminal(Executor& executor, Catalog& catalog) {
    std::cout << "\nSQL terminal (type quit, /tables, or /schema TABLE)\n";
    for (std::string input;;) {
        std::cout << "minidb> ";
        if (!std::getline(std::cin, input) || input == "quit" || input == "exit") break;
        if (input == "/tables") {
            for (const auto& name : catalog.GetTableNames()) std::cout << "  " << name << '\n';
            continue;
        }
        if (input.rfind("/schema ", 0) == 0) {
            TableInfo* table = catalog.GetTable(input.substr(8));
            if (!table) std::cout << "  table not found\n";
            else for (const auto& column : table->schema) std::cout << "  " << column.name << '\n';
            continue;
        }
        if (input.empty()) continue;
        try {
            auto rows = executor.Execute(input);
            if (!rows.empty()) PrintRows(rows); else std::cout << "  OK\n";
        } catch (const std::exception& error) {
            std::cout << "  Error: " << error.what() << '\n';
        }
    }
}

} // namespace minidb
