#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"
#include "../storage/page.h"
#include "../storage/buffer_pool.h"
#include "../index/bplus_tree.h"
#include <unordered_map>
#include <memory>
#include <functional>

// Forward declaration of Transaction
class Transaction;

using RID = std::pair<int, int>; // (page_id, slot_id)
using Record = std::unordered_map<std::string, std::string>;

class AbstractExecutor {
public:
    virtual ~AbstractExecutor() = default;
    virtual void open() = 0;
    virtual Optional<std::pair<RID, Record>> next() = 0;
    virtual void close() = 0;
};

class TableScanExecutor : public AbstractExecutor {
private:
    BufferPool& buffer_pool;
    std::string table_name;
    Transaction* txn;
    int num_pages{0};
    int curr_page_id{0};
    int curr_slot_id{0};
    uint8_t* curr_page_data{nullptr};
    std::shared_ptr<Page> curr_page;

public:
    TableScanExecutor(BufferPool& bp, const std::string& table_name, Transaction* txn = nullptr);
    ~TableScanExecutor() override = default;

    void open() override;
    Optional<std::pair<RID, Record>> next() override;
    void close() override;
};

class IndexScanExecutor : public AbstractExecutor {
private:
    BufferPool& buffer_pool;
    std::string table_name;
    BPlusTree* index;
    int start_key;
    int end_key;
    Transaction* txn;
    std::vector<std::pair<int, RID>> matching_rids;
    size_t curr_idx{0};

public:
    IndexScanExecutor(
        BufferPool& bp, const std::string& table_name, BPlusTree* idx, 
        int start, int end, Transaction* txn = nullptr
    );
    ~IndexScanExecutor() override = default;

    void open() override;
    Optional<std::pair<RID, Record>> next() override;
    void close() override;
};

class FilterExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> child;
    WhereClause where;

    bool matches(const Record& record) const;

public:
    FilterExecutor(AbstractExecutor* child_exec, const WhereClause& where);
    ~FilterExecutor() override = default;

    void open() override;
    Optional<std::pair<RID, Record>> next() override;
    void close() override;
};

class NestedLoopJoinExecutor : public AbstractExecutor {
private:
    std::unique_ptr<AbstractExecutor> outer;
    std::function<AbstractExecutor*()> inner_builder;
    JoinClause join_cond;
    std::unique_ptr<AbstractExecutor> curr_inner;
    Optional<std::pair<RID, Record>> curr_outer_res;

    std::string get_val(const Record& record, const std::string& col) const;

public:
    NestedLoopJoinExecutor(
        AbstractExecutor* outer_exec, 
        std::function<AbstractExecutor*()> inner_exec_builder, 
        const JoinClause& cond
    );
    ~NestedLoopJoinExecutor() override = default;

    void open() override;
    Optional<std::pair<RID, Record>> next() override;
    void close() override;
};

class InsertExecutor {
private:
    BufferPool& buffer_pool;
    std::string table_name;
    BPlusTree* index;
    std::vector<std::string> values;
    std::vector<std::string> columns;
    Transaction* txn;

public:
    InsertExecutor(
        BufferPool& bp, const std::string& table_name, BPlusTree* idx,
        const std::vector<std::string>& vals, const std::vector<std::string>& cols,
        Transaction* txn = nullptr
    );

    RID execute();
};

class DeleteExecutor {
private:
    BufferPool& buffer_pool;
    std::string table_name;
    BPlusTree* index;
    std::unique_ptr<AbstractExecutor> child;
    Transaction* txn;

public:
    DeleteExecutor(
        BufferPool& bp, const std::string& table_name, BPlusTree* idx,
        AbstractExecutor* child_exec, Transaction* txn = nullptr
    );

    int execute();
};

#endif
