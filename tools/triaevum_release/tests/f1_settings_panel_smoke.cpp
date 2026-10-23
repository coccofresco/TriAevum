#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include "oot3d_native_controls_settings_panel.h"
#include "oot3d_top_screen_settings_panel.h"
#include "oot3d_game_language_panel.h"
#include "oot3d_savestate_panel.h"
#include "oot3d_scene_environment.h"
#include "oot3d_scene_environment_codec.h"
#include "fast/oot3d/world_debug_lines.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/MouseCapturePolicy.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <numbers>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace Fast::Oot3d;
namespace {
struct Item { std::string Label; ImRect Rect; ImGuiID Seed = 0; bool Disabled = false; bool Popup = false; };
struct RawBindingSource final : ThreeDsRecomp::Input::HostButtonSource {
    ThreeDsRecomp::Input::HostBinding Held;
    bool IsKeyboardKeyHeld(ThreeDsRecomp::Input::KeyboardKey key) const noexcept override { return key == Held.KeyboardPrimary; }
    bool IsMouseButtonHeld(ThreeDsRecomp::Input::MouseButton button) const noexcept override { return button == Held.Mouse; }
    bool IsGamepadButtonHeld(ThreeDsRecomp::Input::GamepadButton button) const noexcept override { return button == Held.Gamepad; }
};
std::map<ImGuiID, Item> items;
int assertions = 0;
void Check(bool condition, const std::string& what) {
    ++assertions;
    if (!condition) throw std::runtime_error(what);
}
struct MemoryStore final : GraphicsSettingsPersistencePort {
    nlohmann::json Root;
    int Stores = 0;
    bool Fail = false;
    bool LoadRoot(nlohmann::json& root) override { root = Root; return true; }
    bool StoreGraphics(const nlohmann::json& value) override {
        ++Stores;
        if (Fail) return false;
        Root["Graphics"] = value;
        return true;
    }
    int PresetStores = 0;
    bool FailPresets = false;
    bool StoreGraphicsPresets(const nlohmann::json& value) override {
        ++PresetStores;
        if (FailPresets) return false;
        Root["GraphicsPresets"] = value;
        return true;
    }
};
GraphicsSettingsPanel panel;
std::string loggedPanelText;
bool updateTextureObservations = false;
bool showPanel = true;
constexpr uint64_t firstTextureHash = 0xA100U;
constexpr uint64_t secondTextureHash = 0xB200U;
ImVec2 size(760.0F, 680.0F);
void Frame(bool logText = false) {
    if (updateTextureObservations) {
        auto& catalog = TextureCatalogRuntime::Instance();
        catalog.Observe(firstTextureHash, 0x1000U, 32, 32, 0);
        catalog.Observe(secondTextureHash, 0x2000U, 64, 64, 0);
    }
    items.clear();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(std::max(1280.0F, size.x + 16.0F),
                            std::max(720.0F, size.y + 16.0F));
    io.DeltaTime = 1.0F / 60.0F;
    ImGui::NewFrame();
    if (showPanel) {
        ImGui::SetNextWindowPos(ImVec2(8, 8));
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("F1 test", nullptr, ImGuiWindowFlags_NoSavedSettings);
        if (logText) ImGui::LogToBuffer(0);
        panel.Draw();
        if (logText) {
            loggedPanelText = GImGui->LogBuffer.c_str();
            ImGui::LogFinish();
        }
        ImGui::End();
    } else {
        EndGraphicsSettingsPreviews(); // Same close boundary as the host F1 window.
    }
    DrawDisplayConfirmation();
    ImGui::Render();
    Check(GImGui->DisabledStackSize == 0, "unbalanced disabled scope");
    Check(GImGui->ColorStack.Size == 0 && GImGui->StyleVarStack.Size == 0,
          "unbalanced style scope");
    for (auto* list : ImGui::GetDrawData()->CmdLists) {
        for (const auto& v : list->VtxBuffer)
            if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y))
                throw std::runtime_error("non-finite UI geometry");
    }
}
Item Find(const char* label) {
    // ColorEdit3 publishes its actual ##X/##Y/##Z drag widgets beneath
    // PushID(label), rather than a labelled parent item. Resolve that scope.
    const std::string_view path(label);
    if (const auto slash = path.find("/##"); slash != std::string_view::npos) {
        const auto parent = path.substr(0, slash);
        const auto child = path.substr(slash + 1);
        for (const auto& [id, item] : items) {
            const auto parentId = ImHashStr(parent.data(), parent.size(), item.Seed);
            const auto childId = ImHashStr(child.data(), child.size(), parentId);
            const auto found = items.find(childId);
            if (found != items.end() && found->second.Rect.GetHeight() > 0.0F) return found->second;
        }
    }
    std::optional<Item> found;
    for (const auto& [id, item] : items)
        if ((item.Label == label || ImHashStr(label, 0, item.Seed) == id) &&
            item.Rect.GetHeight() > 0.0F) {
            if (item.Popup) return item;
            found = item;
        }
    if (found) return *found;
    std::string available;
    for (const auto& [id, item] : items) available += item.Label + " | ";
    throw std::runtime_error(std::string("Missing UI item: ") + label + " (available: " + available + ")");
}
void Click(const char* label) {
    Frame();
    const auto item = Find(label);
    Check(!item.Disabled, std::string("disabled UI item: ") + label);
    const ImVec2 p(item.Rect.Min.x + std::min(12.0F, item.Rect.GetWidth() / 2.0F),
                   (item.Rect.Min.y + item.Rect.Max.y) / 2.0F);
    const auto bounds = item.Popup ? ImGui::GetIO().DisplaySize : ImVec2(size.x + 8.0F, size.y + 8.0F);
    Check(p.y < bounds.y && p.x < bounds.x, std::string("clipped UI action: ") + label);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame();
    Frame();
}
void Select(const char* combo, const char* option) {
    std::cout << "Select " << combo << " -> " << option << std::endl;
    const auto beforeClick = Find(combo);
    Click(combo);
    if (GImGui->OpenPopupStack.empty())
        std::cerr << "target " << beforeClick.Rect.Min.x << "," << beforeClick.Rect.Min.y
                  << " -> " << beforeClick.Rect.Max.x << "," << beforeClick.Rect.Max.y
                  << "; hovered " << (GImGui->HoveredWindow ? GImGui->HoveredWindow->Name : "none")
                  << "; active id " << GImGui->ActiveId << "\n";
    Check(!GImGui->OpenPopupStack.empty(), std::string("combo did not open: ") + combo);
    Click(option);
}
void EditScalar(const char* label, const char* value) {
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    Click(label);
    Check(GImGui->TempInputId != 0, std::string("scalar input did not open: ") + label);
    io.AddKeyEvent(ImGuiKey_A, true); Frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
    io.AddInputCharactersUTF8(value); Frame();
    io.AddKeyEvent(ImGuiKey_Enter, true); Frame();
    io.AddKeyEvent(ImGuiKey_Enter, false); Frame(); Frame();
}
void EditText(const char* label, const char* value) {
    Click(label);
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true); Frame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false); Frame();
    io.AddKeyEvent(ImGuiKey_Backspace, true); Frame();
    io.AddKeyEvent(ImGuiKey_Backspace, false); Frame();
    io.AddInputCharactersUTF8(value); Frame(); Frame();
}
void ClickTexture(uint64_t hash, bool reorderDuringClick = false) {
    std::cout << "Select texture " << std::hex << hash << std::dec << std::endl;
    Frame();
    char hashText[20]{};
    std::snprintf(hashText, sizeof(hashText), "%016llX", static_cast<unsigned long long>(hash));
    std::optional<Item> target;
    for (const auto& [id, item] : items)
        if (item.Popup && item.Label.find(hashText) != std::string::npos)
            target = item;
    if (!target) {
        std::string visible;
        for (const auto& [id, item] : items) if (item.Popup) visible += item.Label + " | ";
        throw std::runtime_error(std::string("Texture ") + hashText + " absent from open catalog: " + visible);
    }
    const ImVec2 p(target->Rect.Min.x + 12.0F, target->Rect.GetCenter().y);
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(p.x, p.y);
    Frame();
    if (reorderDuringClick) {
        // Move the second texture to the top of the live ranking while the
        // pointer is already over its row in the open popup.
        for (int observation = 0; observation < 1000; ++observation)
            TextureCatalogRuntime::Instance().Observe(secondTextureHash, 0x2000U, 64, 64, 0);
    }
    io.AddMouseButtonEvent(0, true);
    Frame();
    io.AddMouseButtonEvent(0, false);
    Frame(); Frame();
    Check(GImGui->OpenPopupStack.empty(), "texture selection lost while observations changed");
}
void AllCapabilities(bool available) {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    for (unsigned value = 0; value <= static_cast<unsigned>(kLastGraphicsCapability); ++value)
        runtime.SetCapability(static_cast<GraphicsCapability>(value), available, "test capability");
}

void CheckControlPersistence() {
    using namespace Oot3dNativeGame;
    const auto root = std::filesystem::temp_directory_path() /
        ("triaevum-controls-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(root), "could not create isolated config fixture");
    struct Cleanup { std::filesystem::path Root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Root, ec); } } cleanup{root};
    auto controls = std::make_shared<NativeControlConfigRuntime>(root / "controls.json", NativeControlDefaults());
    auto top = std::make_shared<TopScreenUiConfigRuntime>(root / "topscreen.json", TopScreenUiConfig{});
    InstallGraphicsSettingsPanelTabs({CreateNativeControlsSettingsPanel(controls, top)});
    size = ImVec2(760, 680);
    Click("Controls");
    Click("Devices");
    Click("Keyboard");
    Click("Revert changes");
    Check(controls->Snapshot().Config.KeyboardEnabled, "cannot undo before first save");
    Click("Keyboard");
    Click("Camera");
    // The previous panel may have saved this section collapsed in ImGui.
    Frame();
    bool freeCameraVisible = false;
    for (const auto& [id, item] : items) freeCameraVisible |= item.Label == "Enabled##freecam";
    if (!freeCameraVisible) Click("Free camera");
    Click("Enabled##freecam");
    Click("Save controls");
    NativeControlConfig saved;
    TopScreenUiConfig savedTop;
    Check(LoadNativeControlConfig(root / "controls.json", &saved) && !saved.KeyboardEnabled,
          "Save controls did not persist live input");
    Check(LoadTopScreenUiConfig(root / "topscreen.json", &savedTop) && savedTop.FreeCameraEnabled,
          "Save controls did not persist camera settings");
    Click("Enabled##freecam");
    auto externalHud = top->Snapshot().Config;
    externalHud.HudScale = 0.65F;
    externalHud.HudMarginX = 8;
    top->Preview(externalHud);
    Click("Devices");
    Click("Keyboard");
    Click("Revert changes");
    Check(controls->Snapshot().Config == saved, "Revert did not restore saved bindings");
    Check(top->Snapshot().Config.FreeCameraEnabled && top->Snapshot().Config.HudScale == 0.65F &&
          top->Snapshot().Config.HudMarginX == 8, "Revert overwrote unrelated live HUD configuration");
    Click("Camera");
    Click("Enabled##freecam");
    Click("Devices");
    Click("Keyboard");
    const auto live = controls->Snapshot().Config;
    const auto liveTop = top->Snapshot().Config;
    std::ofstream(root / "topscreen.json") << "invalid json";
    Click("Revert changes");
    Check(controls->Snapshot().Config == live && top->Snapshot().Config == liveTop,
          "failed Revert partially modified live configuration");
    Frame(true);
    Check(loggedPanelText.find("parse") != std::string::npos, "Revert error was not visible");
    std::filesystem::create_directory(root / "not-a-file");
    auto blocked = std::make_shared<NativeControlConfigRuntime>(root / "not-a-file", NativeControlDefaults());
    InstallGraphicsSettingsPanelTabs({CreateNativeControlsSettingsPanel(blocked, nullptr)});
    Frame(); Frame();
    Click("Save controls");
    Frame(true);
    Check(loggedPanelText.find("cannot") != std::string::npos, "Save error was not visible");
    InstallGraphicsSettingsPanelTabs({});
}

