#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"
#include <mutex>

namespace Fast::Oot3d {
namespace {
std::mutex& PanelTabMutex() {
    static std::mutex mutex;
    return mutex;
}

std::vector<std::shared_ptr<GraphicsSettingsPanelTab>>& PanelTabs() {
    static std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> tabs;
    return tabs;
}

std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> SnapshotPanelTabs() {
    std::scoped_lock lock(PanelTabMutex());
    return PanelTabs();
}
} // namespace
void InstallGraphicsSettingsPanelTabs(
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> tabs) {
    std::erase(tabs, nullptr);
    std::scoped_lock lock(PanelTabMutex());
    PanelTabs() = std::move(tabs);
}

void InstallGraphicsSettingsPanelTab(
    std::shared_ptr<GraphicsSettingsPanelTab> tab) {
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> tabs;
    if (tab != nullptr) {
        tabs.push_back(std::move(tab));
    }
    InstallGraphicsSettingsPanelTabs(std::move(tabs));
}
void GraphicsSettingsPanel::DrawPresentationStatus() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    runtime.TickPresentation();
    if (!ImGui::IsAnyItemActive()) runtime.SavePending();
    const auto status = runtime.PresentationStatus();
    const auto rejection = runtime.LastPresentationRejection();
    if (!rejection.empty()) {
        ImGui::TextWrapped("Display change reverted: %s", rejection.c_str());
    }
    if (status.Phase == PresentationTransactionPhase::AwaitingConfirmation) {
        ImGui::Text("Confirm display change (%u s)",
                    (status.RemainingMilliseconds + 999U) / 1000U);
        if (ImGui::Button("Keep display settings")) runtime.ConfirmPresentation();
        ImGui::SameLine();
        if (ImGui::Button("Revert display settings")) runtime.RollbackPresentation();
        ImGui::Separator();
    } else if (status.Phase == PresentationTransactionPhase::ApplyRequested) {
        ImGui::TextUnformatted("Applying display settings...");
    } else if (status.Phase == PresentationTransactionPhase::RollbackRequested) {
        ImGui::TextUnformatted("Restoring previous display settings...");
    }
    switch (runtime.SaveState()) {
        case GraphicsSettingsSaveState::Failed:
            ImGui::TextWrapped("Settings applied, but could not be saved.");
            if (ImGui::Button("Retry saving graphics")) runtime.RetrySave();
            break;
        case GraphicsSettingsSaveState::SessionOnly:
            ImGui::TextDisabled("Graphics: session only");
            break;
        case GraphicsSettingsSaveState::Pending:
            ImGui::TextDisabled("Graphics: unsaved");
            break;
        case GraphicsSettingsSaveState::Saved:
            ImGui::TextDisabled("Graphics: saved");
            break;
        case GraphicsSettingsSaveState::WaitingForDisplay:
            break;
    }
}

void GraphicsSettingsPanel::Draw() {
    const bool nativeRequired = GraphicsSettingsRuntime::Instance().NativePresentationOverrideRequired();
    if (nativeRequired) {
        ImGui::TextWrapped("Native presentation: Grass, Toon/outline, CACAO and reflections are unavailable in this build.");
    } else {
        ImGui::TextWrapped("F2: quick native presentation - Grass, Toon/outline, CACAO and reflections off.");
    }
    const bool nativeOverride = GraphicsSettingsRuntime::Instance().NativePresentationOverrideActive();
    ImGui::PushStyleColor(ImGuiCol_Text, nativeOverride ? ImVec4(1.0F, 0.78F, 0.25F, 1.0F)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(nativeRequired ? "Vulkan/NRI active. Configured effect values are preserved." : nativeOverride
        ? "F2 override ACTIVE. Configured effects are suspended; press F2 to restore."
        : "F2 override inactive. Using configured effects.");
    ImGui::PopStyleColor();
    ImGui::Separator();
    DrawPresentationStatus();
    const auto applicationTabs = SnapshotPanelTabs();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.48F);
    if (ImGui::BeginTabBar("##Oot3dSettingsTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Renderer")) {
            DrawRendererSettings();
            ImGui::EndTabItem();
        }
        if (!nativeRequired && ImGui::BeginTabItem("Grass")) {
            DrawGrassSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Textures")) {
            ImGui::BeginChild("##TextureContent");
            DrawTextureSettings();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        for (const auto& tab : applicationTabs) {
            if (tab != nullptr && ImGui::BeginTabItem(tab->Label())) {
                ImGui::PushID(tab->Label());
                tab->Draw();
                ImGui::PopID();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
}

void GraphicsSettingsPanel::DrawGrassSettings() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    if (DrawGrassSettingsPanel(settings.Grass, settings.GrassSavedPreset, mGrassPanelState)) {
        settings.Preset = GraphicsPreset::Custom;
        mGrassPanelState.Status = SettingsUi::DescribeApply(
            runtime.Apply(settings, !ImGui::IsAnyItemActive()));
    }
}

void GraphicsSettingsPanel::DrawTextureSettings() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    if (DrawAzaharTexturePackPanel(settings.TexturePacks.Azahar, mTexturePackPanelState)) {
        settings.Preset = GraphicsPreset::Custom;
        mTexturePackPanelState.Status = SettingsUi::DescribeApply(runtime.Apply(settings));
    }
}

void GraphicsSettingsPanel::DrawRendererSettings() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    const auto capabilities = runtime.Capabilities();
    const char* const presets[] = {"Authentic", "Enhanced", "Toon", "Custom"};
    GraphicsPreset preset = settings.Preset;
    if (SettingsUi::EnumCombo("Preset", preset, presets)) {
        settings = GraphicsSettingsService::PresetForCurrent(preset, settings);
        mRendererStatus = SettingsUi::DescribeApply(runtime.Apply(settings));
        settings = runtime.Snapshot();
        mRenderScaleEditing = false;
        mOutputResolutionEditing = false;
    }
    if (!mRendererStatus.empty() && ImGui::TreeNode("Last change")) {
        ImGui::TextWrapped("%s", mRendererStatus.c_str());
        ImGui::TreePop();
    }

    bool changed = false;
    if (ImGui::BeginTabBar("##RendererSections", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const auto section = [&](const char* label, auto draw) {
            if (ImGui::BeginTabItem(label)) {
                ImGui::BeginChild(label);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.48F);
                changed |= draw();
                ImGui::PopItemWidth();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        };
        section("Display", [&] { return DrawDisplaySettings(settings, capabilities); });
        section("Antialiasing", [&] { return DrawAntialiasingSettings(settings, capabilities); });
        section("Lighting", [&] { return DrawLightingSettings(settings, capabilities); });
        section("Reflections", [&] { return DrawReflectionSettings(settings, capabilities); });
        section("Toon", [&] { return DrawToonSettings(settings); });
        ImGui::EndTabBar();
    }
    if (changed) {
        settings.Preset = GraphicsPreset::Custom;
        mRendererStatus = SettingsUi::DescribeApply(
            runtime.Apply(settings, !ImGui::IsAnyItemActive()));
    }
}
} // namespace Fast::Oot3d
