#include "oot3d_in_game_menu.h"

#include "oot3d_menu_presentation.h"
#include "oot3d_top_screen_controls.h"

#include "fast/oot3d/graphics_settings_window.h"
#include "fast/oot3d/presentation_picture.h"
#include "oot3d/renderer/ui_presentation_layout.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

namespace Oot3dNativeGame {
namespace {

using Fast::Oot3d::GraphicsSettings;
using Menu::Row;
using Menu::RowKind;
using Menu::WriteResult;

constexpr double kLiveRefreshSeconds = 0.5;
// The widescreen canvas the menu is drawn on (oot3d_native_a32_window_frame).
constexpr float kMenuCanvasWidth = 1280.0F / 3.0F;
constexpr float kMenuCanvasHeight = 240.0F;

std::string Percent(int value) { return std::to_string(value) + "%"; }

std::string Multiplier(int percent) {
  std::array<char, 16> text{};
  std::snprintf(text.data(), text.size(), "%.2fx", static_cast<double>(percent) / 100.0);
  return text.data();
}

Row Heading(std::string key) {
  Row row;
  row.Key = std::move(key);
  row.Kind = RowKind::Heading;
  return row;
}

struct ResolutionPreset {
  const char *Label;
  std::uint32_t Width, Height;
};
constexpr std::array<ResolutionPreset, 8> kResolutions{{
    {"800 x 600 (4:3)", 800, 600},
    {"1280 x 960 (4:3)", 1280, 960},
    {"1600 x 1200 (4:3)", 1600, 1200},
    {"1280 x 720 (720p)", 1280, 720},
    {"1600 x 900 (900p)", 1600, 900},
    {"1920 x 1080 (1080p)", 1920, 1080},
    {"2560 x 1440 (1440p)", 2560, 1440},
    {"3840 x 2160 (4K)", 3840, 2160},
}};

// Photo mode free camera speeds (units per second) behind a 1..10 level.
constexpr std::array<float, 10> kPhotoSpeeds{25.0F, 50.0F, 100.0F, 200.0F, 400.0F,
                                              800.0F, 1200.0F, 2000.0F, 4000.0F, 8000.0F};

int PhotoSpeedLevel(float speed) {
  int best = 0;
  float bestDistance = 1.0e9F;
  for (std::size_t index = 0; index < kPhotoSpeeds.size(); ++index) {
    const float distance = std::abs(std::log(speed) - std::log(kPhotoSpeeds[index]));
    if (distance < bestDistance) {
      bestDistance = distance;
      best = static_cast<int>(index);
    }
  }
  return best + 1;
}

} // namespace

InGameMenu::InGameMenu(InGameMenuOwners owners)
    : mOwners(std::move(owners)), mModel((RefreshSnapshots(), BuildDocument())) {}

void InGameMenu::RefreshSnapshots() {
  auto &graphics = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
  mGraphics = graphics.Snapshot();
  mCapabilities = graphics.Capabilities();
  if (mOwners.Controls) mControls = mOwners.Controls->Snapshot().Config;
  if (mOwners.TopScreen) mTopScreen = mOwners.TopScreen->Snapshot().Config;
}

void InGameMenu::Refresh() {
  RefreshSnapshots();
  mModel.Refresh();
}

Menu::WriteResult InGameMenu::ApplyGraphics(GraphicsSettings settings, bool keepPreset) {
  using Fast::Oot3d::GraphicsSettingsSaveState;
  auto &runtime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
  if (!keepPreset) settings.Preset = Fast::Oot3d::GraphicsPreset::Custom;
  const auto validation = runtime.Apply(settings, true);
  RefreshSnapshots();
  if (!validation.Accepted()) {
    std::string message = "Rejected";
    for (const auto &issue : validation.Issues) message += ": " + issue.Field + " " + issue.Message;
    return {message, true};
  }
  switch (runtime.SaveState()) {
  case GraphicsSettingsSaveState::Saved:
    return {};
  case GraphicsSettingsSaveState::Pending:
    return {"Applied, saving", false};
  case GraphicsSettingsSaveState::WaitingForDisplay:
    return {"Applied. Keep or revert the display change.", false};
  case GraphicsSettingsSaveState::SessionOnly:
    // Not a failure: this launch does not save settings (F1 says "session only").
    return {"Applied for this session only: this launch does not save settings", false};
  case GraphicsSettingsSaveState::Failed:
    return {"Applied but not saved: the settings file could not be written", true};
  }
  return {};
}

Menu::WriteResult InGameMenu::ApplyControls(const NativeControlConfig &config) {
  if (!mOwners.Controls) return {"Controls are not configurable", true};
  std::string error;
  const bool applied = mOwners.Controls->Apply(config, &error);
  RefreshSnapshots();
  if (!applied) return {"Not saved: " + error, true};
  if (!mOwners.Controls->Persistent()) return {"Applied for this session only: no configuration file", false};
  return {};
}

// The same write as F1's TopScreen controls: the whole configuration through
// the runtime, which validates and saves it (or previews it when this launch
// has no file).
Menu::WriteResult InGameMenu::ApplyTopScreen(const TopScreenUiConfig &config) {
  if (!mOwners.TopScreen) return {"TopScreen is not active", true};
  if (!mOwners.TopScreen->Persistent()) {
    mOwners.TopScreen->Preview(config);
    RefreshSnapshots();
    return {"Applied for this session only: no configuration file", false};
  }
  std::string error;
  const bool applied = mOwners.TopScreen->Apply(config, &error);
  RefreshSnapshots();
  if (!applied) return {"Not saved: " + error, true};
  return {};
}

Menu::Document InGameMenu::BuildDocument() {
  Menu::Document document;
  BuildVideoTab(document);
  BuildGraphicsTab(document);
  BuildControlsTab(document);
  BuildPhotoModeTab(document);
  BuildGameTab(document);
  return document;
}

void InGameMenu::BuildVideoTab(Menu::Document &document) {
  using namespace Fast::Oot3d;
  Menu::Tab tab;
  tab.Title = "Video";
  // A graphics choice validated like F1's combos: the same validator decides
  // which values the renderer accepts right now. Host-owned settings (the
  // window, resolution, VSync) are outside every preset, so changing them
  // keeps the preset; anything else makes it Custom.
  const auto graphicsChoice = [this](std::string key, std::string help, std::vector<std::string> choices,
                                     std::function<int(const GraphicsSettings &)> get,
                                     std::function<void(GraphicsSettings &, int)> set, bool hostOwned = false) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Choice;
    row.Help = std::move(help);
    row.Choices = std::move(choices);
    row.Read = [this, get] { return get(mGraphics); };
    row.Write = [this, set, hostOwned](int value) {
      auto settings = mGraphics;
      set(settings, value);
      return ApplyGraphics(settings, hostOwned);
    };
    row.ChoiceAvailable = [this, get, set](int value) {
      auto candidate = mGraphics;
      candidate.Preset = GraphicsPreset::Custom;
      set(candidate, value);
      const auto validation = GraphicsSettingsService::Validate(candidate, mCapabilities);
      return validation.Accepted() && get(validation.Value) == value;
    };
    row.Modified = [this, get] { return get(mGraphics) != get(GraphicsSettings{}); };
    return row;
  };
  const auto graphicsToggle = [this](std::string key, std::string help, std::function<bool(const GraphicsSettings &)> get,
                                     std::function<void(GraphicsSettings &, bool)> set, bool hostOwned = false) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Toggle;
    row.Help = std::move(help);
    row.Read = [this, get] { return get(mGraphics) ? 1 : 0; };
    row.Write = [this, set, hostOwned](int value) {
      auto settings = mGraphics;
      set(settings, value != 0);
      return ApplyGraphics(settings, hostOwned);
    };
    row.Modified = [this, get] { return get(mGraphics) != get(GraphicsSettings{}); };
    return row;
  };

  tab.Rows.push_back(Heading("Display"));
  {
    Row row;
    row.Key = "Graphics preset";
    row.Kind = RowKind::Choice;
    row.Help = "Authentic keeps the original look. Enhanced adds the modern lighting, shadows and effects. "
               "Toon applies the cel-shaded style. Any change below makes the preset Custom; the window, "
               "resolution, VSync and texture packs stay as they are.";
    row.Choices = {"Authentic", "Enhanced", "Toon", "Custom"};
    row.Read = [this] { return static_cast<int>(mGraphics.Preset); };
    row.Write = [this](int value) -> WriteResult {
      if (value == static_cast<int>(GraphicsPreset::Custom)) return {"Custom is the result of changing any setting", false};
      return ApplyGraphics(GraphicsSettingsService::PresetForCurrent(static_cast<GraphicsPreset>(value), mGraphics), true);
    };
    row.ChoiceAvailable = [](int value) { return value != static_cast<int>(GraphicsPreset::Custom); };
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(graphicsChoice(
      "Window mode", "Windowed, borderless over the desktop, or exclusive fullscreen. A display change asks you to keep it within a few seconds.",
      {"Windowed", "Borderless", "Exclusive fullscreen"}, [](const auto &s) { return static_cast<int>(s.Window); },
      [](auto &s, int value) { s.Window = static_cast<WindowMode>(value); }, true));
  tab.Rows.push_back(graphicsChoice(
      "Aspect ratio",
      "The shape of the picture. A fixed ratio is centred with black bars where the window or the screen is "
      "wider or taller; in a window it also resizes the window to that shape. Free fills the window as you "
      "shape it and the whole screen in fullscreen. The scene shows more at the sides as the picture widens, "
      "never stretched; the HUD keeps its place.",
      {"Original 3DS (5:3)", "16:9", "21:9", "Free"}, [](const auto &s) { return static_cast<int>(s.Aspect); },
      [](auto &s, int value) {
        s.Aspect = static_cast<Fast::Oot3d::AspectRatioMode>(value);
        (void)Fast::Oot3d::SnapWindowedOutputToAspect(s);
      },
      true));
  {
    Row row;
    row.Key = "Resolution";
    row.Kind = RowKind::Choice;
    row.Help = "The output size of the window or the fullscreen mode. Borderless uses the desktop resolution.";
    for (const auto &preset : kResolutions) row.Choices.emplace_back(preset.Label);
    row.Choices.emplace_back("Custom");
    row.Read = [this] {
      for (std::size_t index = 0; index < kResolutions.size(); ++index) {
        if (kResolutions[index].Width == mGraphics.OutputWidth && kResolutions[index].Height == mGraphics.OutputHeight)
          return static_cast<int>(index);
      }
      return static_cast<int>(kResolutions.size());
    };
    row.Write = [this](int value) -> WriteResult {
      if (value >= static_cast<int>(kResolutions.size())) return {"A custom resolution is typed in F1 > Display", false};
      auto settings = mGraphics;
      settings.OutputWidth = kResolutions[static_cast<std::size_t>(value)].Width;
      settings.OutputHeight = kResolutions[static_cast<std::size_t>(value)].Height;
      return ApplyGraphics(settings, true);
    };
    row.ChoiceAvailable = [](int value) { return value < static_cast<int>(kResolutions.size()); };
    row.Available = [this] { return mGraphics.Window != WindowMode::Borderless; };
    row.UnavailableReason = "Borderless uses the desktop resolution.";
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(graphicsToggle(
      "VSync", "Waits for the display refresh: no tearing, a little more latency.",
      [](const auto &s) { return s.VSync; }, [](auto &s, bool value) { s.VSync = value; }, true));
  tab.Rows.push_back(graphicsChoice(
      "Frame rate", "The game runs at its original 30 frames per second; the interpolated modes present 60 or 90 by "
                    "interpolating the rendered frames. Uncapped presents every rendered frame as it is ready.",
      {"Original 30 FPS", "Interpolated 2x (60 FPS)", "Interpolated 3x (90 FPS)", "Uncapped"},
      [](const auto &s) { return static_cast<int>(s.FrameRate); },
      [](auto &s, int value) { s.FrameRate = static_cast<FrameRateMode>(value); }));

  tab.Rows.push_back(Heading("Rendering"));
  {
    Row row;
    row.Key = "Render scale";
    row.Kind = RowKind::Number;
    row.Help = "The scene is rendered at this fraction of the output and scaled. Above 100% it supersamples.";
    row.Minimum = 50;
    row.Maximum = 200;
    row.Step = 10;
    row.Format = Percent;
    row.Read = [this] { return static_cast<int>(std::lround(mGraphics.InternalResolutionScale * 100.0F)); };
    row.Write = [this](int value) {
      auto settings = mGraphics;
      settings.InternalResolutionScale = static_cast<float>(value) / 100.0F;
      return ApplyGraphics(settings, false);
    };
    row.Available = [this] { return mGraphics.AntiAliasing != AntiAliasingMode::Upscaler; };
    row.UnavailableReason = "The upscaler quality sets the render scale.";
    row.Modified = [this] { return mGraphics.InternalResolutionScale != GraphicsSettings{}.InternalResolutionScale; };
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(graphicsChoice(
      "Anti-aliasing", "FXAA and SMAA smooth edges after rendering, MSAA samples the geometry, Temporal AA "
                       "accumulates frames, and NRI Upscaler renders smaller and reconstructs the image.",
      {"Off", "FXAA", "SMAA 1x (High)", "MSAA", "Temporal AA", "NRI Upscaler"},
      [](const auto &s) { return static_cast<int>(s.AntiAliasing); },
      [](auto &s, int value) { s.AntiAliasing = static_cast<AntiAliasingMode>(value); }));
  {
    auto row = graphicsChoice(
        "Upscaler", "The reconstruction used by NRI Upscaler: NIS and FSR run everywhere, XeSS and DLSS need their GPU.",
        {"NVIDIA Image Scaling (NIS)", "AMD FSR", "Intel XeSS", "NVIDIA DLSS"},
        [](const auto &s) { return static_cast<int>(s.Upscaler); },
        [](auto &s, int value) { s.Upscaler = static_cast<UpscalerProvider>(value); });
    row.Available = [this] { return mGraphics.AntiAliasing == AntiAliasingMode::Upscaler; };
    row.UnavailableReason = "Choose NRI Upscaler as anti-aliasing first.";
    tab.Rows.push_back(std::move(row));
    auto quality = graphicsChoice(
        "Upscaler quality", "How much smaller the scene is rendered before reconstruction: Native renders at full size.",
        {"Native", "Ultra quality", "Quality", "Balanced", "Performance", "Ultra performance"},
        [](const auto &s) { return static_cast<int>(s.UpscalerMode); },
        [](auto &s, int value) { s.UpscalerMode = static_cast<UpscalerQuality>(value); });
    quality.Available = [this] { return mGraphics.AntiAliasing == AntiAliasingMode::Upscaler; };
    quality.UnavailableReason = "Choose NRI Upscaler as anti-aliasing first.";
    tab.Rows.push_back(std::move(quality));
  }
  {
    Row row;
    row.Key = "Scene FOV";
    row.Kind = RowKind::Number;
    row.Help = "Widens the game's field of view in every scene.";
    row.Minimum = 100;
    row.Maximum = 150;
    row.Step = 5;
    row.Format = Multiplier;
    row.Read = [this] { return static_cast<int>(std::lround(mGraphics.FovMultiplier * 100.0F)); };
    row.Write = [this](int value) {
      auto settings = mGraphics;
      settings.FovMultiplier = static_cast<float>(value) / 100.0F;
      return ApplyGraphics(settings, false);
    };
    row.Modified = [this] { return mGraphics.FovMultiplier != GraphicsSettings{}.FovMultiplier; };
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(graphicsToggle(
      "Extended draw distance",
      "Draws and updates actors up to 5x farther, spawns bush, rock and tree groups farther away and removes the "
      "Kokiri fade. Distant actors keep moving, so gameplay can differ from the original.",
      [](const auto &s) { return s.ExtendedDrawDistance; }, [](auto &s, bool value) { s.ExtendedDrawDistance = value; }));
  if (mOwners.TopScreen) {
    // F1 > Display > TopScreen HUD: the standard profile fixes the layout,
    // the user chooses only the size.
    tab.Rows.push_back(Heading("HUD"));
    Row row;
    row.Key = "HUD size";
    row.Kind = RowKind::Choice;
    row.Help = "Native is the game's own HUD size; Optimized is the HUD sized for the single wide screen.";
    row.Choices = {"Native (100%)", "Optimized (70%)"};
    row.Read = [this] { return mTopScreen.HudScale == kTopScreenHudScaleNative ? 0 : 1; };
    row.Write = [this](int value) {
      auto config = mTopScreen;
      config.HudScale = value == 0 ? kTopScreenHudScaleNative : kTopScreenHudScaleOptimized;
      return ApplyTopScreen(config);
    };
    row.Modified = [this] { return mTopScreen.HudScale != kTopScreenHudScaleDefault; };
    tab.Rows.push_back(std::move(row));
  }
  document.Tabs.push_back(std::move(tab));
}

void InGameMenu::BuildGraphicsTab(Menu::Document &document) {
  using namespace Fast::Oot3d;
  Menu::Tab tab;
  tab.Title = "Graphics";
  const auto choice = [this](std::string key, std::string help, std::vector<std::string> choices,
                             std::function<int(const GraphicsSettings &)> get,
                             std::function<void(GraphicsSettings &, int)> set) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Choice;
    row.Help = std::move(help);
    row.Choices = std::move(choices);
    row.Read = [this, get] { return get(mGraphics); };
    row.Write = [this, set](int value) {
      auto settings = mGraphics;
      set(settings, value);
      return ApplyGraphics(settings, false);
    };
    row.ChoiceAvailable = [this, get, set](int value) {
      auto candidate = mGraphics;
      candidate.Preset = GraphicsPreset::Custom;
      set(candidate, value);
      const auto validation = GraphicsSettingsService::Validate(candidate, mCapabilities);
      return validation.Accepted() && get(validation.Value) == value;
    };
    row.Modified = [this, get] { return get(mGraphics) != get(GraphicsSettings{}); };
    return row;
  };

  tab.Rows.push_back(Heading("Lighting"));
  tab.Rows.push_back(choice(
      "Lighting model",
      "Native keeps the original shading (PBR on supported pack materials). Modern lights every surface per pixel "
      "from the scene lights, with PBR materials, shadows and global illumination. Scene lights, materials and "
      "texture packs are set in F1.",
      {"Native", "Modern"}, [](const auto &s) { return UsesFullSceneLighting(s.Effects.SurfaceLighting) ? 1 : 0; },
      [this](auto &s, int value) {
        s.Effects.SurfaceLighting = value == 1 ? SurfaceLightingMode::PerPixelPbr
                                    : mCapabilities.Has(GraphicsCapability::NativeSurfaceLighting)
                                        ? SurfaceLightingMode::NativePbr
                                        : SurfaceLightingMode::Native;
      }));
  tab.Rows.push_back(choice(
      "Global illumination", "Bounced light between surfaces: Brixelizer traces the scene's distance field, "
                             "Screen-space GI gathers from the rendered image. Modern lighting only.",
      {"Off", "Brixelizer GI", "Screen-space GI"},
      [](const auto &s) { return static_cast<int>(s.Effects.GlobalIllumination.Mode); },
      [](auto &s, int value) { s.Effects.GlobalIllumination.Mode = static_cast<GlobalIlluminationMode>(value); }));
  tab.Rows.push_back(choice(
      "Ambient occlusion", "Darkens creases and contact areas. Multiscale GTAO is the lightest, CACAO and MXAO are "
                           "the most detailed; their fine parameters are in F1.",
      {"Off", "Multiscale GTAO", "FidelityFX CACAO", "iMMERSE MXAO"},
      [](const auto &s) { return static_cast<int>(s.Effects.AmbientOcclusion); },
      [](auto &s, int value) { s.Effects.AmbientOcclusion = static_cast<AmbientOcclusionMode>(value); }));
  {
    auto row = choice("Shadows", "Virtual shadow maps cast the scene's sun or moon light from every actor and the "
                                 "world. Resolution and bias are in F1 > Lighting.",
                      {"Off", "Virtual Shadow Maps"},
                      [](const auto &s) { return static_cast<int>(s.Effects.DirectionalShadows.Mode); },
                      [](auto &s, int value) { s.Effects.DirectionalShadows.Mode = static_cast<DirectionalShadowMode>(value); });
    row.Available = [this] { return UsesFullSceneLighting(mGraphics.Effects.SurfaceLighting); };
    row.UnavailableReason = "Available with the Modern lighting model.";
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(choice(
      "Reflections", "Screen-space reflections on water and reflective materials: Hi-Z is the fast tracer, "
                     "FidelityFX SSSR the filtered one. Water keeps its own reflections when Off.",
      {"Off", "Hi-Z SSR", "FidelityFX SSSR"}, [](const auto &s) { return static_cast<int>(s.Effects.Reflections); },
      [](auto &s, int value) { s.Effects.Reflections = static_cast<ReflectionMode>(value); }));

  tab.Rows.push_back(Heading("Style"));
  tab.Rows.push_back(choice(
      "Toon", "Cel shading: the preview bands the lit image, the PICA material mode shades the materials "
              "themselves. Bands, outlines and tints are in F1 > Lighting > Toon.",
      {"Off", "Albedo-preserving preview", "PICA material toon"},
      [](const auto &s) { return static_cast<int>(s.Effects.Toon); },
      [](auto &s, int value) { s.Effects.Toon = static_cast<ToonMode>(value); }));
  {
    Row row;
    row.Key = "Grass";
    row.Kind = RowKind::Choice;
    row.Help = "Interactive grass on the fields: more blades and a longer draw distance at each level. "
               "Placement rules, masks, wind and appearance are F1 > Grass.";
    row.Choices = {"Off", "Low", "Medium", "High", "Custom"};
    row.Read = [this] { return static_cast<int>(mGraphics.Grass.Quality); };
    row.Write = [this](int value) -> WriteResult {
      if (value == static_cast<int>(GrassQuality::Custom)) return {"Custom grass is tuned in F1 > Grass", false};
      auto settings = mGraphics;
      ApplyGrassQualityPreset(settings.Grass, static_cast<GrassQuality>(value));
      return ApplyGraphics(settings, false);
    };
    row.ChoiceAvailable = [](int value) { return value != static_cast<int>(GrassQuality::Custom); };
    row.Modified = [this] { return mGraphics.Grass.Quality != GraphicsSettings{}.Grass.Quality; };
    tab.Rows.push_back(std::move(row));
  }
  {
    Row row;
    row.Key = "Distant grass cards";
    row.Kind = RowKind::Toggle;
    row.Help = "Cut-out cards replace the blades beyond the blade distance: one per cluster, one per group "
               "and one per patch without end. The blades then stop at the blade distance. Distances are "
               "F1 > Grass > Performance.";
    row.Read = [this] { return mGraphics.Grass.CardLodEnabled ? 1 : 0; };
    row.Write = [this](int value) {
      auto settings = mGraphics;
      settings.Grass.CardLodEnabled = value != 0;
      return ApplyGraphics(settings, false);
    };
    row.Modified = [this] { return mGraphics.Grass.CardLodEnabled != GraphicsSettings{}.Grass.CardLodEnabled; };
    row.Available = [this] { return mGraphics.Grass.Quality != GrassQuality::Off; };
    row.UnavailableReason = "Grass is off";
    tab.Rows.push_back(std::move(row));
  }
  document.Tabs.push_back(std::move(tab));
}

void InGameMenu::BuildControlsTab(Menu::Document &document) {
  Menu::Tab tab;
  tab.Title = "Controls";
  const auto controlToggle = [this](std::string key, std::string help, bool NativeControlConfig::*field) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Toggle;
    row.Help = std::move(help);
    row.Read = [this, field] { return mControls.*field ? 1 : 0; };
    row.Write = [this, field](int value) {
      auto config = mControls;
      config.*field = value != 0;
      return ApplyControls(config);
    };
    row.Modified = [this, field] { return mControls.*field != NativeControlConfig{}.*field; };
    return row;
  };
  // The TopScreen rows F1 > Controls > Camera offers (Free camera, C-stick
  // aiming). "Modified" compares with the standard profile, the defaults.
  const auto topScreenToggle = [this](std::string key, std::string help, bool TopScreenUiConfig::*field) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Toggle;
    row.Help = std::move(help);
    row.Read = [this, field] { return mTopScreen.*field ? 1 : 0; };
    row.Write = [this, field](int value) {
      auto config = mTopScreen;
      config.*field = value != 0;
      return ApplyTopScreen(config);
    };
    row.Modified = [this, field] { return mTopScreen.*field != TopScreenUiConfig{}.*field; };
    return row;
  };
  const auto topScreenLevel = [this](std::string key, std::string help, std::uint8_t TopScreenUiConfig::*field, int minimum,
                                     int maximum, int step, std::function<std::string(int)> format) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Number;
    row.Help = std::move(help);
    row.Minimum = minimum;
    row.Maximum = maximum;
    row.Step = step;
    row.Format = std::move(format);
    row.Read = [this, field] { return static_cast<int>(mTopScreen.*field); };
    row.Write = [this, field](int value) {
      auto config = mTopScreen;
      config.*field = static_cast<std::uint8_t>(value);
      return ApplyTopScreen(config);
    };
    row.Modified = [this, field] { return mTopScreen.*field != TopScreenUiConfig{}.*field; };
    return row;
  };

  tab.Rows.push_back(Heading("Devices"));
  tab.Rows.push_back(controlToggle("Keyboard", "The keyboard drives the game.", &NativeControlConfig::KeyboardEnabled));
  tab.Rows.push_back(controlToggle("Mouse", "The mouse aims and drives its bound buttons.", &NativeControlConfig::MouseEnabled));
  tab.Rows.push_back(controlToggle("Controller", "The selected game controller drives the game.", &NativeControlConfig::ControllerEnabled));
  tab.Rows.push_back(controlToggle("Capture mouse", "Hides and locks the pointer while the mouse is used in gameplay.",
                                   &NativeControlConfig::CaptureMouseInGameplay));
  {
    Row row;
    row.Key = "Control preset";
    row.Kind = RowKind::Choice;
    row.Help = "Applies a complete set of bindings. Custom is what any edited binding becomes; bindings are "
               "edited in F1 > Controls, which keeps the selected controller and its calibration.";
    row.Choices = {"Keyboard", "Keyboard + mouse", "Controller", "Custom"};
    row.Read = [this] { return static_cast<int>(mControls.Profile); };
    row.Write = [this](int value) -> WriteResult {
      if (value == static_cast<int>(NativeControlProfile::Custom)) return {"Custom is the result of editing a binding in F1", false};
      const auto previous = mControls;
      auto config = NativeControlPreset(static_cast<NativeControlProfile>(value));
      config.PreferredControllerGuid = previous.PreferredControllerGuid;
      config.PreferredControllerSerial = previous.PreferredControllerSerial;
      config.CalibrationControllerGuid = previous.CalibrationControllerGuid;
      config.CalibrationControllerSerial = previous.CalibrationControllerSerial;
      config.GyroscopeBiasDegreesPerSecond = previous.GyroscopeBiasDegreesPerSecond;
      config.AccelerometerNeutral = previous.AccelerometerNeutral;
      return ApplyControls(config);
    };
    row.ChoiceAvailable = [](int value) { return value != static_cast<int>(NativeControlProfile::Custom); };
    tab.Rows.push_back(std::move(row));
  }

  tab.Rows.push_back(Heading("Aiming"));
  {
    Row row;
    row.Key = "Aim source";
    row.Kind = RowKind::Choice;
    row.Help = "What moves the first-person aim: the mouse, the right stick, the controller's motion sensors, "
               "or Automatic, which follows the device in use.";
    for (int source = 0; source <= static_cast<int>(NativeMotionSource::Automatic); ++source) {
      // The configuration's lowercase names, capitalised like the other values.
      std::string label = NativeMotionSourceName(static_cast<NativeMotionSource>(source));
      if (!label.empty()) label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
      row.Choices.push_back(std::move(label));
    }
    row.Read = [this] { return static_cast<int>(mControls.NativeAimSource); };
    row.Write = [this](int value) {
      auto config = mControls;
      config.NativeAimSource = static_cast<NativeMotionSource>(value);
      return ApplyControls(config);
    };
    row.Modified = [this] { return mControls.NativeAimSource != NativeControlConfig{}.NativeAimSource; };
    tab.Rows.push_back(std::move(row));
  }
  tab.Rows.push_back(controlToggle("Invert aim X", "Mirrors the horizontal aim.", &NativeControlConfig::NativeAimInvertX));
  tab.Rows.push_back(controlToggle("Invert aim Y", "Mirrors the vertical aim.", &NativeControlConfig::NativeAimInvertY));

  if (mOwners.TopScreen) {
    tab.Rows.push_back(Heading("Camera"));
    tab.Rows.push_back(topScreenToggle("Free camera", "The right stick orbits the camera around Link during gameplay.",
                                       &TopScreenUiConfig::FreeCameraEnabled));
    tab.Rows.push_back(topScreenLevel("Free camera speed", "How fast the free camera turns.", &TopScreenUiConfig::FreeCameraSpeedLevel,
                                      0, 6, 1, [](int value) { return std::to_string(value + 1) + " / 7"; }));
    {
      Row row;
      row.Key = "Free camera smoothing";
      row.Kind = RowKind::Choice;
      row.Help = "Smooths the free camera's response to the stick.";
      row.Choices = {"Off", "Light", "Medium", "Default", "Heavy"};
      row.Read = [this] { return static_cast<int>(mTopScreen.FreeCameraSmoothing); };
      row.Write = [this](int value) {
        auto config = mTopScreen;
        config.FreeCameraSmoothing = static_cast<TopScreenFreeCameraSmoothing>(value);
        return ApplyTopScreen(config);
      };
      row.Modified = [this] { return mTopScreen.FreeCameraSmoothing != TopScreenUiConfig{}.FreeCameraSmoothing; };
      tab.Rows.push_back(std::move(row));
    }
    tab.Rows.push_back(topScreenToggle("Invert free camera X", "Mirrors the free camera's horizontal turn.",
                                       &TopScreenUiConfig::FreeCameraInvertX));
    tab.Rows.push_back(topScreenToggle("Invert free camera Y", "Mirrors the free camera's vertical turn.",
                                       &TopScreenUiConfig::FreeCameraInvertY));
    tab.Rows.push_back(topScreenLevel("Camera zoom", "The gameplay camera's distance from Link; 100% is the game's own.",
                                      &TopScreenUiConfig::CameraZoomPercent, kTopScreenCameraZoomMinimum,
                                      kTopScreenCameraZoomMaximum, 5, Percent));
    tab.Rows.push_back(topScreenLevel("Camera FOV", "The gameplay camera's field of view; 100% is the game's own.",
                                      &TopScreenUiConfig::CameraFovPercent, kTopScreenCameraFovMinimum,
                                      kTopScreenCameraFovMaximum, 5, Percent));

    tab.Rows.push_back(Heading("C-Stick aiming"));
    const int aimLevels = static_cast<int>(kTopScreenCStickAimSpeeds.size());
    tab.Rows.push_back(topScreenLevel("C-Stick aim speed", "How fast the right stick aims in first person.",
                                      &TopScreenUiConfig::CStickAimSpeedLevel, 0, aimLevels - 1, 1,
                                      [aimLevels](int value) { return std::to_string(value + 1) + " / " + std::to_string(aimLevels); }));
    tab.Rows.push_back(topScreenToggle("Invert C-Stick X", "Mirrors the right stick's horizontal aim.",
                                       &TopScreenUiConfig::CStickAimInvertX));
    tab.Rows.push_back(topScreenToggle("Invert C-Stick Y", "Mirrors the right stick's vertical aim.",
                                       &TopScreenUiConfig::CStickAimInvertY));
  }
  document.Tabs.push_back(std::move(tab));
}

