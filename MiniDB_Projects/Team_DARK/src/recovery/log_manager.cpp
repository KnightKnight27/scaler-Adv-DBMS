#include "recovery/log_manager.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace minidb {

namespace {

constexpr std::size_t kLogHeaderSize = 13;  // lsn(8) + type(1) + payload_len(4)

void ThrowSyscallError(const char* context) {
    throw std::runtime_error(std::string(context) + ": " + std::strerror(errno));
}

void AppendU64(std::vector<char>& buffer, uint64_t value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(uint64_t));
}

void AppendU32(std::vector<char>& buffer, uint32_t value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(uint32_t));
}

void AppendU16(std::vector<char>& buffer, uint16_t value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(uint16_t));
}

void AppendI32(std::vector<char>& buffer, int32_t value) {
    const char* bytes = reinterpret_cast<const char*>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(int32_t));
}

void AppendString(std::vector<char>& buffer, const std::string& value) {
    AppendU32(buffer, static_cast<uint32_t>(value.size()));
    buffer.insert(buffer.end(), value.begin(), value.end());
}

uint64_t ReadU64(const std::vector<char>& buffer, std::size_t& offset) {
    if (offset + sizeof(uint64_t) > buffer.size()) {
        throw std::runtime_error("log payload truncated");
    }
    uint64_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(uint64_t));
    offset += sizeof(uint64_t);
    return value;
}

uint32_t ReadU32(const std::vector<char>& buffer, std::size_t& offset) {
    if (offset + sizeof(uint32_t) > buffer.size()) {
        throw std::runtime_error("log payload truncated");
    }
    uint32_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    return value;
}

uint16_t ReadU16(const std::vector<char>& buffer, std::size_t& offset) {
    if (offset + sizeof(uint16_t) > buffer.size()) {
        throw std::runtime_error("log payload truncated");
    }
    uint16_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);
    return value;
}

int32_t ReadI32(const std::vector<char>& buffer, std::size_t& offset) {
    if (offset + sizeof(int32_t) > buffer.size()) {
        throw std::runtime_error("log payload truncated");
    }
    int32_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(int32_t));
    offset += sizeof(int32_t);
    return value;
}

std::string ReadString(const std::vector<char>& buffer, std::size_t& offset) {
    const uint32_t len = ReadU32(buffer, offset);
    if (offset + len > buffer.size()) {
        throw std::runtime_error("log payload truncated");
    }
    std::string value(buffer.data() + offset, buffer.data() + offset + len);
    offset += len;
    return value;
}

}  // namespace

LogManager::LogManager(const std::string& log_path)
    : fd_(-1), log_path_(log_path), next_lsn_(1) {
    fd_ = ::open(log_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ < 0) {
        ThrowSyscallError("open log");
    }

    const std::vector<LogRecord> existing = LoadAll();
    if (!existing.empty()) {
        next_lsn_ = existing.back().lsn + 1;
    }
}

LogManager::~LogManager() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

uint64_t LogManager::Append(LogRecordType type, const std::vector<char>& payload) {
    std::lock_guard<std::mutex> lock(mu_);

    const uint64_t lsn = next_lsn_++;
    const uint32_t payload_len = static_cast<uint32_t>(payload.size());

    AppendBytes(&lsn, sizeof(uint64_t));
    const auto type_byte = static_cast<uint8_t>(type);
    AppendBytes(&type_byte, sizeof(uint8_t));
    AppendBytes(&payload_len, sizeof(uint32_t));
    if (!payload.empty()) {
        AppendBytes(payload.data(), payload.size());
    }

    return lsn;
}

void LogManager::Flush() {
    std::lock_guard<std::mutex> lock(mu_);
    if (::fsync(fd_) < 0) {
        ThrowSyscallError("fsync log");
    }
}

