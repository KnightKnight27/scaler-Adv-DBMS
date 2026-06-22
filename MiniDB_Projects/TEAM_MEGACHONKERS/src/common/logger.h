#pragma once

#include <iostream>
#include <string>

namespace minidb {

#define LOG_INFO(...) \
    do { \
        std::cout << "[INFO] " << __VA_ARGS__ << std::endl; \
    } while (0)

#define LOG_DEBUG(...) \
    do { \
        std::cout << "[DEBUG] " << __VA_ARGS__ << std::endl; \
    } while (0)

#define LOG_ERROR(...) \
    do { \
        std::cerr << "[ERROR] " << __VA_ARGS__ << std::endl; \
    } while (0)

} // namespace minidb