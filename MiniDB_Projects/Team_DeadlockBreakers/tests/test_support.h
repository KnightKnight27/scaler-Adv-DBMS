#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

inline void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

inline void RemoveFile(const std::string& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void TestStorageAndIndex();
void TestTransactionsAndDeadlock();
void TestRecovery();
