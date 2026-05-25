#include <thread>

template<typename K>
class BufferManager {
public:
    BufferManager(int max_cap) : capacity(max_cap) {}

    K fetchKey(K key) {
        // Implementation
        return key;
    }

    void storeKey(K key) {
        // Implementation
    }

private:
    unsigned int capacity{0u};
    std::thread worker_thread;
};

int main() {
    BufferManager<int> mgr(10);
    return 0;
}
