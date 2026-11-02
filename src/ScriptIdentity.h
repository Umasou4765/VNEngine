#pragma once

#include <cstdint>
#include <filesystem>

namespace vn {

[[nodiscard]] std::uint64_t ComputeFileContentHash(const std::filesystem::path& path);

} // namespace vn
