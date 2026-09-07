#include "fast/oot3d/pica_scene_publication_adapter.h"

namespace Fast::Oot3d {
namespace {

using ::Fast::Renderer3ds::PicaSceneCapability;

template <typename T>
[[nodiscard]] const T* ResolvePublishedPayload(
    ::Fast::Renderer::ExtensionScenePublicationTableView publications,
    PicaSceneCapability capability, uint32_t expectedSchemaVersion) noexcept {
    const auto* publication = publications.Find(
        ::Fast::Renderer3ds::PicaSceneCapabilityIdentity(capability));
    if (publication == nullptr ||
        publication->PayloadSchemaVersion != expectedSchemaVersion) {
        return nullptr;
    }
    return static_cast<const T*>(publication->Payload);
}

} // namespace

bool PicaScenePublicationAdapter::Publish(
    uint64_t frameId, const PicaSceneFrame& frame,
    const NativeSceneView& view) noexcept {
    Reset();
    if (frameId == 0U || !frame.Active() || frame.FrameId() != frameId) {
        return false;
    }
    mResolvedDrawStream = frame.ResolvedDrawStream();
    if (!mResolvedDrawStream.Available()) {
        Reset();
        return false;
    }
    mFrameId = frameId;
    mPublications[0] =
        ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
            PicaSceneCapability::ResolvedDrawStream,
            &mResolvedDrawStream,
            ::Fast::Renderer3ds::kPicaResolvedDrawStreamSchemaVersion,
            frame.FrameId(), frameId);
    if (!view.Active() || view.FrameId() != frameId ||
        view.PicaFrame() != &frame) {
        return View().Valid();
    }
    mSemanticSceneView = view.SharedSemanticView();
    if (!mSemanticSceneView.Available()) {
        mSemanticSceneView = {};
        return View().Valid();
    }
    mPublications[1] =
        ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
            PicaSceneCapability::SemanticSceneView,
            &mSemanticSceneView,
            ::Fast::Renderer3ds::kPicaSemanticSceneViewSchemaVersion,
            mSemanticSceneView.Generation, frameId);
    const auto& perspective = view.CurrentPerspective();
    if (perspective.has_value() && perspective->CameraAvailable) {
        mPublications[2] =
            ::Fast::Renderer3ds::BuildPicaSceneCapabilityPublication(
                PicaSceneCapability::PerspectiveCamera, &*perspective,
                ::Fast::Renderer3ds::kPicaPerspectiveCameraSchemaVersion,
                view.Generation(), frameId);
    }
    return View().Valid();
}

void PicaScenePublicationAdapter::Reset() noexcept {
    mFrameId = 0U;
    mResolvedDrawStream = {};
    mSemanticSceneView = {};
    mPublications.fill({});
}

::Fast::Renderer::ExtensionScenePublicationTableView
PicaScenePublicationAdapter::View() const noexcept {
    return ::Fast::Renderer::ExtensionScenePublicationTableView{
        mFrameId, mPublications };
}

const ::Fast::Renderer3ds::PicaResolvedDrawStreamView*
PicaScenePublicationAdapter::ResolveDrawStream() const noexcept {
    return ResolvePublishedPayload<
        ::Fast::Renderer3ds::PicaResolvedDrawStreamView>(
        View(), PicaSceneCapability::ResolvedDrawStream,
        ::Fast::Renderer3ds::kPicaResolvedDrawStreamSchemaVersion);
}

const ::Fast::Renderer3ds::PicaSemanticSceneView*
PicaScenePublicationAdapter::ResolveSemanticView() const noexcept {
    return ResolvePublishedPayload<
        ::Fast::Renderer3ds::PicaSemanticSceneView>(
        View(), PicaSceneCapability::SemanticSceneView,
        ::Fast::Renderer3ds::kPicaSemanticSceneViewSchemaVersion);
}

const ::Fast::Renderer3ds::PicaPerspectiveCameraState*
PicaScenePublicationAdapter::ResolvePerspectiveCamera() const noexcept {
    return ResolvePublishedPayload<
        ::Fast::Renderer3ds::PicaPerspectiveCameraState>(
        View(), PicaSceneCapability::PerspectiveCamera,
        ::Fast::Renderer3ds::kPicaPerspectiveCameraSchemaVersion);
}

} // namespace Fast::Oot3d
