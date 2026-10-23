#include "fast/renderer3ds/pica_surface_emission.h"

#include <algorithm>

namespace Fast::Renderer3ds {

float PicaUnitSurfaceEmission(const PicaTevProgram& program, const std::array<std::array<float, 4>, 6>& constants,
                              const std::array<float, 4>& bufferColor, const NativeBlendState& blend) noexcept {
    PicaTevInputs inputs;
    constexpr std::array<float, 4> white{1.0F, 1.0F, 1.0F, 1.0F};
    inputs.Primary = inputs.PrimaryFragment = inputs.SecondaryFragment = white;
    inputs.Textures.fill(white);
    inputs.Constants = constants;
    inputs.BufferColor = bufferColor;
    const auto out = EvaluatePicaTevProgram(program, inputs);
    std::array<float, 3> factor{1.0F, 1.0F, 1.0F};
    if (!PicaBlendReplacesDestination(blend)) {
        for (size_t c = 0; c < 3U; ++c) {
            switch (blend.SourceRgb) {
            case NativeBlendFactor::Zero: factor[c] = 0.0F; break;
            case NativeBlendFactor::SourceColor: factor[c] = out[c]; break;
            case NativeBlendFactor::OneMinusSourceColor: factor[c] = 1.0F - out[c]; break;
            case NativeBlendFactor::SourceAlpha:
            case NativeBlendFactor::SourceAlphaSaturate: factor[c] = out[3]; break;
            case NativeBlendFactor::OneMinusSourceAlpha: factor[c] = 1.0F - out[3]; break;
            case NativeBlendFactor::ConstantColor: factor[c] = blend.ConstantColor[c]; break;
            case NativeBlendFactor::OneMinusConstantColor: factor[c] = 1.0F - blend.ConstantColor[c]; break;
            case NativeBlendFactor::ConstantAlpha: factor[c] = blend.ConstantColor[3]; break;
            case NativeBlendFactor::OneMinusConstantAlpha: factor[c] = 1.0F - blend.ConstantColor[3]; break;
            default: break;
            }
        }
    }
    float emission = 0.0F;
    for (size_t c = 0; c < 3U; ++c) emission = std::max(emission, std::clamp(out[c] * factor[c], 0.0F, 1.0F));
    return emission;
}

PicaSurfaceEmissionRegistry& PicaSurfaceEmissionRegistry::Instance() {
    static PicaSurfaceEmissionRegistry registry;
    return registry;
}

void PicaSurfaceEmissionRegistry::Follow(std::span<const uint64_t> textures) {
    std::scoped_lock lock(mMutex);
    mFollowed.clear();
    for (const uint64_t texture : textures)
        if (texture != 0U) mFollowed.insert(texture);
}

float PicaSurfaceEmissionRegistry::Strength(uint64_t texture) const {
    std::scoped_lock lock(mMutex);
    const auto found = mStrength.find(texture);
    return found == mStrength.end() ? 1.0F : found->second;
}

void PicaSurfaceEmissionRegistry::BeginFrame() {
    std::scoped_lock lock(mMutex);
    for (const auto& [texture, emission] : mFrame) mStrength[texture] = emission;
    // Only followed surfaces keep a strength.
    std::erase_if(mStrength, [this](const auto& entry) { return !mFollowed.contains(entry.first); });
    mFrame.clear();
    mFrameFollowed = mFollowed;
}

bool PicaSurfaceEmissionRegistry::Follows(uint64_t texture) const noexcept {
    return texture != 0U && mFrameFollowed.contains(texture);
}

void PicaSurfaceEmissionRegistry::Observe(uint64_t texture, float emission) {
    auto [entry, inserted] = mFrame.try_emplace(texture, emission);
    if (!inserted) entry->second = std::max(entry->second, emission);
}

} // namespace Fast::Renderer3ds
