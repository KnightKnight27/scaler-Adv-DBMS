// =============================================================================
// include/executor/executor.h
// -----------------------------------------------------------------------------
// Executor base contract + Tuple/Value types. See include/executor/README.md
// for the Volcano pattern.
// =============================================================================
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "catalog/catalog_manager.h"
#include "common/status.h"
#include "index/index_manager.h"
#include "recovery/log_record.h"
#include "recovery/wal.h"
#include "storage/buffer_pool.h"
#include "transaction/transaction.h"
#include "transaction/transaction_manager.h"

namespace minidb::executor {

// Concurrency-control mode for the executors driven by one statement.
//   AUTOCOMMIT — no locks; MVCC visibility applies if a reader txn is set
//                (this is the default for ad-hoc CLI / demo statements, so
//                the existing behaviour is preserved).
//   TWO_PL     — strict 2PL: scans acquire S locks, writes acquire X locks
//                on the touched RecordIds; locks are released at commit.
//   MVCC       — non-locking snapshot path: readers filter by isVisible,
//                writers record their write-set for first-updater-wins.
enum class IsoMode { AUTOCOMMIT, TWO_PL, MVCC };

// ----- Value / Tuple -----
struct Value {
    enum class Tag { INT, FLOAT, STRING, BOOL, NULL_ };
    Tag         tag  = Tag::NULL_;
    int32_t     i    = 0;
    float       f    = 0.0f;
    std::string s;
    bool        b    = false;

    static Value makeInt  (int32_t v)            { Value x; x.tag = Tag::INT;   x.i = v;       return x; }
    static Value makeFloat(float v)              { Value x; x.tag = Tag::FLOAT; x.f = v;       return x; }
    static Value makeStr  (std::string v)        { Value x; x.tag = Tag::STRING;x.s = std::move(v); return x; }
    static Value makeBool (bool v)               { Value x; x.tag = Tag::BOOL;  x.b = v;       return x; }
    static Value makeNull ()                     { Value x; x.tag = Tag::NULL_;            return x; }

    std::string toString() const;
};

struct Tuple {
    std::vector<Value> values;

    std::string toString() const;     // comma-separated, for CLI output
};

// ----- ExecutorContext (deps passed down to every executor) -----
//
// `wal` / `currentTxnId` / `lastLsn` are wired only when a statement runs
// inside an implicit transaction driven by the QueryEngine (INSERT/DELETE).
// They default to null / INVALID so a context is always default-constructible
// and so executors can no-op the WAL path with a cheap null check.
struct ExecutorContext {
    storage::BufferPool*              bp   = nullptr;
    catalog::CatalogManager*          cat  = nullptr;
    index::IndexManager*              idx  = nullptr;
    transaction::TransactionManager*  txn  = nullptr;
    recovery::WAL*                    wal  = nullptr;
    TransactionId                     currentTxnId = INVALID_TXN_ID;
    LSN                               lastLsn      = INVALID_LSN;

    // MVCC: the reader transaction whose snapshot filters rows in scans.
    // Set by QueryEngine when a statement runs inside a transaction (every
    // SELECT and the scans under DELETE). nullptr => no visibility filtering.
    const transaction::Transaction*  readerTxn = nullptr;

    // Which concurrency-control protocol the executors should follow.
    IsoMode                           isoMode  = IsoMode::AUTOCOMMIT;
};

// ----- Base class for every executor -----
class Executor {
public:
    explicit Executor(ExecutorContext* ctx) : ctx_(ctx) {}
    virtual ~Executor() = default;

    virtual Status init()                 = 0;
    virtual Status next (Tuple& out)      = 0;
    virtual Status close()                = 0;

protected:
    ExecutorContext* ctx_;
};

} // namespace minidb::executor