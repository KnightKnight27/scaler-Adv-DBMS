#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <stdexcept>
#include <algorithm>
#include <list>

using namespace std;

// =============================================================================
// Part 1: Shared Value and AST Structures (from Lab 6 / Infix Evaluator)
// =============================================================================

struct Value {
    enum class Type { INT, STRING, BOOL, NULL_VAL } type;
    int intVal = 0;
    string strVal = "";
    bool boolVal = false;

    Value() : type(Type::NULL_VAL) {}
    Value(int v) : type(Type::INT), intVal(v) {}
    Value(const string& v) : type(Type::STRING), strVal(v) {}
    Value(const char* v) : type(Type::STRING), strVal(v) {}
    Value(bool v) : type(Type::BOOL), boolVal(v) {}

    void print() const {
        if (type == Type::INT) cout << intVal;
        else if (type == Type::STRING) cout << "'" << strVal << "'";
        else if (type == Type::BOOL) cout << (boolVal ? "TRUE" : "FALSE");
        else cout << "NULL";
    }

    string toString() const {
        if (type == Type::INT) return to_string(intVal);
        if (type == Type::STRING) return strVal;
        if (type == Type::BOOL) return boolVal ? "true" : "false";
        return "null";
    }
};

enum class TokenType { INTEGER, STRING, IDENTIFIER, OPERATOR, LPAREN, RPAREN };

struct Token {
    TokenType type;
    string value;
};

enum class ASTNodeType { INTEGER_LITERAL, STRING_LITERAL, IDENTIFIER, BINARY_OP, UNARY_OP };

struct ASTNode {
    ASTNodeType type;
    string value;
    ASTNode* left = nullptr;
    ASTNode* right = nullptr;

    ASTNode(ASTNodeType t, string v, ASTNode* l = nullptr, ASTNode* r = nullptr)
        : type(t), value(v), left(l), right(r) {}

    ~ASTNode() {
        delete left;
        delete right;
    }
};

int getPrecedence(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);
    if (upperOp == "*" || upperOp == "/") return 5;
    if (upperOp == "+" || upperOp == "-") return 4;
    if (upperOp == "=" || upperOp == "!=" || upperOp == "<" || upperOp == ">" || upperOp == "<=" || upperOp == ">=") return 3;
    if (upperOp == "NOT") return 2;
    if (upperOp == "AND") return 1;
    if (upperOp == "OR") return 0;
    return -1;
}

bool isRightAssociative(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);
    if (upperOp == "NOT") return true;
    return false;
}

bool isUnary(const string& op) {
    string upperOp = op;
    for (auto& c : upperOp) c = toupper(c);
    if (upperOp == "NOT") return true;
    return false;
}

vector<Token> shuntingYard(const vector<Token>& tokens) {
    vector<Token> outputQueue;
    stack<Token> operatorStack;

    for (const auto& token : tokens) {
        if (token.type == TokenType::INTEGER || token.type == TokenType::STRING || token.type == TokenType::IDENTIFIER) {
            outputQueue.push_back(token);
        } else if (token.type == TokenType::LPAREN) {
            operatorStack.push(token);
        } else if (token.type == TokenType::RPAREN) {
            while (!operatorStack.empty() && operatorStack.top().type != TokenType::LPAREN) {
                outputQueue.push_back(operatorStack.top());
                operatorStack.pop();
            }
            if (operatorStack.empty()) throw runtime_error("Mismatched parentheses");
            operatorStack.pop();
        } else if (token.type == TokenType::OPERATOR) {
            string op1 = token.value;
            int prec1 = getPrecedence(op1);
            bool rightAssoc1 = isRightAssociative(op1);

            while (!operatorStack.empty() && operatorStack.top().type == TokenType::OPERATOR) {
                string op2 = operatorStack.top().value;
                int prec2 = getPrecedence(op2);
                if ((!rightAssoc1 && prec1 <= prec2) || (rightAssoc1 && prec1 < prec2)) {
                    outputQueue.push_back(operatorStack.top());
                    operatorStack.pop();
                } else {
                    break;
                }
            }
            operatorStack.push(token);
        }
    }

    while (!operatorStack.empty()) {
        if (operatorStack.top().type == TokenType::LPAREN || operatorStack.top().type == TokenType::RPAREN) {
            throw runtime_error("Mismatched parentheses");
        }
        outputQueue.push_back(operatorStack.top());
        operatorStack.pop();
    }
    return outputQueue;
}

