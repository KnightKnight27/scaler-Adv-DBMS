#pragma once
#include <memory>
#include <optional>
#include "common/types.h"

namespace minidb {

// Ordered cursor over (key, serialized-row) pairs produced by a storage engine.
struct RowCursor {
  virtual bool next(Key& out_key, Bytes& out_value) = 0;
  virtual ~RowCursor() = default;
};

// The single abstraction the query executor talks to. Both the default
// HeapTable (heap file + B+Tree) and the Track C LsmTable implement it, so the
// executor and benchmark run unchanged over either storage design. Values are
// opaque serialized rows; only the primary key is interpreted.
struct StorageEngine {
  virtual void insert(Key key, const Bytes& value) = 0;  // insert or overwrite
  virtual std::optional<Bytes> get(Key key)        = 0;
  virtual void erase(Key key)                       = 0;

  virtual std::unique_ptr<RowCursor> scan()                          = 0;
  virtual std::unique_ptr<RowCursor> index_range(Key lo, Key hi)     = 0;
  virtual bool supports_index_scan() const                          = 0;

  virtual const TableStats& stats() const = 0;  // read by the optimizer
  virtual void flush()                    = 0;  // persist outstanding state

  virtual ~StorageEngine() = default;
};

}  // namespace minidb
