#pragma once

#include <iostream>
#include <string>
#include <mutex>

namespace minidb {

// Inline mutex to guarantee thread-safe console output across the engine
inline std::mutex global_log_mutex;

#define LOG_INFO(...) \
    do { \
        std::lock_guard<std::mutex> lock(global_log_mutex); \
        std::cout << "[INFO] " << __VA_ARGS__ << std::endl; \
    } while (0)

#define LOG_DEBUG(...) \
    do { \
        std::lock_guard<std::mutex> lock(global_log_mutex); \
        std::cout << "[DEBUG] " << __VA_ARGS__ << std::endl; \
    } while (0)

#define LOG_ERROR(...) \
    do { \
        std::lock_guard<std::mutex> lock(global_log_mutex); \
        std::cerr << "[ERROR] " << __VA_ARGS__ << std::endl; \
    } while (0)

} // namespace minidb