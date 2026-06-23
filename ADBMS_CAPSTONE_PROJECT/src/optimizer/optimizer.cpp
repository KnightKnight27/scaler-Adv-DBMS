#include "optimizer/optimizer.h"
#include "execution/seq_scan_executor.h"
#include "execution/index_scan_executor.h"
#include "execution/insert_executor.h"
#include "execution/delete_executor.h"

namespace minidb {

Optimizer::Optimizer(BufferPool* buffer_pool, DiskManager* disk_manager, BPlusTree* tree, TableStats* stats, LogManager* log_manager)
    : buffer_pool_(buffer_pool), disk_manager_(disk_manager), tree_(tree), stats_(stats), log_manager_(log_manager) {}

std::unique_ptr<Executor> Optimizer::Optimize(const SQLStatement& stmt) {
    if (stmt.type_ == StatementType::SELECT) {
        if (stmt.has_where_) {
            return std::make_unique<IndexScanExecutor>(tree_, buffer_pool_, stmt.where_id_);
        } else {
            return std::make_unique<SeqScanExecutor>(buffer_pool_, disk_manager_);
        }
    } else if (stmt.type_ == StatementType::INSERT) {
        return std::make_unique<InsertExecutor>(tree_, log_manager_, stats_, buffer_pool_, disk_manager_, stmt.insert_id_, stmt.insert_name_);
    } else if (stmt.type_ == StatementType::DELETE) {
        if (stmt.has_where_) {
            auto child = std::make_unique<IndexScanExecutor>(tree_, buffer_pool_, stmt.where_id_);
            return std::make_unique<DeleteExecutor>(std::move(child), tree_, log_manager_, buffer_pool_, disk_manager_);
        } else {
            auto child = std::make_unique<SeqScanExecutor>(buffer_pool_, disk_manager_);
            return std::make_unique<DeleteExecutor>(std::move(child), tree_, log_manager_, buffer_pool_, disk_manager_);
        }
    }
    
    return nullptr;
}

} // namespace minidb
