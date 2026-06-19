#include "vectorized/vectorized_engine.h"

#include <cstring>
#include <vector>

#include "common/value.h"
#include "storage/page.h"
#include "storage/table_page.h"
#include "storage/tuple.h"

namespace minidb {

long VectorizedEngine::FilterSum(int filter_col, int32_t threshold, int sum_col) const {
  const int foff = 4 * filter_col;  // all-INTEGER fixed offsets
  const int soff = 4 * sum_col;
  std::vector<int32_t> fbuf, sbuf;

  long total = 0;
  page_id_t pid = first_page_;
  while (pid != INVALID_PAGE_ID) {
    Page *page = bpm_->FetchPage(pid);  // one fetch per page, not per tuple
    if (page == nullptr) break;
    uint16_t nslots = TablePage::GetNumSlots(page);

    // Gather this page's live tuples into int32 columns, decoded in place.
    fbuf.clear();
    sbuf.clear();
    for (uint16_t s = 0; s < nslots; s++) {
      uint16_t len;
      const char *data = TablePage::RawTuple(page, s, &len);
      if (data == nullptr) continue;  // tombstone
      int32_t fv, sv;
      std::memcpy(&fv, data + foff, 4);
      std::memcpy(&sv, data + soff, 4);
      fbuf.push_back(fv);
      sbuf.push_back(sv);
    }
    page_id_t next = TablePage::GetNextPageId(page);
    bpm_->UnpinPage(pid, false);

    // Filter + aggregate the page batch in a tight loop over arrays.
    const int m = static_cast<int>(fbuf.size());
    const int32_t *fp = fbuf.data();
    const int32_t *sp = sbuf.data();
    for (int i = 0; i < m; i++) {
      if (fp[i] < threshold) total += sp[i];
    }
    pid = next;
  }
  return total;
}

long VectorizedEngine::RowAtATimeFilterSum(int filter_col, int32_t threshold, int sum_col) const {
  long total = 0;
  for (auto it = heap_->Begin(); it != heap_->End(); ++it) {
    Tuple t = *it;
    std::vector<Value> vals = t.GetValues(*schema_);  // per-tuple Value materialization
    if (vals[filter_col].GetInt() < threshold) total += vals[sum_col].GetInt();
  }
  return total;
}

}  // namespace minidb
