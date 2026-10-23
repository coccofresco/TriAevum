#pragma once

#include "fast/renderer3ds/pica_native_state.h"
#include "fast/renderer3ds/pica_tev_program.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace Fast::Renderer3ds {

// How strongly the frame draws the native surfaces a title follows: lights it
// adds as the glow of surfaces it draws (lit windows, lamp halos) follow them.
// A glow card the title fades with the time of day (a combiner constant's
// alpha) reads 0 by day; one drawn at full strength reads 1.

// How strongly a draw shows its surface where its textures and vertex colours
// are white: the combiner's output for unit inputs, weighed by the blend's
// source factor; the largest channel, 0-1. Factors that depend on the
// destination count fully.
[[nodiscard]] float PicaUnitSurfaceEmission(const PicaTevProgram& program,
                                            const std::array<std::array<float, 4>, 6>& constants,
                                            const std::array<float, 4>& bufferColor,
                                            const NativeBlendState& blend) noexcept;

// Surfaces are named by their textures' base-level content hashes. The title
// side follows and reads; the render thread observes. Strengths lag one frame.
class PicaSurfaceEmissionRegistry {
public:
    static PicaSurfaceEmissionRegistry& Instance();

    // Title side: the surfaces it follows now (replacing the earlier set).
    void Follow(std::span<const uint64_t> textures);
    // Title side: the strength the renderer last saw it drawn with, 0-1. A
    // surface not drawn in a frame keeps its last strength (it may lie out of
    // view); one never drawn reads 1.
    [[nodiscard]] float Strength(uint64_t texture) const;

    // Render thread, once per frame before its draws: the last frame's
    // observations become strengths, and the followed set is taken.
    void BeginFrame();
    // Render thread: whether a draw sampling `texture` is followed.
    [[nodiscard]] bool Follows(uint64_t texture) const noexcept;
    // Render thread: a draw of this frame shows `texture` with this emission.
    void Observe(uint64_t texture, float emission);

private:
    mutable std::mutex mMutex;
    std::unordered_set<uint64_t> mFollowed;
    std::unordered_map<uint64_t, float> mStrength;
    // Render thread only.
    std::unordered_set<uint64_t> mFrameFollowed;
    std::unordered_map<uint64_t, float> mFrame;
};

} // namespace Fast::Renderer3ds
