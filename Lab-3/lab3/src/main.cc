
#define LOGGING

#include <thread>
#include "ClockSweepCache.h"

int main()
{
	using namespace std::chrono_literals;

	ClockSweepCache<int> cache;

	cache.put(1, 2);
	cache.put(2, 3);
	cache.put(3, 4);
	cache.put(4, 5);
	cache.put(5, 6);

	for (int key = 5; key >= 1; key--) {
		for (int times = key; times >= 1; times--) {
			[[maybe_unused]] auto res = cache.get(key);
		}
	}

	std::this_thread::sleep_for(7s);
}
