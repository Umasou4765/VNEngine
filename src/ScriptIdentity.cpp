#include "ScriptIdentity.h"

#include <array>
#include <fstream>

namespace vn {

std::uint64_t ComputeFileContentHash(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return 0;
    }

    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    std::uint64_t hash = kFnvOffsetBasis;
    std::array<char, 4096> buffer{};

    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= kFnvPrime;
        }
    }

    return hash == 0 ? 1 : hash;
}

} // namespace vn
