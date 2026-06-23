#include "execution/executor.h"

#include "execution/optimizer.h"
#include "parser/lexer.h"
#include "parser/parser.h"

#include <climits>
#include <stdexcept>

namespace minidb {

namespace {

RecordId ToRecordId(const RowLocation& location) {
    RecordId rid{};
    rid.page_id = location.page_id;
    rid.slot_id = location.slot_index;
    return rid;
}

}  // namespace

int64_t GetIntColumn(const Row& row, const std::string& column) {
    const auto it = row.values.find(column);
    if (it == row.values.end() || it->second.type != ValueType::INT) {
        throw std::runtime_error("Missing int column: " + column);
    }
    return it->second.int_val;
}

std::string GetStringColumn(const Row& row, const std::string& column) {
    const auto it = row.values.find(column);
    if (it == row.values.end() || it->second.type != ValueType::STRING) {
        throw std::runtime_error("Missing string column: " + column);
    }
    return it->second.str_val;
}

bool EvaluatePredicate(const Expr* expr, const Row& row) {
    if (expr == nullptr) {
        return true;
    }

    const auto* binary = dynamic_cast<const BinaryExpr*>(expr);
    if (binary == nullptr) {
        return false;
    }

    if (binary->op == "OR") {
        return EvaluatePredicate(binary->left.get(), row) ||
               EvaluatePredicate(binary->right.get(), row);
    }
    if (binary->op == "AND") {
        return EvaluatePredicate(binary->left.get(), row) &&
               EvaluatePredicate(binary->right.get(), row);
    }

    const auto* column = dynamic_cast<const ColumnRefExpr*>(binary->left.get());
    const auto* literal = dynamic_cast<const LiteralExpr*>(binary->right.get());
    if (column == nullptr || literal == nullptr) {
        return false;
    }

    if (literal->value.type == ValueType::INT) {
        const int64_t left = GetIntColumn(row, column->name);
        const int64_t right = literal->value.int_val;
        if (binary->op == ">") {
            return left > right;
        }
        if (binary->op == "<") {
            return left < right;
        }
        if (binary->op == "=") {
            return left == right;
        }
    } else {
        const std::string left = GetStringColumn(row, column->name);
        const std::string& right = literal->value.str_val;
        if (binary->op == "=") {
            return left == right;
        }
    }

    return false;
}

class SeqScanExecutor : public VolcanoExecutor {
public:
    SeqScanExecutor(const PlanNode& plan, ExecutionContext ctx)
        : table_(plan.table), ctx_(ctx), index_(0) {}

    void Init() override { keys_ = ctx_.catalog->GetRowKeys(table_); }

    std::optional<Row> Next() override {
        while (index_ < keys_.size()) {
            const std::string key = keys_[index_++];
            const auto payload = ctx_.tx_manager->Read(ctx_.txid, key);
            if (!payload.has_value()) {
                continue;
            }
            return ctx_.catalog->DeserializeRow(table_, key, *payload);
        }
        return std::nullopt;
    }

    void Close() override {}

private:
    std::string table_;
    ExecutionContext ctx_;
    std::vector<std::string> keys_;
    std::size_t index_;
};

class IndexScanExecutor : public VolcanoExecutor {
public:
    IndexScanExecutor(const PlanNode& plan, ExecutionContext ctx)
        : table_(plan.table),
          column_(plan.index_scan.column),
          op_(plan.index_scan.op),
          bound_(plan.index_scan.bound),
          ctx_(ctx),
          index_(0) {}

    void Init() override {
        BTree* btree = ctx_.catalog->GetIndex(table_, column_);
        if (btree == nullptr) {
            throw std::runtime_error("Index scan requested without index");
        }

        if (op_ == "=") {
            RecordId rid{};
            if (btree->Search(bound_, &rid)) {
                matches_.push_back(rid);
            }
            return;
        }

        const int64_t cardinality = ctx_.catalog->EstimateTableCardinality(table_);
        const int64_t low = op_ == ">" ? bound_ + 1 : INT64_MIN / 4;
        const int64_t high = op_ == "<" ? bound_ - 1 : INT64_MAX / 4;
        if (op_ == ">") {
            const auto range = btree->RangeSearch(low, high);
            for (const auto& entry : range) {
                matches_.push_back(entry.second);
            }
            return;
        }
        if (op_ == "<") {
            const auto range = btree->RangeSearch(low, high);
            for (const auto& entry : range) {
                matches_.push_back(entry.second);
            }
        }
        (void)cardinality;
    }

