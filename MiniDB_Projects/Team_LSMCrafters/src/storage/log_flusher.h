#pragma once
#include "storage/page.h"  // for LSN

namespace minidb {

// The buffer pool must obey the write-ahead rule: before a dirty page reaches
// disk, the log up to that page's rec_lsn must be durable. The storage layer
// only needs "flush the log up to this LSN", so it depends on this tiny
// interface rather than on the whole WAL module. The WAL's LogManager
// implements it; if no log is attached the buffer pool simply skips the rule.
struct LogFlusher {
  virtual void flush_upto(LSN lsn) = 0;
  virtual ~LogFlusher() = default;
};

}  // namespace minidb
