#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using TransactionId = std::uint64_t;
using Timestamp = std::uint64_t;

constexpr Timestamp kOpenEndedStamp = std::numeric_limits<Timestamp>::max();

enum class TxState {
    Active,
    Committed,
    Aborted
};

struct AccountVersion {
    int balance = 0;
    TransactionId createdBy = 0;
    TransactionId deletedBy = 0;
    Timestamp createStamp = 0;
    Timestamp deleteStamp = kOpenEndedStamp;
    bool tombstone = false;
};

struct PendingMutation {
    bool isErase = false;
    int balance = 0;
};

struct TxRecord {
    TransactionId id = 0;
    Timestamp snapshotStamp = 0;
    TxState state = TxState::Active;
    Timestamp commitStamp = 0;
    std::unordered_map<std::string, PendingMutation> stagedWrites;
    std::set<std::string> heldExclusiveLocks;
};

struct LockBucket {
    TransactionId owner = 0;
};

std::string txStateName(TxState state) {
    if (state == TxState::Active) {
        return "Active";
    }
    if (state == TxState::Committed) {
        return "Committed";
    }
    return "Aborted";
}

std::string txLabel(TransactionId txId) {
    return "TX" + std::to_string(txId);
}

class LedgerTransactionManager {
public:
    explicit LedgerTransactionManager(bool verbose) : verbose_(verbose) {}

    void initializeAccount(const std::string& accountId, int balance) {
        std::lock_guard<std::mutex> guard(mutex_);

        AccountVersion version;
        version.balance = balance;
        version.createdBy = 0;
        version.createStamp = 0;
        version.deleteStamp = kOpenEndedStamp;
        version.tombstone = false;

        ledger_[accountId] = {version};
        logLocked("Initialized " + accountId + " with balance " + std::to_string(balance));
    }

    TransactionId beginTransaction() {
        std::lock_guard<std::mutex> guard(mutex_);

        const TransactionId txId = nextTxId_++;
        TxRecord record;
        record.id = txId;
        record.snapshotStamp = logicalClock_;
        record.state = TxState::Active;
        transactions_.emplace(txId, std::move(record));

        logLocked(txLabel(txId) + " started with snapshot " + std::to_string(logicalClock_));
        return txId;
    }

    std::optional<int> read(TransactionId txId, const std::string& accountId) {
        std::lock_guard<std::mutex> guard(mutex_);
        TxRecord& tx = requireTransactionLocked(txId);
        ensureActiveLocked(tx, "read");

        const auto stagedIt = tx.stagedWrites.find(accountId);
        if (stagedIt != tx.stagedWrites.end()) {
            if (stagedIt->second.isErase) {
                logLocked(txLabel(txId) + " reads " + accountId + " -> <deleted> (own uncommitted delete)");
                return std::nullopt;
            }

            logLocked(txLabel(txId) + " reads " + accountId + " -> " +
                      std::to_string(stagedIt->second.balance) + " (own uncommitted write)");
            return stagedIt->second.balance;
        }

        const AccountVersion* visible = visibleVersionLocked(accountId, tx.snapshotStamp);
        if (visible == nullptr || visible->tombstone) {
            logLocked(txLabel(txId) + " reads " + accountId + " -> <not visible>");
            return std::nullopt;
        }

        logLocked(txLabel(txId) + " reads " + accountId + " -> " + std::to_string(visible->balance) +
                  " (snapshot " + std::to_string(tx.snapshotStamp) + ")");
        return visible->balance;
    }

