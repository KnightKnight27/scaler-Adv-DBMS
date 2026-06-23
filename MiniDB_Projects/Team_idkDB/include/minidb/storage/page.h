#pragma once

#include <array>
#include <cstddef>

#include "minidb/common/types.h"

namespace minidb {

class Page {
 public:
  PageId page_id{kInvalidPageId};
  std::array<std::byte, kPageSize> data{};
  std::size_t pin_count{0};
  bool dirty{false};
};

}  // namespace minidb
