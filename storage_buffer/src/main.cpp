#include "ClockSweep.h"

int main()
{
    ClockSweep<int> clockSweep(4);

    clockSweep.putKey(1);
    clockSweep.putKey(2);
    clockSweep.putKey(3);
    clockSweep.putKey(4);

    clockSweep.display();

    clockSweep.getKey(2);
    clockSweep.getKey(3);

    clockSweep.putKey(5);

    clockSweep.display();

    clockSweep.putKey(6);

    clockSweep.display();

    return 0;
}