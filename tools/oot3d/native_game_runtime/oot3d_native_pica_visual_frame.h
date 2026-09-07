#pragma once

#include "oot3d_native_pica_vulkan_plan.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Oot3dNativeGame {

struct Oot3dPicaVisualFrame {
    uint64_t Sequence = 0;
    Oot3dPicaDisplayTransferSubmission TopTransfer;
    std::vector<Oot3dPicaVulkanDrawPlan> Draws;
    std::vector<uint64_t> StrictDrawIdentities;
    std::vector<Oot3dPicaMemoryFillSubmission> MemoryFills;
    std::vector<Oot3dPicaDisplayTransferSubmission> DisplayTransfers;
};

struct Oot3dPicaVisualFrameSample {
    Oot3dPicaDisplayTransferSubmission TopTransfer;
    std::vector<Oot3dPicaVulkanDrawPlan> Draws;
    std::vector<Oot3dPicaMemoryFillSubmission> MemoryFills;
    std::vector<Oot3dPicaDisplayTransferSubmission> DisplayTransfers;
    size_t MatchedDraws = 0;
    size_t StrictUniqueMatchedDraws = 0;
    size_t StrictOrdinalMatchedDraws = 0;
    size_t StructuralOrdinalMatchedDraws = 0;
    size_t PipelineOrdinalMatchedDraws = 0;
    size_t InterpolatedDraws = 0;
    size_t InterpolatedPerInstanceVertexDraws = 0;
    size_t InterpolatedPerVertexDraws = 0;
};

struct Oot3dPicaVisualDrawView {
    const Oot3dPicaVulkanDrawPlan* BasePlan = nullptr;
    std::optional<Oot3dPicaVertexUniformState> VertexUniforms;
    std::optional<Oot3dPicaFragmentUniformState> FragmentUniforms;
    std::optional<Oot3dPicaViewportState> Viewport;
    std::optional<std::array<float, 4>> BlendConstantColor;
    std::vector<Oot3dPicaVulkanVertexBinding> VertexBindings;
    bool HasVertexBindingOverrides = false;

    Oot3dPicaVulkanDrawOverrides Overrides() const;
};

struct Oot3dPicaVisualFrameViewSample {
    Oot3dPicaDisplayTransferSubmission TopTransfer;
    std::vector<Oot3dPicaVisualDrawView> Draws;
    const std::vector<Oot3dPicaMemoryFillSubmission>* MemoryFills = nullptr;
    const std::vector<Oot3dPicaDisplayTransferSubmission>* DisplayTransfers = nullptr;
    size_t MatchedDraws = 0;
    size_t StrictUniqueMatchedDraws = 0;
    size_t StrictOrdinalMatchedDraws = 0;
    size_t StructuralOrdinalMatchedDraws = 0;
    size_t PipelineOrdinalMatchedDraws = 0;
    size_t InterpolatedDraws = 0;
    size_t InterpolatedPerInstanceVertexDraws = 0;
    size_t InterpolatedPerVertexDraws = 0;
};

struct Oot3dPicaVisualFrameAccumulatorState {
    uint64_t NextSequence = 1;
    std::vector<Oot3dPicaVulkanDrawPlan> PendingDraws;
    std::vector<Oot3dPicaMemoryFillSubmission> PendingMemoryFills;
    std::vector<Oot3dPicaDisplayTransferSubmission> PendingDisplayTransfers;
};

struct Oot3dPicaVisualContinuityTrackerState {
    std::vector<double> AcceptedDeltas;
};

struct Oot3dPicaVisualTransitionStats {
    size_t PreviousDraws = 0;
    size_t CurrentDraws = 0;
    size_t MatchedDraws = 0;
    size_t StrictUniqueMatchedDraws = 0;
    size_t StrictOrdinalMatchedDraws = 0;
    size_t StructuralOrdinalMatchedDraws = 0;
    size_t PipelineOrdinalMatchedDraws = 0;
    size_t ChangedContinuousDraws = 0;
    size_t ChangedPerInstanceVertexDraws = 0;
    size_t ChangedPerVertexDraws = 0;
    size_t AmbiguousPreviousDraws = 0;
    size_t AmbiguousCurrentDraws = 0;
    size_t UnmatchedPreviousDraws = 0;
    size_t UnmatchedCurrentDraws = 0;
    size_t TopTargetMatchedDraws = 0;
    double MatchedDrawCoverage = 0.0;
    double MedianNormalizedContinuousDelta = 0.0;
    double P90NormalizedContinuousDelta = 0.0;
    size_t CameraMatchedDraws = 0;
    size_t CameraDiscontinuousDraws = 0;
};

