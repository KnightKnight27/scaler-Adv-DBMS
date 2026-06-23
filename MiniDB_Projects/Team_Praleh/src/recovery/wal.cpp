#include "recovery/wal.h"
#include <stdexcept>

namespace minidb {

WAL::WAL(const std::string& path) : path_(path) {
    out_.open(path, std::ios::binary | std::ios::app);
    if (!out_.is_open())
        throw std::runtime_error("WAL: cannot open " + path);
}

WAL::~WAL() { Flush(); if (out_.is_open()) out_.close(); }

lsn_t WAL::Append(LogRecord& rec) {
    std::lock_guard<std::mutex> lk(latch_);
    rec.lsn = next_lsn_++;
    out_.write(reinterpret_cast<const char*>(&rec), sizeof(LogRecord));
    return rec.lsn;
}

void WAL::Flush() {
    std::lock_guard<std::mutex> lk(latch_);
    out_.flush();
}

} // namespace minidb
