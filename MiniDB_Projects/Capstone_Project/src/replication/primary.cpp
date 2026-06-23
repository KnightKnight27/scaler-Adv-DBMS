#include "replication/primary.h"
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

Primary::~Primary() {
    close();
}

bool Primary::open(const std::string& replication_log_path) {
    fd_ = ::open(replication_log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[Primary] ERROR: Cannot open replication log\n";
        return false;
    }
    std::cout << "[Primary] Replication log ready: " << replication_log_path << "\n";
    return true;
}

void Primary::close() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

void Primary::shipLog(const std::string& wal_path, LSN lsn_from) {
    const int wal_fd = ::open(wal_path.c_str(), O_RDONLY);
    if (wal_fd < 0) {
        return;
    }

    const off_t offset = static_cast<off_t>(lsn_from) * WAL_RECORD_SIZE;
    if (::lseek(wal_fd, offset, SEEK_SET) < 0) {
        ::close(wal_fd);
        return;
    }

    WALRecord rec;
    int shipped = 0;
    while (::read(wal_fd, &rec, WAL_RECORD_SIZE) == static_cast<ssize_t>(WAL_RECORD_SIZE)) {
        const ssize_t written = ::write(fd_, &rec, WAL_RECORD_SIZE);
        if (written == static_cast<ssize_t>(WAL_RECORD_SIZE)) {
            shipped_lsn_ = rec.lsn;
            shipped++;
        }
    }

    ::fsync(fd_);
    ::close(wal_fd);

    if (shipped > 0) {
        std::cout << "[Primary] Shipped " << shipped << " record(s) to replica. "
                  << "Primary LSN=" << shipped_lsn_ << "\n";
    }
}
