#pragma once

#include "fast/extension/graphics_settings_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace Fast::Extension {

// Explicit diagnostic opt-in. Uses the same settings transaction as F1; callers
// must select a disposable config, since Apply persists accepted settings.
inline void TickAmbientOcclusionDiagnostics(GraphicsSettingsRuntime& runtime, uint64_t frame) {
    static const auto sequence = [] {
        const char* text = std::getenv("TRIAEVUM_AO_DIAGNOSTIC_SEQUENCE");
        if (text == nullptr) return nlohmann::json::array();
        auto value = nlohmann::json::parse(text);
        if (!value.is_array()) throw std::runtime_error("AO diagnostic sequence must be an array");
        uint64_t previous = 0;
        for (const auto& step : value) {
            const auto at = step.at("frame").get<uint64_t>();
            const auto quality = step.at("quality").get<int>();
            if (at <= previous || quality < -1 || quality > 1)
                throw std::runtime_error("Invalid AO diagnostic frame/quality");
            previous = at;
        }
        return value;
    }();
    static size_t next = 0;
    if (next >= sequence.size() || frame < sequence[next].at("frame").get<uint64_t>()) return;
    const int quality = sequence[next++].at("quality").get<int>();
    auto settings = runtime.Snapshot();
    settings.Preset = GraphicsPreset::Custom;
    settings.Effects.AmbientOcclusion = quality < 0 ? AmbientOcclusionMode::Off : AmbientOcclusionMode::Gtao;
    settings.Effects.AoQuality = quality <= 0 ? 0U : 1U;
    runtime.Apply(std::move(settings));
    std::fprintf(stderr, "AO diagnostic: frame=%llu quality=%d\n",
                 static_cast<unsigned long long>(frame), quality);
    std::fflush(stderr);
}

} // namespace Fast::Extension
