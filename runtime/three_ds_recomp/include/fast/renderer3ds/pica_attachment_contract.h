#pragma once

#include <cstddef>
#include <cstdint>

namespace Fast::Renderer3ds {

// Stable order shared by Vulkan render passes, dynamic rendering and NRI.
// Keep shader output locations and framebuffer attachment arrays aligned with
// this contract instead of duplicating literal counts across subsystems.
enum class PicaColorAttachment : uint8_t {
    SceneColor = 0,
    NormalGuide,
    MaterialGuide,
    // RG stores rigid UV motion, B stores its validity, and A is reserved for
    // inverse window depth of native transparent / extension occluders. This
    // masks contours but never participates in native edge detection. The graph owns the
    // packed attachment; each consumer is limited to its documented channels.
    RigidMotionGuide,
    AmbientGuide,
    FogGuide,
    // Native scene unit normal (RGB) and window depth (A), never written by
    // extension geometry. Float32 preserves depth-test precision.
    OutlineGeometryGuide,
    // Global illumination surface guide (R16G16B16A16 UNORM): each RGB channel
    // packs the sRGB albedo (high byte) and the sRGB direct radiance (low
    // byte) of the opaque scene surface; A is validity. Present only while GI
    // requests it: the instrumented layout otherwise keeps seven attachments.
    // Under MSAA the averaged resolve keeps the high bytes and mixes the low
    // ones at polygon edges.
    DiffuseAlbedoGuide,
    Count,
};

// Capacity of every attachment array (the extended instrumented layout).
inline constexpr size_t kPicaColorAttachmentCount =
    static_cast<size_t>(PicaColorAttachment::Count);
// The instrumented layout without the global-illumination albedo guide.
inline constexpr size_t kPicaBaseInstrumentedColorAttachmentCount =
    static_cast<size_t>(PicaColorAttachment::DiffuseAlbedoGuide);

enum class PicaAuxiliaryOutput : uint8_t {
    None = 0,
    NormalGuide = 1U << 0U,
    MaterialGuide = 1U << 1U,
    RigidMotionGuide = 1U << 2U,
    AmbientGuide = 1U << 3U,
    FogGuide = 1U << 4U,
    OutlineGeometryGuide = 1U << 5U,
    DiffuseAlbedoGuide = 1U << 6U,
};

constexpr PicaAuxiliaryOutput operator|(
    PicaAuxiliaryOutput left, PicaAuxiliaryOutput right) noexcept {
    return static_cast<PicaAuxiliaryOutput>(
        static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

constexpr PicaAuxiliaryOutput& operator|=(
    PicaAuxiliaryOutput& left, PicaAuxiliaryOutput right) noexcept {
    left = left | right;
    return left;
}

// The base instrumented guides, allocated together with an instrumented
// target. The albedo guide is extension storage grown on demand.
inline constexpr PicaAuxiliaryOutput kAllPicaAuxiliaryOutputs =
    PicaAuxiliaryOutput::NormalGuide | PicaAuxiliaryOutput::MaterialGuide | PicaAuxiliaryOutput::RigidMotionGuide |
    PicaAuxiliaryOutput::AmbientGuide | PicaAuxiliaryOutput::FogGuide | PicaAuxiliaryOutput::OutlineGeometryGuide;
inline constexpr PicaAuxiliaryOutput kExtendedPicaAuxiliaryOutputs =
    kAllPicaAuxiliaryOutputs | PicaAuxiliaryOutput::DiffuseAlbedoGuide;

struct PicaAttachmentFeatureRequests {
    bool DirectionalShadows = false;
    bool AmbientOcclusion = false;
    bool ToonOutline = false;
    bool Reflections = false;
    bool TemporalReconstruction = false;
    bool InteractiveGrass = false;
    bool GlobalIllumination = false;
    bool PointShadows = false;
    // An effect reads the face normal of the geometry guide.
    bool FaceNormal = false;
    // With GlobalIllumination: the GI gathers on screen from the guides, so
    // no scene distance field is built.
    bool ScreenSpaceGlobalIllumination = false;
    // With DirectionalShadows: screen-space contact shadows of its light.
    bool ContactShadows = false;
    bool operator==(const PicaAttachmentFeatureRequests&) const = default;
};

struct PicaAttachmentRequirements {
    PicaAuxiliaryOutput AuxiliaryOutputs = PicaAuxiliaryOutput::None;

    [[nodiscard]] constexpr bool NativeColorOnly() const noexcept {
        return AuxiliaryOutputs == PicaAuxiliaryOutput::None;
    }

    [[nodiscard]] constexpr bool Requires(
        PicaAuxiliaryOutput output) const noexcept {
        return (static_cast<uint8_t>(AuxiliaryOutputs) &
                static_cast<uint8_t>(output)) != 0U;
    }

    // Instrumented shaders use fixed output locations 1-6 (7 with the GI
    // albedo guide). Keep that layout stable while making the canonical path
    // a true color-only contract.
    [[nodiscard]] constexpr uint32_t ColorAttachmentCount() const noexcept {
        if (NativeColorOnly()) return 1U;
        return Requires(PicaAuxiliaryOutput::DiffuseAlbedoGuide)
            ? static_cast<uint32_t>(kPicaColorAttachmentCount)
            : static_cast<uint32_t>(kPicaBaseInstrumentedColorAttachmentCount);
    }
    // The same requirements without the on-demand albedo guide.
    [[nodiscard]] constexpr PicaAttachmentRequirements BaseGuides() const noexcept {
        return {static_cast<PicaAuxiliaryOutput>(
            static_cast<uint8_t>(AuxiliaryOutputs) &
            static_cast<uint8_t>(kAllPicaAuxiliaryOutputs))};
    }

    [[nodiscard]] constexpr uint8_t Key() const noexcept {
        return static_cast<uint8_t>(AuxiliaryOutputs);
    }

    bool operator==(const PicaAttachmentRequirements&) const = default;
};

[[nodiscard]] constexpr size_t PicaAttachmentIndex(
    PicaColorAttachment attachment) noexcept {
    return static_cast<size_t>(attachment);
}

} // namespace Fast::Renderer3ds
