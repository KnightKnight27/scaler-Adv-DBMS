#include "table.h"

namespace minidb {

RID TableHeap::insert(txn_id_t txn, const Tuple& tuple) {
  std::vector<uint8_t> bytes = tuple.serialize();
  if (bytes.size() > PAGE_SIZE - slotted::SLOTS_OFF - slotted::SLOT_SZ)
    throw DBException("tuple too large for a page");

  page_id_t npages = dm_->num_pages(fid_);
  for (page_id_t pid = 0; pid < npages; pid++) {
    Page* pg = bpool_->fetch_page(fid_, pid);
    int slot = slotted::insert(pg->data(), bytes.data(), static_cast<uint16_t>(bytes.size()));
    if (slot >= 0) {
      uint16_t off, len;
      slotted::get_slot(pg->data(), slot, off, len);
      lsn_t lsn = log_->log_insert(txn, fid_, pid, slot, off, bytes);
      slotted::set_lsn(pg->data(), lsn);
      bpool_->unpin_page(fid_, pid, true);
      return RID{pid, slot};
    }
    bpool_->unpin_page(fid_, pid, false);
  }

  page_id_t pid;
  Page* pg = bpool_->new_page(fid_, &pid);
  int slot = slotted::insert(pg->data(), bytes.data(), static_cast<uint16_t>(bytes.size()));
  if (slot < 0) {
    bpool_->unpin_page(fid_, pid, true);
    throw DBException("failed to insert into fresh page");
  }
  uint16_t off, len;
  slotted::get_slot(pg->data(), slot, off, len);
  lsn_t lsn = log_->log_insert(txn, fid_, pid, slot, off, bytes);
  slotted::set_lsn(pg->data(), lsn);
  bpool_->unpin_page(fid_, pid, true);
  return RID{pid, slot};
}

bool TableHeap::get(RID rid, Tuple* out) const {
  if (!rid.valid()) return false;
  if (rid.page_id >= dm_->num_pages(fid_)) return false;
  Page* pg = bpool_->fetch_page(fid_, rid.page_id);
  uint16_t off, len;
  if (rid.slot >= slotted::num_slots(pg->data())) {
    bpool_->unpin_page(fid_, rid.page_id, false);
    return false;
  }
  slotted::get_slot(pg->data(), rid.slot, off, len);
  if (len == 0) {
    bpool_->unpin_page(fid_, rid.page_id, false);
    return false;
  }
  *out = Tuple::deserialize(reinterpret_cast<const uint8_t*>(pg->data() + off), len, schema_);
  bpool_->unpin_page(fid_, rid.page_id, false);
  return true;
}

void TableHeap::remove(txn_id_t txn, RID rid) {
  Page* pg = bpool_->fetch_page(fid_, rid.page_id);
  uint16_t off, len;
  slotted::get_slot(pg->data(), rid.slot, off, len);
  if (len == 0) {
    bpool_->unpin_page(fid_, rid.page_id, false);
    return;
  }
  std::vector<uint8_t> old(pg->data() + off, pg->data() + off + len);
  lsn_t lsn = log_->log_delete(txn, fid_, rid.page_id, rid.slot, off, old);
  slotted::set_slot(pg->data(), rid.slot, 0, 0);
  slotted::set_lsn(pg->data(), lsn);
  bpool_->unpin_page(fid_, rid.page_id, true);
}

void TableHeap::scan(const std::function<bool(RID, const Tuple&)>& fn) const {
  page_id_t npages = dm_->num_pages(fid_);
  for (page_id_t pid = 0; pid < npages; pid++) {
    Page* pg = bpool_->fetch_page(fid_, pid);
    uint16_t ns = slotted::num_slots(pg->data());
    bool stop = false;
    for (uint16_t s = 0; s < ns && !stop; s++) {
      uint16_t off, len;
      slotted::get_slot(pg->data(), s, off, len);
      if (len == 0) continue;
      Tuple t = Tuple::deserialize(reinterpret_cast<const uint8_t*>(pg->data() + off), len, schema_);
      if (!fn(RID{pid, s}, t)) stop = true;
    }
    bpool_->unpin_page(fid_, pid, false);
    if (stop) break;
  }
}

}  // namespace minidb
