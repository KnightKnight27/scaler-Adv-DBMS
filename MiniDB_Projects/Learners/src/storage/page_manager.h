#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include <string>
#include <unordered_map>
#include <fstream>
#include <memory>

class PageManager {
private:
    std::string db_dir;
    // Map table_name to its file stream
    std::unordered_map<std::string, std::shared_ptr<std::fstream>> files;

    std::shared_ptr<std::fstream> get_file(const std::string& table_name);
    std::string get_file_path(const std::string& table_name) const;

public:
    explicit PageManager(const std::string& db_dir);
    ~PageManager();

    int allocate_page(const std::string& table_name);
    void read_page(const std::string& table_name, int page_id, uint8_t* buffer);
    void write_page(const std::string& table_name, int page_id, const uint8_t* buffer);
    int get_num_pages(const std::string& table_name);
    void close_table(const std::string& table_name);
    void close_all();
};

#endif
