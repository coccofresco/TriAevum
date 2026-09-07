#include "fast/oot3d/grass_settings_panel.h"

#include "fast/oot3d/grass_blade_geometry.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "fast/oot3d/grass_render_telemetry.h"
#include "fast/oot3d/grass_texture_rule_editor.h"
#include "fast/oot3d/texture_assignment_model.h"
#include "fast/oot3d/texture_catalog_viewer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace Fast::Oot3d {
namespace {

template <typename T, size_t N>
bool EnumCombo(
    const char* label, T& value,
    const char* const (&labels)[N]) {
    int selected = static_cast<int>(value);
    if (!ImGui::Combo(
            label, &selected, labels, static_cast<int>(N))) {
        return false;
    }
    value = static_cast<T>(selected);
    return true;
}

bool DrawSourcesAndPlacement(
    InteractiveGrassSettings& grass,
    GrassSettingsPanelState& state) {
    bool changed = false;
    std::vector<TextureCatalogTag> tags;
    tags.reserve(grass.Rules.size());
    for (const auto& rule : grass.Rules) {
        tags.push_back(
            {rule.Target.Rgba8Hash, rule.Target.Width,
             rule.Target.Height, "GRASS"});
    }

    auto findSelectedSource = [&]() -> GrassPlacementRule* {
        const auto found = std::find_if(
            grass.Rules.begin(), grass.Rules.end(),
            [&](const auto& rule) {
                return rule.RuleId ==
                    state.SelectedSourceRuleId;
            });
        return found == grass.Rules.end() ? nullptr : &*found;
    };
    if (findSelectedSource() == nullptr &&
        !grass.Rules.empty()) {
        state.SelectedSourceRuleId =
            grass.Rules.front().RuleId;
    }

    ImGui::SeparatorText("Configured texture sources");
    ImGui::Text(
        "Active source textures: %zu", grass.Rules.size());
    if (grass.Rules.empty()) {
        ImGui::TextDisabled(
            "Add one or more observed textures below.");
    } else {
        auto* selectedSource = findSelectedSource();
        char selectedLabel[144] = "Select source";
        if (selectedSource != nullptr) {
            std::snprintf(
                selectedLabel, sizeof(selectedLabel),
                "%016llX | %ux%u",
                static_cast<unsigned long long>(
                    selectedSource->Target.Rgba8Hash),
                selectedSource->Target.Width,
                selectedSource->Target.Height);
        }
        if (ImGui::BeginCombo(
                "Configured source", selectedLabel)) {
            for (const auto& rule : grass.Rules) {
                char label[144]{};
                std::snprintf(
                    label, sizeof(label),
                    "%016llX | %ux%u",
                    static_cast<unsigned long long>(
                        rule.Target.Rgba8Hash),
                    rule.Target.Width, rule.Target.Height);
                const bool selected =
                    rule.RuleId ==
                    state.SelectedSourceRuleId;
                ImGui::PushID(rule.RuleId);
                if (ImGui::Selectable(label, selected)) {
                    state.SelectedSourceRuleId =
                        rule.RuleId;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        selectedSource = findSelectedSource();
        if (selectedSource != nullptr) {
            const uint32_t selectedRuleId =
                selectedSource->RuleId;
            if (ImGui::Button("Remove configured source")) {
                std::erase_if(
                    grass.Rules,
                    [selectedRuleId](const auto& rule) {
                        return rule.RuleId == selectedRuleId;
                    });
                state.SelectedSourceRuleId =
                    grass.Rules.empty()
                    ? 0U
                    : grass.Rules.front().RuleId;
                state.Status = "Texture removed from grass";
                changed = true;
                selectedSource = nullptr;
            }
        }
        if (selectedSource != nullptr) {
            changed |=
                DrawGrassTextureRuleControls(*selectedSource);
        }
    }

    ImGui::SeparatorText("Add from observed textures");
    const auto catalog = DrawTextureCatalogViewer(
        state.TextureSelection, tags);
    if (catalog.SelectionChanged && catalog.Selected.has_value()) {
        char status[96]{};
        std::snprintf(
            status, sizeof(status),
            "Selected %016llX (%ux%u, format %u)",
            static_cast<unsigned long long>(
                catalog.Selected->ContentHash),
            catalog.Selected->Width, catalog.Selected->Height,
            catalog.Selected->NativeFormat);
        state.Status = status;
    }
    if (!catalog.Selected.has_value()) {
        ImGui::TextDisabled(
            "Select an observed texture to define a grass source.");
        return changed;
    }

    const auto& texture = *catalog.Selected;
    bool assigned =
        FindGrassTextureRule(grass, texture) != nullptr;
    if (!assigned) {
        if (ImGui::Button("Add selected texture source")) {
            changed |= AssignTextureToGrass(grass, texture);
            if (const auto* source =
                    FindGrassTextureRule(grass, texture)) {
                state.SelectedSourceRuleId = source->RuleId;
            }
            state.Status = "Texture assigned to grass";
        }
    } else if (ImGui::Button("Edit configured source")) {
        const auto* source =
            FindGrassTextureRule(grass, texture);
        state.SelectedSourceRuleId =
            source != nullptr ? source->RuleId : 0U;
        state.Status = "Configured source selected";
    } else {
        ImGui::TextDisabled(
            "This texture is already configured as a source.");
    }
    return changed;
}

bool DrawGeneration(InteractiveGrassSettings& grass) {
    auto& generation = grass.Generation;
    bool changed = false;
    ImGui::SeparatorText("Coverage");
    changed |= ImGui::SliderFloat(
        "Blades per square metre",
        &generation.InstancesPerSquareMeter, 0.0F,
        kMaximumGrassInstancesPerSquareMeter, "%.1f",
        ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Minimum spacing", &generation.MinimumSpacing,
        0.0F, 100.0F, "%.2f");

    ImGui::SeparatorText("Distribution");
    changed |= ImGui::SliderFloat(
        "Position randomness",
        &generation.IndividualRandomness,
        0.0F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Cluster strength", &generation.ClusterStrength,
        0.0F, 1.0F, "%.2f");
    ImGui::BeginDisabled(
        generation.ClusterStrength <= 0.0F);
    changed |= ImGui::SliderFloat(
        "Cluster scale", &generation.ClusterScale,
        10.0F, 3000.0F, "%.0f world units",
        ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Cluster coverage", &generation.ClusterCoverage,
        0.01F, 1.0F, "%.2f");
    ImGui::EndDisabled();
    changed |= ImGui::InputScalar(
        "Distribution seed", ImGuiDataType_U32,
        &generation.Seed);

    ImGui::SeparatorText("Blade geometry");
    std::array<float, 2> bladeHeight{
        generation.BladeHeightMin,
        generation.BladeHeightMax};
    if (ImGui::SliderFloat2(
            "Blade height range", bladeHeight.data(),
            0.01F, 200.0F, "%.2f")) {
        generation.BladeHeightMin = bladeHeight[0];
        generation.BladeHeightMax =
            std::max(bladeHeight[0], bladeHeight[1]);
        changed = true;
    }
    std::array<float, 2> bladeWidth{
        generation.BladeWidthMin,
        generation.BladeWidthMax};
    if (ImGui::SliderFloat2(
            "Blade width range", bladeWidth.data(),
            0.01F, 50.0F, "%.2f")) {
        generation.BladeWidthMin = bladeWidth[0];
        generation.BladeWidthMax =
            std::max(bladeWidth[0], bladeWidth[1]);
        changed = true;
    }
    return changed;
}

bool DrawAppearance(InteractiveGrassSettings& grass) {
    bool changed = false;
    changed |= ImGui::ColorEdit3(
        "Root color", grass.Appearance.RootColor.data());
    changed |= ImGui::ColorEdit3(
        "Tip color", grass.Appearance.TipColor.data());
    changed |= ImGui::SliderFloat(
        "Texture color blend",
        &grass.Appearance.TextureColorInfluence,
        0.0F, 1.0F, "%.2f");
    std::array<float, 2> textureBrightness{
        grass.Appearance.TextureRootBrightness,
        grass.Appearance.TextureTipBrightness};
    if (ImGui::SliderFloat2(
            "Texture brightness range",
            textureBrightness.data(), 0.0F, 4.0F, "%.2f")) {
        grass.Appearance.TextureRootBrightness =
            textureBrightness[0];
        grass.Appearance.TextureTipBrightness =
            textureBrightness[1];
        changed = true;
    }
    changed |= ImGui::SliderFloat(
        "Master height scale",
        &grass.Appearance.HeightScale,
        0.05F, 8.0F, "%.2fx",
        ImGuiSliderFlags_Logarithmic);
    ImGui::SeparatorText("Blade shape");
    changed |= ImGui::SliderFloat("Curvature", &grass.Appearance.BladeCurvature,
                                   0.0F, 2.0F, "%.2f");
    changed |= ImGui::SliderFloat("Tip droop", &grass.Appearance.BladeDroop,
                                   0.0F, 0.95F, "%.2f");
    changed |= ImGui::SliderFloat("Shape irregularity", &grass.Appearance.ShapeVariation,
                                   0.0F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat("Blade twist", &grass.Appearance.BladeTwistDegrees,
                                   0.0F, 180.0F, "%.0f deg");
    changed |= ImGui::Checkbox(
        "Receive native lighting",
        &grass.Appearance.ReceiveLighting);
    changed |= ImGui::Checkbox(
        "Receive native fog",
        &grass.Appearance.ReceiveFog);
    return changed;
}

bool DrawPerformance(InteractiveGrassSettings& grass) {
    bool changed = false;
    int instanceBudget =
        static_cast<int>(grass.MaxInstancesPerRoom);
    if (ImGui::SliderInt(
            "Visible blade budget", &instanceBudget,
            1000, 500000, "%d",
            ImGuiSliderFlags_Logarithmic)) {
        grass.MaxInstancesPerRoom =
            static_cast<uint32_t>(instanceBudget);
        changed = true;
    }
    changed |= ImGui::SliderFloat(
        "Draw distance", &grass.DrawDistance,
        100.0F, 50000.0F, "%.0f world units",
        ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat("Final fade range", &grass.DrawFadeFraction,
        0.0F, 1.0F, "%.2f draw distance");
    ImGui::SeparatorText("Density by distance");
    changed |= ImGui::SliderFloat("Density falloff distance", &grass.LodReferenceDistance,
        100.0F, 50000.0F, "%.0f world units", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Density reduction start", &grass.LodStartFraction,
        0.0F, 1.0F, "%.2f falloff distance");
    changed |= ImGui::SliderFloat(
        "Density at falloff end", &grass.FarDensity,
        0.01F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat("Density fade softness", &grass.DensityFadeFraction, 0.0F, 1.0F, "%.2f");
    ImGui::SeparatorText("Distant tufts");
    changed |= ImGui::Checkbox("Distant tufts", &grass.FarTuftsEnabled);
    changed |= ImGui::SliderFloat("Distant LOD starts", &grass.LodEndFraction,
        grass.LodStartFraction, 1.0F, "%.2f falloff distance");
    changed |= ImGui::SliderFloat("LOD transition range", &grass.TuftTransitionFraction,
        0.0F, 1.0F, "%.2f falloff distance");
    if (grass.FarTuftsEnabled) {
        changed |= ImGui::SliderFloat("Distant tuft quantity", &grass.FarTuftDensity, 0.1F, 4.0F, "%.2fx");
        changed |= ImGui::SliderFloat("Distant tuft spread", &grass.FarTuftSpread, 0.25F, 4.0F, "%.2fx");
        int tuftBlades = grass.FarTuftBladeCount;
        if (ImGui::SliderInt("Blades per distant tuft", &tuftBlades, 2, 9)) {
            grass.FarTuftBladeCount = static_cast<uint8_t>(tuftBlades);
            changed = true;
        }
    }
    ImGui::SeparatorText("Segments by distance");
    int bladeSegments = static_cast<int>(grass.Appearance.BladeSegments);
    if (ImGui::SliderInt("Near blade segments", &bladeSegments,
                         kMinimumGrassBladeSegments, kMaximumGrassBladeSegments)) {
        grass.Appearance.BladeSegments = static_cast<uint8_t>(bladeSegments);
        grass.FarBladeSegments = std::min(grass.FarBladeSegments, grass.Appearance.BladeSegments);
        changed = true;
    }
    int farSegments = static_cast<int>(grass.FarBladeSegments);
    if (ImGui::SliderInt(
            "Far blade segments", &farSegments,
            kMinimumGrassBladeSegments,
            grass.Appearance.BladeSegments)) {
        grass.FarBladeSegments =
            static_cast<uint8_t>(farSegments);
        changed = true;
    }
    changed |= ImGui::SliderFloat("Segment reduction begins", &grass.SegmentLodStartDistance,
                                   0.0F, 10000.0F, "%.0f world units");
    grass.SegmentLodEndDistance = std::max(grass.SegmentLodEndDistance, grass.SegmentLodStartDistance);
    changed |= ImGui::SliderFloat("Minimum segments reached", &grass.SegmentLodEndDistance,
                                   grass.SegmentLodStartDistance, 10000.0F, "%.0f world units");
    changed |= ImGui::SliderFloat("Segment transition spread", &grass.SegmentLodSoftness, 0.0F, 1.0F, "%.2f");
    ImGui::SeparatorText("Visibility");
    changed |= ImGui::Checkbox(
        "Frustum culling", &grass.FrustumCulling);
    changed |= ImGui::SliderFloat(
        "Spatial batch size", &grass.CullingClusterSize,
        25.0F, 1500.0F, "%.0f world units",
        ImGuiSliderFlags_Logarithmic);
    if (changed) {
        grass.Quality = GrassQuality::Custom;
    }
    return changed;
}

bool DrawWind(InteractiveGrassSettings& grass) {
    bool changed = false;
    changed |= ImGui::SliderFloat(
        "Direction", &grass.WindDirectionDegrees,
        0.0F, 360.0F, "%.0f deg");
    changed |= ImGui::SliderFloat(
        "Strength", &grass.WindStrength,
        0.0F, 4.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Speed", &grass.WindSpeed,
        0.0F, 10.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Spatial scale", &grass.WindSpatialScale,
        0.01F, 20.0F, "%.2f",
        ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Gust strength", &grass.WindGustStrength,
        0.0F, 4.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Gust frequency", &grass.WindGustFrequency,
        0.01F, 4.0F, "%.2f Hz",
        ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderFloat(
        "Turbulence", &grass.WindTurbulence,
        0.0F, 2.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Per-blade variation", &grass.WindRandomness,
        0.0F, 1.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Maximum bend", &grass.MaximumBend,
        0.05F, 2.0F, "%.2f blade heights");
    return changed;
}

bool DrawInteraction(InteractiveGrassSettings& grass) {
    const auto actors = GrassInteractionBridge::Instance().LatestActors();
    ImGui::Text("Active actor colliders: %zu", actors.size());

    bool changed = false;
    changed |= ImGui::SliderFloat(
        "Push strength", &grass.CollisionPush,
        0.0F, 4.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Velocity response",
        &grass.CollisionVelocityResponse,
        0.0F, 3.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Collider radius",
        &grass.ColliderRadiusMultiplier,
        0.1F, 5.0F, "%.2fx");
    changed |= ImGui::SliderFloat(
        "Collider height",
        &grass.ColliderHeightMultiplier,
        0.1F, 5.0F, "%.2fx");
    changed |= ImGui::SliderFloat(
        "Recovery time", &grass.RecoverySeconds,
        0.05F, 10.0F, "%.2f s");
    changed |= ImGui::SliderFloat(
        "Damping", &grass.InteractionDamping,
        0.1F, 4.0F, "%.2f");
    changed |= ImGui::SliderFloat(
        "Field radius", &grass.InteractionFieldRadius,
        100.0F, 2500.0F, "%.0f world units",
        ImGuiSliderFlags_Logarithmic);

    constexpr std::array<uint16_t, 4> fieldResolutions{
        32, 64, 128, 256};
    const char* const fieldResolutionLabels[] = {
        "32 x 32", "64 x 64", "128 x 128", "256 x 256"};
    int resolutionIndex = 0;
    for (size_t index = 0;
         index < fieldResolutions.size(); ++index) {
        if (grass.InteractionFieldResolution ==
            fieldResolutions[index]) {
            resolutionIndex = static_cast<int>(index);
            break;
        }
    }
    if (ImGui::Combo(
            "Field resolution", &resolutionIndex,
            fieldResolutionLabels,
            static_cast<int>(fieldResolutions.size()))) {
        grass.InteractionFieldResolution =
            fieldResolutions[
                static_cast<size_t>(resolutionIndex)];
        changed = true;
    }
    changed |= ImGui::SliderFloat(
        "Vertical floor tolerance",
        &grass.InteractionVerticalMargin,
        0.0F, 300.0F, "%.0f world units");
    return changed;
}

void DrawRuntimeStatus() {
    const auto telemetry =
        GrassRenderTelemetry::Instance().Snapshot();
    ImGui::Text(
        "Runtime: %s | visible blades: %u",
        GrassRenderStatusName(telemetry.Status),
        telemetry.VisibleBlades);
    ImGui::TextDisabled(
        "rules %zu | masks %zu | surfaces %zu/%zu | "
        "placements %zu (cache %zu/%zu hit/miss) | "
        "pending %zu | "
        "anchors %llu/%llu evaluated/total | "
        "draws %u | workers %u | upload %.1f KiB | "
        "CPU %.2f ms (place %.2f, select %.2f, copy %.2f)",
        telemetry.ConfiguredRules, telemetry.ReadyMasks,
        telemetry.ScopedMeshes, telemetry.MatchingMeshes,
        telemetry.Placements,
        telemetry.PlacementCacheHits,
        telemetry.PlacementCacheMisses,
        telemetry.PendingPlacements,
        static_cast<unsigned long long>(
            telemetry.EvaluatedAnchors),
        static_cast<unsigned long long>(
            telemetry.ExtractedAnchors),
        telemetry.DrawCalls,
        telemetry.CullingWorkers,
        static_cast<double>(telemetry.UploadedBytes) / 1024.0,
        telemetry.CpuMilliseconds,
        telemetry.PlacementMilliseconds,
        telemetry.SelectionMilliseconds,
        telemetry.UploadMilliseconds);
}

} // namespace

bool DrawGrassSettingsPanel(
    InteractiveGrassSettings& grass,
    InteractiveGrassSettings& savedPreset,
    GrassSettingsPanelState& state) {
    bool changed = false;
    if (ImGui::Button("Save current preset")) {
        savedPreset = grass;
        state.Status = "Grass preset saved";
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to saved preset")) {
        grass = savedPreset;
        state.Status = "Saved grass preset restored";
        changed = true;
    }

    const char* const qualities[] = {
        "Off", "Low", "Medium", "High", "Custom"};
    GrassQuality quality = grass.Quality;
    if (EnumCombo("Quality", quality, qualities)) {
        ApplyGrassQualityPreset(grass, quality);
        changed = true;
    }
    if (ImGui::TreeNode("Grass status")) {
        DrawRuntimeStatus();
        ImGui::TreePop();
    }
    if (!state.Status.empty()) {
        ImGui::TextWrapped("%s", state.Status.c_str());
    }

    if (ImGui::BeginTabBar("##GrassFeatureTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Sources")) {
            ImGui::BeginChild("Sources");
            changed |= DrawSourcesAndPlacement(grass, state);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Generation")) {
            ImGui::BeginChild("Generation");
            changed |= DrawGeneration(grass);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Appearance")) {
            ImGui::BeginChild("Appearance");
            changed |= DrawAppearance(grass);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Performance")) {
            ImGui::BeginChild("Performance");
            changed |= DrawPerformance(grass);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Wind")) {
            ImGui::BeginChild("Wind");
            changed |= DrawWind(grass);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Interaction")) {
            ImGui::BeginChild("Interaction");
            changed |= DrawInteraction(grass);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    return changed;
}

} // namespace Fast::Oot3d