    void write(TransactionId txId, const std::string& accountId, int newBalance) {
        std::unique_lock<std::mutex> lock(mutex_);
        TxRecord& tx = requireTransactionLocked(txId);
        ensureActiveLocked(tx, "write");

        logLocked(txLabel(txId) + " requests write on " + accountId + " -> " + std::to_string(newBalance));
        acquireExclusiveLockLocked(lock, txId, accountId);

        if (latestCommittedStampLocked(accountId) > tx.snapshotStamp) {
            const std::string message =
                "Serialization failure: account was changed by another committed transaction";
            logLocked(message + " for " + txLabel(txId) + " on " + accountId);
            abortTransactionLocked(txId, message);
            wakeup_.notify_all();
            throw std::runtime_error(message);
        }

        tx.stagedWrites[accountId] = PendingMutation{false, newBalance};
        logLocked(txLabel(txId) + " staged " + accountId + " = " + std::to_string(newBalance));
    }

    void erase(TransactionId txId, const std::string& accountId) {
        std::unique_lock<std::mutex> lock(mutex_);
        TxRecord& tx = requireTransactionLocked(txId);
        ensureActiveLocked(tx, "erase");

        logLocked(txLabel(txId) + " requests delete on " + accountId);
        acquireExclusiveLockLocked(lock, txId, accountId);

        if (latestCommittedStampLocked(accountId) > tx.snapshotStamp) {
            const std::string message =
                "Serialization failure: account was changed by another committed transaction";
            logLocked(message + " for " + txLabel(txId) + " on " + accountId);
            abortTransactionLocked(txId, message);
            wakeup_.notify_all();
            throw std::runtime_error(message);
        }

        tx.stagedWrites[accountId] = PendingMutation{true, 0};
        logLocked(txLabel(txId) + " staged delete for " + accountId);
    }

    void commit(TransactionId txId) {
        std::unique_lock<std::mutex> lock(mutex_);
        TxRecord& tx = requireTransactionLocked(txId);
        ensureActiveLocked(tx, "commit");

        const Timestamp commitStamp = ++logicalClock_;
        logLocked(txLabel(txId) + " is committing at timestamp " + std::to_string(commitStamp));

        for (const auto& entry : tx.stagedWrites) {
            const std::string& accountId = entry.first;
            const PendingMutation& mutation = entry.second;

            std::vector<AccountVersion>& chain = ledger_[accountId];
            if (!chain.empty() && chain.back().deleteStamp == kOpenEndedStamp) {
                chain.back().deleteStamp = commitStamp;
                chain.back().deletedBy = txId;
            }

            AccountVersion version;
            version.balance = mutation.balance;
            version.createdBy = txId;
            version.createStamp = commitStamp;
            version.deleteStamp = kOpenEndedStamp;
            version.tombstone = mutation.isErase;

            chain.push_back(version);
        }

        tx.commitStamp = commitStamp;
        tx.state = TxState::Committed;
        tx.stagedWrites.clear();
        releaseLocksLocked(tx);
        clearWaitEdgesLocked(txId);

        logLocked(txLabel(txId) + " commit successful");
        lock.unlock();
        wakeup_.notify_all();
    }

