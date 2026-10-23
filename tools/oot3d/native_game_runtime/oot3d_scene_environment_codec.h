#pragma once

// The scene environment profile file (scene-XXXX[.night].environment.json,
// schema "triaevum.scene-environment", versions 1-6; group-ID files use v6)
// for the runtime, the F1
// editor and the developer environment tool alike: decoding that reports
// every problem with its JSON pointer, canonical encoding, and the renderer
// inputs a profile resolves to.

#include "oot3d_scene_environment.h"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct SceneEnvironmentDiagnostic {
  // JSON pointer of the offending value ("" for the document).
  std::string Pointer;
  std::string Message;
  bool operator==(const SceneEnvironmentDiagnostic &) const = default;
};

struct SceneEnvironmentDocument {
  // Set when there are no errors.
  std::optional<SceneEnvironmentProfile> Profile;
  // As the document names them.
  std::optional<uint16_t> SceneId;
  bool Night = false;
  std::optional<SceneEnvironmentGroup> Group;
  std::vector<SceneEnvironmentDiagnostic> Errors;
};

// Decodes a profile document. With an expected scene or time of day, a
// document naming another is an error. Every error is reported, none thrown.
[[nodiscard]] SceneEnvironmentDocument
DecodeSceneEnvironment(const nlohmann::json &document, std::optional<uint16_t> expectedScene = std::nullopt,
                       std::optional<bool> expectedNight = std::nullopt,
                       std::optional<SceneEnvironmentGroup> expectedGroup = std::nullopt);
// A profile file as the runtime reads it: at most 64 KiB, then decoded.
inline constexpr uintmax_t kMaximumSceneEnvironmentFileBytes = 65536U;
struct SceneEnvironmentFile {
  bool Exists = false;
  // The bytes read (empty when missing, unreadable or too large).
  std::string Text;
  SceneEnvironmentDocument Document;
};
[[nodiscard]] SceneEnvironmentFile LoadSceneEnvironmentFile(const std::filesystem::path &path,
                                                            std::optional<uint16_t> expectedScene = std::nullopt,
                                                            std::optional<bool> expectedNight = std::nullopt,
                       std::optional<SceneEnvironmentGroup> expectedGroup = std::nullopt);

// Ranges, counts and identities of a profile (pointers as in the file).
[[nodiscard]] std::vector<SceneEnvironmentDiagnostic>
ValidateSceneEnvironment(const SceneEnvironmentProfile &profile);
// "pointer: message" of the first error, and how many more there are.
[[nodiscard]] std::string
DescribeSceneEnvironmentErrors(const std::vector<SceneEnvironmentDiagnostic> &errors);

// Scene saves retain version 5; group saves use version 6 and groupId instead
// of sceneId. Numbers are written in their shortest exact form.
[[nodiscard]] nlohmann::json EncodeSceneEnvironment(const SceneEnvironmentProfile &profile, uint16_t scene,
                                                    bool night);
[[nodiscard]] nlohmann::json EncodeGroupEnvironment(const SceneEnvironmentProfile &profile, SceneEnvironmentGroup group, bool night);
// Two-space indentation and a final newline: the bytes the runtime saves.
[[nodiscard]] std::string FormatSceneEnvironment(const nlohmann::json &document);

// The renderer's identity of a placed light: unique per scene, time of day
// and light id (the whole 32-bit id).
[[nodiscard]] uint64_t PlacedLightIdentity(uint16_t scene, bool night, uint32_t light);
// What the renderer receives for this profile (hidden lights left out, as
// many placed lights as it takes).
[[nodiscard]] Fast::Renderer3ds::PicaEnvironmentLighting
ResolveSceneEnvironment(const SceneEnvironmentProfile &profile, uint16_t scene, bool night);
// For agents and reviews: the authored document, the renderer inputs, the
// values that depend on the scene's own lights at run time, and notes on
// budgets and limits.
[[nodiscard]] nlohmann::json ExplainSceneEnvironment(const SceneEnvironmentProfile &profile, uint16_t scene,
                                                     bool night);

} // namespace Oot3dNativeGame
