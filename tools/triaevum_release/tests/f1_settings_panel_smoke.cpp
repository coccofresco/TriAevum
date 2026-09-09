#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include "oot3d_native_controls_settings_panel.h"
#include "oot3d_top_screen_settings_panel.h"
#include <imgui_internal.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace Fast::Oot3d;
namespace {
struct Item { std::string Label; ImRect Rect; ImGuiID Seed = 0; bool Disabled = false; bool Popup = false; };
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
};
GraphicsSettingsPanel panel;
std::string loggedPanelText;
bool updateTextureObservations = false;
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
    Check(p.y < size.y + 8.0F && p.x < size.x + 8.0F, std::string("clipped UI action: ") + label);
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
    for (unsigned value = 0; value <= static_cast<unsigned>(GraphicsCapability::ExclusiveFullscreen); ++value)
        runtime.SetCapability(static_cast<GraphicsCapability>(value), available, "test capability");
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

int main() try {
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

    Check(!runtime.NativePresentationOverrideActive(), "F2 must start inactive");
    for (const auto toon : {ToonMode::Off, ToonMode::PostProcessPreview, ToonMode::PicaMaterial}) {
        for (const auto reflections : {ReflectionMode::Off, ReflectionMode::HiZ, ReflectionMode::FidelityFxSssr}) {
            auto configured = runtime.Snapshot();
            configured.Preset = GraphicsPreset::Custom;
            configured.Grass.Quality = GrassQuality::Custom;
            configured.Grass.Generation.InstancesPerSquareMeter = 1048.8F;
            configured.Effects.Toon = toon;
            configured.Effects.ToonStyle.OutlineEnabled = true;
            configured.Effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
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

    Select("Preset", "Authentic");
    Check(runtime.Snapshot().Preset == GraphicsPreset::Authentic, "preset identity lost");
    Click("Antialiasing");
    Select("Mode", "FXAA");
    Check(runtime.Snapshot().AntiAliasing == AntiAliasingMode::Fxaa, "cannot customize Authentic");
    Check(runtime.Snapshot().Preset == GraphicsPreset::Custom, "manual edits must become Custom");
    Select("Preset", "Toon");
    const auto beforeCustom = runtime.Snapshot();
    Select("Preset", "Custom");
    const auto expected = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Custom, beforeCustom);
    Check(SerializeGraphicsSettings(runtime.Snapshot()) == SerializeGraphicsSettings(expected),
          "Custom changed user settings");
    Check(runtime.Snapshot().Effects.Toon == ToonMode::PostProcessPreview, "Custom reset effects");
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

    Click("Lighting");
    runtime.SetCapability(GraphicsCapability::NriDirectionalShadows, false, "test unavailable");
    Frame();
    Select("Shadow mode", "Off");
    Check(runtime.Snapshot().Effects.DirectionalShadows.Mode == DirectionalShadowMode::Off,
          "cannot disable unsupported shadows");
    Click("Shadow mode");
    Check(Find("Single cascade").Disabled, "unsupported shadow mode selectable");
    Click("Off");
    runtime.SetCapability(GraphicsCapability::ValidViewMetadata, false, "test unavailable");
    Frame();
    Select("Ambient occlusion", "Off");
    Check(runtime.Snapshot().Effects.AmbientOcclusion == AmbientOcclusionMode::Off, "cannot disable AO");
    AllCapabilities(true);
    Select("Ambient occlusion", "FidelityFX CACAO");
    Select("AO quality", "Medium");
    Check(runtime.Snapshot().Effects.AoQuality == 1, "AO quality not applied");
    Click("Reflections");
    Select("Reflection provider", "FidelityFX SSSR");
    runtime.SetCapability(GraphicsCapability::NormalGuide, false, "test unavailable");
    Frame();
    Select("Reflection provider", "Off");
    Check(runtime.Snapshot().Effects.Reflections == ReflectionMode::Off, "cannot disable reflections");
    AllCapabilities(true);
    Click("Antialiasing");
    Select("Mode", "MSAA");
    Select("Samples", "4x");
    Check(runtime.Snapshot().MsaaSamples == 4, "MSAA selection lost");
    Select("Mode", "NRI Upscaler");
    Select("Provider", "NVIDIA DLSS");
    Check(runtime.Snapshot().Upscaler == UpscalerProvider::Dlss, "provider not applied");
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
    Click("Renderer");
    Click("Reflections");
    Click("Reflection material assignments");
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
    Frame();
    Click("Keep display settings");
    Check(runtime.PresentationStatus().Phase == PresentationTransactionPhase::Idle,
          "display confirmation not accessible outside Renderer");
    Click("Textures");
    Find("Apply folders");
    Click("Controls");
    for (const char* section : {"Bindings", "Aiming", "Actions", "Camera"}) Click(section);
    Click("Enabled##freecam");
    Check(topScreen->Snapshot().Config.FreeCameraEnabled, "central camera control not connected");
    auto cameraExternal = topScreen->Snapshot().Config;
    cameraExternal.HudMarginX = 7;
    topScreen->Preview(cameraExternal);
    Frame();
    Click("Enabled##freecam");
    Check(!topScreen->Snapshot().Config.FreeCameraEnabled && topScreen->Snapshot().Config.HudMarginX == 7,
          "central camera control overwrote external HUD state");
    for (const char* section : {"Motion", "Devices"}) Click(section);
    Click("Keyboard");
    Check(!controls->Snapshot().Config.KeyboardEnabled, "control preview not connected");
    auto external = controls->Snapshot().Config;
    external.MouseAimDegreesPerPixel = 0.77F;
    controls->Preview(external);
    Frame();
    Click("Keyboard");
    Check(controls->Snapshot().Config.MouseAimDegreesPerPixel == 0.77F, "stale draft overwrote external controls");
    Click("Bindings");
    const auto beforeSwap = controls->Snapshot().Config;
    Click("Swap shoulders / triggers");
    const auto swapped = controls->Snapshot().Config;
    using Action = Oot3dNativeGame::NativeControlAction;
    using Button = Oot3dNativeGame::NativeGamepadButton;
    Check(swapped.Bindings[static_cast<size_t>(Action::L)].Gamepad == Button::LeftTrigger &&
          swapped.Bindings[static_cast<size_t>(Action::R)].Gamepad == Button::RightTrigger &&
          swapped.Bindings[static_cast<size_t>(Action::Zl)].Gamepad == Button::LeftShoulder &&
          swapped.Bindings[static_cast<size_t>(Action::Zr)].Gamepad == Button::RightShoulder,
          "full controller swap failed to move both item bindings");
    Click("Swap shoulders / triggers");
    Check(controls->Snapshot().Config.Bindings == beforeSwap.Bindings,
          "second controller swap did not restore original bindings");
    Click("TopScreen 2.1.1");
    Click("Render HUD");
    Check(!topScreen->Snapshot().Config.RenderHud, "TopScreen preview not connected");
    auto externalTop = topScreen->Snapshot().Config;
    externalTop.HudScale = 0.75F;
    topScreen->Preview(externalTop);
    Frame();
    Click("Render HUD");
    Check(topScreen->Snapshot().Config.HudScale == 0.75F, "stale draft overwrote external TopScreen");
    Click("HUD");
    size = ImVec2(520, 400);
    Frame(); Frame();
    Find("Save TopScreen");
    Click("Controls");
    Find("Save controls");

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
    Click("Renderer");
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
    store->Fail = true;
    pending.FovMultiplier = 1.1F;
    runtime.Apply(pending);
    Check(runtime.SaveState() == GraphicsSettingsSaveState::Failed, "save failure hidden");
    store->Fail = false;
    Frame();
    Click("Retry saving graphics");
    Check(runtime.SaveState() == GraphicsSettingsSaveState::Saved, "save retry failed");
    ImGui::DestroyContext();
    std::cout << "F1 UI smoke passed: " << assertions << " assertions, real renderer/Controls/TopScreen widgets\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
}
