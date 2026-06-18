// Lab 2 - 24BCS10404 - Rajveer Bishnoi
// ClockSweep buffer replacement policy stub.

#include <thread>
#include <cstdint>

template<typename T>
class ClockSweep {
public:
    ClockSweep(int maxNumber) : maxCacheSize(maxNumber) {}

    T getKey(T key) {}

    void putKey(T key) {}

private:
    uint32_t maxCacheSize{0u};
    std::thread bgClockThread;
};

int main() {
    ClockSweep<int> clockSweep(16);
    return 0;
}
