#include "minidb/storage/page_manager.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace minidb {

PageManager::PageManager(const std::string& path) : path_(path) {
  // Ensure the file exists (append-create never truncates), then open it for
  // random-access read+write. Doing the create as a separate step avoids the
  // platform-dependent behaviour of opening a missing file with in|out.
  { std::ofstream create(path_, std::ios::binary | std::ios::app); }
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_) throw std::runtime_error("PageManager: cannot open " + path_);

  file_.seekg(0, std::ios::end);
  std::streamoff size = file_.tellg();
  if (size < 0) size = 0;
  num_pages_ = static_cast<PageId>(size / kPageSize);
}

PageManager::~PageManager() {
  if (file_.is_open()) {
    file_.flush();
    file_.close();
  }
}

PageId PageManager::allocate_page() {
  PageId id = num_pages_;
  std::vector<uint8_t> zero(kPageSize, 0);
  file_.seekp(static_cast<std::streamoff>(id) * kPageSize, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(zero.data()), kPageSize);
  if (!file_) throw std::runtime_error("PageManager: allocate write failed");
  file_.flush();
  ++num_pages_;
  return id;
}

void PageManager::read_page(PageId id, uint8_t* dst) {
  if (id >= num_pages_) throw std::runtime_error("PageManager: read out of range");
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(id) * kPageSize, std::ios::beg);
  file_.read(reinterpret_cast<char*>(dst), kPageSize);
  std::streamsize got = file_.gcount();
  // A short read means the page is logically allocated but its bytes are not
  // (yet) physically present — a freshly allocated page is defined to be all
  // zeros, so zero-fill the remainder rather than failing.
  if (got < static_cast<std::streamsize>(kPageSize)) {
    std::memset(dst + got, 0, kPageSize - static_cast<size_t>(got));
    file_.clear();
  }
}

void PageManager::write_page(PageId id, const uint8_t* src) {
  if (id >= num_pages_) throw std::runtime_error("PageManager: write out of range");
  file_.clear();
  file_.seekp(static_cast<std::streamoff>(id) * kPageSize, std::ios::beg);
  file_.write(reinterpret_cast<const char*>(src), kPageSize);
  if (!file_) throw std::runtime_error("PageManager: write failed");
}

void PageManager::sync() { file_.flush(); }

}  // namespace minidb
