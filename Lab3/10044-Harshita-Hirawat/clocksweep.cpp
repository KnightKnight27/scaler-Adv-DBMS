#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

struct Frame {
    int pageId = -1;       // -1 means the frame is empty
    int usageCount = 0;    // PostgreSQL keeps this between 0 and 5
    bool pinned = false;   // A pinned frame cannot be evicted
};

class BufferPool {
public:
    explicit BufferPool(int capacity): frames(capacity), capacity(capacity) {}

    // Returns the frame containing the page, or -1 if no frame is available.
    int fetch(int pageId) {
        auto found = pageToFrame.find(pageId);

        if(found != pageToFrame.end()) {
            int frameIndex = found->second;
            Frame& frame = frames[frameIndex];
            frame.usageCount = std::min(frame.usageCount + 1, 5);

            std::cout << "[HIT]  Page " << pageId
                      << " is in frame " << frameIndex
                      << " (usage=" << frame.usageCount << ")\n";
            return frameIndex;
        }

        int victim = clockSweep();
        if(victim == -1) {
            std::cout << "[ERROR] All frames are pinned; page "
                      << pageId << " cannot be loaded\n";
            return -1;
        }

        if(frames[victim].pageId != -1) {
            std::cout << "[EVICT] Page " << frames[victim].pageId
                      << " removed from frame " << victim << "\n";
            pageToFrame.erase(frames[victim].pageId);
        }

        frames[victim] = {pageId, 1, false};
        pageToFrame[pageId] = victim;

        std::cout << "[MISS] Page " << pageId
                  << " loaded into frame " << victim << "\n";
        return victim;
    }

    void pin(int pageId) {
        auto found = pageToFrame.find(pageId);
        if(found != pageToFrame.end()) {
            frames[found->second].pinned = true;
        }
    }

    void unpin(int pageId) {
        auto found = pageToFrame.find(pageId);
        if(found != pageToFrame.end()) {
            frames[found->second].pinned = false;
        }
    }

    void printState() const {
        std::cout << "\n--- Buffer Pool State ---\n";
        for(int i = 0; i < capacity; ++i) {
            const Frame& frame = frames[i];

            std::cout << "Frame[" << i << "] page="
                      << (frame.pageId == -1
                              ? std::string("empty")
                              : std::to_string(frame.pageId))
                      << " usage=" << frame.usageCount
                      << (frame.pinned ? " [PINNED]" : "")
                      << (i == clockHand ? " <-- clock hand" : "")
                      << "\n";
        }
    }

private:
    std::vector<Frame> frames;
    std::unordered_map<int, int> pageToFrame;
    int capacity;
    int clockHand = 0;

    int clockSweep() {
        // Avoid an endless sweep when every frame is pinned.
        bool hasUnpinnedFrame = false;
        for(const Frame& frame : frames) {
            if(!frame.pinned) {
                hasUnpinnedFrame = true;
                break;
            }
        }

        if(!hasUnpinnedFrame) {
            return -1;
        }

        while(true) {
            Frame& frame = frames[clockHand];

            if(!frame.pinned) {
                if(frame.usageCount == 0) {
                    int victim = clockHand;
                    clockHand = (clockHand + 1) % capacity;
                    return victim;
                }

                // Give the page another chance before considering eviction.
                --frame.usageCount;
            }

            clockHand = (clockHand + 1) % capacity;
        }
    }
};

int main() {
    BufferPool pool(4);
    std::vector<int> accesses = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

    for(int pageId : accesses) {
        pool.fetch(pageId);
    }

    pool.printState();
    return 0;
}
