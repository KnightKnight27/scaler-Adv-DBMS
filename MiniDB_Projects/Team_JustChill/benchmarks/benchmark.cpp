#include "../src/page.h"
#include "../src/heap_file.h"
#include "../src/buffer_pool.h"
#include "../src/wal.h"
#include "../src/replication.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <mutex>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace fs = std::filesystem;

void test_heap_file_basics() {
    std::cout << "[RUN] test_heap_file_basics\n";
    std::string filename = "test_heap_file.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        int p0 = hf.allocatePage();
        int p1 = hf.allocatePage();
        assert(p0 == 0);
        assert(p1 == 1);

        Page page0;
        std::strcpy(page0.data, "Data on page 0");
        hf.writePage(p0, &page0);

        Page page1;
        std::strcpy(page1.data, "Data on page 1");
        hf.writePage(p1, &page1);
    } // Destructor closes file

    // Reopen and read to verify persistence
    {
        HeapFile hf(filename);
        Page page0;
        hf.readPage(0, &page0);
        assert(std::strcmp(page0.data, "Data on page 0") == 0);

        Page page1;
        hf.readPage(1, &page1);
        assert(std::strcmp(page1.data, "Data on page 1") == 0);
    }

    fs::remove(filename);
    std::cout << "[PASS] test_heap_file_basics\n";
}

void test_buffer_pool_basics() {
    std::cout << "[RUN] test_buffer_pool_basics\n";
    std::string filename = "test_buffer_pool.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        BufferPool bp(3, &hf); // Pool size 3

        // Allocate 4 pages on disk
        int p0 = hf.allocatePage();
        int p1 = hf.allocatePage();
        int p2 = hf.allocatePage();
        int p3 = hf.allocatePage();

        // Get page 0, write data, unpin (mark dirty)
        Page* pg0 = bp.getPage(p0);
        assert(pg0 != nullptr);
        std::strcpy(pg0->data, "Page 0 Content");
        bp.unpinPage(p0, true); // Pin count = 0, is_dirty = true

        // Get page 1, write data, unpin (mark dirty)
        Page* pg1 = bp.getPage(p1);
        assert(pg1 != nullptr);
        std::strcpy(pg1->data, "Page 1 Content");
        bp.unpinPage(p1, true);

        // Get page 2, write data, unpin (mark dirty)
        Page* pg2 = bp.getPage(p2);
        assert(pg2 != nullptr);
        std::strcpy(pg2->data, "Page 2 Content");
        bp.unpinPage(p2, true);

        // Current LRU order: p2 (front), p1, p0 (back)
        // Access p0 to make it recently used
        Page* pg0_again = bp.getPage(p0);
        assert(pg0_again != nullptr);
        assert(std::strcmp(pg0_again->data, "Page 0 Content") == 0);
        bp.unpinPage(p0, false); // pin_count goes back to 0, stays dirty

        // LRU order: p0 (front), p2, p1 (back)
        // Now get p3. This causes a cache miss and should evict p1 (back of LRU list)
        Page* pg3 = bp.getPage(p3);
        assert(pg3 != nullptr);
        std::strcpy(pg3->data, "Page 3 Content");
        bp.unpinPage(p3, true);

        // Since p1 was evicted and it was dirty, it should have been written to disk.
        // Let's get page 1 again. It should be loaded from disk.
        Page* pg1_again = bp.getPage(p1);
        assert(pg1_again != nullptr);
        assert(std::strcmp(pg1_again->data, "Page 1 Content") == 0);
        bp.unpinPage(p1, false);
    }

    fs::remove(filename);
    std::cout << "[PASS] test_buffer_pool_basics\n";
}

void test_buffer_pool_concurrency() {
    std::cout << "[RUN] test_buffer_pool_concurrency\n";
    std::string filename = "test_buffer_pool_concurrent.dat";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        HeapFile hf(filename);
        BufferPool bp(5, &hf); // Pool size 5

        // Allocate 10 pages
        std::vector<int> pids;
        for (int i = 0; i < 10; ++i) {
            pids.push_back(hf.allocatePage());
        }

        // Spawn threads that concurrently get, modify, unpin pages
        auto worker = [&](int thread_id) {
            for (int step = 0; step < 50; ++step) {
                int page_idx = (thread_id + step) % 10;
                int pid = pids[page_idx];

                Page* pg = bp.getPage(pid);
                if (pg != nullptr) {
                    // Simulating page write
                    std::string msg = "T" + std::to_string(thread_id) + "S" + std::to_string(step);
                    std::strncpy(pg->data, msg.c_str(), PAGE_SIZE - 1);
                    bp.unpinPage(pid, true);
                }
            }
        };

        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back(worker, i);
        }

        for (auto& t : threads) {
            t.join();
        }
    }

    fs::remove(filename);
    std::cout << "[PASS] test_buffer_pool_concurrency\n";
}

