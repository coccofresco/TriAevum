#include "fast/renderer3ds/pica_composition_schedule.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace Fast::Renderer3ds {
namespace {

using CompositionAttribution = PicaCompositionAttribution;
using CompositionDomain = PicaCompositionDomain;
using CompositionDrawReference = PicaCompositionDrawReference;
using CompositionLayer = PicaCompositionLayer;
using CompositionStage = ::Fast::Renderer::ExtensionStage;
using CompositionTarget = PicaCompositionTargetReference;

void SetError(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

[[nodiscard]] bool KnownLayerMatchesDomain(
    CompositionDomain domain, CompositionLayer layer) noexcept {
    switch (layer) {
        case CompositionLayer::OpaqueWorld:
        case CompositionLayer::TransparentWorld:
        case CompositionLayer::Atmosphere:
            return domain == CompositionDomain::Scene;
        case CompositionLayer::Ui:
            return domain == CompositionDomain::Ui;
        case CompositionLayer::Unknown:
            return true;
    }
    return false;
}

[[nodiscard]] bool SameRun(const PicaCompositionRun& run,
                           const CompositionDrawReference& draw) noexcept {
    return run.Target == draw.Target && run.Domain == draw.Domain &&
           run.Composition == draw.Composition;
}

} // namespace

bool PicaCompositionSchedule::Compile(
    const PicaCompositionSequenceView& sequence,
    std::string* error) {
    Reset();
    if (sequence.SchemaVersion !=
        kPicaCompositionSequenceSchemaVersion) {
        SetError(error, "native PICA composition sequence schema is unsupported");
        return false;
    }
    if (sequence.SequenceId == 0U) {
        SetError(error, "native PICA composition sequence identity is missing");
        return false;
    }
    if (sequence.Draws.empty()) {
        SetError(error, "native PICA composition sequence has no draws");
        return false;
    }

    std::unordered_set<uint64_t> submissionIds;
    submissionIds.reserve(sequence.Draws.size());
    mDraws.reserve(sequence.Draws.size());
    for (const auto& draw : sequence.Draws) {
        if (draw.SubmissionId == 0U ||
            !submissionIds.insert(draw.SubmissionId).second) {
            SetError(error,
                     "native PICA composition sequence has an invalid draw identity");
            Reset();
            return false;
        }
        if (draw.Target.FramebufferWidth == 0U ||
            draw.Target.FramebufferHeight == 0U) {
            SetError(error,
                     "native PICA composition sequence has an invalid target extent");
            Reset();
            return false;
        }
        if (!KnownLayerMatchesDomain(draw.Domain, draw.Composition.Layer)) {
            SetError(error,
                     "native PICA composition layer contradicts its domain");
            Reset();
            return false;
        }
        mDraws.push_back(draw);
    }
    mSequenceId = sequence.SequenceId;

    for (size_t index = 0U; index < mDraws.size(); ++index) {
        const auto& draw = mDraws[index];
        if (!mRuns.empty() && SameRun(mRuns.back(), draw)) {
            ++mRuns.back().DrawCount;
        } else {
            mRuns.push_back({draw.Target, index, 1U, draw.Domain,
                             draw.Composition});
        }
        auto target = std::find_if(
            mTargets.begin(), mTargets.end(),
            [&draw](const PicaCompositionTargetSchedule& candidate) {
                return candidate.Target == draw.Target;
            });
        if (target == mTargets.end()) {
            mTargets.push_back({.Target = draw.Target});
            target = std::prev(mTargets.end());
        }
        ++target->DrawCount;
        switch (draw.Composition.Layer) {
            case CompositionLayer::OpaqueWorld:
                ++target->OpaqueWorldDrawCount;
                break;
            case CompositionLayer::TransparentWorld:
                ++target->TransparentWorldDrawCount;
                break;
            case CompositionLayer::Atmosphere:
                ++target->AtmosphereDrawCount;
                break;
            case CompositionLayer::Ui:
                ++target->UiDrawCount;
                break;
            case CompositionLayer::Unknown:
                ++target->UnknownDrawCount;
                break;
        }
    }

    const auto addAnchor = [this](
                               CompositionStage stage,
                               const CompositionTarget& target,
                               size_t beforeDrawIndex,
                               bool atTargetEnd) {
        const uint64_t submissionId = beforeDrawIndex < mDraws.size()
            ? mDraws[beforeDrawIndex].SubmissionId
            : 0U;
        mAnchors.push_back({stage, target, beforeDrawIndex, submissionId,
                            atTargetEnd});
    };

    for (auto& target : mTargets) {
        std::vector<size_t> targetDraws;
        targetDraws.reserve(target.DrawCount);
        for (size_t index = 0U; index < mDraws.size(); ++index) {
            if (mDraws[index].Target == target.Target) {
                targetDraws.push_back(index);
            }
        }
        if (target.UnknownDrawCount != 0U) {
            target.WorldStageIssue =
                PicaCompositionScheduleIssue::UnknownLayer;
            continue;
        }

        const bool hasSceneDraw = std::any_of(
            targetDraws.begin(), targetDraws.end(),
            [this](size_t drawIndex) {
                return mDraws[drawIndex].Domain == CompositionDomain::Scene;
            });
        if (!hasSceneDraw) {
            target.WorldStageIssue =
                PicaCompositionScheduleIssue::NoSceneDomain;
            continue;
        }

        std::vector<size_t> opaquePositions;
        for (size_t position = 0U; position < targetDraws.size(); ++position) {
            if (mDraws[targetDraws[position]].Composition.Layer ==
                CompositionLayer::OpaqueWorld) {
                opaquePositions.push_back(position);
            }
        }
        if (opaquePositions.empty()) {
            target.WorldStageIssue =
                PicaCompositionScheduleIssue::MissingOpaqueWorld;
            continue;
        }

        const size_t firstOpaquePosition = opaquePositions.front();
        const size_t lastOpaquePosition = opaquePositions.back();
        // Completion of a scene-only target does not require a canonical
        // opaque/transparent order. Mid-scene effects still need their stricter
        // anchors; display effects may read the completed, UI-free target.
        if (target.UiDrawCount == 0U) {
            const size_t afterTarget = targetDraws.back() + 1U;
            addAnchor(CompositionStage::SceneResolved, target.Target, afterTarget, true);
            addAnchor(CompositionStage::BeforeUi, target.Target, afterTarget, true);
        }
        size_t prefixEnd = firstOpaquePosition + 1U;
        while (prefixEnd < targetDraws.size() &&
               mDraws[targetDraws[prefixEnd]].Composition.Layer ==
                   CompositionLayer::OpaqueWorld) {
            ++prefixEnd;
        }
        const bool prefixAtEnd = prefixEnd == targetDraws.size();
        const size_t prefixEndIndex = prefixAtEnd ? mDraws.size() : targetDraws[prefixEnd];
        mPublishedGeometryAnchors.push_back({
            CompositionStage::BeforeTransparent, target.Target, prefixEndIndex,
            prefixAtEnd ? 0U : mDraws[prefixEndIndex].SubmissionId, prefixAtEnd});
        bool contiguousOpaque = true;
        for (size_t position = firstOpaquePosition;
             position <= lastOpaquePosition; ++position) {
            const auto& draw = mDraws[targetDraws[position]];
            if (draw.Domain != CompositionDomain::Scene ||
                draw.Composition.Layer != CompositionLayer::OpaqueWorld) {
                contiguousOpaque = false;
                break;
            }
        }
        if (!contiguousOpaque) {
            target.WorldStageIssue =
                PicaCompositionScheduleIssue::NonContiguousOpaqueWorld;
            continue;
        }

        bool atmosphereTail = false;
        bool canonicalTail = true;
        std::optional<size_t> firstTrailingAtmospherePosition;
        std::optional<size_t> lastTrailingTransparentPosition;
        for (size_t position = lastOpaquePosition + 1U;
             position < targetDraws.size(); ++position) {
            const auto& draw = mDraws[targetDraws[position]];
            if (draw.Domain == CompositionDomain::Ui) {
                continue;
            }
            if (draw.Domain != CompositionDomain::Scene) {
                canonicalTail = false;
                break;
            }
            if (draw.Composition.Layer ==
                CompositionLayer::TransparentWorld) {
                if (atmosphereTail) {
                    canonicalTail = false;
                    break;
                }
                lastTrailingTransparentPosition = position;
            } else if (draw.Composition.Layer ==
                       CompositionLayer::Atmosphere) {
                atmosphereTail = true;
                if (!firstTrailingAtmospherePosition.has_value()) {
                    firstTrailingAtmospherePosition = position;
                }
            } else {
                canonicalTail = false;
                break;
            }
        }
        if (!canonicalTail) {
            target.WorldStageIssue =
                PicaCompositionScheduleIssue::NonCanonicalPostOpaqueOrder;
            continue;
        }

        target.WorldStageIssue = PicaCompositionScheduleIssue::None;
        const size_t beforeOpaque = targetDraws[firstOpaquePosition];
        const bool opaqueAtTargetEnd =
            lastOpaquePosition + 1U == targetDraws.size();
        const size_t afterOpaque = opaqueAtTargetEnd
            ? mDraws.size()
            : targetDraws[lastOpaquePosition + 1U];
        addAnchor(CompositionStage::BeforeOpaque, target.Target, beforeOpaque,
                  false);
        addAnchor(CompositionStage::AfterOpaque, target.Target, afterOpaque,
                  opaqueAtTargetEnd);
        addAnchor(CompositionStage::BeforeTransparent, target.Target, afterOpaque,
                  opaqueAtTargetEnd);

        size_t afterTransparent = afterOpaque;
        bool transparentAtTargetEnd = opaqueAtTargetEnd;
        if (firstTrailingAtmospherePosition.has_value()) {
            afterTransparent =
                targetDraws[*firstTrailingAtmospherePosition];
            transparentAtTargetEnd = false;
        } else if (lastTrailingTransparentPosition.has_value()) {
            const size_t nextPosition =
                *lastTrailingTransparentPosition + 1U;
            transparentAtTargetEnd = nextPosition == targetDraws.size();
            afterTransparent = transparentAtTargetEnd
                ? mDraws.size()
                : targetDraws[nextPosition];
        }
        addAnchor(CompositionStage::AfterTransparent, target.Target,
                  afterTransparent, transparentAtTargetEnd);
        addAnchor(CompositionStage::Atmosphere, target.Target,
                  firstTrailingAtmospherePosition.has_value()
                      ? targetDraws[*firstTrailingAtmospherePosition]
                      : afterTransparent,
                  firstTrailingAtmospherePosition.has_value()
                      ? false
                      : transparentAtTargetEnd);

        size_t lastSceneIndex = 0U;
        bool sceneFound = false;
        for (const size_t drawIndex : targetDraws) {
            if (mDraws[drawIndex].Domain == CompositionDomain::Scene) {
                lastSceneIndex = drawIndex;
                sceneFound = true;
            }
        }
        if (sceneFound) {
            size_t beforeUi = mDraws.size();
            for (size_t index = lastSceneIndex + 1U;
                 index < mDraws.size(); ++index) {
                if (mDraws[index].Domain == CompositionDomain::Ui) {
                    beforeUi = index;
                    break;
                }
            }
            if (FindAnchor(CompositionStage::SceneResolved, target.Target) == nullptr) {
                addAnchor(CompositionStage::SceneResolved, target.Target, beforeUi,
                          beforeUi == mDraws.size());
                addAnchor(CompositionStage::BeforeUi, target.Target, beforeUi,
                          beforeUi == mDraws.size());
            }
            addAnchor(CompositionStage::AfterUi, target.Target, mDraws.size(),
                      true);
        }
    }

    mValid = true;
    return true;
}

void PicaCompositionSchedule::Reset() noexcept {
    mDraws.clear();
    mRuns.clear();
    mAnchors.clear();
    mPublishedGeometryAnchors.clear();
    mTargets.clear();
    mSequenceId = 0U;
    mConsumedDrawCount = 0U;
    mExecutionMismatchCount = 0U;
    mValid = false;
}

bool PicaCompositionSchedule::Valid() const noexcept {
    return mValid;
}

uint64_t PicaCompositionSchedule::SequenceId() const noexcept {
    return mSequenceId;
}

std::span<const PicaCompositionRun>
PicaCompositionSchedule::Runs() const noexcept {
    return mRuns;
}

std::span<const PicaCompositionStageAnchor>
PicaCompositionSchedule::Anchors() const noexcept {
    return mAnchors;
}

std::span<const PicaCompositionTargetSchedule>
PicaCompositionSchedule::Targets() const noexcept {
    return mTargets;
}

const PicaCompositionTargetSchedule* PicaCompositionSchedule::FindTarget(
    const CompositionTarget& target) const noexcept {
    const auto found = std::find_if(
        mTargets.begin(), mTargets.end(),
        [&target](const PicaCompositionTargetSchedule& candidate) {
            return candidate.Target == target;
        });
    return found == mTargets.end() ? nullptr : &*found;
}

const PicaCompositionStageAnchor* PicaCompositionSchedule::FindAnchor(
    CompositionStage stage, const CompositionTarget& target) const noexcept {
    const auto found = std::find_if(
        mAnchors.begin(), mAnchors.end(),
        [stage, &target](const PicaCompositionStageAnchor& anchor) {
            return anchor.Stage == stage && anchor.Target == target;
        });
    return found == mAnchors.end() ? nullptr : &*found;
}

const PicaCompositionStageAnchor*
PicaCompositionSchedule::FindAnchorBeforeDraw(
    CompositionStage stage, const CompositionTarget& target,
    uint64_t submissionId) const noexcept {
    const auto found = std::find_if(
        mAnchors.begin(), mAnchors.end(),
        [stage, &target, submissionId](
            const PicaCompositionStageAnchor& anchor) {
            return anchor.Stage == stage && anchor.Target == target &&
                   !anchor.AtTargetEnd &&
                   anchor.BeforeSubmissionId == submissionId;
        });
    return found == mAnchors.end() ? nullptr : &*found;
}

const PicaCompositionStageAnchor*
PicaCompositionSchedule::FindTargetEndAnchor(
    CompositionStage stage, const CompositionTarget& target) const noexcept {
    const auto* anchor = FindAnchor(stage, target);
    return anchor != nullptr && anchor->AtTargetEnd ? anchor : nullptr;
}

bool PicaCompositionSchedule::Reached(
    const PicaCompositionStageAnchor& anchor) const noexcept {
    return mValid && anchor.BeforeDrawIndex <= mConsumedDrawCount;
}

const PicaCompositionStageAnchor* PicaCompositionSchedule::FindPublishedGeometryAnchor(
    const CompositionTarget& target) const noexcept {
    const auto found = std::find_if(
        mPublishedGeometryAnchors.begin(), mPublishedGeometryAnchors.end(),
        [&target](const auto& anchor) { return anchor.Target == target; });
    return found == mPublishedGeometryAnchors.end() ? nullptr : &*found;
}

bool PicaCompositionSchedule::ConsumeDraw(
    const PicaCompositionDrawReference& draw,
    std::string* error) {
    if (!mValid || mConsumedDrawCount >= mDraws.size()) {
        ++mExecutionMismatchCount;
        SetError(error,
                 "native PICA draw has no matching composition schedule entry");
        return false;
    }
    const auto& expected = mDraws[mConsumedDrawCount];
    if (expected.SubmissionId != draw.SubmissionId ||
        expected.Target != draw.Target ||
        expected.Domain != draw.Domain ||
        expected.Composition != draw.Composition) {
        ++mExecutionMismatchCount;
        SetError(error,
                 "native PICA draw contradicts its composition schedule");
        return false;
    }
    ++mConsumedDrawCount;
    return true;
}

PicaCompositionScheduleStats PicaCompositionSchedule::Stats() const noexcept {
    PicaCompositionScheduleStats stats;
    stats.DrawCount = static_cast<uint32_t>(mDraws.size());
    stats.ConsumedDrawCount =
        static_cast<uint32_t>(mConsumedDrawCount);
    stats.RunCount = static_cast<uint32_t>(mRuns.size());
    stats.TargetCount = static_cast<uint32_t>(mTargets.size());
    stats.StageAnchorCount = static_cast<uint32_t>(mAnchors.size());
    stats.ExecutionMismatchCount = mExecutionMismatchCount;
    for (const auto& target : mTargets) {
        switch (target.WorldStageIssue) {
            case PicaCompositionScheduleIssue::None:
                ++stats.WorldStageTargetCount;
                break;
            case PicaCompositionScheduleIssue::NoSceneDomain:
                ++stats.NonSceneTargetCount;
                break;
            case PicaCompositionScheduleIssue::UnknownLayer:
                ++stats.UnknownLayerTargetCount;
                break;
            case PicaCompositionScheduleIssue::NonContiguousOpaqueWorld:
                ++stats.NonContiguousWorldTargetCount;
                break;
            case PicaCompositionScheduleIssue::NonCanonicalPostOpaqueOrder:
                ++stats.NonCanonicalTailTargetCount;
                break;
            case PicaCompositionScheduleIssue::MissingOpaqueWorld:
                ++stats.MissingOpaqueWorldTargetCount;
                break;
        }
    }
    return stats;
}

} // namespace Fast::Renderer3ds