std::vector<LogRecord> LogManager::LoadAll() const {
    std::vector<LogRecord> records;

    const int read_fd = ::open(log_path_.c_str(), O_RDONLY);
    if (read_fd < 0) {
        ThrowSyscallError("open log for read");
    }

    std::vector<char> file_data;
    char chunk[4096];
    while (true) {
        const ssize_t bytes_read = ::read(read_fd, chunk, sizeof(chunk));
        if (bytes_read < 0) {
            ::close(read_fd);
            ThrowSyscallError("read log");
        }
        if (bytes_read == 0) {
            break;
        }
        file_data.insert(file_data.end(), chunk, chunk + bytes_read);
    }
    ::close(read_fd);

    std::size_t offset = 0;
    while (offset + kLogHeaderSize <= file_data.size()) {
        LogRecord record{};
        std::memcpy(&record.lsn, file_data.data() + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);

        uint8_t type_byte = 0;
        std::memcpy(&type_byte, file_data.data() + offset, sizeof(uint8_t));
        offset += sizeof(uint8_t);
        record.type = static_cast<LogRecordType>(type_byte);

        uint32_t payload_len = 0;
        std::memcpy(&payload_len, file_data.data() + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (offset + payload_len > file_data.size()) {
            break;
        }

        record.payload.assign(file_data.data() + offset, file_data.data() + offset + payload_len);
        offset += payload_len;
        records.push_back(std::move(record));
    }

    return records;
}

uint64_t LogManager::GetNextLSN() const {
    std::lock_guard<std::mutex> lock(mu_);
    return next_lsn_;
}

void LogManager::AppendBytes(const void* data, std::size_t size) {
    const ssize_t bytes_written = ::write(fd_, data, size);
    if (bytes_written < 0) {
        ThrowSyscallError("write log");
    }
    if (static_cast<std::size_t>(bytes_written) != size) {
        throw std::runtime_error("write log wrote fewer bytes than expected");
    }
}

std::vector<char> LogManager::EncodeBegin(TxID tx_id) {
    std::vector<char> payload;
    AppendU64(payload, tx_id);
    return payload;
}

std::vector<char> LogManager::EncodeCommit(TxID tx_id) {
    std::vector<char> payload;
    AppendU64(payload, tx_id);
    return payload;
}

std::vector<char> LogManager::EncodeAbort(TxID tx_id) {
    std::vector<char> payload;
    AppendU64(payload, tx_id);
    return payload;
}

std::vector<char> LogManager::EncodeInsertRow(TxID tx_id, int32_t page_id, uint16_t slot_index,
                                              uint16_t row_offset, uint16_t row_length,
                                              const std::string& key, uint64_t xmin, uint64_t xmax,
                                              uint64_t prev_version_tid, const std::string& value) {
    std::vector<char> payload;
    AppendU64(payload, tx_id);
    AppendI32(payload, page_id);
    AppendU16(payload, slot_index);
    AppendU16(payload, row_offset);
    AppendU16(payload, row_length);
    AppendString(payload, key);
    AppendU64(payload, xmin);
    AppendU64(payload, xmax);
    AppendU64(payload, prev_version_tid);
    AppendString(payload, value);
    return payload;
}

std::vector<char> LogManager::EncodeUpdateXmax(TxID tx_id, int32_t page_id, uint16_t slot_index,
                                               uint64_t old_xmax, uint64_t new_xmax) {
    std::vector<char> payload;
    AppendU64(payload, tx_id);
    AppendI32(payload, page_id);
    AppendU16(payload, slot_index);
    AppendU64(payload, old_xmax);
    AppendU64(payload, new_xmax);
    return payload;
}

TxID LogManager::DecodeTxId(const std::vector<char>& payload) {
    std::size_t offset = 0;
    return ReadU64(payload, offset);
}

void LogManager::DecodeInsertRow(const std::vector<char>& payload, TxID& tx_id, int32_t& page_id,
                                 uint16_t& slot_index, uint16_t& row_offset, uint16_t& row_length,
                                 std::string& key, uint64_t& xmin, uint64_t& xmax,
                                 uint64_t& prev_version_tid, std::string& value) {
    std::size_t offset = 0;
    tx_id = ReadU64(payload, offset);
    page_id = ReadI32(payload, offset);
    slot_index = ReadU16(payload, offset);
    row_offset = ReadU16(payload, offset);
    row_length = ReadU16(payload, offset);
    key = ReadString(payload, offset);
    xmin = ReadU64(payload, offset);
    xmax = ReadU64(payload, offset);
    prev_version_tid = ReadU64(payload, offset);
    value = ReadString(payload, offset);
}

void LogManager::DecodeUpdateXmax(const std::vector<char>& payload, TxID& tx_id,
                                  int32_t& page_id, uint16_t& slot_index, uint64_t& old_xmax,
                                  uint64_t& new_xmax) {
    std::size_t offset = 0;
    tx_id = ReadU64(payload, offset);
    page_id = ReadI32(payload, offset);
    slot_index = ReadU16(payload, offset);
    old_xmax = ReadU64(payload, offset);
    new_xmax = ReadU64(payload, offset);
}

}  // namespace minidb
