#pragma once

#include "storage/buffer.h"
#include "storage/page.h"
#include "query/parser.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

struct Tuple {
    std::vector<std::string> values;
    RID rid;
    
    // MVCC visibility fields copied during scan
    TxId_t xmin = 0;
    TxId_t xmax = 0;
    RID prev_rid;
};

class Operator {
public:
    virtual ~Operator() = default;
    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple) = 0;
    virtual void Close() = 0;
    virtual const std::vector<std::string>& GetSchema() const = 0;
};

// Visibility checker callback signature
using VisibilityChecker_t = std::function<bool(TxId_t xmin, TxId_t xmax)>;

class SeqScanExecutor : public Operator {
public:
    SeqScanExecutor(const std::string& table_name,
                    const std::vector<std::string>& schema,
                    PageId_t first_page_id,
                    BufferPoolManager* bpm,
                    VisibilityChecker_t visibility_checker = nullptr);

    void Init() override;
    bool Next(Tuple* tuple) override;
    void Close() override;
    const std::vector<std::string>& GetSchema() const override { return schema_; }

private:
    std::string table_name_;
    std::vector<std::string> schema_;
    PageId_t first_page_id_;
    BufferPoolManager* bpm_;
    VisibilityChecker_t visibility_checker_;

    PageId_t curr_page_id_ = INVALID_PAGE_ID;
    uint16_t curr_slot_id_ = 0;
    Page* curr_page_ = nullptr;
};

class IndexScanExecutor : public Operator {
public:
    IndexScanExecutor(const std::string& table_name,
                      const std::vector<std::string>& schema,
                      const std::vector<RID>& rids,
                      BufferPoolManager* bpm,
                      VisibilityChecker_t visibility_checker = nullptr);

    void Init() override;
    bool Next(Tuple* tuple) override;
    void Close() override;
    const std::vector<std::string>& GetSchema() const override { return schema_; }

private:
    std::string table_name_;
    std::vector<std::string> schema_;
    std::vector<RID> rids_;
    BufferPoolManager* bpm_;
    VisibilityChecker_t visibility_checker_;

    size_t curr_idx_ = 0;
};

class FilterExecutor : public Operator {
public:
    FilterExecutor(std::unique_ptr<Operator> child, const SQLWhereCondition& condition);

    void Init() override;
    bool Next(Tuple* tuple) override;
    void Close() override;
    const std::vector<std::string>& GetSchema() const override { return child_->GetSchema(); }

private:
    bool Evaluate(const Tuple& tuple);

    std::unique_ptr<Operator> child_;
    SQLWhereCondition condition_;
    int col_idx_ = -1;
};

class ProjectExecutor : public Operator {
public:
    ProjectExecutor(std::unique_ptr<Operator> child, const std::vector<std::string>& projection_fields);

    void Init() override;
    bool Next(Tuple* tuple) override;
    void Close() override;
    const std::vector<std::string>& GetSchema() const override { return projected_schema_; }

private:
    std::unique_ptr<Operator> child_;
    std::vector<std::string> projection_fields_;
    std::vector<std::string> projected_schema_;
    std::vector<int> col_indices_;
};

class NestedLoopJoinExecutor : public Operator {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Operator> outer,
                           std::unique_ptr<Operator> inner,
                           const std::string& outer_join_col,
                           const std::string& inner_join_col);

    void Init() override;
    bool Next(Tuple* tuple) override;
    void Close() override;
    const std::vector<std::string>& GetSchema() const override { return schema_; }

private:
    std::unique_ptr<Operator> outer_;
    std::unique_ptr<Operator> inner_;
    std::string outer_join_col_;
    std::string inner_join_col_;
    std::vector<std::string> schema_;

    int outer_col_idx_ = -1;
    int inner_col_idx_ = -1;
    Tuple outer_tuple_;
    bool has_outer_tuple_ = false;
};
