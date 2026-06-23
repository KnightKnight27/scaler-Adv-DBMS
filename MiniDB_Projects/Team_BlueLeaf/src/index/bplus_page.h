#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "common/types.h"

namespace minidb {

using BTKey = std::int64_t;  // MiniDB indexes on a 64-bit integer key (the primary key)

// BPlusNode is a typed view over one 4 KiB page that holds a B+Tree node.
// Header (bytes [0,12)):
//   byte 0  : uint32 checksum   (managed by DiskManager, like every page)
//   byte 4  : uint8  is_leaf
//   byte 6  : uint16 key_count
//   byte 8  : int32  next_leaf  (leaf only; INVALID_PAGE_ID otherwise)
//
// Entries are stored compactly after the header (no gaps), so the value array's
// position depends on key_count:
//   leaf:     keys[key_count] (int64)  then  rids[key_count] (6 bytes: int32 page + int16 slot)
//   internal: keys[key_count] (int64)  then  children[key_count+1] (int32 page ids)
//
// To keep structural edits trivially correct, callers load a node into vectors,
// modify the vectors, and store them back (rebuilding the page). Nodes are small
// (one page), so this is cheap and avoids fiddly in-place byte shuffling.
class BPlusNode {
public:
    static constexpr std::size_t HEADER = 12;
    static constexpr std::size_t KEY_SZ = sizeof(BTKey);  // 8
    static constexpr std::size_t RID_SZ = 6;              // int32 page + int16 slot
    static constexpr std::size_t CHILD_SZ = sizeof(PageId);  // 4

    explicit BPlusNode(char* d) : data_(d) {}

    void init(bool leaf) {
        set_leaf(leaf);
        set_key_count(0);
        set_next_leaf(INVALID_PAGE_ID);
    }

    bool          is_leaf() const   { return read<std::uint8_t>(4) != 0; }
    std::uint16_t key_count() const { return read<std::uint16_t>(6); }
    PageId        next_leaf() const { return read<PageId>(8); }
    void          set_next_leaf(PageId p) { write<PageId>(8, p); }

    // --- leaf load/store ---
    void load_leaf(std::vector<BTKey>& keys, std::vector<RID>& rids) const {
        std::uint16_t n = key_count();
        keys.resize(n);
        rids.resize(n);
        std::size_t off = HEADER;
        for (std::uint16_t i = 0; i < n; ++i) { keys[i] = read<BTKey>(off); off += KEY_SZ; }
        for (std::uint16_t i = 0; i < n; ++i) {
            rids[i].page_id = read<std::int32_t>(off);
            rids[i].slot    = read<std::int16_t>(off + 4);
            off += RID_SZ;
        }
    }
    void store_leaf(const std::vector<BTKey>& keys, const std::vector<RID>& rids) {
        set_leaf(true);
        set_key_count(static_cast<std::uint16_t>(keys.size()));
        std::size_t off = HEADER;
        for (BTKey k : keys) { write<BTKey>(off, k); off += KEY_SZ; }
        for (const RID& r : rids) {
            write<std::int32_t>(off, r.page_id);
            write<std::int16_t>(off + 4, r.slot);
            off += RID_SZ;
        }
    }

    // --- internal load/store ---
    void load_internal(std::vector<BTKey>& keys, std::vector<PageId>& children) const {
        std::uint16_t n = key_count();
        keys.resize(n);
        children.resize(static_cast<std::size_t>(n) + 1);
        std::size_t off = HEADER;
        for (std::uint16_t i = 0; i < n; ++i) { keys[i] = read<BTKey>(off); off += KEY_SZ; }
        for (std::uint16_t i = 0; i <= n; ++i) { children[i] = read<PageId>(off); off += CHILD_SZ; }
    }
    void store_internal(const std::vector<BTKey>& keys, const std::vector<PageId>& children) {
        set_leaf(false);
        set_key_count(static_cast<std::uint16_t>(keys.size()));
        std::size_t off = HEADER;
        for (BTKey k : keys) { write<BTKey>(off, k); off += KEY_SZ; }
        for (PageId c : children) { write<PageId>(off, c); off += CHILD_SZ; }
    }

    // Largest number of keys that physically fit in a node of each kind.
    static std::size_t leaf_capacity() { return (PAGE_SIZE - HEADER) / (KEY_SZ + RID_SZ); }
    static std::size_t internal_capacity() {
        return (PAGE_SIZE - HEADER - CHILD_SZ) / (KEY_SZ + CHILD_SZ);
    }

private:
    void set_leaf(bool f)            { write<std::uint8_t>(4, f ? 1 : 0); }
    void set_key_count(std::uint16_t n) { write<std::uint16_t>(6, n); }

    template <typename T> T read(std::size_t off) const {
        T v;
        std::memcpy(&v, data_ + off, sizeof(T));
        return v;
    }
    template <typename T> void write(std::size_t off, T v) {
        std::memcpy(data_ + off, &v, sizeof(T));
    }

    char* data_;
};

} // namespace minidb