    void abort(TransactionId txId) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool changed = abortTransactionLocked(txId, "abort requested by caller");
        lock.unlock();
        if (changed) {
            wakeup_.notify_all();
        }
    }

    std::size_t vacuum() {
        std::lock_guard<std::mutex> guard(mutex_);

        Timestamp oldestSnapshot = logicalClock_ + 1;
        bool hasActiveTransaction = false;

        for (const auto& entry : transactions_) {
            if (entry.second.state == TxState::Active) {
                oldestSnapshot = std::min(oldestSnapshot, entry.second.snapshotStamp);
                hasActiveTransaction = true;
            }
        }

        if (!hasActiveTransaction) {
            oldestSnapshot = logicalClock_ + 1;
        }

        std::size_t removedVersions = 0;

        for (auto& ledgerEntry : ledger_) {
            std::vector<AccountVersion>& chain = ledgerEntry.second;
            if (chain.empty()) {
                continue;
            }

            std::set<std::size_t> preservedIndexes;
            preservedIndexes.insert(chain.size() - 1);

            for (std::size_t index = chain.size(); index > 0; --index) {
                const std::size_t current = index - 1;
                if (!chain[current].tombstone) {
                    preservedIndexes.insert(current);
                    break;
                }
            }

            for (const auto& txEntry : transactions_) {
                if (txEntry.second.state != TxState::Active) {
                    continue;
                }

                const std::size_t visibleIndex = visibleIndexLocked(chain, txEntry.second.snapshotStamp);
                if (visibleIndex != invalidIndex()) {
                    preservedIndexes.insert(visibleIndex);
                }
            }

            std::vector<AccountVersion> compacted;
            compacted.reserve(chain.size());

            for (std::size_t index = 0; index < chain.size(); ++index) {
                const AccountVersion& version = chain[index];
                const bool expired = version.deleteStamp != kOpenEndedStamp;
                const bool neededByActiveSnapshot =
                    hasActiveTransaction && version.deleteStamp > oldestSnapshot;

                if (preservedIndexes.count(index) != 0U || !expired || neededByActiveSnapshot) {
                    compacted.push_back(version);
                } else {
                    ++removedVersions;
                }
            }

            chain.swap(compacted);
        }

        logLocked("Vacuum removed " + std::to_string(removedVersions) + " obsolete version(s)");
        return removedVersions;
    }

    std::size_t versionCount(const std::string& accountId) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = ledger_.find(accountId);
        return it == ledger_.end() ? 0U : it->second.size();
    }

    void printVersionChain(const std::string& accountId) const {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto it = ledger_.find(accountId);

        std::cout << "Version chain for " << accountId << '\n';
        if (it == ledger_.end() || it->second.empty()) {
            std::cout << "  <empty>\n";
            return;
        }

        std::size_t index = 0;
        for (const AccountVersion& version : it->second) {
            std::cout << "  [" << index << "] create=" << version.createStamp
                      << ", delete=";
            if (version.deleteStamp == kOpenEndedStamp) {
                std::cout << "INF";
            } else {
                std::cout << version.deleteStamp;
            }

            std::cout << ", state=" << (version.tombstone ? "TOMBSTONE" : "LIVE")
                      << ", balance=";
            if (version.tombstone) {
                std::cout << "<deleted>";
            } else {
                std::cout << version.balance;
            }

            std::cout << ", createdBy=" << version.createdBy
                      << ", deletedBy=" << version.deletedBy << '\n';
            ++index;
        }
    }

    void printCurrentLedger() const {
        std::lock_guard<std::mutex> guard(mutex_);

        std::vector<std::string> accounts;
        accounts.reserve(ledger_.size());
        for (const auto& entry : ledger_) {
            accounts.push_back(entry.first);
        }
        std::sort(accounts.begin(), accounts.end());

        std::cout << "+---------+-----------+\n";
        std::cout << "| Account | Balance   |\n";
        std::cout << "+---------+-----------+\n";

        for (const std::string& accountId : accounts) {
            const std::vector<AccountVersion>& chain = ledger_.at(accountId);
            const AccountVersion& latest = chain.back();

            std::cout << "| " << std::left << std::setw(7) << accountId << " | ";
            if (latest.tombstone) {
                std::cout << std::left << std::setw(9) << "<deleted>";
            } else {
                std::cout << std::left << std::setw(9) << latest.balance;
            }
            std::cout << " |\n";
        }

        std::cout << "+---------+-----------+\n";
    }

