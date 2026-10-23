#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Oot3dNativeGame {
enum class SceneEnvironmentGroup : uint8_t { Indoor, TimeFlows, TimeAtEntry, FixedTime };
inline constexpr std::array<const char*, 4> kEnvironmentGroupIds{
    "indoor", "outdoor-time-flows", "outdoor-time-at-entry", "outdoor-fixed-time"};
inline constexpr std::array<const char*, 4> kEnvironmentGroupLabels{
    "Indoor", "Outdoor, time flows", "Outdoor, time at entry", "Outdoor, fixed time"};
inline const char* EnvironmentGroupId(SceneEnvironmentGroup group) { return kEnvironmentGroupIds.at(size_t(group)); }
inline const char* EnvironmentGroupLabel(SceneEnvironmentGroup group) { return kEnvironmentGroupLabels.at(size_t(group)); }
inline std::optional<SceneEnvironmentGroup> ParseEnvironmentGroup(std::string_view id) {
  for (size_t i = 0; i < kEnvironmentGroupIds.size(); ++i)
    if (id == kEnvironmentGroupIds[i]) return SceneEnvironmentGroup(i);
  return {};
}
// DefaultScene and DefaultGroup: the release's read-only profiles
// (resources/environments), each after the user's file of the same level.
enum class SceneEnvironmentSource : uint8_t { Scene, Group, Base, Day, DefaultScene, DefaultGroup };
inline const char* EnvironmentSourceLabel(SceneEnvironmentSource source) {
  switch (source) {
  case SceneEnvironmentSource::Scene: return "Scene's own";
  case SceneEnvironmentSource::Group: return "Group standard";
  case SceneEnvironmentSource::Base: return "Base";
  case SceneEnvironmentSource::Day: return "Day result";
  case SceneEnvironmentSource::DefaultScene: return "TriAevum default (scene)";
  case SceneEnvironmentSource::DefaultGroup: return "TriAevum default (group)";
  }
  return "Base";
}
} // namespace Oot3dNativeGame