    std::optional<Row> Next() override {
        while (index_ < matches_.size()) {
            const RecordId rid = matches_[index_++];
            const std::string row_key = ctx_.catalog->FindRowKeyByRecordId(rid);
            if (row_key.empty()) {
                continue;
            }

            const auto payload = ctx_.tx_manager->Read(ctx_.txid, row_key);
            if (!payload.has_value()) {
                continue;
            }
            Row row = ctx_.catalog->DeserializeRow(table_, row_key, *payload);
            const int64_t value = GetIntColumn(row, column_);
            if (op_ == "=" && value != bound_) {
                continue;
            }
            if (op_ == ">" && value <= bound_) {
                continue;
            }
            if (op_ == "<" && value >= bound_) {
                continue;
            }
            return row;
        }
        return std::nullopt;
    }

    void Close() override {}

private:
    std::string table_;
    std::string column_;
    std::string op_;
    int64_t bound_;
    ExecutionContext ctx_;
    std::vector<RecordId> matches_;
    std::size_t index_;
};

class FilterExecutor : public VolcanoExecutor {
public:
    FilterExecutor(std::unique_ptr<VolcanoExecutor> child, std::unique_ptr<Expr> predicate)
        : child_(std::move(child)), predicate_(std::move(predicate)) {}

    void Init() override { child_->Init(); }

    std::optional<Row> Next() override {
        while (true) {
            std::optional<Row> row = child_->Next();
            if (!row.has_value()) {
                return std::nullopt;
            }
            if (EvaluatePredicate(predicate_.get(), *row)) {
                return row;
            }
        }
    }

    void Close() override { child_->Close(); }

private:
    std::unique_ptr<VolcanoExecutor> child_;
    std::unique_ptr<Expr> predicate_;
};

class ProjectExecutor : public VolcanoExecutor {
public:
    ProjectExecutor(std::unique_ptr<VolcanoExecutor> child, std::string column)
        : child_(std::move(child)), column_(std::move(column)) {}

    void Init() override { child_->Init(); }

    std::optional<Row> Next() override {
        std::optional<Row> row = child_->Next();
        if (!row.has_value()) {
            return std::nullopt;
        }

        Row projected;
        projected.table = row->table;
        projected.row_key = row->row_key;
        const auto it = row->values.find(column_);
        if (it != row->values.end()) {
            projected.values[column_] = it->second;
        }
        return projected;
    }

    void Close() override { child_->Close(); }

private:
    std::unique_ptr<VolcanoExecutor> child_;
    std::string column_;
};

class NestedLoopJoinExecutor : public VolcanoExecutor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<VolcanoExecutor> left,
                             std::unique_ptr<VolcanoExecutor> right, JoinParams join)
        : left_(std::move(left)), right_(std::move(right)), join_(std::move(join)) {}

    void Init() override {
        left_->Init();
        right_->Init();
        current_left_ = left_->Next();
    }

    std::optional<Row> Next() override {
        while (current_left_.has_value()) {
            std::optional<Row> right_row = right_->Next();
            if (!right_row.has_value()) {
                right_->Init();
                current_left_ = left_->Next();
                continue;
            }

            const int64_t left_val = GetIntColumn(*current_left_, join_.left_column);
            const int64_t right_val = GetIntColumn(*right_row, join_.right_column);
            if (left_val != right_val) {
                continue;
            }

            Row joined = *current_left_;
            for (const auto& entry : right_row->values) {
                joined.values[entry.first] = entry.second;
            }
            joined.table = join_.left_table + "+" + join_.right_table;
            return joined;
        }
        return std::nullopt;
    }

    void Close() override {
        left_->Close();
        right_->Close();
    }

private:
    std::unique_ptr<VolcanoExecutor> left_;
    std::unique_ptr<VolcanoExecutor> right_;
    JoinParams join_;
    std::optional<Row> current_left_;
};

class InsertExecutor : public VolcanoExecutor {
public:
    InsertExecutor(const InsertParams& params, ExecutionContext ctx)
        : params_(params), ctx_(ctx), done_(false) {}

    void Init() override {}