ASTNode* buildAST(const vector<Token>& postfix) {
    stack<ASTNode*> astStack;
    for (const auto& token : postfix) {
        if (token.type == TokenType::INTEGER) {
            astStack.push(new ASTNode(ASTNodeType::INTEGER_LITERAL, token.value));
        } else if (token.type == TokenType::STRING) {
            astStack.push(new ASTNode(ASTNodeType::STRING_LITERAL, token.value));
        } else if (token.type == TokenType::IDENTIFIER) {
            astStack.push(new ASTNode(ASTNodeType::IDENTIFIER, token.value));
        } else if (token.type == TokenType::OPERATOR) {
            if (isUnary(token.value)) {
                if (astStack.empty()) throw runtime_error("Malformed expression");
                ASTNode* operand = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::UNARY_OP, token.value, operand));
            } else {
                if (astStack.size() < 2) throw runtime_error("Malformed expression");
                ASTNode* right = astStack.top();
                astStack.pop();
                ASTNode* left = astStack.top();
                astStack.pop();
                astStack.push(new ASTNode(ASTNodeType::BINARY_OP, token.value, left, right));
            }
        }
    }
    if (astStack.size() != 1) {
        while (!astStack.empty()) { delete astStack.top(); astStack.pop(); }
        throw runtime_error("Malformed expression");
    }
    return astStack.top();
}

void printAST(ASTNode* node, int depth = 0) {
    if (!node) return;
    printAST(node->right, depth + 1);
    for (int i = 0; i < depth; ++i) cout << "    ";
    cout << node->value << "\n";
    printAST(node->left, depth + 1);
}

// =============================================================================
// Part 2: SQL Query Parser (from Lab 7)
// =============================================================================

enum class SQLTokenType { SELECT, FROM, WHERE, COMMA, IDENTIFIER, LITERAL_INT, LITERAL_STRING, OPERATOR, LPAREN, RPAREN };

struct SQLToken {
    SQLTokenType type;
    string value;
};

vector<SQLToken> tokenizeSQL(const string& sql) {
    vector<SQLToken> tokens;
    size_t i = 0;
    while (i < sql.length()) {
        char c = sql[i];
        if (isspace(c)) { i++; continue; }
        if (c == ',') { tokens.push_back({SQLTokenType::COMMA, ","}); i++; continue; }
        if (c == '(') { tokens.push_back({SQLTokenType::LPAREN, "("}); i++; continue; }
        if (c == ')') { tokens.push_back({SQLTokenType::RPAREN, ")"}); i++; continue; }
        if (c == '\'') {
            string strVal = "";
            i++;
            while (i < sql.length() && sql[i] != '\'') {
                strVal += sql[i];
                i++;
            }
            if (i >= sql.length()) throw runtime_error("Unterminated string literal");
            i++;
            tokens.push_back({SQLTokenType::LITERAL_STRING, strVal});
            continue;
        }
        if (isdigit(c)) {
            string numVal = "";
            while (i < sql.length() && isdigit(sql[i])) { numVal += sql[i]; i++; }
            tokens.push_back({SQLTokenType::LITERAL_INT, numVal});
            continue;
        }
        if (c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/') {
            string op = "";
            op += c;
            i++;
            if (i < sql.length()) {
                char nextC = sql[i];
                if ((c == '!' && nextC == '=') || (c == '<' && nextC == '=') || (c == '>' && nextC == '=') || (c == '<' && nextC == '>')) {
                    op += nextC;
                    i++;
                }
            }
            if (op == "<>") op = "!=";
            tokens.push_back({SQLTokenType::OPERATOR, op});
            continue;
        }
        if (isalpha(c) || c == '_') {
            string ident = "";
            while (i < sql.length() && (isalnum(sql[i]) || sql[i] == '_')) { ident += sql[i]; i++; }
            string upperIdent = ident;
            for (auto& ch : upperIdent) ch = toupper(ch);

            if (upperIdent == "SELECT") tokens.push_back({SQLTokenType::SELECT, upperIdent});
            else if (upperIdent == "FROM") tokens.push_back({SQLTokenType::FROM, upperIdent});
            else if (upperIdent == "WHERE") tokens.push_back({SQLTokenType::WHERE, upperIdent});
            else if (upperIdent == "AND" || upperIdent == "OR" || upperIdent == "NOT") tokens.push_back({SQLTokenType::OPERATOR, upperIdent});
            else tokens.push_back({SQLTokenType::IDENTIFIER, ident});
            continue;
        }
        throw runtime_error("Invalid character in SQL");
    }
    return tokens;
}

