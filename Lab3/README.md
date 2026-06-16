# Lab 3 - Clock Sweep Page Replacement Algorithm

## Objective

Implement PostgreSQL's ClockSweep page replacement algorithm and understand how it approximates LRU while avoiding the overhead of maintaining an ordered list.

## Files

* clocksweep.cpp
* observations.md

## Compilation

```bash
g++ -std=c++17 clocksweep.cpp -o clocksweep
```

## Execution

```bash
./clocksweep
```

## Output

The program simulates page accesses and demonstrates:

* Page hits
* Page misses
* Page evictions
* Clock hand movement
* Usage count updates

This approximates PostgreSQL's actual buffer replacement algorithm.
