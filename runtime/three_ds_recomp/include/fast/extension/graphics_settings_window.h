#pragma once

#include "fast/extension/azahar_texture_pack_panel.h"
#include "fast/extension/grass_settings_panel.h"
#include "fast/extension/texture_catalog_selection.h"
#include "fast/extension/graphics_preset_library.h"
#include "fast/extension/graphics_settings.h"
#include "fast/extension/pbr_material_editor.h"

#include <memory>
#include <array>
#include <initializer_list>
#include <string>
#include <vector>

namespace Fast::Extension {

// Draw independently of F1 visibility, collapse state, tabs and scroll position.
void DrawDisplayConfirmation();
// While another surface (the in-game menu) asks the "keep display settings"
// question, the ImGui confirmation stays hidden so the question is asked once.
void DeferDisplayConfirmation(bool deferred);
[[nodiscard]] bool DisplayConfirmationDeferred();

// A short host notice (for example the result of a hotkey action) shown for a
// few seconds outside F1. A later notice replaces the current one.
void PostHostNotice(std::string text, bool failure = false);
// A notice found during startup (before the first frame): its time starts
// when it is first drawn, so a long load does not hide it.
void PostStartupHostNotice(std::string text, bool failure = false);
[[nodiscard]] bool HostNoticeActive();
void DrawHostNotice();

// The F1 groups that application tabs can join. Display and Lighting are
// built-in tabs; other group names become top-level tabs in the order they
// first appear.
inline constexpr const char* kDisplaySettingsGroup = "Display";
inline constexpr const char* kLightingSettingsGroup = "Lighting";
inline constexpr const char* kGameSettingsGroup = "Game";

class GraphicsSettingsPanelTab {
  public:
    virtual ~GraphicsSettingsPanelTab() = default;
    [[nodiscard]] virtual const char* Label() const noexcept = 0;
    // Tabs naming a group are sections of that top-level tab; without one,
    // the tab is top-level under its own label.
    [[nodiscard]] virtual const char* Group() const noexcept { return nullptr; }
    virtual void Draw() = 0;
    // Session-only previews end when F1 closes or F2 is pressed.
    virtual void EndPreview() {}
    // Edits shown as a preview but not saved yet (the scene lights before
    // their Save): F1 marks the tab and its group "Preview, not saved" until
    // they are saved or reverted, wherever the user is in F1 (#68).
    [[nodiscard]] virtual bool HasUnsavedPreview() const { return false; }
};

// The marker F1 shows while a tab has an unsaved preview.
inline constexpr const char* kUnsavedPreviewMarker = "Preview, not saved";

// Game frontends may contribute application-owned settings tabs without
// making the renderer depend on frontend code.
void InstallGraphicsSettingsPanelTabs(
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> tabs);

// Backward-compatible convenience wrapper for frontends with one tab.
void InstallGraphicsSettingsPanelTab(
    std::shared_ptr<GraphicsSettingsPanelTab> tab);
void EndGraphicsSettingsPreviews();
// The host F2 action, including ending application-owned previews.
void ToggleGraphicsNativePresentation();

// The F1 window's title, named for the running game. The game frontend
// installs it before the GUI is created; the default is "OOT3D Graphics".
void InstallGraphicsSettingsWindowTitle(std::string title);
[[nodiscard]] std::string GraphicsSettingsWindowTitle();

class GraphicsSettingsPanel final {
  public:
    void Draw();

  private:
    using Section = bool (GraphicsSettingsPanel::*)(GraphicsSettings&, const GraphicsCapabilities&);
    void DrawPresetSelector();
    // Save as / Delete for the user's presets, below the selector.
    void DrawUserPresetControls(const std::string& activeUserPreset);
    // One page of renderer settings: its sections in order, applied together.
    void DrawSettingsPage(const char* id, std::initializer_list<Section> sections);
    // The same sections without their own scrolling region.
    void DrawSettingsSections(std::initializer_list<Section> sections);
    void DrawLightingTab(const std::vector<std::shared_ptr<GraphicsSettingsPanelTab>>& lightingTabs);
    void DrawMaterialsTab();
    void DrawPresentationStatus();
    // Why the view-dependent effects are inactive, when the title publishes
    // no scene view.
    void DrawTitleSceneViewNotice();
    bool DrawDisplaySettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawAntialiasingSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawMotionSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawLightingSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawShadowSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawAmbientOcclusionSettings(GraphicsSettings&, const GraphicsCapabilities&);
    // MXAO's controls, labelled with the texts of the iMMERSE module
    // (fast/extension/mxao_settings_panel.cpp); none without the module.
    bool DrawMxaoSettings(EffectsSettings&);
    // MXAO's credit and licence exception, at the foot of Ambient Occlusion.
    void DrawMxaoCredits();
    bool DrawReflectionSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawReflectionMaterialSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawToonSettings(GraphicsSettings&, const GraphicsCapabilities&);
    void DrawGrassSettings();
    void DrawTextureSettings();

    float mPendingRenderScale = 1.0F;
    bool mRenderScaleEditing = false;
    std::array<int, 2> mPendingOutputResolution{1280, 720};
    bool mOutputResolutionEditing = false;
    GrassSettingsPanelState mGrassPanelState;
    AzaharTexturePackPanelState mTexturePackPanelState;
    PbrMaterialEditorState mMaterialEditorState;
    TextureCatalogSelectionState mReflectionTextureSelection;
    std::string mReflectionAssignmentStatus;
    std::string mRendererStatus;
    std::array<char, kMaximumGraphicsPresetNameLength + 1U> mPresetName{};
    // The user preset awaiting a second, confirming click on Delete.
    std::string mPresetPendingDelete;
    std::string mPresetStatus;
    bool mPresetStatusFailed = false;
};

} // namespace Fast::Extension
