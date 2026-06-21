#include "schema.hpp"

#include <cstdint>
#include <cstring>

int Schema::index_of(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i)
        if (columns[i].name == name) return i;
    return -1;
}

std::string Schema::serialize(const std::vector<Value>& row) const {
    std::string out;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].type == ColumnType::INT) {
            int v = std::get<int>(row[i]);
            out.append(reinterpret_cast<const char*>(&v), sizeof(int));
        } else {
            const std::string& s = std::get<std::string>(row[i]);
            std::uint16_t len = static_cast<std::uint16_t>(s.size());
            out.append(reinterpret_cast<const char*>(&len), sizeof(len));
            out.append(s);
        }
    }
    return out;
}

std::vector<Value> Schema::deserialize(const std::string& bytes) const {
    std::vector<Value> row;
    row.reserve(columns.size());
    std::size_t off = 0;
    for (const Column& col : columns) {
        if (col.type == ColumnType::INT) {
            int v;
            std::memcpy(&v, bytes.data() + off, sizeof(int));
            off += sizeof(int);
            row.emplace_back(v);
        } else {
            std::uint16_t len;
            std::memcpy(&len, bytes.data() + off, sizeof(len));
            off += sizeof(len);
            row.emplace_back(std::string(bytes.data() + off, len));
            off += len;
        }
    }
    return row;
}