struct SQLQuery {
    string tableName;
    vector<string> columns;
    ASTNode* whereClause = nullptr;
    ~SQLQuery() { delete whereClause; }
};

SQLQuery parseSQL(const string& sqlQueryStr) {
    vector<SQLToken> tokens = tokenizeSQL(sqlQueryStr);
    size_t idx = 0;
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::SELECT) throw runtime_error("Syntax error: query must start with SELECT");
    idx++;

    SQLQuery query;
    bool expectColumn = true;
    while (idx < tokens.size() && tokens[idx].type != SQLTokenType::FROM) {
        if (expectColumn) {
            if (tokens[idx].type == SQLTokenType::IDENTIFIER || (tokens[idx].type == SQLTokenType::OPERATOR && tokens[idx].value == "*")) {
                query.columns.push_back(tokens[idx].value);
                idx++;
                expectColumn = false;
            } else throw runtime_error("Expected column or *");
        } else {
            if (tokens[idx].type == SQLTokenType::COMMA) { idx++; expectColumn = true; }
            else throw runtime_error("Expected comma or FROM");
        }
    }
    if (expectColumn) throw runtime_error("Trailing comma in SELECT");
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::FROM) throw runtime_error("Missing FROM");
    idx++;
    if (idx >= tokens.size() || tokens[idx].type != SQLTokenType::IDENTIFIER) throw runtime_error("Expected table name");
    query.tableName = tokens[idx].value;
    idx++;

    if (idx < tokens.size()) {
        if (tokens[idx].type != SQLTokenType::WHERE) throw runtime_error("Expected WHERE clause");
        idx++;
        vector<Token> whereExprTokens;
        while (idx < tokens.size()) {
            Token t;
            if (tokens[idx].type == SQLTokenType::IDENTIFIER) t.type = TokenType::IDENTIFIER;
            else if (tokens[idx].type == SQLTokenType::LITERAL_INT) t.type = TokenType::INTEGER;
            else if (tokens[idx].type == SQLTokenType::LITERAL_STRING) t.type = TokenType::STRING;
            else if (tokens[idx].type == SQLTokenType::OPERATOR) t.type = TokenType::OPERATOR;
            else if (tokens[idx].type == SQLTokenType::LPAREN) t.type = TokenType::LPAREN;
            else if (tokens[idx].type == SQLTokenType::RPAREN) t.type = TokenType::RPAREN;
            t.value = tokens[idx].value;
            whereExprTokens.push_back(t);
            idx++;
        }
        if (whereExprTokens.empty()) throw runtime_error("Empty WHERE clause");
        query.whereClause = buildAST(shuntingYard(whereExprTokens));
    }
    return query;
}

// =============================================================================
// Part 3: Transaction Manager with MVCC, Strict 2PL & Deadlock Detection
// =============================================================================

typedef uint64_t txid_t;
enum class TxState { ACTIVE, COMMITTED, ABORTED };

struct RowVersion {
    string value;
    txid_t xmin; // TxID that created this version
    txid_t xmax; // TxID that replaced/deleted this version
    RowVersion* prev;

    RowVersion(const string& val, txid_t min_tx)
        : value(val), xmin(min_tx), xmax(0), prev(nullptr) {}
};

struct MVCCRow {
    int id;
    RowVersion* head;

    MVCCRow(int _id, const string& initial_val, txid_t min_tx) {
        id = _id;
        head = new RowVersion(initial_val, min_tx);
    }
    ~MVCCRow() {
        RowVersion* curr = head;
        while (curr) {
            RowVersion* p = curr->prev;
            delete curr;
            curr = p;
        }
    }
};

enum class LockMode { SHARED, EXCLUSIVE };

struct LockRequest {
    txid_t txid;
    LockMode mode;
    bool granted;

    LockRequest(txid_t t, LockMode m, bool g) : txid(t), mode(m), granted(g) {}
};

class TransactionManager {
private:
    txid_t next_txid = 100;
    unordered_map<int, MVCCRow*> database;
    unordered_map<txid_t, TxState> tx_states;
    unordered_map<txid_t, unordered_set<txid_t>> tx_active_snapshots; // TxID -> active txids at start
    unordered_set<txid_t> committed_transactions;

