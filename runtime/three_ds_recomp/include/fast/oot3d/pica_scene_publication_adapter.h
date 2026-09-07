#pragma once

#include "fast/oot3d/native_scene_view.h"
#include "fast/renderer3ds/pica_scene_capabilities.h"

#include <array>
#include <cstdint>

namespace Fast::Oot3d {

// OOT3D owns concrete scene payloads; this adapter exposes only the common
// PICA capabilities needed by cross-title 3DS renderer providers.
class PicaScenePublicationAdapter final {
  public:
    [[nodiscard]] bool Publish(
        uint64_t frameId, const PicaSceneFrame& frame,
        const NativeSceneView& view) noexcept;
    void Reset() noexcept;

    [[nodiscard]] ::Fast::Renderer::ExtensionScenePublicationTableView
    View() const noexcept;
    [[nodiscard]] const ::Fast::Renderer3ds::PicaResolvedDrawStreamView*
    ResolveDrawStream() const noexcept;
    [[nodiscard]] const ::Fast::Renderer3ds::PicaSemanticSceneView*
    ResolveSemanticView() const noexcept;
    [[nodiscard]] const ::Fast::Renderer3ds::PicaPerspectiveCameraState*
    ResolvePerspectiveCamera() const noexcept;

  private:
    static constexpr size_t kPublicationCapacity = 3U;

    uint64_t mFrameId = 0U;
    ::Fast::Renderer3ds::PicaResolvedDrawStreamView mResolvedDrawStream;
    ::Fast::Renderer3ds::PicaSemanticSceneView mSemanticSceneView;
    std::array<::Fast::Renderer::ExtensionSceneCapabilityPublication,
               kPublicationCapacity>
        mPublications{};
};

[[nodiscard]] inline std::array<
    ::Fast::Renderer::ExtensionSceneCapabilityRequirement, 3U>
BuildOot3dGeometryProviderSceneRequirements() noexcept {
    return ::Fast::Renderer3ds::
        BuildPicaGeometryProviderSceneRequirements();
}

} // namespace Fast::Oot3d
