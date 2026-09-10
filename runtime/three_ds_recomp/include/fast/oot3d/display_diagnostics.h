#pragma once

#include "fast/oot3d/graphics_settings_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace Fast::Oot3d {

// Bounded test schedules use the same transaction as F1, with a disposable config.
inline void TickDisplayDiagnostics(GraphicsSettingsRuntime& runtime, uint64_t frame) {
    static const auto sequence = [] {
        const char* text = std::getenv("TRIAEVUM_DISPLAY_DIAGNOSTIC_SEQUENCE");
        if (!text) return nlohmann::json::array();
        auto steps = nlohmann::json::parse(text);
        if (!steps.is_array()) throw std::runtime_error("Display diagnostic sequence must be an array");
        uint64_t previous = 0;
        for (const auto& step : steps) {
            const auto at = step.at("frame").get<uint64_t>();
            if (at <= previous) throw std::runtime_error("Display diagnostic frames must increase");
            previous = at;
        }
        return steps;
    }();
    static size_t next = 0;
    if (next >= sequence.size() || frame < sequence[next].at("frame").get<uint64_t>()) return;
    const auto& step = sequence[next++];
    auto settings = runtime.Snapshot();
    settings.Preset = GraphicsPreset::Custom;
    if (step.contains("width")) settings.OutputWidth = step.at("width").get<uint32_t>();
    if (step.contains("height")) settings.OutputHeight = step.at("height").get<uint32_t>();
    if (step.contains("scale")) settings.InternalResolutionScale = step.at("scale").get<float>();
    if (step.contains("mode")) settings.Window = static_cast<WindowMode>(step.at("mode").get<int>());
    runtime.Apply(settings);
    if (step.value("confirm", false)) runtime.ConfirmPresentation();
    const auto actual = runtime.DisplayMetrics();
    std::fprintf(stderr, "DISPLAY diagnostic frame=%llu step=%s previous_output=%ux%u previous_scene=%ux%u\n",
        static_cast<unsigned long long>(frame), step.dump().c_str(), actual.OutputWidth, actual.OutputHeight,
        actual.SceneWidth, actual.SceneHeight);
    std::fflush(stderr);
}
} // namespace Fast::Oot3d
