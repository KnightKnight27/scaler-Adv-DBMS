#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

class ClockSweep
{
private:
    struct Frame
    {
        int page;
        int reference;
    };

    vector<Frame> frames;
    unordered_map<int, int> position;
    int capacity;
    int pointer;

public:
    ClockSweep(int totalFrames)
    {
        capacity = totalFrames;
        pointer = 0;
    }

    void requestPage(int page)
    {
        if (position.find(page) != position.end())
        {
            int index = position[page];
            frames[index].reference = 1;
            cout << "Page " << page << " -> HIT\n";
            return;
        }

        cout << "Page " << page << " -> MISS\n";

        if ((int)frames.size() < capacity)
        {
            Frame newFrame;
            newFrame.page = page;
            newFrame.reference = 1;
            frames.push_back(newFrame);
            position[page] = frames.size() - 1;
            return;
        }

        while (frames[pointer].reference == 1)
        {
            frames[pointer].reference = 0;
            pointer = (pointer + 1) % capacity;
        }

        position.erase(frames[pointer].page);
        frames[pointer].page = page;
        frames[pointer].reference = 1;
        position[page] = pointer;
        pointer = (pointer + 1) % capacity;
    }

    void printFrames()
    {
        cout << "Frames: ";

        for (int i = 0; i < (int)frames.size(); i++)
        {
            cout << "[" << frames[i].page << ", ref=" << frames[i].reference << "] ";

            if (i == pointer)
            {
                cout << "<-pointer ";
            }
        }

        cout << "\n\n";
    }
};

int main()
{
    ClockSweep memory(3);

    vector<int> pages = {1, 2, 3, 2, 4, 1, 5};

    for (int page : pages)
    {
        memory.requestPage(page);
        memory.printFrames();
    }

    return 0;
}
