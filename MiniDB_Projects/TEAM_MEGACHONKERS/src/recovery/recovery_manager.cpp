#include "recovery/recovery_manager.h"
#include "common/logger.h"
#include <fstream>

namespace minidb {

void RecoveryManager::ReplayWAL(const std::string& wal_file_path, TableMetadata* table) {
    std::ifstream log_stream(wal_file_path, std::ios::binary);
    if (!log_stream.is_open()) {
        LOG_INFO("No WAL file found at " + wal_file_path + ". Starting fresh.");
        return;
    }

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

        // Decode the binary InternalKey (Extracts Table OID + String Key)
        const char* key_data = key.data();
        table_oid_t oid = *reinterpret_cast<const table_oid_t*>(key_data);
        std::string actual_key = key.substr(sizeof(table_oid_t));
        InternalKey decoded_key{oid, actual_key};

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