#pragma once
#include "page.h"
#include <string>
#include <fstream>
#include <mutex>

class HeapFile {
private:
    std::fstream file;
    std::string file_name;
    std::mutex disk_io_lock; // Prevents overlapping disk reads/writes

public:
    HeapFile(const std::string& name);
    ~HeapFile();
    
    void readPage(int page_id, Page* page);
    void writePage(int page_id, Page* page);
    int allocatePage(); // Returns the next available page_id based on file size
};
