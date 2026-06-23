#pragma once
#include "common/types.h"

// Converts between a logical Row and its on-page byte representation.
// Layout per row: each column in schema order, where
//   INT  -> 8 raw bytes (host order; MiniDB is single-machine)
//   TEXT -> 2-byte length prefix, then that many characters
namespace minidb {

Bytes serialize(const Row& row, const Schema& schema);
Row   deserialize(const Bytes& bytes, const Schema& schema);

}  // namespace minidb