    // Lock manager structures
    unordered_map<int, list<LockRequest>> lock_table; // row_id -> list of requests
    unordered_map<txid_t, unordered_set<int>> locks_held_by_tx; // txid -> set of row_ids

    // Deadlock detection structures
    unordered_map<txid_t, int> wait_list; // txid -> waiting for row_id

    bool checkCycleDFS(txid_t curr, unordered_map<txid_t, unordered_set<txid_t>>& graph, 
                       unordered_set<txid_t>& visited, unordered_set<txid_t>& recStack, 
                       vector<txid_t>& cyclePath) {
        visited.insert(curr);
        recStack.insert(curr);
        cyclePath.push_back(curr);

        for (txid_t neighbor : graph[curr]) {
            if (recStack.find(neighbor) != recStack.end()) {
                cyclePath.push_back(neighbor);
                return true;
            }
            if (visited.find(neighbor) == visited.end()) {
                if (checkCycleDFS(neighbor, graph, visited, recStack, cyclePath)) return true;
            }
        }
        cyclePath.pop_back();
        recStack.erase(curr);
        return false;
    }

public:
    TransactionManager() {
        // Initialize sample rows (row_id, value, creator_txid)
        database[1] = new MVCCRow(1, "Apple", 50);
        database[2] = new MVCCRow(2, "Banana", 50);
        database[3] = new MVCCRow(3, "Cherry", 50);
        committed_transactions.insert(50); // initial rows created by committed Tx 50
    }

    ~TransactionManager() {
        for (auto& pair : database) {
            delete pair.second;
        }
    }

    txid_t start_transaction() {
        txid_t id = next_txid++;
        tx_states[id] = TxState::ACTIVE;
        
        // Take visibility snapshot of currently active transactions
        unordered_set<txid_t> active;
        for (auto& pair : tx_states) {
            if (pair.second == TxState::ACTIVE && pair.first != id) {
                active.insert(pair.first);
            }
        }
        tx_active_snapshots[id] = active;
        
        cout << "[TxManager] Transaction T" << id << " started (State: ACTIVE).\n";
        return id;
    }

    bool is_visible(txid_t reader, RowVersion* ver) {
        txid_t xmin = ver->xmin;
        txid_t xmax = ver->xmax;

        // Check if xmin is visible
        bool xmin_visible = false;
        if (xmin == reader) {
            xmin_visible = true;
        } else if (committed_transactions.find(xmin) != committed_transactions.end()) {
            // Must have started after xmin committed (not active when we started)
            if (tx_active_snapshots[reader].find(xmin) == tx_active_snapshots[reader].end()) {
                xmin_visible = true;
            }
        }

        if (!xmin_visible) return false;

        // Check if xmax is visible
        if (xmax == 0) return true; // version not replaced/deleted
        if (xmax == reader) return false; // version replaced by current tx itself

        bool xmax_committed = (committed_transactions.find(xmax) != committed_transactions.end());
        if (xmax_committed && (tx_active_snapshots[reader].find(xmax) == tx_active_snapshots[reader].end())) {
            return false; // version replaced/deleted by committed tx not active when we started
        }

        return true; // replacement is uncommitted/invisible, so this version is still visible
    }

    // MVCC snapshot read with Strict 2PL Lock
    bool read_row(txid_t tx, int row_id, string& out_val) {
        if (tx_states[tx] != TxState::ACTIVE) {
            cout << "[TxManager] T" << tx << " cannot read: transaction is no longer ACTIVE.\n";
            return false;
        }
        if (database.find(row_id) == database.end()) {
            cout << "[TxManager] Row " << row_id << " not found.\n";
            return false;
        }

        // Acquire Shared Lock (Strict 2PL)
        bool lock_acquired = acquire_lock(tx, row_id, LockMode::SHARED);
        if (!lock_acquired) {
            cout << "[TxManager] T" << tx << " waiting for Shared Lock on Row " << row_id << "\n";
            wait_list[tx] = row_id;
            return false; // transaction is put in lock-wait state
        }

        // Read visible version from chain
        MVCCRow* row = database[row_id];
        RowVersion* curr = row->head;
        while (curr != nullptr) {
            if (is_visible(tx, curr)) {
                out_val = curr->value;
                cout << "[TxManager] T" << tx << " read Row " << row_id << " version (xmin=" << curr->xmin << "): \"" << out_val << "\"\n";
                return true;
            }
            curr = curr->prev;
        }
        cout << "[TxManager] T" << tx << " read Row " << row_id << ": No visible version found.\n";
        return false;
    }

