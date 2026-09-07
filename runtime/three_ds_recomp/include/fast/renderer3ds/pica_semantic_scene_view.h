#pragma once

#include "fast/renderer3ds/pica_frame_timing.h"
#include "fast/renderer3ds/pica_resolved_draw_stream.h"
#include "fast/renderer3ds/pica_view_family.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kPicaSemanticSceneViewSchemaVersion = 1U;

enum class PicaSemanticAvailability : uint8_t {
    Unavailable,
    Available,
};

struct PicaVersionedReference {
    uint64_t Identity = 0U;
    uint64_t ContentVersion = 0U;
    uintptr_t NativeHandle = 0U;
    bool IdentityAvailable = false;
    bool ContentVersionAvailable = false;
    bool NativeHandleAvailable = false;

    [[nodiscard]] bool Available() const noexcept {
        return IdentityAvailable || NativeHandleAvailable;
    }
};

struct PicaMaterialReference {
    uint32_t DescriptorSchemaVersion = 0U;
    uint64_t VertexProgramId = 0U;
    uint64_t FragmentProgramId = 0U;
    uint64_t RasterStateId = 0U;
    uint64_t PipelineId = 0U;
    uint64_t DynamicStateId = 0U;
    uint64_t FullRegisterStateId = 0U;
    const PicaSceneResolvedMaterialState* ResolvedState = nullptr;

    [[nodiscard]] bool Available() const noexcept {
        return DescriptorSchemaVersion != 0U &&
               FullRegisterStateId != 0U;
    }

    [[nodiscard]] bool ResolvedStateAvailable() const noexcept {
        return ResolvedState != nullptr && ResolvedState->Available();
    }
};

struct PicaRasterReference {
    const PicaSceneResolvedRasterState* ResolvedState = nullptr;

    [[nodiscard]] bool Available() const noexcept {
        return ResolvedState != nullptr && ResolvedState->Available();
    }
};

struct PicaRenderTargetReference {
    const PicaSceneRenderTargetState* ResolvedState = nullptr;

    [[nodiscard]] bool Available() const noexcept {
        return ResolvedState != nullptr && ResolvedState->Available();
    }

    [[nodiscard]] bool GpuResourcesAvailable() const noexcept {
        return ResolvedState != nullptr &&
               ResolvedState->GpuResourcesAvailable();
    }
};

struct PicaTextureReference {
    PicaVersionedReference Image;
    uint64_t NativeBaseLevelContentHash = 0U;
    uint64_t ReplacementContentHash = 0U;
    uint32_t PhysicalAddress = 0U;
    uint8_t Slot = 0U;
    bool Bound = false;
    bool NativeBaseLevelContentHashAvailable = false;
    bool CustomReplacement = false;
};

struct PicaBufferSliceReference {
    uintptr_t NativeHandle = 0U;
    uint64_t Offset = 0U;
    uint64_t Size = 0U;
    uint64_t ContentVersion = 0U;
    bool NativeHandleAvailable = false;
    bool ContentVersionAvailable = false;

    [[nodiscard]] bool Available() const noexcept {
        return NativeHandleAvailable && Size != 0U;
    }
};

// A draw-local projection of resolved 3DS/PICA state. Game object, actor,
// asset and gameplay identities are deliberately supplied by title adapters.
struct PicaSemanticDrawView {
    const PicaResolvedDrawRecord* PicaDraw = nullptr;
    uint64_t SubmissionId = 0U;
    const PicaCompositionAttribution* Composition = nullptr;
    PicaRasterReference Raster;
    PicaRenderTargetReference RenderTarget;
    PicaVersionedReference Geometry;
    PicaMaterialReference Material;
    std::array<PicaTextureReference, 3U> Textures{};
    PicaBufferSliceReference VertexUniforms;
    PicaBufferSliceReference PreviousVertexUniforms;
    PicaBufferSliceReference FragmentUniforms;
    const PicaNativeLightingState* Lighting = nullptr;
    const PicaNativeFragmentLightingState* FragmentLighting = nullptr;
    const PicaNativeDepthState* Depth = nullptr;
    const PicaNativeFogState* Fog = nullptr;
    const PicaNativeTransformState* Transform = nullptr;
    const PicaNativeSkeletonState* Skeleton = nullptr;
    PicaSemanticAvailability CurrentTransform =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability PreviousTransform =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability CurrentSkeleton =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability PreviousSkeleton =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability NativeLight =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability NativeFragmentLight =
        PicaSemanticAvailability::Unavailable;
    PicaSemanticAvailability NativeFog =
        PicaSemanticAvailability::Unavailable;
};

// Frame-lifetime semantic projection over a resolved PICA draw stream. The
// view copies only spans and scalar identities, never renderer-owned payloads.
struct PicaSemanticSceneView {
    uint32_t SchemaVersion = 0U;
    uint64_t FrameId = 0U;
    uint64_t Generation = 0U;
    PicaResolvedDrawStreamView DrawStream;
    const PicaFrameTemporalSample* TemporalSample = nullptr;
    const PicaViewFamily* CurrentViewFamily = nullptr;
    const PicaViewFamily* PreviousViewFamily = nullptr;

    [[nodiscard]] bool Available() const noexcept;
    [[nodiscard]] bool TemporalSampleAvailable() const noexcept;
    [[nodiscard]] bool CurrentViewFamilyAvailable() const noexcept;
    [[nodiscard]] bool ViewFamilyHistoryAvailable() const noexcept;
    [[nodiscard]] std::span<const PicaResolvedDrawRecord>
    Draws() const noexcept;
    [[nodiscard]] std::optional<PicaSemanticDrawView>
    DescribeDraw(size_t drawIndex) const noexcept;
};

} // namespace Fast::Renderer3ds
