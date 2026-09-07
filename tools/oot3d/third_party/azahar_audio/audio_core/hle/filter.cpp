// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include "audio_core/hle/common.h"
#include "audio_core/hle/filter.h"
#include "audio_core/hle/shared_memory.h"
#include "common/common_types.h"

namespace AudioCore::HLE {

namespace {

void SetStateError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

void SourceFilters::Reset() {
    Enable(false, false);
}

void SourceFilters::Enable(bool simple, bool biquad) {
    simple_filter_enabled = simple;
    biquad_filter_enabled = biquad;

    if (!simple)
        simple_filter.Reset();
    if (!biquad)
        biquad_filter.Reset();
}

void SourceFilters::Configure(SourceConfiguration::Configuration::SimpleFilter config) {
    simple_filter.Configure(config);
}

void SourceFilters::Configure(SourceConfiguration::Configuration::BiquadFilter config) {
    biquad_filter.Configure(config);
}

void SourceFilters::ProcessFrame(StereoFrame16& frame) {
    if (!simple_filter_enabled && !biquad_filter_enabled)
        return;

    if (simple_filter_enabled) {
        FilterFrame(frame, simple_filter);
    }

    if (biquad_filter_enabled) {
        FilterFrame(frame, biquad_filter);
    }
}

nlohmann::json SourceFilters::CaptureState() const {
    return {
        {"schema", "azahar_source_filters_v1"},
        {"simple_enabled", simple_filter_enabled},
        {"biquad_enabled", biquad_filter_enabled},
        {"simple",
         {{"a1", simple_filter.a1},
          {"b0", simple_filter.b0},
          {"y1", simple_filter.y1}}},
        {"biquad",
         {{"a1", biquad_filter.a1},
          {"a2", biquad_filter.a2},
          {"b0", biquad_filter.b0},
          {"b1", biquad_filter.b1},
          {"b2", biquad_filter.b2},
          {"x1", biquad_filter.x1},
          {"x2", biquad_filter.x2},
          {"y1", biquad_filter.y1},
          {"y2", biquad_filter.y2}}},
    };
}

bool SourceFilters::RestoreState(const nlohmann::json& state,
                                 std::string* error) {
    try {
        if (!state.is_object() ||
            state.value("schema", std::string{}) !=
                "azahar_source_filters_v1") {
            SetStateError(error, "invalid source-filter state schema");
            return false;
        }

        SourceFilters restored;
        restored.simple_filter_enabled = state.at("simple_enabled").get<bool>();
        restored.biquad_filter_enabled = state.at("biquad_enabled").get<bool>();
        const auto& simple = state.at("simple");
        restored.simple_filter.a1 = simple.at("a1").get<s32>();
        restored.simple_filter.b0 = simple.at("b0").get<s32>();
        restored.simple_filter.y1 =
            simple.at("y1").get<std::array<s16, 2>>();
        const auto& biquad = state.at("biquad");
        restored.biquad_filter.a1 = biquad.at("a1").get<s32>();
        restored.biquad_filter.a2 = biquad.at("a2").get<s32>();
        restored.biquad_filter.b0 = biquad.at("b0").get<s32>();
        restored.biquad_filter.b1 = biquad.at("b1").get<s32>();
        restored.biquad_filter.b2 = biquad.at("b2").get<s32>();
        restored.biquad_filter.x1 =
            biquad.at("x1").get<std::array<s16, 2>>();
        restored.biquad_filter.x2 =
            biquad.at("x2").get<std::array<s16, 2>>();
        restored.biquad_filter.y1 =
            biquad.at("y1").get<std::array<s16, 2>>();
        restored.biquad_filter.y2 =
            biquad.at("y2").get<std::array<s16, 2>>();
        *this = restored;
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("invalid source-filter state: ") +
                     exception.what();
        }
        return false;
    }
}

// SimpleFilter

void SourceFilters::SimpleFilter::Reset() {
    y1.fill(0);
    // Configure as passthrough.
    a1 = 0;
    b0 = 1 << 15;
}

void SourceFilters::SimpleFilter::Configure(
    SourceConfiguration::Configuration::SimpleFilter config) {

    a1 = config.a1;
    b0 = config.b0;
}

std::array<s16, 2> SourceFilters::SimpleFilter::ProcessSample(const std::array<s16, 2>& x0) {
    std::array<s16, 2> y0;
    for (std::size_t i = 0; i < 2; i++) {
        const s32 tmp = (b0 * x0[i] + a1 * y1[i]) >> 15;
        y0[i] = std::clamp(tmp, -32768, 32767);
    }

    y1 = y0;

    return y0;
}

// BiquadFilter

void SourceFilters::BiquadFilter::Reset() {
    x1.fill(0);
    x2.fill(0);
    y1.fill(0);
    y2.fill(0);
    // Configure as passthrough.
    a1 = a2 = b1 = b2 = 0;
    b0 = 1 << 14;
}

void SourceFilters::BiquadFilter::Configure(
    SourceConfiguration::Configuration::BiquadFilter config) {

    a1 = config.a1;
    a2 = config.a2;
    b0 = config.b0;
    b1 = config.b1;
    b2 = config.b2;
}

std::array<s16, 2> SourceFilters::BiquadFilter::ProcessSample(const std::array<s16, 2>& x0) {
    std::array<s16, 2> y0;
    for (std::size_t i = 0; i < 2; i++) {
        const s32 tmp = (b0 * x0[i] + b1 * x1[i] + b2 * x2[i] + a1 * y1[i] + a2 * y2[i]) >> 14;
        y0[i] = std::clamp(tmp, -32768, 32767);
    }

    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;

    return y0;
}

} // namespace AudioCore::HLE
