#include "storage/lsm/compactor.h"
#include "storage/lsm/sstable_reader.h"
#include "storage/lsm/sstable_writer.h"
#include "common/logger.h"
#include <map>
#include <filesystem>

namespace minidb {

bool Compactor::Compact(const std::vector<std::string>& input_sstables, const std::string& output_sstable) {
    if (input_sstables.empty()) {
        return false;
    }

    LOG_INFO("Starting Major Compaction of " + std::to_string(input_sstables.size()) + " SSTables.");

    // We will merge everything into a single map. 
    // Because std::map overwrites existing keys, if we load from oldest to newest,
    // the newest version of a row will naturally overwrite the older versions.
    std::map<std::string, std::string> merged_data;

    for (const auto& file_path : input_sstables) {
        LOG_DEBUG("Compacting file: " + file_path);
        
        auto file_data = SSTableReader::ReadAll(file_path);
        for (const auto& [key, value] : file_data) {
            merged_data[key] = value;
        }
    }

    // Purge Tombstones. 
    // Now that we have merged all history, any key that resolves to an empty string (tombstone)
    // is permanently deleted. We can safely remove it from the map before writing to disk.
    for (auto it = merged_data.begin(); it != merged_data.end(); ) {
        if (it->second.empty()) {
            it = merged_data.erase(it); // Erase returns the iterator to the next element
        } else {
            ++it;
        }
    }

    // Write the cleaned, merged data to the new unified SSTable
    bool success = SSTableWriter::WriteSSTable(output_sstable, merged_data);

    if (success) {
        // Atomic cleanup: Only delete the old files if the new one was written successfully
        std::error_code ec;
        for (const auto& file_path : input_sstables) {
            // FIX: Catch OS-level filesystem exceptions safely to prevent compaction crashes
            std::filesystem::remove(file_path, ec);
            if (ec) {
                LOG_ERROR("Failed to delete old SSTable during compaction: " + ec.message());
            }
        }
        LOG_INFO("Compaction complete. Created unified SSTable: " + output_sstable);
    } else {
        LOG_ERROR("Compaction failed during write phase.");
    }

    return success;
}

} // namespace minidb