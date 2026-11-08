#include "triaevum_product_info.h"
#include "triaevum_data_routing.h"
#include "triaevum_product_build.h"

#include "fast/extension/graphics_settings.h"
#include "fast/extension/graphics_settings_persistence.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <ostream>
#include <system_error>
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
#include "oot3d_native_direct_aot.h"
#endif

nlohmann::json TriAevumProductDefaultConfig() {
    using namespace Fast::Extension;
    // A new installation starts from the product's default graphics (owned by
    // the graphics runtime: the platform's preset, the default window and the
    // platform's choices) with the title's grass source texture rules, which
    // no preset owns. Existing installations retain their config.
    auto graphics = ProductDefaultGraphicsSettings();
    const auto defaults = nlohmann::json::parse(
#include "triaevum_product_graphics.inc"
    );
    auto document = SerializeGraphicsSettings(graphics);
    document.merge_patch(defaults);
    document["GrassSavedPreset"] = document["Grass"];
    graphics = DeserializeGraphicsSettings(document, graphics).Value;
    return {{"Graphics", SerializeGraphicsSettings(graphics)}};
}

bool InitializeTriAevumProductConfig(const std::filesystem::path& path,
                                     bool* written, std::string* error) {
    *written = false;
    nlohmann::json root = nlohmann::json::object();
    std::error_code status;
    if (std::filesystem::exists(path, status)) {
        std::ifstream input(path, std::ios::binary);
        try {
            input >> root;
        } catch (const std::exception& exception) {
            // A malformed user file must not be silently replaced.
            *error = "cannot read " + path.string() + ": " + exception.what();
            return false;
        }
        if (!root.is_object()) {
            *error = path.string() + " is not a JSON object";
            return false;
        }
        if (root.contains("Graphics")) return true;
    }
    root["Graphics"] = TriAevumProductDefaultConfig()["Graphics"];
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), status);
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << root.dump(4) << '\n';
        if (!output) {
            *error = "cannot write " + temporary.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, path, status);
    if (status) {
        *error = "cannot replace " + path.string() + ": " + status.message();
        return false;
    }
    *written = true;
    return true;
}

void WriteTriAevumProductInfo(std::ostream& output) {
    const nlohmann::json info = {
        {"format", "triaevum_product_info_v1"},
        {"runtime", "oot3d_native_game"},
        // The title adapters this build contains (TRIAEVUM_TITLES); a
        // per-title Forge refuses a runtime without its title.
#if defined(TRIAEVUM_TITLE_MM3D)
        {"titles", {"oot3d", "mm3d"}},
#else
        {"titles", {"oot3d"}},
#endif
        {"source_commit", kTriAevumSourceCommit},
        {"whole_aot_plugin_abi", kTriAevumProductPlugin ? 2 : 0},
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
        {"private_title_loaded", Oot3dNativeGame::Oot3dWholeAotPluginV2Available()},
#else
        {"private_title_loaded", false},
#endif
        {"capabilities", {{"nri", kTriAevumProductNri},
                          {"experimental_global_illumination", kTriAevumProductExperimentalGi},
                          {"developer_tools", kTriAevumProductDeveloperTools},
                          // --log-directory, --crash-directory, --texture-{load,dump}-directory,
                          // --savestate-directory and --title-id (triaevum_data_routing.h).
                          {Oot3dNativeGame::kManagedDataPathsCapability, true},
#ifdef OOT3D_NATIVE_A32_WINDOW_AVAILABLE
                          {"f1", true}, {"topscreen", true}}},
#else
                          {"f1", false}, {"topscreen", false}}},
#endif
        {"default_config", TriAevumProductDefaultConfig()},
        {"default_ui_profile", "topscreen"},
    };
    output << info.dump(2) << '\n';
}
