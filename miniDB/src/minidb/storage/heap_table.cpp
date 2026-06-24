#include "minidb/storage/heap_table.h"

namespace minidb {

HeapTable::HeapTable(BufferPool& buffer, PageId first_page) : buffer_(buffer), first_page_(first_page) {}

PageId HeapTable::Create(BufferPool& buffer) {
  Page& page = buffer.NewPage();
  SlottedPage(page).Init();
  PageId id = page.page_id();
  buffer.UnpinPage(id, true);
  return id;
}

Rid HeapTable::Insert(std::string_view encoded_row) {
  PageId current = first_page_;
  while (true) {
    Page& page = buffer_.FetchPage(current);
    SlottedPage slotted(page);
    if (auto slot = slotted.Insert(encoded_row); slot.has_value()) {
      buffer_.UnpinPage(current, true);
      return Rid{current, *slot};
    }
    PageId next = slotted.NextPage();
    if (next == kInvalidPageId) {
      Page& new_page = buffer_.NewPage();
      SlottedPage(new_page).Init();
      next = new_page.page_id();
      buffer_.UnpinPage(next, true);
      slotted.SetNextPage(next);
      buffer_.UnpinPage(current, true);
      current = next;
    } else {
      buffer_.UnpinPage(current, false);
      current = next;
    }
  }
}

std::optional<std::string> HeapTable::Get(Rid rid) {
  if (!rid.IsValid()) {
    return std::nullopt;
  }
  Page& page = buffer_.FetchPage(rid.page_id);
  auto record = SlottedPage(page).Get(rid.slot_id);
  buffer_.UnpinPage(rid.page_id, false);
  return record;
}

bool HeapTable::Delete(Rid rid) {
  if (!rid.IsValid()) {
    return false;
  }
  Page& page = buffer_.FetchPage(rid.page_id);
  bool deleted = SlottedPage(page).Delete(rid.slot_id);
  buffer_.UnpinPage(rid.page_id, deleted);
  return deleted;
}

std::vector<std::pair<Rid, std::string>> HeapTable::Scan() {
  std::vector<std::pair<Rid, std::string>> records;
  PageId current = first_page_;
  while (current != kInvalidPageId) {
    Page& page = buffer_.FetchPage(current);
    SlottedPage slotted(page);
    for (auto& [slot_id, row] : slotted.Scan()) {
      records.push_back({Rid{current, slot_id}, row});
    }
    PageId next = slotted.NextPage();
    buffer_.UnpinPage(current, false);
    current = next;
  }
  return records;
}

}  // namespace minidb
