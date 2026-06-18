#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <algorithm>

template <typename K, typename V>
class ClockSweep{
public:
    ClockSweep(int maxNumber, int maxFreq) : maxCacheSize(maxNumber), maxFreq(maxFreq) {};

    V getKey(K key){
        auto it = look_up.find(key);
        if(it==look_up.end()){
            throw std::runtime_error("Key not in cache");
        }else{
            size_t idx = it->second.second;
            clock[idx].second = std::min(clock[idx].second+1,maxFreq);
            return it->second.first;
        }
    }


    void putKey(K key, V value) {
        auto it = look_up.find(key);
        if(it!=look_up.end()){
            it->second.first = value;
            size_t idx = it->second.second;
            clock[idx].second = std::min(clock[idx].second+1,maxFreq);
            return;
        }
        if(look_up.size()<maxCacheSize){
            look_up.insert({key,{value,clock.size()}});
            clock.push_back({key,1});
        }else{
            while(true){
                if(clock[curr].second==0){
                    K to_evict = clock[curr].first;
                    look_up.erase(to_evict);
                    look_up.insert({key,{value,curr}});
                    clock[curr] = {key,1};
                    curr = (curr+1)%clock.size();
                    break;
                }else{
                    clock[curr].second-=1;
                }
                curr = (curr+1)%clock.size();
            }
        }
    }

    void printState(){
        std::cout<<curr<<std::endl;
        for(int i =0;i<clock.size();i++){
            std::cout<<i<<" "<<clock[i].first<<" "<<clock[i].second<<std::endl;
        }
    }

private:
    unsigned int maxCacheSize;
    unsigned int maxFreq;
    unsigned curr = 0;

    std::vector<std::pair<K,unsigned int>> clock;
    std::unordered_map<K, std::pair<V,size_t>> look_up;
};


int main() {

    ClockSweep<int, std::string> cache(3,20);

    cache.putKey(1, "one");
    cache.putKey(2, "two");
    // cache.putKey(3, "three");

    cache.printState();

    std::cout << "\nAccessing 1 twice\n";

    cache.getKey(1);
    cache.getKey(1);

    cache.printState();

    std::cout << "\nInserting 4\n";

    cache.putKey(4, "four");

    cache.printState();

    std::cout << "\nInserting 5\n";

    cache.putKey(5, "five");

    cache.printState();
    std::cout << "\nTesting gets\n\n";

    try {

        std::cout << "1 -> " << cache.getKey(1) << '\n';

    }
    catch(const std::runtime_error&) {

        std::cout << "1 not found\n";
    }

    try {

        std::cout << "2 -> " << cache.getKey(2) << '\n';

    }
    catch(const std::runtime_error&) {

        std::cout << "2 not found\n";
    }

    try {

        std::cout << "3 -> " << cache.getKey(3) << '\n';
    }
    catch(const std::runtime_error&) {

        std::cout << "3 not found\n";
    }

    try {

        std::cout << "4 -> " << cache.getKey(4) << '\n';

    }
    catch(const std::runtime_error&) {

        std::cout << "4 not found\n";
    }

    try {

        std::cout << "5 -> " << cache.getKey(5) << '\n';

    }
    catch(const std::runtime_error&) {

        std::cout << "5 not found\n";
    }

    std::cout << "\nMaking 1 very hot\n";

    for(int i = 0; i < 10; i++) {

        cache.getKey(1);
    }

    cache.printState();

    std::cout << "Inserting 6, 7, 8\n";

    cache.putKey(6, "six");
    cache.printState();

    cache.putKey(7, "seven");
    cache.printState();

    cache.putKey(8, "eight");
    cache.printState();

    std::cout << "\nchecking update\n";

    cache.putKey(1, "updated");
    std::cout << "1 -> " << cache.getKey(1) << '\n';
    cache.printState();


    return 0;
}