// Save states tab: the four hotkey actions as buttons, the folder browser
// with its default, the listing that picks the state F8 loads, and status.
void CheckSavestatePanel() {
    using namespace Oot3dNativeGame;
    const auto root = std::filesystem::temp_directory_path() /
        ("triaevum-savestates-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto defaultFolder = root / "exe" / "savestates";
    auto library = std::make_shared<SavestateLibrary>(root / "config" / "savestates.json", defaultFolder);
    InstallGraphicsSettingsPanelTabs({CreateSavestatePanel(library)});
    Frame(); Frame();
    Click("Game"); Click("Save states"); Frame();
    Check(Find("Load last state (F8)").Disabled && Find("Load last state + settings (Shift+F8)").Disabled,
          "load buttons enabled without any state");
    const auto expectRequest = [&](const char* button, SavestateRequestKind kind, bool withSettings) {
        Click(button);
        const auto request = library->TakeRequest();
        Check(request && request->Kind == kind && request->WithSettings == withSettings &&
                  request->Path.empty(),
              std::string("wrong request from ") + button);
    };
    expectRequest("Save state (F5)", SavestateRequestKind::Save, false);
    expectRequest("Save state + settings (Shift+F5)", SavestateRequestKind::Save, true);

    std::filesystem::create_directories(defaultFolder);
    const auto older = defaultFolder / "state-older.oot3dsav";
    const auto newer = defaultFolder / "state-newer+settings.oot3dsav";
    { std::ofstream(older, std::ios::binary) << "1"; }
    { std::ofstream(newer, std::ios::binary) << "22"; }
    std::filesystem::last_write_time(newer, std::filesystem::last_write_time(older) + std::chrono::seconds(5));
    Click("Refresh");
    Frame(true);
    Check(loggedPanelText.find("2 states") != std::string::npos, "folder listing missing");
    Click("state-older.oot3dsav");
    Check(library->LastState() == older, "selecting a listed state did not make it the last state");
    expectRequest("Load last state (F8)", SavestateRequestKind::Load, false);
    expectRequest("Load last state + settings (Shift+F8)", SavestateRequestKind::Load, true);

    // The folder browser starts at the current folder; Up then Use this folder
    // selects its parent, and the default can be restored.
    Click("Browse...##SavestateFolder");
    Click("Up");
    Click("Use this folder");
    Check(library->Directory() == defaultFolder.parent_path() && !library->UsingDefaultDirectory(),
          "folder browser did not choose the folder");
    Click("Use default folder");
    Check(library->UsingDefaultDirectory() && library->Directory() == defaultFolder,
          "default folder was not restored");
    // The file browser picks any state as the one F8 loads.
    Click("Choose a state file...");
    Click("state-newer+settings.oot3dsav");
    Check(library->LastState() == newer, "file browser did not pick the state");

    library->Report(false, "Load failed: this state was saved with 60 Hz gameplay");
    Frame(true);
    Check(loggedPanelText.find("60 Hz gameplay") != std::string::npos, "failure status not visible");
    SavestateLibrary reopened(root / "config" / "savestates.json", defaultFolder);
    Check(reopened.LastState() == newer, "last state was not remembered");
    InstallGraphicsSettingsPanelTabs({});
    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

// The profile codec shared by the runtime and the environment tool.
void CheckSceneEnvironmentCodec() {
    using namespace Oot3dNativeGame;
    auto profile = BaseSceneEnvironmentProfile();
    profile.AmbientIntensity = 0.3F;
    PlacedLight high;
    high.Id = 0xFFFFFFFFU;
    high.Name = "High id";
    high.Type = PlacedLightType::Spot;
    high.Intensity = 0.6F;
    high.Spread = 30.0F;
    profile.PlacedLights.push_back(high);
    const auto document = EncodeSceneEnvironment(profile, 0x55, true);
    const auto text = FormatSceneEnvironment(document);
    Check(text.find("\"ambientIntensity\": 0.3,") != std::string::npos && text.back() == '\n',
          "canonical profiles do not write numbers in their shortest form");
    const auto decoded = DecodeSceneEnvironment(nlohmann::json::parse(text), 0x55, true);
    Check(decoded.Profile && *decoded.Profile == profile && decoded.Errors.empty(),
          "a canonical profile does not decode to the same profile");
    Check(!DecodeSceneEnvironment(nlohmann::json::parse(text), 0x55, false).Profile,
          "a night profile decodes as a day profile");
    // The whole 32-bit id: night and day, scenes and lights stay distinct.
    Check(PlacedLightIdentity(0x55, false, 0xFFFFFFFFU) != PlacedLightIdentity(0x55, true, 0xFFFFFFFFU) &&
              PlacedLightIdentity(0x55, false, 0x80000001U) != PlacedLightIdentity(0x55, true, 1U) &&
              PlacedLightIdentity(0x55, false, 1U) != PlacedLightIdentity(0x56, false, 1U),
          "placed light identities collide across time of day, scene or high ids");
    // A light saved before the spread existed is a diffuse surface.
    auto older = document;
    older["placedLights"][0].erase("spread");
    const auto olderDecoded = DecodeSceneEnvironment(older, 0x55, true);
    Check(olderDecoded.Profile && olderDecoded.Profile->PlacedLights[0].Spread == 180.0F,
          "a placed light without a spread is not a diffuse surface");
    auto broken = document;
    broken["placedLights"][0]["id"] = 2.5;
    broken["placedLights"][0]["intensity"] = 30;
    broken["placedLights"][0]["spread"] = 0;
    broken["directionalLights"][1]["color"] = nlohmann::json::array({1, 2});
    broken.erase("fogStrength");
    broken["placedLights"].push_back(document["placedLights"][0]);
    broken["placedLights"].push_back(document["placedLights"][0]);
    const auto errors = DecodeSceneEnvironment(broken, 0x55, true).Errors;
    const auto has = [&](const char* pointer) {
        return std::any_of(errors.begin(), errors.end(), [&](const auto& error) { return error.Pointer == pointer; });
    };
    Check(has("/placedLights/0/id") && has("/placedLights/0/intensity") && has("/placedLights/0/spread") &&
              has("/directionalLights/1/color") &&
              has("/fogStrength") && has("/placedLights/2/id"),
          "profile errors are not all reported at their JSON pointers");
    const auto explained = ExplainSceneEnvironment(profile, 0x55, true);
    Check(explained.at("renderer").at("placedLights").size() == 1U &&
              explained.at("renderer").at("placedLights")[0].at("id") == 0xFFFFFFFFU &&
              explained.at("dependsOnScene").size() == 1U,
          "explain does not show the renderer's placed lights or the scene-following key light");
}

void CheckSceneEnvironment() {
    using namespace Oot3dNativeGame;
    using Fast::Renderer3ds::PicaPlacedLightKind;
    const auto root = std::filesystem::temp_directory_path() /
        ("triaevum-environment-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path Root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Root, ec); } } cleanup{root};
    auto& graphics = GraphicsSettingsRuntime::Instance();
    AllCapabilities(true);
    auto settings = graphics.Snapshot();
    settings.Preset = GraphicsPreset::Custom;
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
    graphics.Apply(settings);
    auto environment = std::make_shared<SceneEnvironmentRuntime>(root, [&](const auto& value) {
        graphics.PublishSceneEnvironment(value);
    });
    environment->ObserveScene(0x55, {true, 0x8000, false});
    // A scene without its own profile takes the base environment.
    Check(environment->Snapshot().Profile == BaseSceneEnvironmentProfile() &&
              !environment->Snapshot().FileExists && graphics.SnapshotForRendering().Environment.Enabled &&
              graphics.SnapshotForRendering().Environment.NativeAmbientStrength == 0.6F,
          "a scene without a profile did not take the base environment");
    InstallGraphicsSettingsPanelTabs({CreateSceneEnvironmentPanel(environment)});
    size = ImVec2(760, 2600);
    // The scene lights and the shadows share Lighting > Lights & Shadows.
    Frame(); Frame(); Click("Lighting"); Click("Lights & Shadows");
    Frame(true);
    Check(loggedPanelText.find("Scene lights") != std::string::npos &&
              loggedPanelText.find("Profile source: Base") != std::string::npos &&
              loggedPanelText.find("Directional shadows") != std::string::npos,
          "Lights & Shadows does not show the scene lights, the base environment and the shadows");
    for (const char* old : {"Shadows", "Scene environment", "Occlusion"})
        Check(std::none_of(items.begin(), items.end(), [&](const auto& pair) { return pair.second.Label == old; }),
              std::string("former Lighting section still present: ") + old);
    Select("Shadow mode", "Virtual Shadow Maps");
    EditScalar("Shadow strength", "4");
    Check(graphics.Snapshot().Effects.DirectionalShadows.Strength == 4.0F,
          "shadow strength above one did not apply live");
    EditScalar("Shadow strength", "0");
    Check(graphics.Snapshot().Effects.DirectionalShadows.Strength == 0.0F,
          "zero shadow strength did not apply live");
    graphics.SetCapability(GraphicsCapability::NriDirectionalShadows, false, "test unavailable");
    Frame();
    Select("Shadow mode", "Off");
    Check(graphics.Snapshot().Effects.DirectionalShadows.Mode == DirectionalShadowMode::Off,
          "cannot disable unsupported shadows");
    Click("Shadow mode");
    Check(Find("Virtual Shadow Maps").Disabled, "unsupported shadow mode selectable");
    Click("Off");
    AllCapabilities(true);
    Select("Shadow mode", "Virtual Shadow Maps");
    // Lights & Shadows exists only with the Modern model.
    const auto lightsAndShadowsOffered = [] {
        Frame();
        return std::any_of(items.begin(), items.end(),
                           [](const auto& pair) { return pair.second.Label == "Lights & Shadows"; });
    };
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::Native;
    graphics.Apply(settings);
    Frame();
    Check(graphics.Snapshot().Effects.DirectionalShadows.Mode == DirectionalShadowMode::VirtualShadowMaps &&
          graphics.SnapshotForRendering().Value.Effects.DirectionalShadows.Mode == DirectionalShadowMode::Off,
          "native lighting did not bypass VSM while preserving its preference");
    Check(!lightsAndShadowsOffered(), "Lights & Shadows offered with native lighting");
    settings.Effects.SurfaceLighting = SurfaceLightingMode::NativePbr;
    graphics.Apply(settings);
    Check(!lightsAndShadowsOffered() &&
              graphics.SnapshotForRendering().Value.Effects.DirectionalShadows.Mode == DirectionalShadowMode::Off &&
              graphics.Snapshot().Effects.DirectionalShadows.Mode == DirectionalShadowMode::VirtualShadowMaps,
          "Native PBR offered Lights & Shadows, enabled VSM or erased its saved preference");
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixel;
    graphics.Apply(settings);
    Check(lightsAndShadowsOffered(), "Lights & Shadows not offered with the Modern model");
    Click("Lights & Shadows");
    Select("Shadow caster faces", "Native");
    Check(environment->Snapshot().Profile.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::Native,
          "native caster faces combo did not apply");
    Select("Shadow caster faces", "Both faces");
    Check(graphics.SnapshotForRendering().Environment.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::BothFaces,
          "both faces combo did not publish");
    Select("Shadow caster faces", "Interior sun / offline");
    Check(graphics.SnapshotForRendering().Environment.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::InteriorShells,
          "interior shells combo did not publish");
    {
        const auto enabled = environment->Snapshot().Profile;
        auto disabled = enabled;
        disabled.Enabled = false;
        environment->Preview(disabled, environment->Snapshot().Revision);
        Check(!graphics.SnapshotForRendering().Environment.Enabled &&
                  graphics.SnapshotForRendering().Environment.CasterFaces == Fast::Renderer3ds::ShadowCasterFaces::InteriorShells,
              "disabled light override erased the independent shell policy");
        environment->Preview(enabled, environment->Snapshot().Revision);
    }
    Select("Shadow mode", "Off");
    Select("Shadow mode", "Virtual Shadow Maps");
    Check(graphics.SnapshotForRendering().Value.Effects.DirectionalShadows.Mode == DirectionalShadowMode::VirtualShadowMaps,
          "per-pixel lighting did not enable VSM");
    graphics.ToggleNativePresentationOverride();
    Check(graphics.SnapshotForRendering().Value.Effects.DirectionalShadows.Mode == DirectionalShadowMode::Off &&
          graphics.Snapshot().Effects.DirectionalShadows.Mode == DirectionalShadowMode::VirtualShadowMaps,
          "F2 did not bypass VSM without erasing its preference");
    Check(graphics.SnapshotForRendering().Value.Effects.SurfaceLighting == SurfaceLightingMode::Native &&
              graphics.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::PerPixelPbr,
          "F2 did not force Native lighting while preserving the configured model");
    graphics.ToggleNativePresentationOverride();
    Check(graphics.SnapshotForRendering().Value.Effects.SurfaceLighting == SurfaceLightingMode::PerPixelPbr,
          "F2 did not restore Modern lighting");
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
    graphics.Apply(settings);
    Check(graphics.SnapshotForRendering().Value.Effects.DirectionalShadows.Mode == DirectionalShadowMode::VirtualShadowMaps,
          "per-pixel PBR lighting did not restore VSM");
    Select("Point shadow mode", "Cube maps");
    Check(graphics.SnapshotForRendering().Value.Effects.PointShadows.Mode == PointShadowMode::CubeMaps,
          "point shadows did not enable live");
    Select("Point shadow face resolution", "1024 (24 MiB per light)");
    EditScalar("Shadowed point lights", "6");
    EditScalar("Point shadow reach", "50");
    EditScalar("Point shadow strength", "0.25");
    {
        const auto points = graphics.Snapshot().Effects.PointShadows;
        Check(points.Resolution == 1024U && points.MaxLights == 6U && std::abs(points.Range - 0.5F) < 1.0e-5F &&
                  std::abs(points.Strength - 0.25F) < 1.0e-5F,
              "point shadow controls did not apply live");
    }
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::Native;
    graphics.Apply(settings);
    Frame();
    Check(graphics.Snapshot().Effects.PointShadows.Mode == PointShadowMode::CubeMaps &&
              graphics.SnapshotForRendering().Value.Effects.PointShadows.Mode == PointShadowMode::Off,
          "native lighting did not bypass point shadows while preserving their preference");
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
    graphics.Apply(settings);
    Frame();
    Click("Lights & Shadows");
    Select("Point shadow mode", "Off");
    Check(graphics.Snapshot().Effects.PointShadows.Mode == PointShadowMode::Off, "cannot disable point shadows");
    // The base environment is on, without the scene brightness reference.
    Check(Find("Use scene environment").Rect.GetHeight() > 0.0F && !Find("Native ambient contribution").Disabled,
          "the base environment is not shown as an enabled profile");
    Click("Preserve scene brightness");
    Check(Find("Native ambient contribution").Disabled, "scene reference did not own the ambient baseline");
    Click("Preserve scene brightness");
    EditScalar("Ambient intensity", "0.12");
    // Rim is a scene-profile property: real widgets, no second graphics owner.
    Check(environment->Snapshot().Profile.Rim.Enabled && !Find("Rim intensity").Disabled,
          "base actor rim is not enabled");
    EditScalar("Rim intensity", "0.45");
    EditScalar("Rim sharpness", "6");
    EditScalar("Rim azimuth", "90");
    EditScalar("Rim coverage", "0.6");
    EditScalar("Rim color/##X", "0.8");
    EditScalar("Rim color/##Y", "0.7");
    EditScalar("Rim color/##Z", "0.6");
    Check(environment->Snapshot().Profile.Rim.Color[0] == 0.8F, "rim color widget did not apply");
    Select("Rim source", "Scene lights");
    Check(Find("Rim color/##X").Disabled && Find("Rim azimuth").Disabled && Find("Rim coverage").Disabled &&
              !Find("Rim intensity").Disabled &&
              graphics.SnapshotForRendering().Environment.Rim.Source == Fast::Renderer3ds::PicaRimLightSource::SceneLights,
          "scene source did not disable independent controls or publish");
    Select("Rim source", "Independent");
    Check(!Find("Rim color/##X").Disabled && !Find("Rim azimuth").Disabled && !Find("Rim coverage").Disabled,
          "independent source did not restore its controls");
    Select("Rim scope", "Actors and world");
    Check(graphics.SnapshotForRendering().Environment.Rim.Intensity == 0.45F &&
              graphics.SnapshotForRendering().Environment.Rim.Sharpness == 6.0F &&
              graphics.SnapshotForRendering().Environment.Rim.Azimuth == 90 &&
              graphics.SnapshotForRendering().Environment.Rim.Coverage == 0.6F &&
              graphics.SnapshotForRendering().Environment.Rim.Color[0] == 0.8F &&
              graphics.SnapshotForRendering().Environment.Rim.Source == Fast::Renderer3ds::PicaRimLightSource::Independent &&
              graphics.SnapshotForRendering().Environment.Rim.Scope == Fast::Renderer3ds::PicaRimLightScope::ActorsAndWorld,
          "rim widgets did not reach the rendering snapshot");
    Select("Rim scope", "Actors");
    Click("Enable rim light");
    Check(Find("Rim intensity").Disabled && Find("Rim sharpness").Disabled && Find("Rim source").Disabled &&
              Find("Rim color/##X").Disabled && Find("Rim azimuth").Disabled && Find("Rim coverage").Disabled &&
              Find("Rim scope").Disabled && !graphics.SnapshotForRendering().Environment.Rim.Enabled,
          "rim Off did not disable its controls and rendering");
    const auto rimOff = graphics.SnapshotForRendering().Environment;
    auto rimEdit = environment->Snapshot().Profile;
    rimEdit.Rim.Intensity = 0.6F;
    rimEdit.Rim.Source = Fast::Renderer3ds::PicaRimLightSource::SceneLights;
    rimEdit.Rim.Azimuth = -45;
    rimEdit.Rim.Color = {0.1F, 0.2F, 0.3F};
    rimEdit.Rim.Coverage = 1;
    Check(environment->Preview(rimEdit) && graphics.SnapshotForRendering().Environment == rimOff,
          "inactive rim edits changed renderer state");
    Click("Enable rim light");
    Select("Rim source", "Independent");
    EditScalar("Rim azimuth", "90");
    EditScalar("Rim coverage", "0.6");
    EditScalar("Rim color/##X", "0.8");
    EditScalar("Rim color/##Y", "0.7");
    EditScalar("Rim color/##Z", "0.6");
    EditScalar("Rim intensity", "8");
    Check(graphics.SnapshotForRendering().Environment.Rim.Intensity == 8, "rim intensity lost 0..8 range");
    EditScalar("Rim intensity", "0.45");
    EditScalar("Native scene direct contribution", "0.25");
    EditScalar("Native local contribution", "0.7");
    EditScalar("Native ambient contribution", "0.15");
    EditScalar("Native fog contribution", "0.4");
    EditScalar("Light intensity", "1.4");
    // The base key light follows the main scene light; edit its own direction.
    Click("Key light direction"); Click("Manual");
    EditScalar("Azimuth", "90");
    EditScalar("Elevation", "30");
    const auto rendered = graphics.SnapshotForRendering().Environment;
    Check(rendered.Enabled && rendered.AmbientIntensity == 0.12F &&
          rendered.NativeDirectStrength == 0.25F && rendered.NativeLocalStrength == 0.7F &&
          rendered.NativeAmbientStrength == 0.15F &&
          rendered.FogStrength == 0.4F && rendered.Lights[0].Intensity == 1.4F,
          "scene controls did not reach the live rendering snapshot");
    Check(std::abs(rendered.Lights[0].DirectionTowardSource[0] - std::sqrt(0.75F)) < 0.0001F &&
          std::abs(rendered.Lights[0].DirectionTowardSource[1] - 0.5F) < 0.0001F &&
          std::abs(rendered.Lights[0].DirectionTowardSource[2]) < 0.0001F,
          "scene light direction does not follow the displayed world axes");
    using KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection;
    Click("Key light direction"); Click("Ambient light orientation");
    Check(graphics.SnapshotForRendering().Environment.KeyDirection == KeyDirection::SceneAmbient,
          "key light did not follow the ambient light orientation");
    Click("Key light direction"); Click("Main scene light");
    EditScalar("Azimuth offset", "30");
    EditScalar("Elevation offset", "-10");
    EditScalar("Fallback elevation", "30");
    {
        const auto keyed = graphics.SnapshotForRendering().Environment;
        const float degree = std::numbers::pi_v<float> / 180.0F;
        Check(keyed.KeyDirection == KeyDirection::SceneMain &&
                  std::abs(keyed.KeyAzimuthOffset - 30.0F * degree) < 0.0001F &&
                  std::abs(keyed.KeyElevationOffset + 10.0F * degree) < 0.0001F &&
                  keyed.WorldUp == std::array{0.0F, 1.0F, 0.0F} &&
                  keyed.WorldAzimuthZero == std::array{0.0F, 0.0F, 1.0F} &&
                  std::abs(keyed.Lights[0].DirectionTowardSource[1] - 0.5F) < 0.0001F,
              "key light offsets, axes or fallback did not reach the rendering snapshot");
    }
    Click("Fill light"); EditScalar("Light intensity", "0.3");
    EditScalar("Navi intensity", "2.5");
    EditScalar("Fairy radius scale", "1.75");
    Click("Fairy point lights");
    Click("Preserve scene brightness");
    Check(graphics.SnapshotForRendering().Environment.PreserveSceneBrightness &&
              Find("Native ambient contribution").Disabled,
          "scene brightness reference did not apply independently of native source gains");
    Click(environment->Snapshot().EditingNight ? "Save night" : "Save day");
    const auto saved = environment->Snapshot();
    Check(saved.FileExists && !saved.Dirty && saved.Error.empty(), "scene environment was not saved");
    Check(saved.Profile.Rim.Enabled && saved.Profile.Rim.Intensity == 0.45F && saved.Profile.Rim.Sharpness == 6.0F &&
              saved.Profile.Rim.Azimuth == 90 && saved.Profile.Rim.Coverage == 0.6F &&
              saved.Profile.Rim.Color == (std::array<float, 3>{0.8F, 0.7F, 0.6F}) &&
              saved.Profile.Rim.Source == Fast::Renderer3ds::PicaRimLightSource::Independent && saved.Profile.Rim.Scope == Fast::Renderer3ds::PicaRimLightScope::Actors,
          "rim settings were not saved with the scene profile");
    Check(saved.Profile.KeyDirection == KeyDirection::SceneMain && saved.Profile.KeyAzimuthOffset == 30.0F &&
              saved.Profile.KeyElevationOffset == -10.0F,
          "the saved profile lost the key light direction");
    Click("Use native environment");
    Check(!graphics.SnapshotForRendering().Environment.Enabled, "native environment restore was not immediate");
    Check(!graphics.SnapshotForRendering().Environment.Rim.Enabled, "disabled environment kept its rim");
    Check(graphics.SnapshotForRendering().Environment.PointLights[0].Intensity == 2.5F &&
              !graphics.SnapshotForRendering().Environment.PointLights[1].Enabled,
          "using native environment erased independent scene point-light controls");
    const auto inactiveEnvironment = graphics.SnapshotForRendering().Environment;
    auto inactiveEdit = environment->Snapshot().Profile;
    inactiveEdit.AmbientIntensity = 1.7F;
    inactiveEdit.Lights[0].Azimuth = -75.0F;
    environment->Preview(inactiveEdit);
    Check(graphics.SnapshotForRendering().Environment == inactiveEnvironment,
          "disabled profile edits changed rendering inputs and invalidated history");
    Click(environment->Snapshot().EditingNight ? "Revert night" : "Revert day");
    Check(environment->Snapshot().Profile == saved.Profile, "scene profile reload lost authored settings");
    settings = graphics.Snapshot();
    settings.Effects.SurfaceLighting = SurfaceLightingMode::NativePbr;
    graphics.Apply(settings);
    const auto nativePbrEnvironment = graphics.SnapshotForRendering().Environment;
    Check(!nativePbrEnvironment.Enabled && environment->Snapshot().Profile.Enabled,
          "Native PBR applied the scene environment or erased its profile");
    Check(!nativePbrEnvironment.Rim.Enabled, "Native PBR consumed modern scene rim settings");
    auto nativePbrEdit = saved.Profile;
    nativePbrEdit.NativeDirectStrength = 0;
    nativePbrEdit.NativeLocalStrength = 0;
    nativePbrEdit.PointLights[0].Enabled = !nativePbrEdit.PointLights[0].Enabled;
    environment->Preview(nativePbrEdit);
    Check(graphics.SnapshotForRendering().Environment == nativePbrEnvironment,
          "Native PBR consumed scene or fairy override edits");
    environment->Preview(saved.Profile);
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
    graphics.Apply(settings);
    graphics.ToggleNativePresentationOverride();
    Check(!graphics.SnapshotForRendering().Environment.Enabled && environment->Snapshot().Profile.Enabled,
          "F2 did not bypass scene relighting without erasing its profile");
    Check(!graphics.SnapshotForRendering().Environment.Rim.Enabled && environment->Snapshot().Profile.Rim.Enabled,
          "F2 did not bypass rim while retaining its preference");
    graphics.ToggleNativePresentationOverride();
    Check(graphics.SnapshotForRendering().Environment.Enabled, "F2 did not restore the authored environment");
    environment->ObserveScene(0x56, {true, 0x8000, false});
    Check(environment->Snapshot().Profile == BaseSceneEnvironmentProfile() &&
              graphics.SnapshotForRendering().Environment.AmbientIntensity == 0.0F,
          "previous scene's environment leaked across a transition instead of the base");
    environment->ObserveScene(0x55, {true, 0x8000, false});
    Check(environment->Snapshot().Profile == saved.Profile, "returning to scene did not restore its file");
    // Both real editor tabs are available at any game time. Editing does not
    // change the preview selection; the indicator reports the applied blend.
    environment->ObserveScene(0x55, {true, 0, true});
    Frame();
    Check(environment->Snapshot().Night && !environment->Snapshot().EditingNight &&
              environment->Snapshot().Profile == saved.Profile,
          "night without a night profile did not keep the day profile");
    const auto dayScope = graphics.SnapshotForRendering().Environment.ScopeIdentity;
    Click("Create night (from day)");
    const auto nightPath = root / "scene-0055.night.environment.json";
    Check(std::filesystem::exists(nightPath) && environment->Snapshot().EditingNight &&
              environment->Snapshot().Path == nightPath &&
              graphics.SnapshotForRendering().Environment.ScopeIdentity == dayScope,
          "creating night did not select its draft or changed the continuous scope");
    EditScalar("Ambient intensity", "0.4");
    Click("Save night");
    const auto savedNight = environment->Snapshot().Profile;
    nlohmann::json nightDocument;
    { std::ifstream file(nightPath); file >> nightDocument; }
    Check(nightDocument.value("timeOfDay", "") == "night" && nightDocument.at("ambientIntensity").get<float>() == 0.4F,
          "night was not saved to its own file");
    environment->ObserveScene(0x55, {true, 0x8000, false});
    Frame();
    Check(environment->Snapshot().EditingNight && environment->Snapshot().Profile == savedNight &&
              graphics.SnapshotForRendering().Environment.AmbientIntensity == saved.Profile.AmbientIntensity,
          "day time changed the editor or failed to use day lighting");
    EditScalar("Ambient intensity", "0.8");
    Select("Preview", "Force night");
    Check(graphics.SnapshotForRendering().Environment.AmbientIntensity == 0.8F, "force night did not preview the night draft by day");
    Click("Day");
    Check(!environment->Snapshot().EditingNight && environment->Snapshot().Profile == saved.Profile,
          "Day tab did not restore its own draft");
    EditScalar("Ambient intensity", "1.1");
    environment->ObserveScene(0x55, {true, 0, true});
    Frame();
    Check(!environment->Snapshot().EditingNight && environment->Snapshot().DirtyTabs == (std::array<bool,2>{true,true}),
          "native time change discarded a draft or selected a different tab");
    Select("Preview", "Force day");
    Frame(true);
    Check(graphics.SnapshotForRendering().Environment.AmbientIntensity == 1.1F &&
              loggedPanelText.find("day 100% / night 0%") != std::string::npos,
          "force day/indicator does not show the actual applied blend");
    Select("Preview", "Auto");
    environment->SetForcedNightWeight(0.5F);
    Frame(true);
    Check(loggedPanelText.find("day 50% / night 50%") != std::string::npos &&
              std::abs(graphics.SnapshotForRendering().Environment.AmbientIntensity - 0.95F) < 1e-6F,
          "half-weight indicator disagrees with the applied profiles");
    environment->SetForcedNightWeight(std::nullopt);
    Select("Preview", "Force day");
    ToggleGraphicsNativePresentation(); // Actual host F2 action, no game/window.
    Check(environment->Snapshot().PreviewMode == SceneEnvironmentPreview::Auto, "F2 did not clear forced preview");
    ToggleGraphicsNativePresentation();
    Frame();
    Select("Preview", "Force day");
    showPanel = false; Frame();
    Check(environment->Snapshot().PreviewMode == SceneEnvironmentPreview::Auto &&
              graphics.SnapshotForRendering().Environment.AmbientIntensity == 0.8F,
          "closing F1 did not restore Auto");
    showPanel = true; Frame(); Frame();
    Click("Night");
    Click("Revert night");
    Check(environment->Snapshot().Profile == savedNight && environment->Snapshot().DirtyTabs[0] &&
              !environment->Snapshot().DirtyTabs[1], "revert night discarded the day draft");
    Click("Copy day -> night"); Click("Cancel");
    Check(environment->Snapshot().Profile == savedNight, "cancel copy changed night");
    Click("Copy day -> night"); Click("Copy");
    Check(environment->Snapshot().EditingNight && environment->Snapshot().Dirty &&
              environment->Snapshot().Profile.AmbientIntensity == 1.1F, "copy did not populate the unsaved night draft");
    { std::ifstream file(nightPath); file >> nightDocument; }
    Check(nightDocument.at("ambientIntensity").get<float>() == 0.4F, "copy persisted before Save night");
    Click("Save night");
    Click("Day"); Click("Revert day");
    Check(environment->Snapshot().Profile == saved.Profile, "revert day changed its saved profile");
    Click("Remove night"); Click("Cancel");
    Check(std::filesystem::exists(nightPath), "cancel remove deleted the night file");
    Click("Remove night"); Click("Remove");
    Check(!std::filesystem::exists(nightPath) && !environment->Snapshot().EditingNight &&
              environment->Snapshot().Profile == saved.Profile && environment->Snapshot().AppliedNightWeight == 0,
          "remove night failed to preserve the day draft");
    environment->ObserveScene(0x56, {false,0,true}); Frame(true);
    for (const char* label : {"Day", "Night", "Preview", "Create night (from day)", "Copy day -> night", "Remove night"})
        Check(std::none_of(items.begin(),items.end(),[&](const auto& item){ return item.second.Label == label; }),
              std::string("fixed scene shows day/night UI: ") + label);
    Check(loggedPanelText.find("Fixed scene lighting") != std::string::npos && !Find("Save profile").Disabled,
          "fixed scene does not show one editable profile");
    environment->ObserveScene(0x55, {true, 0x8000, false}); Frame(); Frame();
    // Group standards use the real widgets and persistence path.
    environment->ObserveScene(0x58,{true,0x8000,false,0,SceneEnvironmentGroup::TimeFlows}); Frame(true); Frame(true);
    Check(loggedPanelText.find("Outdoor, time flows")!=std::string::npos &&
          loggedPanelText.find("Day source: Base")!=std::string::npos,"group/source labels missing");
    const auto groupDay=root/"group-outdoor-time-flows.environment.json";
    const auto groupNight=root/"group-outdoor-time-flows.night.environment.json";
    const auto ownGroupScene=root/"scene-0058.environment.json";
    EditScalar("Ambient intensity","0.37");
    Click("Save as group standard"); Click("Cancel");
    Check(!std::filesystem::exists(groupDay),"cancel persisted group standard");
    Click("Save as group standard"); Click("Save standard");
    Check(std::filesystem::exists(groupDay) && !std::filesystem::exists(ownGroupScene) &&
          environment->Snapshot().Sources[0]==SceneEnvironmentSource::Group && !environment->Snapshot().Dirty,
          "save standard did not commit fallback draft");
    EditScalar("Ambient intensity","0.62"); Click("Save day");
    Check(std::filesystem::exists(ownGroupScene) && environment->Snapshot().Sources[0]==SceneEnvironmentSource::Scene,
          "editing fallback did not create scene own profile");
    Click("Use group standard"); Click("Cancel");
    Check(std::filesystem::exists(ownGroupScene),"cancel removed scene profile");
    Click("Use group standard"); Click("Use standard");
    Check(!std::filesystem::exists(ownGroupScene) && std::abs(environment->Snapshot().Profile.AmbientIntensity-.37F)<1e-6F,
          "use group standard did not discard own values");
    auto groupValue=environment->Snapshot().Profile; groupValue.AmbientIntensity=.73F;
    { std::ofstream file(groupNight); file<<FormatSceneEnvironment(EncodeGroupEnvironment(groupValue,SceneEnvironmentGroup::TimeFlows,true)); }
    environment->ObserveScene(0x51,{true,0,true,0,SceneEnvironmentGroup::TimeFlows}); Frame(); Frame();
    Check(std::abs(graphics.SnapshotForRendering().Environment.AmbientIntensity-.73F)<1e-6F,
          "another group scene did not inherit night standard");
    Click("Night"); Frame(true);
    Check(loggedPanelText.find("Night source: Group standard")!=std::string::npos,"night provenance missing");
    EditScalar("Ambient intensity","0.81");
    Click("Save as group standard"); Click("Save standard");
    Check(!std::filesystem::exists(root/"scene-0051.night.environment.json") && !environment->Snapshot().Dirty,
          "night group save wrote a scene file");
    // Confirmation cannot apply to a scene/tab that changed while it was open.
    Click("Use group standard");
    environment->ObserveScene(0x58,{true,0x8000,false,0,SceneEnvironmentGroup::TimeFlows}); Frame();
    Check(Find("Use standard").Disabled,"stale group confirmation remained enabled"); Click("Cancel");
    // Errors remain visible in the same control surface.
    std::filesystem::remove(groupDay); std::filesystem::create_directory(groupDay);
    Click("Save as group standard"); Click("Save standard"); Frame(true);
    Check(!environment->Snapshot().Error.empty() && loggedPanelText.find("Environment error:")!=std::string::npos,
          "group persistence failure hidden");
    std::filesystem::remove(groupDay);
    environment->ObserveScene(0x55,{true,0x8000,false}); Frame(); Frame();
    // Placed lights: added where Link is, edited by type, saved in the profile.
    environment->ObserveScene(0x55, {true, 0x8000, false}, ScenePlayerPlacement{{100.0F, 0.0F, 200.0F}, 90.0F});
    Click("Add light at Link");
    {
        const auto placed = environment->Snapshot().Profile.PlacedLights;
        const auto& rendered = graphics.SnapshotForRendering().Environment;
        Check(placed.size() == 1U && placed[0].Position == std::array<float, 3>{100.0F, 50.0F, 200.0F} &&
                  placed[0].Rotation[0] == 90.0F && placed[0].Type == PlacedLightType::Point &&
                  rendered.PlacedLightCount == 1U && rendered.PlacedLights[0].Reach == 300.0F &&
                  rendered.PlacedLights[0].Kind == PicaPlacedLightKind::Point &&
                  rendered.PlacedLights[0].PositionWorld == placed[0].Position && !rendered.PlacedLights[0].CastShadows,
              "a light added at Link was not placed there or not drawn");
        // Yaw 90 faces +X; tilted 30 degrees down.
        const auto axis = rendered.PlacedLights[0].AxisWorld;
        Check(std::abs(axis[0] - 0.8660254F) < 1.0e-4F && std::abs(axis[1] + 0.5F) < 1.0e-4F &&
                  std::abs(axis[2]) < 1.0e-4F,
              "the placed light's axis does not follow its yaw and pitch");
    }
    Select("Light type", "Spot");
    Check(environment->Snapshot().Profile.PlacedLights[0].Type == PlacedLightType::Spot &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].Kind == PicaPlacedLightKind::Spot &&
              std::abs(graphics.SnapshotForRendering().Environment.PlacedLights[0].CosOuter - 0.9238795F) < 1.0e-4F &&
              Find("Beam angle").Rect.GetHeight() > 0.0F,
          "a spot light is not drawn as a spot or its beam controls are missing");
    Select("Light type", "Surface");
    Check(Find("Surface width").Rect.GetHeight() > 0.0F && Find("Emit from both sides").Rect.GetHeight() > 0.0F &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].Kind == PicaPlacedLightKind::Rectangle &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].HalfWidth == 50.0F,
          "the surface light controls are missing or it is not drawn as a rectangle");
    Check(graphics.SnapshotForRendering().Environment.PlacedLights[0].Spread == 180.0F,
          "a new surface light is not diffuse");
    EditScalar("Surface spread", "40");
    Check(environment->Snapshot().Profile.PlacedLights[0].Spread == 40.0F &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].Spread == 40.0F,
          "the surface spread control does not reach the rendered light");
    Select("Surface shape", "Disc");
    Check(graphics.SnapshotForRendering().Environment.PlacedLights[0].Kind == PicaPlacedLightKind::Disc &&
              Find("Surface radius").Rect.GetHeight() > 0.0F,
          "a disc surface is not drawn as a disc");
    Select("Light type", "Sun");
    Check(Find("Cast shadows").Disabled &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].Kind == PicaPlacedLightKind::Sun,
          "a sun light offers shadows or is not drawn as a sun");
    Select("Light type", "Omni");
    Click("Cast shadows");
    Check(graphics.SnapshotForRendering().Environment.PlacedLightCount == 1U &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].Kind == PicaPlacedLightKind::Omni &&
              graphics.SnapshotForRendering().Environment.PlacedLights[0].CastShadows,
          "an omni light did not cast shadows");
    // Light gizmos: published while the editor is drawn, none once turned off.
    {
        using Fast::Oot3d::WorldDebugLineRuntime;
        environment->PublishLightGizmo(std::array<float, 3>{100.0F, 50.0F, 700.0F});
        const auto lines = WorldDebugLineRuntime::Instance().Latest();
        const auto hasLine = [&](const std::array<float, 3>& from, const std::array<float, 3>& to) {
            return lines != nullptr && std::any_of(lines->Lines.begin(), lines->Lines.end(), [&](const auto& line) {
                       return line.From == from && line.To == to;
                   });
        };
        Check(lines != nullptr && lines->Lines.size() >= 3U * 64U + 3U &&
                  hasLine({100.0F - 20000.0F, 50.0F, 200.0F}, {100.0F + 20000.0F, 50.0F, 200.0F}) &&
                  hasLine({100.0F, 50.0F - 20000.0F, 200.0F}, {100.0F, 50.0F + 20000.0F, 200.0F}),
              "the placed light's gizmo (its reach and move axes) was not published");
        Click("Show light gizmos");
        environment->PublishLightGizmo(std::array<float, 3>{100.0F, 50.0F, 700.0F});
        Check(WorldDebugLineRuntime::Instance().Latest() == nullptr, "light gizmos turned off are still drawn");
        Click("Show light gizmos");
        // A camera at the origin looking down -Z (near 1, far 1000): a line
        // from behind it is cut at the near plane; the physical framebuffer
        // takes the logical Y as X and the logical X reversed as Y.
        std::array<float, 16> clip{};
        clip[0] = 1.0F;
        clip[5] = 1.0F;
        clip[10] = 1000.0F / (1.0F - 1000.0F);
        clip[11] = -1.0F;
        clip[14] = 1000.0F / (1.0F - 1000.0F);
        Fast::Oot3d::WorldDebugLineSet set;
        set.Lines.push_back({{1.0F, 2.0F, 10.0F}, {1.0F, 2.0F, -10.0F}, {1.0F, 1.0F, 1.0F, 1.0F}});
        set.Lines.push_back({{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}});
        const auto vertices = Fast::Oot3d::BuildWorldDebugLineVertices(set, clip);
        Check(vertices.size() == 6U && std::abs(vertices[0].ClipFrom[2] / vertices[0].ClipFrom[3]) < 1.0e-3F &&
                  std::abs(vertices[0].ClipTo[0] / vertices[0].ClipTo[3] - 0.2F) < 1.0e-5F &&
                  std::abs(vertices[0].ClipTo[1] / vertices[0].ClipTo[3] + 0.1F) < 1.0e-5F,
              "world debug lines are not cut at the near plane or not turned into the framebuffer");
    }
    Click(environment->Snapshot().EditingNight ? "Save night" : "Save day");
    {
        nlohmann::json document;
        { std::ifstream file(saved.Path); file >> document; }
        Check(document.at("placedLights").size() == 1U && document["placedLights"][0].at("type") == "omni" &&
                  document["placedLights"][0].at("castShadows") == true,
              "the placed light was not saved in the profile");
    }
    Click("Duplicate light");
    Check(environment->Snapshot().Profile.PlacedLights.size() == 2U &&
              environment->Snapshot().Profile.PlacedLights[1].Id != environment->Snapshot().Profile.PlacedLights[0].Id,
          "a duplicated light kept its identity");
    Click("Delete light");
    Click("Delete light");
    Check(environment->Snapshot().Profile.PlacedLights.empty() &&
              graphics.SnapshotForRendering().Environment.PlacedLightCount == 0U,
          "deleted lights are still drawn");
    Click(environment->Snapshot().EditingNight ? "Revert night" : "Revert day");
    Check(environment->Snapshot().Profile.PlacedLights.size() == 1U, "reload lost the saved placed light");
    auto withoutLights = environment->Snapshot().Profile;
    withoutLights.PlacedLights.clear();
    environment->Preview(withoutLights);
    Click(environment->Snapshot().EditingNight ? "Save night" : "Save day");
    const auto backup = root / "saved.json";
    std::filesystem::rename(saved.Path, backup);
    std::filesystem::create_directory(saved.Path);
    Check(!environment->Save() && !environment->Snapshot().Error.empty(), "profile save failure was hidden");
    std::filesystem::remove(saved.Path);
    { std::ofstream file(saved.Path); file << "{invalid"; }
    Check(!environment->Reload() && environment->Snapshot().Profile == saved.Profile,
          "malformed reload replaced the last valid live environment");
    environment->ObserveScene(0x56, {true, 0x8000, false});
    environment->ObserveScene(0x55, {true, 0x8000, false});
    Check(!graphics.SnapshotForRendering().Environment.Enabled && !environment->Snapshot().Error.empty(),
          "invalid destination profile inherited another scene's lights");
    std::filesystem::remove(saved.Path);
    std::filesystem::rename(backup, saved.Path);
    Click(environment->Snapshot().EditingNight ? "Revert night" : "Revert day");
    nlohmann::json originalProfile;
    { std::ifstream file(saved.Path); file >> originalProfile; }
    for (int version : {1, 2, 3}) {
        auto legacy = originalProfile;
        legacy["version"] = version;
        if (version < 3) legacy.erase("nativeLocalStrength");
        legacy.erase("preserveSceneBrightness");
        if (version == 1) legacy.erase("pointLights");
        { std::ofstream file(saved.Path); file << legacy; }
        Click(environment->Snapshot().EditingNight ? "Revert night" : "Revert day");
        const auto migrated = environment->Snapshot();
        Check(migrated.Error.empty() && !migrated.Dirty &&
                  migrated.Profile.NativeDirectStrength == 0.25F &&
                  migrated.Profile.NativeLocalStrength == (version < 3 ? 0.25F : 0.7F) &&
                  !migrated.Profile.PreserveSceneBrightness,
              "legacy profile lost its native direct/local gain");
        Check(migrated.Profile.PointLights[1].Enabled == (version == 1),
              "legacy profile lost point-group defaults or saved Off");
        nlohmann::json unchanged;
        { std::ifstream file(saved.Path); file >> unchanged; }
        Check(unchanged == legacy, "loading a legacy profile rewrote the user's file");
    }
    graphics.SetCapability(GraphicsCapability::NativeSurfaceLighting, false, "test unavailable");
    Frame(); Click("Use native environment");
    Check(!environment->Snapshot().Profile.Enabled, "capability loss prevented the native off control");
    Click("Navi point lights");
    Check(!graphics.SnapshotForRendering().Environment.PointLights[0].Enabled,
          "capability loss prevented disabling Navi point lights");
    environment->ObserveScene(std::nullopt);
    AllCapabilities(true);
}