private:
    static std::size_t invalidIndex() {
        return std::numeric_limits<std::size_t>::max();
    }

    TxRecord& requireTransactionLocked(TransactionId txId) {
        auto it = transactions_.find(txId);
        if (it == transactions_.end()) {
            throw std::runtime_error("Unknown transaction id: " + std::to_string(txId));
        }
        return it->second;
    }

    const TxRecord& requireTransactionLocked(TransactionId txId) const {
        auto it = transactions_.find(txId);
        if (it == transactions_.end()) {
            throw std::runtime_error("Unknown transaction id: " + std::to_string(txId));
        }
        return it->second;
    }

    void ensureActiveLocked(const TxRecord& tx, const std::string& action) const {
        if (tx.state == TxState::Active) {
            return;
        }

        throw std::runtime_error(txLabel(tx.id) + " cannot " + action + " because it is " +
                                 txStateName(tx.state));
    }

    const AccountVersion* visibleVersionLocked(const std::string& accountId, Timestamp snapshot) const {
        const auto it = ledger_.find(accountId);
        if (it == ledger_.end()) {
            return nullptr;
        }

        const std::vector<AccountVersion>& chain = it->second;
        for (auto rit = chain.rbegin(); rit != chain.rend(); ++rit) {
            if (rit->createStamp <= snapshot && rit->deleteStamp > snapshot) {
                return &(*rit);
            }
        }

        return nullptr;
    }

    std::size_t visibleIndexLocked(const std::vector<AccountVersion>& chain, Timestamp snapshot) const {
        for (std::size_t index = chain.size(); index > 0; --index) {
            const std::size_t current = index - 1;
            if (chain[current].createStamp <= snapshot && chain[current].deleteStamp > snapshot) {
                return current;
            }
        }

        return invalidIndex();
    }

    Timestamp latestCommittedStampLocked(const std::string& accountId) const {
        const auto it = ledger_.find(accountId);
        if (it == ledger_.end() || it->second.empty()) {
            return 0;
        }
        return it->second.back().createStamp;
    }

    void releaseLocksLocked(TxRecord& tx) {
        for (const std::string& accountId : tx.heldExclusiveLocks) {
            auto lockIt = lockTable_.find(accountId);
            if (lockIt != lockTable_.end() && lockIt->second.owner == tx.id) {
                lockIt->second.owner = 0;
            }
        }
        tx.heldExclusiveLocks.clear();
    }

    void clearWaitEdgesLocked(TransactionId txId) {
        waitsFor_.erase(txId);
        for (auto& entry : waitsFor_) {
            entry.second.erase(txId);
        }
    }

    bool isActiveTransactionLocked(TransactionId txId) const {
        const auto it = transactions_.find(txId);
        return it != transactions_.end() && it->second.state == TxState::Active;
    }

    std::vector<TransactionId> detectCycleLocked() const {
        std::unordered_map<TransactionId, int> color;
        std::vector<TransactionId> recursionStack;
        std::vector<TransactionId> cycleNodes;

        std::function<bool(TransactionId)> dfs = [&](TransactionId txId) {
            color[txId] = 1;
            recursionStack.push_back(txId);

            const auto graphIt = waitsFor_.find(txId);
            if (graphIt != waitsFor_.end()) {
                for (TransactionId neighbour : graphIt->second) {
                    if (!isActiveTransactionLocked(neighbour)) {
                        continue;
                    }

                    if (color[neighbour] == 0) {
                        if (dfs(neighbour)) {
                            return true;
                        }
                    } else if (color[neighbour] == 1) {
                        const auto begin =
                            std::find(recursionStack.begin(), recursionStack.end(), neighbour);
                        cycleNodes.assign(begin, recursionStack.end());
                        return true;
                    }
                }
            }

            recursionStack.pop_back();
            color[txId] = 2;
            return false;
        };

        for (const auto& entry : waitsFor_) {
            if (!isActiveTransactionLocked(entry.first)) {
                continue;
            }
            if (color[entry.first] == 0 && dfs(entry.first)) {
                break;
            }
        }

        return cycleNodes;
    }

    bool abortTransactionLocked(TransactionId txId, const std::string& reason) {
        auto it = transactions_.find(txId);
        if (it == transactions_.end()) {
            throw std::runtime_error("Unknown transaction id: " + std::to_string(txId));
        }

        TxRecord& tx = it->second;
        if (tx.state == TxState::Aborted) {
            return false;
        }
        if (tx.state == TxState::Committed) {
            throw std::runtime_error(txLabel(txId) + " cannot be aborted because it is already committed");
        }

        tx.state = TxState::Aborted;
        tx.stagedWrites.clear();
        releaseLocksLocked(tx);
        clearWaitEdgesLocked(txId);

        logLocked(txLabel(txId) + " abort successful (" + reason + ")");
        return true;
    }

    void acquireExclusiveLockLocked(std::unique_lock<std::mutex>& lock,
                                    TransactionId txId,
                                    const std::string& accountId) {
        while (true) {
            TxRecord& tx = requireTransactionLocked(txId);
            ensureActiveLocked(tx, "acquire write lock");

            LockBucket& bucket = lockTable_[accountId];
            if (bucket.owner == 0 || bucket.owner == txId) {
                bucket.owner = txId;
                tx.heldExclusiveLocks.insert(accountId);
                clearWaitEdgesLocked(txId);
                logLocked(txLabel(txId) + " acquired write lock on " + accountId);
                return;
            }

            const TransactionId blocker = bucket.owner;
            waitsFor_[txId].clear();
            waitsFor_[txId].insert(blocker);

            logLocked(txLabel(txId) + " is waiting for " + accountId + " because " +
                      txLabel(blocker) + " holds the write lock");

            const std::vector<TransactionId> cycle = detectCycleLocked();
            if (!cycle.empty()) {
                const TransactionId victim =
                    *std::max_element(cycle.begin(), cycle.end());

                std::ostringstream cycleMessage;
                cycleMessage << "Deadlock detected in waits-for graph: ";
                for (std::size_t index = 0; index < cycle.size(); ++index) {
                    if (index != 0U) {
                        cycleMessage << " -> ";
                    }
                    cycleMessage << txLabel(cycle[index]);
                }
                logLocked(cycleMessage.str());
                logLocked("Victim transaction: " + txLabel(victim));

                abortTransactionLocked(victim, "chosen as deadlock victim");
                wakeup_.notify_all();

                if (victim == txId) {
                    throw std::runtime_error(txLabel(txId) + " was aborted during deadlock resolution");
                }

                continue;
            }

            wakeup_.wait(lock, [&]() {
                const auto txIt = transactions_.find(txId);
                if (txIt == transactions_.end()) {
                    return true;
                }

                return txIt->second.state != TxState::Active || lockTable_[accountId].owner == 0 ||
                       lockTable_[accountId].owner == txId;
            });

            clearWaitEdgesLocked(txId);
        }
    }

    void logLocked(const std::string& message) const {
        if (verbose_) {
            std::cout << message << '\n';
        }
    }

    bool verbose_ = true;
    mutable std::mutex mutex_;
    std::condition_variable wakeup_;
    TransactionId nextTxId_ = 1;
    Timestamp logicalClock_ = 0;
    std::unordered_map<std::string, std::vector<AccountVersion>> ledger_;
    std::unordered_map<TransactionId, TxRecord> transactions_;
    std::unordered_map<std::string, LockBucket> lockTable_;
    std::unordered_map<TransactionId, std::set<TransactionId>> waitsFor_;
};

