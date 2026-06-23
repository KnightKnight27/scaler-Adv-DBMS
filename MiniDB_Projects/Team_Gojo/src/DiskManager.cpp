#include "DiskManager.h"

#include <cstring>
#include <stdexcept>

DiskManager::DiskManager(const std::string& filePath)
    : filePath_(filePath)
{
    // Create the file if it doesn't exist by opening in app mode first
    {
        std::ofstream creator(filePath, std::ios::app | std::ios::binary);
        if (!creator.is_open()) {
            throw std::runtime_error("Cannot create database file: " + filePath);
        }
    }

    // Now open in read-write binary mode
    dbFile_.open(filePath,
                 std::ios::in | std::ios::out | std::ios::binary);
    if (!dbFile_.is_open()) {
        throw std::runtime_error("Cannot open database file: " + filePath);
    }
}

DiskManager::~DiskManager() {
    if (dbFile_.is_open()) {
        dbFile_.close();
    }
}

void DiskManager::readPage(int pageId, uint8_t* dest) {
    validatePageId(pageId);

    long offset = static_cast<long>(pageId) * Page::PAGE_SIZE;

    // Zero-fill the destination first (handles partial/beyond-EOF reads)
    std::memset(dest, 0, Page::PAGE_SIZE);

    dbFile_.seekg(offset, std::ios::beg);
    if (dbFile_.good()) {
        dbFile_.read(reinterpret_cast<char*>(dest), Page::PAGE_SIZE);
        // Clear any EOF/fail bits from a partial read (page is zero-padded)
        dbFile_.clear();
    }
}

void DiskManager::writePage(int pageId, const uint8_t* src) {
    validatePageId(pageId);

    if (src == nullptr) {
        throw std::invalid_argument("Page data must not be null");
    }

    long offset = static_cast<long>(pageId) * Page::PAGE_SIZE;

    dbFile_.seekp(offset, std::ios::beg);
    dbFile_.write(reinterpret_cast<const char*>(src), Page::PAGE_SIZE);

    if (!dbFile_.good()) {
        throw std::runtime_error("Failed to write page " + std::to_string(pageId));
    }
}

int DiskManager::getNumPages() {
    dbFile_.seekg(0, std::ios::end);
    long length = dbFile_.tellg();
    if (length <= 0) return 0;
    return static_cast<int>((length + Page::PAGE_SIZE - 1) / Page::PAGE_SIZE);
}

int DiskManager::allocatePage() {
    int newPageId = getNumPages();
    uint8_t blank[Page::PAGE_SIZE];
    std::memset(blank, 0, Page::PAGE_SIZE);
    writePage(newPageId, blank);
    return newPageId;
}

void DiskManager::sync() {
    dbFile_.flush();
}

void DiskManager::validatePageId(int pageId) {
    if (pageId < 0) {
        throw std::invalid_argument(
            "pageId must be >= 0, got " + std::to_string(pageId));
    }
}
