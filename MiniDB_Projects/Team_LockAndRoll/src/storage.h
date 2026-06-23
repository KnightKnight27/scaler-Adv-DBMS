#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.h"

namespace minidb {

using file_id_t = int32_t;

class Page {
 public:
  Page() { reset(); }

  void reset() {
    std::memset(data_, 0, PAGE_SIZE);
    pin_count_ = 0;
    is_dirty_ = false;
    file_id_ = -1;
    page_id_ = INVALID_PAGE_ID;
  }

  char* data() { return data_; }
  const char* data() const { return data_; }

  file_id_t file_id() const { return file_id_; }
  page_id_t page_id() const { return page_id_; }
  int pin_count() const { return pin_count_; }
  bool is_dirty() const { return is_dirty_; }
  void set_dirty(bool d) { is_dirty_ = d; }

 private:
  friend class BufferPool;
  char data_[PAGE_SIZE];
  int pin_count_ = 0;
  bool is_dirty_ = false;
  file_id_t file_id_ = -1;
  page_id_t page_id_ = INVALID_PAGE_ID;
};

// slotted page layout:
//   [int64 page_lsn][u16 num_slots][u16 free_ptr]
//   [slot array: (u16 offset, u16 length) x num_slots] ... free ...
//   [tuple data, growing down from PAGE_SIZE]
// a slot with length 0 is a tombstone (deleted/reusable)
namespace slotted {
constexpr int LSN_OFF = 0;
constexpr int NSLOTS_OFF = 8;
constexpr int FREEPTR_OFF = 10;
constexpr int SLOTS_OFF = 12;
constexpr int SLOT_SZ = 4;

inline lsn_t get_lsn(const char* p) {
  lsn_t v;
  std::memcpy(&v, p + LSN_OFF, sizeof(v));
  return v;
}
inline void set_lsn(char* p, lsn_t v) { std::memcpy(p + LSN_OFF, &v, sizeof(v)); }

inline uint16_t get_u16(const char* p, int off) {
  uint16_t v;
  std::memcpy(&v, p + off, sizeof(v));
  return v;
}
inline void set_u16(char* p, int off, uint16_t v) { std::memcpy(p + off, &v, sizeof(v)); }

inline uint16_t num_slots(const char* p) { return get_u16(p, NSLOTS_OFF); }
inline uint16_t free_ptr(const char* p) { return get_u16(p, FREEPTR_OFF); }

inline void init(char* p) {
  std::memset(p, 0, PAGE_SIZE);
  set_lsn(p, 0);
  set_u16(p, NSLOTS_OFF, 0);
  set_u16(p, FREEPTR_OFF, PAGE_SIZE);
}

inline void get_slot(const char* p, int slot, uint16_t& off, uint16_t& len) {
  int base = SLOTS_OFF + slot * SLOT_SZ;
  off = get_u16(p, base);
  len = get_u16(p, base + 2);
}
inline void set_slot(char* p, int slot, uint16_t off, uint16_t len) {
  int base = SLOTS_OFF + slot * SLOT_SZ;
  set_u16(p, base, off);
  set_u16(p, base + 2, len);
}

// returns slot, or -1 if no room
inline int insert(char* p, const uint8_t* bytes, uint16_t len) {
  uint16_t ns = num_slots(p);
  int slot = -1;
  for (uint16_t i = 0; i < ns; i++) {
    uint16_t o, l;
    get_slot(p, i, o, l);
    if (l == 0) { slot = i; break; }
  }
  bool new_slot = (slot < 0);
  uint16_t slot_array_end = SLOTS_OFF + (ns + (new_slot ? 1 : 0)) * SLOT_SZ;
  uint16_t fp = free_ptr(p);
  if (fp < slot_array_end + len) return -1;
  uint16_t data_off = fp - len;
  std::memcpy(p + data_off, bytes, len);
  set_u16(p, FREEPTR_OFF, data_off);
  if (new_slot) {
    slot = ns;
    set_u16(p, NSLOTS_OFF, ns + 1);
  }
  set_slot(p, slot, data_off, len);
  return slot;
}

// place bytes at an exact slot for recovery redo/undo. off/len/slot come from log
// records, so validate them or a corrupt log drives an out-of-bounds write
inline void apply_slot(char* p, int slot, uint16_t off, uint16_t len, const uint8_t* bytes) {
  if (slot < 0 || SLOTS_OFF + (slot + 1) * SLOT_SZ > PAGE_SIZE)
    throw DBException("recovery: slot index out of range (corrupt log)");
  if (len > 0 && (off < SLOTS_OFF || static_cast<int>(off) + len > PAGE_SIZE))
    throw DBException("recovery: slot offset/length out of range (corrupt log)");
  // an initialized page's free_ptr is never 0, so free_ptr == 0 means a page that
  // was allocated but crashed before init flushed; treat it as fresh
  if (free_ptr(p) == 0) set_u16(p, FREEPTR_OFF, PAGE_SIZE);
  uint16_t ns = num_slots(p);
  if (slot >= ns) set_u16(p, NSLOTS_OFF, slot + 1);
  if (len > 0 && bytes != nullptr) {
    std::memcpy(p + off, bytes, len);
    if (off < free_ptr(p)) set_u16(p, FREEPTR_OFF, off);
  }
  set_slot(p, slot, off, len);
}

}  // namespace slotted

class DiskManager {
 public:
  explicit DiskManager(std::string dir);
  ~DiskManager();

