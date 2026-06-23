#pragma once
#include "sql/parser.h"
#include "storage/buffer_pool.h"
#include "index/bplus_tree.h"
#include "transaction/transaction.h"
#include "recovery/log_manager.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <cstring>

namespace minidb {

enum class ColumnType { INT, VARCHAR };

struct Column {
    std::string name;
    ColumnType type;
    size_t size; // Used for VARCHAR, for INT can be 4
};

struct Schema {
    std::vector<Column> columns;
};

class TupleSerializer {
public:
    // Serializes a list of string values into a byte array representing the Tuple
    static Tuple Serialize(const std::vector<std::string>& values, const Schema& schema, int32_t created_by = 0, int32_t deleted_by = -1) {
        std::vector<uint8_t> data;
        
        uint8_t* p_c = reinterpret_cast<uint8_t*>(&created_by);
        data.insert(data.end(), p_c, p_c + sizeof(int32_t));
        uint8_t* p_d = reinterpret_cast<uint8_t*>(&deleted_by);
        data.insert(data.end(), p_d, p_d + sizeof(int32_t));
        
        for (size_t i = 0; i < values.size() && i < schema.columns.size(); ++i) {
            if (schema.columns[i].type == ColumnType::INT) {
                int32_t val = std::stoi(values[i]);
                uint8_t* p = reinterpret_cast<uint8_t*>(&val);
                data.insert(data.end(), p, p + sizeof(int32_t));
            } else if (schema.columns[i].type == ColumnType::VARCHAR) {
                int32_t len = values[i].length();
                uint8_t* p = reinterpret_cast<uint8_t*>(&len);
                data.insert(data.end(), p, p + sizeof(int32_t));
                for (char c : values[i]) {
                    data.push_back(static_cast<uint8_t>(c));
                }
            }
        }
        return Tuple(std::move(data));
    }

    // Deserializes a Tuple's byte array back into a list of string values
    static std::vector<std::string> Deserialize(const Tuple& tuple, const Schema& schema) {
        std::vector<std::string> values;
        size_t offset = 8; // Skip MVCC headers
        for (const auto& col : schema.columns) {
            if (offset >= tuple.data_.size()) break;
            
            if (col.type == ColumnType::INT) {
                int32_t val;
                std::memcpy(&val, tuple.data_.data() + offset, sizeof(int32_t));
                values.push_back(std::to_string(val));
                offset += sizeof(int32_t);
            } else if (col.type == ColumnType::VARCHAR) {
                int32_t len;
                std::memcpy(&len, tuple.data_.data() + offset, sizeof(int32_t));
                offset += sizeof(int32_t);
                std::string str(reinterpret_cast<const char*>(tuple.data_.data() + offset), len);
                values.push_back(str);
                offset += len;
            }
        }
        return values;
    }
};

struct TableInfo {
    std::string name;
    page_id_t first_page_id;
    page_id_t last_page_id;
    BPlusTree *index;
    Schema schema;
};

class MVCCManager;

class ExecutorContext {
public:
    BufferPool *buffer_pool_;
    Transaction *txn_;
    std::unordered_map<std::string, TableInfo> catalog_;
    MVCCManager *mvcc_manager_ = nullptr;
    
    // Map from txn_id to their write set
    std::unordered_map<int32_t, std::vector<LogRecord>> write_sets_;
    
    void SaveCatalog(const std::string &filename);
    void LoadCatalog(const std::string &filename);
};

class Operator {
public:
    virtual ~Operator() = default;
    virtual void Open() = 0;
    virtual bool Next(Tuple *tuple) = 0;
    virtual void Close() = 0;
};

class SeqScan : public Operator {
public:
    SeqScan(ExecutorContext *ctx, const std::string &table_name, const std::string &filter_col = "", const std::string &filter_op = "", const std::string &filter_val = "");
    void Open() override;
    bool Next(Tuple *tuple) override;
    void Close() override;
private:
    ExecutorContext *ctx_;
    std::string table_name_;
    std::string filter_col_;
    std::string filter_op_;
    std::string filter_val_;
    
    page_id_t current_page_id_;
    int current_slot_id_;
    Page *current_page_;
};

class IndexScan : public Operator {
public:
    IndexScan(ExecutorContext *ctx, const std::string &table_name, int32_t key);
    void Open() override;
    bool Next(Tuple *tuple) override;
    void Close() override;
private:
    ExecutorContext *ctx_;
    std::string table_name_;
    int32_t key_;
    bool done_;
};

class NestedLoopJoin : public Operator {
public:
    NestedLoopJoin(ExecutorContext *ctx, std::unique_ptr<Operator> left, std::unique_ptr<Operator> right,
                   const std::string &left_table, const std::string &right_table,
                   const std::string &left_cond, const std::string &right_cond);

    void Open() override;
    bool Next(Tuple *tuple) override;
    void Close() override;

private:
    ExecutorContext *ctx_;
    std::unique_ptr<Operator> left_;
    std::unique_ptr<Operator> right_;
    std::string left_table_;
    std::string right_table_;
    std::string left_cond_;
    std::string right_cond_;
    
    bool has_left_;
    Tuple left_tuple_;
};

} // namespace minidb
