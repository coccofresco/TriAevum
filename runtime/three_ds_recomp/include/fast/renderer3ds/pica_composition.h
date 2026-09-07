#pragma once

#include <cstdint>
#include <span>

namespace Fast::Renderer3ds {

enum class PicaCompositionDomain : uint8_t {
    Unknown = 0,
    Scene = 1,
    Ui = 2,
};

enum class PicaCompositionLayer : uint8_t {
    Unknown = 0,
    OpaqueWorld = 1,
    TransparentWorld = 2,
    Atmosphere = 3,
    Ui = 4,
};

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

    bool operator==(const PicaCompositionAttribution&) const = default;
};

inline constexpr uint32_t kPicaCompositionSequenceSchemaVersion = 1U;

struct PicaCompositionTargetReference {
    uint64_t RenderTargetNamespace = 0;
    uint32_t ColorPhysicalAddress = 0;
    uint32_t DepthPhysicalAddress = 0;
    uint16_t FramebufferWidth = 0;
    uint16_t FramebufferHeight = 0;
    uint8_t ColorFormat = 0;
    uint8_t DepthFormat = 0;

    bool operator==(const PicaCompositionTargetReference&) const = default;
};

struct PicaCompositionDrawReference {
    uint64_t SubmissionId = 0;
    PicaCompositionTargetReference Target;
    PicaCompositionDomain Domain = PicaCompositionDomain::Unknown;
    PicaCompositionAttribution Composition;
};

// Complete ordered metadata for one selected PICA visual sample. The title
// frontend owns the span; a backend retaining the schedule copies only this
// compact metadata surface.
struct PicaCompositionSequenceView {
    uint32_t SchemaVersion = kPicaCompositionSequenceSchemaVersion;
    uint64_t SequenceId = 0;
    std::span<const PicaCompositionDrawReference> Draws;
};

} // namespace Fast::Renderer3ds
