#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "Result.h"

namespace Cortex::Utils {

// Read entire file into memory
Result<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path);

// Read text file
Result<std::string> ReadTextFile(const std::filesystem::path& path);

// Check if file exists
bool FileExists(const std::filesystem::path& path);

// Get file extension
std::string GetFileExtension(const std::filesystem::path& path);

} // namespace Cortex::Utils
