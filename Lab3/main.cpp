#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

template <typename T>
class ClockSweep {
public:
    explicit ClockSweep(std::size_t maxNumber,
                        std::chrono::milliseconds sweepInterval = std::chrono::milliseconds(500))
        : maxCacheSize(maxNumber),
          frames(maxNumber),
          sweepIntervalMs(sweepInterval),
          running(true) {
        if (maxNumber == 0) {
            throw std::invalid_argument("ClockSweep: maxCacheSize must be > 0");
        }
        bgClockThread = std::thread(&ClockSweep::evictionClockThread, this);
    }

    ~ClockSweep() {
        {
            std::lock_guard<std::mutex> lk(mu);
            running = false;
        }
        cv.notify_all();
        if (bgClockThread.joinable()) {
            bgClockThread.join();
        }
    }

    ClockSweep(const ClockSweep&) = delete;
    ClockSweep& operator=(const ClockSweep&) = delete;

    // Returns the key if present (and marks it referenced); std::nullopt on miss.
    std::optional<T> get(const T& key) {
        std::lock_guard<std::mutex> lk(mu);
        auto it = index.find(key);
        if (it == index.end()) return std::nullopt;
        frames[it->second].referenceBit = true;
        return frames[it->second].key;
    }

    void put(const T& key) {
        std::lock_guard<std::mutex> lk(mu);

        if (index.find(key) != index.end()) {
            frames[index[key]].referenceBit = true;
            return;
        }

        std::size_t slot;
        if (occupiedCount < maxCacheSize) {
            slot = occupiedCount;
            ++occupiedCount;
        } else {
            slot = sweepForVictim();
            index.erase(frames[slot].key);
        }

        frames[slot].key = key;
        frames[slot].referenceBit = true;
        frames[slot].occupied = true;
        index[key] = slot;
    }

    void dump(std::ostream& os) const {
        std::lock_guard<std::mutex> lk(mu);
        os << "[ ";
        for (std::size_t i = 0; i < maxCacheSize; ++i) {
            if (i == hand) os << "*";
            if (frames[i].occupied) {
                os << frames[i].key << "(" << (frames[i].referenceBit ? 1 : 0) << ") ";
            } else {
                os << "_ ";
            }
        }
        os << "]  hand=" << hand << "\n";
    }

private:
    struct Frame {
        T key{};
        bool referenceBit{false};
        bool occupied{false};
    };

    // Caller must hold mu. Advances hand, clearing reference bits, until a victim is found.
    std::size_t sweepForVictim() {
        while (true) {
            Frame& f = frames[hand];
            if (!f.referenceBit) {
                std::size_t victim = hand;
                hand = (hand + 1) % maxCacheSize;
                return victim;
            }
            f.referenceBit = false;
            hand = (hand + 1) % maxCacheSize;
        }
    }

    // Background thread: periodically advance the hand one step, clearing a reference bit.
    // Simulates the "aging" pass that PostgreSQL's bgwriter does.
    void evictionClockThread() {
        std::unique_lock<std::mutex> lk(mu);
        while (running) {
            cv.wait_for(lk, sweepIntervalMs, [this] { return !running; });
            if (!running) break;
            if (occupiedCount == 0) continue;
            if (frames[hand].occupied && frames[hand].referenceBit) {
                frames[hand].referenceBit = false;
            }
            hand = (hand + 1) % maxCacheSize;
        }
    }

    const std::size_t maxCacheSize;
    std::vector<Frame> frames;
    std::unordered_map<T, std::size_t> index;
    std::size_t hand{0};
    std::size_t occupiedCount{0};

    mutable std::mutex mu;
    std::condition_variable cv;
    std::chrono::milliseconds sweepIntervalMs;
    bool running;
    std::thread bgClockThread;
};

int main() {
    ClockSweep<int> cs(4, std::chrono::milliseconds(200));

    std::cout << "-- inserting 1..4 (fills cache) --\n";
    for (int k : {1, 2, 3, 4}) {
        cs.put(k);
        cs.dump(std::cout);
    }

    std::cout << "\n-- get(2): marks 2 as referenced --\n";
    auto v = cs.get(2);
    std::cout << "get(2) -> " << (v ? std::to_string(*v) : "miss") << "\n";
    cs.dump(std::cout);

    std::cout << "\n-- put(5): triggers sweep, should evict an unreferenced frame --\n";
    cs.put(5);
    cs.dump(std::cout);

    std::cout << "\n-- put(6), put(7) --\n";
    cs.put(6);
    cs.dump(std::cout);
    cs.put(7);
    cs.dump(std::cout);

    std::cout << "\n-- get(99) (miss) --\n";
    auto miss = cs.get(99);
    std::cout << "get(99) -> " << (miss ? std::to_string(*miss) : "miss") << "\n";

    std::cout << "\n-- sleeping 1s so background thread runs sweeps --\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    cs.dump(std::cout);

    std::cout << "\n-- string cache demo --\n";
    ClockSweep<std::string> scs(3);
    scs.put("alpha");
    scs.put("beta");
    scs.put("gamma");
    scs.get("alpha");
    scs.put("delta");
    scs.dump(std::cout);

    return 0;
}