std::mutex mock_db_lock;
std::vector<std::string> received_queries;

void mock_execute_sql_callback(const std::string& sql) {
    std::lock_guard<std::mutex> lock(mock_db_lock);
    received_queries.push_back(sql);
}

void test_wal_basics() {
    std::cout << "[RUN] test_wal_basics\n";
    std::string filename = "test_wal.log";
    if (fs::exists(filename)) {
        fs::remove(filename);
    }

    {
        LogManager lm(filename);
        lm.writeLog("INSERT INTO students VALUES (1, 'Alice');");
        lm.writeLog("DELETE FROM students WHERE id = 1;");
    }

    // Verify log contents
    std::ifstream infile(filename);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(infile, line)) {
        lines.push_back(line);
    }
    assert(lines.size() == 2);
    assert(lines[0] == "INSERT INTO students VALUES (1, 'Alice');");
    assert(lines[1] == "DELETE FROM students WHERE id = 1;");

    fs::remove(filename);
    std::cout << "[PASS] test_wal_basics\n";
}

void test_replication_basics() {
    std::cout << "[RUN] test_replication_basics\n";
    received_queries.clear();

    // Start replica server on port 9999 in a background thread
    ReplicationNode replica("127.0.0.1", 9999);
    std::thread replica_thread([&]() {
        try {
            replica.startReplicaServer(mock_execute_sql_callback);
        } catch (...) {
            // Ignore error when stopped
        }
    });

    // Wait for the replica server to start listening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Send logs from primary
    ReplicationNode primary("127.0.0.1", 9999);
    bool ok1 = primary.sendLogToReplica("INSERT INTO t VALUES (1, 'Bob')");
    bool ok2 = primary.sendLogToReplica("DELETE FROM t WHERE id = 1");

    assert(ok1 == true);
    assert(ok2 == true);

    // Stop replica server
    replica.stopReplicaServer();
    if (replica_thread.joinable()) {
        replica_thread.join();
    }

    // Verify replication
    std::lock_guard<std::mutex> lock(mock_db_lock);
    assert(received_queries.size() == 2);
    assert(received_queries[0] == "INSERT INTO t VALUES (1, 'Bob')");
    assert(received_queries[1] == "DELETE FROM t WHERE id = 1");

    std::cout << "[PASS] test_replication_basics\n";
}

// Dummy hanging server function
void run_hanging_replica_server(int port, std::atomic<bool>& stop_flag) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return;
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        return;
    }
    if (listen(server_fd, 1) < 0) {
        close(server_fd);
        return;
    }

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (!stop_flag) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (new_socket >= 0) {
            char buffer[1024] = {0};
            recv(new_socket, buffer, sizeof(buffer) - 1, 0);
            
            // Sleep for 3 seconds to exceed the primary's 2-second timeout
            std::this_thread::sleep_for(std::chrono::seconds(3));
            close(new_socket);
        }
    }
    close(server_fd);
}

void test_replication_failure_scenarios() {
    std::cout << "[RUN] test_replication_failure_scenarios\n";

    // Scenario A: Replica server is down (no one listening on port)
    ReplicationNode primary_to_dead("127.0.0.1", 9997);
    bool ok_dead = primary_to_dead.sendLogToReplica("INSERT INTO t VALUES (2)");
    assert(ok_dead == false);

    // Scenario B: Replica accepts but hangs (exceeds 2-second timeout)
    std::atomic<bool> stop_flag(false);
    std::thread hang_thread(run_hanging_replica_server, 9998, std::ref(stop_flag));
    
    // Wait for the hanging server to boot up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ReplicationNode primary_to_hanging("127.0.0.1", 9998);
    auto start_time = std::chrono::steady_clock::now();
    bool ok_hanging = primary_to_hanging.sendLogToReplica("INSERT INTO t VALUES (3)");
    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    // Must return false due to timeout
    assert(ok_hanging == false);
    // Timeout should take approximately 2 seconds (definitely >= 1.5s and < 2.5s)
    std::cout << "[INFO] Hanging replica replication call took " << diff.count() << " seconds\n";
    assert(diff.count() >= 1.5 && diff.count() < 2.5);

    // Clean up
    stop_flag = true;
    // Unblock the accept loop in case it's waiting
    int dummy_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(9998);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);
    connect(dummy_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    close(dummy_sock);

    if (hang_thread.joinable()) {
        hang_thread.join();
    }

    std::cout << "[PASS] test_replication_failure_scenarios\n";
}

int main() {
    try {
        test_heap_file_basics();
        test_buffer_pool_basics();
        test_buffer_pool_concurrency();
        
        // Track 1 Tests
        test_wal_basics();
        test_replication_basics();
        test_replication_failure_scenarios();
        
        std::cout << "\nAll Track 1 & 2 tests PASSED successfully!\n";
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
