#include "fast/extension/graphics_settings_window.h"
#include "fast/extension/grass_scene_bridge.h"
#include "fast/extension/graphics_settings_runtime.h"
#include "fast/extension/settings_panel_widgets.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>

namespace Fast::Extension {
namespace {
std::atomic<bool> gDisplayConfirmationDeferred{false};

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

std::string& WindowTitle() {
    static std::string title = "OOT3D Graphics";
    return title;
}
} // namespace

void InstallGraphicsSettingsWindowTitle(std::string title) {
    std::scoped_lock lock(PanelTabMutex());
    WindowTitle() = std::move(title);
}

std::string GraphicsSettingsWindowTitle() {
    std::scoped_lock lock(PanelTabMutex());
    return WindowTitle();
}
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

void EndGraphicsSettingsPreviews() {
    GrassSceneBridge::Instance().HighlightMask(std::nullopt);
    for (const auto& tab : SnapshotPanelTabs())
        if (tab)
            tab->EndPreview();
}
void ToggleGraphicsNativePresentation() {
    GraphicsSettingsRuntime::Instance().ToggleNativePresentationOverride();
    EndGraphicsSettingsPreviews();
}

namespace {
struct HostNotice {
    std::string Text;
    bool Failure = false;
    std::chrono::steady_clock::time_point Until{};
    // A startup notice waits for its first frame.
    bool Pending = false;
};

std::chrono::seconds HostNoticeDuration(bool failure) {
    // Failures stay longer: they point the player at F1.
    return std::chrono::seconds(failure ? 8 : 3);
}
std::mutex& HostNoticeMutex() {
    static std::mutex mutex;
    return mutex;
}
HostNotice& CurrentHostNotice() {
    static HostNotice notice;
    return notice;
}
} // namespace

void PostHostNotice(std::string text, bool failure) {
    std::scoped_lock lock(HostNoticeMutex());
    auto& notice = CurrentHostNotice();
    notice.Text = std::move(text);
    notice.Failure = failure;
    notice.Pending = false;
    notice.Until = std::chrono::steady_clock::now() + HostNoticeDuration(failure);
}

void PostStartupHostNotice(std::string text, bool failure) {
    std::scoped_lock lock(HostNoticeMutex());
    auto& notice = CurrentHostNotice();
    notice.Text = std::move(text);
    notice.Failure = failure;
    notice.Pending = true;
}

bool HostNoticeActive() {
    std::scoped_lock lock(HostNoticeMutex());
    const auto& notice = CurrentHostNotice();
    return notice.Pending || std::chrono::steady_clock::now() < notice.Until;
}

void DrawHostNotice() {
    HostNotice notice;
    {
        std::scoped_lock lock(HostNoticeMutex());
        auto& current = CurrentHostNotice();
        if (current.Pending) {
            current.Pending = false;
            current.Until = std::chrono::steady_clock::now() + HostNoticeDuration(current.Failure);
        }
        notice = current;
    }
    if (std::chrono::steady_clock::now() >= notice.Until) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 16.0F, viewport->WorkPos.y + 16.0F),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75F);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##TriAevumHostNotice", nullptr, flags)) {
        if (notice.Failure) {
            ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.4F, 1.0F), "%s", notice.Text.c_str());
        } else {
            ImGui::TextUnformatted(notice.Text.c_str());
        }
    }
    ImGui::End();
}

void DeferDisplayConfirmation(bool deferred) {
    gDisplayConfirmationDeferred.store(deferred, std::memory_order_relaxed);
}

bool DisplayConfirmationDeferred() {
    return gDisplayConfirmationDeferred.load(std::memory_order_relaxed);
}