void seedAccounts(LedgerTransactionManager& manager) {
    manager.initializeAccount("ACC1001", 5000);
    manager.initializeAccount("ACC1002", 3000);
    manager.initializeAccount("ACC1003", 7000);
    manager.initializeAccount("ACC1004", 2000);
}

void printSection(const std::string& title) {
    std::cout << "\n==============================\n";
    std::cout << title << '\n';
    std::cout << "==============================\n";
}

void runDemo1BasicCommit() {
    printSection("Demo 1: Basic Commit");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    const std::optional<int> oldBalance = manager.read(t1, "ACC1001");
    if (oldBalance.has_value()) {
        manager.write(t1, "ACC1001", *oldBalance + 750);
    }
    manager.commit(t1);

    const TransactionId t2 = manager.beginTransaction();
    manager.read(t2, "ACC1001");
    manager.printCurrentLedger();
}

void runDemo2SnapshotIsolation() {
    printSection("Demo 2: Snapshot Isolation");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    manager.read(t1, "ACC1002");

    const TransactionId t2 = manager.beginTransaction();
    manager.write(t2, "ACC1002", 3600);
    manager.commit(t2);

    manager.read(t1, "ACC1002");

    const TransactionId t3 = manager.beginTransaction();
    manager.read(t3, "ACC1002");
    manager.printCurrentLedger();
}

