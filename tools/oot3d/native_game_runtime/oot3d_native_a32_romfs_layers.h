#pragma once

#include "oot3d_native_a32_ctr_host.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace Oot3dNativeGame {

// A read-only window [offset, offset + size) of a host file.
std::shared_ptr<NativeA32CtrFile> OpenNativeA32HostFileRange(const std::filesystem::path& path,
                                                             uint64_t offset, uint64_t size,
                                                             std::string* error = nullptr);

class NativeA32RomFsOverlay;
// Read-only O3RO adapter so a title's required prepared files can coexist
// with user-enabled mods without materializing another full RomFS image.
std::shared_ptr<NativeA32CtrFile> ComposeNativeA32RomFsOverlay(
    std::shared_ptr<NativeA32CtrFile> base,
    std::shared_ptr<const NativeA32RomFsOverlay> overlay);

struct NativeA32RomFsLayers {
    // The merged RomFS level 3, read-only: what the title's RomFS archive
    // serves in place of the mounted image's.
    std::shared_ptr<NativeA32CtrFile> File;
    size_t ReplacedFiles = 0;
    size_t AddedFiles = 0;
    size_t AddedDirectories = 0;
};

// Used by Forge's immutable title UI layer and development LayeredFS.
// Optional user mods use O3RO and can be composed as the base below.
// A RomFS with files laid over the title's own, as LayeredFS does: base is
// the mounted RomFS level 3 (its header at offset 0; IVFC levels 1-2 are not
// served to the title), and each layer is a host directory whose files sit
// at their RomFS paths (layer/a/b.bin is /a/b.bin). A layer file replaces the
// base file at its path or is added there, creating its directories; later
// layers win. The directory and file metadata and their hash tables are
// rebuilt in the RomFS level-3 format; file data is read from the base image
// and the layer files when the title reads it, never copied. Fails (nullopt,
// error set) on a malformed base or an unreadable layer.
std::optional<NativeA32RomFsLayers> BuildNativeA32RomFsLayers(
    std::shared_ptr<NativeA32CtrFile> base, std::span<const std::filesystem::path> layers,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
