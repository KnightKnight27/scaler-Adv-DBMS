#include "ClockSweepManager.hpp"
#include <iostream>

int main() {
    try {
        ClockSweepManager buffer(3);
        
        std::cout << "Starting Lab 3: PostgreSQL Clock Sweep Simulation\n";

        buffer.AccessPage(101);
        buffer.AccessPage(102);
        buffer.AccessPage(103);
        buffer.PrintState();

        buffer.AccessPage(101);
        buffer.AccessPage(102);
        buffer.AccessPage(102);
        buffer.PrintState();

        buffer.PinPage(101);

        buffer.AccessPage(104);
        buffer.PrintState();

        buffer.UnpinPage(101);
        buffer.AccessPage(105);
        buffer.PrintState();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }

    return 0;
}