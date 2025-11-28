#include "FileUtils.h"
#include <fstream>
#include <spdlog/spdlog.h>

namespace Cortex::Utils {

Result<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        return Result<std::vector<uint8_t>>::Err("Failed to open file: " + path.string());
    }

    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);

    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return Result<std::vector<uint8_t>>::Err("Failed to read file: " + path.string());
    }

    return Result<std::vector<uint8_t>>::Ok(std::move(buffer));
}

Result<std::string> ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        return Result<std::string>::Err("Failed to open file: " + path.string());
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    return Result<std::string>::Ok(std::move(content));
}

bool FileExists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

std::string GetFileExtension(const std::filesystem::path& path) {
    return path.extension().string();
}

} // namespace Cortex::Utils
