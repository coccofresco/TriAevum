#include "triaevum_product_info.h"
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
    // A new installation starts from the Enhanced preset with the title's grass
    // source texture rules, which no preset owns. Existing installations
    // retain their config.
    auto graphics = GraphicsSettingsService::Preset(GraphicsPreset::Enhanced);
    const auto defaults = nlohmann::json::parse(
#include "triaevum_product_graphics.inc"
    );
    auto document = SerializeGraphicsSettings(graphics);
    document.merge_patch(defaults);
    document["GrassSavedPreset"] = document["Grass"];
    graphics = DeserializeGraphicsSettings(document, graphics).Value;
    graphics.OutputWidth = 1280;
    graphics.OutputHeight = 720;
#if defined(__ANDROID__)
    // The full Enhanced preset; the picture fills the phone's display instead
    // of a 16:9 pillarbox (the aspect is host-owned, so the preset stays).
    graphics.Aspect = AspectRatioMode::Free;
    // Water keeps its waves, Fresnel sky (fog colour) reflection, sun specular
    // and foam in the scene shader; the screen-space water trace (Hi-Z, the
    // material guide on every scene draw, the composite) cost ~4 ms of an
    // 18 ms frame on the Adreno 830 for reflections that are mostly sky.
    graphics.Effects.WaterReflections = false;
#endif
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
