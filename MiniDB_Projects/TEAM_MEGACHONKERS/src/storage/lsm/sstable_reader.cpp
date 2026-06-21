#include "storage/lsm/sstable_reader.h"
#include "common/logger.h"
#include <fstream>
#include <vector>

namespace minidb {

SearchResult SSTableReader::FindKey(const std::string& file_path, const InternalKey& target_key, Row* out_row) {
    std::ifstream in(file_path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        LOG_ERROR("Failed to open SSTable for reading: " + file_path);
        return SearchResult::NOT_FOUND;
    }

    std::string encoded_target = target_key.Encode();

    // Read through the file sequentially
    while (in.peek() != EOF) {
        uint32_t key_size = 0;
        
        // Attempt to read the next key_size. If we fail, we hit the footer.
        if (!in.read(reinterpret_cast<char*>(&key_size), sizeof(key_size))) {
            break; 
        }

        // We hit the 4-byte footer (Total Entries) if key_size looks suspiciously like an entry count
        // and we are at the end of the file. A safer way is to check the file position.
        std::string current_key(key_size, '\0');
        in.read(&current_key[0], key_size);

        uint32_t val_size = 0;
        in.read(reinterpret_cast<char*>(&val_size), sizeof(val_size));

        // If this is the key we are looking for
        if (current_key == encoded_target) {
            if (val_size == 0) {
                // It's a tombstone
                return SearchResult::DELETED;
            }

            std::string current_val(val_size, '\0');
            in.read(&current_val[0], val_size);

            if (out_row != nullptr) {
                *out_row = Row::Deserialize(current_val);
            }
            return SearchResult::FOUND;
        } else {
            // Not our key. Skip the value payload to save memory and CPU.
            in.seekg(val_size, std::ios::cur);
        }
    }

    return SearchResult::NOT_FOUND;
}

std::map<std::string, std::string> SSTableReader::ReadAll(const std::string& file_path) {
    std::map<std::string, std::string> entries;
    std::ifstream in(file_path, std::ios::in | std::ios::binary);
    
    if (!in.is_open()) return entries;

    while (in.peek() != EOF) {
        uint32_t key_size = 0;
        if (!in.read(reinterpret_cast<char*>(&key_size), sizeof(key_size))) break;

        std::string current_key(key_size, '\0');
        in.read(&current_key[0], key_size);

        uint32_t val_size = 0;
        in.read(reinterpret_cast<char*>(&val_size), sizeof(val_size));

        std::string current_val(val_size, '\0');
        if (val_size > 0) {
            in.read(&current_val[0], val_size);
        }

        entries[current_key] = current_val;
    }

    return entries;
}

} // namespace minidb