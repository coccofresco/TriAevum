#include "fast/oot3d/azahar_texture_pack_panel.h"

#include "oot3d/renderer/azahar_texture_pack.h"

#include <imgui.h>

namespace Fast::Oot3d {
namespace {

int ResizeStringInput(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) {
        return 0;
    }
    auto& value = *static_cast<std::string*>(data->UserData);
    value.resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = value.data();
    return 0;
}

bool DrawDirectoryInput(const char* label, std::string& value) {
    if (value.capacity() < 256U) {
        value.reserve(256U);
    }
    return ImGui::InputTextWithHint(label, "Azahar default", value.data(), value.capacity() + 1U,
                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackResize,
                                    ResizeStringInput, &value);
}

void SynchronizeDirectoryInputs(const AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    if (state.DirectoriesInitialized && state.BoundLoadDirectory == settings.LoadDirectory &&
        state.BoundDumpDirectory == settings.DumpDirectory) {
        return;
    }
    state.LoadDirectoryInput = settings.LoadDirectory;
    state.DumpDirectoryInput = settings.DumpDirectory;
    state.BoundLoadDirectory = settings.LoadDirectory;
    state.BoundDumpDirectory = settings.DumpDirectory;
    state.DirectoriesInitialized = true;
}

} // namespace

bool DrawAzaharTexturePackPanel(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    SynchronizeDirectoryInputs(settings, state);
    bool changed = false;
    changed |= ImGui::Checkbox("Load custom textures", &settings.LoadCustomTextures);
    changed |= ImGui::Checkbox("Dump native textures", &settings.DumpTextures);

    ImGui::SeparatorText("Folders");
    bool applyDirectories = DrawDirectoryInput("Custom texture folder", state.LoadDirectoryInput);
    applyDirectories |= DrawDirectoryInput("Texture dump folder", state.DumpDirectoryInput);
    if (ImGui::Button("Apply folders")) {
        applyDirectories = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Use Azahar defaults")) {
        state.LoadDirectoryInput.clear();
        state.DumpDirectoryInput.clear();
        applyDirectories = true;
    }
    if (applyDirectories) {
        settings.LoadDirectory = state.LoadDirectoryInput;
        settings.DumpDirectory = state.DumpDirectoryInput;
        state.BoundLoadDirectory = settings.LoadDirectory;
        state.BoundDumpDirectory = settings.DumpDirectory;
        changed = true;
    }

    auto& texturePacks = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
    if (ImGui::Button("Reload custom textures")) {
        texturePacks.Reload();
        state.Status = "Custom texture index reloaded";
    }

    const auto status = texturePacks.Snapshot();
    ImGui::SeparatorText("Resolved paths");
    ImGui::TextWrapped("Load: %s", status.LoadDirectory.string().c_str());
    ImGui::TextWrapped("Dump: %s", status.DumpDirectory.string().c_str());
    ImGui::Text(
        "Indexed: %zu  Loaded: %zu  Loading: %zu  Failed: %zu",
        status.IndexedTextures, status.LoadedTextures,
        status.PendingLoads, status.FailedLoads);
    ImGui::Text("Dumped: %zu  Pending dumps: %zu",
                status.DumpedTextures, status.PendingDumps);
    if (status.UnsupportedTextureFiles != 0U) {
        ImGui::TextDisabled("DDS/KTX awaiting compressed GPU upload: %zu", status.UnsupportedTextureFiles);
    }
    if (!status.LastError.empty()) {
        ImGui::TextWrapped("Error: %s", status.LastError.c_str());
    } else if (!state.Status.empty()) {
        ImGui::TextDisabled("%s", state.Status.c_str());
    }
    return changed;
}

} // namespace Fast::Oot3d