void runDemo3AbortRollback() {
    printSection("Demo 3: Abort / Rollback");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    manager.write(t1, "ACC1003", 6400);
    manager.abort(t1);

    const TransactionId t2 = manager.beginTransaction();
    manager.read(t2, "ACC1003");
    manager.printCurrentLedger();
}

void runDemo4WriteBlocking() {
    printSection("Demo 4: Strict 2PL Write Blocking");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    std::thread holder([&manager]() {
        const TransactionId t1 = manager.beginTransaction();
        manager.write(t1, "ACC1004", 2450);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        manager.abort(t1);
    });

    std::thread waiter([&manager]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        try {
            const TransactionId t2 = manager.beginTransaction();
            manager.write(t2, "ACC1004", 2600);
            manager.commit(t2);
        } catch (const std::exception& error) {
            std::cout << "Worker thread error: " << error.what() << '\n';
        }
    });

    holder.join();
    waiter.join();
    manager.printCurrentLedger();
}

void runDemo5DeadlockDetection() {
    printSection("Demo 5: Deadlock Detection");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    std::mutex syncMutex;
    std::condition_variable syncCv;
    bool t1HasFirstLock = false;
    bool t2HasFirstLock = false;
    bool t1ReadyForSecondLock = false;

    std::thread first([&]() {
        try {
            const TransactionId t1 = manager.beginTransaction();
            manager.write(t1, "ACC1001", 5100);

            {
                std::lock_guard<std::mutex> guard(syncMutex);
                t1HasFirstLock = true;
            }
            syncCv.notify_all();

            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t2HasFirstLock; });
                t1ReadyForSecondLock = true;
            }
            syncCv.notify_all();

            manager.write(t1, "ACC1002", 3200);
            manager.commit(t1);
        } catch (const std::exception& error) {
            std::cout << "First deadlock thread: " << error.what() << '\n';
        }
    });

    std::thread second([&]() {
        try {
            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t1HasFirstLock; });
            }

            const TransactionId t2 = manager.beginTransaction();
            manager.write(t2, "ACC1002", 3300);

            {
                std::lock_guard<std::mutex> guard(syncMutex);
                t2HasFirstLock = true;
            }
            syncCv.notify_all();

            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t1ReadyForSecondLock; });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            manager.write(t2, "ACC1001", 5200);
            manager.commit(t2);
        } catch (const std::exception& error) {
            std::cout << "Second deadlock thread: " << error.what() << '\n';
        }
    });

    first.join();
    second.join();
    manager.printCurrentLedger();
}

void runDemo6LostUpdatePrevention() {
    printSection("Demo 6: Lost Update Prevention");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    const TransactionId t2 = manager.beginTransaction();

    manager.write(t1, "ACC1001", 5800);
    manager.commit(t1);

    try {
        manager.write(t2, "ACC1001", 6000);
    } catch (const std::exception& error) {
        std::cout << "Expected conflict: " << error.what() << '\n';
    }

    const TransactionId t3 = manager.beginTransaction();
    manager.read(t3, "ACC1001");
    manager.printCurrentLedger();
}

void runDemo7VacuumCleanup() {
    printSection("Demo 7: Vacuum Cleanup");
    LedgerTransactionManager manager(true);
    seedAccounts(manager);

    {
        const TransactionId t1 = manager.beginTransaction();
        manager.write(t1, "ACC1003", 7100);
        manager.commit(t1);
    }
    {
        const TransactionId t2 = manager.beginTransaction();
        manager.write(t2, "ACC1003", 7300);
        manager.commit(t2);
    }
    {
        const TransactionId t3 = manager.beginTransaction();
        manager.write(t3, "ACC1003", 7600);
        manager.commit(t3);
    }

    std::cout << "Version count before vacuum: " << manager.versionCount("ACC1003") << '\n';
    manager.printVersionChain("ACC1003");
    const std::size_t removed = manager.vacuum();
    std::cout << "Vacuum removed version count: " << removed << '\n';
    std::cout << "Version count after vacuum: " << manager.versionCount("ACC1003") << '\n';
    manager.printVersionChain("ACC1003");
    manager.printCurrentLedger();
}

