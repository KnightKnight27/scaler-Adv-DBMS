#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "storage/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/page.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string TempDbPath(const char* suffix) {
    return std::string("/tmp/minidb_test_") + suffix + "_" +
           std::to_string(static_cast<unsigned long>(std::time(nullptr))) + ".db";
}

void RemoveFile(const std::string& path) {
    std::remove(path.c_str());
}

bool FilesMatch(const std::string& expected_path, const std::string& actual_path) {
    std::ifstream expected(expected_path, std::ios::binary);
    std::ifstream actual(actual_path, std::ios::binary);
    if (!expected || !actual) {
        return false;
    }

    char expected_byte = 0;
    char actual_byte = 0;
    while (expected.get(expected_byte)) {
        if (!actual.get(actual_byte) || expected_byte != actual_byte) {
            return false;
        }
    }
    return !actual.get(actual_byte);
}

void WriteMarkerPage(minidb::DiskManager& disk, minidb::page_id_t page_id, uint32_t marker) {
    char* buffer = minidb::DiskManager::AllocatePageBuffer();
    minidb::Page::InitPage(buffer);
    minidb::Page page(buffer);
    minidb::PageHeader header = page.GetHeader();
    header.checksum = marker;
    page.SetHeader(header);
    disk.WritePage(page_id, buffer);
    minidb::DiskManager::FreePageBuffer(buffer);
}

}  // namespace

TEST_CASE("Page layout and serialization") {
    char page_data[minidb::PAGE_SIZE]{};
    minidb::Page page(page_data);

    minidb::PageHeader header = minidb::Page::MakeDefaultHeader();
    header.checksum = 0xDEADBEEF;
    header.slot_count = 2;
    header.free_space_pointer = 4000;
    header.lsn = 42;
    page.SetHeader(header);

    minidb::SlotEntry slot0{100, 50};
    minidb::SlotEntry slot1{200, 75};
    page.SetSlot(0, slot0);
    page.SetSlot(1, slot1);

    minidb::PageHeader read_header = page.GetHeader();
    CHECK(read_header.checksum == 0xDEADBEEF);
    CHECK(read_header.slot_count == 2);
    CHECK(read_header.free_space_pointer == 4000);
    CHECK(read_header.lsn == 42);

    minidb::SlotEntry read_slot0 = page.GetSlot(0);
    minidb::SlotEntry read_slot1 = page.GetSlot(1);
    CHECK(read_slot0.offset == 100);
    CHECK(read_slot0.length == 50);
    CHECK(read_slot1.offset == 200);
    CHECK(read_slot1.length == 75);
}

TEST_CASE("DiskManager direct 4096-byte page I/O") {
    const std::string db_path = TempDbPath("disk");
    RemoveFile(db_path);

    {
        minidb::DiskManager disk(db_path);
        WriteMarkerPage(disk, 0, 111);
        WriteMarkerPage(disk, 7, 777);

        char* read_buffer = minidb::DiskManager::AllocatePageBuffer();
        disk.ReadPage(7, read_buffer);

        minidb::Page page(read_buffer);
        minidb::PageHeader header = page.GetHeader();
        CHECK(header.checksum == 777);

        minidb::DiskManager::FreePageBuffer(read_buffer);
    }

    RemoveFile(db_path);
}

TEST_CASE("BufferPoolManager pin and unpin behavior") {
    const std::string db_path = TempDbPath("pin");
    RemoveFile(db_path);

    {
        minidb::DiskManager disk(db_path);
        WriteMarkerPage(disk, 1, 101);

        minidb::BufferPoolManager pool(&disk, 4);
        char* page = pool.FetchPage(1);
        CHECK(page != nullptr);

        minidb::Page wrapped(page);
        CHECK(wrapped.GetHeader().checksum == 101);

        pool.UnpinPage(1);

        char* page_again = pool.FetchPage(1);
        CHECK(page_again == page);
        pool.UnpinPage(1);
    }

    RemoveFile(db_path);
}

TEST_CASE("BufferPoolManager loads 10000 pages with eviction and binary match") {
    const std::string db_path = TempDbPath("10k");
    const std::string reference_path = TempDbPath("10k_ref");
    RemoveFile(db_path);
    RemoveFile(reference_path);

    constexpr minidb::page_id_t kPageCount = 10000;
    constexpr std::size_t kPoolSize = 64;

    {
        minidb::DiskManager reference_disk(reference_path);
        for (minidb::page_id_t page_id = 0; page_id < kPageCount; ++page_id) {
            WriteMarkerPage(reference_disk, page_id, static_cast<uint32_t>(page_id + 1));
        }
    }

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, kPoolSize);

        for (minidb::page_id_t page_id = 0; page_id < kPageCount; ++page_id) {
            char* page = pool.FetchPage(page_id);
            minidb::Page::InitPage(page);
            minidb::Page wrapped(page);
            minidb::PageHeader header = minidb::Page::MakeDefaultHeader();
            header.checksum = static_cast<uint32_t>(page_id + 1);
            wrapped.SetHeader(header);
            pool.MarkDirty(page_id);
            pool.UnpinPage(page_id);
        }

        pool.FlushAllPages();
    }

    CHECK(FilesMatch(reference_path, db_path));

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, kPoolSize);

        for (minidb::page_id_t page_id = 0; page_id < kPageCount; ++page_id) {
            char* page = pool.FetchPage(page_id);
            minidb::Page wrapped(page);
            CHECK(wrapped.GetHeader().checksum == static_cast<uint32_t>(page_id + 1));
            pool.UnpinPage(page_id);
        }
    }

    RemoveFile(db_path);
    RemoveFile(reference_path);
}

TEST_CASE("BufferPoolManager flushes dirty pages on eviction") {
    const std::string db_path = TempDbPath("evict");
    RemoveFile(db_path);

    {
        minidb::DiskManager disk(db_path);
        minidb::BufferPoolManager pool(&disk, 2);

        char* page0 = pool.FetchPage(0);
        minidb::Page::InitPage(page0);
        minidb::Page(page0).SetHeader([] {
            minidb::PageHeader header = minidb::Page::MakeDefaultHeader();
            header.checksum = 900;
            return header;
        }());
        pool.MarkDirty(0);
        pool.UnpinPage(0);

        char* page1 = pool.FetchPage(1);
        minidb::Page::InitPage(page1);
        pool.UnpinPage(1);

        char* page2 = pool.FetchPage(2);
        minidb::Page::InitPage(page2);
        minidb::Page(page2).SetHeader([] {
            minidb::PageHeader header = minidb::Page::MakeDefaultHeader();
            header.checksum = 902;
            return header;
        }());
        pool.MarkDirty(2);
        pool.UnpinPage(2);

        pool.FlushAllPages();
    }

    {
        minidb::DiskManager disk(db_path);
        char* buffer0 = minidb::DiskManager::AllocatePageBuffer();
        disk.ReadPage(0, buffer0);
        CHECK(minidb::Page(buffer0).GetHeader().checksum == 900);
        minidb::DiskManager::FreePageBuffer(buffer0);

        char* buffer2 = minidb::DiskManager::AllocatePageBuffer();
        disk.ReadPage(2, buffer2);
        CHECK(minidb::Page(buffer2).GetHeader().checksum == 902);
        minidb::DiskManager::FreePageBuffer(buffer2);
    }

    RemoveFile(db_path);
}