    std::optional<Row> Next() override {
        if (done_) {
            return std::nullopt;
        }
        done_ = true;

        const TableDef* table = ctx_.catalog->GetTable(params_.table);
        if (table == nullptr) {
            throw std::runtime_error("Unknown table: " + params_.table);
        }

        std::unordered_map<std::string, Value> values;
        if (!params_.columns.empty()) {
            if (params_.columns.size() != params_.values.size()) {
                throw std::runtime_error("Column/value count mismatch");
            }
            for (std::size_t i = 0; i < params_.columns.size(); ++i) {
                values[params_.columns[i]] = params_.values[i];
            }
        } else {
            const std::size_t count = std::min(table->columns.size(), params_.values.size());
            for (std::size_t i = 0; i < count; ++i) {
                values[table->columns[i].name] = params_.values[i];
            }
        }

        const auto pk_it = values.find(table->primary_key);
        if (pk_it == values.end() || pk_it->second.type != ValueType::INT) {
            throw std::runtime_error("Missing primary key for insert");
        }
        const int64_t pk = pk_it->second.int_val;
        const std::string row_key = ctx_.catalog->MakeRowKey(params_.table, pk);
        const std::string payload = ctx_.catalog->SerializeRow(params_.table, values);
        const RowLocation location =
            ctx_.tx_manager->InsertReturningLocation(ctx_.txid, row_key, payload);
        ctx_.catalog->TrackRow(params_.table, row_key);

        if (ctx_.catalog->HasIndex(params_.table, table->primary_key)) {
            ctx_.catalog->IndexInsert(params_.table, table->primary_key, pk, ToRecordId(location),
                                      row_key);
        }

        Row result;
        result.table = params_.table;
        result.row_key = row_key;
        result.values = values;
        return result;
    }

    void Close() override {}

private:
    InsertParams params_;
    ExecutionContext ctx_;
    bool done_;
};

class DeleteExecutor : public VolcanoExecutor {
public:
    DeleteExecutor(DeleteParams params, ExecutionContext ctx)
        : params_(std::move(params)), ctx_(ctx), index_(0) {}

    void Init() override { keys_ = ctx_.catalog->GetRowKeys(params_.table); }

    std::optional<Row> Next() override {
        while (index_ < keys_.size()) {
            const std::string key = keys_[index_++];
            const auto payload = ctx_.tx_manager->Read(ctx_.txid, key);
            if (!payload.has_value()) {
                continue;
            }
            Row row = ctx_.catalog->DeserializeRow(params_.table, key, *payload);
            if (!EvaluatePredicate(params_.predicate.get(), row)) {
                continue;
            }

            ctx_.tx_manager->Remove(ctx_.txid, key);
            ctx_.catalog->UntrackRow(params_.table, key);

            const TableDef* table = ctx_.catalog->GetTable(params_.table);
            if (table != nullptr && table->primary_key == "id") {
                const int64_t pk = GetIntColumn(row, table->primary_key);
                ctx_.catalog->IndexRemove(params_.table, table->primary_key, pk);
            }
            return row;
        }
        return std::nullopt;
    }

    void Close() override {}

private:
    DeleteParams params_;
    ExecutionContext ctx_;
    std::vector<std::string> keys_;
    std::size_t index_;
};

std::unique_ptr<VolcanoExecutor> ExecutorFactory::Create(const PlanNode& plan,
                                                         const ExecutionContext& ctx) {
    switch (plan.type) {
        case PlanType::SEQ_SCAN:
            return std::make_unique<SeqScanExecutor>(plan, ctx);
        case PlanType::INDEX_SCAN:
            return std::make_unique<IndexScanExecutor>(plan, ctx);
        case PlanType::FILTER: {
            if (plan.children.empty()) {
                throw std::runtime_error("Filter requires child plan");
            }
            auto child = Create(*plan.children.front(), ctx);
            return std::make_unique<FilterExecutor>(std::move(child),
                                                    CloneExpr(plan.filter.predicate.get()));
        }
        case PlanType::PROJECT: {
            if (plan.children.empty()) {
                throw std::runtime_error("Project requires child plan");
            }
            auto child = Create(*plan.children.front(), ctx);
            return std::make_unique<ProjectExecutor>(std::move(child), plan.project.column);
        }
        case PlanType::NESTED_LOOP_JOIN: {
            if (plan.children.size() < 2) {
                throw std::runtime_error("Join requires two children");
            }
            auto left = Create(*plan.children[0], ctx);
            auto right = Create(*plan.children[1], ctx);
            return std::make_unique<NestedLoopJoinExecutor>(std::move(left), std::move(right),
                                                            plan.join);
        }
        case PlanType::INSERT:
            return std::make_unique<InsertExecutor>(plan.insert, ctx);
        case PlanType::DELETE: {
            DeleteParams params;
            params.table = plan.delete_plan.table;
            params.predicate = CloneExpr(plan.delete_plan.predicate.get());
            return std::make_unique<DeleteExecutor>(std::move(params), ctx);
        }
    }
    throw std::runtime_error("Unsupported plan type");
}

