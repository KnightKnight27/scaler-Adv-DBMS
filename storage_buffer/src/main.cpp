#include "ClockSweep.h"

int main()
{
    try
    {
        ClockSweep<int> cache(4);

        cache.putKey(1);
        cache.putKey(2);
        cache.putKey(3);
        cache.putKey(4);

        cache.display();

        cache.getKey(2);
        cache.getKey(3);

        cache.putKey(5);
        cache.display();

        cache.putKey(6);
        cache.display();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
    }

    return 0;
}