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
bool ApplicationSettingsCapturingInput() {
    for (const auto& tab : SnapshotPanelTabs()) if (tab->CapturingInput()) return true;
    return false;
}
bool ApplicationSettingsReservesControllerBack() {
    for (const auto& tab : SnapshotPanelTabs()) if (tab->ReservesControllerBack()) return true;
    return false;
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
void DrawDisplayConfirmation() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    const bool awaiting = runtime.PresentationStatus().Phase == PresentationTransactionPhase::AwaitingConfirmation;
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
    DrawContents(true, true);
}

void GraphicsSettingsPanel::DrawStandard() {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    const bool native = runtime.NativePresentationOverrideActive();
    ImGui::TextColored(native ? ImVec4(1.0F, 0.78F, 0.25F, 1.0F) :
                       ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                       "F2: native presentation %s", native ? "ON" : "OFF");
    DrawPresentationStatus();
    std::vector<Fast::ApplicationSettingsPage> pages;
    const auto rendererPage = [&](const char* id, const char* label, auto draw) {
        pages.push_back({id, label, "Graphics", [this, draw] {
            auto& runtime = GraphicsSettingsRuntime::Instance();
            auto settings = runtime.Snapshot();
            if (draw(settings, runtime.Capabilities())) {
                settings.Preset = GraphicsPreset::Custom;
                mRendererStatus = SettingsUi::DescribeApply(runtime.Apply(settings, !ImGui::IsAnyItemActive()));
            }
            if (!mRendererStatus.empty()) ImGui::TextWrapped("%s", mRendererStatus.c_str());
        }});
    };
    rendererPage("display", "Display", [this](auto& settings, const auto& caps) {
        return DrawDisplaySettings(settings, caps);
    });
    rendererPage("antialiasing", "Antialiasing", [this](auto& settings, const auto& caps) {
        return DrawAntialiasingSettings(settings, caps);
    });
    for (const auto& tab : SnapshotPanelTabs()) {
        for (size_t index = 0; index < tab->PageCount(); ++index) {
            pages.push_back({std::string(tab->Label()) + "/" + std::to_string(index),
                             tab->PageLabel(index), tab->Label(),
                             [tab, index] { tab->DrawPage(index); }});
        }
    }
    mStandardMenu.Draw(pages);
}

void GraphicsSettingsPanel::OnHidden() {
    NotifyApplicationSettingsHidden();
}

void NotifyApplicationSettingsHidden() {
    for (const auto& tab : SnapshotPanelTabs()) tab->OnHidden();
}