    // MVCC write with Strict 2PL lock (Exclusive lock)
    bool write_row(txid_t tx, int row_id, const string& new_val) {
        if (tx_states[tx] != TxState::ACTIVE) {
            cout << "[TxManager] T" << tx << " cannot write: transaction is no longer ACTIVE.\n";
            return false;
        }
        if (database.find(row_id) == database.end()) {
            cout << "[TxManager] Row " << row_id << " not found.\n";
            return false;
        }

        // Acquire Exclusive Lock (Strict 2PL)
        bool lock_acquired = acquire_lock(tx, row_id, LockMode::EXCLUSIVE);
        if (!lock_acquired) {
            cout << "[TxManager] T" << tx << " waiting for Exclusive Lock on Row " << row_id << "\n";
            wait_list[tx] = row_id;
            return false;
        }

        // Write row version under MVCC: append new version to chain head
        MVCCRow* row = database[row_id];
        RowVersion* curr = row->head;
        
        // Find visible version to update xmax (delete older active version)
        while (curr != nullptr) {
            if (curr->xmax == 0 && is_visible(tx, curr)) {
                curr->xmax = tx; // Mark old version as deleted/replaced by this transaction
                break;
            }
            curr = curr->prev;
        }

        // Create new version with xmin = tx, xmax = 0
        RowVersion* new_ver = new RowVersion(new_val, tx);
        new_ver->prev = row->head;
        row->head = new_ver;

        cout << "[TxManager] T" << tx << " wrote Row " << row_id << ": \"" << new_val << "\" (New version added)\n";
        return true;
    }

    // Strict 2PL Lock manager
    bool acquire_lock(txid_t tx, int row_id, LockMode mode) {
        auto& queue = lock_table[row_id];

        // Check if tx already holds a compatible lock or the exclusive lock
        for (auto& req : queue) {
            if (req.txid == tx && req.granted) {
                if (mode == LockMode::SHARED || req.mode == LockMode::EXCLUSIVE) {
                    return true; // already holds compatible lock
                }
                // Lock upgrade: shared lock -> exclusive lock request
                if (queue.size() == 1) {
                    req.mode = LockMode::EXCLUSIVE;
                    return true;
                }
            }
        }

        // Check for conflicts
        bool conflict = false;
        for (auto& req : queue) {
            if (req.granted && req.txid != tx) {
                if (mode == LockMode::EXCLUSIVE || req.mode == LockMode::EXCLUSIVE) {
                    conflict = true;
                    break;
                }
            }
        }

        if (!conflict) {
            // Grant lock immediately
            queue.push_back(LockRequest(tx, mode, true));
            locks_held_by_tx[tx].insert(row_id);
            return true;
        } else {
            // Put transaction in wait queue (if not already there)
            bool already_waiting = false;
            for (auto& req : queue) {
                if (req.txid == tx && !req.granted) {
                    already_waiting = true;
                    break;
                }
            }
            if (!already_waiting) {
                queue.push_back(LockRequest(tx, mode, false));
            }
            return false;
        }
    }

    void release_locks(txid_t tx) {
        for (int row_id : locks_held_by_tx[tx]) {
            auto& queue = lock_table[row_id];
            queue.remove_if([tx](const LockRequest& req) { return req.txid == tx; });

            // Wake up waiting lock requests
            for (auto& req : queue) {
                if (req.granted) continue;
                
                // check if this waiting request can be granted now
                bool conflict = false;
                for (auto& other : queue) {
                    if (other.granted && other.txid != req.txid) {
                        if (req.mode == LockMode::EXCLUSIVE || other.mode == LockMode::EXCLUSIVE) {
                            conflict = true;
                            break;
                        }
                    }
                }
                if (!conflict) {
                    req.granted = true;
                    locks_held_by_tx[req.txid].insert(row_id);
                    if (wait_list.find(req.txid) != wait_list.end() && wait_list[req.txid] == row_id) {
                        wait_list.erase(req.txid);
                        cout << "[TxManager] Waking up T" << req.txid << " (Granted lock on Row " << row_id << ").\n";
                    }
                }
            }
        }
        locks_held_by_tx.erase(tx);
    }

