#pragma once

#include "recovery/log_manager.h"
#include "recovery/log_record.h"

#include <functional>
#include <vector>

namespace minidb {

using namespace std;

class RecoveryManager {
public:
  explicit RecoveryManager(LogManager* lm) : lm_(lm) {}

  void Redo();
  void Undo();

private:
  LogManager* lm_;
};

} // namespace minidb