# Lab 3: Clock Sweep Buffer Pool

**Name**: Pratham Onkar Singh

**Roll No.**: 24bcs10136

This is a C++ implementation of the **Clock Sweep (Second Chance)** buffer replacement algorithm. This is the same underlying memory-management concept used by databases like PostgreSQL to decide which data stays in fast RAM, and which data gets kicked out to make room for new stuff.

---

### The Concept

Imagine a circular track with a fixed number of parking spots. Each spot holds a piece of data, and a small sign that says either **`KEEP`** `(1)` or **`EVICT`** `(
template<typename T>
class ClockSweep
public:
   ClockSweep(int maxNumber): maxCacheSize(maxNumber) {};

   T getKey(T key){}

   void putKey(T key){}

private:
  uint maxCacheSize{0u};
  std::thread bgClockThread;
	
};
int main(){
	ClockSweep<int> clockSweep;

}
0)`. 

A pointer (the "clock hand") sits in the middle of the track. 

1. **Getting an item (`getKey`):** When someone asks for an item and it's in the buffer, we hand it to them and flip its sign to **`KEEP`**.
2. **Adding an item (`putKey`):** * If there is an empty parking spot, it parks right there.
   * If the parking lot is completely full, the clock hand starts walking clockwise:
     * If it points to an item marked **`KEEP`**, it flips its sign to **`EVICT`** (giving it a "second chance") and walks to the next spot.
     * If it points to an item marked **`EVICT`**, it kicks that item out, parks the new incoming data in that spot, marks it **`KEEP`**, and stops.
3. **The Background Timer (Aging):** If data got marked `KEEP` once, it shouldn't get to sit in RAM forever. A background thread wakes up every few hundred milliseconds and walks through the whole lot, forcing *everyone's* sign back to **`EVICT`**. If an item isn't used again before the next sweep, it becomes fair game to be replaced.

---

### How to Run It

Open your terminal in this directory. 

**Option A: Using CMake (Recommended)**

    cmake .
    make
    ./clock_pool


**Option B: The quick 1-line g++ command**

    g++ -std=c++17 -pthread main.cpp -o clock_pool
    ./clock_pool

---

### What the 3 Demos Show

When you run the program, it runs three automated tests:

* **Test 1 (The Textbook Case):** Fills a 3-slot buffer with integers `[10, 20, 30]`. It pauses to let the background thread flip all their safety bits to `0`. It then accesses `20` (saving it), and tries to squeeze a `40` in. You will watch it skip `20` and evict `10` instead.
* **Test 2 (Generic Types):** Proves the template works with non-number types by passing `std::string` objects into it.
* **Test 3 (Re-pinning):** Proves that if you try to `putKey()` an item that is *already* sitting inside the buffer, it doesn't duplicate it; it just flips its safety bit back to `1`.