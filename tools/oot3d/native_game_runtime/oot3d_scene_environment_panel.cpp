#include "fast/oot3d/graphics_settings_runtime.h"
#include "fast/oot3d/settings_panel_widgets.h"
#include "oot3d_scene_environment.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <string>

namespace Oot3dNativeGame {
namespace {
class EnvironmentPanel final : public Fast::Oot3d::GraphicsSettingsPanelTab {
public:
  explicit EnvironmentPanel(std::shared_ptr<SceneEnvironmentRuntime> runtime)
      : mRuntime(std::move(runtime)) {}
  // A section of Lighting > Lights & Shadows, above the shadow settings.
  const char *Label() const noexcept override { return "Scene lights"; }
  const char *Group() const noexcept override {
    return Fast::Oot3d::kLightingSettingsGroup;
  }
  void EndPreview() override { mRuntime->EndPreview(); }
  void Draw() override {
    ImGui::SeparatorText("Scene lights");
    auto state = mRuntime->Snapshot();
    if (!state.SceneId) {
      ImGui::TextWrapped("No native gameplay scene is available. Environment overrides are inactive.");
      return;
    }
    ImGui::Text("Native scene: 0x%04X (all rooms)", unsigned(*state.SceneId));
    ImGui::Text("Group: %s", state.Group ? EnvironmentGroupLabel(*state.Group) : "Unknown (no group standard)");
    ImGui::Text("Day source: %s", EnvironmentSourceLabel(state.Sources[0]));
    if (state.TimeDriven) ImGui::Text("Night source: %s", EnvironmentSourceLabel(state.Sources[1]));
    // Keep confirmation IDs outside the tab scope: a native scene change can
    // select Day while a Night action is awaiting confirmation.
    DrawGroupActions(state);
    if (!state.TimeDriven) {
      ImGui::TextWrapped("Fixed scene lighting: one profile applies at every time.");
      DrawProfile(state);
      mLastScene.reset();
      return;
    }
    if (mLastScene != state.SceneId) { mRequestedTab = state.EditingNight; mLastScene = state.SceneId; }
    if (state.EditingNight != mLastEditingNight) mRequestedTab = state.EditingNight;
    DrawDayNight(state);
    state = mRuntime->Snapshot();
    if (ImGui::BeginTabBar("##SceneEnvironmentProfiles")) {
      for (bool night : {false, true}) {
        ImGuiTabItemFlags flags = state.DirtyTabs[night ? 1 : 0] ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
        if (mRequestedTab && *mRequestedTab == night) flags |= ImGuiTabItemFlags_SetSelected;
        ImGui::BeginDisabled(night && !state.NightProfileExists);
        if (ImGui::BeginTabItem(night ? "Night" : "Day", nullptr, flags)) {
          if (mRuntime->SelectTab(night)) DrawProfile(mRuntime->Snapshot());
          ImGui::EndTabItem();
        }
        ImGui::EndDisabled();
      }
      ImGui::EndTabBar();
    }
    mRequestedTab.reset();
    mLastEditingNight = mRuntime->Snapshot().EditingNight;
  }
private:
  void DrawProfile(const SceneEnvironmentSnapshot& state) {
    ImGui::Text("Profile source: %s", EnvironmentSourceLabel(state.Sources[state.EditingNight ? 1 : 0]));
    if (!state.SourcePaths[state.EditingNight ? 1 : 0].empty())
      ImGui::TextWrapped("Source file: %s", state.SourcePaths[state.EditingNight ? 1 : 0].string().c_str());
    ImGui::TextWrapped("Save writes %s for this scene only.", state.Path.filename().string().c_str());
    auto value = state.Profile;
    auto &graphics = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
    const auto settings = graphics.Snapshot();
    const bool supported =
        graphics.Capabilities()
            .Get(Fast::Oot3d::GraphicsCapability::NativeSurfaceLighting)
            .Available;
    ImGui::BeginDisabled(!supported && !value.Enabled);
    bool changed = ImGui::Checkbox("Use scene environment", &value.Enabled);
    ImGui::EndDisabled();
    if (!supported ||
        !Fast::Oot3d::UsesFullSceneLighting(settings.Effects.SurfaceLighting) ||
        settings.Preset == Fast::Oot3d::GraphicsPreset::Authentic ||
        graphics.NativePresentationOverrideActive())
      ImGui::TextWrapped(
          "Inactive: needs the Modern lighting model, F2 native override off "
          "and a supported renderer.");
    ImGui::BeginDisabled(!value.Enabled || !supported);
    changed |= ImGui::Checkbox("Preserve scene brightness", &value.PreserveSceneBrightness);
    ImGui::TextWrapped(
        "Keep the native ambient reference and authored unlit color when native lights are off. "
        "Added lights still add illumination; this is not automatic exposure.");
    changed |= ImGui::ColorEdit3("Ambient color", value.AmbientColor.data(),
                                 ImGuiColorEditFlags_Float);
    changed |=
        ImGui::SliderFloat("Ambient intensity", &value.AmbientIntensity, 0.0F,
                           2.0F, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Ambient is uniform, without direction. Colors are linear RGB.");
    changed |= ImGui::SliderFloat("Native scene direct contribution",
                                  &value.NativeDirectStrength, 0.0F, 2.0F,
                                  "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Native local contribution",
                                  &value.NativeLocalStrength, 0.0F, 2.0F,
                                  "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Scene direct uses the game's two environment directions on world and actors. "
        "Local keeps other native per-draw lights separate. Neither controls the added key/fill or fairy lights.");
    ImGui::BeginDisabled(value.PreserveSceneBrightness);
    changed |= ImGui::SliderFloat("Native ambient contribution",
                                  &value.NativeAmbientStrength, 0.0F, 2.0F,
                                  "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    changed |=
        ImGui::SliderFloat("Native fog contribution", &value.FogStrength, 0.0F,
                           1.0F, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Rim light");
    changed |= ImGui::Checkbox("Enable rim light", &value.Rim.Enabled);
    ImGui::BeginDisabled(!value.Rim.Enabled);
    int rimSource = static_cast<int>(value.Rim.Source);
    if (ImGui::Combo("Rim source", &rimSource, "Independent\0Scene lights\0")) {
      value.Rim.Source = static_cast<Fast::Renderer3ds::PicaRimLightSource>(rimSource);
      changed = true;
    }
    changed |= ImGui::SliderFloat("Rim intensity", &value.Rim.Intensity, 0.0F,
                                  Fast::Renderer3ds::kMaximumPicaRimIntensity, "%.2fx",
                                  ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Rim sharpness", &value.Rim.Sharpness, 1.0F, 16.0F, "%.1f",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::BeginDisabled(value.Rim.Source != Fast::Renderer3ds::PicaRimLightSource::Independent);
    changed |= ImGui::ColorEdit3("Rim color", value.Rim.Color.data(), ImGuiColorEditFlags_Float);
    changed |= ImGui::SliderFloat("Rim azimuth", &value.Rim.Azimuth, -180.0F, 180.0F, "%.0f deg",
                                  ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat("Rim coverage", &value.Rim.Coverage, 0.0F, 1.0F, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    int rimScope = static_cast<int>(value.Rim.Scope);
    if (ImGui::Combo("Rim scope", &rimScope, "Actors\0Actors and world\0")) {
      value.Rim.Scope = static_cast<Fast::Renderer3ds::PicaRimLightScope>(rimScope);
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Independent is a camera-relative edge light, unchanged by scene lights or shadows. "
                       "Azimuth: 0 top, 90 right, -90 left, 180 bottom. Coverage: 0 directional, 1 whole silhouette. "
                       "Scene lights uses the existing lights and their shadows. Sharpness narrows the edge. "
                       "Opaque surfaces only.");
    ImGui::TextUnformatted("Additional environment lights");
    if (ImGui::BeginTabBar("Scene lights")) {
      const char *labels[] = {"Key light", "Fill light"};
      for (size_t i = 0; i < value.Lights.size(); ++i) {
        if (!ImGui::BeginTabItem(labels[i]))
          continue;
        auto &light = value.Lights[i];
        ImGui::PushID(static_cast<int>(i));
        changed |= ImGui::ColorEdit3("Light color", light.Color.data(),
                                     ImGuiColorEditFlags_Float);
        changed |=
            ImGui::SliderFloat("Light intensity", &light.Intensity, 0.0F, 4.0F,
                               "%.2f", ImGuiSliderFlags_AlwaysClamp);
        using KeyDirection = Fast::Renderer3ds::PicaEnvironmentKeyDirection;
        const bool scene = i == 0 && value.KeyDirection != KeyDirection::Authored;
        if (i == 0) {
          const char *directions[] = {"Manual", "Main scene light",
                                      "Ambient light orientation"};
          int direction = static_cast<int>(value.KeyDirection);
          if (ImGui::Combo("Key light direction", &direction, directions,
                           IM_ARRAYSIZE(directions))) {
            value.KeyDirection = static_cast<KeyDirection>(direction);
            changed = true;
          }
          if (value.KeyDirection == KeyDirection::SceneMain) {
            changed |= ImGui::SliderFloat("Azimuth offset", &value.KeyAzimuthOffset,
                                          -180.0F, 180.0F, "%.1f deg",
                                          ImGuiSliderFlags_AlwaysClamp);
            changed |= ImGui::SliderFloat("Elevation offset", &value.KeyElevationOffset,
                                          -90.0F, 90.0F, "%.1f deg",
                                          ImGuiSliderFlags_AlwaysClamp);
          }
          ImGui::TextWrapped(
              value.KeyDirection == KeyDirection::SceneMain
                  ? "Follows the scene's main light (the strongest environment light "
                    "above the horizon, the sun or the moon outdoors), turned by a "
                    "fixed offset. It also takes that light's colour and brightness: "
                    "the colour above tints it and the intensity scales it (white "
                    "and 1 give the game's own light)."
              : value.KeyDirection == KeyDirection::SceneAmbient
                  ? "Follows the axis of the scene's other environment light, always "
                    "pointing to the main light's side, with the main light's colour "
                    "and brightness, tinted and scaled as above."
                  : "Uses the azimuth and elevation below.");
        }
        changed |=
            ImGui::SliderFloat(scene ? "Fallback azimuth" : "Azimuth",
                               &light.Azimuth, -180.0F, 180.0F,
                               "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |=
            ImGui::SliderFloat(scene ? "Fallback elevation" : "Elevation",
                               &light.Elevation, -90.0F, 90.0F,
                               "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        if (scene)
          ImGui::TextWrapped("The fallback direction applies while the scene has "
                             "no environment light.");
        ImGui::TextWrapped(
            i == 0 ? "Key light casts directional shadows when they are enabled "
                     "below. It lights actors and static world surfaces."
                   : "Fill light is unshadowed.");
        ImGui::PopID();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::TextWrapped("Direction points toward the source: azimuth 0 = +Z, 90 "
                       "= +X; elevation 90 = overhead. Painted texture "
                       "lighting remains in the albedo.");
    ImGui::EndDisabled();
    ImGui::SeparatorText("Dynamic scene point lights");
    ImGui::TextWrapped("Replace Navi and fairy vertex-light contributions with spatial scene lights, and light "
                       "the flames the game only draws (torches, fire objects). Architecture lights the room "
                       "torches, lava, windows, light shafts and glows of the room geometry, in the rooms the "
                       "game has loaded. Native positions, colors and ranges are retained. These controls are independent of Use scene environment and affect "
                       "only the Modern lighting model.");
    const char *pointLabels[] = {"Navi point lights", "Fairy point lights", "Flame point lights",
                                 "Architecture point lights"};
    const char *pointNames[] = {"Navi", "Fairy", "Flame", "Architecture"};
    static_assert(std::size(pointLabels) == Fast::Renderer3ds::kPicaScenePointLightGroups);
    for (size_t i = 0; i < value.PointLights.size(); ++i) {
      auto &light = value.PointLights[i];
      ImGui::PushID(pointLabels[i]);
      ImGui::BeginDisabled(!supported && !light.Enabled);
      changed |= ImGui::Checkbox(pointLabels[i], &light.Enabled);
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!supported || !light.Enabled);
      const std::string name = pointNames[i];
      changed |= ImGui::SliderFloat((name + " intensity").c_str(), &light.Intensity, 0.0F, 8.0F, "%.2fx",
                                    ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::SliderFloat((name + " radius scale").c_str(), &light.RadiusScale, 0.1F, 8.0F, "%.2fx",
                                    ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::ColorEdit3((name + " color tint").c_str(), light.ColorTint.data(),
                                   ImGuiColorEditFlags_Float);
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::BeginDisabled(!value.Enabled || !supported);
    changed |= DrawPlacedLights(value, state);
    ImGui::EndDisabled();
    ImGui::SeparatorText("Shadows");
    ImGui::BeginDisabled(!supported);
    int faces = static_cast<int>(value.CasterFaces);
    if (ImGui::Combo("Shadow caster faces", &faces, "Native\0Both faces\0Interior sun / offline\0")) {
      value.CasterFaces = static_cast<Fast::Renderer3ds::ShadowCasterFaces>(faces);
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Native uses the title's face culling. Both faces closes one-sided gaps. "
                       "Interior sun uses offline room data to admit the key light into authored interiors. Inner roofs, "
                       "walls and foliage still cast. Rooms use both faces while their local cache is prepared.");
    if (changed)
      mRuntime->Preview(value, state.Revision);
    if (ImGui::Button(!state.TimeDriven ? "Save profile" : state.EditingNight ? "Save night" : "Save day"))
      mRuntime->Save();
    ImGui::SameLine();
    if (ImGui::Button(!state.TimeDriven ? "Revert profile" : state.EditingNight ? "Revert night" : "Revert day"))
      mRuntime->Reload();
    if (ImGui::Button("Use native environment")) {
      value.Enabled = false;
      mRuntime->Preview(value, state.Revision);
    }
    const auto status = mRuntime->Snapshot();
    if (!status.Error.empty())
      ImGui::TextWrapped("Environment error: %s", status.Error.c_str());
    if (status.Dirty)
      ImGui::TextWrapped("Unsaved changes in this tab. Time changes keep both drafts; save before leaving this scene.");
    else
      ImGui::TextUnformatted(
          status.FileExists ? "Scene profile saved."
                            : "Using a fallback. Save creates this scene's own profile.");
  }

private:
  uint64_t mGroupActionRevision = 0;
  void DrawGroupActions(const SceneEnvironmentSnapshot& state) {
    ImGui::TextWrapped("Day: scene's own > group standard > base. Night: scene's own > group standard > day result. "
                       "A scene with its own day profile can still inherit the group's night standard.");
    ImGui::BeginDisabled(!state.Group);
    if (ImGui::Button("Save as group standard")) {
      mGroupActionRevision = state.Revision;
      ImGui::OpenPopup("Save group standard?");
    }
    ImGui::SameLine();
    if (ImGui::Button("Use group standard")) {
      mGroupActionRevision = state.Revision;
      ImGui::OpenPopup("Use group standard?");
    }
    ImGui::EndDisabled();
    for (bool save : {true, false}) {
      if (ImGui::IsPopupOpen(save ? "Save group standard?" : "Use group standard?"))
        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
      if (!ImGui::BeginPopupModal(save ? "Save group standard?" : "Use group standard?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) continue;
      ImGui::Text("Group: %s; slot: %s", state.Group ? EnvironmentGroupLabel(*state.Group) : "Unknown",
                    state.EditingNight ? "Night" : "Day");
      ImGui::TextWrapped(save
          ? "Replace this group's standard with the current draft? Every scene inheriting this slot uses the new values. Scene-owned files keep precedence."
          : "Delete this scene's own file for this tab and discard its custom values and unsaved edits? The group standard applies; if absent, day uses base and night uses the day result.");
      const bool stale = state.Revision != mGroupActionRevision;
      if (stale) ImGui::TextWrapped("The scene or profile changed. Cancel and reopen this action.");
      ImGui::BeginDisabled(stale || !state.Group);
      if (ImGui::Button(save ? "Save standard" : "Use standard")) {
        if (save) mRuntime->SaveAsGroupStandard(); else mRuntime->UseGroupStandard();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  // Lights placed in the scene: added where Link is, then moved, turned and
  // shaped with the controls of their type.
  bool DrawPlacedLights(SceneEnvironmentProfile &value, const SceneEnvironmentSnapshot &state) {
    ImGui::SeparatorText("Placed lights");
    ImGui::TextWrapped("Lights of this scene's profile (day or night). New lights start where Link "
                       "is; save the scene environment to keep them.");
    ImGui::Checkbox("Show light gizmos", &mShowGizmos);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("While this editor is open (and with the free camera): every light's shape in "
                        "its colour, and through the selected light its move axes (X red, Y green, Z "
                        "blue) and turn axes (yaw about Y, pitch orange, roll cyan) across the scene. "
                        "Parts behind surfaces are dashed, so an axis shows where it meets them.");
    bool changed = false;
    auto &lights = value.PlacedLights;
    ImGui::BeginDisabled(lights.size() >= kMaximumPlacedLights);
    if (ImGui::Button("Add light at Link")) {
      PlacedLight light;
      for (const auto &existing : lights)
        light.Id = std::max(light.Id, existing.Id);
      ++light.Id;
      light.Name = "Light " + std::to_string(light.Id);
      if (state.Player) {
        // At head height, facing where Link faces, tilted down.
        light.Position = state.Player->Position;
        light.Position[1] += kPlacedLightHeadHeight;
        light.Rotation = {state.Player->Yaw, -30.0F, 0.0F};
      }
      lights.push_back(light);
      mSelectedLight = light.Id;
      changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu", lights.size(), kMaximumPlacedLights);
    if (!state.Player)
      ImGui::TextWrapped("Link's position is not available: new lights start at the scene origin.");
    if (lights.empty())
      return changed;
    if (std::none_of(lights.begin(), lights.end(), [&](const auto &light) { return light.Id == mSelectedLight; }))
      mSelectedLight = lights.front().Id;
    if (ImGui::BeginListBox("##PlacedLights",
                            ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() *
                                                 static_cast<float>(std::min<size_t>(lights.size(), 6U) + 0.25F)))) {
      for (const auto &light : lights) {
        ImGui::PushID(static_cast<int>(light.Id));
        char label[96];
        std::snprintf(label, sizeof(label), "%s (%s)%s", light.Name.c_str(), kTypeNames[static_cast<size_t>(light.Type)],
                      light.Visible ? "" : ", hidden");
        if (ImGui::Selectable(label, light.Id == mSelectedLight))
          mSelectedLight = light.Id;
        ImGui::PopID();
      }
      ImGui::EndListBox();
    }
    mRuntime->EditLightGizmo(mSelectedLight, mShowGizmos);
    const auto selected = std::find_if(lights.begin(), lights.end(),
                                       [&](const auto &light) { return light.Id == mSelectedLight; });
    auto &light = *selected;
    ImGui::PushID("PlacedLight");
    char name[65]{};
    std::snprintf(name, sizeof(name), "%s", light.Name.c_str());
    if (ImGui::InputText("Light name", name, sizeof(name))) {
      light.Name = name;
      changed = true;
    }
    int type = static_cast<int>(light.Type);
    if (ImGui::Combo("Light type", &type, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
      light.Type = static_cast<PlacedLightType>(type);
      changed = true;
    }
    ImGui::TextWrapped("%s", kTypeHelp[static_cast<size_t>(light.Type)]);
    changed |= ImGui::Checkbox("Light visible", &light.Visible);
    changed |= ImGui::ColorEdit3("Placed light color", light.Color.data(), ImGuiColorEditFlags_Float);
    changed |= ImGui::SliderFloat("Placed light intensity", &light.Intensity, 0.0F, 16.0F, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    const bool sun = light.Type == PlacedLightType::Sun;
    if (!sun) {
      changed |= ImGui::DragFloat3("Position", light.Position.data(), 1.0F, -100000.0F, 100000.0F, "%.1f",
                                   ImGuiSliderFlags_AlwaysClamp);
      ImGui::BeginDisabled(!state.Player);
      if (ImGui::Button("Move to Link") && state.Player) {
        light.Position = state.Player->Position;
        light.Position[1] += kPlacedLightHeadHeight;
        changed = true;
      }
      ImGui::EndDisabled();
    }
    // Direction: the sun's light, the spot's beam, the surface's face.
    if (light.Type == PlacedLightType::Sun || light.Type == PlacedLightType::Spot ||
        light.Type == PlacedLightType::Surface) {
      changed |= ImGui::SliderFloat("Yaw", &light.Rotation[0], -180.0F, 180.0F, "%.1f deg",
                                    ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::SliderFloat("Pitch", &light.Rotation[1], -90.0F, 90.0F, "%.1f deg",
                                    ImGuiSliderFlags_AlwaysClamp);
      if (light.Type == PlacedLightType::Surface)
        changed |= ImGui::SliderFloat("Roll", &light.Rotation[2], -180.0F, 180.0F, "%.1f deg",
                                      ImGuiSliderFlags_AlwaysClamp);
      ImGui::BeginDisabled(!state.Player);
      if (ImGui::Button("Face Link's direction") && state.Player) {
        light.Rotation[0] = state.Player->Yaw;
        changed = true;
      }
      ImGui::EndDisabled();
      ImGui::TextWrapped("Yaw 0 faces +Z, 90 faces +X; pitch 90 points up, -90 down.");
    }
    ImGui::BeginDisabled(sun);
    bool shadows = light.CastShadows && !sun;
    if (ImGui::Checkbox("Cast shadows", &shadows)) {
      light.CastShadows = shadows;
      changed = true;
    }
    ImGui::EndDisabled();
    if (sun)
      ImGui::TextWrapped("Sun lights do not cast shadows; the key light keeps the directional shadows.");
    else
      ImGui::TextWrapped(light.Type == PlacedLightType::Surface
                             ? "Uses a point-shadow cube map at the surface's centre (Point shadows: Cube "
                               "maps), shared with Navi and the fairies; a large surface's shadows are "
                               "approximate."
                             : "Uses a point-shadow cube map at the light (Point shadows: Cube maps), "
                               "shared with Navi and the fairies.");
    switch (light.Type) {
    case PlacedLightType::Point:
    case PlacedLightType::Omni:
      changed |= ImGui::SliderFloat("Reach", &light.Radius, 1.0F, 5000.0F, "%.0f",
                                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      break;
    case PlacedLightType::Spot:
      changed |= ImGui::SliderFloat("Reach", &light.Radius, 1.0F, 5000.0F, "%.0f",
                                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      changed |= ImGui::SliderFloat("Beam angle", &light.BeamAngle, 1.0F, 179.0F, "%.1f deg",
                                    ImGuiSliderFlags_AlwaysClamp);
      changed |= ImGui::SliderFloat("Beam softness", &light.BeamSoftness, 0.0F, 1.0F, "%.2f",
                                    ImGuiSliderFlags_AlwaysClamp);
      break;
    case PlacedLightType::Surface: {
      const char *shapes[] = {"Rectangle", "Disc"};
      int shape = static_cast<int>(light.Shape);
      if (ImGui::Combo("Surface shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
        light.Shape = static_cast<PlacedLightShape>(shape);
        changed = true;
      }
      if (light.Shape == PlacedLightShape::Rectangle) {
        changed |= ImGui::SliderFloat("Surface width", &light.Width, 1.0F, 5000.0F, "%.0f",
                                      ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Surface height", &light.Height, 1.0F, 5000.0F, "%.0f",
                                      ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      } else {
        changed |= ImGui::SliderFloat("Surface radius", &light.Radius, 1.0F, 5000.0F, "%.0f",
                                      ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      }
      changed |= ImGui::Checkbox("Emit from both sides", &light.TwoSided);
      changed |= ImGui::SliderFloat("Surface spread", &light.Spread, 1.0F, 180.0F, "%.0f deg",
                                    ImGuiSliderFlags_AlwaysClamp);
      ImGui::TextWrapped("180 is a diffuse surface. Less straightens its light along its axis: a sharper "
                         "footprint of its shape, as bright under it.");
      changed |= ImGui::SliderFloat("Surface reach", &light.Range, 1.0F, 5000.0F, "%.0f",
                                    ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
      break;
    }
    case PlacedLightType::Sun:
      break;
    }
    ImGui::BeginDisabled(lights.size() >= kMaximumPlacedLights);
    if (ImGui::Button("Duplicate light")) {
      auto copy = light;
      for (const auto &existing : lights)
        copy.Id = std::max(copy.Id, existing.Id);
      ++copy.Id;
      copy.Name = light.Name + " copy";
      if (copy.Name.size() > 64U)
        copy.Name.resize(64U);
      mSelectedLight = copy.Id;
      lights.push_back(copy);
      changed = true;
      ImGui::EndDisabled();
      ImGui::PopID();
      return changed;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Delete light")) {
      lights.erase(selected);
      changed = true;
    }
    ImGui::PopID();
    return changed;
  }
  static constexpr const char *kTypeNames[] = {"Sun", "Spot", "Point", "Surface", "Omni"};
  static constexpr const char *kTypeHelp[] = {
      "Parallel light along its direction, everywhere in the scene, without falloff.",
      "A cone from its position: inverse-square falloff (intensity 1 is its colour at 100 "
      "units), cut at the reach.",
      "Inverse-square falloff from its position (intensity 1 is its colour at 100 units), cut "
      "smoothly at the reach.",
      "An emitting rectangle or disc facing its direction (both faces when two-sided): bright "
      "close to it, fading with distance, cut at the reach.",
      "Soft falloff to zero at the reach, like Navi and the fairies: easy to dose."};
  // World units above Link's feet where a new light starts.
  static constexpr float kPlacedLightHeadHeight = 50.0F;
  uint32_t mSelectedLight = 0;
  bool mShowGizmos = true;
  std::optional<bool> mRequestedTab;
  std::optional<uint16_t> mLastScene;
  bool mLastEditingNight = false;
  void DrawDayNight(const SceneEnvironmentSnapshot& state) {
    if (!state.NightError.empty()) ImGui::TextWrapped("Night profile: %s", state.NightError.c_str());
    int preview = static_cast<int>(state.PreviewMode);
    if (ImGui::BeginCombo("Preview", preview == 0 ? "Auto" : preview == 1 ? "Force day" : "Force night")) {
      const char* names[] = {"Auto", "Force day", "Force night"};
      for (int i = 0; i < 3; ++i) {
        ImGui::BeginDisabled(i == 2 && !state.NightProfileExists);
        if (ImGui::Selectable(names[i], preview == i))
          mRuntime->SetPreviewMode(static_cast<SceneEnvironmentPreview>(i));
        ImGui::EndDisabled();
      }
      ImGui::EndCombo();
    }
    const auto applied = mRuntime->Snapshot();
    ImGui::Text("Applied: day %.0f%% / night %.0f%%", (1 - applied.AppliedNightWeight) * 100,
                 applied.AppliedNightWeight * 100);
    if (applied.ForcedWeight) ImGui::TextUnformatted("Lab weight override active (process environment).");
    ImGui::TextWrapped("Edit either tab at any time. Auto follows native dawn and dusk; forced preview ends on F2 or closing F1.");
    if (!state.NightProfileExists) {
      ImGui::TextWrapped("Day applies at all times until a night profile is created.");
      if (ImGui::Button("Create night (from day)") && mRuntime->CreateNightProfile()) mRequestedTab = true;
      return;
    }
    if (ImGui::Button("Copy day -> night")) ImGui::OpenPopup("Copy day to night?");
    if (ImGui::BeginPopupModal("Copy day to night?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Replace the night draft with the day draft? Save night to keep the copy.");
      if (ImGui::Button("Copy")) {
        if (mRuntime->CopyDayToNight()) mRequestedTab = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.Sources[1] != SceneEnvironmentSource::Scene);
    if (ImGui::Button("Remove night")) ImGui::OpenPopup("Remove night profile?");
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal("Remove night profile?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480);
      ImGui::TextWrapped("Delete the scene night file and discard its unsaved edits? The group night standard applies when present; otherwise the day result applies.");
      ImGui::PopTextWrapPos();
      if (ImGui::Button("Remove")) {
        if (mRuntime->RemoveNightProfile()) mRequestedTab = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }
  std::shared_ptr<SceneEnvironmentRuntime> mRuntime;
};
} // namespace
std::shared_ptr<Fast::Oot3d::GraphicsSettingsPanelTab>
CreateSceneEnvironmentPanel(std::shared_ptr<SceneEnvironmentRuntime> runtime) {
  return std::make_shared<EnvironmentPanel>(std::move(runtime));
}
} // namespace Oot3dNativeGame
