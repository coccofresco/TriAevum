#pragma once

#include "fast/renderer3ds/pica_scene_payloads.h"
#include "fast/renderer3ds/pica_vertex_input_layout.h"
#include "fast/renderer3ds/virtual_shadow_map.h"

namespace Fast::Renderer3ds {

struct PicaShadowGeometryView {
    std::span<const uint8_t> Payload;
    std::span<const PicaSceneVertexBufferBinding> Bindings;
    std::span<const PicaNriPackedVertexAttribute> Attributes;
    std::span<const PicaNriPackedVertexBinding> InputBindings;
    uint64_t BufferBaseOffset = 0;
    uint64_t IndexOffset = 0;
    uint32_t VertexOrIndexCount = 0;
    int32_t BaseVertex = 0;
    bool Indexed = false;
};

struct PicaShadowCasterCache {
    uint64_t Signature = 0;
    ShadowBounds Bounds;
};

// The accepted pose of one caster instance (one native geometry drawn by one
// temporal owner part). Float reconstruction of a still caster's world pose
// from a moving camera jitters by roundoff; a pose within half a
// quantization step of the anchor on every coefficient (1/131072 on the
// 3x3, 1/128 unit on the translation) keeps the anchor's version and
// bounds. The existing bounds guard contains one whole step, so the anchor's
// bounds stay conservative; drawn depth still uses the current pose. The
// anchor never moves on a hit, so slow motion cannot drift unnoticed: a pose
// outside it starts a new anchor with a new revision.
struct PicaShadowCasterAnchor {
    ShadowMatrix World{};
    ShadowBounds Bounds;
    uint64_t GeometryVersion = 0;
    uint64_t Signature = 0;
    uint64_t Revision = 0;
    uint64_t LastFrame = 0;
    // The draw that used it in LastFrame; another draw with the same
    // instance identity in the same frame makes the identity ambiguous.
    uint64_t LastSubmission = 0;
    bool Ambiguous = false;
    bool Valid = false;
};

// Uses only typed native transform/skeleton semantics and the same packed
// float4 inputs submitted to the GPU. Unknown programs remain unbounded and
// uncached; no guessed bounds may discard a real occluder.
[[nodiscard]] VirtualShadowCaster
BuildPicaShadowCaster(const PicaShadowGeometryView& geometry, const PicaVertexShaderHookLayout& hooks,
                      uint32_t booleanUniforms, std::span<const uint8_t> floatUniforms, const ShadowMatrix& viewToWorld,
                      uint64_t geometryVersion, PicaShadowCasterCache& cache,
                      PicaShadowCasterAnchor* anchor = nullptr) noexcept;

} // namespace Fast::Renderer3ds
