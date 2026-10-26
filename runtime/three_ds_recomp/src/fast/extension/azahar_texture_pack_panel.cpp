#include "fast/extension/azahar_texture_pack_panel.h"

#include "fast/extension/azahar_texture_pack_import.h"
#include "oot3d/renderer/azahar_texture_pack.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <future>
#include <mutex>
#include <thread>

namespace Fast::Extension {

enum class AzaharTextureFolderTarget : uint8_t { LoadFolder, DumpFolder, Import };

struct AzaharTextureFolderOutcome {
    std::optional<std::string> Folder;
    std::string Error;
    std::optional<AzaharTexturePackImportResult> Import;
};

// One Browse.../Import... at a time: the chooser and the copy run on a
// detached worker so neither blocks the frame; the panel polls Result.
struct AzaharTexturePackFolderOperation {
    AzaharTextureFolderTarget Target = AzaharTextureFolderTarget::LoadFolder;
    std::future<AzaharTextureFolderOutcome> Result;
    std::shared_ptr<std::atomic<bool>> Importing = std::make_shared<std::atomic<bool>>(false);
};

namespace {

std::mutex gPickerMutex;
AzaharTextureFolderPicker gPicker;

constexpr const char* kNoManagedFolder = "Start the game from Forge to use the managed texture folder";

int ResizeStringInput(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) {
        return 0;
    }
    auto& value = *static_cast<std::string*>(data->UserData);
    value.resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = value.data();
    return 0;
}

bool DrawDirectoryInput(const char* label, std::string& value, float reserve) {
    if (value.capacity() < 256U) {
        value.reserve(256U);
    }
    // Leaves room for the Browse... button on the same line.
    ImGui::SetNextItemWidth(std::max(120.0F, ImGui::CalcItemWidth() - reserve));
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

// The state's chooser, the process-wide one, then the platform dialog;
// null with *reason when there is none.
AzaharTextureFolderPicker ResolvePicker(const AzaharTexturePackPanelState& state, std::string* reason) {
    if (state.PickFolder) return state.PickFolder;
    {
        std::scoped_lock lock(gPickerMutex);
        if (gPicker) return gPicker;
    }
    if (!NativeFolderDialogAvailable(reason)) return {};
    return [] {
        std::string error;
        auto folder = PickFolderWithNativeDialog("Choose a folder", &error);
        if (!folder && !error.empty()) throw std::runtime_error(error);
        return folder;
    };
}

void StartFolderOperation(AzaharTexturePackPanelState& state, AzaharTextureFolderTarget target,
                          AzaharTextureFolderPicker picker, std::filesystem::path managedFolder, uint64_t titleId) {
    auto operation = std::make_shared<AzaharTexturePackFolderOperation>();
    operation->Target = target;
    std::promise<AzaharTextureFolderOutcome> promise;
    operation->Result = promise.get_future();
    std::thread([promise = std::move(promise), picker = std::move(picker), importing = operation->Importing, target,
                 managedFolder = std::move(managedFolder), titleId]() mutable {
        AzaharTextureFolderOutcome outcome;
        try {
            outcome.Folder = picker();
            if (outcome.Folder && target == AzaharTextureFolderTarget::Import) {
                importing->store(true);
                outcome.Import = ImportAzaharTexturePack(
                    {.Source = PathFromUtf8(*outcome.Folder), .Destination = managedFolder, .InstalledTitleId = titleId});
            }
        } catch (const std::exception& exception) {
            outcome.Error = exception.what();
        }
        promise.set_value(std::move(outcome));
    }).detach();
    state.FolderOperation = std::move(operation);
    state.FolderStatus.clear();
}

void ApplyDirectoryInputs(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    settings.LoadDirectory = state.LoadDirectoryInput;
    settings.DumpDirectory = state.DumpDirectoryInput;
    state.BoundLoadDirectory = settings.LoadDirectory;
    state.BoundDumpDirectory = settings.DumpDirectory;
}

// Takes a finished operation's outcome into the settings; true when they
// changed (the caller applies them through the owner).
bool PollFolderOperation(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    auto& operation = state.FolderOperation;
    if (!operation || operation->Result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    auto outcome = operation->Result.get();
    const auto target = operation->Target;
    operation.reset();
    if (!outcome.Error.empty()) {
        state.FolderStatus = outcome.Error;
        return false;
    }
    if (!outcome.Folder) {
        state.FolderStatus = target == AzaharTextureFolderTarget::Import ? "Import cancelled" : "No folder chosen";
        return false;
    }
    if (target == AzaharTextureFolderTarget::LoadFolder || target == AzaharTextureFolderTarget::DumpFolder) {
        auto& input = target == AzaharTextureFolderTarget::LoadFolder ? state.LoadDirectoryInput : state.DumpDirectoryInput;
        input = *outcome.Folder;
        ApplyDirectoryInputs(settings, state);
        state.FolderStatus = std::string(target == AzaharTextureFolderTarget::LoadFolder ? "Custom texture folder: "
                                                                                          : "Texture dump folder: ") +
                             *outcome.Folder;
        return true;
    }
    const auto& result = *outcome.Import;
    if (!result.Imported) {
        state.FolderStatus = "Import refused: " + result.Message;
        return false;
    }
    // The managed folder now holds the pack: load it from there.
    settings.LoadCustomTextures = true;
    state.LoadDirectoryInput.clear();
    ApplyDirectoryInputs(settings, state);
    ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance().Reload();
    state.FolderStatus = result.Message;
    return true;
}

} // namespace

void SetAzaharTextureFolderPicker(AzaharTextureFolderPicker picker) {
    std::scoped_lock lock(gPickerMutex);
    gPicker = std::move(picker);
}

bool DrawAzaharTexturePackPanel(AzaharTexturePackSettings& settings, AzaharTexturePackPanelState& state) {
    SynchronizeDirectoryInputs(settings, state);
    bool changed = PollFolderOperation(settings, state);
    changed |= ImGui::Checkbox("Load custom textures", &settings.LoadCustomTextures);
    changed |= ImGui::Checkbox("Dump native textures", &settings.DumpTextures);

    auto& texturePacks = ::Oot3d::Renderer::AzaharTexturePackRuntime::Instance();
    std::string pickerUnavailable;
    auto picker = ResolvePicker(state, &pickerUnavailable);
    const bool busy = state.FolderOperation != nullptr;
    const float browseWidth = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    const float reserve = browseWidth + ImGui::GetStyle().ItemSpacing.x;
    const auto browseButton = [&](const char* id, AzaharTextureFolderTarget target) {
        ImGui::SameLine();
        ImGui::BeginDisabled(busy || !picker);
        if (ImGui::Button(id)) StartFolderOperation(state, target, picker, {}, 0);
        ImGui::EndDisabled();
    };

    ImGui::SeparatorText("Folders");
    bool applyDirectories = DrawDirectoryInput("Custom texture folder", state.LoadDirectoryInput, reserve);
    browseButton("Browse...##load", AzaharTextureFolderTarget::LoadFolder);
    applyDirectories |= DrawDirectoryInput("Texture dump folder", state.DumpDirectoryInput, reserve);
    browseButton("Browse...##dump", AzaharTextureFolderTarget::DumpFolder);
    if (!picker) ImGui::TextDisabled("%s", pickerUnavailable.c_str());
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
        ApplyDirectoryInputs(settings, state);
        changed = true;
    }

    // Import... copies a pack into the managed folder Forge's launch names
    // (TEXTURE_IMPORT_RULES.md, shared with Forge > Enhancements).
    ImGui::SeparatorText("Import");
    const auto hostDefaults = texturePacks.HostDefaults();
    const auto status = texturePacks.Snapshot();
    const bool managed = !hostDefaults.LoadDirectory.empty();
    ImGui::BeginDisabled(busy || !picker || !managed);
    if (ImGui::Button("Import...")) {
        StartFolderOperation(state, AzaharTextureFolderTarget::Import, picker, hostDefaults.LoadDirectory,
                             hostDefaults.TitleId.value_or(status.TitleId));
    }
    ImGui::EndDisabled();
    if (!managed) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kNoManagedFolder);
    } else {
        ImGui::SameLine();
        ImGui::TextWrapped("Into %s", PathToUtf8(hostDefaults.LoadDirectory).c_str());
    }
    if (busy) {
        const bool importing = state.FolderOperation->Importing->load();
        ImGui::TextDisabled("%s", importing ? "Importing..." : "Choosing a folder...");
    } else if (!state.FolderStatus.empty()) {
        ImGui::TextWrapped("%s", state.FolderStatus.c_str());
    }

    if (ImGui::Button("Reload custom textures")) {
        texturePacks.Reload();
        state.Status = "Custom texture and material reload requested";
    }

    ImGui::SeparatorText("Resolved paths");
    ImGui::TextWrapped("Load: %s", status.LoadDirectory.string().c_str());
    ImGui::TextWrapped("Dump: %s", status.DumpDirectory.string().c_str());
    ImGui::Text(
        "Indexed: %zu  Loaded: %zu  Loading: %zu  Failed: %zu",
        status.IndexedTextures, status.LoadedTextures,
        status.PendingLoads, status.FailedLoads);
    ImGui::Text("PBR materials: %zu indexed  %zu loaded  %zu failed",
                status.IndexedMaterials, status.LoadedMaterials, status.FailedMaterials);
    if (status.IndexPending) ImGui::TextDisabled("Indexing texture and material files...");
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

} // namespace Fast::Extension
