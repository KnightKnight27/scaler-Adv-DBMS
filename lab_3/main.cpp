/*
 * Clock Sweep Buffer Replacement Policy
 * Advanced DBMS - Lab Assignment 3
 *
 * The clock sweep algorithm is a page replacement strategy used in
 * database buffer managers (e.g. PostgreSQL uses a variant of this).
 * It works like a circular clock: each frame has a "usage" bit.
 * When a new page needs to be loaded, the hand sweeps around;
 * if the usage bit is set it clears it and moves on,
 * if not it evicts that frame.
 */

#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <stdexcept>

// BufferPool implements the clock sweep eviction strategy.
// Template param K is the key/page-id type.
template<typename K>
class BufferPool {
public:

    explicit BufferPool(int capacity)
        : poolSize(capacity), hand(0), running(true)
    {
        if (capacity <= 0)
            throw std::invalid_argument("Buffer pool capacity must be > 0");

        frames.resize(poolSize);

        // background thread simulates the sweep ticker
        sweeper = std::thread(&BufferPool::sweepLoop, this);
    }

    ~BufferPool()
    {
        running = false;
        if (sweeper.joinable())
            sweeper.join();
    }

    // fetch a page from the buffer; sets usage bit on hit
    K fetch(K pageId)
    {
        auto it = pageTable.find(pageId);
        if (it == pageTable.end()) {
            std::cout << "[MISS] Page " << pageId << " not in buffer\n";
            return K{};
        }

        int idx = it->second;
        frames[idx].usageBit = true;
        std::cout << "[HIT]  Page " << pageId << " found at frame " << idx << "\n";
        return frames[idx].pageId;
    }

    // load a page into the buffer, evicting if necessary
    void load(K pageId)
    {
        // already in buffer — just refresh usage
        auto it = pageTable.find(pageId);
        if (it != pageTable.end()) {
            frames[it->second].usageBit = true;
            std::cout << "[SKIP] Page " << pageId << " already buffered\n";
            return;
        }

        // find a victim using clock sweep
        int victim = findVictim();
        evictFrame(victim);
        placeAt(victim, pageId);
    }

    void printBuffer() const
    {
        std::cout << "\n--- Buffer State (hand=" << hand << ") ---\n";
        for (int i = 0; i < poolSize; i++) {
            if (!frames[i].occupied) {
                std::cout << "  Frame[" << i << "] : <empty>\n";
                continue;
            }
            std::cout << "  Frame[" << i << "] : page=" << frames[i].pageId
                      << "  usage=" << frames[i].usageBit << "\n";
        }
        std::cout << "-----------------------------------\n\n";
    }

private:

    struct Frame {
        K      pageId    = K{};
        bool   usageBit  = false;
        bool   occupied  = false;
    };

    // advance the clock hand until a victim frame is found
    int findVictim()
    {
        while (true) {
            Frame &f = frames[hand];

            if (!f.occupied) {
                // free slot — take it directly
                return hand;
            }

            if (f.usageBit) {
                // second chance: clear bit, move on
                f.usageBit = false;
                advance();
            } else {
                // no usage since last sweep — evict this one
                int victim = hand;
                advance();
                return victim;
            }
        }
    }

    void evictFrame(int idx)
    {
        if (!frames[idx].occupied)
            return;

        std::cout << "[EVICT] Removing page " << frames[idx].pageId
                  << " from frame " << idx << "\n";
        pageTable.erase(frames[idx].pageId);
        frames[idx].occupied = false;
        frames[idx].usageBit = false;
    }

    void placeAt(int idx, K pageId)
    {
        frames[idx].pageId  = pageId;
        frames[idx].usageBit = true;
        frames[idx].occupied = true;
        pageTable[pageId]   = idx;
        std::cout << "[LOAD]  Page " << pageId << " -> frame " << idx << "\n";
    }

    void advance()
    {
        hand = (hand + 1) % poolSize;
    }

    // background thread — just keeps running to simulate async sweep ticker
    void sweepLoop()
    {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    int  poolSize;
    int  hand;
    bool running;

    std::vector<Frame>           frames;
    std::unordered_map<K, int>   pageTable;
    std::thread                  sweeper;
};


int main()
{
    // buffer pool with 3 frames
    BufferPool<int> bp(3);

    std::cout << "=== Loading pages 10, 20, 30 ===\n";
    bp.load(10);
    bp.load(20);
    bp.load(30);
    bp.printBuffer();

    // access page 10 — its usage bit gets set again
    std::cout << "=== Fetching page 10 ===\n";
    bp.fetch(10);
    bp.printBuffer();

    // buffer is full; loading 40 should evict page 20 (usage bit was 0)
    std::cout << "=== Loading page 40 (eviction expected) ===\n";
    bp.load(40);
    bp.printBuffer();

    // try fetching a page that was evicted
    std::cout << "=== Fetching page 20 (should miss) ===\n";
    bp.fetch(20);

    // load a couple more to stress the sweeper
    std::cout << "\n=== Loading pages 50, 60 ===\n";
    bp.load(50);
    bp.load(60);
    bp.printBuffer();

    return 0;
}