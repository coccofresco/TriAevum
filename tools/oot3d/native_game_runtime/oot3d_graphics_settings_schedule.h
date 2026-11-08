#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

// Developer qualification input (--graphics-settings-schedule): at a run
// frame, a partial "Graphics" block is merged onto the current settings and
// applied through the same runtime call as an F1 commit, without persisting.
// It lets deterministic runs switch effects on and off mid-run; the settings
// runtime stays the only owner of the values.
//
// { "schema": "oot3d.graphics_settings_schedule.v1",
//   "steps": [ { "frame": 300,
//                "graphics": { "Effects": { "GlobalIllumination": { "Mode": "Off" } } } } ] }
//
// A step may instead (or also) carry an "audio" block: a partial "Audio"
// settings block (MasterVolume, Muted, MuteWhenUnfocused) merged onto the
// current output settings and applied through Ship::AudioOutputPolicy::
// Preview (the one owner; applied, never saved), plus "PreviewTone": true to
// ask the owner for one preview tone. Window focus is not schedulable: the
// frame loop reports the real keyboard focus to the owner every frame.
//   { "frame": 900, "audio": { "MasterVolume": 40 } }
//   { "frame": 960, "audio": { "Muted": true } }
struct GraphicsSettingsScheduleStep {
  uint32_t Frame = 0;
  nlohmann::json Graphics; // null when the step changes no graphics
  nlohmann::json Audio;    // null when the step changes no audio
  bool PreviewTone = false;
};

class GraphicsSettingsSchedule {
public:
  static constexpr const char *kSchema = "oot3d.graphics_settings_schedule.v1";

  // An empty path disables the schedule. Throws on malformed documents:
  // unknown schema, missing or non-object "graphics", frames not strictly
  // increasing.
  [[nodiscard]] static GraphicsSettingsSchedule
  Parse(const nlohmann::json &document);
  [[nodiscard]] static GraphicsSettingsSchedule
  LoadFile(const std::filesystem::path &path);

  [[nodiscard]] bool Enabled() const { return !mSteps.empty(); }
  [[nodiscard]] const std::vector<GraphicsSettingsScheduleStep> &
  Steps() const {
    return mSteps;
  }
  // The steps due at `runFrame`, each returned once, in order.
  [[nodiscard]] std::vector<const GraphicsSettingsScheduleStep *>
  TakeDue(uint32_t runFrame);

private:
  std::vector<GraphicsSettingsScheduleStep> mSteps;
  size_t mNext = 0;
};

// The audio keys a step may name (the "Audio" block of the settings).
[[nodiscard]] bool IsScheduledAudioKey(const std::string &key);

// Merges a step onto the serialized current block (JSON merge patch).
[[nodiscard]] nlohmann::json
MergeScheduledGraphics(nlohmann::json current, const nlohmann::json &patch);

} // namespace Oot3dNativeGame