void InGameMenu::BuildPhotoModeTab(Menu::Document &document) {
  Menu::Tab tab;
  tab.Title = "Photo mode";
  if (!mOwners.FreeCamera || !mOwners.PhotoMode) {
    document.Tabs.push_back(std::move(tab));
    return;
  }
  tab.Rows.push_back(Heading("Camera"));
  {
    Row row;
    row.Key = "Free camera (F6)";
    row.Kind = RowKind::Toggle;
    row.Help = "A camera detached from Link: the game draws from it and Link stays still, for this session. "
               "Close the menu and drive it with the controls listed below.";
    row.Read = [this] { return mOwners.FreeCamera->Active() ? 1 : 0; };
    row.Write = [this](int value) {
      mOwners.FreeCamera->SetActive(value != 0);
      return WriteResult{value != 0 ? "Free camera on for this session" : "Free camera off", false};
    };
    row.Modified = [this] { return mOwners.FreeCamera->Active(); };
    tab.Rows.push_back(std::move(row));
  }
  {
    Row row;
    row.Key = "Hide HUD";
    row.Kind = RowKind::Toggle;
    row.Help = "Hides hearts, magic, rupees, buttons, the action icon and the minimap for this session. "
               "Dialogue and menus stay.";
    row.Read = [this] { return mOwners.PhotoMode->HideHud.load(std::memory_order_relaxed) ? 1 : 0; };
    row.Write = [this](int value) {
      mOwners.PhotoMode->HideHud.store(value != 0, std::memory_order_relaxed);
      return WriteResult{value != 0 ? "HUD hidden for this session" : "HUD shown", false};
    };
    row.Modified = [this] { return mOwners.PhotoMode->HideHud.load(std::memory_order_relaxed); };
    tab.Rows.push_back(std::move(row));
  }
  {
    Row row;
    row.Key = "Free camera speed";
    row.Kind = RowKind::Number;
    row.Help = "How fast the camera moves, from a slow glide to a fast flight. Shift and Ctrl change it while driving.";
    row.Minimum = 1;
    row.Maximum = static_cast<int>(kPhotoSpeeds.size());
    row.Step = 1;
    row.Format = [](int value) {
      return std::to_string(static_cast<int>(kPhotoSpeeds[static_cast<std::size_t>(value - 1)])) + " units/s";
    };
    row.Read = [this] { return PhotoSpeedLevel(mOwners.FreeCamera->Speed()); };
    row.Write = [this](int value) {
      mOwners.FreeCamera->SetSpeed(kPhotoSpeeds[static_cast<std::size_t>(std::clamp(value, 1, static_cast<int>(kPhotoSpeeds.size())) - 1)]);
      return WriteResult{"Speed set for this session", false};
    };
    tab.Rows.push_back(std::move(row));
  }
  {
    Row row;
    row.Key = "Back to the game's view";
    row.Kind = RowKind::Action;
    row.Help = "Returns the free camera to where the game's camera is now.";
    row.Activate = [this] {
      mOwners.FreeCamera->ResetToGame();
      return WriteResult{"Camera reset to the game's view", false};
    };
    row.Available = [this] { return mOwners.FreeCamera->Active(); };
    row.UnavailableReason = "Turn the free camera on first.";
    tab.Rows.push_back(std::move(row));
  }

  // The camera's controls, read from the devices themselves (not the game's
  // bindings), as oot3d_native_a32_window_frame.cpp UpdateDevFreeCamera reads
  // them.
  tab.Rows.push_back(Heading("Controls"));
  const auto info = [](std::string key, std::string text) {
    Row row;
    row.Key = std::move(key);
    row.Kind = RowKind::Info;
    row.Text = std::move(text);
    return row;
  };
  tab.Rows.push_back(info("Camera on / off", "F6"));
  tab.Rows.push_back(info("Move", "W A S D \xC2\xB7 left stick"));           // ·
  tab.Rows.push_back(info("Down / up", "Q / E \xC2\xB7 L / R shoulders"));
  tab.Rows.push_back(info("Look", "Mouse drag (any button) \xC2\xB7 right stick"));
  tab.Rows.push_back(info("Faster / slower", "Shift / Ctrl"));
  tab.Rows.push_back(info("Hide HUD", "This tab, or F1 > Photo mode"));
  document.Tabs.push_back(std::move(tab));
}

