#include "oot3d_graphics_settings_schedule.h"

#include <fstream>
#include <stdexcept>

namespace Oot3dNativeGame {

GraphicsSettingsSchedule
GraphicsSettingsSchedule::Parse(const nlohmann::json &document) {
  if (!document.is_object() || !document.contains("schema") ||
      document.at("schema") != kSchema)
    throw std::runtime_error(std::string("graphics settings schedule schema must be ") + kSchema);
  const auto steps = document.find("steps");
  if (steps == document.end() || !steps->is_array())
    throw std::runtime_error("graphics settings schedule requires a steps array");
  GraphicsSettingsSchedule schedule;
  for (const auto &step : *steps) {
    const bool hasGraphics = step.is_object() && step.contains("graphics");
    const bool hasAudio = step.is_object() && step.contains("audio");
    if (!step.is_object() || !step.contains("frame") ||
        !step.at("frame").is_number_unsigned() || (!hasGraphics && !hasAudio) ||
        (hasGraphics && !step.at("graphics").is_object()) ||
        (hasAudio && !step.at("audio").is_object()))
      throw std::runtime_error(
          "graphics settings schedule steps need an unsigned frame and a graphics and/or audio object");
    GraphicsSettingsScheduleStep parsed;
    if (hasGraphics) parsed.Graphics = step.at("graphics");
    if (hasAudio) {
      for (const auto &[key, value] : step.at("audio").items()) {
        if (key == "PreviewTone") {
          if (!value.is_boolean())
            throw std::runtime_error("audio PreviewTone must be a boolean");
          parsed.PreviewTone = value.get<bool>();
          continue;
        }
        if (!IsScheduledAudioKey(key))
          throw std::runtime_error("unknown scheduled audio setting " + key);
        if (key == "MasterVolume" ? !value.is_number_integer() : !value.is_boolean())
          throw std::runtime_error("scheduled audio setting " + key + " has the wrong type");
        parsed.Audio[key] = value;
      }
      if (parsed.Audio.is_null() && !parsed.PreviewTone)
        throw std::runtime_error("an audio step must change a setting or ask for a preview tone");
    }
    const auto frame = step.at("frame").get<uint64_t>();
    if (frame > UINT32_MAX ||
        (!schedule.mSteps.empty() && frame <= schedule.mSteps.back().Frame))
      throw std::runtime_error("graphics settings schedule frames must increase");
    parsed.Frame = static_cast<uint32_t>(frame);
    schedule.mSteps.push_back(std::move(parsed));
  }
  return schedule;
}

GraphicsSettingsSchedule
GraphicsSettingsSchedule::LoadFile(const std::filesystem::path &path) {
  if (path.empty()) return {};
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot open graphics settings schedule " + path.string());
  return Parse(nlohmann::json::parse(input));
}

std::vector<const GraphicsSettingsScheduleStep *>
GraphicsSettingsSchedule::TakeDue(uint32_t runFrame) {
  std::vector<const GraphicsSettingsScheduleStep *> due;
  while (mNext < mSteps.size() && mSteps[mNext].Frame <= runFrame)
    due.push_back(&mSteps[mNext++]);
  return due;
}

bool IsScheduledAudioKey(const std::string &key) {
  return key == "MasterVolume" || key == "Muted" || key == "MuteWhenUnfocused";
}

nlohmann::json MergeScheduledGraphics(nlohmann::json current,
                                      const nlohmann::json &patch) {
  current.merge_patch(patch);
  return current;
}

} // namespace Oot3dNativeGame
