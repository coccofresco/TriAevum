#include "fast/oot3d/native_scene_view.h"

#include <utility>

namespace Fast::Oot3d {
namespace {

[[nodiscard]] bool SamePerspectiveState(const PerspectiveViewState& left, const PerspectiveViewState& right) noexcept {
    if (left.Serial != 0U || right.Serial != 0U) {
        return left.Serial == right.Serial && left.CameraAvailable == right.CameraAvailable;
    }
    return left.GuestFunction == right.GuestFunction && left.GuestReturnAddress == right.GuestReturnAddress &&
           left.Left == right.Left && left.Right == right.Right && left.Bottom == right.Bottom &&
           left.Top == right.Top && left.NearPlane == right.NearPlane && left.FarPlane == right.FarPlane &&
           left.Eye == right.Eye && left.At == right.At && left.CameraAvailable == right.CameraAvailable;
}

[[nodiscard]] bool SamePerspective(
    const std::optional<PerspectiveViewState>& left,
    const std::optional<PerspectiveViewState>& right) noexcept {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    return SamePerspectiveState(*left, *right);
}

[[nodiscard]] bool SameViewFamily(const std::optional<NativeViewFamily>& left,
                                  const std::optional<NativeViewFamily>& right) noexcept {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    if (left->SchemaVersion != right->SchemaVersion || left->FamilyId != right->FamilyId ||
        left->ViewCount != right->ViewCount) {
        return false;
    }
    for (size_t index = 0; index < left->ViewCount; ++index) {
        const auto& leftView = left->Views[index];
        const auto& rightView = right->Views[index];
        if (leftView.SchemaVersion != rightView.SchemaVersion || leftView.ViewId != rightView.ViewId ||
            leftView.PoseVersion != rightView.PoseVersion ||
            leftView.ProjectionVersion != rightView.ProjectionVersion || leftView.Role != rightView.Role ||
            !SamePerspectiveState(leftView.Perspective, rightView.Perspective)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool NativeSceneView::Publish(
    uint64_t frameId, const PicaSceneFrame& picaFrame,
    std::optional<PerspectiveViewState> perspective) noexcept {
    std::optional<NativeViewFamily> viewFamily;
    if (perspective.has_value()) {
        viewFamily = BuildMonoNativeViewFamily(*perspective);
    }
    return PublishResolved(frameId, picaFrame, std::move(perspective), std::move(viewFamily));
}

bool NativeSceneView::PublishViewFamily(uint64_t frameId, const PicaSceneFrame& picaFrame,
                                        NativeViewFamily viewFamily) noexcept {
    if (!viewFamily.Available()) {
        Reset();
        return false;
    }

    std::optional<PerspectiveViewState> perspective = viewFamily.Views[0].Perspective;
    return PublishResolved(frameId, picaFrame, std::move(perspective), std::move(viewFamily));
}

bool NativeSceneView::PublishResolved(uint64_t frameId, const PicaSceneFrame& picaFrame,
                                      std::optional<PerspectiveViewState> perspective,
                                      std::optional<NativeViewFamily> viewFamily) noexcept {
    if (frameId == 0U || !picaFrame.Active() ||
        picaFrame.FrameId() != frameId ||
        (viewFamily.has_value() && !viewFamily->Available())) {
        Reset();
        return false;
    }

    const bool frameChanged = mPicaFrame != &picaFrame ||
                              mFrameId != frameId;
    const bool perspectiveChanged =
        !SamePerspective(mCurrentPerspective, perspective);
    const bool viewFamilyChanged = !SameViewFamily(mCurrentViewFamily, viewFamily);
    if (frameChanged || perspectiveChanged || viewFamilyChanged) {
        mPreviousPerspective = mCurrentPerspective;
        mPreviousViewFamily = mCurrentViewFamily;
        mCurrentPerspective = std::move(perspective);
        mCurrentViewFamily = std::move(viewFamily);
        mGeneration = mNextGeneration++;
    }
    mPicaFrame = &picaFrame;
    mFrameId = frameId;
    return true;
}

void NativeSceneView::Reset() noexcept {
    if (mPicaFrame != nullptr || mCurrentPerspective.has_value() ||
        mPreviousPerspective.has_value() ||
        mCurrentViewFamily.has_value() || mPreviousViewFamily.has_value()) {
        mGeneration = mNextGeneration++;
    }
    mPicaFrame = nullptr;
    mFrameId = 0;
    mCurrentPerspective.reset();
    mPreviousPerspective.reset();
    mCurrentViewFamily.reset();
    mPreviousViewFamily.reset();
}

bool NativeSceneView::Active() const noexcept {
    return mPicaFrame != nullptr && mFrameId != 0U &&
           mPicaFrame->Active() && mPicaFrame->FrameId() == mFrameId;
}

uint64_t NativeSceneView::FrameId() const noexcept {
    return Active() ? mFrameId : 0U;
}

uint64_t NativeSceneView::Generation() const noexcept {
    return mGeneration;
}

const PicaSceneFrame* NativeSceneView::PicaFrame() const noexcept {
    return Active() ? mPicaFrame : nullptr;
}

const NativeFrameTemporalSample& NativeSceneView::TemporalSample() const noexcept {
    static const NativeFrameTemporalSample unavailable;
    return Active() ? mPicaFrame->TemporalSample() : unavailable;
}

const std::optional<PerspectiveViewState>&
NativeSceneView::CurrentPerspective() const noexcept {
    return mCurrentPerspective;
}

const std::optional<PerspectiveViewState>&
NativeSceneView::PreviousPerspective() const noexcept {
    return mPreviousPerspective;
}

const std::optional<NativeViewFamily>& NativeSceneView::CurrentViewFamily() const noexcept {
    return mCurrentViewFamily;
}

const std::optional<NativeViewFamily>& NativeSceneView::PreviousViewFamily() const noexcept {
    return mPreviousViewFamily;
}

bool NativeSceneView::CameraHistoryAvailable() const noexcept {
    return Active() && mCurrentPerspective.has_value() &&
           mPreviousPerspective.has_value() &&
           mCurrentPerspective->CameraAvailable &&
           mPreviousPerspective->CameraAvailable;
}

bool NativeSceneView::ViewFamilyHistoryAvailable() const noexcept {
    if (!Active() || !mCurrentViewFamily.has_value() || !mPreviousViewFamily.has_value() ||
        !mCurrentViewFamily->Available() || !mPreviousViewFamily->Available() ||
        mCurrentViewFamily->FamilyId != mPreviousViewFamily->FamilyId ||
        mCurrentViewFamily->ViewCount != mPreviousViewFamily->ViewCount) {
        return false;
    }
    for (size_t index = 0; index < mCurrentViewFamily->ViewCount; ++index) {
        if (mCurrentViewFamily->Views[index].ViewId != mPreviousViewFamily->Views[index].ViewId) {
            return false;
        }
    }
    return true;
}

std::span<const PicaSceneDrawRecord> NativeSceneView::Draws() const noexcept {
    return Active() ? mPicaFrame->Draws()
                    : std::span<const PicaSceneDrawRecord>{};
}

::Fast::Renderer3ds::PicaSemanticSceneView
NativeSceneView::SharedSemanticView() const noexcept {
    if (!Active()) {
        return {};
    }
    const auto& temporalSample = mPicaFrame->TemporalSample();
    const auto* currentViewFamily =
        mCurrentViewFamily.has_value() &&
                mCurrentViewFamily->Available()
            ? &*mCurrentViewFamily
            : nullptr;
    const auto* previousViewFamily =
        mPreviousViewFamily.has_value() &&
                mPreviousViewFamily->Available()
            ? &*mPreviousViewFamily
            : nullptr;
    return {
        ::Fast::Renderer3ds::kPicaSemanticSceneViewSchemaVersion,
        mFrameId,
        mGeneration,
        mPicaFrame->ResolvedDrawStream(),
        temporalSample.Available() ? &temporalSample : nullptr,
        currentViewFamily,
        previousViewFamily,
    };
}

std::optional<NativeSceneDrawSemantics>
NativeSceneView::DescribeDraw(size_t drawIndex) const noexcept {
    const auto shared = SharedSemanticView().DescribeDraw(drawIndex);
    if (!shared.has_value()) {
        return std::nullopt;
    }
    NativeSceneDrawSemantics result;
    static_cast<::Fast::Renderer3ds::PicaSemanticDrawView&>(result) =
        *shared;
    return result;
}

} // namespace Fast::Oot3d
