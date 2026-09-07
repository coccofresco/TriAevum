#pragma once

#include <filesystem>

namespace Oot3dNativeGame {
// Set once, before the first ABI query. The module stays loaded for process life.
void ConfigureTitlePlugin(const std::filesystem::path& path);
}
