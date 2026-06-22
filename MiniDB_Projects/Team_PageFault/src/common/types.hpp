#pragma once
#include <cstdint>
#include <functional>

namespace minidb {

// A page is addressed by a small integer id; offset on disk is id * PAGE_SIZE.
using PageId = int32_t;
constexpr PageId INVALID_PAGE = -1;

// Transactions are numbered monotonically from 1. 0 means "no transaction".
using TxnId = int64_t;
constexpr TxnId INVALID_TXN = 0;

// A RowId names a tuple by the page it lives on and its slot within that page's
// slot directory. Because the slot directory is an indirection layer, a RowId
// stays valid even if the tuple bytes move around inside the page.
struct RowId {
    PageId page = INVALID_PAGE;
    int16_t slot = -1;

    bool operator==(const RowId& o) const { return page == o.page && slot == o.slot; }
    bool operator!=(const RowId& o) const { return !(*this == o); }
    bool valid() const { return page != INVALID_PAGE && slot >= 0; }
};

} // namespace minidb

// Allow RowId to be used as a hash-map key (handy for the recovery layer).
namespace std {
template <> struct hash<minidb::RowId> {
    size_t operator()(const minidb::RowId& r) const noexcept {
        return (static_cast<size_t>(static_cast<uint32_t>(r.page)) << 16) ^
               static_cast<size_t>(static_cast<uint16_t>(r.slot));
    }
};
} // namespace std
