#pragma once
#include "fast/ApplicationSettingsPanel.h"
#include "fast/appui/SettingsModel.h"

#include "fast/oot3d/azahar_texture_pack_panel.h"
#include "fast/oot3d/grass_settings_panel.h"
#include "fast/oot3d/texture_catalog_selection.h"
#include "fast/oot3d/graphics_settings.h"

#include <memory>
#include <array>
#include <string>
#include <vector>

namespace Fast::Oot3d {

// Draw independently of F1 visibility, collapse state, tabs and scroll position.
void DrawDisplayConfirmation();

class GraphicsSettingsPanelTab {
  public:
    virtual ~GraphicsSettingsPanelTab() = default;
    [[nodiscard]] virtual const char* Label() const noexcept = 0;
    virtual void Draw() = 0;
    [[nodiscard]] virtual size_t PageCount() const noexcept { return 1; }
    [[nodiscard]] virtual const char* PageLabel(size_t) const noexcept { return Label(); }
    virtual void DrawPage(size_t) { Draw(); }
    virtual void OnHidden() {}
    virtual void AppendSettingsPages(AppUi::Pages&) {}
    virtual void UpdateSettings() {}
    [[nodiscard]] virtual bool CapturingInput() const { return false; }
    [[nodiscard]] virtual bool ReservesControllerBack() const { return false; }
};

// Game frontends may contribute application-owned settings tabs without
// making the renderer depend on frontend code.
void InstallGraphicsSettingsPanelTabs(
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> tabs);

// Backward-compatible convenience wrapper for frontends with one tab.
void InstallGraphicsSettingsPanelTab(
    std::shared_ptr<GraphicsSettingsPanelTab> tab);

bool ApplicationSettingsCapturingInput();
bool ApplicationSettingsReservesControllerBack();
void NotifyApplicationSettingsHidden();
AppUi::Pages BuildApplicationSettingsPages();
void UpdateApplicationSettings();

class GraphicsSettingsPanel final {
  public:
    // Combined entry retained for existing diagnostic widget callers only.
    void Draw();
    void DrawStandard();
    void DrawAdvanced();
    void OnHidden();

  private:
    void DrawContents(bool standard, bool advanced);
    void DrawRendererSettings(bool standard, bool advanced);
    void DrawPresentationStatus();
    bool DrawDisplaySettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawAntialiasingSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawLightingSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawReflectionSettings(GraphicsSettings&, const GraphicsCapabilities&);
    bool DrawToonSettings(GraphicsSettings&);
    void DrawGrassSettings();
    void DrawTextureSettings();

    float mPendingRenderScale = 1.0F;
    bool mRenderScaleEditing = false;
    std::array<int, 2> mPendingOutputResolution{1280, 720};
    bool mOutputResolutionEditing = false;
    GrassSettingsPanelState mGrassPanelState;
    AzaharTexturePackPanelState mTexturePackPanelState;
    TextureCatalogSelectionState mReflectionTextureSelection;
    std::string mReflectionAssignmentStatus;
    std::string mRendererStatus;
    Fast::ApplicationSettingsPanel mStandardMenu;
};

} // namespace Fast::Oot3d