AppUi::Pages BuildApplicationSettingsPages() {
    using namespace AppUi;
    Pages pages;
    const auto read=[] {return GraphicsSettingsRuntime::Instance().Snapshot();};
    const auto normalize=[](GraphicsSettings& config) {
        const auto capabilities=GraphicsSettingsRuntime::Instance().Capabilities();
        if(config.AntiAliasing==AntiAliasingMode::Msaa && config.MsaaSamples<2)config.MsaaSamples=2;
        if(config.AntiAliasing==AntiAliasingMode::Upscaler &&
            GraphicsSettingsService::Validate(config,capabilities).Value.AntiAliasing!=AntiAliasingMode::Upscaler) {
            for(auto provider:{UpscalerProvider::Nis,UpscalerProvider::Fsr,UpscalerProvider::Xess,UpscalerProvider::Dlss}) {
                auto probe=config;probe.Upscaler=provider;
                if(GraphicsSettingsService::Validate(probe,capabilities).Value.AntiAliasing==AntiAliasingMode::Upscaler){config.Upscaler=provider;break;}
            }
        }
    };
    const auto write=[](GraphicsSettings config) {
        auto& runtime=GraphicsSettingsRuntime::Instance();
        const auto result=runtime.Apply(config);
        std::string errors;
        for(const auto& issue:result.Issues) errors += issue.Field+": "+issue.Message+" ";
        if(runtime.SaveState()==GraphicsSettingsSaveState::Failed) errors+="Could not save settings.";
        return errors;
    };
    Page display{"display","Display",{}};
    auto add=[&](auto member,const char* id,const char* label,double lo,double hi,double step=1,std::vector<Option> options={}) {
        auto field=Member(id,label,member,read,[write,normalize](GraphicsSettings config){normalize(config);return write(config);},lo,hi,step,std::move(options));
        if(!field.Options.empty())field.UnavailableReason=[read,member,normalize](const std::string& value) {
            using Value=std::remove_cvref_t<decltype(read().*member)>;
            auto probe=read();const auto requested=static_cast<Value>(std::stoi(value));probe.*member=requested;normalize(probe);
            const auto checked=GraphicsSettingsService::Validate(probe,GraphicsSettingsRuntime::Instance().Capabilities());
            if(checked.Value.*member==requested)return std::string();
            std::string reason="Unavailable on this renderer";
            for(const auto& issue:checked.Issues)reason+="; "+issue.Message;
            return reason;
        };
        display.Fields.push_back(std::move(field));
    };
    add(&GraphicsSettings::Window,"window","Window mode",0,2,1,{{"0","Windowed"},{"1","Borderless"},{"2","Exclusive fullscreen"}});
    add(&GraphicsSettings::DisplayIndex,"monitor","Display index",0,16);
    add(&GraphicsSettings::OutputWidth,"width","Output width",320,16384);
    add(&GraphicsSettings::OutputHeight,"height","Output height",240,16384);
    add(&GraphicsSettings::RefreshRate,"refresh","Refresh rate",24,360);
    add(&GraphicsSettings::InternalResolutionScale,"scale","Internal resolution scale",.25,8,.25);
    add(&GraphicsSettings::FrameRate,"fps","Frame presentation",0,3,1,{{"0","Native 30"},{"1","Interpolated 60"},{"2","Interpolated 90"},{"3","Uncapped"}});
    add(&GraphicsSettings::VSync,"vsync","VSync",0,1);
    add(&GraphicsSettings::FovMultiplier,"fov","Scene FOV multiplier",.5,2,.05);
    display.Fields.push_back({"actual","Actual output / scene",FieldKind::Text,[]{
        const auto s=GraphicsSettingsRuntime::Instance().DisplayMetrics();
        return std::to_string(s.OutputWidth)+" x "+std::to_string(s.OutputHeight)+" / "+std::to_string(s.SceneWidth)+" x "+std::to_string(s.SceneHeight);
    }});
    display.Fields.push_back({"saved","Settings persistence",FieldKind::Text,[]{
        return GraphicsSettingsRuntime::Instance().SaveState()==GraphicsSettingsSaveState::Failed
            ? "Could not save settings. Check available disk space and file permissions." : "Automatic";
    }});
    display.Fields.push_back({"native","F2: toggle native presentation",FieldKind::Text,[]{
        return GraphicsSettingsRuntime::Instance().NativePresentationOverrideActive()
            ? "Active: configured scene effects are suspended" : "Inactive: configured scene effects are enabled";
    }});
    pages.push_back(std::move(display)); display={"aa","Antialiasing",{}};
    add(&GraphicsSettings::AntiAliasing,"aa","Antialiasing",0,5,1,{{"0","Off"},{"1","FXAA"},{"2","SMAA"},{"3","MSAA"},{"4","TAA"},{"5","Upscaler"}});
    add(&GraphicsSettings::MsaaSamples,"samples","MSAA samples",1,8,1,{{"1","1"},{"2","2"},{"4","4"},{"8","8"}});
    add(&GraphicsSettings::TaaHistoryWeight,"history","TAA history weight",0,.99,.01);
    add(&GraphicsSettings::TaaClampExpansion,"clamp","TAA clamp expansion",0,1,.01);
    add(&GraphicsSettings::TaaSharpness,"sharpness","TAA sharpness",0,1,.01);
    add(&GraphicsSettings::UpscalerSharpness,"upsharp","Upscaler sharpness",0,1,.01);
    add(&GraphicsSettings::Upscaler,"provider","Upscaler provider",0,3,1,{{"0","NIS"},{"1","FSR"},{"2","XeSS"},{"3","DLSS"}});
    add(&GraphicsSettings::UpscalerMode,"quality","Upscaler quality",0,5,1,{{"0","Native"},{"1","Ultra quality"},{"2","Quality"},{"3","Balanced"},{"4","Performance"},{"5","Ultra performance"}});
    for(auto& field:display.Fields) {
        if(field.Id=="samples")field.Enabled=[read]{return read().AntiAliasing==AntiAliasingMode::Msaa;};
        if(field.Id=="history"||field.Id=="clamp"||field.Id=="sharpness")field.Enabled=[read]{return read().AntiAliasing==AntiAliasingMode::Taa;};
        if(field.Id=="provider"||field.Id=="quality")field.Enabled=[read]{return read().AntiAliasing==AntiAliasingMode::Upscaler;};
        if(field.Id=="upsharp")field.Enabled=[read]{auto s=read();return s.AntiAliasing==AntiAliasingMode::Upscaler&&(s.Upscaler==UpscalerProvider::Nis||s.Upscaler==UpscalerProvider::Fsr);};
    }
    pages.push_back(std::move(display));
    for(const auto& tab:SnapshotPanelTabs()) tab->AppendSettingsPages(pages);
    return pages;
}
void UpdateApplicationSettings() {
    for(const auto& tab:SnapshotPanelTabs()) tab->UpdateSettings();
}

void GraphicsSettingsPanel::DrawAdvanced() {
    DrawContents(false, true);
}

void GraphicsSettingsPanel::DrawContents(bool standard, bool advanced) {
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
            DrawRendererSettings(standard, advanced);
            ImGui::EndTabItem();
        }
        if (advanced && !nativeRequired && ImGui::BeginTabItem("Grass")) {
            DrawGrassSettings();
            ImGui::EndTabItem();
        }
        if (advanced && ImGui::BeginTabItem("Textures")) {
            ImGui::BeginChild("##TextureContent");
            DrawTextureSettings();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (standard) for (const auto& tab : applicationTabs) {
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

void GraphicsSettingsPanel::DrawRendererSettings(bool standard, bool advanced) {
    auto& runtime = GraphicsSettingsRuntime::Instance();
    auto settings = runtime.Snapshot();
    const auto capabilities = runtime.Capabilities();
    const char* const presets[] = {"Authentic", "Enhanced", "Toon", "Custom"};
    GraphicsPreset preset = settings.Preset;
    if (advanced && SettingsUi::EnumCombo("Preset", preset, presets)) {
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
        if (standard) {
            section("Display", [&] { return DrawDisplaySettings(settings, capabilities); });
            section("Antialiasing", [&] { return DrawAntialiasingSettings(settings, capabilities); });
        }
        if (advanced) {
            section("Lighting", [&] { return DrawLightingSettings(settings, capabilities); });
            section("Reflections", [&] { return DrawReflectionSettings(settings, capabilities); });
            section("Toon", [&] { return DrawToonSettings(settings); });
        }
        ImGui::EndTabBar();
    }
    if (changed) {
        settings.Preset = GraphicsPreset::Custom;
        mRendererStatus = SettingsUi::DescribeApply(
            runtime.Apply(settings, !ImGui::IsAnyItemActive()));
    }
}
} // namespace Fast::Oot3d