QueryEngine::QueryEngine(TransactionManager* tx_manager)
    : tx_manager_(tx_manager), catalog_(tx_manager->GetBufferPoolManager()) {
    if (tx_manager_ == nullptr) {
        throw std::invalid_argument("tx_manager must not be null");
    }
    catalog_.RegisterTable(Catalog::DefaultUsersTable());
    catalog_.RebuildFromTableHeap(tx_manager_->GetTableHeap());
}

void QueryEngine::SeedDemoData() {
    const TxID txid = tx_manager_->Begin();

    const std::vector<std::string> inserts = {
        "INSERT INTO users (id, name, age) VALUES (1, 'Kartik', 20)",
        "INSERT INTO users (id, name, age) VALUES (2, 'Krishank', 30)",
        "INSERT INTO users (id, name, age) VALUES (3, 'Sandip', 15)",
        "INSERT INTO users (id, name, age) VALUES (4, 'Nitish', 17)",
        "INSERT INTO users (id, name, age) VALUES (5, 'Kp', 20)",
    };

    for (const std::string& sql : inserts) {
        Lexer lexer(sql);
        Parser parser(lexer.Tokenize());
        const Statement statement = parser.Parse();
        Optimizer optimizer(&catalog_);
        const std::unique_ptr<PlanNode> plan = optimizer.Optimize(statement);
        (void)ExecutePlanWithTx(*plan, txid);
    }

    tx_manager_->Commit(txid);
}

QueryResult QueryEngine::ExecuteSql(const std::string& sql) {
    Lexer lexer(sql);
    Parser parser(lexer.Tokenize());
    const Statement statement = parser.Parse();

    Optimizer optimizer(&catalog_);
    const std::unique_ptr<PlanNode> plan = optimizer.Optimize(statement);

    const TxID txid = tx_manager_->Begin();
    QueryResult result = ExecutePlanWithTx(*plan, txid);
    result.used_index_scan = PlanUsesIndexScan(*plan);
    result.root_plan_type = plan->type;
    tx_manager_->Commit(txid);
    return result;
}

QueryResult QueryEngine::ExecutePlanWithTx(const PlanNode& plan, TxID txid) {
    return ExecutePlan(plan, txid);
}

std::unique_ptr<PlanNode> QueryEngine::OptimizeStatement(const Statement& statement) {
    Optimizer optimizer(&catalog_);
    return optimizer.Optimize(statement);
}

PlanType QueryEngine::FindAccessPlanType(const PlanNode& plan) const {
    if (plan.type == PlanType::INDEX_SCAN || plan.type == PlanType::SEQ_SCAN) {
        return plan.type;
    }
    for (const auto& child : plan.children) {
        const PlanType child_type = FindAccessPlanType(*child);
        if (child_type == PlanType::INDEX_SCAN || child_type == PlanType::SEQ_SCAN) {
            return child_type;
        }
    }
    return plan.type;
}

bool QueryEngine::PlanUsesIndexScan(const PlanNode& plan) const {
    if (plan.type == PlanType::INDEX_SCAN) {
        return true;
    }
    for (const auto& child : plan.children) {
        if (PlanUsesIndexScan(*child)) {
            return true;
        }
    }
    return false;
}

QueryResult QueryEngine::ExecutePlan(const PlanNode& plan, TxID txid) {
    ExecutionContext ctx{tx_manager_, &catalog_, txid};
    auto executor = BuildExecutor(plan, ctx);
    executor->Init();

    QueryResult result;
    while (true) {
        std::optional<Row> row = executor->Next();
        if (!row.has_value()) {
            break;
        }
        if (plan.type == PlanType::INSERT || plan.type == PlanType::DELETE) {
            continue;
        }

        if (row->values.size() == 1) {
            const Value& value = row->values.begin()->second;
            if (value.type == ValueType::STRING) {
                result.rows.push_back(value.str_val);
            } else {
                result.rows.push_back(std::to_string(value.int_val));
            }
            continue;
        }

        for (const auto& entry : row->values) {
            if (entry.second.type == ValueType::STRING) {
                result.rows.push_back(entry.second.str_val);
            } else {
                result.rows.push_back(std::to_string(entry.second.int_val));
            }
        }
    }
    executor->Close();
    return result;
}

std::unique_ptr<VolcanoExecutor> QueryEngine::BuildExecutor(const PlanNode& plan,
                                                            const ExecutionContext& ctx) {
    return ExecutorFactory::Create(plan, ctx);
}

}  // namespace minidb
