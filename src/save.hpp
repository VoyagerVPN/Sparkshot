#pragma once
#include <filesystem>
#include <cstdint>
#include <vector>
namespace sparkshot {
std::filesystem::path SaveScreenshot(const std::filesystem::path& directory, const std::vector<std::uint8_t>& png);
}