  file_id_t open_file(const std::string& name);

  void read_page(file_id_t fid, page_id_t pid, char* dest);
  void write_page(file_id_t fid, page_id_t pid, const char* src);
  page_id_t allocate_page(file_id_t fid);
  page_id_t num_pages(file_id_t fid) const;
  void sync();

 private:
  struct FileEntry {
    std::string path;
    int fd = -1;
    std::atomic<page_id_t> pages{0};
  };
  // unique_ptr keeps entry addresses stable across open_file's push_back. fd is set
  // once and pages is atomic, so I/O needs no lock once we hold the pointer
  FileEntry* entry(file_id_t fid) const;

  std::string dir_;
  mutable std::mutex meta_mtx_;
  std::vector<std::unique_ptr<FileEntry>> files_;
  std::unordered_map<std::string, file_id_t> by_name_;
};

// lets the buffer pool honor the WAL rule without depending on the recovery module
class LogFlusher {
 public:
  virtual ~LogFlusher() = default;
  virtual void flush_to(lsn_t lsn) = 0;
};

class BufferPool {
 public:
  BufferPool(DiskManager* dm, size_t num_frames);

  void set_log_flusher(LogFlusher* f) { log_flusher_ = f; }

  // pins the page; caller must unpin when done
  Page* fetch_page(file_id_t fid, page_id_t pid);
  Page* new_page(file_id_t fid, page_id_t* out_pid);
  void unpin_page(file_id_t fid, page_id_t pid, bool dirty);
  void flush_page(file_id_t fid, page_id_t pid);
  void flush_all();
  // drop every cached page without writing dirty ones back, to simulate a crash
  void reset_discard();

  size_t hits() const { return hits_; }
  size_t misses() const { return misses_; }

 private:
  static uint64_t key(file_id_t fid, page_id_t pid) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(fid)) << 32) |
           static_cast<uint32_t>(pid);
  }
  int find_victim();

  DiskManager* dm_;
  LogFlusher* log_flusher_ = nullptr;
  std::vector<std::unique_ptr<Page>> frames_;
  std::unordered_map<uint64_t, int> page_table_;
  std::list<int> lru_;  // front = most recent
  std::unordered_map<int, std::list<int>::iterator> lru_pos_;
  std::vector<int> free_list_;
  std::mutex latch_;
  std::atomic<size_t> hits_{0}, misses_{0};
};

}  // namespace minidb
