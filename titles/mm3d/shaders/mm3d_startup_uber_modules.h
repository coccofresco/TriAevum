#pragma once
// MM3D's startup uber fragment modules: title data, read by both the title
// session (which hands it to the renderer) and Forge's pipeline helper, so the
// runtime and the prewarm prepare the same parts.
//
// Data: titles/mm3d/shaders/mm3d_startup_uber_modules.json, embedded when it
// exists at configure time (CMake/oot3d/game.cmake and
// tools/renderer/pipeline_prepare/CMakeLists.txt). Format and key:
// fast/extension/pica_startup_uber_modules.h. Produce it from real sessions
// with TRIAEVUM_RECORD_UBER_MODULES=<file> (the file is directly this data).
// Absent or with no modules: MM3D prepares the whole native-lighting uber
// family at startup, as before the list existed.
#include "mm3d_builtin_shader_packs.h"
#include "fast/extension/pica_startup_uber_modules.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Oot3dNativeGame {
// The listed modules; empty without a list. Malformed data is a build defect
// (the data is embedded) and throws.
[[nodiscard]] inline std::vector<Fast::Extension::PicaUberModuleKey> Mm3dStartupUberModules(
    std::span<const unsigned char> data = Mm3dStartupUberModulesData()) {
    std::vector<Fast::Extension::PicaUberModuleKey> modules;
    if (data.empty()) return modules;
    std::string error;
    if (!Fast::Extension::ParseStartupUberModules(
            std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), modules, &error))
        throw std::runtime_error("MM3D startup uber module list: " + error);
    return modules;
}
} // namespace Oot3dNativeGame