void CheckMm3dSceneEnvironment() {
    using namespace Oot3dNativeGame;
    const auto root=std::filesystem::temp_directory_path()/
        ("triaevum-mm3d-environment-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup { std::filesystem::path Root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(Root,ec); } } cleanup{root};
    auto& graphics=GraphicsSettingsRuntime::Instance();
    AllCapabilities(true);
    auto settings=graphics.Snapshot();
    settings.Preset=GraphicsPreset::Custom;
    settings.Effects.SurfaceLighting=SurfaceLightingMode::PerPixelPbr;
    graphics.Apply(settings);
    auto environment=std::make_shared<SceneEnvironmentRuntime>(root,[&](const auto& v){
        graphics.PublishSceneEnvironment(v);
    },std::filesystem::path{},NativePreservingSceneEnvironmentProfile());
    // MM3D's time is unknown here: not time-driven, no night profile.
    environment->ObserveScene(0x2A);
    InstallGraphicsSettingsPanelTabs({CreateSceneEnvironmentPanel(environment)});
    size=ImVec2(760,2600);
    Frame();Frame();Click("Lighting");Click("Lights & Shadows");Frame(true);
    Check(!environment->Snapshot().Profile.Enabled && !graphics.SnapshotForRendering().Environment.Enabled,
          "MM3D widget enabled a missing profile");
    Check(loggedPanelText.find("Fixed scene lighting")!=std::string::npos,
          "MM3D widget presented unknown time as time-driven");
    Click("Use scene environment");
    Check(environment->Snapshot().Profile.Enabled,"MM3D profile control did not enable explicit override");
    graphics.SetCapability(GraphicsCapability::NativeSurfaceLighting,false,"test unavailable");
    Frame();
    Check(!Find("Use scene environment").Disabled,"unsupported active environment cannot be disabled");
    Click("Use scene environment");
    Check(!environment->Snapshot().Profile.Enabled,"unsupported MM3D environment did not switch Off");
    Frame();
    Check(Find("Use scene environment").Disabled,"unsupported inactive environment offered On");
    environment->ObserveScene(std::nullopt);
    Frame(true);
    Check(loggedPanelText.find("No native gameplay scene is available")!=std::string::npos &&
          !graphics.SnapshotForRendering().Environment.Enabled,"lost MM3D scene did not deactivate widget/runtime");
    AllCapabilities(true);
    InstallGraphicsSettingsPanelTabs({});
    size=ImVec2(760,1000);
}

void CheckPbrMaterialEditor() {
    using namespace ::Oot3d::Renderer;
    auto& packs = AzaharTexturePackRuntime::Instance();
    auto& catalog = TextureCatalogRuntime::Instance();
    const auto root = std::filesystem::temp_directory_path() /
        ("triaevum-material-editor-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto packRoot = root / "pack";
    const auto inputs = root / "inputs";
    std::filesystem::create_directories(packRoot);
    std::filesystem::create_directory(inputs);
    struct Cleanup {
        std::filesystem::path Root;
        ~Cleanup() {
            auto& runtime = AzaharTexturePackRuntime::Instance();
            runtime.Configure({});
            runtime.WaitForPendingLoads();
            TextureCatalogRuntime::Instance().Clear();
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }
    } cleanup{root};
    const auto image = [&](const char* name, std::array<uint8_t, 4> pixel, uint32_t width = 2) {
        std::vector<uint8_t> pixels(width * 2 * 4);
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
            std::copy(pixel.begin(), pixel.end(), pixels.begin() + offset);
        WriteAzaharTexturePng(inputs / name, width, 2, pixels);
    };
    image("normal.png", {128, 128, 255, 255});
    image("orm.png", {255, 51, 0, 255});
    image("base.png", {201, 127, 51, 255});
    image("emissive.png", {25, 35, 45, 255});
    image("height.png", {64, 64, 64, 255});
    image("occlusion.png", {17, 23, 29, 255});
    image("roughness.png", {41, 63, 47, 255});
    image("metallic.png", {97, 101, 129, 255});
    image("wrong-size.png", {7, 11, 13, 255}, 4);
    {
        std::ofstream other(packRoot / "000000000000DCBA.material.json");
        other << R"({"schema":"triaevum.pbr-material","version":1,"specularLevel":0.25})";
    }
    packs.Configure({.LoadCustomTextures = true, .LoadDirectory = packRoot});
    packs.WaitForPendingLoads();
    catalog.Clear();
    // Catalog/FNV identity deliberately differs from the Azahar/native-byte identity.
    catalog.Observe(0x12345678, 0x1000, 32, 32, 0);
    catalog.AssociateMaterial(0x12345678, 32, 32, 0, 0xABCD, packs.Snapshot().PackUsesNewHash);
    size = ImVec2(1000, 1350);
    Click("Materials");
    Click("Material editor");
    Click("Create from texture");
    Click("Texture catalog selection");
    ClickTexture(0x12345678);
    Click("Create / edit selected texture");
    Click("Material");
    EditScalar("Normal strength", "3.5");
    EditScalar("Glossiness strength", "0.6");
    EditScalar("Minimum roughness", "0.4");
    EditScalar("Dielectric reflectance", "1.5");
    EditScalar("Specular weight", "0.4");
    EditScalar("Screen-space reflection weight", "1.5");
    EditScalar("Height depth (UV)", "0.05");
    Click("UV seam softening");
    Click("Enable UV seam softening");
    EditScalar("Seam strength", "0.75");
    EditScalar("Seam width (% edge)", "12.5");
    Click("Soften normals");
    Click("Soften emission");
    Click("UV seam softening");
    Click("Save material");
    packs.WaitForPendingLoads();
    Frame();
    const auto descriptor = packRoot / "000000000000ABCD.material.json";
    Check(std::filesystem::is_regular_file(descriptor) &&
              !std::filesystem::exists(packRoot / "0000000012345678.material.json"),
          "creation used the catalog hash instead of the native texture's Azahar identity");
    const auto resolved = [&](uint64_t hash) {
        (void)packs.ResolveMaterialOrQueue(hash, packs.Generation());
        packs.WaitForPendingLoads();
        return packs.ResolveMaterialOrQueue(hash, packs.Generation());
    };
    auto material = resolved(0xABCD);
    Check(material.Material && material.Material->Parameters.NormalScale == 3.5F &&
              material.Material->Parameters.GlossinessStrength == 0.6F &&
              material.Material->Parameters.MinimumRoughness == 0.4F &&
              material.Material->Parameters.SpecularLevel == 1.5F &&
              material.Material->Parameters.SpecularWeight == 0.4F &&
              material.Material->Parameters.ScreenSpaceReflectionWeight == 1.5F &&
              material.Material->Parameters.HeightScale == 0.05F,
          "saved editor parameters did not reach the material loader");
    Check(material.Material->Parameters.SeamSoftening &&
              material.Material->Parameters.SeamStrength == 0.75F &&
              material.Material->Parameters.SeamWidth == 0.125F &&
              material.Material->Parameters.SeamChannels == 62U,
          "saved seam controls did not reach the material loader");
    Click("UV seam softening");
    EditScalar("Seam strength", "0");
    Click("Soften height");
    Click("UV seam softening");
    EditScalar("Dielectric reflectance", "0.5");
    EditScalar("Specular weight", "0");
    EditScalar("Screen-space reflection weight", "2");
    material = resolved(0xABCD);
    Check(material.Preview && material.Preview->SpecularLevel == 0.5F &&
              material.Preview->SpecularWeight == 0.0F &&
              material.Preview->ScreenSpaceReflectionWeight == 2.0F &&
              material.Material->Parameters.SpecularWeight == 0.4F &&
              material.Material->Parameters.ScreenSpaceReflectionWeight == 1.5F &&
              material.Material->Parameters.SpecularLevel == 1.5F,
          "selected material preview either failed or wrote unsaved values to disk");
    Check(material.Preview->SeamStrength == 0 && material.Preview->SeamChannels == 30U &&
              material.Material->Parameters.SeamStrength == 0.75F &&
              material.Material->Parameters.SeamChannels == 62U,
          "live seam preview changed persisted channel selection or strength");
    const auto other = resolved(0xDCBA);
    Check(other.Material && !other.Preview && other.Material->Parameters.SpecularLevel == 0.25F &&
              other.Material->Parameters.SpecularWeight == 1.0F &&
              other.Material->Parameters.ScreenSpaceReflectionWeight == 1.0F,
          "editing one material changed another material");
    Select("Existing PBR material", "000000000000DCBA");
    Click("Keep editing");
    Click("Save material");
    packs.WaitForPendingLoads();
    Frame();
    Select("Material view", "Roughness");
    Check(resolved(0xABCD).Preview->DebugView == Fast::Renderer3ds::PicaPbrDebugView::Roughness,
          "selected-material diagnostic preview was not published");
    auto& graphics = GraphicsSettingsRuntime::Instance();
    graphics.ToggleNativePresentationOverride();
    Frame();
    Check(graphics.SnapshotForRendering().Value.Effects.SurfaceLighting == SurfaceLightingMode::Native,
          "F2 did not bypass material shading");
    graphics.ToggleNativePresentationOverride();
    Frame();
    Check(resolved(0xABCD).Preview->SpecularLevel == 0.5F &&
              resolved(0xABCD).Preview->SpecularWeight == 0.0F &&
              resolved(0xABCD).Preview->ScreenSpaceReflectionWeight == 2.0F,
          "F2 lost the authored material preview");
    Select("Material view", "Shaded");
    AllCapabilities(false);
    Frame();
    Check(!Find("Minimum roughness").Disabled, "material authoring disappeared when renderer capabilities were unavailable");
    Click("UV seam softening");
    Check(!Find("Enable UV seam softening").Disabled,
          "seam Off control became inaccessible after capability loss");
    Click("Enable UV seam softening");
    Check(!resolved(0xABCD).Preview->SeamSoftening,
          "seam Off control did not disable the material preview");
    Click("UV seam softening");
    AllCapabilities(true);
    Click("Images");
    Click("Base color");
    Click("Browse image");
    EditText("Folder", inputs.string().c_str());
    Click("Open folder");
    Click("base.png");
    packs.WaitForPendingLoads();
    Frame();
    const auto loadImage = [&](const char* path) {
        EditText("Image path", (inputs / path).string().c_str());
        Click("Load image");
        packs.WaitForPendingLoads();
        Frame(); Frame();
    };
    Click("Normal"); loadImage("normal.png");
    Click("Emissive"); loadImage("emissive.png");
    Click("Height"); loadImage("height.png");
    Click("ORM"); loadImage("orm.png");
    Click("Save material");
    packs.WaitForPendingLoads();
    Frame();
    material = resolved(0xABCD);
    Check(material.Material->ColorReplacement && (*material.Material->ColorReplacement->Rgba8)[0] == 201 &&
              (*material.Material->Textures[0]->Rgba8)[2] == 255 &&
              (*material.Material->Textures[1]->Rgba8)[1] == 51 &&
              (*material.Material->Textures[2]->Rgba8)[2] == 45 &&
              (*material.Material->Textures[3]->Rgba8)[0] == 64,
          "an imported PBR slot did not survive save and loader publication");
    Select("ORM input", "Separate channels");
    Click("Occlusion"); loadImage("occlusion.png"); Select("Source channel", "R");
    Click("Roughness"); loadImage("roughness.png"); Select("Source channel", "G");
    Click("Metallic"); loadImage("metallic.png"); Select("Source channel", "B");
    Click("Preview packed ORM");
    Click("Save material");
    packs.WaitForPendingLoads();
    Frame();
    material = resolved(0xABCD);
    const auto packed = material.Material->Textures[1];
    Check(packed && (*packed->Rgba8)[0] == 17 && (*packed->Rgba8)[1] == 63 &&
              (*packed->Rgba8)[2] == 129 && (*packed->Rgba8)[3] == 255,
          "separate ORM channels were swizzled, gamma-converted, or not exported as one image");
    auto readJson = [&] {
        std::ifstream file(descriptor);
        return nlohmann::json::parse(file);
    };
    const auto saved = readJson();
    Check(std::filesystem::is_regular_file(descriptor.parent_path() / saved["orm"]["texture"].get<std::string>()),
          "saved descriptor does not reference the packed ORM export");
    loadImage("wrong-size.png");
    Click("Save material");
    Check(readJson() == saved, "mismatched ORM dimensions replaced the last valid material");
    loadImage("metallic.png");
    {
        std::ofstream broken(inputs / "broken.png");
        broken << "not an image";
    }
    loadImage("broken.png");
    Click("Save material");
    Check(readJson() == saved, "failed image import corrupted the material descriptor");
    loadImage("metallic.png");
    Click("Material");
    EditScalar("Dielectric reflectance", "0.8");
    std::filesystem::rename(descriptor, descriptor.string() + ".backup");
    std::filesystem::create_directory(descriptor);
    Click("Save material");
    Check(resolved(0xABCD).Preview->SpecularLevel == 0.8F &&
              std::filesystem::is_regular_file(descriptor.string() + ".backup"),
          "failed descriptor write discarded the live draft");
    std::filesystem::remove(descriptor);
    std::filesystem::rename(descriptor.string() + ".backup", descriptor);
    Click("Save material");
    packs.WaitForPendingLoads();
    Frame();
    Check(resolved(0xABCD).Material->Parameters.SpecularLevel == 0.8F,
          "retrying material save did not commit the retained draft");
    Click("Reset material parameters");
    Check(resolved(0xABCD).Preview->SpecularWeight == 1.0F &&
              resolved(0xABCD).Preview->ScreenSpaceReflectionWeight == 1.0F,
          "reset did not restore neutral material reflection weights");
    Check(!resolved(0xABCD).Preview->SeamSoftening,
          "reset left auxiliary seam blending enabled");
    Select("Existing PBR material", "000000000000DCBA");
    Click("Discard and select");
    Check(resolved(0xABCD).Material->Parameters.SpecularLevel == 0.8F && !resolved(0xABCD).Preview,
          "discarding a draft changed the saved material or left its preview active");
    Select("Existing PBR material", "000000000000ABCD");
    packs.WaitForPendingLoads();
    Frame();
    Check(resolved(0xABCD).Preview->MinimumRoughness == 0.4F,
          "selecting an existing material lost its authored roughness floor");
    Click("Lighting");
    Click("Lighting & GI");
    size = ImVec2(760, 1100);
    Frame(); Frame();
}

// A title without scene view or FOV hooks (MM3D): the FOV slider is disabled
// with the title's reason, the panel says which effects stay inactive, and the
// stored FOV is kept.
void CheckTitleWithoutSceneHooks() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    const float storedFov = runtime.Snapshot().FovMultiplier;
    runtime.SetCapability(GraphicsCapability::TitleSceneView, false,
                          "Test title does not publish its scene view yet.");
    runtime.SetCapability(GraphicsCapability::TitleSceneFov, false,
                          "Test title has no scene projection hooks yet.");
    Click("Display");
    Frame(true);
    Check(Find("Global scene FOV").Disabled, "FOV slider enabled without the title's FOV hooks");
    Check(loggedPanelText.find("Test title has no scene projection hooks yet.") != std::string::npos,
          "missing FOV reason");
    Check(loggedPanelText.find("Test title does not publish its scene view yet. Grass, shadows") !=
              std::string::npos,
          "missing inactive-effects notice");
    Check(runtime.Snapshot().FovMultiplier == storedFov, "stored FOV changed");
    runtime.SetCapability(GraphicsCapability::TitleSceneView, true);
    runtime.SetCapability(GraphicsCapability::TitleSceneFov, true);
    Frame(true);
    Check(!Find("Global scene FOV").Disabled, "FOV slider disabled with the title's FOV hooks");
    Check(loggedPanelText.find("Grass, shadows, ambient occlusion") == std::string::npos,
          "inactive-effects notice shown with a scene view");
}

void CheckGameSurfaceMouseResume() {
    Fast::MouseCapturePolicy policy;
    Check(policy.Request(true), "initial mouse capture denied");
    policy.Release();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(100, 100);
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(640, 480));
        ImGui::Begin("Main Game", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground);
        ImGui::End();
        ImGui::Render();
    }
    Check(io.WantCaptureMouse, "game viewport did not reproduce generic ImGui capture");
    Check(!policy.Request(true) && !policy.ResumeClick(true), "a real F1 window allowed recapture");
    Check(policy.ResumeClick(false) && policy.Request(true),
          "game-surface-only ImGui capture blocked resuming gameplay");
    Check(policy.ConsumeClickRelease(), "recapture click would leak a game action");
}
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb,
                               const ImGuiLastItemData*) {
    if (id == 0) return;
    items[id].Rect = bb;
    items[id].Popup = (ctx->CurrentWindow->Flags & ImGuiWindowFlags_Popup) != 0;
    items[id].Seed = ctx->CurrentWindow->IDStack.empty()
        ? ctx->CurrentWindow->ID : ctx->CurrentWindow->IDStack.back();
    items[id].Disabled = (ctx->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags) {
    items[id].Label = label ? label : "";
}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID id) {
    auto it = items.find(id);
    return it == items.end() ? nullptr : it->second.Label.c_str();
}

