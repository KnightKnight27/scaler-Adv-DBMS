#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace minidb {

class Trace {
 public:
  static void SetEnabled(bool enabled) { Enabled() = enabled; }
  static bool IsEnabled() { return Enabled(); }
  static void Log(std::string_view component, std::string_view message) {
    if (!Enabled()) return;
    std::scoped_lock lock(Mutex());
    std::cout << '[' << component << "] " << message << '\n';
  }

 private:
  static bool &Enabled() {
    static bool enabled = false;
    return enabled;
  }
  static std::mutex &Mutex() {
    static std::mutex mutex;
    return mutex;
  }
};

}  // namespace minidb
