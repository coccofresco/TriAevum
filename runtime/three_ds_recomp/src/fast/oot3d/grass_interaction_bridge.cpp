#include "fast/oot3d/grass_interaction_bridge.h"

#include <algorithm>
#include <cmath>

namespace Fast::Oot3d {

GrassInteractionBridge& GrassInteractionBridge::Instance() {
    static GrassInteractionBridge bridge;
    return bridge;
}

void GrassInteractionBridge::PublishLink(
    const std::array<float, 3>& position, float radius, float halfHeight,
    uint64_t frameId) {
    PublishLink(position, {}, radius, halfHeight, frameId, 1U, 0U);
}

void GrassInteractionBridge::PublishLink(
    const std::array<float, 3>& position,
    const std::array<float, 3>& velocity, float radius, float halfHeight,
    uint64_t frameId, uint64_t stableId, uint64_t contextId) {
    std::scoped_lock lock(mMutex);
    GrassInteractor next;
    next.StableId = stableId;
    next.ContextId = contextId;
    next.Position = position;
    next.Velocity = velocity;
    next.Radius = std::max(0.01F, radius);
    next.HalfHeight = std::max(0.01F, halfHeight);
    next.FrameId = frameId;
    const bool identityChanged =
        mLink.has_value() &&
        (mLink->StableId != stableId || mLink->ContextId != contextId);
    const bool sequenceDiscontinuity =
        mLink.has_value() &&
        (frameId < mLink->FrameId || frameId > mLink->FrameId + 4U);
    float distance = 0.0F;
    if (mLink.has_value()) {
        for (size_t axis = 0; axis < 3; ++axis) {
            const float delta = position[axis] - mLink->Position[axis];
            distance += delta * delta;
        }
        distance = std::sqrt(distance);
    }
    const bool spatialDiscontinuity =
        mLink.has_value() &&
        distance > std::max(500.0F, next.Radius * 12.0F);
    next.Teleported = !mLink.has_value() || identityChanged ||
                      sequenceDiscontinuity || spatialDiscontinuity;
    next.PreviousPosition =
        next.Teleported ? position : mLink->Position;
    if (mLink.has_value() && frameId == mLink->FrameId &&
        !identityChanged) {
        // Multiple presentations can consume one gameplay sample. Retain the
        // original swept segment instead of collapsing it on the second draw.
        next.PreviousPosition = mLink->PreviousPosition;
        next.Teleported = mLink->Teleported;
    }
    mLink = next;
}

std::optional<GrassInteractor> GrassInteractionBridge::LatestLink() const {
    std::scoped_lock lock(mMutex);
    return mLink;
}

void GrassInteractionBridge::Reset() {
    std::scoped_lock lock(mMutex);
    mLink.reset();
    mFrames.clear();
    mActors.clear();
    mSample = {};
    mPresentationId = 0;
}

void GrassInteractionBridge::PublishFrame(uint64_t sourceFrame,
                                         std::span<const GrassInteractor> actors) {
    std::scoped_lock lock(mMutex);
    if (!mFrames.empty() && sourceFrame <= mFrames.back().Id) {
        if (sourceFrame == mFrames.back().Id) return;
        mFrames.clear();
        mActors.clear();
        mSample = {};
    }
    Frame frame{sourceFrame, {actors.begin(), actors.end()}};
    std::erase_if(frame.Actors, [](const auto& actor) {
        return !std::isfinite(actor.Radius) || !std::isfinite(actor.HalfHeight) ||
               actor.Radius <= 0 || actor.HalfHeight <= 0 ||
               !std::all_of(actor.Position.begin(), actor.Position.end(),
                            [](float v) { return std::isfinite(v); });
    });
    mFrames.push_back(std::move(frame));
    while (mFrames.size() > 4) mFrames.pop_front();
}

void GrassInteractionBridge::SelectSample(const Renderer3ds::PicaFrameTemporalSample& sample) {
    std::scoped_lock lock(mMutex);
    if (sample.CurrentSourceFrameId == mSample.CurrentSourceFrameId &&
        sample.PreviousSourceFrameId == mSample.PreviousSourceFrameId &&
        sample.ContinuityEpoch == mSample.ContinuityEpoch && sample.Alpha == mSample.Alpha) return;
    const auto findFrame = [&](uint64_t id) -> const Frame* {
        for (const auto& frame : mFrames) if (frame.Id == id) return &frame;
        return nullptr;
    };
    const auto* current = findFrame(sample.CurrentSourceFrameId);
    const auto* previous = findFrame(sample.PreviousSourceFrameId);
    const bool reset = sample.HistoryReset || sample.ContinuityEpoch != mSample.ContinuityEpoch;
    const auto old = std::move(mActors);
    mActors.clear();
    mSample = sample;
    ++mPresentationId;
    if (!current) return; // Never retain actors from a different scene/frame.
    const auto same = [](const GrassInteractor& a, const GrassInteractor& b) {
        return a.StableId == b.StableId && a.ContextId == b.ContextId;
    };
    const auto continuous = [](const GrassInteractor& a, const GrassInteractor& b) {
        float distance = 0;
        for (size_t axis = 0; axis < 3; ++axis) {
            const float d = a.Position[axis] - b.Position[axis];
            distance += d * d;
        }
        const float limit = std::max(500.0F, a.Radius * 12.0F);
        return distance <= limit * limit;
    };
    for (auto actor : current->Actors) {
        if (!reset && previous && previous != current) {
            const auto it = std::find_if(previous->Actors.begin(), previous->Actors.end(),
                                         [&](const auto& a) { return same(a, actor); });
            if (it != previous->Actors.end() && continuous(actor, *it)) {
                for (size_t axis = 0; axis < 3; ++axis)
                    actor.Position[axis] = std::lerp(it->Position[axis], actor.Position[axis], sample.Alpha);
            }
        }
        const auto it = std::find_if(old.begin(), old.end(), [&](const auto& a) { return same(a, actor); });
        actor.Teleported = reset || it == old.end() || !continuous(actor, *it);
        actor.PreviousPosition = actor.Teleported ? actor.Position : it->Position;
        actor.Velocity = {};
        if (!actor.Teleported && sample.SampleDeltaSeconds > 0) {
            for (size_t axis = 0; axis < 3; ++axis)
                actor.Velocity[axis] = (actor.Position[axis] - actor.PreviousPosition[axis]) / sample.SampleDeltaSeconds;
        }
        actor.FrameId = mPresentationId;
        mActors.push_back(actor);
    }
}

std::vector<GrassInteractor> GrassInteractionBridge::LatestActors() const {
    std::scoped_lock lock(mMutex);
    return mActors;
}

} // namespace Fast::Oot3d
