#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace minidb {

class SSTable {
 public:
  static void Write(
      const std::filesystem::path &path,
      const std::map<int, std::optional<std::string>> &entries);
  static std::map<int, std::optional<std::string>> Read(
      const std::filesystem::path &path);
};

}  // namespace minidb
