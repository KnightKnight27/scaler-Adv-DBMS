#pragma once

#include <cstring>
#include <string>
#include <cstdio>

// postgres uses 8KB pages — we use a smaller size for testing
static constexpr int PAGE_SIZE = 64;

// represents a disk page loaded into memory
class Page {
public:
    Page() {
        memset(m_Data, 0, PAGE_SIZE);
    }

    // create a page with some content
    explicit Page(const std::string& content) {
        memset(m_Data, 0, PAGE_SIZE);
        strncpy(m_Data, content.c_str(), PAGE_SIZE - 1);
    }

    // read the content
    [[nodiscard]] std::string read() const {
        return std::string(m_Data);
    }

    // write new content into the page
    void write(const std::string& content) {
        memset(m_Data, 0, PAGE_SIZE);
        strncpy(m_Data, content.c_str(), PAGE_SIZE - 1);
    }

    void print() const {
        printf("Page content: [%s]\n", m_Data);
    }

private:
    char m_Data[PAGE_SIZE]{};
};