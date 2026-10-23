#pragma once

#include <string_view>
#include "fast/oot3d/graphics_settings.h"

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace Fast::Oot3d {

inline constexpr uint32_t kGraphicsSettingsSchemaVersion = 15U;

struct GraphicsSettingsLoadResult {
    GraphicsSettings Value;
    std::vector<GraphicsSettingsIssue> Issues;
    bool Found = false;
    bool NeedsRewrite = false;
    bool UnsupportedFutureVersion = false;
    bool MigratedLegacy = false;
};

// Serializes the complete renderer configuration as the value of the
// top-level "Graphics" config block. Enum values and texture hashes remain
// human-readable and stable across compiler changes.
[[nodiscard]] nlohmann::json
SerializeGraphicsSettings(const GraphicsSettings& settings);

// Decodes one "Graphics" block. Missing or malformed fields retain the
// supplied fallback and are reported as corrected issues. A future schema is
// never rewritten so a newer build's data cannot be destroyed.
[[nodiscard]] GraphicsSettingsLoadResult DeserializeGraphicsSettings(
    const nlohmann::json& document,
    GraphicsSettings fallback = {});

// The Enhanced preset, the default of a new installation: a tuned play
// configuration's "Graphics" block, without the state a preset never sets
// (window, output, presentation, texture packs, grass sources, reflection
// materials, the saved grass preset).
[[nodiscard]] std::string_view EnhancedPresetDocument() noexcept;
[[nodiscard]] const GraphicsSettings& EnhancedPresetSettings();

// Loads the top-level "Graphics" block from a runtime config document.
// When it is absent, legacy Window.* and CVars.* values are migrated once.
[[nodiscard]] GraphicsSettingsLoadResult LoadGraphicsSettingsConfig(
    const nlohmann::json& configRoot,
    GraphicsSettings fallback = {});

// The title's shipped grass source and mask rules, {"Sources": [...],
// "Masks": [...]}, installed by its adapter before any settings load. A
// "Graphics" block older than schema 15 takes them in place of its own rules
// in Grass and GrassSavedPreset; every other grass setting (quality, budget,
// performance, generation, appearance, interaction, wind) is kept.
void InstallTitleGrassRuleDefaults(const nlohmann::json& rules);
void ClearTitleGrassRuleDefaults();

// Central guard used by every runtime save site. Automated environment
// overrides are ephemeral, and no state is durable while a display-mode
// transaction is awaiting confirmation or rollback acknowledgement.
[[nodiscard]] bool ShouldPersistGraphicsSettings(
    bool environmentOverridesActive,
    bool presentationTransactionActive) noexcept;

} // namespace Fast::Oot3d