int main(int argc, char** argv) try {
    PresentationSettingsTransaction delayedConfirmation(500);
    PresentationSettingsValue previousDisplay;
    auto nextDisplay = previousDisplay;
    nextDisplay.Window = WindowMode::Borderless;
    Check(delayedConfirmation.Begin(previousDisplay, nextDisplay), "display transaction did not begin");
    Check(delayedConfirmation.MarkApplied(nextDisplay, 100, true), "display transaction did not apply");
    Check(!delayedConfirmation.Advance(100000), "hidden confirmation expired before it could be seen");
    Check(delayedConfirmation.ConfirmationVisible(100000), "visible confirmation did not start the timer");
    Check(!delayedConfirmation.ConfirmationVisible(100100), "redrawing the confirmation renewed its timer");
    Check(!delayedConfirmation.Advance(100499) && delayedConfirmation.Advance(100500),
          "visible confirmation lost the automatic rollback deadline");
    auto store = std::make_shared<MemoryStore>();
    auto initial = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    store->Root["Graphics"] = SerializeGraphicsSettings(initial);
    InstallGraphicsSettingsPersistencePort(store);
    auto& runtime = GraphicsSettingsRuntime::Instance();
    AllCapabilities(true);
    auto controls = std::make_shared<Oot3dNativeGame::NativeControlConfigRuntime>(
        std::filesystem::path{}, Oot3dNativeGame::NativeControlPreset(
            Oot3dNativeGame::NativeControlProfile::KeyboardMouse));
    auto topScreen = std::make_shared<Oot3dNativeGame::TopScreenUiConfigRuntime>(
        std::filesystem::path{}, Oot3dNativeGame::TopScreenUiConfig{});
    InstallGraphicsSettingsPanelTabs({
        Oot3dNativeGame::CreateNativeControlsSettingsPanel(controls, topScreen),
        Oot3dNativeGame::CreateTopScreenSettingsPanel(topScreen)});
    ImGui::CreateContext();
    GImGui->TestEngineHookItems = true;
    ImGui::GetIO().IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int width, height;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Frame(); Frame();

    if (argc == 2 && std::string_view(argv[1]) == "--grass-masks") {
        auto& bridge = GrassSceneBridge::Instance();
        bridge.Clear();
        GrassSceneMesh source;
        source.GeometryId = source.InstanceId = 1;
        source.DrawOrder = 1;
        source.FrameId = 1;
        source.RenderTargetNamespace = 1;
        source.TextureHash = 0xCC00;
        source.TextureWidth = source.TextureHeight = 32;
        source.Vertices = std::make_shared<const std::vector<GrassSourceVertex>>(
            std::initializer_list<GrassSourceVertex>{ { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 } },
                                                      { { 100, 0, 0 }, { 0, 1, 0 }, { 1, 0 } },
                                                      { { 0, 0, 100 }, { 0, 1, 0 }, { 0, 1 } } });
        source.Indices = std::make_shared<const std::vector<uint32_t>>(std::initializer_list<uint32_t>{ 0, 1, 2 });
        auto overlay = source;
        overlay.GeometryId = overlay.InstanceId = 2;
        overlay.DrawOrder = 2;
        overlay.TextureHash = firstTextureHash;
        overlay.Overlay = std::make_shared<GrassOverlayAlpha>();
        bridge.Publish(source);
        bridge.Publish(overlay);
        (void)bridge.OverlaysFor(source);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (bridge.ObservedMasks().empty() && std::chrono::steady_clock::now() < deadline)
            (void)bridge.OverlaysFor(source);
        TextureCatalogRuntime::Instance().Clear();
        TextureCatalogRuntime::Instance().Observe(firstTextureHash, 0x1000, 32, 32, 0);
        TextureCatalogRuntime::Instance().Observe(secondTextureHash, 0x2000, 64, 64, 0);
        std::vector<uint8_t> rgba(32 * 32 * 4, 128);
        GrassTextureSourceCache::Instance().ObserveDecoded(firstTextureHash, 32, 32, rgba);
        auto settings = runtime.Snapshot();
        settings.Preset = GraphicsPreset::Custom;
        settings.Grass.Masks.clear();
        runtime.Apply(settings);
        Click("Grass");
        Click("Masks");
        Frame(true);
        Check(loggedPanelText.find("Covered source area") != std::string::npos, "mask overlap absent from panel");
        Check(loggedPanelText.find("MASK auto") != std::string::npos, "automatic catalog badge missing");
        Click("Overlay mask");
        ClickTexture(firstTextureHash);
        Check(bridge.MaskHighlight({ 1, 1, 0 }).Lines.empty(), "highlight active without request");
        Click("Highlight mask in scene");
        Check(!bridge.MaskHighlight({ 1, 1, 0 }).Lines.empty(), "selected mask did not highlight");
        Click("Sources");
        Check(bridge.MaskHighlight({ 1, 1, 0 }).Lines.empty(), "tab switch retained highlight");
        Click("Masks");
        ToggleGraphicsNativePresentation();
        Frame();
        Check(bridge.MaskHighlight({ 1, 1, 0 }).Lines.empty(), "F2 retained highlight");
        ToggleGraphicsNativePresentation();
        Frame();
        showPanel = false;
        Frame();
        Check(bridge.MaskHighlight({ 1, 1, 0 }).Lines.empty(), "closed F1 retained highlight");
        showPanel = true;
        Frame();
        Click("Highlight mask in scene");
        AllCapabilities(false);
        Select("Mask mode", "Off");
        Check(runtime.Snapshot().Grass.Masks.size() == 1 &&
                  runtime.Snapshot().Grass.Masks[0].Mode == GrassMaskMode::Off,
              "capability loss prevented mask Off");
        AllCapabilities(true);
        Select("Mask mode", "Custom");
        size.y = 2200;
        Frame();
        Frame();
        Select("Sample channel", "Green");
        Click("Invert mask");
        EditScalar("Mask strength", "0.4");
        EditScalar("Input black", "0.2");
        EditScalar("Input white", "0.8");
        EditScalar("Response exponent", "2");
        EditScalar("Output black", "0.1");
        EditScalar("Output white", "0.7");
        Click("Add rectangle");
        const auto custom = runtime.Snapshot().Grass.Masks.front();
        Check(custom.Mode == GrassMaskMode::Custom && custom.Coverage.Channel == GrassSampleChannel::Green &&
                  custom.Coverage.Invert && custom.Strength == .4F && custom.Coverage.InputBlack == .2F &&
                  custom.Coverage.InputWhite == .8F && custom.Coverage.ResponseExponent == 2 &&
                  custom.Coverage.OutputBlack == .1F && custom.Coverage.OutputWhite == .7F &&
                  custom.Coverage.ActiveRects.size() == 1,
              "shared custom mask controls failed");
        const auto saved = DeserializeGraphicsSettings(store->Root["Graphics"]);
        Check(saved.Value.Grass.Masks.front().Coverage.ActiveRects.size() == 1, "mask controls were not persisted");
        Frame(true);
        Check(loggedPanelText.find("MASK custom") != std::string::npos, "custom catalog badge missing");
        store->Fail = true;
        Select("Mask mode", "Off");
        Frame(true);
        Check(loggedPanelText.find("Settings applied, but could not be saved.") != std::string::npos &&
                  runtime.SaveState() == GraphicsSettingsSaveState::Failed,
              "mask persistence failure not shown");
        Check(runtime.Snapshot().Grass.Masks[0].Mode == GrassMaskMode::Off, "failed save prevented live Off");
        store->Fail = false;
        Click("Retry saving graphics");
        Check(runtime.SaveState() == GraphicsSettingsSaveState::Saved, "mask save retry failed");
        Click("Reset mask to automatic");
        Check(runtime.Snapshot().Grass.Masks.empty(), "reset to automatic retained override");
        Click("Texture catalog selection");
        ClickTexture(secondTextureHash);
        Click("Add selected texture mask");
        Check(runtime.Snapshot().Grass.Masks.size() == 1 &&
                  runtime.Snapshot().Grass.Masks[0].Coverage.Target.Rgba8Hash == secondTextureHash,
              "manual catalog mask used wrong identity");
        Click("Remove mask rule");
        Check(runtime.Snapshot().Grass.Masks.empty(), "remove mask failed");
        size.y = 680;
        Frame();
        Frame();
        bridge.Clear();
        EndGraphicsSettingsPreviews();
        ImGui::DestroyContext();
        std::cout << "F1 grass masks smoke passed: " << assertions << " assertions\n";
        return 0;
    }

    // Run the owning panel independently of unrelated material/controls UI.
    if (argc == 2 && std::string_view(argv[1]) == "--scene-environment") {
        CheckSceneEnvironment();
        ImGui::DestroyContext();
        std::cout << "F1 scene environment smoke passed: " << assertions << " assertions\n";
        return 0;
    }

    // Top-level tabs in their order; the former Renderer/Textures/TopScreen
    // tabs no longer exist.
    {
        float previous = -1.0F;
        for (const char* tab : {"Display", "Lighting", "Grass", "Materials", "Controls", "HUD"}) {
            const auto item = Find(tab);
            Check(item.Rect.Min.x > previous, std::string("top-level tab out of order: ") + tab);
            previous = item.Rect.Min.x;
        }
        for (const char* old : {"Renderer", "Textures", "Environment", "TopScreen 2.1.1"})
            Check(std::none_of(items.begin(), items.end(),
                               [&](const auto& pair) { return pair.second.Label == old; }),
                  std::string("former top-level tab still present: ") + old);
        Click("Lighting");
        // Native lighting: no Lights & Shadows section.
        float sectionX = -1.0F;
        for (const char* section : {"Lighting & GI", "Ambient Occlusion", "Reflections", "Toon"}) {
            const auto item = Find(section);
            Check(item.Rect.Min.x > sectionX, std::string("Lighting section out of order: ") + section);
            sectionX = item.Rect.Min.x;
        }
        for (const char* old : {"Lights & Shadows", "Shadows", "Occlusion", "Scene environment"})
            Check(std::none_of(items.begin(), items.end(),
                               [&](const auto& pair) { return pair.second.Label == old; }),
                  std::string("Lighting section offered with native lighting: ") + old);
        auto modern = runtime.Snapshot();
        modern.Effects.SurfaceLighting = SurfaceLightingMode::PerPixelPbr;
        runtime.Apply(modern);
        // A tab appearing is laid out on its next frame.
        Frame(); Frame();
        sectionX = -1.0F;
        for (const char* section : {"Lighting & GI", "Lights & Shadows", "Ambient Occlusion", "Reflections", "Toon"}) {
            const auto item = Find(section);
            Check(item.Rect.Min.x > sectionX, std::string("Modern Lighting section out of order: ") + section);
            sectionX = item.Rect.Min.x;
        }
        runtime.Apply(initial);
        // The remaining tabs close up over the next frames.
        Frame(); Frame();
        Click("Ambient Occlusion");
        // The credits close the page: show all of it so its text is logged.
        const auto pageSize = size;
        size = ImVec2(760, 2600);
        Frame();
        Frame(true);
        Check(loggedPanelText.find("iMMERSE by Pascal Gilcher") != std::string::npos &&
                  loggedPanelText.find("all rights reserved") != std::string::npos &&
                  loggedPanelText.find("Licence exception") != std::string::npos,
              "Ambient Occlusion lacks the MXAO credit and licence exception");
        size = pageSize;
        Click("Materials");
        for (const char* section : {"Texture packs", "Material editor", "Reflection materials"}) Find(section);
        Click("Display");
    }

    auto requestedDisplay = runtime.Snapshot();
    requestedDisplay.OutputWidth = 3840;
    requestedDisplay.OutputHeight = 2160;
    runtime.Apply(requestedDisplay);
    runtime.AcknowledgePresentationApplied(requestedDisplay);
    runtime.PublishDisplayMetrics({1920, 1080, 960, 540, 0.5F, WindowMode::Windowed});
    Frame(true);
    Check(loggedPanelText.find("Output framebuffer: 1920 x 1080") != std::string::npos &&
          loggedPanelText.find("Scene image: 960 x 540") != std::string::npos,
          "F1 labels the requested resolution as the actual renderer extent");
    Check(runtime.Snapshot().OutputWidth == 3840, "observed extent overwrote the requested resolution");
    runtime.Apply(initial);
    runtime.AcknowledgePresentationApplied(initial);
    Frame();

    Check(!runtime.NativePresentationOverrideActive(), "F2 must start inactive");
    for (const auto toon : {ToonMode::Off, ToonMode::PostProcessPreview, ToonMode::PicaMaterial}) {
        for (const auto reflections : {ReflectionMode::Off, ReflectionMode::HiZ, ReflectionMode::FidelityFxSssr}) {
            auto configured = runtime.Snapshot();
            configured.Preset = GraphicsPreset::Custom;
            configured.Grass.Quality = GrassQuality::Custom;
            configured.Grass.Generation.InstancesPerSquareMeter = 1048.8F;
            configured.Effects.Toon = toon;
            configured.Effects.ToonStyle.OutlineEnabled = true;
            configured.Effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
            configured.Effects.Reflections = reflections;
            configured.FrameRate = FrameRateMode::Interpolated2x;
            configured.FovMultiplier = 1.1F;
            runtime.Apply(configured);
            const auto before = runtime.SnapshotWithRevision();
            const auto serialized = SerializeGraphicsSettings(before.Value);
            const auto storedBefore = store->Root;
            const int savesBefore = store->Stores;
            runtime.ToggleNativePresentationOverride();
            const auto effective = runtime.SnapshotForRendering();
            auto expectedOff = before.Value;
            expectedOff.Grass.Quality = GrassQuality::Off;
            expectedOff.Effects.Toon = ToonMode::Off;
            expectedOff.Effects.ToonStyle.OutlineEnabled = false;
            expectedOff.Effects.AmbientOcclusion = AmbientOcclusionMode::Off;
            expectedOff.Effects.Reflections = ReflectionMode::Off;
            // F2 shows plain native shading, below the Native model's PBR materials.
            expectedOff.Effects.SurfaceLighting = SurfaceLightingMode::Native;
            Check(runtime.NativePresentationOverrideActive() && effective.Revision > before.Revision,
                  "F2 must invalidate frame settings");
            Check(SerializeGraphicsSettings(effective.Value) == SerializeGraphicsSettings(expectedOff),
                  "F2 must disable only the requested effects");
            Check(SerializeGraphicsSettings(runtime.Snapshot()) == serialized,
                  "F2 overwrote configured values");
            Frame(true);
            Check(loggedPanelText.find("F2 override ACTIVE") != std::string::npos,
                  "active F2 status missing from real panel");
            Check(store->Stores == savesBefore && store->Root == storedBefore,
                  "F2 or opening F1 persisted temporary Off values");
            auto edited = runtime.Snapshot();
            edited.Effects.ToonStyle.OutlineWidth = 4.5F;
            edited.Grass.Generation.InstancesPerSquareMeter = 2048.0F;
            runtime.Apply(edited);
            Check(runtime.SnapshotForRendering().Value.Grass.Quality == GrassQuality::Off,
                  "editing configured effects bypassed F2");
            const auto editedJson = SerializeGraphicsSettings(runtime.Snapshot());
            Check(store->Root["Graphics"] == editedJson, "editing under F2 saved effective settings");
            const int savesAfterEdit = store->Stores;
            runtime.ToggleNativePresentationOverride();
            Check(!runtime.NativePresentationOverrideActive() &&
                      SerializeGraphicsSettings(runtime.SnapshotForRendering().Value) == editedJson,
                  "F2 did not restore latest edited settings exactly");
            Check(store->Stores == savesAfterEdit, "F2 restore wrote a profile");
        }
    }
    Frame(true);
    Check(loggedPanelText.find("F2 override inactive") != std::string::npos,
          "inactive F2 status missing from real panel");
    runtime.Apply(initial);
    Frame();

    Select("Graphics preset", "Authentic");
    Check(runtime.Snapshot().Preset == GraphicsPreset::Authentic, "preset identity lost");
    Click("Display");
    Select("Mode", "FXAA");
    Check(runtime.Snapshot().AntiAliasing == AntiAliasingMode::Fxaa, "cannot customize Authentic");
    Check(runtime.Snapshot().Preset == GraphicsPreset::Custom, "manual edits must become Custom");
    {
        auto withPack = runtime.Snapshot();
        withPack.TexturePacks.Azahar.LoadCustomTextures = true;
        withPack.TexturePacks.Azahar.LoadDirectory = "pack";
        runtime.Apply(withPack);
        Frame();
    }
    Select("Graphics preset", "Enhanced");
    Check(runtime.Snapshot().Preset == GraphicsPreset::Enhanced &&
              runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Mxao &&
              runtime.Snapshot().Effects.MxaoNormals == MxaoNormalSource::Geometry &&
              runtime.Snapshot().TexturePacks.Azahar.LoadCustomTextures &&
              runtime.Snapshot().TexturePacks.Azahar.LoadDirectory == "pack",
          "Enhanced is not the captured profile or replaced the texture pack");
    Select("Graphics preset", "Toon");
    const auto beforeCustom = runtime.Snapshot();
    Select("Graphics preset", "Custom");
    const auto expected = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Custom, beforeCustom);
    Check(SerializeGraphicsSettings(runtime.Snapshot()) == SerializeGraphicsSettings(expected),
          "Custom changed user settings");
    Check(runtime.Snapshot().Effects.Toon == ToonMode::PostProcessPreview, "Custom reset effects");
    // User presets: built-in names refused, save, edit, load, overwrite,
    // a failed write reported without loss, delete only after confirmation.
    {
        Click("Save or delete presets");
        Check(Find("Save preset").Disabled, "an empty preset name could be saved");
        Check(Find("Delete preset").Disabled, "Delete offered without a user preset");
        EditText("Preset name", " enhanced ");
        Frame(true);
        Check(Find("Save preset").Disabled &&
                  loggedPanelText.find("Built-in presets cannot be changed: choose") != std::string::npos,
              "a built-in preset name was accepted");
        EditText("Preset name", "Sera");
        Click("Save preset");
        Check(runtime.UserPresetNames() == std::vector<std::string>{"Sera"} &&
                  store->Root["GraphicsPresets"]["Presets"][0]["Name"] == "Sera" &&
                  runtime.ActiveUserPreset() == "Sera",
              "user preset not saved or not selected");
        const auto saved = runtime.Snapshot();
        auto edited = saved;
        edited.FovMultiplier = saved.FovMultiplier == 1.25F ? 1.0F : 1.25F;
        runtime.Apply(edited);
        Frame();
        Check(runtime.ActiveUserPreset().empty(), "an edited preset still shows as loaded");
        Select("Graphics preset", "Sera");
        Check(SerializeGraphicsSettings(runtime.Snapshot()) == SerializeGraphicsSettings(saved) &&
                  runtime.ActiveUserPreset() == "Sera",
              "loading the user preset did not restore it");
        EditText("Preset name", "SERA");
        Frame();
        Find("Overwrite preset");
        store->FailPresets = true;
        EditText("Preset name", "Notte");
        Click("Save preset");
        Frame(true);
        Check(loggedPanelText.find("Not saved") != std::string::npos &&
                  runtime.UserPresetNames() == std::vector<std::string>{"Sera"},
              "a failed preset write was hidden or changed the presets");
        store->FailPresets = false;
        Click("Delete preset");
        Check(runtime.UserPresetNames().size() == 1U, "a preset was deleted without confirmation");
        Click("Cancel");
        Click("Delete preset");
        Click("Confirm delete");
        Check(runtime.UserPresetNames().empty() && store->Root["GraphicsPresets"]["Presets"].empty() &&
                  SerializeGraphicsSettings(runtime.Snapshot()) == SerializeGraphicsSettings(saved),
              "delete did not remove the preset or changed the settings");
        Check(Find("Delete preset").Disabled, "Delete offered after the preset was removed");
        Click("Save or delete presets");
    }
    Click("Lighting");
    Click("Toon");
    auto custom = runtime.Snapshot();
    custom.Effects.ToonStyle.LightBandLevels[1] = 0.22F;
    runtime.Apply(custom);
    Frame();
    Click("Custom light band profile");
    Check(runtime.Snapshot().Effects.ToonStyle.LightBandLevels[1] == 0.22F, "enabling custom bands reset values");
    Click("Custom light band profile");
    Click("Custom light band profile");
    Check(runtime.Snapshot().Effects.ToonStyle.LightBandLevels[1] == 0.22F, "toggling custom bands reset values");
    auto bands = runtime.Snapshot().Effects.ToonStyle;
    ResizeToonLightBandProfile(bands, 6);
    Check(bands.LightBands == 6 && bands.LightBandLevels.front() == 0.0F &&
          bands.LightBandLevels[5] == 1.0F, "band resizing endpoints");
    for (size_t i = 1; i < 5; ++i)
        Check(bands.LightBandThresholds[i] > bands.LightBandThresholds[i-1], "threshold order");

    size.y = 1100.0F;
    Frame(); Frame();
    Click("Custom light band profile");
    Click("Outline");
    if (!runtime.Snapshot().Effects.ToonStyle.OutlineEnabled) Click("Enable depth outline");
    EditScalar("Outline width (1080p)", "9.5");
    EditScalar("Outline softness", "0.25");
    Check(runtime.Snapshot().Effects.ToonStyle.OutlineWidth == 9.5F,
          "outline width not applied");
    Check(runtime.Snapshot().Effects.ToonStyle.OutlineSoftness == 0.25F,
          "outline softness not applied");

    Click("Lighting & GI");
    // Two lighting models: Native (native shading, PBR on supported pack
    // materials) and Modern (per-pixel + PBR).
    Click("Lighting model");
    Check(std::none_of(items.begin(), items.end(), [](const auto& pair) {
              return pair.second.Label == "Per-pixel" || pair.second.Label == "Per-pixel + PBR" ||
                     pair.second.Label == "Native + PBR";
          }), "a removed lighting model is still offered");
    Click("Modern");
    Check(runtime.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::PerPixelPbr,
          "Modern lighting selection not applied");
    Select("Lighting model", "Native");
    Check(runtime.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::NativePbr,
          "Native lighting selection not applied");
    {
        // Saved Per-pixel becomes Modern and plain native shading becomes Native.
        auto legacy = runtime.Snapshot();
        legacy.Effects.SurfaceLighting = SurfaceLightingMode::PerPixel;
        runtime.Apply(legacy);
        Check(runtime.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::PerPixelPbr,
              "Per-pixel was not migrated to Modern");
        legacy.Effects.SurfaceLighting = SurfaceLightingMode::Native;
        runtime.Apply(legacy);
        Check(runtime.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::NativePbr,
              "plain native shading was not migrated to Native");
    }
    const auto persistedNativePbr = DeserializeGraphicsSettings(store->Root["Graphics"]);
    Check(persistedNativePbr.Value.Effects.SurfaceLighting == SurfaceLightingMode::NativePbr,
          "native PBR lighting selection lost on reload");
    runtime.ToggleNativePresentationOverride();
    Check(runtime.SnapshotForRendering().Value.Effects.SurfaceLighting == SurfaceLightingMode::Native,
          "F2 did not bypass native PBR");
    runtime.ToggleNativePresentationOverride();
    Check(runtime.SnapshotForRendering().Value.Effects.SurfaceLighting == SurfaceLightingMode::NativePbr,
          "F2 did not restore native PBR");
    CheckPbrMaterialEditor();
    runtime.SetCapability(GraphicsCapability::NativeSurfaceLighting, false, "test unavailable");
    Frame();
    Select("Lighting model", "Native");
    Check(runtime.Snapshot().Effects.SurfaceLighting == SurfaceLightingMode::Native,
          "cannot restore native lighting after capability loss");
    Click("Lighting model");
    Check(Find("Modern").Disabled, "unsupported lighting model selectable");
    Click("Native");
    AllCapabilities(true);
    Frame();
    Select("Global illumination", "Brixelizer GI");
    Select("GI resolution", "Quarter");
    EditScalar("GI diffuse intensity", "2.5");
    EditScalar("GI native scene light", "0.4");
    EditScalar("GI key light", "1.5");
    EditScalar("GI dynamic lights", "0.5");
    EditScalar("GI sky light", "0.6");
    {
        const auto gi = runtime.Snapshot().Effects.GlobalIllumination;
        Check(gi.SceneLight == 0.4F && gi.KeyLight == 1.5F && gi.DynamicLights == 0.5F && gi.SkyLight == 0.6F,
              "GI source controls did not apply");
    }
    Click("GI specular reflections");
    EditScalar("GI specular intensity", "0.75");
    {
        const auto gi = runtime.Snapshot().Effects.GlobalIllumination;
        Check(gi.Mode == GlobalIlluminationMode::BrixelizerGi &&
                  gi.Resolution == GlobalIlluminationResolution::Quarter && gi.DiffuseIntensity == 2.5F,
              "global illumination controls did not apply");
        Check(gi.Specular && gi.SpecularIntensity == 0.75F, "GI specular controls did not apply");
        const auto persistedSpecular = DeserializeGraphicsSettings(store->Root["Graphics"]);
        Check(persistedSpecular.Value.Effects.GlobalIllumination.Specular &&
                  persistedSpecular.Value.Effects.GlobalIllumination.SpecularIntensity == 0.75F,
              "GI specular selection lost on reload");
        Check(runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
              "global illumination rendered without per-pixel lighting");
        const auto persistedGi = DeserializeGraphicsSettings(store->Root["Graphics"]);
        Check(persistedGi.Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi &&
                  persistedGi.Value.Effects.GlobalIllumination.DiffuseIntensity == 2.5F,
              "global illumination selection lost on reload");
    }
    Select("Lighting model", "Modern");
    Check(runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi,
          "per-pixel lighting did not activate the stored global illumination");
    runtime.ToggleNativePresentationOverride();
    Check(runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
          "F2 did not bypass global illumination");
    runtime.ToggleNativePresentationOverride();
    runtime.SetCapability(GraphicsCapability::BrixelizerGlobalIllumination, false, "provider unavailable");
    Check(runtime.Snapshot().Effects.GlobalIllumination.Mode == GlobalIlluminationMode::BrixelizerGi &&
              runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
          "an unavailable global illumination mode rendered");
    Frame();
    Select("Global illumination", "Off");
    Check(runtime.Snapshot().Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
          "cannot disable global illumination after capability loss");
    Click("Global illumination");
    Check(Find("Brixelizer GI").Disabled, "unsupported global illumination selectable");
    Click("Off");
    Select("Lighting model", "Native");
    AllCapabilities(true);
    // Screen-space GI: another mode of the same control with its own
    // parameters, persisted; the Brixelizer-only controls are hidden; the
    // provider's readiness is shown; capability-safe Off.
    {
        const auto shown = [](const char* label) {
            for (const auto& [id, item] : items)
                if (item.Label == label) return true;
            return false;
        };
        Frame();
        Select("Global illumination", "Screen-space GI");
        Frame();
        Check(!shown("GI sky light") && !shown("GI specular reflections"),
              "Brixelizer-only GI controls shown for screen-space GI");
        Check(!shown("GI resolution"), "Brixelizer's resolution shown for screen-space GI");
        Select("SSGI work scale", "Half");
        EditScalar("SSGI sections", "2");
        Select("SSGI radial candidates", "16");
        Select("SSGI sectors", "128");
        EditScalar("SSGI radius", "240");
        EditScalar("SSGI thickness", "45");
        Click("SSGI temporal accumulation");
        EditScalar("SSGI filter levels", "3");
        EditScalar("SSGI highlight clamp", "1.5");
        Click("SSGI validation");
        const auto gi = runtime.Snapshot().Effects.GlobalIllumination;
        const auto& ss = gi.ScreenSpace;
        Check(gi.Mode == GlobalIlluminationMode::ScreenSpace && ss.WorkScale == GlobalIlluminationResolution::Half &&
                  gi.Resolution == GlobalIlluminationResolution::Quarter && ss.Sections == 2U &&
                  ss.RadialCandidates == 16U && ss.Sectors == 128U && ss.Radius == 240.0F &&
                  ss.Thickness == 45.0F && !ss.Temporal && ss.FilterLevels == 3U && ss.HighlightClamp == 1.5F &&
                  ss.Validation,
              "screen-space GI controls did not apply");
        const auto persisted = DeserializeGraphicsSettings(store->Root["Graphics"]);
        Check(persisted.Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::ScreenSpace &&
                  persisted.Value.Effects.GlobalIllumination.ScreenSpace == ss,
              "screen-space GI selection lost on reload");
        Check(runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
              "screen-space GI rendered without per-pixel lighting");
        Select("Lighting model", "Modern");
        Check(runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode ==
                  GlobalIlluminationMode::ScreenSpace,
              "per-pixel lighting did not activate screen-space GI");
        runtime.PublishGlobalIlluminationStatus({GlobalIlluminationMode::ScreenSpace, false, "creating pipelines"});
        Frame(true);
        Check(loggedPanelText.find("GI not available yet: creating pipelines") != std::string::npos,
              "screen-space GI readiness not shown");
        runtime.PublishGlobalIlluminationStatus({GlobalIlluminationMode::ScreenSpace, true, {}});
        Frame(true);
        Check(loggedPanelText.find("GI not available yet") == std::string::npos,
              "ready screen-space GI reported unavailable");
        runtime.SetCapability(GraphicsCapability::ScreenSpaceGlobalIllumination, false, "provider unavailable");
        Check(runtime.Snapshot().Effects.GlobalIllumination.Mode == GlobalIlluminationMode::ScreenSpace &&
                  runtime.SnapshotForRendering().Value.Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
              "an unavailable screen-space GI rendered");
        Frame();
        Select("Global illumination", "Off");
        Check(runtime.Snapshot().Effects.GlobalIllumination.Mode == GlobalIlluminationMode::Off,
              "cannot disable screen-space GI after capability loss");
        Click("Global illumination");
        Check(Find("Screen-space GI").Disabled, "unsupported screen-space GI selectable");
        Click("Off");
        Select("Lighting model", "Native");
        runtime.PublishGlobalIlluminationStatus({});
        AllCapabilities(true);
    }
    Click("Ambient Occlusion");
    runtime.SetCapability(GraphicsCapability::ValidViewMetadata, false, "test unavailable");
    Frame();
    Select("Ambient occlusion", "Off");
    Check(runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Off, "cannot disable AO");
    AllCapabilities(true);
    Select("Ambient occlusion", "Multiscale GTAO");
    Select("AO quality", "Medium");
    Check(runtime.Snapshot().Effects.AoQuality == 1, "AO quality not applied");
    EditScalar("Broad radius", "8");
    EditScalar("Fine radius", "1.25");
    EditScalar("Fine strength", "0");
    EditScalar("Broad strength", "2");
    const auto ao = runtime.Snapshot().Effects;
    Check(ao.AoRadius == 8.0F && ao.AoDetailRadius == 1.25F &&
              ao.AoDetailStrength == 0.0F && ao.AoStrength == 2.0F,
          "AO fine/broad controls are coupled or were not applied");
    Select("Ambient occlusion", "FidelityFX CACAO");
    Click("Advanced AO");
    EditScalar("Horizon angle threshold", "0.12");
    EditScalar("Blur passes", "5");
    Check(runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Cacao &&
              runtime.Snapshot().Effects.AoHorizonAngleThreshold == 0.12F &&
              runtime.Snapshot().Effects.AoBlurPassCount == 5,
          "original CACAO controls did not apply");
