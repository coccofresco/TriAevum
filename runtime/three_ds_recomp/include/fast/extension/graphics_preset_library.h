#pragma once

#include "fast/extension/graphics_settings.h"

#include <cstddef>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Fast::Extension {

inline constexpr uint32_t kGraphicsPresetLibrarySchemaVersion = 1U;
inline constexpr size_t kMaximumGraphicsPresetNameLength = 40U;

// A graphics preset saved by the user. It holds the settings a preset
// selects; host presentation and content identities (window, output, VSync,
// texture packs, grass sources, reflection materials, the saved grass preset)
// stay the user's current ones, exactly as with the built-in presets.
struct UserGraphicsPreset {
    std::string Name;
    GraphicsSettings Settings;
};

enum class GraphicsPresetNameError : uint8_t { None, Empty, TooLong, InvalidCharacter, BuiltIn };

// The user's preset library. Built-in presets (BuiltInGraphicsPresets)
// and Custom are not part of it and their names cannot be taken, so a
// built-in preset can never be overwritten or deleted.
class GraphicsPresetLibrary {
  public:
    // Surrounding spaces are not part of a name.
    [[nodiscard]] static std::string NormalizeName(std::string_view name);
    [[nodiscard]] static GraphicsPresetNameError ValidateName(std::string_view name);
    [[nodiscard]] static const char* DescribeNameError(GraphicsPresetNameError error) noexcept;

    [[nodiscard]] const std::vector<UserGraphicsPreset>& Presets() const noexcept { return mPresets; }
    // Names compare without regard to ASCII case.
    [[nodiscard]] const UserGraphicsPreset* Find(std::string_view name) const;
    // Stores the preset part of the settings under the name, replacing a
    // preset of the same name in place. The name must be valid.
    void Save(std::string_view name, const GraphicsSettings& settings);
    bool Remove(std::string_view name);
    // The user preset the settings are, unmodified, or nullptr.
    [[nodiscard]] const UserGraphicsPreset* Match(const GraphicsSettings& settings) const;

    // Non-empty when the library cannot be written without destroying data
    // (a newer library schema): it then offers nothing and must not be saved.
    [[nodiscard]] const std::string& ReadOnlyReason() const noexcept { return mReadOnlyReason; }

    // The top-level "GraphicsPresets" config block.
    [[nodiscard]] nlohmann::json Serialize() const;
    // Entries with an invalid or duplicate name or without settings are
    // skipped and reported. A preset saved by a newer version is not offered
    // but kept as it is, so saving the library never destroys it; saving a
    // preset of the same name replaces it.
    [[nodiscard]] static GraphicsPresetLibrary Deserialize(const nlohmann::json& block,
                                                           std::vector<std::string>* issues = nullptr);

  private:
    struct PreservedPreset {
        std::string Name;
        std::string Document;
    };
    std::vector<UserGraphicsPreset> mPresets;
    std::vector<PreservedPreset> mPreserved;
    std::string mReadOnlyReason;
};

} // namespace Fast::Extension
