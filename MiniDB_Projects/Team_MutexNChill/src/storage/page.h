#pragma once
#include <cstdint>
#include <cstring>

// A page is 4096 bytes — same as the OS memory page size.
// This avoids partial-page I/O when the OS reads from disk.
static const int PAGE_SIZE = 4096;

// Every row stored in this database uses this fixed layout.
// Both "students" and "departments" share the same struct;
// columns that don't apply to a table are simply left as 0.
//
//   students:    id | name | age | extra=dept_id
//   departments: id | name | age=0 | extra=0
struct Row {
    int  id;
    char name[32];
    int  age;
    int  extra;    // dept_id in students, unused elsewhere
    bool is_valid; // false means the slot was deleted
};

// The first 8 bytes of every page are this header.
struct PageHeader {
    int page_id;
    int num_rows; // slots used so far (includes deleted rows)
};

// How many rows fit in one page after the 8-byte header.
static const int ROWS_PER_PAGE =
    (PAGE_SIZE - (int)sizeof(PageHeader)) / (int)sizeof(Row);

// A Page is a raw 4096-byte block.
// We overlay the header and row array on top of the same memory.
struct Page {
    uint8_t data[PAGE_SIZE];

    PageHeader& header() {
        return *reinterpret_cast<PageHeader*>(data);
    }

    Row* rows() {
        return reinterpret_cast<Row*>(data + sizeof(PageHeader));
    }
};