#ifdef ENABLE_OOT3D_IMMERSE
    // The MXAO controls belong to the iMMERSE module (fast/immerse/mxao_settings_panel.cpp).
    Select("Ambient occlusion", "iMMERSE MXAO");
    Select("MXAO quality", "High");
    Select("MXAO method", "Solid angle");
    EditScalar("MXAO radius", "2.5");
    EditScalar("MXAO amount", "1.5");
    EditScalar("MXAO filter quality", "2");
    const auto mxao = runtime.Snapshot().Effects;
    Check(mxao.AmbientOcclusion == AmbientOcclusionMode::Mxao && mxao.MxaoQuality == 2 &&
              mxao.MxaoMethod == MxaoEstimator::SolidAngle && mxao.AoRadius == 2.5F && mxao.AoStrength == 1.5F &&
              mxao.MxaoFilter == 2 && mxao.AoHorizonAngleThreshold == 0.12F,
          "MXAO controls did not apply or disturbed the CACAO controls");
    Click("MXAO temporal accumulation");
    Check(runtime.Snapshot().Effects.MxaoTemporal && runtime.Snapshot().Effects.MxaoQuality == 2 &&
              runtime.Snapshot().Effects.MxaoFilter == 2,
          "MXAO temporal accumulation did not apply or disturbed the MXAO controls");
    // The original's remaining options and the debug views.
    Select("MXAO quality", "IDGAF");
    Select("MXAO shading rate", "Quarter rate");
    Select("MXAO normals", "Renderer normals");
    Click("Increase radius with distance");
    EditScalar("MXAO sample radius", "4");
    Click("Original fade");
    EditScalar("Fade out distance", "0.5");
    Select("MXAO debug view", "Normal disagreement");
    {
        const auto options = runtime.Snapshot().Effects;
        Check(options.MxaoQuality == 6 && options.MxaoShadingRate == 2 &&
                  options.MxaoNormals == MxaoNormalSource::Renderer && options.MxaoRadiusGrowsWithDistance &&
                  options.MxaoScreenRadius == 4.0F && options.MxaoOriginalFade && options.MxaoFadeDistance == 0.5F &&
                  options.MxaoDebug == MxaoDebugView::NormalDisagreement && options.AoRadius == 2.5F &&
                  options.MxaoTemporal,
              "the original MXAO options did not apply or disturbed the other MXAO controls");
    }
    // Face normals and the views that compare them.
    Select("MXAO normals", "Face normals");
    Select("MXAO debug view", "Face vs depth normals");
    Check(runtime.Snapshot().Effects.MxaoNormals == MxaoNormalSource::Geometry &&
              runtime.Snapshot().Effects.MxaoDebug == MxaoDebugView::FaceDisagreement,
          "MXAO face normals or their debug view did not apply");
    Select("MXAO debug view", "Face normals");
    Check(runtime.Snapshot().Effects.MxaoDebug == MxaoDebugView::FaceNormals,
          "the MXAO face normal view did not apply");
    Select("MXAO debug view", "Off");
    Click("Original fade");
    Click("Increase radius with distance");
    Select("MXAO normals", "From depth (original)");
    Check(runtime.Snapshot().Effects.MxaoNormals == MxaoNormalSource::Depth, "MXAO depth normals cannot be selected");
    Select("MXAO normals", "Face normals");
    Select("MXAO shading rate", "Full rate");
    Select("MXAO quality", "High");
    {
        const auto restored = runtime.Snapshot().Effects;
        Check(restored.MxaoDebug == MxaoDebugView::Off, "the MXAO debug view cannot be turned off");
        Check(restored.MxaoNormals == MxaoNormalSource::Geometry, "MXAO face normals cannot be restored");
        Check(!restored.MxaoRadiusGrowsWithDistance, "MXAO increase radius with distance cannot be cleared");
        Check(!restored.MxaoOriginalFade, "the MXAO original fade cannot be cleared");
        Check(restored.MxaoShadingRate == 0, "the MXAO full shading rate cannot be restored");
    }
    Select("AO texture relief", "Strong");
    Check(runtime.Snapshot().Effects.AoRelief == AoTextureRelief::Strong &&
              runtime.Snapshot().Effects.MxaoQuality == 2,
          "AO texture relief did not apply or disturbed the MXAO controls");
