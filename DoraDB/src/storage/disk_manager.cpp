#include "storage/disk_manager.h"
#include "common/config.h"

#include <fcntl.h>      // open()
#include <unistd.h>     // read(), write(), close(), lseek()
#include <sys/stat.h>   // file permissions
#include <cstring>      // memset
#include <stdexcept>
#include <iostream>
#include <filesystem>

DiskManager::DiskManager(const std::string& file_path) : file_path_(file_path) {
    // Make sure the parent directory exists
    std::filesystem::path p(file_path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }

    // Open or create the file (read-write, create if missing)
    fd_ = open(file_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("DiskManager: cannot open file " + file_path);
    }

    // Figure out how many pages already exist by checking file size
    off_t file_size = lseek(fd_, 0, SEEK_END);
    num_pages_ = file_size / PAGE_SIZE;
}

DiskManager::~DiskManager() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void DiskManager::ReadPage(int page_id, char* data) {
    if (page_id < 0 || page_id >= num_pages_) {
        throw std::runtime_error("DiskManager::ReadPage: invalid page_id " +
                                 std::to_string(page_id));
    }

    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    lseek(fd_, offset, SEEK_SET);

    ssize_t bytes_read = read(fd_, data, PAGE_SIZE);
    if (bytes_read != PAGE_SIZE) {
        throw std::runtime_error("DiskManager::ReadPage: short read on page " +
                                 std::to_string(page_id));
    }
}

void DiskManager::WritePage(int page_id, const char* data) {
    if (page_id < 0) {
        throw std::runtime_error("DiskManager::WritePage: invalid page_id");
    }

    off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
    lseek(fd_, offset, SEEK_SET);

    ssize_t bytes_written = write(fd_, data, PAGE_SIZE);
    if (bytes_written != PAGE_SIZE) {
        throw std::runtime_error("DiskManager::WritePage: short write on page " +
                                 std::to_string(page_id));
    }
}

int DiskManager::AllocatePage() {
    int new_page_id = num_pages_;
    num_pages_++;

    // Extend the file by writing one empty page
    char empty[PAGE_SIZE];
    memset(empty, 0, PAGE_SIZE);
    WritePage(new_page_id, empty);

    return new_page_id;
}
