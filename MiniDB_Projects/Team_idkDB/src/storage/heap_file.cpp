#include "minidb/storage/heap_file.h"

#include <cstring>
#include <stdexcept>

#include "minidb/common/trace.h"

namespace minidb {
namespace {

constexpr std::uint32_t kHeapMagic = 0x48454150;
constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kSlotSize = 8;

template <typename T>
T ReadAt(const std::array<std::byte, kPageSize> &data, std::size_t offset) {
  T value{};
  std::memcpy(&value, data.data() + offset, sizeof(T));
  return value;
}

template <typename T>
void WriteAt(std::array<std::byte, kPageSize> &data, std::size_t offset,
             T value) {
  std::memcpy(data.data() + offset, &value, sizeof(T));
}

void Initialize(Page &page) {
  page.data.fill(std::byte{0});
  WriteAt<std::uint32_t>(page.data, 0, kHeapMagic);
  WriteAt<std::uint16_t>(page.data, 4, 0);
  WriteAt<std::uint16_t>(page.data, 6, static_cast<std::uint16_t>(kHeaderSize));
  WriteAt<std::uint16_t>(page.data, 8, static_cast<std::uint16_t>(kPageSize));
}

std::vector<std::byte> Encode(const Record &record) {
  std::vector<std::byte> bytes(sizeof(std::int32_t) + record.value.size());
  const auto key = static_cast<std::int32_t>(record.key);
  std::memcpy(bytes.data(), &key, sizeof(key));
  if (!record.value.empty()) {
    std::memcpy(bytes.data() + sizeof(key), record.value.data(),
                record.value.size());
  }
  return bytes;
}

std::optional<SlotId> InsertInto(Page &page, const Record &record) {
  const auto bytes = Encode(record);
  auto count = ReadAt<std::uint16_t>(page.data, 4);
  auto free_start = ReadAt<std::uint16_t>(page.data, 6);
  auto free_end = ReadAt<std::uint16_t>(page.data, 8);
  std::optional<SlotId> reusable;
  for (SlotId slot = 0; slot < count; ++slot) {
    if (ReadAt<std::uint16_t>(page.data, kHeaderSize + slot * kSlotSize + 4) ==
        0) {
      reusable = slot;
      break;
    }
  }
  const std::size_t required = bytes.size() + (reusable ? 0 : kSlotSize);
  if (free_end < free_start ||
      static_cast<std::size_t>(free_end - free_start) < required) {
    return std::nullopt;
  }
  free_end = static_cast<std::uint16_t>(free_end - bytes.size());
  std::memcpy(page.data.data() + free_end, bytes.data(), bytes.size());
  const SlotId slot = reusable.value_or(count);
  const std::size_t slot_offset = kHeaderSize + slot * kSlotSize;
  WriteAt<std::uint16_t>(page.data, slot_offset, free_end);
  WriteAt<std::uint16_t>(page.data, slot_offset + 2,
                         static_cast<std::uint16_t>(bytes.size()));
  WriteAt<std::uint16_t>(page.data, slot_offset + 4, 1);
  if (!reusable) {
    count++;
    free_start = static_cast<std::uint16_t>(free_start + kSlotSize);
    WriteAt<std::uint16_t>(page.data, 4, count);
    WriteAt<std::uint16_t>(page.data, 6, free_start);
  }
  WriteAt<std::uint16_t>(page.data, 8, free_end);
  return slot;
}

std::optional<Record> ReadFrom(const Page &page, SlotId slot) {
  if (ReadAt<std::uint32_t>(page.data, 0) != kHeapMagic) return std::nullopt;
  const auto count = ReadAt<std::uint16_t>(page.data, 4);
  if (slot >= count) return std::nullopt;
  const std::size_t base = kHeaderSize + slot * kSlotSize;
  if (ReadAt<std::uint16_t>(page.data, base + 4) == 0) return std::nullopt;
  const auto offset = ReadAt<std::uint16_t>(page.data, base);
  const auto length = ReadAt<std::uint16_t>(page.data, base + 2);
  if (length < sizeof(std::int32_t) || offset + length > kPageSize) {
    throw std::runtime_error("corrupt heap slot");
  }
  std::int32_t key{};
  std::memcpy(&key, page.data.data() + offset, sizeof(key));
  std::string value(length - sizeof(key), '\0');
  if (!value.empty()) {
    std::memcpy(value.data(), page.data.data() + offset + sizeof(key),
                value.size());
  }
  return Record{key, std::move(value)};
}

}  // namespace

HeapFile::HeapFile(BufferPoolManager &buffer) : buffer_(buffer) {}

RID HeapFile::Insert(const Record &record) {
  if (record.value.size() + sizeof(std::int32_t) + kHeaderSize + kSlotSize >
      kPageSize) {
    throw std::invalid_argument("record is too large for a page");
  }
  for (std::size_t id = 0; id < PageCount(); ++id) {
    auto guard = buffer_.FetchPage(static_cast<PageId>(id));
    if (auto slot = InsertInto(guard.Get(), record)) {
      guard.MarkDirty();
      return {static_cast<PageId>(id), *slot};
    }
  }
  auto guard = buffer_.NewPage();
  Initialize(guard.Get());
  auto slot = InsertInto(guard.Get(), record);
  guard.MarkDirty();
  if (!slot) throw std::runtime_error("new heap page rejected record");
  Trace::Log("BUFFER", "heap insert on page " +
                           std::to_string(guard->page_id));
  return {guard->page_id, *slot};
}

std::optional<Record> HeapFile::Read(RID rid) {
  if (rid.page_id < 0 || static_cast<std::size_t>(rid.page_id) >= PageCount()) {
    return std::nullopt;
  }
  auto guard = buffer_.FetchPage(rid.page_id);
  return ReadFrom(guard.Get(), rid.slot_id);
}

bool HeapFile::Delete(RID rid) {
  if (rid.page_id < 0 || static_cast<std::size_t>(rid.page_id) >= PageCount()) {
    return false;
  }
  auto guard = buffer_.FetchPage(rid.page_id);
  const auto count = ReadAt<std::uint16_t>(guard->data, 4);
  if (rid.slot_id >= count) return false;
  const std::size_t base = kHeaderSize + rid.slot_id * kSlotSize;
  if (ReadAt<std::uint16_t>(guard->data, base + 4) == 0) return false;
  WriteAt<std::uint16_t>(guard->data, base + 4, 0);
  guard.MarkDirty();
  return true;
}

std::vector<std::pair<RID, Record>> HeapFile::Scan() {
  std::vector<std::pair<RID, Record>> records;
  for (std::size_t id = 0; id < PageCount(); ++id) {
    auto guard = buffer_.FetchPage(static_cast<PageId>(id));
    if (ReadAt<std::uint32_t>(guard->data, 0) != kHeapMagic) continue;
    const auto count = ReadAt<std::uint16_t>(guard->data, 4);
    for (SlotId slot = 0; slot < count; ++slot) {
      if (auto record = ReadFrom(guard.Get(), slot)) {
        records.emplace_back(RID{static_cast<PageId>(id), slot},
                             std::move(*record));
      }
    }
  }
  return records;
}

std::size_t HeapFile::PageCount() const {
  // All pages in a table file are heap pages.
  return buffer_.DiskPageCount();
}

}  // namespace minidb