void runDemoSuite() {
    std::cout << "Lab 8 - In-Memory Transaction Manager\n";
    std::cout << "Dataset : Bank account ledger stored only in memory\n";

    runDemo1BasicCommit();
    runDemo2SnapshotIsolation();
    runDemo3AbortRollback();
    runDemo4WriteBlocking();
    runDemo5DeadlockDetection();
    runDemo6LostUpdatePrevention();
    runDemo7VacuumCleanup();
}

bool testBasicCommit() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    const std::optional<int> before = manager.read(t1, "ACC1001");
    if (!before.has_value()) {
        return false;
    }

    manager.write(t1, "ACC1001", *before + 400);
    manager.commit(t1);

    const TransactionId t2 = manager.beginTransaction();
    const std::optional<int> after = manager.read(t2, "ACC1001");
    return after.has_value() && *after == 5400;
}

bool testSnapshotIsolation() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    const std::optional<int> firstRead = manager.read(t1, "ACC1002");

    const TransactionId t2 = manager.beginTransaction();
    manager.write(t2, "ACC1002", 3550);
    manager.commit(t2);

    const std::optional<int> secondRead = manager.read(t1, "ACC1002");
    const TransactionId t3 = manager.beginTransaction();
    const std::optional<int> freshRead = manager.read(t3, "ACC1002");

    return firstRead.has_value() && secondRead.has_value() && freshRead.has_value() &&
           *firstRead == 3000 && *secondRead == 3000 && *freshRead == 3550;
}

bool testAbortRollback() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    manager.write(t1, "ACC1003", 6400);
    manager.abort(t1);

    const TransactionId t2 = manager.beginTransaction();
    const std::optional<int> value = manager.read(t2, "ACC1003");
    return value.has_value() && *value == 7000;
}

bool testWriteLockBlocking() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    std::mutex orderMutex;
    std::vector<std::string> events;

    auto record = [&](const std::string& event) {
        std::lock_guard<std::mutex> guard(orderMutex);
        events.push_back(event);
    };

    std::thread holder([&]() {
        const TransactionId t1 = manager.beginTransaction();
        manager.write(t1, "ACC1004", 2450);
        record("t1-locked");
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
        manager.abort(t1);
        record("t1-aborted");
    });

    std::thread waiter([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        const TransactionId t2 = manager.beginTransaction();
        record("t2-started");
        manager.write(t2, "ACC1004", 2600);
        record("t2-lock-finished");
        manager.commit(t2);
    });

    holder.join();
    waiter.join();

    const auto indexOf = [&](const std::string& event) {
        const auto it = std::find(events.begin(), events.end(), event);
        return it == events.end() ? events.size() : static_cast<std::size_t>(it - events.begin());
    };

    const TransactionId reader = manager.beginTransaction();
    const std::optional<int> balance = manager.read(reader, "ACC1004");

    return indexOf("t1-locked") < indexOf("t1-aborted") &&
           indexOf("t1-aborted") < indexOf("t2-lock-finished") &&
           balance.has_value() && *balance == 2600;
}

