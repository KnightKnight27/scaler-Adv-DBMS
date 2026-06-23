#include <iostream>
#include <string>
#include "storage/disk_manager.h"
#include "storage/buffer_pool.h"
#include "index/b_plus_tree.h"
#include "recovery/log_manager.h"
#include "recovery/recovery_manager.h"
#include "server/parser.h"
#include "optimizer/table_stats.h"
#include "optimizer/optimizer.h"

using namespace minidb;

int main() {
    std::cout << "Starting MiniDB with Distributed Replication..." << std::endl;

    // 1. Initialize Primary Node components
    DiskManager disk_manager("minidb_primary.db");
    BufferPool buffer_pool(10, &disk_manager); 
    
    if (disk_manager.GetFileSize() == 0) {
        PageId meta_id = disk_manager.AllocatePage(); // should be 0
        Page* meta_page = buffer_pool.FetchPage(meta_id);
        auto* meta = reinterpret_cast<MetaPage*>(meta_page->data_);
        meta->magic_ = META_MAGIC;
        meta->root_page_id_ = INVALID_PAGE_ID;
        buffer_pool.UnpinPage(meta_id, true);
    }
    BPlusTree b_tree(&buffer_pool, 0); // 0 is meta_page_id
    LogManager log_manager("wal_primary.log");
    
    // 2. Initialize Replica Node components
    DiskManager replica_disk_manager("minidb_replica.db");
    BufferPool replica_buffer_pool(10, &replica_disk_manager); 
    
    if (replica_disk_manager.GetFileSize() == 0) {
        PageId meta_id = replica_disk_manager.AllocatePage(); // should be 0
        Page* meta_page = replica_buffer_pool.FetchPage(meta_id);
        auto* meta = reinterpret_cast<MetaPage*>(meta_page->data_);
        meta->magic_ = META_MAGIC;
        meta->root_page_id_ = INVALID_PAGE_ID;
        replica_buffer_pool.UnpinPage(meta_id, true);
    }
    BPlusTree replica_b_tree(&replica_buffer_pool, 0);
    LogManager replica_log_manager("wal_replica.log");

    // 3. Recovery phase
    std::cout << "Running Recovery on Primary..." << std::endl;
    RecoveryManager recovery_manager(&log_manager, &b_tree);
    recovery_manager.Recover();
    
    std::cout << "Running Recovery on Replica..." << std::endl;
    RecoveryManager replica_recovery_manager(&replica_log_manager, &replica_b_tree);
    replica_recovery_manager.Recover();
    std::cout << "Recovery Complete." << std::endl;

    // 4. Setup Optimizers
    TableStats stats;
    Optimizer optimizer(&buffer_pool, &disk_manager, &b_tree, &stats, &log_manager);

    TableStats replica_stats;
    Optimizer replica_optimizer(&replica_buffer_pool, &replica_disk_manager, &replica_b_tree, &replica_stats, &replica_log_manager);

    // 5. Simple REPL
    std::string query;
    std::cout << "MiniDB > ";
    while (std::getline(std::cin, query)) {
        if (query == "exit" || query == "quit") {
            break;
        }
        if (query == "exit;" || query == "quit;") {
            break;
        }
        if (query.empty()) {
            std::cout << "MiniDB > ";
            continue;
        }

        SQLStatement stmt = Parser::Parse(query);
        
        // Pick the correct optimizer
        Optimizer* active_optimizer = stmt.is_replica_ ? &replica_optimizer : &optimizer;
        
        auto executor = active_optimizer->Optimize(stmt);
        if (executor) {
            executor->Init();
            Tuple result;
            int count = 0;
            while (executor->Next(&result)) {
                if (stmt.type_ == StatementType::SELECT) {
                    std::cout << "| ID: " << result.id << " | Name: " << result.name << " |" << (stmt.is_replica_ ? " (from replica)" : "") << std::endl;
                }
                count++;
            }
            if (stmt.type_ == StatementType::INSERT) {
                std::cout << "Inserted " << count << " rows." << std::endl;
            } else if (stmt.type_ == StatementType::DELETE) {
                std::cout << "Deleted " << count << " rows." << std::endl;
            } else if (stmt.type_ == StatementType::SELECT) {
                std::cout << count << " rows returned." << std::endl;
            }
            
            // Distributed Replication (Statement-Based)
            if (stmt.type_ == StatementType::INSERT || stmt.type_ == StatementType::DELETE) {
                auto replica_executor = replica_optimizer.Optimize(stmt);
                if (replica_executor) {
                    replica_executor->Init();
                    Tuple discard;
                    while (replica_executor->Next(&discard)) {}
                    std::cout << "[Replication] Statement broadcast to replica node." << std::endl;
                }
            }
            
        } else {
            std::cout << "Unsupported or invalid query." << std::endl;
        }

        std::cout << "MiniDB > ";
    }

    std::cout << "Shutting down MiniDB..." << std::endl;
    return 0;
}
