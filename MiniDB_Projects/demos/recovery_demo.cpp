#include <filesystem>
#include <fstream>
#include <iostream>

#include "minidb/common/trace.h"
#include "minidb/db/database.h"
#include "minidb/recovery/log_manager.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_file.h"

int main(int argc, char **argv) {
  const std::filesystem::path dir =
      argc > 1 ? std::filesystem::path(argv[1])
               : std::filesystem::temp_directory_path() / "minidb_recovery_demo";
  std::filesystem::create_directories(dir);
  minidb::Trace::SetEnabled(true);

  std::cout << "Recovery demo path: " << dir << '\n';

  {
    std::cout << "\nScenario 1: committed transaction survives restart\n";
    minidb::Database db(dir);
    db.Execute("INSERT users 1 committed");
    db.Flush();
  }
  {
    minidb::Database db(dir);
    auto result = db.Execute("SELECT users WHERE id=1");
    std::cout << "After restart: " << result.message << '\n';
  }

  {
    std::cout << "\nScenario 2: uncommitted transaction is undone after crash\n";
    std::ofstream catalog(dir / "catalog.txt", std::ios::app);
    catalog << "crash_users\n";
    catalog.close();

    minidb::LogManager log(dir / "minidb.wal");
    log.Append(999, minidb::LogType::Begin);
    log.Append(999, minidb::LogType::Insert, "crash_users", 2, {}, "uncommitted");
    log.Flush();

    minidb::DiskManager disk(dir / "crash_users.tbl");
    minidb::BufferPoolManager buffer(disk, 2);
    minidb::HeapFile heap(buffer);
    heap.Insert({2, "uncommitted"});
    buffer.FlushAll();
    std::cout << "Simulated crash before COMMIT: heap page and WAL INSERT exist, but no COMMIT.\n";
  }
  {
    minidb::Database db(dir);
    auto result = db.Execute("SELECT crash_users WHERE id=2");
    std::cout << "After recovery undo: " << result.message << '\n';
  }
  return 0;
}
