#pragma once

#include "fast/renderer3ds/pica_temporal_identity.h"

#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

enum class PicaCompositionDomain : uint8_t {
    Unknown = 0,
    Scene = 1,
    Ui = 2,
    // Two-dimensional content the title composites over the whole native
    // screen: a full-frame redraw of the copied frame, a full-screen wash, a
    // movie frame. It is frame content rather than a placed UI element, so it
    // owns every output pixel (PicaScreenContent says how). Placed UI keeps
    // `Ui`: HUD, menu layout and pointer mapping depend on the uniform native
    // UI canvas.
    Screen = 3,
};

// What screen-space frame content shows, which decides its mapping onto a
// target wider or narrower than the native screen.
enum class PicaScreenContent : uint8_t {
    None = 0,
    // The rendered frame itself - a redraw of a copy of rendered output, or a
    // wash over it: it takes the scene's own mapping, so a redraw lands exactly
    // on the pixels it copied.
    RenderedFrame = 1,
    // A picture of its own whose textures carry no alpha, such as a movie
    // frame: it replaces the frame. It is shown whole at its native
    // proportions, centred, and the pixels around it are black bars.
    OwnPicture = 2,
    // A picture of its own whose textures carry alpha (a mask, a glow) laid
    // over the frame: it covers the target without distortion, native
    // proportions kept and the excess centred, so the whole frame lies under it.
    PictureOverlay = 3,
};

// What a UI-attributed draw rasterises, as far as frame content is concerned.
struct PicaUiFrameContentEvidence {
    // The draw's viewport covers its whole colour target.
    bool CoversWholeTarget = false;
    // This frame's scene drew into that colour target.
    bool SceneDrewOnTarget = false;
    // Its projected primitives cover the whole viewport.
    bool GeometryCoversViewport = false;
    // It samples no texture: a solid or vertex-coloured wash.
    bool Untextured = false;
    // It samples at least one texture, and each is owned by an ordered GPU
    // copy of rendered output.
    bool SamplesOnlyRenderedCopies = false;
    // It samples at least one texture, and none stores alpha per texel
    // (`PicaTextureFormatCarriesAlpha`): what it draws is opaque but for a
    // uniform or vertex fade.
    bool SamplesOnlyAlphaFreeTextures = false;
};

struct PicaUiFrameContent {
    PicaCompositionDomain Domain = PicaCompositionDomain::Unknown;
    PicaScreenContent Content = PicaScreenContent::None;

    bool operator==(const PicaUiFrameContent&) const = default;
};

// A UI-attributed draw is screen-space frame content only when it covers the
// frame itself: its geometry covers the whole colour target the scene drew
// into. Placed UI - HUD, minimap, dialogue, menus - also draws over the scene
// with a whole-target viewport, but its primitives cover part of it, so it
// stays `Ui`. Covering content that washes the frame or redraws a copy of
// rendered output shows the rendered frame; anything else is a picture of its
// own, opaque when its textures carry no alpha and an overlay when they do.
[[nodiscard]] constexpr PicaUiFrameContent ResolvePicaUiFrameContent(
    PicaCompositionDomain declared,
    const PicaUiFrameContentEvidence& evidence) noexcept {
    if (declared != PicaCompositionDomain::Ui || !evidence.CoversWholeTarget ||
        !evidence.SceneDrewOnTarget || !evidence.GeometryCoversViewport) {
        return {declared, PicaScreenContent::None};
    }
    if (evidence.Untextured || evidence.SamplesOnlyRenderedCopies) {
        return {PicaCompositionDomain::Screen, PicaScreenContent::RenderedFrame};
    }
    return {PicaCompositionDomain::Screen,
            evidence.SamplesOnlyAlphaFreeTextures
                ? PicaScreenContent::OwnPicture
                : PicaScreenContent::PictureOverlay};
}

enum class PicaCompositionLayer : uint8_t {
    Unknown = 0,
    OpaqueWorld = 1,
    TransparentWorld = 2,
    Atmosphere = 3,
    Ui = 4,
};

// Only proven world surfaces admit replacement illumination. Atmosphere owns
// its authored appearance; unknown ownership is not evidence of a lit surface.
constexpr bool PicaCompositionReceivesSurfaceLighting(PicaCompositionLayer layer) noexcept {
    return layer == PicaCompositionLayer::OpaqueWorld || layer == PicaCompositionLayer::TransparentWorld;
}

enum class PicaCompositionProvenance : uint8_t {
    Unknown = 0,
    NativeCmbDrawPass = 1,
    NativeControlFlow = 2,
    NativeUiLifecycle = 3,
};

struct PicaCompositionAttribution {
    PicaCompositionLayer Layer = PicaCompositionLayer::Unknown;
    PicaCompositionProvenance Provenance =
        PicaCompositionProvenance::Unknown;
    uint32_t SourcePc = 0;
    uint32_t NativeValue = 0;
    // Title-proven native projected/blob shadow, not inferred from material.
    bool NativeProjectedShadow = false;
    // Native actor draw ownership, independent of skinning and material.
    bool NativeActorGeometry = false;
    uint64_t NativeActorIdentity = 0;
    // The title lights this draw as an object of its own: in the full
    // per-pixel model it keeps its own native lights and receives none of the
    // scene's environment profile, shadows, global illumination or ambient
    // occlusion.
    bool IsolatedLighting = false;
    PicaTemporalIdentity TemporalIdentity;
    // Title-catalogued stationary prop: its rigid native actor geometry may
    // be cached in static shadow layers instead of being redrawn as a dynamic
    // caster in every frame. Not a promise that it never moves: a moved rigid
    // caster changes its content version and refreshes what it covers. Only
    // set together with NativeActorGeometry and a nonzero NativeActorIdentity.
    bool StaticShadowCacheEligible = false;

    bool operator==(const PicaCompositionAttribution&) const = default;
};

inline constexpr uint32_t kPicaCompositionSequenceSchemaVersion = 5U;

struct PicaCompositionTargetReference {
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t ColorFormat = 0;
    uint8_t DepthFormat = 0;

    auto operator<=>(const PicaCompositionTargetReference&) const = default;
};

struct PicaCompositionDrawReference {
    uint64_t SubmissionId = 0;
    PicaCompositionTargetReference Target;
    PicaCompositionDomain Domain = PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
};

// Native memory visibility uses canonical raster results. Presentation may
// compose extensions and interpolate, but never writes those results to RAM.
enum class PicaCompositionPurpose : uint8_t {
    Presentation,
    NativeMemory,
};

// Complete ordered metadata for one selected PICA visual sample. The title
// frontend owns the span; a backend retaining the schedule copies only this
// compact metadata surface.
// A native clear of a target's whole color buffer, before the draw
// BeforeSubmissionId: what the target held is gone, so its draws before the
// clear take no part in the composition of its draws after it.
struct PicaCompositionTargetReset {
    PicaCompositionTargetReference Target;
    uint64_t BeforeSubmissionId = 0;
};

struct PicaCompositionSequenceView {
    uint32_t SchemaVersion = kPicaCompositionSequenceSchemaVersion;
    uint64_t SequenceId = 0;
    std::span<const PicaCompositionDrawReference> Draws;
    PicaCompositionPurpose Purpose = PicaCompositionPurpose::Presentation;
    std::span<const PicaCompositionTargetReset> TargetResets;
};

} // namespace Fast::Renderer3ds