    // Deadlock detection using Wait-For Graph (WFG) cycle detection
    void detect_deadlocks() {
        cout << "[TxManager] Running deadlock detection...\n";
        
        // Build wait-for graph: active waiting txids -> transaction holding the lock they need
        unordered_map<txid_t, unordered_set<txid_t>> graph;
        for (auto& pair : wait_list) {
            txid_t waiting_tx = pair.first;
            int row_id = pair.second;

            // Find who holds the granted lock on row_id
            for (auto& req : lock_table[row_id]) {
                if (req.granted && req.txid != waiting_tx) {
                    graph[waiting_tx].insert(req.txid);
                }
            }
        }

        // Print Wait-For Graph edges
        cout << "[WFG] Current dependencies:\n";
        bool has_edges = false;
        for (auto& pair : graph) {
            for (txid_t held : pair.second) {
                cout << "  T" << pair.first << " waits for T" << held << "\n";
                has_edges = true;
            }
        }
        if (!has_edges) cout << "  No transaction dependencies.\n";

        // Check for cycles
        unordered_set<txid_t> visited;
        unordered_set<txid_t> recStack;
        vector<txid_t> cyclePath;

        for (auto& pair : graph) {
            txid_t startNode = pair.first;
            if (visited.find(startNode) == visited.end()) {
                if (checkCycleDFS(startNode, graph, visited, recStack, cyclePath)) {
                    cout << "[TxManager] DEADLOCK DETECTED! Cycle: ";
                    for (size_t i = 0; i < cyclePath.size(); i++) {
                        cout << "T" << cyclePath[i] << (i + 1 < cyclePath.size() ? " -> " : "");
                    }
                    cout << "\n";

                    // Pick the youngest transaction in the cycle (highest txid) to abort
                    txid_t youngest = 0;
                    for (txid_t tx : cyclePath) {
                        if (tx > youngest) youngest = tx;
                    }

                    cout << "[TxManager] Aborting T" << youngest << " (youngest transaction) to break deadlock.\n";
                    abort(youngest);
                    return; // cycle resolved, abort handles lock releases and wake-ups
                }
            }
        }
    }

    void commit(txid_t tx) {
        if (tx_states[tx] != TxState::ACTIVE) return;
        tx_states[tx] = TxState::COMMITTED;
        committed_transactions.insert(tx);
        cout << "[TxManager] T" << tx << " COMMITTED.\n";
        
        // Release locks held by this transaction (Strict 2PL)
        release_locks(tx);
        wait_list.erase(tx);
    }

    void abort(txid_t tx) {
        if (tx_states[tx] != TxState::ACTIVE) return;
        tx_states[tx] = TxState::ABORTED;
        cout << "[TxManager] T" << tx << " ABORTED (Transaction rolled back).\n";

        // Roll back MVCC changes made by this transaction: clean/revert versions
        for (auto& pair : database) {
            MVCCRow* row = pair.second;
            RowVersion* prev_node = nullptr;
            RowVersion* curr = row->head;

            while (curr != nullptr) {
                // If this version was created by aborted tx, unlink it
                if (curr->xmin == tx) {
                    RowVersion* to_delete = curr;
                    if (prev_node == nullptr) {
                        row->head = curr->prev;
                    } else {
                        prev_node->prev = curr->prev;
                    }
                    curr = curr->prev;
                    delete to_delete;
                } else {
                    // Revert xmax if it was set by this aborted transaction
                    if (curr->xmax == tx) {
                        curr->xmax = 0;
                    }
                    prev_node = curr;
                    curr = curr->prev;
                }
            }
        }

        // Release locks
        release_locks(tx);
        wait_list.erase(tx);
    }

    void printDatabase() {
        cout << "--- Current DB State (MVCC Chains) ---\n";
        for (int id = 1; id <= 3; id++) {
            MVCCRow* row = database[id];
            cout << "Row " << id << " versions:";
            RowVersion* curr = row->head;
            while (curr != nullptr) {
                cout << " -> [val=\"" << curr->value << "\", xmin=" << curr->xmin << ", xmax=" << curr->xmax << "]";
                curr = curr->prev;
            }
            cout << "\n";
        }
        cout << "-------------------------------------\n";
    }
};

// =============================================================================
// Part 4: Test Runners
// =============================================================================

