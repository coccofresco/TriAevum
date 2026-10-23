#include "fast/oot3d/graphics_preset_library.h"

#include "fast/oot3d/graphics_settings_persistence.h"

#include <algorithm>
#include <array>

#include <nlohmann/json.hpp>

namespace Fast::Oot3d {
namespace {

bool SameName(std::string_view left, std::string_view right) {
    const auto lower = [](char value) {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    };
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(),
                      [&](char a, char b) { return lower(a) == lower(b); });
}

constexpr std::array<std::string_view, 4> kReservedNames{"Authentic", "Enhanced", "Toon", "Custom"};

// The documents of the host-owned state (KeepHostOwnedSettings), which a
// preset never applies: a preset document leaves them out, as the Enhanced
// document does, and reading one restores them from the defaults.
constexpr std::array<std::string_view, 8> kHostOwnedPaths{ "/Window",           "/Output",
                                                           "/Presentation",     "/TexturePacks",
                                                           "/Grass/Sources",    "/Grass/Masks",
                                                           "/GrassSavedPreset", "/Effects/Reflections/Materials" };

nlohmann::json PresetDocument(const GraphicsSettings& settings) {
    auto document = SerializeGraphicsSettings(settings);
    for (const auto path : kHostOwnedPaths) {
        const nlohmann::json::json_pointer pointer{std::string(path)};
        if (document.contains(pointer)) document[pointer.parent_pointer()].erase(pointer.back());
    }
    return document;
}

nlohmann::json WithHostOwnedDefaults(nlohmann::json document) {
    static const auto defaults = SerializeGraphicsSettings(GraphicsSettings{});
    for (const auto path : kHostOwnedPaths) {
        const nlohmann::json::json_pointer pointer{std::string(path)};
        if (!document.contains(pointer) || document[pointer].is_null()) document[pointer] = defaults[pointer];
    }
    return document;
}

// What a user preset stores: the preset part of the settings, as Custom (a
// user preset is a custom configuration, never a built-in one), with the
// host-owned state at its defaults since applying the preset keeps the
// current one.
GraphicsSettings PresetPart(const GraphicsSettings& settings) {
    auto value = GraphicsSettingsService::KeepHostOwnedSettings(settings, GraphicsSettings{});
    value.Preset = GraphicsPreset::Custom;
    return value;
}

} // namespace

std::string GraphicsPresetLibrary::NormalizeName(std::string_view name) {
    const auto space = [](char value) { return value == ' ' || value == '\t'; };
    while (!name.empty() && space(name.front())) name.remove_prefix(1);
    while (!name.empty() && space(name.back())) name.remove_suffix(1);
    return std::string(name);
}

GraphicsPresetNameError GraphicsPresetLibrary::ValidateName(std::string_view name) {
    const auto normalized = NormalizeName(name);
    if (normalized.empty()) return GraphicsPresetNameError::Empty;
    if (normalized.size() > kMaximumGraphicsPresetNameLength) return GraphicsPresetNameError::TooLong;
    // "##" would hide the rest of the name in the menu.
    if (normalized.find("##") != std::string::npos ||
        std::any_of(normalized.begin(), normalized.end(), [](char value) {
            const auto byte = static_cast<unsigned char>(value);
            return byte < 0x20U || byte == 0x7FU;
        }))
        return GraphicsPresetNameError::InvalidCharacter;
    if (std::any_of(kReservedNames.begin(), kReservedNames.end(),
                    [&](std::string_view reserved) { return SameName(normalized, reserved); }))
        return GraphicsPresetNameError::BuiltIn;
    return GraphicsPresetNameError::None;
}

const char* GraphicsPresetLibrary::DescribeNameError(GraphicsPresetNameError error) noexcept {
    switch (error) {
        case GraphicsPresetNameError::None: return "";
        case GraphicsPresetNameError::Empty: return "Enter a preset name.";
        case GraphicsPresetNameError::TooLong: return "The name is longer than 40 characters.";
        case GraphicsPresetNameError::InvalidCharacter: return "The name contains a control character or \"##\".";
        case GraphicsPresetNameError::BuiltIn: return "Built-in presets cannot be changed: choose another name.";
    }
    return "Invalid preset name.";
}

const UserGraphicsPreset* GraphicsPresetLibrary::Find(std::string_view name) const {
    const auto normalized = NormalizeName(name);
    const auto found = std::find_if(mPresets.begin(), mPresets.end(),
                                    [&](const UserGraphicsPreset& preset) { return SameName(preset.Name, normalized); });
    return found == mPresets.end() ? nullptr : &*found;
}

