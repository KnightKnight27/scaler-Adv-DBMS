#pragma once
#include <cstdint>
#include "common/types.h"

namespace minidb {

using SeqNo = uint64_t;  // global, monotonically increasing version stamp

enum class RecType : uint8_t { Put = 0, Tombstone = 1 };

// One versioned value as stored in a MemTable or SSTable. The highest seq for a
// key is the live version ("newest wins"); a Tombstone marks a deletion.
struct ValueEntry {
  RecType type = RecType::Put;
  SeqNo   seq  = 0;
  Bytes   value;  // empty for a Tombstone
};

constexpr uint32_t kSstMagic   = 0x4C534D31;  // "LSM1"
constexpr int      kIndexStride = 64;          // sparse-index entry every N records

}  // namespace minidb
