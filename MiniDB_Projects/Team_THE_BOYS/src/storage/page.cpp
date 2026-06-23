#include "storage/page.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace minidb {

namespace {
constexpr int kPageHeaderSize = 16;
constexpr int kSlotDirEntrySize = 4;  // 2 bytes offset + 2 bytes length
constexpr int kTupleHeaderSize = 2;
}  // namespace

Page::Page(int page_id, const char* data)
    : page_id_(page_id), data_(PAGE_SIZE, '\0') {
    if (data != nullptr) {
        std::memcpy(data_.data(), data, PAGE_SIZE);
        std::memcpy(&lsn_, data_.data() + 8, sizeof(lsn_));
    }
}

void Page::Initialize() {
    SetHeader(0, static_cast<uint16_t>(PAGE_SIZE - kPageHeaderSize));
    dirty_ = true;
}

uint16_t Page::NumSlots() const {
    uint16_t num = 0;
    std::memcpy(&num, data_.data() + 4, sizeof(num));
    return num;
}

uint16_t Page::FreeSpace() const {
    uint16_t free = 0;
    std::memcpy(&free, data_.data() + 6, sizeof(free));
    return free;
}

void Page::SetHeader(uint16_t num_slots, uint16_t free_space) {
    uint32_t pid = static_cast<uint32_t>(page_id_);
    std::memcpy(data_.data(), &pid, sizeof(pid));
    std::memcpy(data_.data() + 4, &num_slots, sizeof(num_slots));
    std::memcpy(data_.data() + 6, &free_space, sizeof(free_space));
    std::memcpy(data_.data() + 8, &lsn_, sizeof(lsn_));
}

int Page::SlotOffset(int slot_id) const {
    return static_cast<int>(PAGE_SIZE) - (slot_id + 1) * kSlotDirEntrySize;
}

std::pair<int, int> Page::ReadSlot(int slot_id) const {
    int off = SlotOffset(slot_id);
    uint16_t offset = 0;
    uint16_t length = 0;
    std::memcpy(&offset, data_.data() + off, sizeof(offset));
    if (offset == 0xFFFF) {
        return {-1, 0};
    }
    std::memcpy(&length, data_.data() + off + 2, sizeof(length));
    return {offset, length};
}

void Page::WriteSlot(int slot_id, int offset, int length) {
    int off = SlotOffset(slot_id);
    uint16_t off16 = static_cast<uint16_t>(offset);
    uint16_t len16 = static_cast<uint16_t>(length);
    std::memcpy(data_.data() + off, &off16, sizeof(off16));
    std::memcpy(data_.data() + off + 2, &len16, sizeof(len16));
}

std::optional<int> Page::InsertTuple(const std::vector<uint8_t>& raw) {
    uint16_t num_slots = NumSlots();
    int needed = static_cast<int>(raw.size()) + kTupleHeaderSize + kSlotDirEntrySize;
    if (needed > static_cast<int>(FreeSpace())) {
        return std::nullopt;
    }

    int data_start = kPageHeaderSize;
    for (int s = 0; s < num_slots; ++s) {
        auto [off, length] = ReadSlot(s);
        if (off >= 0) {
            data_start = std::max(data_start, off + kTupleHeaderSize + length);
        }
    }

    int tuple_off = data_start;
    uint16_t tuple_len = static_cast<uint16_t>(raw.size());
    std::memcpy(data_.data() + tuple_off, &tuple_len, sizeof(tuple_len));
    std::memcpy(data_.data() + tuple_off + kTupleHeaderSize, raw.data(), raw.size());

    WriteSlot(num_slots, tuple_off, static_cast<int>(raw.size()));
    SetHeader(num_slots + 1, static_cast<uint16_t>(FreeSpace() - needed));
    dirty_ = true;
    return num_slots;
}

std::optional<std::vector<uint8_t>> Page::GetTuple(int slot_id) const {
    auto [off, length] = ReadSlot(slot_id);
    if (off < 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> result(length);
    std::memcpy(result.data(), data_.data() + off + kTupleHeaderSize, length);
    return result;
}

bool Page::DeleteTuple(int slot_id) {
    auto [off, length] = ReadSlot(slot_id);
    if (off < 0) {
        return false;
    }
    (void)length;
    int slot_pos = SlotOffset(slot_id);
    uint16_t tombstone = 0xFFFF;
    std::memcpy(data_.data() + slot_pos, &tombstone, sizeof(tombstone));
    dirty_ = true;
    return true;
}

std::vector<int> Page::ValidSlots() const {
    std::vector<int> slots;
    for (int s = 0; s < NumSlots(); ++s) {
        if (ReadSlot(s).first >= 0) {
            slots.push_back(s);
        }
    }
    return slots;
}

void Page::WriteBack(char* dest) const {
    std::memcpy(dest, data_.data(), PAGE_SIZE);
    std::memcpy(dest + 8, &lsn_, sizeof(lsn_));
}

std::vector<uint8_t> Page::SerializeRow(const Row& row) {
    std::vector<uint8_t> out;
    for (const auto& v : row.values) {
        if (v.type == ValueType::NULL_TYPE) {
            out.push_back(0);
        } else if (v.type == ValueType::INT) {
            out.push_back(1);
            int64_t val = std::get<int64_t>(v.data);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&val);
            out.insert(out.end(), bytes, bytes + sizeof(val));
        } else {
            out.push_back(2);
            const auto& s = std::get<std::string>(v.data);
            uint16_t len = static_cast<uint16_t>(s.size());
            const auto* len_bytes = reinterpret_cast<const uint8_t*>(&len);
            out.insert(out.end(), len_bytes, len_bytes + sizeof(len));
            out.insert(out.end(), s.begin(), s.end());
        }
    }
    return out;
}

Row Page::DeserializeRow(const std::vector<uint8_t>& raw) {
    Row row;
    std::size_t i = 0;
    while (i < raw.size()) {
        uint8_t tag = raw[i++];
        if (tag == 0) {
            row.values.push_back(Value::Null());
        } else if (tag == 1) {
            int64_t val = 0;
            std::memcpy(&val, raw.data() + i, sizeof(val));
            i += sizeof(val);
            row.values.push_back(Value::Int(val));
        } else if (tag == 2) {
            uint16_t len = 0;
            std::memcpy(&len, raw.data() + i, sizeof(len));
            i += sizeof(len);
            std::string s(raw.begin() + static_cast<long>(i),
                          raw.begin() + static_cast<long>(i + len));
            i += len;
            row.values.push_back(Value::Str(std::move(s)));
        } else {
            throw std::runtime_error("Unknown tuple tag");
        }
    }
    return row;
}

}  // namespace minidb