int main() {
    cout << "========================================================\n";
    cout << "RUNNING PART 1 & 2: SQL QUERY PARSER WITH AST CONSTRUCTION\n";
    cout << "========================================================\n";
    
    // Valid queries
    string q1 = "SELECT id, name, salary FROM employees";
    string q2 = "SELECT * FROM students WHERE grade = 'A'";
    string q3 = "SELECT title, author FROM books WHERE year > 2000 AND price <= 50";
    string q4 = "SELECT name FROM staff WHERE (age >= 18 AND status = 'active') OR NOT dept = 'HR'";

    cout << "Parsing Query 1: " << q1 << "\n";
    SQLQuery query1 = parseSQL(q1);
    cout << "Table Name: " << query1.tableName << "\nColumns: ";
    for (auto& col : query1.columns) cout << col << " ";
    cout << "\n\n";

    cout << "Parsing Query 2: " << q2 << "\n";
    SQLQuery query2 = parseSQL(q2);
    cout << "Table Name: " << query2.tableName << "\n";
    if (query2.whereClause) {
        cout << "WHERE clause AST:\n";
        printAST(query2.whereClause, 0);
    }
    cout << "\n";

    cout << "Parsing Query 3: " << q3 << "\n";
    SQLQuery query3 = parseSQL(q3);
    cout << "Table Name: " << query3.tableName << "\n";
    if (query3.whereClause) {
        cout << "WHERE clause AST:\n";
        printAST(query3.whereClause, 0);
    }
    cout << "\n";

    cout << "Parsing Query 4: " << q4 << "\n";
    SQLQuery query4 = parseSQL(q4);
    cout << "Table Name: " << query4.tableName << "\n";
    if (query4.whereClause) {
        cout << "WHERE clause AST:\n";
        printAST(query4.whereClause, 0);
    }
    cout << "\n";

    cout << "========================================================\n";
    cout << "RUNNING PART 3: C++ TRANSACTION MANAGER WITH MVCC & 2PL\n";
    cout << "========================================================\n";
    
    TransactionManager tm;
    tm.printDatabase();

    cout << "\n--- Scenario 1: MVCC Snapshot Isolation (No Write-Write Conflict) ---\n";
    txid_t T1 = tm.start_transaction();
    txid_t T2 = tm.start_transaction();

    // T1 writes to Row 1
    tm.write_row(T1, 1, "Apple_T1");

    // T2 reads Row 1. Under MVCC, T2 should read the old committed version "Apple"
    string val;
    tm.read_row(T2, 1, val); 
    cout << "Result: T2 read Row 1 value as: \"" << val << "\"\n";

    // Commit T1
    tm.commit(T1);

    // T2 reads Row 1 again. T2 should STILL read "Apple" because T2's snapshot began before T1 committed!
    tm.read_row(T2, 1, val);
    cout << "Result: T2 read Row 1 value (post-T1 commit) as: \"" << val << "\"\n";

    // T3 starts now, should see T1's changes
    txid_t T3 = tm.start_transaction();
    tm.read_row(T3, 1, val);
    cout << "Result: T3 read Row 1 value as: \"" << val << "\"\n";

    tm.commit(T2);
    tm.commit(T3);

    tm.printDatabase();

    cout << "\n--- Scenario 2: Strict 2PL and Deadlock Detection ---\n";
    txid_t T4 = tm.start_transaction();
    txid_t T5 = tm.start_transaction();

    // T4 locks and writes Row 2
    tm.write_row(T4, 2, "Banana_T4");
    // T5 locks and writes Row 3
    tm.write_row(T5, 3, "Cherry_T5");

    // T4 tries to write Row 3. Conflicts with T5's X-lock on Row 3. T4 blocks.
    cout << "T4 attempts to write to Row 3 (held by T5)...\n";
    tm.write_row(T4, 3, "Cherry_T4"); 

    // T5 tries to write Row 2. Conflicts with T4's X-lock on Row 2. T5 blocks.
    cout << "T5 attempts to write to Row 2 (held by T4)...\n";
    tm.write_row(T5, 2, "Banana_T5");

    // Run deadlock detector
    tm.detect_deadlocks();

    // T5 should be aborted (youngest txid=104 vs T4=103), releasing Row 3 lock, waking up T4.
    // Let's verify T4 completes
    cout << "T4 attempts to write to Row 3 again (should succeed since T5 aborted)...\n";
    tm.write_row(T4, 3, "Cherry_T4_Retry");

    tm.commit(T4);
    tm.printDatabase();

    return 0;
}
