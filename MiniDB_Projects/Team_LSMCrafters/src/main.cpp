// MiniDB interactive shell. Seeds two demo tables, then runs SQL entered line
// by line. Meta-commands start with ':' (try :help). The data file is fresh on
// every launch so the demo always starts from a known state.
#include <cstdio>
#include <iostream>
#include <string>
#include "catalog/catalog.h"
#include "exec/executor.h"
#include "storage/buffer_pool.h"
#include "storage/disk_manager.h"
#include "storage/heap_table.h"
#include "storage/tuple.h"

using namespace minidb;

namespace {

void print_result(const QueryResult& result) {
  if (!result.message.empty()) { std::cout << result.message << "\n"; return; }
  if (!result.is_select) return;

  std::string header;
  for (std::size_t i = 0; i < result.columns.size(); ++i)
    header += (i ? " | " : "") + result.columns[i];
  std::cout << header << "\n" << std::string(header.size(), '-') << "\n";

  for (const Row& row : result.rows) {
    std::string line;
    for (std::size_t i = 0; i < row.size(); ++i)
      line += (i ? " | " : "") + value_to_string(row[i]);
    std::cout << line << "\n";
  }
  std::cout << "(" << result.rows.size() << " rows)\n";
}

void seed_demo_tables(Catalog& catalog, BufferPool& pool) {
  Schema students{{{"id", ColumnType::Int}, {"name", ColumnType::Text}, {"age", ColumnType::Int}}};
  Schema enroll{{{"id", ColumnType::Int}, {"student_id", ColumnType::Int}, {"course", ColumnType::Text}}};

  TableInfo& s = catalog.add_table("students", students, 0, make_heap_table(pool));
  TableInfo& e = catalog.add_table("enrollments", enroll, 0, make_heap_table(pool));

  // Enough rows to span several heap pages (demonstrates page allocation).
  const char* courses[] = {"DBMS", "OS", "Networks", "AI"};
  for (int i = 1; i <= 200; ++i) {
    Row r{static_cast<int64_t>(i), std::string("student_") + std::to_string(i),
          static_cast<int64_t>(18 + (i % 10))};
    s.storage->insert(i, serialize(r, students));
  }
  for (int i = 1; i <= 400; ++i) {
    Row r{static_cast<int64_t>(i), static_cast<int64_t>(1 + (i % 200)), courses[i % 4]};
    e.storage->insert(i, serialize(r, enroll));
  }
}

const char* kHelp =
    "MiniDB shell commands:\n"
    "  SELECT cols FROM t [JOIN t2 ON a=b] [WHERE expr]\n"
    "  INSERT INTO t VALUES (...)\n"
    "  DELETE FROM t [WHERE expr]\n"
    "  EXPLAIN <select>      show the chosen physical plan\n"
    "  :stats                buffer-pool hits / misses / evictions\n"
    "  :reset                reset buffer-pool counters\n"
    "  :help                 this message\n"
    "  :quit                 exit\n";

}  // namespace

int main() {
  const std::string db_path = "/tmp/minidb_cli.db";
  std::remove(db_path.c_str());

  DiskManager disk(db_path);
  BufferPool pool(disk);
  Catalog catalog;
  seed_demo_tables(catalog, pool);
  Executor executor(catalog);

  std::cout << "MiniDB ready. Tables: students(id,name,age), enrollments(id,student_id,course).\n"
            << "Type :help for commands.\n";

  std::string line;
  while (true) {
    std::cout << "minidb> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    if (line.empty()) continue;

    if (line == ":quit" || line == ":q") break;
    if (line == ":help") { std::cout << kHelp; continue; }
    if (line == ":stats") { pool.print_stats(); continue; }
    if (line == ":reset") { pool.reset_stats(); std::cout << "counters reset\n"; continue; }

    try {
      print_result(executor.run(line));
    } catch (const std::exception& ex) {
      std::cout << "Error: " << ex.what() << "\n";
    }
  }
  pool.flush_all();
  return 0;
}
