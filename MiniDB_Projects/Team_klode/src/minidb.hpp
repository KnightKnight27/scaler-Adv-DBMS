#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidb {

struct Row {
    bool deleted = false;
    std::vector<std::string> values;
};

struct TableSchema {
    std::string name;
    std::vector<std::string> columns;
    std::size_t primaryKeyColumn = 0;
};

struct QueryResult {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::string message;
};

class BPlusTree {
public:
    explicit BPlusTree(std::size_t order = 4);
    void insert(int key, std::size_t rowId);
    void remove(int key);
    std::optional<std::size_t> search(int key) const;
    std::vector<int> keys() const;

private:
    struct Node {
        bool leaf = true;
        std::vector<int> keys;
        std::vector<std::size_t> rowIds;
        std::vector<Node*> children;
        Node* next = nullptr;
    };

    std::size_t order_;
    std::vector<Node*> ownedNodes_;
    Node* root_;

    Node* makeNode(bool leaf);
    void splitChild(Node* parent, std::size_t childIndex);
    void insertNonFull(Node* node, int key, std::size_t rowId);
};

class BufferPool {
public:
    explicit BufferPool(std::size_t capacity = 8);
    std::string readPage(const std::string& path, std::size_t pageId);
    void writePage(const std::string& path, std::size_t pageId, const std::string& data);
    void flushAll();

private:
    struct Frame {
        std::string path;
        std::size_t pageId = 0;
        std::string data;
        bool dirty = false;
        bool reference = true;
    };

    std::size_t capacity_;
    std::size_t hand_ = 0;
    std::vector<Frame> frames_;

    Frame& pinFrame(const std::string& path, std::size_t pageId);
    void flush(Frame& frame);
};

class PageManager {
public:
    static constexpr std::size_t pageSize = 4096;

    explicit PageManager(std::string dataDir, std::size_t bufferPages = 8);
    void writeRecords(const std::string& tableName, const std::vector<std::string>& records);
    std::vector<std::string> readRecords(const std::string& tableName);
    void flush();
    std::string tablePath(const std::string& tableName) const;

private:
    std::string dataDir_;
    BufferPool bufferPool_;
};

class Wal {
public:
    explicit Wal(std::string path);
    void append(const std::string& record);
    std::vector<std::string> readAll() const;

private:
    std::string path_;
};

class LockManager {
public:
    bool acquireShared(int txId, const std::string& resource);
    bool acquireExclusive(int txId, const std::string& resource);
    void releaseAll(int txId);
    bool deadlockDetected() const;
    std::string state() const;

private:
    struct LockState {
        std::set<int> sharedOwners;
        std::optional<int> exclusiveOwner;
    };

    std::unordered_map<std::string, LockState> locks_;
    std::unordered_map<int, std::set<int>> waitsFor_;

    void recordWait(int txId, const std::set<int>& blockers);
    bool hasPath(int fromTx, int targetTx, std::set<int>& visited) const;
};

class MiniDB {
public:
    explicit MiniDB(std::string dataDir);

    QueryResult execute(const std::string& sql);
    QueryResult demo();
    void recover();

private:
    struct Table {
        TableSchema schema;
        std::vector<Row> rows;
        BPlusTree primaryIndex;
    };

    struct PendingOp {
        std::string table;
        std::string type;
        Row row;
        std::size_t rowId = 0;
    };

    std::string dataDir_;
    PageManager pages_;
    Wal wal_;
    LockManager locks_;
    std::unordered_map<std::string, Table> tables_;
    std::unordered_map<int, std::vector<PendingOp>> pending_;
    int currentTx_ = 0;
    int nextTx_ = 1;
    bool explicitTx_ = false;

    QueryResult createTable(const std::string& sql);
    QueryResult insertInto(const std::string& sql);
    QueryResult selectFrom(const std::string& sql);
    QueryResult deleteFrom(const std::string& sql);
    QueryResult lockDemo();
    QueryResult perfDemo();
    QueryResult storageDemo();
    QueryResult indexDemo(const std::string& sql);
    QueryResult begin();
    QueryResult commit();
    QueryResult rollback();

    void ensureLoaded(const std::string& tableName);
    void persist(const std::string& tableName);
    void applyInsert(int txId, const std::string& tableName, const Row& row);
    void applyDelete(int txId, const std::string& tableName, std::size_t rowId);
    bool pendingInsertHasPrimaryKey(int txId, const std::string& tableName, int primaryKey) const;
    void rebuildIndex(Table& table);
    void replayWal();
    int txForStatement();
    void autocommitIfNeeded(int txId);
};

std::string renderResult(const QueryResult& result);

} // namespace minidb
