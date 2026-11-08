#pragma once

#include "fast/renderer3ds/pica_render_backend.h"

#include <array>
#include <bit>
#include <map>
#include <set>

namespace Fast::Renderer3ds {

struct PicaTemporalHistoryKey {
    PicaCompositionTargetReference View;
    PicaTemporalIdentity Instance;
    uint64_t Epoch = 0;
    auto operator<=>(const PicaTemporalHistoryKey&) const = default;
};

// Program/layout compatibility is separate from instance identity. Input bytes
// include default/per-instance attributes and indices: the previous shader is
// evaluated on CURRENT bindings, so changed inputs cannot claim exact motion.
struct PicaTemporalVertexState {
    uint64_t Program = 0;
    uint64_t Shader = 0;
    std::vector<uint64_t> Layout;
    std::vector<std::vector<uint8_t>> Inputs;
    std::vector<uint8_t> Indices;

    static PicaTemporalVertexState Capture(const PicaDrawView& draw) {
        PicaTemporalVertexState result;
        result.Program = draw.CanonicalVertexProgramId;
        result.Shader = draw.VertexShaderKey;
        result.Layout = {uint64_t(draw.Topology), draw.Indexed, draw.IndicesAre16Bit,
            uint64_t(uint32_t(draw.BaseVertex)), draw.VertexCount,
            draw.VertexBindings.size(), draw.VertexAttributes.size(),
            std::bit_cast<uint32_t>(draw.ViewportX), std::bit_cast<uint32_t>(draw.ViewportY),
            std::bit_cast<uint32_t>(draw.ViewportWidth), std::bit_cast<uint32_t>(draw.ViewportHeight),
            std::bit_cast<uint32_t>(draw.DepthRange), std::bit_cast<uint32_t>(draw.NearPlane),
            draw.WBuffering, draw.FramebufferFlipped};
        for (const auto& binding : draw.VertexBindings) {
            result.Layout.insert(result.Layout.end(),
                {binding.Binding, binding.ByteStride, binding.PerInstance, binding.Bytes.size()});
            result.Inputs.emplace_back(binding.Bytes.begin(), binding.Bytes.end());
        }
        for (const auto& attribute : draw.VertexAttributes)
            result.Layout.insert(result.Layout.end(), {attribute.Location, attribute.Binding,
                uint64_t(attribute.Format), attribute.ComponentCount, attribute.ByteOffset});
        result.Indices.assign(draw.IndexBytes.begin(), draw.IndexBytes.end());
        return result;
    }
};

struct PicaTemporalHistoryEntry {
    PicaTemporalVertexState Vertex;
    std::vector<uint8_t> Bytes;
    std::array<float, 2> JitterNdc{};
};

struct PicaTemporalMatch {
    PicaTemporalMatchReason Reason = PicaTemporalMatchReason::MissingIdentity;
    const PicaTemporalHistoryEntry* Previous = nullptr;
    [[nodiscard]] bool Exact() const { return Reason == PicaTemporalMatchReason::Exact; }
};

class PicaTemporalHistory {
  public:
    // A presentation sequence declares the COMPLETE draw set for each target.
    // A second presentation sequence for that target in the same host frame is
    // refused before drawing (a streaming duplicate check cannot retract an
    // already rasterized first match): none of its draws is published, so they
    // all draw without history. Native-memory replays do not participate.
    [[nodiscard]] bool PrepareSequence(const PicaCompositionSequenceView& sequence, uint64_t frame,
                                       const PicaFrameTemporalSample* sample = nullptr) {
        mCounts.clear();
        mSubmissions.clear();
        mConsumed.clear();
        // Dependency replays may occur on host refreshes with no presentation.
        // They must neither advance nor seed presented object history.
        if (sequence.Purpose != PicaCompositionPurpose::Presentation) return true;
        if (frame != mFrame) {
            const bool consecutiveSource = frame > mFrame && sample != nullptr &&
                ConsecutiveAuthoritativeSources(mLastPresentationSample, *sample);
            mPrevious = frame == mFrame + 1U || consecutiveSource
                ? std::move(mCurrent) : decltype(mCurrent){};
            mCurrent.clear();
            mPublishedViews.clear();
            mFrame = frame;
            mLastPresentationSample = sample != nullptr ? *sample : PicaFrameTemporalSample{};
            Reasons = {};
        }
        std::set<PicaCompositionTargetReference> views;
        for (const auto& draw : sequence.Draws) views.insert(draw.Target);
        for (const auto& view : views)
            if (mPublishedViews.contains(view)) { ++DuplicateTargetSequences; return false; }
        mPublishedViews.insert(views.begin(), views.end());
        for (const auto& draw : sequence.Draws) {
            const PicaTemporalHistoryKey key{draw.Target, draw.Composition.TemporalIdentity, 0};
            ++mCounts[key];
            mSubmissions.emplace(draw.SubmissionId, key);
        }
        return true;
    }

