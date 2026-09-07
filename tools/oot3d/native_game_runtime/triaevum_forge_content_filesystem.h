#pragma once

#include "triaevum/rooted_filesystem_backend.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

std::unique_ptr<triaevum::module::RootedFilesystemBackendV1>
CreateTriAevumForgeContentFilesystem(
    const std::filesystem::path& contentIndexPath,
    const std::filesystem::path& saveRootOverride = {},
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
