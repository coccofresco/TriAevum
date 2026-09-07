#pragma once

#include "fast/oot3d/native_view_family.h"
#include "fast/oot3d/pica_scene_frame.h"
#include "fast/oot3d/scene_view_runtime.h"
#include "fast/renderer3ds/pica_semantic_scene_view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Fast::Oot3d {

using NativeSceneSemanticAvailability =
    ::Fast::Renderer3ds::PicaSemanticAvailability;
using NativeSceneVersionedReference =
    ::Fast::Renderer3ds::PicaVersionedReference;
using NativeSceneMaterialReference =
    ::Fast::Renderer3ds::PicaMaterialReference;
using NativeSceneRasterReference =
    ::Fast::Renderer3ds::PicaRasterReference;
using NativeSceneRenderTargetReference =
    ::Fast::Renderer3ds::PicaRenderTargetReference;
using NativeSceneTextureReference =
    ::Fast::Renderer3ds::PicaTextureReference;
using NativeSceneBufferSliceReference =
    ::Fast::Renderer3ds::PicaBufferSliceReference;

// A draw-local semantic projection over PicaSceneFrame. It contains only
// compact identities and references; geometry, uniforms and textures remain
// renderer-owned.
struct NativeSceneDrawSemantics final
    : ::Fast::Renderer3ds::PicaSemanticDrawView {
    // Title-owned enrichment remains separate from the shared PICA view.
    NativeSceneSemanticAvailability ObjectIdentity =
        NativeSceneSemanticAvailability::Unavailable;
};

// Stable renderer-owned scene contract for extension consumers. It references
// PicaSceneFrame directly and never copies bulk draw payloads.
class NativeSceneView final {
  public:
    static constexpr uint32_t kSchemaVersion = 9U;

    [[nodiscard]] bool Publish(
        uint64_t frameId, const PicaSceneFrame& picaFrame,
        std::optional<PerspectiveViewState> perspective) noexcept;
    [[nodiscard]] bool PublishViewFamily(uint64_t frameId, const PicaSceneFrame& picaFrame,
                                         NativeViewFamily viewFamily) noexcept;
    void Reset() noexcept;

    [[nodiscard]] bool Active() const noexcept;
    [[nodiscard]] uint64_t FrameId() const noexcept;
    [[nodiscard]] uint64_t Generation() const noexcept;
    [[nodiscard]] const PicaSceneFrame* PicaFrame() const noexcept;
    [[nodiscard]] const NativeFrameTemporalSample& TemporalSample() const noexcept;
    [[nodiscard]] const std::optional<PerspectiveViewState>&
    CurrentPerspective() const noexcept;
    [[nodiscard]] const std::optional<PerspectiveViewState>&
    PreviousPerspective() const noexcept;
    [[nodiscard]] const std::optional<NativeViewFamily>& CurrentViewFamily() const noexcept;
    [[nodiscard]] const std::optional<NativeViewFamily>& PreviousViewFamily() const noexcept;
    [[nodiscard]] bool CameraHistoryAvailable() const noexcept;
    [[nodiscard]] bool ViewFamilyHistoryAvailable() const noexcept;
    [[nodiscard]] std::span<const PicaSceneDrawRecord> Draws() const noexcept;
    [[nodiscard]] ::Fast::Renderer3ds::PicaSemanticSceneView
    SharedSemanticView() const noexcept;
    [[nodiscard]] std::optional<NativeSceneDrawSemantics>
    DescribeDraw(size_t drawIndex) const noexcept;

  private:
    [[nodiscard]] bool PublishResolved(uint64_t frameId, const PicaSceneFrame& picaFrame,
                                       std::optional<PerspectiveViewState> perspective,
                                       std::optional<NativeViewFamily> viewFamily) noexcept;

    const PicaSceneFrame* mPicaFrame = nullptr;
    uint64_t mFrameId = 0;
    uint64_t mGeneration = 0;
    uint64_t mNextGeneration = 1;
    std::optional<PerspectiveViewState> mCurrentPerspective;
    std::optional<PerspectiveViewState> mPreviousPerspective;
    std::optional<NativeViewFamily> mCurrentViewFamily;
    std::optional<NativeViewFamily> mPreviousViewFamily;
};

} // namespace Fast::Oot3d