    PicaTemporalMatch Track(const PicaDrawView& draw, uint64_t frame,
                           const PicaFrameTemporalSample& sample,
                           const std::array<float, 2>& jitterNdc) {
        const auto finish = [this](PicaTemporalMatchReason reason,
                                   const PicaTemporalHistoryEntry* previous = nullptr) {
            ++Reasons[static_cast<size_t>(reason)];
            return PicaTemporalMatch{reason, previous};
        };
        const auto reference = BuildPicaCompositionDrawReference(draw);
        PicaTemporalHistoryKey key{reference.Target, draw.Composition.TemporalIdentity, 0};
        if (!key.Instance.Available()) return finish(PicaTemporalMatchReason::MissingIdentity);
        const auto published = mSubmissions.find(draw.SubmissionId);
        if (frame != mFrame || published == mSubmissions.end() || published->second != key)
            return finish(PicaTemporalMatchReason::Unpublished);
        if (mCounts.at(key) != 1U) return finish(PicaTemporalMatchReason::Ambiguous);
        if (!mConsumed.insert(draw.SubmissionId).second)
            return finish(PicaTemporalMatchReason::DuplicateSubmission);
        if (!sample.Available()) return finish(PicaTemporalMatchReason::InvalidSample);
        if (draw.CanonicalVertexProgramId == 0 && draw.VertexShaderKey == 0)
            return finish(PicaTemporalMatchReason::IncompatibleProgramOrLayout);
        key.Epoch = sample.ContinuityEpoch;
        PicaTemporalHistoryEntry next{PicaTemporalVertexState::Capture(draw),
            {draw.VertexUniformBytes.begin(), draw.VertexUniformBytes.end()}, jitterNdc};
        const auto prior = mPrevious.find(key);
        auto reason = PicaTemporalMatchReason::MissingPrevious;
        const PicaTemporalHistoryEntry* previous = nullptr;
        if (sample.HistoryReset) reason = PicaTemporalMatchReason::Reset;
        else if (prior != mPrevious.end()) {
            const auto& old = prior->second;
            if (old.Vertex.Program != next.Vertex.Program || old.Vertex.Shader != next.Vertex.Shader ||
                old.Vertex.Layout != next.Vertex.Layout)
                reason = PicaTemporalMatchReason::IncompatibleProgramOrLayout;
            else if (old.Vertex.Inputs != next.Vertex.Inputs || old.Vertex.Indices != next.Vertex.Indices)
                reason = PicaTemporalMatchReason::ChangedInputs;
            else if (old.Bytes.size() != next.Bytes.size() || next.Bytes.empty())
                reason = PicaTemporalMatchReason::UniformSize;
            else {
                reason = PicaTemporalMatchReason::Exact;
                previous = &old;
            }
        }
        mCurrent.insert_or_assign(key, std::move(next));
        return finish(reason, previous);
    }

    void Reset() { *this = {}; }
    std::array<uint64_t, static_cast<size_t>(PicaTemporalMatchReason::Count)> Reasons{};
    uint64_t DuplicateTargetSequences = 0;

  private:
    // A host-frame gap alone is not continuity evidence. Only explicit,
    // adjacent authoritative sources in the same epoch/rate may bridge it.
    // Interpolated gaps, skipped sources and unpublished/invalid samples keep
    // the conservative rejection; a published cull still clears the object.
    static bool ConsecutiveAuthoritativeSources(const PicaFrameTemporalSample& old,
                                                const PicaFrameTemporalSample& next) {
        const auto authoritative = [](const PicaFrameTemporalSample& value) {
            return value.Available() && !value.HistoryReset && !value.Synthetic &&
                value.Interpolation == PicaVisualInterpolationMode::Disabled &&
                value.Kind == PicaFrameTemporalSampleKind::Authoritative &&
                value.PreviousSourceFrameId == value.CurrentSourceFrameId;
        };
        return authoritative(old) && authoritative(next) &&
            old.ContinuityEpoch == next.ContinuityEpoch &&
            old.NativeRateHz == next.NativeRateHz &&
            old.TargetPresentationRateHz == next.TargetPresentationRateHz &&
            next.CurrentSourceFrameId > old.CurrentSourceFrameId &&
            next.CurrentSourceFrameId - old.CurrentSourceFrameId == 1U;
    }
    uint64_t mFrame = 0;
    PicaFrameTemporalSample mLastPresentationSample;
    std::map<PicaTemporalHistoryKey, PicaTemporalHistoryEntry> mPrevious, mCurrent;
    std::map<PicaTemporalHistoryKey, size_t> mCounts;
    std::map<uint64_t, PicaTemporalHistoryKey> mSubmissions;
    std::set<uint64_t> mConsumed;
    std::set<PicaCompositionTargetReference> mPublishedViews;
};

} // namespace Fast::Renderer3ds
