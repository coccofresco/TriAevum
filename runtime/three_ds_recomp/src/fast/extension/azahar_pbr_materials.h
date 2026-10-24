#pragma once

#include "oot3d/renderer/azahar_texture_pack.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Oot3d::Renderer {


[[nodiscard]] std::optional<uint64_t> ParseAzaharTextureFilenameHash(const std::filesystem::path& path);
[[nodiscard]] bool IsAzaharPbrSidecar(const std::filesystem::path& path);

// Runs on the existing pack worker, using the same sorted file inventory as
// color replacements. No decoder, thread, GPU ownership or hot-path I/O here.
[[nodiscard]] std::unordered_map<uint64_t, IndexedAzaharPbrMaterial> IndexAzaharPbrMaterials(
    const std::filesystem::path& root, std::span<const std::filesystem::path> files,
    const std::unordered_map<std::string, std::vector<uint64_t>>& mappedHashes);

} // namespace Oot3d::Renderer
