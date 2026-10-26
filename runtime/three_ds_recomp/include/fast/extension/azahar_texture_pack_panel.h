#pragma once

#include "fast/extension/graphics_settings.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Fast::Extension {

// Chooses a folder; returns it as UTF-8, or nothing when cancelled. It runs
// on a worker thread, never in frame code.
using AzaharTextureFolderPicker = std::function<std::optional<std::string>()>;

// A Browse.../Import... in flight (azahar_texture_pack_panel.cpp).
struct AzaharTexturePackFolderOperation;

struct AzaharTexturePackPanelState {
    std::string Status;
    std::string LoadDirectoryInput;
    std::string DumpDirectoryInput;
    std::string BoundLoadDirectory;
    std::string BoundDumpDirectory;
    bool DirectoriesInitialized = false;
    // UX plan WP-07: the folder chooser for Browse... and Import...; null
    // uses SetAzaharTextureFolderPicker's, then the platform dialog.
    AzaharTextureFolderPicker PickFolder;
    // The last Browse.../Import... outcome ("Imported N textures into ...",
    // the refusal reason), separate from the settings Status.
    std::string FolderStatus;
    std::shared_ptr<AzaharTexturePackFolderOperation> FolderOperation;
};

// Draws F1 > Materials > Texture packs. Returns true when settings changed;
// the caller applies them through GraphicsSettingsRuntime (the owner).
bool DrawAzaharTexturePackPanel(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state);

// Process-wide folder chooser for panels whose state names none; null
// restores the platform dialog. The unattended F1 smoke installs one so it
// never opens a native dialog.
void SetAzaharTextureFolderPicker(AzaharTextureFolderPicker picker);

} // namespace Fast::Extension
