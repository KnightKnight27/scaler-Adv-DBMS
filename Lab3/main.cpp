#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <string>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber): maxCacheSize(maxNumber){
        bgClockThread = std::thread(&ClockSweep::runClock, this);
    }

    ~ClockSweep()
    {
        stopThread.store(true);
        if (bgClockThread.joinable()){
            bgClockThread.join();
        }
    }

    T getKey(T key)
    {
        std::lock_guard<std::mutex> guard(mtx);
        auto pos = cache.find(key);
        if (pos == cache.end())
        {
            std::cout<< "[Search] Key Missing: "<< key<< "\n";
            return T();
        }
        int frameIndex = pos->second;
        Frame& currentFrame = frames[frameIndex];
        currentFrame.usageCount++;
        currentFrame.pinned = true;
        std::cout<< "[Search] Key Found: "<< key<< "\n";
        return currentFrame.key;
    }

    void putKey(T key)
    {
        std::lock_guard<std::mutex> guard(mtx);
        auto existingKey = cache.find(key);
        if (existingKey != cache.end())
        {
            int frameIndex = existingKey->second;
            frames[frameIndex].usageCount++;
            frames[frameIndex].pinned = true;
            std::cout<< "[Insert] Key Already Exists: "<< key<< "\n";
            return;
        }

        // Space available
        if (frames.size() < maxCacheSize)
        {
            Frame newFrame;
            newFrame.key = key;
            newFrame.usageCount = 1;
            newFrame.pinned = true;
            frames.emplace_back(newFrame);
            cache[key] = static_cast<int>(frames.size() - 1);
            std::cout<< "[Insert] Added Key: "<< key<< "\n";
            return;
        }

        // Cache full -> replace frame
        replaceFrame(key);
    }

private:
    struct Frame
    {
        T key;
        int usageCount{0};
        bool pinned{false};
    };

private:
    unsigned int maxCacheSize{0};
    std::vector<Frame> frames;
    std::unordered_map<T, int> cache;
    std::thread bgClockThread;
    std::mutex mtx;
    int clockHand{0};
    std::atomic<bool> stopThread{false};

private:
    void moveClockHand()
    {
        clockHand = (clockHand + 1) % frames.size();
    }

    void replaceFrame(T key)
    {
        while (true)
        {
            Frame& victimFrame = frames[clockHand];

            // Give another chance
            if (victimFrame.usageCount > 0)
            {
                victimFrame.usageCount--;
            }
            else if (!victimFrame.pinned)
            {
                T removedKey = victimFrame.key;
                cache.erase(removedKey);
                victimFrame.key = key;
                victimFrame.usageCount = 1;
                victimFrame.pinned = true;
                cache[key] = clockHand;
                std::cout<< "[Replace] Removed: "<< removedKey<< " | Added: "<< key<< "\n";
                moveClockHand();
                return;
            }
            moveClockHand();
        }
    }

    void runClock()
    {
        while (!stopThread.load())
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::lock_guard<std::mutex> guard(mtx);
            for (Frame& frame : frames)
            {
                // Remove temporary protection
                if (frame.pinned)
                {
                    frame.pinned = false;
                }
                // Gradually reduce usage score
                if (frame.usageCount > 0)
                {
                    frame.usageCount--;
                }
            }

            std::cout<< "[Clock] Background sweep completed\n";
        }
    }
};

int main()
{
    ClockSweep<std::string> clockSweep(3);

    clockSweep.putKey("apple");
    clockSweep.putKey("banana");
    clockSweep.putKey("orange");

    clockSweep.getKey("apple");

    std::this_thread::sleep_for(std::chrono::seconds(3));

    clockSweep.putKey("grapes");

    clockSweep.getKey("banana");
    clockSweep.getKey("orange");
    clockSweep.getKey("grapes");

    std::this_thread::sleep_for(std::chrono::seconds(5));

    return 0;
}