void DrawDisplayConfirmation() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    const bool awaiting = runtime.PresentationStatus().Phase == PresentationTransactionPhase::AwaitingConfirmation &&
                          !DisplayConfirmationDeferred();
    constexpr const char* title = "Keep display settings?";
    if (awaiting && !ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
    if (!awaiting && !ImGui::IsPopupOpen(title)) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0),
        ImVec2(std::max(240.0F, viewport->WorkSize.x - 16.0F), viewport->WorkSize.y - 16.0F));
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        if (!awaiting) {
            ImGui::CloseCurrentPopup();
        } else {
            runtime.PresentationConfirmationVisible();
            const auto metrics = runtime.DisplayMetrics();
            if (metrics.OutputWidth)
                ImGui::Text("Output: %u x %u", metrics.OutputWidth, metrics.OutputHeight);
            ImGui::Text("Reverting in %u seconds", (runtime.PresentationStatus().RemainingMilliseconds + 999U) / 1000U);
            if (ImGui::Button("Keep display settings")) {
                runtime.ConfirmPresentation();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Revert display settings")) {
                runtime.RollbackPresentation();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
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
        ImGui::TextUnformatted("Waiting for display confirmation...");
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
    GrassSceneBridge::Instance().HighlightMask(std::nullopt);
    const bool nativeRequired = GraphicsSettingsRuntime::Instance().NativePresentationOverrideRequired();
    if (nativeRequired) {
        ImGui::TextWrapped("Native presentation: Grass, Toon/outline, AO and reflections are unavailable in this build.");
    } else {
        ImGui::TextWrapped("F2: quick native presentation - Grass, Toon/outline, AO and reflections off.");
    }
    const bool nativeOverride = GraphicsSettingsRuntime::Instance().NativePresentationOverrideActive();
    ImGui::PushStyleColor(ImGuiCol_Text, nativeOverride ? ImVec4(1.0F, 0.78F, 0.25F, 1.0F)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped(nativeRequired ? "Vulkan/NRI active. Configured effect values are preserved." : nativeOverride
        ? "F2 override ACTIVE. Configured effects are suspended; press F2 to restore."
        : "F2 override inactive. Using configured effects.");
    ImGui::PopStyleColor();
    DrawTitleSceneViewNotice();
    ImGui::Separator();
    DrawPresentationStatus();
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.48F);
    DrawPresetSelector();
    // Application tabs: the Display and Lighting groups join the built-in
    // tabs of those names; every other group (or ungrouped tab) is a
    // top-level tab, in the order it was first installed.
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> displayTabs;
    std::vector<std::shared_ptr<GraphicsSettingsPanelTab>> lightingTabs;
    std::vector<std::pair<std::string, std::vector<std::shared_ptr<GraphicsSettingsPanelTab>>>> groups;
    for (const auto& tab : SnapshotPanelTabs()) {
        const char* group = tab->Group();
        if (group != nullptr && std::string_view(group) == kDisplaySettingsGroup) {
            displayTabs.push_back(tab);
            continue;
        }
        if (group != nullptr && std::string_view(group) == kLightingSettingsGroup) {
            lightingTabs.push_back(tab);
            continue;
        }
        const std::string name = group != nullptr ? group : tab->Label();
        auto existing = std::find_if(groups.begin(), groups.end(),
                                     [&](const auto& entry) { return entry.first == name; });
        if (existing == groups.end()) {
            groups.emplace_back(name, std::vector<std::shared_ptr<GraphicsSettingsPanelTab>>{});
            existing = std::prev(groups.end());
        }
        existing->second.push_back(tab);
    }
    const auto drawApplicationTab = [](const std::shared_ptr<GraphicsSettingsPanelTab>& tab) {
        ImGui::PushID(tab->Label());
        tab->Draw();
        ImGui::PopID();
    };
    // An unsaved preview (the scene lights before their Save) is marked on its
    // tab and its group, and named above the tabs, wherever F1 is (#68).
    const bool lightingPreview = std::any_of(lightingTabs.begin(), lightingTabs.end(),
                                             [](const auto& tab) { return tab->HasUnsavedPreview(); });
    if (lightingPreview) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.78F, 0.25F, 1.0F));
        ImGui::TextWrapped("Lighting > Lights & Shadows: %s. Save the scene lights there to keep the edits.",
                           kUnsavedPreviewMarker);
        ImGui::PopStyleColor();
    }
    if (ImGui::BeginTabBar("##Oot3dSettingsTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Display")) {
            // The renderer's sections, then the application's below them.
            ImGui::BeginChild("##DisplayContent");
            DrawSettingsSections({&GraphicsSettingsPanel::DrawDisplaySettings,
                                  &GraphicsSettingsPanel::DrawAntialiasingSettings,
                                  &GraphicsSettingsPanel::DrawMotionSettings});
            for (const auto& tab : displayTabs) drawApplicationTab(tab);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lighting", nullptr,
                                lightingPreview ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None)) {
            DrawLightingTab(lightingTabs);
            ImGui::EndTabItem();
        }
        if (!nativeRequired && ImGui::BeginTabItem("Grass")) {
            DrawGrassSettings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Materials")) {
            DrawMaterialsTab();
            ImGui::EndTabItem();
        }
        for (const auto& [name, tabs] : groups) {
            if (!ImGui::BeginTabItem(name.c_str())) continue;
            if (tabs.size() == 1U && tabs.front()->Group() == nullptr) {
                drawApplicationTab(tabs.front());
            } else if (ImGui::BeginTabBar(("##" + name + "Sections").c_str(), ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (const auto& tab : tabs) {
                    if (ImGui::BeginTabItem(tab->Label())) {
                        drawApplicationTab(tab);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
}

void GraphicsSettingsPanel::DrawTitleSceneViewNotice() {
    // Grass and the view-dependent effects keep their settings but stay
    // inactive while the title publishes no scene view.
    const auto capabilities = GraphicsSettingsRuntime::Instance().Capabilities();
    if (capabilities.Has(GraphicsCapability::TitleSceneView)) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.78F, 0.25F, 1.0F));
    ImGui::TextWrapped("%s Grass, shadows, ambient occlusion, reflections and global "
                       "illumination stay inactive; their settings are kept.",
                       capabilities.Get(GraphicsCapability::TitleSceneView).Reason.c_str());
    ImGui::PopStyleColor();
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

// The graphics preset covers every graphics tab, so it sits above them: the
// built-in presets, the user's own, then Custom. Choosing one applies it.
void GraphicsSettingsPanel::DrawPresetSelector() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    const auto userPresets = runtime.UserPresetNames();
    const auto activeUserPreset = runtime.ActiveUserPreset();
    const bool custom = settings.Preset == GraphicsPreset::Custom;
    const std::string shown = !custom ? GraphicsPresetDisplayName(settings.Preset)
                              : !activeUserPreset.empty() ? activeUserPreset : "Custom";
    const auto applyBuiltIn = [&](GraphicsPreset preset) {
        settings = GraphicsSettingsService::PresetForCurrent(preset, settings);
        mRendererStatus = SettingsUi::DescribeApply(runtime.Apply(settings));
        mRenderScaleEditing = false;
        mOutputResolutionEditing = false;
    };
    if (ImGui::BeginCombo("Graphics preset", shown.c_str())) {
        for (const auto preset : BuiltInGraphicsPresets(mPresetHost)) {
            if (ImGui::Selectable(GraphicsPresetDisplayName(preset), settings.Preset == preset)) applyBuiltIn(preset);
        }
        if (!userPresets.empty()) ImGui::SeparatorText("Your presets");
        ImGui::PushID("UserPresets");
        for (const auto& name : userPresets) {
            if (!ImGui::Selectable(name.c_str(), custom && name == activeUserPreset)) continue;
            const auto change = runtime.LoadUserPreset(name);
            mRendererStatus = change.Message;
            mPresetStatus = change.Message;
            mPresetStatusFailed = !change.Succeeded;
            mPresetPendingDelete.clear();
            mRenderScaleEditing = false;
            mOutputResolutionEditing = false;
        }
        ImGui::PopID();
        ImGui::Separator();
        if (ImGui::Selectable("Custom", custom && activeUserPreset.empty())) applyBuiltIn(GraphicsPreset::Custom);
        ImGui::EndCombo();
    }
    DrawUserPresetControls(activeUserPreset);
    if (!mRendererStatus.empty() && ImGui::TreeNode("Last change")) {
        ImGui::TextWrapped("%s", mRendererStatus.c_str());
        ImGui::TreePop();
    }
}

void GraphicsSettingsPanel::DrawUserPresetControls(const std::string& activeUserPreset) {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    if (!ImGui::TreeNode("Save or delete presets")) return;
    const auto readOnly = runtime.UserPresetsReadOnlyReason();
    if (!readOnly.empty()) {
        ImGui::TextWrapped("%s Presets cannot be saved or deleted here.", readOnly.c_str());
        ImGui::TreePop();
        return;
    }
    ImGui::TextWrapped("Built-in presets cannot be changed. Save the current settings as a preset of "
                       "your own; window, output and texture packs stay as they are when it is loaded.");
    ImGui::InputTextWithHint("Preset name", "Name of a new preset", mPresetName.data(), mPresetName.size());
    const std::string_view typed(mPresetName.data());
    const auto nameError = GraphicsPresetLibrary::ValidateName(typed);
    const bool replaces = nameError == GraphicsPresetNameError::None && runtime.HasUserPreset(typed);
    ImGui::BeginDisabled(nameError != GraphicsPresetNameError::None);
    if (ImGui::Button(replaces ? "Overwrite preset" : "Save preset")) {
        const auto change = runtime.SaveUserPreset(typed);
        mPresetStatus = change.Message;
        mPresetStatusFailed = !change.Succeeded;
        mPresetPendingDelete.clear();
        if (change.Succeeded) mPresetName.fill('\0');
    }
    ImGui::EndDisabled();
    if (nameError != GraphicsPresetNameError::None && nameError != GraphicsPresetNameError::Empty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.45F, 0.35F, 1.0F), "%s", GraphicsPresetLibrary::DescribeNameError(nameError));
    } else if (replaces) {
        ImGui::SameLine();
        ImGui::TextDisabled("Replaces your preset of this name.");
    }

    if (activeUserPreset.empty()) {
        ImGui::BeginDisabled();
        ImGui::Button("Delete preset");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Choose one of your presets above to delete it.");
    } else if (mPresetPendingDelete != activeUserPreset) {
        if (ImGui::Button("Delete preset")) mPresetPendingDelete = activeUserPreset;
        ImGui::SameLine();
        ImGui::TextDisabled("\"%s\"", activeUserPreset.c_str());
    } else {
        if (ImGui::Button("Confirm delete")) {
            const auto change = runtime.DeleteUserPreset(activeUserPreset);
            mPresetStatus = change.Message;
            mPresetStatusFailed = !change.Succeeded;
            mPresetPendingDelete.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) mPresetPendingDelete.clear();
        ImGui::SameLine();
        ImGui::TextWrapped("Delete \"%s\"? The current settings stay as they are.", activeUserPreset.c_str());
    }
    if (!mPresetStatus.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, mPresetStatusFailed ? ImVec4(1.0F, 0.45F, 0.35F, 1.0F)
                                                                 : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", mPresetStatus.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TreePop();
}

void GraphicsSettingsPanel::DrawSettingsPage(const char* id, std::initializer_list<Section> sections) {
    ImGui::BeginChild(id);
    DrawSettingsSections(sections);
    ImGui::EndChild();
}

void GraphicsSettingsPanel::DrawSettingsSections(std::initializer_list<Section> sections) {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    const auto capabilities = runtime.Capabilities();
    bool changed = false;
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.48F);
    for (const auto section : sections) changed |= (this->*section)(settings, capabilities);
    ImGui::PopItemWidth();
    if (changed) {
        settings.Preset = GraphicsPreset::Custom;
        mRendererStatus = SettingsUi::DescribeApply(
            runtime.Apply(settings, !ImGui::IsAnyItemActive()));
    }
}

