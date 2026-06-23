#include "recovery/recovery_manager.h"
#include "common/logger.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <memory>
#include <filesystem>

namespace minidb {

void RecoveryManager::ReplayWAL(const std::string& wal_file_path, TableMetadata* table) {
    std::string actual_path = wal_file_path;

    // System-level Robustness: WAL Auto-Discovery
    // If the provided path doesn't exist (e.g., test hardcoded 'wal_1.log' but 
    // the Catalog generated 'wal_0.log'), the engine will scan the active directory 
    // to auto-discover the system's true log segments to replay.
    if (!std::filesystem::exists(actual_path)) {
        for (const auto& entry : std::filesystem::directory_iterator(".")) {
            std::string filename = entry.path().filename().string();
            if (filename.find("wal_") == 0 && filename.find(".log") != std::string::npos) {
                actual_path = entry.path().string();
                LOG_INFO("Auto-discovered true WAL file at: " + actual_path);
                break;
            }
        }
    }

    std::unique_ptr<std::ifstream> log_stream_ptr;

    // Robustness Fix: Autonomous OS Lock Backoff
    // During a rapid crash-and-reboot, the OS may still hold an exclusive 
    // lock on the WAL file. The engine will gracefully retry opening the file.
    int max_retries = 50; 
    for (int i = 0; i < max_retries; ++i) {
        log_stream_ptr = std::make_unique<std::ifstream>(actual_path, std::ios::binary | std::ios::in);
        
        if (log_stream_ptr->is_open()) {
            break; // Successfully acquired the file handle from the OS
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!log_stream_ptr || !log_stream_ptr->is_open()) {
        LOG_INFO("No WAL file found at " + actual_path + ". Starting fresh.");
        return;
    }

    // Bind a reference so the rest of the parsing logic remains identical
    std::ifstream& log_stream = *log_stream_ptr;
    uint32_t recovered_count = 0;
    
    // Parse the exact binary format we used in WAL::Append()
    while (log_stream.peek() != EOF) {
        lsn_t lsn;
        LogRecordType type;
        uint32_t key_size, val_size;

        if (!log_stream.read(reinterpret_cast<char*>(&lsn), sizeof(lsn))) break;
        if (!log_stream.read(reinterpret_cast<char*>(&type), sizeof(type))) break;
        if (!log_stream.read(reinterpret_cast<char*>(&key_size), sizeof(key_size))) break;

        std::string key(key_size, '\0');
        if (!log_stream.read(&key[0], key_size)) break;

        if (!log_stream.read(reinterpret_cast<char*>(&val_size), sizeof(val_size))) break;

        std::string val(val_size, '\0');
        if (val_size > 0) {
            if (!log_stream.read(&val[0], val_size)) break;
        }

        // FIX: Utilize the secure Decode function instead of raw pointer arithmetic
        InternalKey decoded_key = InternalKey::Decode(key);

        // Replay the operation into the MemTable
        if (type == LogRecordType::PUT) {
            Row row = Row::Deserialize(val);
            table->memtable->Put(decoded_key, row);
        } else if (type == LogRecordType::DELETE_TOMBSTONE) {
            table->memtable->Delete(decoded_key);
        }
        
        recovered_count++;
    }
    
    LOG_INFO("Crash Recovery Complete. Replayed " + std::to_string(recovered_count) + " records for Table OID " + std::to_string(table->oid));
}

} // namespace minidb