void InGameMenu::BuildGameTab(Menu::Document &document) {
  Menu::Tab tab;
  tab.Title = "Game";
  if (mOwners.TopScreen) {
    // F1 > Controls > Gameplay and the SELECT action of Shortcuts. The
    // D-pad bindings and the minimap stay in F1 with the other bindings.
    const auto topScreenToggle = [this](std::string key, std::string help, bool TopScreenUiConfig::*field) {
      Row row;
      row.Key = std::move(key);
      row.Kind = RowKind::Toggle;
      row.Help = std::move(help);
      row.Read = [this, field] { return mTopScreen.*field ? 1 : 0; };
      row.Write = [this, field](int value) {
        auto config = mTopScreen;
        config.*field = value != 0;
        return ApplyTopScreen(config);
      };
      row.Modified = [this, field] { return mTopScreen.*field != TopScreenUiConfig{}.*field; };
      return row;
    };
    tab.Rows.push_back(Heading("Gameplay"));
    tab.Rows.push_back(topScreenToggle("Quick put away", "Items can be put away immediately instead of after one second.",
                                       &TopScreenUiConfig::QuickPutAway));
    tab.Rows.push_back(topScreenToggle("Landing roll", "Roll when holding forward after a fall, as on the N64.",
                                       &TopScreenUiConfig::LandingRollFix));
    tab.Rows.push_back(topScreenToggle("Roll dash", "Hold the action button after a roll to dash.", &TopScreenUiConfig::RollDash));
    tab.Rows.push_back(topScreenToggle("Aiming reticle", "Show a reticle while aiming the bow or slingshot.",
                                       &TopScreenUiConfig::AimingReticle));
    tab.Rows.push_back(topScreenToggle("3D rain", "Rain falls in the world around the camera and stops at roofs.",
                                       &TopScreenUiConfig::Rain3d));
    {
      Row row;
      row.Key = "Rain density";
      row.Kind = RowKind::Number;
      row.Help = "How many drops the 3D rain draws.";
      row.Minimum = kTopScreenRainDensityMinimum;
      row.Maximum = kTopScreenRainDensityMaximum;
      row.Step = kTopScreenRainDensityStep;
      row.Format = Percent;
      row.Read = [this] { return static_cast<int>(mTopScreen.Rain3dDensityPercent); };
      row.Write = [this](int value) {
        auto config = mTopScreen;
        config.Rain3dDensityPercent = static_cast<std::uint16_t>(value);
        return ApplyTopScreen(config);
      };
      row.Available = [this] { return mTopScreen.Rain3d; };
      row.UnavailableReason = "Turn 3D rain on first.";
      row.Modified = [this] { return mTopScreen.Rain3dDensityPercent != TopScreenUiConfig{}.Rain3dDensityPercent; };
      tab.Rows.push_back(std::move(row));
    }
    {
      Row row;
      row.Key = "SELECT button";
      row.Kind = RowKind::Choice;
      row.Help = "What the SELECT button does: open the Save screen, or show and hide the minimap.";
      row.Choices = {"Open Save screen", "Toggle minimap"};
      row.Read = [this] { return static_cast<int>(mTopScreen.SelectAction); };
      row.Write = [this](int value) {
        auto config = mTopScreen;
        config.SelectAction = static_cast<TopScreenSelectAction>(value);
        return ApplyTopScreen(config);
      };
      row.Modified = [this] { return mTopScreen.SelectAction != TopScreenUiConfig{}.SelectAction; };
      tab.Rows.push_back(std::move(row));
    }
  }

  if (mOwners.Language && !mOwners.Language->Available().empty()) {
    tab.Rows.push_back(Heading("Language"));
    Row row;
    row.Key = "Game language";
    row.Kind = RowKind::Choice;
    row.Help = "The game's text and voice language. It applies at the next full start of the game.";
    for (const auto &language : mOwners.Language->Available()) row.Choices.push_back(language.Label);
    row.Read = [this] {
      const auto &available = mOwners.Language->Available();
      for (std::size_t index = 0; index < available.size(); ++index)
        if (available[index].Code == mOwners.Language->Selected()) return static_cast<int>(index);
      return 0;
    };
    row.Write = [this](int value) -> WriteResult {
      const auto &available = mOwners.Language->Available();
      if (value < 0 || value >= static_cast<int>(available.size())) return {"Unknown language", true};
      if (!mOwners.Language->Select(available[static_cast<std::size_t>(value)].Code)) {
        return {"Not saved: " + mOwners.Language->Error(), true};
      }
      if (mOwners.Language->Selected() != mOwners.Language->BootLanguage()) return {"Saved. Restart the game to apply it.", false};
      return {};
    };
    row.Modified = [this] { return mOwners.Language->Selected() != mOwners.Language->BootLanguage(); };
    tab.Rows.push_back(std::move(row));
  }

  if (mOwners.Savestates) {
    tab.Rows.push_back(Heading("Save states"));
    const auto action = [this](std::string key, std::string help, SavestateRequestKind kind, bool withSettings) {
      Row row;
      row.Key = std::move(key);
      row.Kind = RowKind::Action;
      row.Help = std::move(help);
      row.Activate = [this, kind, withSettings] {
        mOwners.Savestates->Request({kind, withSettings, {}});
        return WriteResult{kind == SavestateRequestKind::Save ? "Saving the state" : "Loading the last state", false};
      };
      return row;
    };
    tab.Rows.push_back(action("Save state (F5)", "Saves the game moment to a new file in the save-state folder.",
                              SavestateRequestKind::Save, false));
    tab.Rows.push_back(action("Save state + settings", "Saves the game moment with the current host settings beside it.",
                              SavestateRequestKind::Save, true));
    tab.Rows.push_back(action("Load last state (F8)", "Loads the most recent save state.", SavestateRequestKind::Load, false));
    tab.Rows.push_back(action("Load last state + settings", "Loads the most recent save state and applies the settings it carries.",
                              SavestateRequestKind::Load, true));
  }
  document.Tabs.push_back(std::move(tab));
}

