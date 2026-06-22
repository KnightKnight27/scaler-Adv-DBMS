#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

template <typename T>
class ClockSweep {
private:
    struct PageFrame {
        T page;
        bool refBit;

        PageFrame(T p, bool r)
            : page(p), refBit(r) {}
    };

    vector<PageFrame*> frames;
    unordered_map<T, int> pageIndex;

    int capacity;
    int hand;

public:
    ClockSweep(int size) {
        capacity = size;
        hand = 0;
    }

    ~ClockSweep() {
        for (auto f : frames) {
            delete f;
        }
    }

    void access(const T& page) {
        if (pageIndex.count(page)) {
            int idx = pageIndex[page];
            frames[idx]->refBit = true;

            cout << "Page " << page << " -> HIT\n";
            return;
        }

        cout << "Page " << page << " -> MISS\n";

        if ((int)frames.size() < capacity) {
            frames.push_back(new PageFrame(page, true));
            pageIndex[page] = frames.size() - 1;

            return;
        }

        while (true) {
            if (!frames[hand]->refBit) {
                pageIndex.erase(frames[hand]->page);

                frames[hand]->page = page;
                frames[hand]->refBit = true;

                pageIndex[page] = hand;

                hand = (hand + 1) % capacity;

                break;
            }

            frames[hand]->refBit = false;
            hand = (hand + 1) % capacity;
        }
    }

    void display() {
        cout << "Frames: ";

        for (int i = 0; i < frames.size(); i++) {
            cout << "[" << frames[i]->page
                 << ", ref=" << frames[i]->refBit << "] ";

            if (i == hand) {
                cout << "<-hand ";
            }
        }

        cout << "\n\n";
    }
};

int main() {
    ClockSweep<int> cache(3);

    vector<int> pages = {
        1, 2, 3, 2, 4, 1, 5
    };

    for (int p : pages) {
        cache.access(p);
        cache.display();
    }

    return 0;
}