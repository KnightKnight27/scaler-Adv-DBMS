#include "storage/disk_manager.h"
#include <stdexcept>
#include <vector>

namespace minidb {

DiskManager::DiskManager(const std::string& db_path) {
  // Open for read+write; create the file first if it does not exist yet.
  file_.open(db_path, std::ios::in | std::ios::out | std::ios::binary);
  if (!file_.is_open()) {
    std::ofstream create(db_path, std::ios::binary);
    create.close();
    file_.open(db_path, std::ios::in | std::ios::out | std::ios::binary);
  }
  if (!file_.is_open()) throw std::runtime_error("DiskManager: cannot open " + db_path);

  file_.seekg(0, std::ios::end);
  num_pages_ = static_cast<PageId>(file_.tellg() / PAGE_SIZE);
}

void DiskManager::read_page(PageId id, Page& out) {
  file_.seekg(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
  file_.read(out.data.data(), PAGE_SIZE);
  file_.clear();  // clear EOF/fail bits in case the page was short
  out.id = id;
}

void DiskManager::write_page(PageId id, const Page& page) {
  file_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
  file_.write(page.data.data(), PAGE_SIZE);
  file_.flush();
  bytes_written_ += PAGE_SIZE;
}

PageId DiskManager::allocate_page() {
  PageId id = num_pages_++;
  std::vector<char> zeros(PAGE_SIZE, 0);
  file_.seekp(static_cast<std::streamoff>(id) * PAGE_SIZE, std::ios::beg);
  file_.write(zeros.data(), PAGE_SIZE);
  file_.flush();
  bytes_written_ += PAGE_SIZE;
  return id;
}

}  // namespace minidb