void InGameMenu::UpdateDisplayConfirmation() {
  using Fast::Oot3d::PresentationTransactionPhase;
  auto &runtime = Fast::Oot3d::GraphicsSettingsRuntime::Instance();
  const auto status = runtime.PresentationStatus();
  const bool awaiting = status.Phase == PresentationTransactionPhase::AwaitingConfirmation;
  if (awaiting) {
    runtime.PresentationConfirmationVisible();
    const auto metrics = runtime.DisplayMetrics();
    std::vector<std::string> lines;
    if (metrics.OutputWidth != 0U) {
      lines.push_back("Output: " + std::to_string(metrics.OutputWidth) + " x " + std::to_string(metrics.OutputHeight));
    }
    lines.push_back("Reverting in " + std::to_string((status.RemainingMilliseconds + 999U) / 1000U) + " seconds");
    if (!mConfirmationModal) {
      Menu::Modal modal;
      modal.Title = "Keep display settings?";
      modal.ConfirmLabel = "Keep";
      modal.CancelLabel = "Revert";
      modal.OnConfirm = [this] {
        Fast::Oot3d::GraphicsSettingsRuntime::Instance().ConfirmPresentation();
        mConfirmationModal = false;
        Refresh();
        mModel.SetStatus("Display settings kept", false);
      };
      modal.OnCancel = [this] {
        Fast::Oot3d::GraphicsSettingsRuntime::Instance().RollbackPresentation();
        mConfirmationModal = false;
        Refresh();
        mModel.SetStatus("Display settings reverted", false);
      };
      modal.Lines = lines;
      mModel.SetModal(std::move(modal));
      mConfirmationModal = true;
    } else if (auto &modal = mModel.CurrentModal(); modal.has_value()) {
      modal->Lines = std::move(lines);
    }
  } else if (mConfirmationModal) {
    // Timed out or decided elsewhere.
    mConfirmationModal = false;
    mModel.SetModal(std::nullopt);
    Refresh();
    const auto rejection = runtime.LastPresentationRejection();
    if (!rejection.empty()) mModel.SetStatus("Display change reverted: " + rejection, true);
  }
}

