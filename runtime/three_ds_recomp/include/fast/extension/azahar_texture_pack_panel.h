#pragma once

#include "fast/extension/graphics_settings.h"

#include <string>

namespace Fast::Extension {

struct AzaharTexturePackPanelState {
    std::string Status;
    std::string LoadDirectoryInput;
    std::string DumpDirectoryInput;
    std::string BoundLoadDirectory;
    std::string BoundDumpDirectory;
    bool DirectoriesInitialized = false;
};

bool DrawAzaharTexturePackPanel(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state);

} // namespace Fast::Extension