#endif
    Select("Ambient occlusion", "Multiscale GTAO");
    Select("AO texture relief", "Subtle");
    Check(runtime.Snapshot().Effects.AoRelief == AoTextureRelief::Subtle &&
              runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Gtao,
          "AO texture relief is not shared by the AO providers");
    Select("AO texture relief", "Off");
#ifdef ENABLE_OOT3D_IMMERSE
    Select("Ambient occlusion", "iMMERSE MXAO");
    Check(runtime.Snapshot().Effects.AoRelief == AoTextureRelief::Off && runtime.Snapshot().Effects.MxaoTemporal,
          "cannot disable AO texture relief, or another provider dropped MXAO accumulation");
#endif
    runtime.SetCapability(GraphicsCapability::ImmerseMxao, false, "provider unavailable");
    Frame();
#ifdef ENABLE_OOT3D_IMMERSE
    // A selected provider that is not available (the iMMERSE module is not
    // installed) renders Off; the preference stays.
    Check(runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Mxao &&
              runtime.SnapshotForRendering().Value.Effects.AmbientOcclusion == AmbientOcclusionMode::Off,
          "an unavailable MXAO is rendered, or its preference was dropped");
#endif
    Select("Ambient occlusion", "Off");
    Click("Ambient occlusion");
    Check(Find("iMMERSE MXAO").Disabled && !Find("Multiscale GTAO").Disabled,
          "MXAO capability incorrectly disabled GTAO or allowed unsupported MXAO");
    Click("Off");
    runtime.SetCapability(GraphicsCapability::ImmerseMxao, true);
    Frame();
    runtime.SetCapability(GraphicsCapability::FidelityFxCacao, false, "provider unavailable");
    Frame();
    Select("Ambient occlusion", "Off");
    Click("Ambient occlusion");
    Check(Find("FidelityFX CACAO").Disabled && !Find("Multiscale GTAO").Disabled,
          "CACAO capability incorrectly disabled GTAO or allowed unsupported CACAO");
    Click("Multiscale GTAO");
    Check(runtime.Snapshot().Effects.AoDetailRadius == 1.25F,
          "switching AO provider erased the GTAO fine radius");
    runtime.SetCapability(GraphicsCapability::NormalGuide, false, "test unavailable");
    Frame();
    Select("Ambient occlusion", "Off");
    Check(runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Off,
          "cannot disable AO after normal-guide capability loss");
    AllCapabilities(true);
    Click("Reflections");
    Select("Reflection provider", "FidelityFX SSSR");
    runtime.SetCapability(GraphicsCapability::NormalGuide, false, "test unavailable");
    Frame();
    Select("Reflection provider", "Off");
    Check(runtime.Snapshot().Effects.Reflections == ReflectionMode::Off, "cannot disable reflections");
    AllCapabilities(true);
    Click("Display");
    Select("Mode", "MSAA");
    Select("Samples", "4x");
    Check(runtime.Snapshot().MsaaSamples == 4, "MSAA selection lost");
    Select("Mode", "NRI Upscaler");
    Select("Provider", "NVIDIA DLSS");
    Check(runtime.Snapshot().Upscaler == UpscalerProvider::Dlss, "provider not applied");
    Select("DLSS model", "CNN (E/F)");
    Check(runtime.Snapshot().UpscalerDlssModel == DlssModel::Cnn, "DLSS model not applied");
    Select("DLSS model", "Default");
    Check(std::none_of(items.begin(), items.end(), [](const auto& pair) { return pair.second.Label == "Sharpness"; }),
          "unused DLSS sharpness exposed");

    Click("Grass");
    Click("Generation");
    EditScalar("Blades per square metre", "4096");
    Check(runtime.Snapshot().Grass.Generation.InstancesPerSquareMeter == 4096.0F,
          "4096 grass density not applied");
    Click("Appearance");
    EditScalar("Curvature", "1.8");
    EditScalar("Tip droop", "0.8");
    EditScalar("Shape irregularity", "0.9");
    EditScalar("Blade twist", "150");
    const auto shape = runtime.Snapshot().Grass.Appearance;
    Check(std::abs(shape.BladeCurvature - 1.8F) < 0.001F &&
          std::abs(shape.BladeDroop - 0.8F) < 0.001F &&
          std::abs(shape.ShapeVariation - 0.9F) < 0.001F &&
          shape.BladeTwistDegrees == 150.0F, "grass shape controls not applied");
    EditScalar("Rim fade start (m)", "3");
    EditScalar("Rim fade end (m)", "12");
    Check(runtime.Snapshot().Grass.Appearance.ToonRimFadeStart == 300.0F &&
          runtime.Snapshot().Grass.Appearance.ToonRimFadeEnd == 1200.0F, "grass rim metre conversion failed");
    Click("Enable nearby rim");
    Check(!runtime.Snapshot().Grass.Appearance.ToonRimEnabled, "grass rim disable failed");
    Click("Enable nearby rim");
    Check(runtime.Snapshot().Grass.Appearance.ToonRimEnabled, "grass rim enable failed");
    Click("Performance");
    EditScalar("Draw distance", "20000");
    EditScalar("Density falloff distance", "5000");
    EditScalar("Final fade range", "0.25");
    EditScalar("Density fade softness", "0.30");
    EditScalar("LOD transition range", "0.35");
    EditScalar("Distant tuft quantity", "2.5");
    EditScalar("Distant tuft spread", "1.8");
    Check(runtime.Snapshot().Grass.DrawDistance == 20000.0F &&
          runtime.Snapshot().Grass.LodReferenceDistance == 5000.0F,
          "grass visibility and density distances not independent");
    EditScalar("Near blade segments", "8");
    EditScalar("Far blade segments", "2");
    EditScalar("Segment reduction begins", "120");
    EditScalar("Minimum segments reached", "650");
    EditScalar("Segment transition spread", "0.75");
    const auto grassLod = runtime.Snapshot().Grass;
    Check(grassLod.Appearance.BladeSegments == 8U && grassLod.FarBladeSegments == 2U &&
          grassLod.SegmentLodStartDistance == 120.0F && grassLod.SegmentLodEndDistance == 650.0F,
          "independent grass segment LOD controls not applied");
    Check(grassLod.DrawFadeFraction == 0.25F && grassLod.DensityFadeFraction == 0.30F &&
          grassLod.TuftTransitionFraction == 0.35F && grassLod.FarTuftDensity == 2.5F &&
          grassLod.FarTuftSpread == 1.8F && grassLod.SegmentLodSoftness == 0.75F,
          "grass transition, tuft quantity/spread controls not applied");
    size.y = 680.0F;
    for (const char* section : {"Generation", "Appearance", "Performance", "Wind", "Interaction", "Sources"})
        Click(section);
    updateTextureObservations = true;
    Frame();
    Click("Texture catalog selection");
    ClickTexture(secondTextureHash, true);
    Click("Add selected texture source");
    Check(runtime.Snapshot().Grass.Rules.size() == 1 &&
          runtime.Snapshot().Grass.Rules.front().Target.Rgba8Hash == secondTextureHash,
          "texture selection did not reach the grass consumer");
    Click("Materials");
    Click("Reflection materials");
    Click("Texture catalog selection");
    ClickTexture(firstTextureHash);
    Click("Assign selected as water");
    Check(runtime.Snapshot().Effects.ReflectionMaterials.size() == 1 &&
          runtime.Snapshot().Effects.ReflectionMaterials.front().Target.ContentHash == firstTextureHash,
          "texture selection did not reach the reflection consumer");
    for (uint64_t index = 0; index < 120; ++index)
        TextureCatalogRuntime::Instance().Observe(0xC000U + index, 0x3000U, 16, 16, 0);
    Click("Texture catalog selection");
    auto* popup = ImGui::FindWindowByName("##Combo_00");
    Check(popup != nullptr && popup->Active, "texture popup is not active");
    ImGui::GetIO().AddMousePosEvent(popup->Pos.x + 24.0F, popup->Pos.y + 24.0F);
    Frame();
    ImGui::GetIO().AddMouseWheelEvent(0.0F, -100.0F);
    Frame(); Frame();
    ClickTexture(0xC000U + 119U);
    Click("Assign selected as metal");
    const auto reflectionRules = runtime.Snapshot().Effects.ReflectionMaterials;
    Check(std::any_of(reflectionRules.begin(), reflectionRules.end(), [](const auto& rule) {
        return rule.Target.ContentHash == 0xC000U + 119U;
    }), "catalog truncated entries after the first 96");
    Click("Grass");
    updateTextureObservations = false;
    auto display = runtime.Snapshot();
    display.Window = WindowMode::Borderless;
    runtime.Apply(display);
    runtime.AcknowledgePresentationApplied(display);
    showPanel = false;
    runtime.PublishDisplayMetrics({1920, 1080, 1280, 720, 2.0F / 3.0F, WindowMode::Borderless});
    Frame();
    Click("Keep display settings");
    Check(runtime.PresentationStatus().Phase == PresentationTransactionPhase::Idle,
          "display confirmation not accessible with F1 closed");
    showPanel = true;
    const auto confirmedDisplay = runtime.Snapshot();
    auto rejectedDisplay = confirmedDisplay;
    rejectedDisplay.Window = WindowMode::ExclusiveFullscreen;
    Check(runtime.Apply(rejectedDisplay).Accepted(), "display request rejected before backend");
    const std::string displayFailure = "Test monitor: exclusive mode unsupported";
    Check(runtime.RejectPresentationApply(rejectedDisplay, displayFailure),
          "backend display failure not accepted");
    Check(GetPresentationSettings(runtime.Snapshot()) == GetPresentationSettings(confirmedDisplay),
          "failed display request did not restore previous settings");
    Frame(true);
    Check(loggedPanelText.find("Display change reverted: " + displayFailure) != std::string::npos,
          "backend failure not visible in F1 outside Display");
    Check(!runtime.RejectPresentationApply(rejectedDisplay, "stale failure"),
          "stale display failure accepted");
    Check(!runtime.RejectPresentationApply(confirmedDisplay, "idle failure"),
          "idle display failure accepted");
    Check(!runtime.AcknowledgePresentationApplied(confirmedDisplay),
          "duplicate recovery acknowledgement accepted");
    Check(runtime.LastPresentationRejection() == displayFailure,
          "stale response erased the useful display failure");
    Check(runtime.Apply(rejectedDisplay).Accepted(), "display retry not staged");
    Check(runtime.RollbackPresentation(), "pending display retry not reverted");
    Check(runtime.AcknowledgePresentationApplied(confirmedDisplay),
          "display retry rollback not acknowledged");
    Check(runtime.LastPresentationRejection() == displayFailure,
          "rollback acknowledgement erased the useful display failure");
    Check(runtime.Apply(rejectedDisplay).Accepted(), "second display retry not staged");
    Check(!runtime.RejectPresentationApply(confirmedDisplay, "older request failed"),
          "older display request replaced a newer one");
    Check(!runtime.AcknowledgePresentationApplied(confirmedDisplay),
          "older display acknowledgement replaced a newer one");
    Check(runtime.LastPresentationRejection() == displayFailure,
          "retry lost display failure before backend success");
    Check(runtime.AcknowledgePresentationApplied(rejectedDisplay),
          "successful display retry not acknowledged");
    Check(runtime.LastPresentationRejection().empty(), "successful retry retained old error");
    Frame(true);
    Check(loggedPanelText.find("Display change reverted:") == std::string::npos,
          "F1 retained error after successful retry");
    Click("Revert display settings");
    Check(runtime.AcknowledgePresentationApplied(confirmedDisplay),
          "smoke test could not restore its original display");
    Click("Materials");
    Click("Texture packs");
    Find("Apply folders");
    Click("Controls");
    for (const char* section : {"Bindings", "Shortcuts", "Camera"}) Click(section);
    Click("Enabled##freecam");
    Check(topScreen->Snapshot().Config.FreeCameraEnabled, "central camera control not connected");
    Click("##Free-camera source");
    Frame(true);
    Check(std::none_of(items.begin(), items.end(), [](const auto& pair) {
              return pair.second.Popup && (pair.second.Label == "Controller Gyroscope" ||
                  pair.second.Label == "Controller Accelerometer" ||
                  pair.second.Label == "Controller gyro + accelerometer");
          }),
          "free-camera source exposes motion sensors");
    Click("Right Stick");
    Check(controls->Snapshot().Config.FreeCameraSource == Oot3dNativeGame::NativeMotionSource::RightStick,
          "free-camera stick choice not connected");
    auto cameraExternal = topScreen->Snapshot().Config;
    cameraExternal.HudMarginX = 7;
    topScreen->Preview(cameraExternal);
    Frame();
    Click("Enabled##freecam");
    Check(!topScreen->Snapshot().Config.FreeCameraEnabled && topScreen->Snapshot().Config.HudMarginX == 7,
          "central camera control overwrote external HUD state");
    Click("Free camera");
    Click("Aiming");
    Select("##Aim source", "Mouse");
    EditScalar("##Mouse aim sensitivity", "0.61");
    Check(std::abs(controls->Snapshot().Config.MouseAimDegreesPerPixel - 0.61F) < 0.001F,
          "aiming sensitivity not connected");
    Click("Devices");
    Click("Keyboard");
    Check(!controls->Snapshot().Config.KeyboardEnabled, "control preview not connected");
    Click("Controller touchpad");
    Check(!controls->Snapshot().Config.ControllerTouchpadEnabled, "touchpad disable not connected");
    Click("Controller touchpad");
    Check(controls->Snapshot().Config.ControllerTouchpadEnabled, "touchpad enable not connected");
    controls->ObserveDevices({
        {11, "same-model", "Twin pad", false, false, "first", true},
        {12, "same-model", "Twin pad", false, false, "second", false},
    });
    Frame();
    Select("##Active controller", "Twin pad [12]##12");
    Check(controls->Snapshot().Config.PreferredControllerGuid == "same-model" &&
          controls->Snapshot().Config.PreferredControllerSerial == "second",
          "controller selection discarded physical identity");
    auto external = controls->Snapshot().Config;
    external.MouseAimDegreesPerPixel = 0.77F;
    controls->Preview(external);
    Frame();
    Click("Keyboard");
    Check(controls->Snapshot().Config.MouseAimDegreesPerPixel == 0.77F, "stale draft overwrote external controls");
    Click("Bindings");
    using Action = Oot3dNativeGame::NativeControlAction;
    using Key = Oot3dNativeGame::NativeKeyboardKey;
    const auto movementIndex = static_cast<size_t>(Action::MoveForward);
    // Old content-proportional columns shrink progressively, not just on resize.
    EditText("##ActionFilter", "move");
    for (const auto width : {520.0F, 760.0F, 1100.0F, 520.0F}) {
        size = ImVec2(width, 680.0F);
        Frame(); Frame(); Frame();
        const auto primary = Find("##primary:Move forward").Rect;
        const auto alternate = Find("##alternate:Move forward").Rect;
        Check(primary.GetWidth() > 100.0F && alternate.GetWidth() > 100.0F,
              "binding fields became too narrow");
        Check(primary.Max.x < alternate.Min.x && alternate.Max.x < size.x + 8.0F,
              "binding columns overlap or leave the window");
        for (int frame = 0; frame < 120; ++frame) Frame();
        const auto after = Find("##primary:Move forward").Rect;
        Check(std::abs(after.Min.x - primary.Min.x) < 1.0F &&
              std::abs(after.GetWidth() - primary.GetWidth()) < 1.0F,
              "binding columns drifted across idle frames");
    }
    size = ImVec2(760.0F, 680.0F); Frame(); Frame();
    Select("##primary:Move forward", "Unassigned");
    Check(controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::None,
          "clear key did not update runtime");
    Click("##primary:Move forward");
    EditText("##BindingSearch", "numpad 8");
    Click("Numpad 8");
    Check(controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::Numpad8,
          "searchable key assignment did not update runtime");
    Click("##primary:Move forward");
    Check(GImGui->OpenPopupStack.Size > 0, "key popup failed to reopen");
    Click("W");
    Check(controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::W,
          "key popup retained stale search on reopen");
    Click("Mouse");
    Select("##mouse:Move forward", "Back");
    Check(controls->Snapshot().Config.Bindings[movementIndex].Mouse == Oot3dNativeGame::NativeMouseButton::Back &&
          controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::W,
          "mouse assignment changed the wrong device");
    Select("##mouse:Move forward", "Unassigned");
    Click("Controller");
    const auto beforeSwap = controls->Snapshot().Config;
    Click("Swap shoulders / triggers");
    const auto swapped = controls->Snapshot().Config;
    using Button = Oot3dNativeGame::NativeGamepadButton;
    Check(swapped.Bindings[static_cast<size_t>(Action::L)].Gamepad == Button::LeftTrigger &&
          swapped.Bindings[static_cast<size_t>(Action::R)].Gamepad == Button::RightTrigger &&
          swapped.Bindings[static_cast<size_t>(Action::Zl)].Gamepad == Button::LeftShoulder &&
          swapped.Bindings[static_cast<size_t>(Action::Zr)].Gamepad == Button::RightShoulder,
          "full controller swap failed to move both item bindings");
    Click("Swap shoulders / triggers");
    Check(controls->Snapshot().Config.Bindings == beforeSwap.Bindings,
          "second controller swap did not restore original bindings");
    Click("##gamepad:Move forward");
    EditText("##BindingSearch", "right stick");
    Click("Right Stick");
    Check(controls->Snapshot().Config.Bindings[movementIndex].Gamepad == Button::RightStick &&
          controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::W,
          "controller assignment changed the wrong device");
    Select("##gamepad:Move forward", "Unassigned");
    EditText("##ActionFilter", "no such control");
    Frame(true);
    Check(loggedPanelText.find("No matching controls") != std::string::npos,
          "empty binding search not reported");
    EditText("##ActionFilter", "");
    Find("##gamepad:Move forward");
    EditText("##ActionFilter", "Move forward");
    RawBindingSource rawBinding;
    using CapturePhase = ThreeDsRecomp::Input::BindingCapturePhase;
    Click("##gamepad:Move forward"); Click("Listen...");
    Check(controls->BindingCaptureStatus().Phase == CapturePhase::Release, "controller Listen did not start capture");
    controls->ObserveBindingCapture(rawBinding, false); Frame();
    rawBinding.Held.Gamepad = Button::LeftTrigger;
    controls->ObserveBindingCapture(rawBinding, false); Frame(); Frame();
    Check(controls->Snapshot().Config.Bindings[movementIndex].Gamepad == Button::LeftTrigger &&
          !controls->Snapshot().Config.ControllerEnabled, "Listen did not assign a disabled controller source");
    Click("Keyboard");
    rawBinding.Held = {};
    Click("##alternate:Move forward"); Click("Listen...");
    controls->ObserveBindingCapture(rawBinding, false); Frame();
    rawBinding.Held.KeyboardPrimary = Key::P;
    controls->ObserveBindingCapture(rawBinding, false); Frame(); Frame();
    Check(controls->Snapshot().Config.Bindings[movementIndex].KeyboardSecondary == Key::P &&
          controls->Snapshot().Config.Bindings[movementIndex].KeyboardPrimary == Key::W,
          "Listen assigned wrong keyboard slot");
    Click("Mouse");
    rawBinding.Held = {};
    rawBinding.Held.Mouse = Oot3dNativeGame::NativeMouseButton::Left;
    const auto beforeCapture = controls->Snapshot().Config;
    Click("##mouse:Move forward"); Click("Listen...");
    controls->ObserveBindingCapture(rawBinding, false); Frame();
    Check(controls->Snapshot().Config == beforeCapture && controls->BindingCaptureStatus().Phase == CapturePhase::Release,
          "Listen captured its opening mouse click");
    rawBinding.Held = {};
    controls->ObserveBindingCapture(rawBinding, false); Frame();
    rawBinding.Held.Mouse = Oot3dNativeGame::NativeMouseButton::Forward;
    controls->ObserveBindingCapture(rawBinding, false); Frame(); Frame();
    Check(controls->Snapshot().Config.Bindings[movementIndex].Mouse == Oot3dNativeGame::NativeMouseButton::Forward,
          "Listen did not assign the mouse side button");
    const auto beforeCancel = controls->Snapshot().Config;
    Click("##mouse:Move forward"); Click("Listen...");
    Click("Cancel");
    Check(controls->Snapshot().Config == beforeCancel, "cancelling Listen modified a binding");
    Click("Presets...");
    const auto beforePreset = controls->Snapshot().Config;
    Select("##Preset", "Controller");
    Check(controls->Snapshot().Config == beforePreset, "preset selection applied without confirmation");
    Click("Cancel");
    Check(controls->Snapshot().Config == beforePreset, "cancel changed the control profile");
    Click("Presets...");
    Click("Apply preset");
    Check(controls->Snapshot().Config.Profile == Oot3dNativeGame::NativeControlProfile::Controller,
          "confirmed preset not applied");
    Check(controls->Snapshot().Config.PreferredControllerGuid == beforePreset.PreferredControllerGuid &&
          controls->Snapshot().Config.PreferredControllerSerial == beforePreset.PreferredControllerSerial &&
          controls->Snapshot().Config.GyroscopeBiasDegreesPerSecond == beforePreset.GyroscopeBiasDegreesPerSecond &&
          controls->Snapshot().Config.AccelerometerNeutral == beforePreset.AccelerometerNeutral,
          "preset discarded controller identity or calibration");
    Click("HUD");
    Frame(true);
    Check(loggedPanelText.find("TopScreen mod by M-1") != std::string::npos, "HUD lacks the TopScreen credit");
    Click("Render HUD");
    Check(!topScreen->Snapshot().Config.RenderHud, "TopScreen preview not connected");
    auto externalTop = topScreen->Snapshot().Config;
    externalTop.HudScale = 0.75F;
    topScreen->Preview(externalTop);
    Frame();
    Click("Render HUD");
    Check(topScreen->Snapshot().Config.HudScale == 0.75F, "stale draft overwrote external TopScreen");
    size = ImVec2(520, 400);
    Frame(); Frame();
    Find("Save TopScreen");
    Click("Controls");
    for (const char* section : {"Bindings", "Camera", "Devices", "Shortcuts"}) {
        Click(section);
        Frame(); Frame();
        const auto saveRect = Find("Save controls").Rect;
        const auto revertRect = Find("Revert changes").Rect;
        Check(saveRect.Min.y > 8.0F && saveRect.Max.y < size.y + 8.0F &&
              revertRect.Max.x < size.x + 8.0F && saveRect.Max.x < revertRect.Min.x,
              "compact Controls footer is clipped or overlapping");
        for (const auto* window : GImGui->Windows)
            if (window->Active && (window->Flags & ImGuiWindowFlags_ChildWindow))
                Check(window->ScrollMax.x == 0.0F, "Controls contents overflow horizontally");
    }

    auto pending = runtime.Snapshot();
    const int storesBefore = store->Stores;
    for (int i = 0; i < 30; ++i) {
        pending.FovMultiplier = 1.0F + i * 0.01F;
        runtime.Apply(pending, false);
    }
    Check(store->Stores == storesBefore, "drag wrote settings on every step");
    runtime.SavePending();
    Check(store->Stores == storesBefore + 1, "deferred save not coalesced");
    size = ImVec2(760, 680);
    Click("Display");
    Frame();
    const auto fov = Find("Global scene FOV");
    const int beforeDrag = store->Stores;
    auto& io = ImGui::GetIO();
    const float y = (fov.Rect.Min.y + fov.Rect.Max.y) * 0.5F;
    io.AddMousePosEvent(fov.Rect.Min.x + 30.0F, y);
    Frame();
    io.AddMouseButtonEvent(0, true);
    Frame();
    for (int step = 0; step < 10; ++step) {
        io.AddMousePosEvent(fov.Rect.Min.x + 35.0F + step * 5.0F, y);
        Frame();
    }
    Check(store->Stores == beforeDrag, "live UI drag wrote to disk");
    io.AddMouseButtonEvent(0, false);
    Frame(); Frame();
    Check(store->Stores == beforeDrag + 1, "UI slider release did not save once");
    const bool extendedBefore = runtime.Snapshot().ExtendedDrawDistance;
    const int beforeExtended = store->Stores;
    Click("Extended draw distance");
    Frame(); Frame();
    Check(runtime.Snapshot().ExtendedDrawDistance != extendedBefore, "draw distance checkbox not applied");
    Check(store->Stores == beforeExtended + 1, "draw distance checkbox did not save once");
    Click("Extended draw distance");
    Frame(); Frame();
    Check(runtime.Snapshot().ExtendedDrawDistance == extendedBefore, "draw distance checkbox did not restore");
    pending = runtime.Snapshot();
    store->Fail = true;
    pending.FovMultiplier = 1.1F;
    runtime.Apply(pending);
    Check(runtime.SaveState() == GraphicsSettingsSaveState::Failed, "save failure hidden");
    store->Fail = false;
    Frame();
    Click("Retry saving graphics");
    Check(runtime.SaveState() == GraphicsSettingsSaveState::Saved, "save retry failed");
    CheckControlPersistence();
    CheckGameSurfaceMouseResume();
    const auto languagePath = std::filesystem::temp_directory_path() /
        ("triaevum-language-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    const std::vector<Oot3dNativeGame::GameLanguage> languages{{"en","English",1},{"it","Italiano",4}};
    auto language = std::make_shared<Oot3dNativeGame::GameLanguageSettings>(languagePath,languages);
    InstallGraphicsSettingsPanelTabs({Oot3dNativeGame::CreateGameLanguagePanel(language)});
    Frame(); Frame();
    Click("Game"); Click("Language"); Frame();
    Select("Game language", "Italiano");
    Check(language->Selected() == "it", "language widget did not persist choice");
    Check(language->SystemId() == 1, "language selection mutated the running guest");
    Frame(true);
    Check(loggedPanelText.find("Restart required") != std::string::npos, "language restart notice missing");
    Oot3dNativeGame::GameLanguageSettings restarted(languagePath,languages);
    Check(restarted.SystemId() == 4, "next boot did not select Italian CFG language");
    Check(!restarted.Select("de"), "language absent from ROM was accepted");
    Oot3dNativeGame::GameLanguageSettings differentRom(languagePath,{{"en","English",1}});
    Check(differentRom.SystemId() == 1, "unsupported old language leaked into another ROM");
    std::filesystem::remove(languagePath);
    CheckSavestatePanel();
    CheckSceneEnvironmentCodec();
    CheckSceneEnvironment();
    CheckMm3dSceneEnvironment();
    InstallGraphicsSettingsPanelTabs({});
    CheckTitleWithoutSceneHooks();
    ImGui::DestroyContext();
    std::cout << "F1 UI smoke passed: " << assertions << " assertions, real renderer/Controls/TopScreen widgets\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
