#include "storage/lsm/sstable_writer.h"
#include "common/logger.h"

namespace minidb {

bool SSTableWriter::WriteSSTable(const std::string& file_path, const std::map<std::string, std::string>& entries) {
    // Open in binary truncation mode (overwrite if exists, though SSTables should have unique names)
    std::ofstream out(file_path, std::ios::out | std::ios::binary | std::ios::trunc);
    
    if (!out.is_open()) {
        LOG_ERROR("Failed to open SSTable for writing: " + file_path);
        return false;
    }

    uint32_t entry_count = 0;

    // Binary Layout of an SSTable (Sequential Data Blocks):
    // For each entry: [Key Size: 4 bytes] | [Key] | [Value Size: 4 bytes] | [Value]
    for (const auto& [key, value] : entries) {
        uint32_t key_size = key.size();
        uint32_t val_size = value.size();

        out.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
        out.write(key.data(), key_size);
        out.write(reinterpret_cast<const char*>(&val_size), sizeof(val_size));
        
        if (val_size > 0) {
            out.write(value.data(), val_size);
        }
        
        entry_count++;
    }

    // Write a simple metadata footer at the very end of the file.
    // [Total Entries: 4 bytes]
    out.write(reinterpret_cast<const char*>(&entry_count), sizeof(entry_count));

    out.flush();
    out.close();

    LOG_INFO("Flushed SSTable to " + file_path + " with " + std::to_string(entry_count) + " entries.");
    return true;
}

} // namespace minidb