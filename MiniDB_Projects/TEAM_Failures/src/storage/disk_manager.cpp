#include "storage/disk_manager.h"

#include <cstring>
#include "common/exception.h"

namespace minidb {

DiskManager::DiskManager(const string &db_file) : file_name_(db_file) {
  // Open for read+write in binary mode.  If the file does not exist yet we must
  // create it first (a non-existent file makes the read+write open fail).
  io_.open(file_name_, ios::in | ios::out | ios::binary);
  if (!io_.is_open()) {
    io_.clear();
    io_.open(file_name_, ios::out | ios::binary);  // create
    io_.close();
    io_.open(file_name_, ios::in | ios::out | ios::binary);
    if (!io_.is_open()) throw StorageError("cannot open data file: " + file_name_);
  }

  // Determine how many pages already exist by measuring the file length.
  io_.seekg(0, ios::end);
  streampos sz = io_.tellg();
  num_pages_ = (sz <= 0) ? 0 : static_cast<int>(sz / PAGE_SIZE);
}

DiskManager::~DiskManager() { shutdown(); }

void DiskManager::shutdown() {
  if (io_.is_open()) {
    io_.flush();
    io_.close();
  }
}

void DiskManager::readPage(page_id_t page_id, char *dest) {
  lock_guard<mutex> g(latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;

  // Reading beyond the end of the file (a freshly allocated but never-written
  // page) is legitimate: return zeros.
  if (static_cast<int>(offset / PAGE_SIZE) >= num_pages_) {
    memset(dest, 0, PAGE_SIZE);
    return;
  }

  io_.seekg(offset);
  io_.read(dest, PAGE_SIZE);
  // A short read can happen on the last partial page; zero-fill the remainder.
  streamsize got = io_.gcount();
  if (got < PAGE_SIZE) memset(dest + got, 0, PAGE_SIZE - got);
  io_.clear();  // clear any eof/fail bits so the stream stays usable
}

void DiskManager::writePage(page_id_t page_id, const char *src) {
  lock_guard<mutex> g(latch_);
  size_t offset = static_cast<size_t>(page_id) * PAGE_SIZE;
  io_.seekp(offset);
  io_.write(src, PAGE_SIZE);
  if (io_.fail()) throw StorageError("write failed for page " + to_string(page_id));
  io_.flush();  // push to the OS so a crash after commit keeps the data
  if (static_cast<int>(offset / PAGE_SIZE) >= num_pages_)
    num_pages_ = static_cast<int>(offset / PAGE_SIZE) + 1;
}

page_id_t DiskManager::allocatePage() {
  lock_guard<mutex> g(latch_);
  return num_pages_++;  // hand out the next id; file grows on first write
}

}  // namespace minidb
