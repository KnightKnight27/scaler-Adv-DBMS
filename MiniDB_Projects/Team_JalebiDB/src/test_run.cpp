#include "common/config.h"
#include "common/types.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "recovery/log_manager.h"
#include "concurrency/lock_manager.h"
#include "concurrency/transaction_manager.h"
#include "index/b_plus_tree.h"
#include <iostream>

using namespace minidb;

int main() {
    std::remove("test_db.db");
    std::remove("test_db.db.log");

    auto disk_mgr = std::make_unique<DiskManager>("test_db.db");
    auto log_mgr = std::make_unique<LogManager>(disk_mgr.get());
    auto bpm = std::make_unique<BufferPoolManager>(10, disk_mgr.get(), log_mgr.get());
    auto lock_mgr = std::make_unique<LockManager>();
    auto txn_mgr = std::make_unique<TransactionManager>(lock_mgr.get(), log_mgr.get(), bpm.get());

    page_id_t first_page = bpm->NewPage()->GetPageId();
    page_id_t root_page = bpm->NewPage()->GetPageId();

    BPlusTreeNode root(bpm->FetchPage(root_page));
    root.SetIsLeaf(true);
    root.SetSize(0);
    root.SetParentPageId(INVALID_PAGE_ID);
    root.SetNextPageId(INVALID_PAGE_ID);
    bpm->UnpinPage(root_page, true);
    bpm->UnpinPage(first_page, true);

    BPlusTree tree(root_page, bpm.get());
    Transaction *txn = txn_mgr->Begin();

    std::cout << "Starting B+ Tree inserts..." << std::endl;
    for (int i = 1; i <= 100; ++i) {
        std::cout << "Inserting key " << i << "..." << std::endl;
        RID rid{0, i};
        tree.Insert(i, rid);
    }
    std::cout << "All inserts completed successfully!" << std::endl;

    txn_mgr->Commit(txn);
    return 0;
}
