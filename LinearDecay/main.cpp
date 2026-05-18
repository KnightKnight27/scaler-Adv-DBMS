#include <unordered_map>
#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

using namespace std;

template <typename T>
class LinearDecay
{
public:
    LinearDecay(int maxSize) : maxSize(maxSize) , decayStatus(true)
    {
        decayThread = thread(&LinearDecay::decay, this);
    }

    ~LinearDecay()
    {
        decayStatus = false;
        if(decayThread.joinable())
        {
            decayThread.join();
        }
    }

    T get(T key)
    {
        lock_guard<mutex> lock(mtx);
        if(cache.find(key) != cache.end())
        {
            cache[key]++;
            return key;
        }
        cout<<"Key not found in cache. Might have been evicted."<<endl;
        return T();
    }

    void put(T key)
    {
        lock_guard<mutex> lock(mtx);
        cache[key]++;
        if(cache.size() > maxSize)
        {
            evict();
        }
    }

private:
    int maxSize;
    unordered_map<T,int> cache;
    thread decayThread;
    atomic<bool> decayStatus;
    mutex mtx;
    void evict()
    {
        auto it = cache.begin();

        for(auto i = cache.begin(); i != cache.end(); i++)
        {
            if(i->second < it->second)
            {
                it = i;
            }
        }
        cout<<"Evicting : "<<it->first<<endl;
        cache.erase(it);
    }

    void decay()
    {
        while(decayStatus.load())
        {
            this_thread::sleep_for(chrono::seconds(1));
            lock_guard<mutex> lock(mtx);
            for(auto &x : cache)
            {
                x.second = max(0, x.second - 1);
            }
            cout<<"Decay Applied"<<endl;
        }
    }
};

int main()
{
    LinearDecay<int> linearDecay(3);
}