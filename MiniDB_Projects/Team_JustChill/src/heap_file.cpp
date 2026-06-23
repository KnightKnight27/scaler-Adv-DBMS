#include "heap_file.h"
#include <stdexcept>
#include <iostream>

HeapFile::HeapFile(const std::string& name) : file_name(name) {
    // Open the file in binary read/write mode
    file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        // Clear flags and create the file if it does not exist
        file.clear();
        std::ofstream create_file(file_name, std::ios::binary | std::ios::out);
        create_file.close();
        
        // Open the newly created file
        file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file.is_open()) {
        throw std::runtime_error("HeapFile Error: Failed to open or create file: " + file_name);
    }
}

HeapFile::~HeapFile() {
    std::lock_guard<std::mutex> lock(disk_io_lock);
    if (file.is_open()) {
        file.close();
    }
}

void HeapFile::readPage(int page_id, Page* page) {
    std::lock_guard<std::mutex> lock(disk_io_lock);
    if (!file.is_open()) {
        throw std::runtime_error("HeapFile Error: File is not open");
    }
    
    file.clear();
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    file.seekg(offset, std::ios::beg);
    
    if (!file.good()) {
        // If seek fails (offset beyond end of file), reset the page data
        page->reset();
        page->page_id = page_id;
        return;
    }
    
    file.read(page->data, PAGE_SIZE);
    std::streamsize bytes_read = file.gcount();
    if (bytes_read < PAGE_SIZE) {
        std::memset(page->data + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
    
    page->page_id = page_id;
    page->pin_count = 0;
    page->is_dirty = false;
}

void HeapFile::writePage(int page_id, Page* page) {
    std::lock_guard<std::mutex> lock(disk_io_lock);
    if (!file.is_open()) {
        throw std::runtime_error("HeapFile Error: File is not open");
    }
    
    file.clear();
    std::streamoff offset = static_cast<std::streamoff>(page_id) * PAGE_SIZE;
    file.seekp(offset, std::ios::beg);
    
    file.write(page->data, PAGE_SIZE);
    file.flush();
    
    page->is_dirty = false;
}

int HeapFile::allocatePage() {
    std::lock_guard<std::mutex> lock(disk_io_lock);
    if (!file.is_open()) {
        throw std::runtime_error("HeapFile Error: File is not open");
    }
    
    file.clear();
    file.seekg(0, std::ios::end);
    std::streamoff file_size = file.tellg();
    if (file_size < 0) {
        file_size = 0;
    }
    
    int next_page_id = static_cast<int>(file_size / PAGE_SIZE);
    
    // Extend the file by writing a full page of zeroes
    std::streamoff offset = static_cast<std::streamoff>(next_page_id) * PAGE_SIZE;
    file.seekp(offset, std::ios::beg);
    
    char zero_data[PAGE_SIZE] = {0};
    file.write(zero_data, PAGE_SIZE);
    file.flush();
    
    return next_page_id;
}