void InGameMenu::UpdateSavestateOutcome() {
  if (!mOwners.Savestates) return;
  const auto outcome = mOwners.Savestates->LastOutcome();
  if (!outcome.has_value()) return;
  if (mSeenSavestateOutcome.has_value() && *mSeenSavestateOutcome == outcome->When) return;
  const bool first = !mSeenSavestateOutcome.has_value();
  mSeenSavestateOutcome = outcome->When;
  if (!first) mModel.SetStatus(outcome->Message, !outcome->Success);
}

void InGameMenu::Update(const InGameMenuHostInput &input, bool hostGuiOwnsInput, double elapsedSeconds) {
  const bool toggle = input.Toggle && !mToggleHeld;
  mToggleHeld = input.Toggle;
  if (hostGuiOwnsInput) {
    if (mModel.IsOpen()) mModel.Close();
    mConfirmationModal = false;
    Fast::Oot3d::DeferDisplayConfirmation(false);
    return;
  }
  if (toggle) {
    if (mModel.IsOpen()) {
      mModel.Close();
    } else {
      RefreshSnapshots();
      mModel.Open();
      mRefreshTimer = 0.0;
      if (mOwners.Savestates) {
        const auto outcome = mOwners.Savestates->LastOutcome();
        mSeenSavestateOutcome = outcome.has_value() ? std::optional(outcome->When) : std::nullopt;
      }
    }
  }
  // The ImGui confirmation stays away while this menu asks the question.
  Fast::Oot3d::DeferDisplayConfirmation(mModel.IsOpen());
  if (!mModel.IsOpen()) {
    mConfirmationModal = false;
    return;
  }
  Menu::Input held;
  held.Up = input.Up;
  held.Down = input.Down;
  held.Left = input.Left;
  held.Right = input.Right;
  held.Confirm = input.Confirm;
  held.Back = input.Back;
  held.TabPrevious = input.TabPrevious;
  held.TabNext = input.TabNext;
  // The mouse, on the widescreen canvas the menu is drawn on: the same fit
  // the UI renderer applies to the presentation picture, so the mapping is
  // the picture's and not the window's.
  Menu::Pointer pointer;
  if (input.PointerPresent) {
    const auto point = Oot3d::Renderer::MapUiPresentationPoint(
        input.PictureWidth, input.PictureHeight, kMenuCanvasWidth, kMenuCanvasHeight,
        input.PointerX - static_cast<float>(input.PictureX), input.PointerY - static_cast<float>(input.PictureY));
    pointer.Present = point.Inside;
    pointer.X = point.X;
    pointer.Y = point.Y;
    pointer.Pressed = input.PointerPressed;
    pointer.Wheel = input.PointerWheel;
  }
  mModel.Update(held, pointer, elapsedSeconds);
  if (!mModel.IsOpen()) {
    Fast::Oot3d::DeferDisplayConfirmation(false);
    return;
  }
  UpdateDisplayConfirmation();
  UpdateSavestateOutcome();
  mRefreshTimer += elapsedSeconds;
  if (mRefreshTimer >= kLiveRefreshSeconds) {
    mRefreshTimer = 0.0;
    Refresh();
  }
}

std::vector<oot3d::ui::UiPrimitive> InGameMenu::BuildPresentation(float canvasWidth, float canvasHeight) {
  std::vector<oot3d::ui::UiPrimitive> primitives;
  Menu::AppendMenuPresentation(mModel, canvasWidth, canvasHeight, primitives);
  return primitives;
}

} // namespace Oot3dNativeGame
