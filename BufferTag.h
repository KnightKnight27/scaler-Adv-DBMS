#pragma once

#include <functional>
#include <string>

// uniquely identifies a disk page
struct BufferTag {
    int spcNode;  // tablespace id
    int dbNode;   // database id
    int relNode;  // relation (table) id
    int blockNum; // block number within the file

    bool operator==(const BufferTag& other) const {
        return spcNode == other.spcNode &&
               dbNode  == other.dbNode  &&
               relNode == other.relNode &&
               blockNum == other.blockNum;
    }

    [[nodiscard]] std::string toString() const {
        return "spc=" + std::to_string(spcNode) +
               " db="  + std::to_string(dbNode)  +
               " rel=" + std::to_string(relNode)  +
               " blk=" + std::to_string(blockNum);
    }
};

// custom hash so BufferTag works as unordered_map key
template <>
struct std::hash<BufferTag> {
    size_t operator()(const BufferTag& t) const noexcept {
        // combine all four fields into one hash
        size_t h = 0;
        h ^= std::hash<int>{}(t.spcNode) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(t.dbNode)  + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(t.relNode) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(t.blockNum)+ 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};
