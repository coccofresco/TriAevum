#include "triaevum_product_info.h"
#include "triaevum_product_build.h"

#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include <nlohmann/json.hpp>
#include <ostream>
#if defined(OOT3D_REQUIRE_WHOLE_AOT_PLUGIN_V2)
#include "oot3d_native_direct_aot.h"
#endif

void WriteTriAevumProductInfo(std::ostream& output) {
    using namespace Fast::Oot3d;
    auto graphics = GraphicsSettingsService::Preset(GraphicsPreset::Authentic);
    // Title-owned style and source texture rules must not change the shared
    // renderer's Authentic preset. Existing installations retain their config.
    const auto defaults = nlohmann::json::parse(
#include "triaevum_product_graphics.inc"
    );
    auto document = SerializeGraphicsSettings(graphics);
    document.merge_patch(defaults);
    document["GrassSavedPreset"] = document["Grass"];
    graphics = DeserializeGraphicsSettings(document, graphics).Value;
    graphics.OutputWidth = 1280;
    graphics.OutputHeight = 720;
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
#ifdef OOT3D_NATIVE_A32_WINDOW_AVAILABLE
                          {"f1", true}, {"topscreen", true}}},
#else
                          {"f1", false}, {"topscreen", false}}},
#endif
        {"default_config", {{"Graphics", SerializeGraphicsSettings(graphics)}}},
        {"default_ui_profile", "topscreen"},
    };
    output << info.dump(2) << '\n';
}