bool testDeadlockDetection() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    std::mutex syncMutex;
    std::condition_variable syncCv;
    bool t1HasFirstLock = false;
    bool t2HasFirstLock = false;
    bool t1ReadyForSecondLock = false;
    bool t1Committed = false;
    bool t2Aborted = false;

    std::thread first([&]() {
        try {
            const TransactionId t1 = manager.beginTransaction();
            manager.write(t1, "ACC1001", 5100);

            {
                std::lock_guard<std::mutex> guard(syncMutex);
                t1HasFirstLock = true;
            }
            syncCv.notify_all();

            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t2HasFirstLock; });
                t1ReadyForSecondLock = true;
            }
            syncCv.notify_all();

            manager.write(t1, "ACC1002", 3200);
            manager.commit(t1);
            t1Committed = true;
        } catch (...) {
            t1Committed = false;
        }
    });

    std::thread second([&]() {
        try {
            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t1HasFirstLock; });
            }

            const TransactionId t2 = manager.beginTransaction();
            manager.write(t2, "ACC1002", 3300);

            {
                std::lock_guard<std::mutex> guard(syncMutex);
                t2HasFirstLock = true;
            }
            syncCv.notify_all();

            {
                std::unique_lock<std::mutex> guard(syncMutex);
                syncCv.wait(guard, [&]() { return t1ReadyForSecondLock; });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            manager.write(t2, "ACC1001", 5200);
            manager.commit(t2);
        } catch (...) {
            t2Aborted = true;
        }
    });

    first.join();
    second.join();

    const TransactionId reader = manager.beginTransaction();
    const std::optional<int> acc1 = manager.read(reader, "ACC1001");
    const std::optional<int> acc2 = manager.read(reader, "ACC1002");

    return t1Committed && t2Aborted && acc1.has_value() && acc2.has_value() &&
           *acc1 == 5100 && *acc2 == 3200;
}

bool testLostUpdatePrevention() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    const TransactionId t1 = manager.beginTransaction();
    const TransactionId t2 = manager.beginTransaction();

    manager.write(t1, "ACC1001", 5800);
    manager.commit(t1);

    bool conflictRaised = false;
    try {
        manager.write(t2, "ACC1001", 6000);
    } catch (const std::exception& error) {
        conflictRaised = std::string(error.what()).find("Serialization failure") != std::string::npos;
    }

    const TransactionId reader = manager.beginTransaction();
    const std::optional<int> finalValue = manager.read(reader, "ACC1001");

    return conflictRaised && finalValue.has_value() && *finalValue == 5800;
}

bool testVacuumCleanup() {
    LedgerTransactionManager manager(false);
    seedAccounts(manager);

    for (int balance : {7100, 7300, 7600}) {
        const TransactionId tx = manager.beginTransaction();
        manager.write(tx, "ACC1003", balance);
        manager.commit(tx);
    }

    const std::size_t before = manager.versionCount("ACC1003");
    const std::size_t removed = manager.vacuum();
    const std::size_t after = manager.versionCount("ACC1003");

    const TransactionId reader = manager.beginTransaction();
    const std::optional<int> latest = manager.read(reader, "ACC1003");

    return before > after && removed > 0U && latest.has_value() && *latest == 7600;
}

int runTestSuite() {
    std::cout << "Running Lab 8 Transaction Manager Tests\n";

    const std::vector<std::pair<std::string, bool (*)()>> tests = {
        {"Test 1: Basic commit", &testBasicCommit},
        {"Test 2: Snapshot isolation", &testSnapshotIsolation},
        {"Test 3: Abort rollback", &testAbortRollback},
        {"Test 4: Write locking", &testWriteLockBlocking},
        {"Test 5: Deadlock detection", &testDeadlockDetection},
        {"Test 6: Lost update prevention", &testLostUpdatePrevention},
        {"Test 7: Vacuum cleanup", &testVacuumCleanup},
    };

    int passed = 0;
    for (const auto& test : tests) {
        bool ok = false;
        try {
            ok = test.second();
        } catch (...) {
            ok = false;
        }

        std::cout << test.first << " ... " << (ok ? "PASS" : "FAIL") << '\n';
        if (ok) {
            ++passed;
        }
    }

    std::cout << "Result: " << passed << " / " << tests.size() << " passed\n";
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--test") {
        return runTestSuite();
    }

    runDemoSuite();
    return 0;
}
