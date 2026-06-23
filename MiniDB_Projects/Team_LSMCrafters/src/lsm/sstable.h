#pragma once
#include <string>
#include "lsm/lsm_types.h"

namespace minidb {

// Summary of one on-disk SSTable. An SSTable file is laid out as:
//   [HEADER: magic(4) version(4)]
//   [DATA: records sorted ascending by key -> key(8) seq(8) type(1) vlen(4) value]
//   [SPARSE INDEX: every kIndexStride records -> key(8) fileOffset(8)]
//   [FOOTER (fixed): indexOffset(8) indexCount(8) count(8)
//                    minKey(8) maxKey(8) maxSeq(8) magic(4)]
// The footer is read first (from the end of the file) to locate the index.
struct SSTableMeta {
  std::string path;
  Key         min_key   = 0;
  Key         max_key   = 0;
  SeqNo       max_seq    = 0;
  uint64_t    count      = 0;
  uint64_t    file_bytes = 0;
};

constexpr int kSstFooterBytes = 8 * 6 + 4;  // six 64-bit fields + magic

}  // namespace minidb