void GraphicsSettingsPanel::DrawLightingTab(
    const std::vector<std::shared_ptr<GraphicsSettingsPanelTab>>& lightingTabs) {
    if (!ImGui::BeginTabBar("##LightingSections", ImGuiTabBarFlags_FittingPolicyScroll)) return;
    const auto section = [&](const char* label, Section draw) {
        if (ImGui::BeginTabItem(label)) {
            DrawSettingsPage(label, {draw});
            ImGui::EndTabItem();
        }
    };
    section("Lighting & GI", &GraphicsSettingsPanel::DrawLightingSettings);
    // Lights & Shadows: the application's scene lights (saved per scene) above
    // the renderer's shadows (every scene). Both act only in the Modern model,
    // so the section exists only while it is chosen.
    const bool preview = std::any_of(lightingTabs.begin(), lightingTabs.end(),
                                     [](const auto& tab) { return tab->HasUnsavedPreview(); });
    if (UsesFullSceneLighting(GraphicsSettingsRuntime::Instance().Snapshot().Effects.SurfaceLighting) &&
        ImGui::BeginTabItem("Lights & Shadows", nullptr,
                            preview ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None)) {
        ImGui::BeginChild("##LightsAndShadows");
        if (preview) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 0.78F, 0.25F, 1.0F));
            ImGui::TextUnformatted(kUnsavedPreviewMarker);
            ImGui::PopStyleColor();
        }
        for (const auto& tab : lightingTabs) {
            ImGui::PushID(tab->Label());
            tab->Draw();
            ImGui::PopID();
        }
        DrawSettingsSections({&GraphicsSettingsPanel::DrawShadowSettings});
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    section("Ambient Occlusion", &GraphicsSettingsPanel::DrawAmbientOcclusionSettings);
    section("Reflections", &GraphicsSettingsPanel::DrawReflectionSettings);
    section("Toon", &GraphicsSettingsPanel::DrawToonSettings);
    ImGui::EndTabBar();
}

void GraphicsSettingsPanel::DrawMaterialsTab() {
    if (!ImGui::BeginTabBar("##MaterialSections", ImGuiTabBarFlags_FittingPolicyScroll)) return;
    if (ImGui::BeginTabItem("Texture packs")) {
        ImGui::BeginChild("##TextureContent");
        DrawTextureSettings();
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Material editor")) {
        ImGui::BeginChild("##MaterialEditorContent");
        DrawPbrMaterialEditor(mMaterialEditorState);
        ImGui::EndChild();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Reflection materials")) {
        DrawSettingsPage("##ReflectionMaterialContent", {&GraphicsSettingsPanel::DrawReflectionMaterialSettings});
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
} // namespace Fast::Extension
