#include <thread>

typedef unsigned int uint;

template<typename T>
class ClockSweep
{
public:
   ClockSweep(int maxNumber = 0): maxCacheSize(maxNumber) {};

   T getKey(T key) { return key; }

   void putKey(T key) {}

private:
  uint maxCacheSize{0u};
  std::thread bgClockThread;
};

int main(){
	ClockSweep<int> clockSweep;
}
