#include <iostream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <chrono>

template<typename T>
class ClockCache
{
private:

    struct Frame
    {
        T value{};
        bool used{false};
        bool occupied{false};
    };

public:

    ClockCache(size_t capacity)
        : limit(capacity)
    {
        slots.resize(limit);

        cleanerThread = std::thread(
            &ClockCache::backgroundTask,
            this
        );
    }

    ~ClockCache()
    {
        terminate = true;

        if(cleanerThread.joinable())
        {
            cleanerThread.join();
        }
    }

    void insert(const T& item)
    {
        if(positionMap.count(item))
        {
            int pos = positionMap[item];

            slots[pos].used = true;

            std::cout
                << "Item already present : "
                << item
                << "\n";

            return;
        }

        while(true)
        {
            Frame &current = slots[pointer];

            if(!current.occupied)
            {
                place(pointer, item);
                return;
            }

            if(current.used)
            {
                current.used = false;
            }
            else
            {
                positionMap.erase(current.value);

                place(pointer, item);
                return;
            }

            movePointer();
        }
    }

    T access(const T& item)
    {
        if(!positionMap.count(item))
        {
            std::cout
                << "Item missing\n";

            return T();
        }

        int pos = positionMap[item];

        slots[pos].used = true;

        std::cout
            << "Fetched : "
            << item
            << "\n";

        return slots[pos].value;
    }

    void printState() const
    {
        std::cout
            << "\nCurrent Cache:\n";

        for(const auto &entry : slots)
        {
            if(entry.occupied)
            {
                std::cout
                    << entry.value
                    << " [used="
                    << entry.used
                    << "]\n";
            }
        }

        std::cout << "\n";
    }

private:

    void place(int index, const T& item)
    {
        slots[index].value = item;
        slots[index].used = true;
        slots[index].occupied = true;

        positionMap[item] = index;

        std::cout
            << "Inserted : "
            << item
            << "\n";

        movePointer();
    }

    void movePointer()
    {
        pointer = (pointer + 1) % limit;
    }

    void backgroundTask()
    {
        while(!terminate)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1500)
            );
        }
    }

private:

    size_t limit{0};

    int pointer{0};

    bool terminate{false};

    std::vector<Frame> slots;

    std::unordered_map<T, int> positionMap;

    std::thread cleanerThread;
};

int main()
{
    ClockCache<int> cache(3);

    cache.insert(10);
    cache.insert(20);
    cache.insert(30);

    cache.printState();

    cache.access(10);

    cache.insert(40);

    cache.printState();

    return 0;
}