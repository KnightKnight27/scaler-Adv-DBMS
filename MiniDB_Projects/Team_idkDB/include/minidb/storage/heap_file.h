#pragma once

#include <optional>
#include <vector>

#include "minidb/common/types.h"
#include "minidb/storage/buffer_pool_manager.h"

namespace minidb {

class HeapFile {
 public:
  explicit HeapFile(BufferPoolManager &buffer);

  RID Insert(const Record &record);
  std::optional<Record> Read(RID rid);
  bool Delete(RID rid);
  std::vector<std::pair<RID, Record>> Scan();
  std::size_t PageCount() const;

 private:
  BufferPoolManager &buffer_;
};

}  // namespace minidb