struct Oot3dPicaVisualInterpolationTiming {
    uint64_t MatchingNanoseconds = 0;
    uint64_t AnalysisNanoseconds = 0;
    uint64_t SamplingNanoseconds = 0;
};

uint64_t
ComputeOot3dPicaVisualDrawIdentity(const Oot3dPicaVulkanDrawPlan& plan);

bool AreOot3dPicaVisualDrawsCompatible(const Oot3dPicaVulkanDrawPlan& previous,
                                       const Oot3dPicaVulkanDrawPlan& current);

bool InterpolateOot3dPicaVisualDraw(const Oot3dPicaVulkanDrawPlan& previous,
                                    const Oot3dPicaVulkanDrawPlan& current,
                                    float alpha,
                                    Oot3dPicaVulkanDrawPlan& output);

Oot3dPicaVisualTransitionStats
AnalyzeOot3dPicaVisualTransition(const Oot3dPicaVisualFrame& previous,
                                 const Oot3dPicaVisualFrame& current);

bool SampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous,
                                const Oot3dPicaVisualFrame& current,
                                float alpha,
                                Oot3dPicaVisualFrameSample& output);

bool AnalyzeAndSampleOot3dPicaVisualFrame(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                          float alpha, Oot3dPicaVisualTransitionStats& stats,
                                          Oot3dPicaVisualFrameSample& output);

bool AnalyzeAndSampleOot3dPicaVisualFrameView(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                                              float alpha, Oot3dPicaVisualTransitionStats& stats,
                                              Oot3dPicaVisualFrameViewSample& output,
                                              Oot3dPicaVisualInterpolationTiming* timing = nullptr);

bool ViewOot3dPicaVisualFrame(
    const Oot3dPicaVisualFrame& frame,
    Oot3dPicaVisualFrameViewSample& output);

// Draw matching and transition analysis are immutable for a pair of source
// frames. Prepare once, then sample at 1/2 for 2x or at 1/3 and 2/3 for 3x
// without repeating the matching work on every host presentation.
class Oot3dPicaPreparedVisualTransition final {
  public:
    Oot3dPicaPreparedVisualTransition();
    ~Oot3dPicaPreparedVisualTransition();
    Oot3dPicaPreparedVisualTransition(
        Oot3dPicaPreparedVisualTransition&&) noexcept;
    Oot3dPicaPreparedVisualTransition& operator=(
        Oot3dPicaPreparedVisualTransition&&) noexcept;
    Oot3dPicaPreparedVisualTransition(
        const Oot3dPicaPreparedVisualTransition&) = delete;
    Oot3dPicaPreparedVisualTransition& operator=(
        const Oot3dPicaPreparedVisualTransition&) = delete;

    bool Prepare(const Oot3dPicaVisualFrame& previous,
                 const Oot3dPicaVisualFrame& current,
                 Oot3dPicaVisualInterpolationTiming* timing = nullptr);
    bool Sample(const Oot3dPicaVisualFrame& previous,
                const Oot3dPicaVisualFrame& current, float alpha,
                Oot3dPicaVisualFrameViewSample& output,
                Oot3dPicaVisualInterpolationTiming* timing = nullptr) const;
    void Reset() noexcept;
    [[nodiscard]] bool Ready() const noexcept;
    [[nodiscard]] const Oot3dPicaVisualTransitionStats& Stats() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

class Oot3dPicaVisualFrameAccumulator final {
  public:
    void Append(Oot3dPicaVulkanDrawPlan plan);
    void Append(Oot3dPicaMemoryFillSubmission fill);
    void Append(Oot3dPicaDisplayTransferSubmission transfer);
    std::optional<Oot3dPicaVisualFrame>
    Finish(const Oot3dPicaDisplayTransferSubmission& topTransfer);
    size_t PendingDrawCount() const;
    Oot3dPicaVisualFrameAccumulatorState CaptureState() const;
    bool RestoreState(Oot3dPicaVisualFrameAccumulatorState state);

  private:
    uint64_t mNextSequence = 1;
    std::vector<Oot3dPicaVulkanDrawPlan> mPendingDraws;
    std::vector<Oot3dPicaMemoryFillSubmission> mPendingMemoryFills;
    std::vector<Oot3dPicaDisplayTransferSubmission> mPendingDisplayTransfers;
};

class Oot3dPicaVisualContinuityTracker final {
  public:
    bool Accept(const Oot3dPicaVisualTransitionStats& transition);
    double CurrentThreshold() const;
    Oot3dPicaVisualContinuityTrackerState CaptureState() const;
    bool RestoreState(Oot3dPicaVisualContinuityTrackerState state);

  private:
    std::vector<double> mAcceptedDeltas;
};

} // namespace Oot3dNativeGame
