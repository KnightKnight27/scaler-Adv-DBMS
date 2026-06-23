#pragma once

#include "storage/buffer.h"
#include "recovery/log_manager.h"
#include <string>
#include <unordered_set>
#include <unordered_map>

class RecoveryManager {
public:
    RecoveryManager(LogManager* log_manager, BufferPoolManager* bpm);
    ~RecoveryManager() = default;

    // Executes the ARIES recovery process: Analysis, Redo, Undo
    void Recover(const std::string& log_file_name);

private:
    LogManager* log_manager_;
    BufferPoolManager* bpm_;
};
