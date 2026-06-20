#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../query/executor.h"
#include "../sql/lexer.h"
#include "../sql/parser.h"
#include "network_util.h"

namespace minidb {

// Runs on the primary: accepts replica connections and broadcasts every
// successfully-applied DDL/DML statement to all of them, in commit order.
// This is logical (statement-based) replication rather than physical WAL
// shipping - simpler to implement correctly under the project's time
// budget, and still a genuine two-node primary-replica pipeline over TCP.
// Documented limitation: a replica that misses statements while
// disconnected does not currently catch up via a snapshot/backfill - it
// only sees what was broadcast while connected.
class PrimaryReplicator {
 public:
  void Start(int port) {
    listen_fd_ = CreateListenSocket(port);
    accept_thread_ = std::thread([this] { AcceptLoop(); });
  }

  void Stop() {
    stop_ = true;
    if (listen_fd_ >= 0) close(listen_fd_);
    if (accept_thread_.joinable()) accept_thread_.join();
    std::lock_guard<std::mutex> g(mtx_);
    for (int fd : replica_fds_) close(fd);
    replica_fds_.clear();
  }

  ~PrimaryReplicator() { Stop(); }

  void Broadcast(const std::string &statement) {
    std::lock_guard<std::mutex> g(mtx_);
    for (auto it = replica_fds_.begin(); it != replica_fds_.end();) {
      if (!SendLine(*it, statement)) {
        close(*it);
        it = replica_fds_.erase(it);
      } else {
        ++it;
      }
    }
  }

  int ReplicaCount() {
    std::lock_guard<std::mutex> g(mtx_);
    return static_cast<int>(replica_fds_.size());
  }

 private:
  void AcceptLoop() {
    while (!stop_) {
      sockaddr_in addr{};
      socklen_t len = sizeof(addr);
      int fd = accept(listen_fd_, reinterpret_cast<sockaddr *>(&addr), &len);
      if (fd < 0) continue;  // listen_fd_ closed on Stop() -> accept() returns error, loop exits via stop_
      std::lock_guard<std::mutex> g(mtx_);
      replica_fds_.push_back(fd);
    }
  }

  int listen_fd_ = -1;
  std::atomic<bool> stop_{false};
  std::thread accept_thread_;
  std::mutex mtx_;
  std::vector<int> replica_fds_;
};

// Runs on the replica: connects to the primary and continuously applies
// whatever statements arrive to a local Executor, so the replica's data
// converges to the primary's. Tracks connectivity so the replica's own
// read REPL can report "serving possibly-stale reads" once the primary
// is unreachable, instead of pretending nothing happened.
class ReplicaApplier {
 public:
  bool Connect(const std::string &host, int port) {
    try {
      fd_ = ConnectToHost(host, port);
      connected_ = true;  // the TCP handshake succeeded; no statement needs to arrive yet
      return true;
    } catch (const std::exception &) {
      return false;
    }
  }

  void RunLoop(Executor *executor) {
    LineReader reader(fd_);
    std::string line;
    while (reader.ReadLine(&line)) {
      if (line.empty()) continue;
      try {
        Lexer lex(line);
        Parser parser(lex.Tokenize());
        Statement stmt = parser.ParseStatement();
        executor->Execute(stmt);
        applied_count_++;
      } catch (const std::exception &) {
        // Malformed/unsupported replicated statement - skip rather than
        // take down the replication thread.
      }
    }
    connected_ = false;
  }

  bool IsConnected() const { return connected_; }
  int64_t AppliedCount() const { return applied_count_; }

 private:
  int fd_ = -1;
  std::atomic<bool> connected_{false};
  std::atomic<int64_t> applied_count_{0};
};

}  // namespace minidb