void GraphicsPresetLibrary::Save(std::string_view name, const GraphicsSettings& settings) {
    const auto normalized = NormalizeName(name);
    const auto found = std::find_if(mPresets.begin(), mPresets.end(),
                                    [&](const UserGraphicsPreset& preset) { return SameName(preset.Name, normalized); });
    std::erase_if(mPreserved, [&](const PreservedPreset& preset) { return SameName(preset.Name, normalized); });
    if (found != mPresets.end()) {
        found->Name = normalized;
        found->Settings = PresetPart(settings);
        return;
    }
    mPresets.push_back({normalized, PresetPart(settings)});
}

bool GraphicsPresetLibrary::Remove(std::string_view name) {
    const auto normalized = NormalizeName(name);
    const auto found = std::find_if(mPresets.begin(), mPresets.end(),
                                    [&](const UserGraphicsPreset& preset) { return SameName(preset.Name, normalized); });
    if (found == mPresets.end()) return false;
    mPresets.erase(found);
    return true;
}

const UserGraphicsPreset* GraphicsPresetLibrary::Match(const GraphicsSettings& settings) const {
    if (settings.Preset != GraphicsPreset::Custom || mPresets.empty()) return nullptr;
    const auto current = SerializeGraphicsSettings(settings);
    for (const auto& preset : mPresets) {
        auto applied = GraphicsSettingsService::KeepHostOwnedSettings(preset.Settings, settings);
        applied.Preset = GraphicsPreset::Custom;
        if (SerializeGraphicsSettings(applied) == current) return &preset;
    }
    return nullptr;
}

nlohmann::json GraphicsPresetLibrary::Serialize() const {
    auto presets = nlohmann::json::array();
    for (const auto& preset : mPresets)
        presets.push_back({{"Name", preset.Name}, {"Graphics", PresetDocument(preset.Settings)}});
    for (const auto& preset : mPreserved)
        presets.push_back({{"Name", preset.Name}, {"Graphics", nlohmann::json::parse(preset.Document)}});
    return {{"SchemaVersion", kGraphicsPresetLibrarySchemaVersion}, {"Presets", std::move(presets)}};
}

GraphicsPresetLibrary GraphicsPresetLibrary::Deserialize(const nlohmann::json& block,
                                                         std::vector<std::string>* issues) {
    const auto report = [&](std::string message) {
        if (issues != nullptr) issues->push_back(std::move(message));
    };
    GraphicsPresetLibrary library;
    if (!block.is_object()) {
        if (!block.is_null()) report("GraphicsPresets is not an object");
        return library;
    }
    const auto version = block.find("SchemaVersion");
    if (version == block.end() || !version->is_number_unsigned()) {
        report("GraphicsPresets.SchemaVersion is missing");
        return library;
    }
    if (version->get<uint64_t>() > kGraphicsPresetLibrarySchemaVersion) {
        library.mReadOnlyReason = "The graphics presets were saved by a newer version of TriAevum.";
        report("GraphicsPresets: " + library.mReadOnlyReason);
        return library;
    }
    const auto presets = block.find("Presets");
    // The config store flattens an empty list to null.
    if (presets != block.end() && presets->is_null()) return library;
    if (presets == block.end() || !presets->is_array()) {
        report("GraphicsPresets.Presets is not a list");
        return library;
    }
    for (const auto& entry : *presets) {
        const auto name = entry.is_object() && entry.contains("Name") && entry["Name"].is_string()
            ? entry["Name"].get<std::string>() : std::string{};
        if (const auto error = ValidateName(name); error != GraphicsPresetNameError::None) {
            report("GraphicsPresets: skipped preset \"" + name + "\": " + DescribeNameError(error));
            continue;
        }
        if (library.Find(name) != nullptr ||
            std::any_of(library.mPreserved.begin(), library.mPreserved.end(),
                        [&](const PreservedPreset& preset) { return SameName(preset.Name, NormalizeName(name)); })) {
            report("GraphicsPresets: skipped duplicate preset \"" + name + "\"");
            continue;
        }
        const auto graphics = entry.find("Graphics");
        if (graphics == entry.end() || !graphics->is_object()) {
            report("GraphicsPresets: skipped preset \"" + name + "\": no settings");
            continue;
        }
        const auto loaded = DeserializeGraphicsSettings(WithHostOwnedDefaults(*graphics));
        if (loaded.UnsupportedFutureVersion) {
            report("GraphicsPresets: preset \"" + name + "\" was saved by a newer version; kept, not offered");
            library.mPreserved.push_back({NormalizeName(name), graphics->dump()});
            continue;
        }
        for (const auto& issue : loaded.Issues)
            report("GraphicsPresets \"" + name + "\" " + issue.Field + ": " + issue.Message);
        library.Save(name, loaded.Value);
    }
    return library;
}

} // namespace Fast::Oot3d
