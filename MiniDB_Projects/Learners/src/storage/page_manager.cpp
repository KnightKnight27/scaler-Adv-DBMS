#include "page_manager.h"
#include "page.h"
#include "../compat.h"
#include <iostream>

PageManager::PageManager(const std::string& db_dir) : db_dir(db_dir) {
    fs_compat::create_directories(db_dir);
}

PageManager::~PageManager() {
    close_all();
}

std::string PageManager::get_file_path(const std::string& table_name) const {
    return db_dir + "/" + table_name + ".db";
}

std::shared_ptr<std::fstream> PageManager::get_file(const std::string& table_name) {
    auto it = files.find(table_name);
    if (it != files.end()) {
        return it->second;
    }

    std::string path = get_file_path(table_name);
    
    // Ensure file exists by opening in append mode first if it doesn't
    if (!fs_compat::exists(path)) {
        std::ofstream tmp(path, std::ios::binary);
        tmp.close();
    }

    // Open file in read/write/binary mode
    auto stream = std::make_shared<std::fstream>(
        path, 
        std::ios::in | std::ios::out | std::ios::binary
    );

    if (!stream->is_open()) {
        throw std::runtime_error("Failed to open database file: " + path);
    }

    files[table_name] = stream;
    return stream;
}

int PageManager::allocate_page(const std::string& table_name) {
    auto f = get_file(table_name);
    f->seekp(0, std::ios::end);
    long long file_size = f->tellp();
    int page_id = file_size / PAGE_SIZE;

    // Write a blank page
    std::vector<uint8_t> blank_page(PAGE_SIZE, 0);
    f->write((char*)blank_page.data(), PAGE_SIZE);
    f->flush();

    return page_id;
}

void PageManager::read_page(const std::string& table_name, int page_id, uint8_t* buffer) {
    auto f = get_file(table_name);
    f->clear();

    f->seekg(0, std::ios::end);
    long long file_size = f->tellg();
    int num_pages = file_size / PAGE_SIZE;
    if (page_id >= num_pages) {
        f->seekp(0, std::ios::end);
        std::vector<uint8_t> blank(PAGE_SIZE, 0);
        for (int i = num_pages; i <= page_id; ++i) {
            f->write((char*)blank.data(), PAGE_SIZE);
        }
        f->flush();
    }

    f->seekg(page_id * PAGE_SIZE, std::ios::beg);
    f->read((char*)buffer, PAGE_SIZE);
    
    // Pad with zeros if read was partial (reached EOF or read fewer bytes)
    int bytes_read = f->gcount();
    if (bytes_read < PAGE_SIZE) {
        std::memset(buffer + bytes_read, 0, PAGE_SIZE - bytes_read);
    }
}

void PageManager::write_page(const std::string& table_name, int page_id, const uint8_t* buffer) {
    auto f = get_file(table_name);
    f->clear();
    f->seekp(page_id * PAGE_SIZE, std::ios::beg);
    f->write((const char*)buffer, PAGE_SIZE);
    f->flush();
}

int PageManager::get_num_pages(const std::string& table_name) {
    auto f = get_file(table_name);
    f->clear();
    f->seekg(0, std::ios::end);
    long long file_size = f->tellg();
    return file_size / PAGE_SIZE;
}

void PageManager::close_table(const std::string& table_name) {
    auto it = files.find(table_name);
    if (it != files.end()) {
        if (it->second->is_open()) {
            it->second->close();
        }
        files.erase(it);
    }
}

void PageManager::close_all() {
    for (auto& entry : files) {
        if (entry.second->is_open()) {
            entry.second->close();
        }
    }
    files.clear();
}
