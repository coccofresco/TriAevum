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
  NativeFrame,
};

struct Oot3dPicaPresentationSchedulerStats {
  uint64_t ProducedGuestDraws = 0;
  uint64_t CapturedMemoryFills = 0;
  uint64_t CapturedDisplayTransfers = 0;
  uint64_t CompletedVisualFrames = 0;
  // Guest frames completed before the selected one within one host frame:
  // executed for the canonical target and kept as interpolation history,
  // never presented.
  uint64_t SupersededVisualFrames = 0;
  uint64_t SelectedDraws = 0;
  uint64_t ExecutedDraws = 0;
  // Draws handed to the backend before their presentation sequence executed.
  uint64_t PreparedDraws = 0;
  uint64_t InterpolatedDraws = 0;
  uint64_t ExecutionLists = 0;
  uint64_t CurrentFrameLists = 0;
  uint64_t InterpolatedFrameLists = 0;
  uint64_t RepeatedFrameLists = 0;
  uint64_t DependencyFlushes = 0;
  uint64_t NativeFrameLists = 0;
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
  double DrawPreparationSeconds = 0.0;
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
// A draw plan is captured once. Native dependency execution and visual samples
// use distinct target namespaces; each executes a draw at most once per host
// presentation. Raw-copy completion belongs only to the native dependency.
class Oot3dPicaPresentationScheduler final {
public:
  void Capture(Oot3dPicaVulkanDrawPlan plan);
  void Capture(Oot3dPicaMemoryFillSubmission fill);
  void Capture(Oot3dPicaDisplayTransferSubmission transfer);

  std::optional<Oot3dPicaVisualFrame>
  FinishFrame(const Oot3dPicaDisplayTransferSubmission &topTransfer);
  // One visual frame per guest frame completed since the last call, oldest
  // first (see Oot3dPicaVisualFrameAccumulator::FinishFrames).
  std::vector<Oot3dPicaVisualFrame>
  FinishFrames(const Oot3dPicaDisplayTransferSubmission &topTransfer,
               std::span<const uint32_t> displayOutputs,
               std::span<const Oot3dPicaViewportState> continuationViewports = {});
  // GPU completion may precede scanout. Retain the native prefix in the complete
  // frame rather than consuming the only interpolation source.
  bool ViewDependencyWork(Oot3dPicaVisualFrameViewSample& output);

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
  // Live plans not yet published as a frame: their copy references still pin
  // the GPU copy versions they sampled.
  [[nodiscard]] std::span<const Oot3dPicaVulkanDrawPlan> PendingDraws()
      const noexcept {
    return mAccumulator.PendingDraws();
  }
  // True while an ordered raw TextureCopy still owes the guest a PPF
  // completion and therefore needs a dependency flush.
  bool HasPendingRawCopyCompletion() const;
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
  std::map<uint64_t, std::unordered_set<uint64_t>>
      mExecutedDrawsThisPresentation;
  std::vector<Oot3d::Renderer::PicaCompositionDrawReference>
      mCompositionDrawScratch;
  std::vector<Oot3d::Renderer::PicaCompositionTargetReset>
      mCompositionResetScratch;
  std::map<SnapshotTarget, uint64_t> mSnapshotCompletions;
};

} // namespace Oot3dNativeGame
