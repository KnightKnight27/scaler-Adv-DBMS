#pragma once

#include <functional>
#include <optional>

#include "minidb/buffer/buffer_pool.h"

namespace minidb {

class HeapTable {
 public:
  HeapTable(BufferPool& buffer, PageId first_page);

  static PageId Create(BufferPool& buffer);
  Rid Insert(std::string_view encoded_row);
  std::optional<std::string> Get(Rid rid);
  bool Delete(Rid rid);
  std::vector<std::pair<Rid, std::string>> Scan();

 private:
  BufferPool& buffer_;
  PageId first_page_;
};

}  // namespace minidb
