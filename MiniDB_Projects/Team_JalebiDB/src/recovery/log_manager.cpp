#include "recovery/log_manager.h"
#include <iostream>

namespace minidb {

LogManager::LogManager(DiskManager *disk_manager) : disk_manager_(disk_manager) {
    flush_thread_ = std::thread(&LogManager::RunFlushThread, this);
}

LogManager::~LogManager() {
    {
        std::lock_guard<std::mutex> lock(latch_);
        run_flush_thread_ = false;
        cv_.notify_all();
    }
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
}

lsn_t LogManager::AppendLogRecord(LogRecord *log_record) {
    std::unique_lock<std::mutex> lock(latch_);

    // If record doesn't fit, swap buffers and notify flush thread
    if (log_buffer_offset_ + log_record->GetSize() > LOG_BUFFER_SIZE) {
        // Swap buffers
        while (flush_buffer_size_ > 0) {
            cv_.wait(lock);
        }
        std::memcpy(flush_buffer_, log_buffer_, log_buffer_offset_);
        flush_buffer_size_ = log_buffer_offset_;
        log_buffer_offset_ = 0;
        cv_.notify_one();
    }

    lsn_t lsn = next_lsn_++;
    log_record->SetLSN(lsn);
    log_record->Serialize(log_buffer_ + log_buffer_offset_);
    log_buffer_offset_ += log_record->GetSize();

    return lsn;
}

void LogManager::Flush(lsn_t lsn) {
    std::unique_lock<std::mutex> lock(latch_);
    
    if (lsn >= next_lsn_ || persistent_lsn_ >= lsn) {
        return;
    }

    if (log_buffer_offset_ > 0) {
        while (flush_buffer_size_ > 0) {
            cv_.wait(lock);
        }
        std::memcpy(flush_buffer_, log_buffer_, log_buffer_offset_);
        flush_buffer_size_ = log_buffer_offset_;
        log_buffer_offset_ = 0;
        cv_.notify_all();
    }

    cv_.wait(lock, [this, lsn]() {
        return !run_flush_thread_ || persistent_lsn_ >= lsn;
    });
}

void LogManager::FlushAll() {
    std::unique_lock<std::mutex> lock(latch_);
    
    // Swap buffer and force write
    while (flush_buffer_size_ > 0) {
        cv_.wait(lock);
    }
    
    if (log_buffer_offset_ > 0) {
        std::memcpy(flush_buffer_, log_buffer_, log_buffer_offset_);
        flush_buffer_size_ = log_buffer_offset_;
        log_buffer_offset_ = 0;
        cv_.notify_one();
    }
    
    // Wait for the background thread to finish writing
    cv_.wait(lock, [this]() {
        return flush_buffer_size_ == 0;
    });
}

void LogManager::RunFlushThread() {
    std::unique_lock<std::mutex> lock(latch_);
    while (run_flush_thread_) {
        // Wait until there is something to flush, or we're stopping
        cv_.wait(lock, [this]() {
            return !run_flush_thread_ || flush_buffer_size_ > 0 || log_buffer_offset_ > 0;
        });

        if (!run_flush_thread_) {
            break;
        }

        // If active buffer has data but flush_buffer is empty, swap them
        if (flush_buffer_size_ == 0 && log_buffer_offset_ > 0) {
            std::memcpy(flush_buffer_, log_buffer_, log_buffer_offset_);
            flush_buffer_size_ = log_buffer_offset_;
            log_buffer_offset_ = 0;
        }

        if (flush_buffer_size_ > 0) {
            // Write to disk manager outside of holding lock too long if possible,
            // but we must update persistent_lsn_ correctly.
            // Let's release the lock during I/O to avoid blocking writers.
            lock.unlock();
            disk_manager_->WriteLog(flush_buffer_, flush_buffer_size_);
            lock.lock();

            // Find the highest LSN in the flush buffer to update persistent_lsn_
            int offset = 0;
            lsn_t highest_lsn = persistent_lsn_;
            while (offset < flush_buffer_size_) {
                uint32_t size;
                std::memcpy(&size, flush_buffer_ + offset, sizeof(uint32_t));
                lsn_t lsn;
                std::memcpy(&lsn, flush_buffer_ + offset + 4, sizeof(lsn_t));
                highest_lsn = lsn;
                offset += size;
            }

            persistent_lsn_ = highest_lsn;
            flush_buffer_size_ = 0;
            cv_.notify_all();
        }
    }

    // Flush any remaining logs before exiting
    if (log_buffer_offset_ > 0) {
        disk_manager_->WriteLog(log_buffer_, log_buffer_offset_);
        int offset = 0;
        lsn_t highest_lsn = persistent_lsn_;
        while (offset < log_buffer_offset_) {
            uint32_t size;
            std::memcpy(&size, log_buffer_ + offset, sizeof(uint32_t));
            lsn_t lsn;
            std::memcpy(&lsn, log_buffer_ + offset + 4, sizeof(lsn_t));
            highest_lsn = lsn;
            offset += size;
        }
        persistent_lsn_ = highest_lsn;
        log_buffer_offset_ = 0;
    }
    persistent_lsn_ = next_lsn_ - 1;
    cv_.notify_all();
}

} // namespace minidb
