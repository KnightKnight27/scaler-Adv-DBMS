#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <common/config.h>

namespace minidb {

struct Row {
    std::vector<std::string> columns;

    // Serialize row into a single byte array for storage
    std::string Serialize() const {
        std::string buffer;
        for (const auto& col : columns) {
            uint32_t len = col.size();
            buffer.append(reinterpret_cast<const char*>(&len), sizeof(len));
            buffer.append(col);
        }
        return buffer;
    }

    // Deserialize byte array back into a Row object
    static Row Deserialize(const std::string& data) {
        Row row;
        size_t offset = 0;
        while (offset < data.size()) {
            uint32_t len = *reinterpret_cast<const uint32_t*>(data.data() + offset);
            offset += sizeof(len);
            row.columns.push_back(data.substr(offset, len));
            offset += len;
        }
        return row;
    }
};

// Represents the LSM Key (e.g., TableID + Primary Key)
struct InternalKey {
    table_oid_t table_id;
    std::string user_key;

    std::string Encode() const {
        std::string buffer;
        buffer.append(reinterpret_cast<const char*>(&table_id), sizeof(table_id));
        buffer.append(user_key);
        return buffer;
    }
};

} // namespace minidb