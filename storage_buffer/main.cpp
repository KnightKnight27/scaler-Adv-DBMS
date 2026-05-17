#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>

template<typename T>
class ClockSweep{
public:

   ClockSweep(int maxNumber)
      : maxCacheSize(maxNumber)
   {
      bgClockThread =
         std::thread(&ClockSweep::runClockSweep,this);
   }

   ~ClockSweep(){
      stopThread=true;

      if(bgClockThread.joinable())
         bgClockThread.join();
   }

   T getKey(T key){

      std::lock_guard<std::mutex> lock(cacheMutex);

      for(auto &entry:cache){

         if(entry.key==key){

            entry.referenceBit=true;
            return entry.key;
         }
      }

      return T{};
   }

   void putKey(T key){

      std::lock_guard<std::mutex> lock(cacheMutex);

      for(auto &entry:cache){

         if(entry.key==key){

            entry.referenceBit=true;
            return;
         }
      }

      if(cache.size()<maxCacheSize){

         cache.push_back({key,true});
         return;
      }

      while(true){

         if(clockHand>=cache.size())
            clockHand=0;

         if(cache[clockHand].referenceBit){

            cache[clockHand].referenceBit=false;
         }
         else{

            cache[clockHand].key=key;
            cache[clockHand].referenceBit=true;

            clockHand++;
            break;
         }

         clockHand++;
      }
   }

private:

   struct CacheEntry{

      T key;
      bool referenceBit;
   };

   void runClockSweep(){

      while(!stopThread){

         std::this_thread::sleep_for(
            std::chrono::seconds(5)
         );
      }
   }

   uint maxCacheSize{0u};

   std::vector<CacheEntry> cache;

   int clockHand{0};

   std::thread bgClockThread;

   std::mutex cacheMutex;

   bool stopThread{false};
};

int main(){

   ClockSweep<int> clockSweep(3);

   clockSweep.putKey(1);
   clockSweep.putKey(2);
   clockSweep.putKey(3);

   clockSweep.getKey(1);

   clockSweep.putKey(4);
}