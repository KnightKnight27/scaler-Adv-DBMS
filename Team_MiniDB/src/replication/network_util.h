#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace minidb {

// Writing to a socket whose peer has already closed (e.g. a replica that
// disconnected) raises SIGPIPE, whose default action is to terminate the
// *whole process* - taking the primary down for every other client, not
// just the dead connection. send() failing with EPIPE and returning -1 is
// the behavior the rest of this file is written to handle (see
// SendLine/PrimaryReplicator::Broadcast); SIGPIPE must be silenced so that
// actually happens instead of the process dying first.
inline void IgnoreSigPipeOnce() {
  static bool done = [] {
    signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)done;
}

inline int CreateListenSocket(int port) {
  IgnoreSigPipeOnce();
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("socket() failed");
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    close(fd);
    throw std::runtime_error("bind() failed on port " + std::to_string(port));
  }
  if (listen(fd, 16) < 0) {
    close(fd);
    throw std::runtime_error("listen() failed");
  }
  return fd;
}

inline int ConnectToHost(const std::string &host, int port) {
  IgnoreSigPipeOnce();
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) throw std::runtime_error("socket() failed");
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    close(fd);
    throw std::runtime_error("invalid host: " + host);
  }
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    close(fd);
    throw std::runtime_error("connect() failed to " + host + ":" + std::to_string(port));
  }
  return fd;
}

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // not available on macOS; IgnoreSigPipeOnce() covers it instead
#endif

inline bool SendLine(int fd, const std::string &line) {
  std::string framed = line + "\n";
  size_t sent = 0;
  while (sent < framed.size()) {
    ssize_t n = send(fd, framed.data() + sent, framed.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

// Buffers partial reads from a streaming socket so each call to ReadLine()
// returns exactly one newline-delimited message (replication statements,
// in our case), regardless of how TCP happened to chunk the bytes.
class LineReader {
 public:
  explicit LineReader(int fd) : fd_(fd) {}

  // Returns false on EOF/error (e.g. the primary went away).
  bool ReadLine(std::string *out) {
    while (true) {
      size_t nl = buf_.find('\n');
      if (nl != std::string::npos) {
        *out = buf_.substr(0, nl);
        buf_.erase(0, nl + 1);
        return true;
      }
      char chunk[4096];
      ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
      if (n <= 0) return false;
      buf_.append(chunk, n);
    }
  }

 private:
  int fd_;
  std::string buf_;
};

}  // namespace minidb
