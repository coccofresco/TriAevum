#pragma once

// Optional title-owned source/mask storage. Empty path retains the legacy
// configuration byte-for-byte. The host supplies the path before F1 starts;
// neither the codec nor the renderer knows title names or texture identities.
#include "ship/config/ConfigPersistence.h"
#include "fast/extension/graphics_settings_persistence.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace Fast::Extension {

class TitleGrassRulesFile {
public:
    void SetPath(std::filesystem::path path) {
        std::scoped_lock lock(mMutex);
        mPath = std::move(path);
    }
    [[nodiscard]] std::filesystem::path Path() const {
        std::scoped_lock lock(mMutex);
        return mPath;
    }
    // Overlay only content, never quality, lighting, budget or interaction.
    bool Load(nlohmann::json& root) const {
        const auto path = Path();
        if (path.empty() || !std::filesystem::exists(path)) return false;
        const auto file = Read(path);
        for (const auto* block : {"Grass", "GrassSavedPreset"})
            for (const auto* key : {"Sources", "Masks"})
                root["Graphics"][block][key] = file.at(block).at(key);
        return true;
    }
    // Apply AFTER the host's legacy/schema migration: the sidecar is always
    // current-format content, even beside an old or not-yet-created config.
    void LoadSettings(GraphicsSettingsLoadResult& settings) const {
        if (settings.UnsupportedFutureVersion || Path().empty()) return;
        // A new namespace starts empty, not by importing another title's edited
        // legacy rules. Retain those in the host document for its legacy owner.
        // Corrupt sidecars also leave an empty, fail-closed active rule set.
        settings.Value.Grass.Rules.clear(); settings.Value.Grass.Masks.clear();
        settings.Value.GrassSavedPreset.Rules.clear(); settings.Value.GrassSavedPreset.Masks.clear();
        nlohmann::json root{{"Graphics", SerializeGraphicsSettings(settings.Value)}};
        if (!Load(root)) return;
        const auto decoded = DeserializeGraphicsSettings(root.at("Graphics"), settings.Value);
        settings.Value.Grass.Rules = decoded.Value.Grass.Rules;
        settings.Value.Grass.Masks = decoded.Value.Grass.Masks;
        settings.Value.GrassSavedPreset.Rules = decoded.Value.GrassSavedPreset.Rules;
        settings.Value.GrassSavedPreset.Masks = decoded.Value.GrassSavedPreset.Masks;
        settings.Issues.insert(settings.Issues.end(), decoded.Issues.begin(), decoded.Issues.end());
        settings.NeedsRewrite |= !settings.Found;
        settings.Found = true;
    }
    // Save the sidecar atomically before the host config. Keep that config's
    // legacy rules so another title using the same file cannot lose its work.
    // A partial host save is reported as failed and is safe to retry.
    bool Store(const nlohmann::json& graphics, const nlohmann::json& previousRoot,
               nlohmann::json& hostGraphics) const {
        hostGraphics = graphics;
        const auto path = Path();
        if (path.empty()) return true;
        try {
            // Never overwrite an unreadable/future-version authoring file.
            nlohmann::json file = std::filesystem::exists(path) ? Read(path)
                : nlohmann::json{{"schema", "triaevum.grass-rules"}, {"version", 1}};
            for (const auto* block : {"Grass", "GrassSavedPreset"}) {
                for (const auto* key : {"Sources", "Masks"}) {
                    file[block][key] = graphics.at(block).at(key);
                    const auto old = previousRoot.value("Graphics", nlohmann::json::object());
                    const auto section = old.value(block, nlohmann::json::object());
                    hostGraphics[block][key] = section.value(key, nlohmann::json::array());
                }
            }
            const auto text = file.dump(2) + "\n";
            if (text.size() > 4U * 1024U * 1024U) return false;
            if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
            return Ship::WriteConfigFileAtomically(path, text).Success;
        } catch (const std::exception&) { return false; }
    }
private:
    static nlohmann::json Read(const std::filesystem::path& path) {
        if (std::filesystem::file_size(path) > 4U * 1024U * 1024U)
            throw std::runtime_error("grass rule file too large: " + path.string());
        std::ifstream stream(path, std::ios::binary);
        auto file = nlohmann::json::parse(stream);
        if (file.value("schema", "") != "triaevum.grass-rules" || file.value("version", 0) != 1)
            throw std::runtime_error("unsupported grass rule file: " + path.string());
        for (const auto* block : {"Grass", "GrassSavedPreset"})
            for (const auto* key : {"Sources", "Masks"})
                if (!file.at(block).at(key).is_array() || file.at(block).at(key).size() > 256)
                    throw std::runtime_error("invalid grass rule array: " + path.string());
        return file;
    }
    mutable std::mutex mMutex;
    std::filesystem::path mPath;
};

// One running title, installed before the persistence port takes its snapshot.
inline TitleGrassRulesFile& ActiveTitleGrassRulesFile() {
    static TitleGrassRulesFile file;
    return file;
}
} // namespace Fast::Extension
