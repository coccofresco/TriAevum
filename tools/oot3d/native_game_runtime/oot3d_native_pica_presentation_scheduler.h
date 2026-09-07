#pragma once

#include "oot3d_native_pica_visual_frame.h"
#include "oot3d/renderer/pica_render_backend.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Oot3dNativeGame {

enum class Oot3dPicaPresentationExecutionKind : uint8_t {
  CurrentFrame,
  InterpolatedFrame,
  RepeatedFrame,
  DependencyFlush,
};

struct Oot3dPicaPresentationSchedulerStats {
  uint64_t ProducedGuestDraws = 0;
  uint64_t CapturedMemoryFills = 0;
  uint64_t CapturedDisplayTransfers = 0;
  uint64_t CompletedVisualFrames = 0;
  uint64_t SelectedDraws = 0;
  uint64_t ExecutedDraws = 0;
  uint64_t InterpolatedDraws = 0;
  uint64_t ExecutionLists = 0;
  uint64_t CurrentFrameLists = 0;
  uint64_t InterpolatedFrameLists = 0;
  uint64_t RepeatedFrameLists = 0;
  uint64_t DependencyFlushes = 0;
  uint64_t MemoryFillsExecuted = 0;
  uint64_t DisplaySnapshotsExecuted = 0;
  uint64_t PresentedTransfers = 0;
  uint64_t ReusedSnapshots = 0;
  uint64_t DuplicateDrawAttempts = 0;
  uint64_t RedundantTopSnapshotCopiesAvoided = 0;
  uint64_t CompositionSequencesPublished = 0;
  uint64_t CompositionDrawReferencesPublished = 0;
  uint64_t CompositionPublicationFailures = 0;
  uint64_t Resets = 0;
};

struct Oot3dPicaPresentationSchedulerTimings {
  bool Enabled = false;
  double ExecuteSeconds = 0.0;
  double ValidationSeconds = 0.0;
  double CompositionPublicationSeconds = 0.0;
  double DrawSubmissionSeconds = 0.0;
  double DrawUniformPackingSeconds = 0.0;
  double DrawViewConstructionSeconds = 0.0;
  double DrawBackendSubmissionSeconds = 0.0;
  double MemoryFillSeconds = 0.0;
  double SnapshotTransferSeconds = 0.0;
  double PresentTransferSeconds = 0.0;
  double PresentExistingSeconds = 0.0;
};

struct Oot3dPicaPresentationSchedulerState {
  Oot3dPicaVisualFrameAccumulatorState Accumulator;
  std::map<std::pair<uint64_t, uint32_t>, uint64_t> SnapshotCompletions;
};

// Owns deferred PICA work between guest refreshes and host presentations.
// A draw plan is captured once, then executed by exactly one selected list
// during a host presentation. Guest-visible completion signaling remains the
// caller's responsibility because it belongs to the CTR service contract.
class Oot3dPicaPresentationScheduler final {
public:
  void Capture(Oot3dPicaVulkanDrawPlan plan);
  void Capture(Oot3dPicaMemoryFillSubmission fill);
  void Capture(Oot3dPicaDisplayTransferSubmission transfer);

  std::optional<Oot3dPicaVisualFrame>
  FinishFrame(const Oot3dPicaDisplayTransferSubmission &topTransfer);

  void BeginPresentation(uint64_t presentationSerial);

  bool Execute(Oot3d::Renderer::PicaRenderBackend &backend,
               const Oot3dPicaVisualFrameViewSample &sample,
               uint64_t renderTargetNamespace,
               Oot3dPicaPresentationExecutionKind kind, bool present,
               std::string *error = nullptr);

  bool PresentExisting(Oot3d::Renderer::PicaRenderBackend &backend,
                       const Oot3dPicaDisplayTransferSubmission &transfer,
                       uint64_t renderTargetNamespace,
                       std::string *error = nullptr);

  bool HasSnapshot(const Oot3dPicaDisplayTransferSubmission &transfer,
                   uint64_t renderTargetNamespace) const;

  void SetTimingEnabled(bool enabled) noexcept;
  void Reset();
  Oot3dPicaPresentationSchedulerState CaptureState() const;
  bool RestoreState(Oot3dPicaPresentationSchedulerState state);
  size_t PendingDrawCount() const;
  const Oot3dPicaPresentationSchedulerStats &Stats() const noexcept;
  const Oot3dPicaPresentationSchedulerTimings &Timings() const noexcept;

private:
  using SnapshotTarget = std::pair<uint64_t, uint32_t>;

  static SnapshotTarget
  MakeSnapshotTarget(const Oot3dPicaDisplayTransferSubmission &transfer,
                     uint64_t renderTargetNamespace);

  Oot3dPicaVisualFrameAccumulator mAccumulator;
  Oot3dPicaPresentationSchedulerStats mStats;
  Oot3dPicaPresentationSchedulerTimings mTimings;
  std::optional<uint64_t> mPresentationSerial;
  uint64_t mNextCompositionSequenceId = 1U;
  std::unordered_set<uint64_t> mExecutedDrawsThisPresentation;
  std::vector<Oot3d::Renderer::PicaCompositionDrawReference>
      mCompositionDrawScratch;
  std::map<SnapshotTarget, uint64_t> mSnapshotCompletions;
};

} // namespace Oot3dNativeGame
