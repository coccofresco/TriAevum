#pragma once

// The OoT3D grass source and mask rules the product ships (the maintainer's
// profile in triaevum_product_graphics.inc), as the persistence layer's
// {"Sources", "Masks"} title defaults.

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {

inline nlohmann::json Oot3dTitleGrassRuleDefaults() {
    const auto defaults = nlohmann::json::parse(
#include "triaevum_product_graphics.inc"
    );
    const auto& grass = defaults.at("Grass");
    return {{"Sources", grass.at("Sources")},
            {"Masks", grass.value("Masks", nlohmann::json::array())}};
}

} // namespace Oot3dNativeGame
