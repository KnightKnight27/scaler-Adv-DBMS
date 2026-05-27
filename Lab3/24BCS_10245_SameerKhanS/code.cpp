#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

template <typename T>
class ClockSweep
{
private:
    struct Frame
    {
        T key;
        bool referenceBit;
        bool valid;

        Frame()
        {
            referenceBit = false;
            valid = false;
        }
    };

    std::vector<Frame> cache;

    unsigned int capacity;
    unsigned int clockHand;

    std::thread sweepThread;
    std::mutex mtx;

    bool stopThread;

    void runClock()
    {
        while (!stopThread)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            std::lock_guard<std::mutex> lock(mtx);

            if (cache[clockHand].valid)
            {
                cache[clockHand].referenceBit = false;
            }

            clockHand = (clockHand + 1) % capacity;
        }
    }

public:
    ClockSweep(unsigned int size)
    {
        capacity = size;
        clockHand = 0;
        stopThread = false;

        cache.resize(capacity);

        sweepThread = std::thread(&ClockSweep::runClock, this);
    }

    ~ClockSweep()
    {
        stopThread = true;

        if (sweepThread.joinable())
        {
            sweepThread.join();
        }
    }

    void get(const T &key)
    {
        std::lock_guard<std::mutex> lock(mtx);

        for (auto &frame : cache)
        {
            if (frame.valid && frame.key == key)
            {
                frame.referenceBit = true;

                std::cout << key << " found\n";
                return;
            }
        }

        std::cout << key << " not found\n";
    }

    void put(const T &key)
    {
        std::lock_guard<std::mutex> lock(mtx);

        for (auto &frame : cache)
        {
            if (frame.valid && frame.key == key)
            {
                frame.referenceBit = true;
                return;
            }
        }

        while (true)
        {
            if (!cache[clockHand].valid)
            {
                cache[clockHand].key = key;
                cache[clockHand].referenceBit = true;
                cache[clockHand].valid = true;

                clockHand = (clockHand + 1) % capacity;

                return;
            }

            if (cache[clockHand].referenceBit)
            {
                cache[clockHand].referenceBit = false;
            }
            else
            {
                cache[clockHand].key = key;
                cache[clockHand].referenceBit = true;
                cache[clockHand].valid = true;

                clockHand = (clockHand + 1) % capacity;

                return;
            }

            clockHand = (clockHand + 1) % capacity;
        }
    }

    void display()
    {
        std::lock_guard<std::mutex> lock(mtx);

        std::cout << "\nCache State\n";

        for (unsigned int i = 0; i < capacity; i++)
        {
            if (cache[i].valid)
            {
                std::cout << cache[i].key
                          << " "
                          << cache[i].referenceBit;
            }
            else
            {
                std::cout << "EMPTY";
            }

            if (i == clockHand)
            {
                std::cout << " <- clock";
            }

            std::cout << std::endl;
        }
    }
};

int main()
{
    ClockSweep<int> cache(3);

    cache.put(1);
    cache.put(2);
    cache.put(3);

    cache.display();

    cache.get(2);

    cache.put(4);

    cache.display();

    return 0;
}