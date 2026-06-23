#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstring>

struct Record {
    uint32_t id;
    char uuid[36];
    char payload[216]; // 256 bytes total
};

int main(int argc, char* argv[]) {
    std::string filename = "io_test.dat";
    if (argc > 1) {
        filename = argv[1];
    }

    std::cout << "[Lab 1] Initializing high-throughput file I/O test on file: " << filename << std::endl;
    
    const int num_records = 4096; // 1 MB total data
    std::vector<Record> records(num_records);
    
    for (int i = 0; i < num_records; ++i) {
        records[i].id = i;
        std::strcpy(records[i].uuid, "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d");
        std::memset(records[i].payload, 'A' + (i % 26), sizeof(records[i].payload) - 1);
        records[i].payload[sizeof(records[i].payload) - 1] = '\0';
    }

    std::ofstream outfile(filename, std::ios::out | std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Failed to open output file: " << filename << std::endl;
        return 1;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Write in blocks of 16 records (4KB blocks, matching OS page size)
    const int block_size = 16;
    for (int i = 0; i < num_records; i += block_size) {
        outfile.write(reinterpret_cast<const char*>(&records[i]), block_size * sizeof(Record));
        outfile.flush(); // Force write system call
    }

    outfile.close();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> duration = end_time - start_time;

    std::cout << "[Lab 1] Wrote " << num_records << " records (" 
              << (num_records * sizeof(Record)) / 1024.0 << " KB) successfully." << std::endl;
    std::cout << "[Lab 1] Elapsed time: " << duration.count() << " ms" << std::endl;

    return 0;
}
