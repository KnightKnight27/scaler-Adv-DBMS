#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "minidb/index/b_plus_tree.h"
#include "minidb/query/optimizer.h"
#include "minidb/recovery/log_manager.h"
#include "minidb/storage/buffer_pool_manager.h"
#include "minidb/storage/disk_manager.h"
#include "minidb/storage/heap_file.h"
#include "minidb/transaction/lock_manager.h"

namespace minidb {

struct QueryResult {
  bool ok{true};
  std::string message;
  std::optional<Record> record;
  std::vector<Record> records;
  QueryPlan plan;
};

class Database {
 public:
  explicit Database(std::filesystem::path directory);
  ~Database();

  QueryResult Execute(const std::string &sql);
  std::optional<Record> Get(const std::string &table, int key);
  std::size_t RowCount(const std::string &table);
  void Flush();

 private:
  struct Table {
    explicit Table(const std::filesystem::path &path);

    DiskManager disk;
    BufferPoolManager buffer;
    HeapFile heap;
    BPlusTree index;
  };

  Table &OpenTable(const std::string &name);
  void LoadCatalog();
  void SaveCatalog() const;
  void RebuildIndex(Table &table);
  void Recover();
  void ApplyRedoInsert(const std::string &table_name, const Record &record);
  void ApplyRedoDelete(const std::string &table_name, int key);
  void UndoInsert(const std::string &table_name, int key);
  void UndoDelete(const std::string &table_name, const Record &old_record);
  void UndoWriteSet(std::vector<WriteAction> &write_set);
  TableStats Stats(const std::string &table_name);
  QueryResult ExecuteInsert(const Query &query);
  QueryResult ExecuteSelect(const Query &query);
  QueryResult ExecuteDelete(const Query &query);
  QueryResult ExecuteJoin(const Query &query);
  QueryResult ExecuteBegin(const Query &query);
  QueryResult ExecuteCommit(const Query &query);
  QueryResult ExecuteAbort(const Query &query);

  std::filesystem::path directory_;
  std::filesystem::path catalog_path_;
  LogManager log_;
  LockManager locks_;
  Optimizer optimizer_;
  std::unordered_map<std::string, std::unique_ptr<Table>> tables_;
  std::vector<std::string> table_names_;
  std::unique_ptr<Transaction> current_txn_;
};

}  // namespace minidb
