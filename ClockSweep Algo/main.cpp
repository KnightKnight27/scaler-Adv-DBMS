#include <iostream>
#include <vector>
#include <unordered_map>

using namespace std;

class ClockReplacementPolicy {
private:
    struct CacheBlock {
        int page_num;
        bool is_referenced;
        
        CacheBlock(int id) : page_num(id), is_referenced(true) {}
    };

    int max_size;
    int current_ptr;
    vector<CacheBlock> blocks;
    unordered_map<int, int> lookup_table;

public:
    ClockReplacementPolicy(int size) : max_size(size), current_ptr(0) {}

    void requestPage(int page_num) {
        cout << "Requesting page " << page_num << "...\n";
        
        if (lookup_table.find(page_num) != lookup_table.end()) {
            cout << "  -> Cache Hit! Marking page as referenced.\n";
            int idx = lookup_table[page_num];
            blocks[idx].is_referenced = true;
            return;
        }

        cout << "  -> Cache Miss!\n";
        if (blocks.size() < max_size) {
            blocks.emplace_back(page_num);
            lookup_table[page_num] = blocks.size() - 1;
            cout << "  -> Inserted page " << page_num << " into a free block.\n";
            return;
        }

        while (true) {
            if (blocks[current_ptr].is_referenced) {
                blocks[current_ptr].is_referenced = false;
                current_ptr = (current_ptr + 1) % max_size;
            } else {
                int victim = blocks[current_ptr].page_num;
                cout << "  -> Evicting page " << victim << "\n";
                
                lookup_table.erase(victim);
                
                blocks[current_ptr].page_num = page_num;
                blocks[current_ptr].is_referenced = true;
                lookup_table[page_num] = current_ptr;
                cout << "  -> Placed page " << page_num << " at block index " << current_ptr << ".\n";

                current_ptr = (current_ptr + 1) % max_size;
                break;
            }
        }
    }

    void showState() {
        cout << "--- Cache State (Max Size: " << max_size << ") ---\n";
        if (blocks.empty()) {
            cout << "  [Empty Cache]\n";
            return;
        }
        for (size_t i = 0; i < blocks.size(); ++i) {
            cout << (i == current_ptr ? "=> " : "   ");
            cout << "Block " << i << " | Page: " << blocks[i].page_num 
                 << " | Referenced: " << (blocks[i].is_referenced ? "Yes" : "No") << "\n";
        }
    }
};

int main() {
    ClockReplacementPolicy policy(3);

    vector<int> page_requests = {1, 2, 3, 2, 4, 1, 5, 2};

    for (int p : page_requests) {
        policy.requestPage(p);
        policy.showState();
    }

    return 0;
}
