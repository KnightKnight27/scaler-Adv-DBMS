#include "recovery/recovery.h"
#include <unordered_set>

namespace minidb {

using namespace std;

void RecoveryManager::Redo() {
  auto recs = lm_->ReadAll();
  // Repeating history: Replay all INSERT and UPDATE operations
  for (const auto& r : recs) {
    if (r.type == LogRecordType::INSERT) {
      // In a complete integration with buffer pool:
      // Page* page = bp->FetchPage(r.pageId);
      // if (page->GetLSN() < r.lsn) { page->ApplyInsert(r.slotId, r.newData); }
    } else if (r.type == LogRecordType::UPDATE) {
      // In a complete integration with buffer pool:
      // Page* page = bp->FetchPage(r.pageId);
      // if (page->GetLSN() < r.lsn) { page->ApplyUpdate(r.slotId, r.newData); }
    }
  }
}

void RecoveryManager::Undo() {
  auto recs = lm_->ReadAll();
  
  // Identify active (loser) transactions at the time of crash
  unordered_set<int64_t> active_txns;
  for (const auto& r : recs) {
    if (r.type == LogRecordType::BEGIN) {
      active_txns.insert(r.txnId);
    } else if (r.type == LogRecordType::COMMIT || r.type == LogRecordType::ABORT) {
      active_txns.erase(r.txnId);
    }
  }

  // Scan logs backward and revert changes made by active transactions
  for (auto it = recs.rbegin(); it != recs.rend(); ++it) {
    const auto& r = *it;
    if (active_txns.count(r.txnId) == 0) {
      continue;
    }

    if (r.type == LogRecordType::INSERT || r.type == LogRecordType::UPDATE) {
      // Undo: Revert page modification using oldData
      // In a complete integration with buffer pool:
      // Page* page = bp->FetchPage(r.pageId);
      // page->Revert(r.slotId, r.oldData);

      // Write Compensation Log Record (CLR) to WAL
      LogRecord clr;
      clr.type = LogRecordType::CLR;
      clr.txnId = r.txnId;
      clr.pageId = r.pageId;
      clr.slotId = r.slotId;
      clr.oldData = r.oldData;
      clr.prevLSN = r.prevLSN;
      lm_->Append(clr);
    }
  }
}

} // namespace minidb