#include "oot3d_native_pica_presentation_scheduler.h"

#include "oot3d_native_pica_vulkan_bridge.h"

#include <chrono>
#include <string_view>
#include <utility>

namespace Oot3dNativeGame {
namespace {

void SetError(std::string *error, std::string_view message) {
  if (error != nullptr) {
    *error = message;
  }
}

bool SameTransfer(const Oot3dPicaDisplayTransferSubmission &left,
                  const Oot3dPicaDisplayTransferSubmission &right) {
  return left.CompletionId == right.CompletionId &&
         left.InputPhysicalAddress == right.InputPhysicalAddress &&
         left.OutputPhysicalAddress == right.OutputPhysicalAddress;
}

class OptionalTimer final {
public:
  OptionalTimer(bool enabled, double &destination)
      : mEnabled(enabled), mDestination(destination),
        mStarted(enabled ? std::chrono::steady_clock::now()
                         : std::chrono::steady_clock::time_point{}) {
  }

  ~OptionalTimer() {
    if (mEnabled) {
      mDestination +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        mStarted)
              .count();
    }
  }

private:
  bool mEnabled = false;
  double &mDestination;
  std::chrono::steady_clock::time_point mStarted;
};

} // namespace

void Oot3dPicaPresentationScheduler::Capture(Oot3dPicaVulkanDrawPlan plan) {
  ++mStats.ProducedGuestDraws;
  mAccumulator.Append(std::move(plan));
}

void Oot3dPicaPresentationScheduler::Capture(
    Oot3dPicaMemoryFillSubmission fill) {
  ++mStats.CapturedMemoryFills;
  mAccumulator.Append(std::move(fill));
}

void Oot3dPicaPresentationScheduler::Capture(
    Oot3dPicaDisplayTransferSubmission transfer) {
  ++mStats.CapturedDisplayTransfers;
  mAccumulator.Append(std::move(transfer));
}

std::optional<Oot3dPicaVisualFrame> Oot3dPicaPresentationScheduler::FinishFrame(
    const Oot3dPicaDisplayTransferSubmission &topTransfer) {
  auto frame = mAccumulator.Finish(topTransfer);
  mStats.CompletedVisualFrames += frame.has_value() ? 1U : 0U;
  return frame;
}

std::vector<Oot3dPicaVisualFrame> Oot3dPicaPresentationScheduler::FinishFrames(
    const Oot3dPicaDisplayTransferSubmission &topTransfer,
    std::span<const uint32_t> displayOutputs,
    std::span<const Oot3dPicaViewportState> continuationViewports) {
  auto frames = mAccumulator.FinishFrames(topTransfer, displayOutputs,
                                          continuationViewports);
  mStats.CompletedVisualFrames += frames.size();
  mStats.SupersededVisualFrames += frames.empty() ? 0U : frames.size() - 1U;
  return frames;
}

void Oot3dPicaPresentationScheduler::BeginPresentation(
    uint64_t presentationSerial) {
  if (mPresentationSerial == presentationSerial) {
    return;
  }
  mPresentationSerial = presentationSerial;
  for (auto& [targetNamespace, draws] : mExecutedDrawsThisPresentation) {
    draws.clear();
  }
}

bool Oot3dPicaPresentationScheduler::ViewDependencyWork(
    Oot3dPicaVisualFrameViewSample& output) {
  return mAccumulator.ViewDependencyWork(output);
}

Oot3dPicaPresentationSchedulerState
Oot3dPicaPresentationScheduler::CaptureState() const {
  return {mAccumulator.CaptureState(), mSnapshotCompletions};
}

bool Oot3dPicaPresentationScheduler::RestoreState(
    Oot3dPicaPresentationSchedulerState state) {
  if (!mAccumulator.RestoreState(std::move(state.Accumulator))) {
    return false;
  }
  mSnapshotCompletions = std::move(state.SnapshotCompletions);
  mPresentationSerial.reset();
  mExecutedDrawsThisPresentation.clear();
  return true;
}

bool Oot3dPicaPresentationScheduler::Execute(
    Oot3d::Renderer::PicaRenderBackend &backend,
    const Oot3dPicaVisualFrameViewSample &sample,
    uint64_t renderTargetNamespace, Oot3dPicaPresentationExecutionKind kind,
    bool present, std::string *error) {
  OptionalTimer executeTimer(mTimings.Enabled, mTimings.ExecuteSeconds);
  if (!mPresentationSerial.has_value()) {
    SetError(error, "PICA presentation execution began without a host frame");
    return false;
  }
  if (sample.Draws.empty() && sample.DisplayTransfers.empty() &&
      kind == Oot3dPicaPresentationExecutionKind::DependencyFlush) {
    SetError(error, "PICA presentation sample is incomplete");
    return false;
  }

  {
    OptionalTimer validationTimer(mTimings.Enabled,
                                  mTimings.ValidationSeconds);
    std::unordered_set<uint64_t> sampleDraws;
    sampleDraws.reserve(sample.Draws.size());
    auto& executedDraws =
        mExecutedDrawsThisPresentation[renderTargetNamespace];
    for (const auto &draw : sample.Draws) {
      if (draw.BasePlan == nullptr ||
          !sampleDraws.insert(draw.BasePlan->SubmissionId).second ||
          executedDraws.contains(draw.BasePlan->SubmissionId)) {
        ++mStats.DuplicateDrawAttempts;
        SetError(error,
                 "PICA draw would execute more than once in one presentation");
        return false;
      }
    }
    executedDraws.insert(sampleDraws.begin(), sampleDraws.end());
  }
  mStats.SelectedDraws += sample.Draws.size();

  mCompositionDrawScratch.clear();
  mCompositionDrawScratch.reserve(sample.Draws.size());
  for (const auto &drawView : sample.Draws) {
    const auto &draw = *drawView.BasePlan;
    const auto &framebuffer = draw.State.Framebuffer;
    mCompositionDrawScratch.push_back({
        .SubmissionId = draw.SubmissionId,
        .Target = {
            .RenderTargetNamespace = renderTargetNamespace,
            .ColorPhysicalAddress = framebuffer.ColorPhysicalAddress,
            .DepthPhysicalAddress = framebuffer.DepthPhysicalAddress,
            .FramebufferWidth = framebuffer.Width,
            .FramebufferHeight = framebuffer.Height,
            .ColorFormat = framebuffer.ColorFormat,
            .DepthFormat = framebuffer.DepthFormat,
        },
        .Domain = static_cast<Oot3d::Renderer::PicaCompositionDomain>(
            static_cast<uint8_t>(draw.CompositionDomain)),
        .Composition = {
            .Layer = static_cast<Oot3d::Renderer::PicaCompositionLayer>(
                static_cast<uint8_t>(draw.Composition.Layer)),
            .Provenance =
                static_cast<Oot3d::Renderer::PicaCompositionProvenance>(
                    static_cast<uint8_t>(draw.Composition.Provenance)),
            .SourcePc = draw.Composition.SourcePc,
            .NativeValue = draw.Composition.NativeValue,
            .NativeProjectedShadow = draw.Composition.NativeProjectedShadow,
            .NativeActorGeometry = draw.Composition.NativeActorGeometry,
            .NativeActorIdentity = draw.Composition.NativeActorIdentity,
            .IsolatedLighting = draw.Composition.IsolatedLighting,
            .TemporalIdentity = draw.Composition.TemporalIdentity,
        },
    });
  }
  // A memory fill over a draw target's whole color buffer clears it: the
  // target's composition starts again at the draw after the fill.
  mCompositionResetScratch.clear();
  for (const auto &fill : sample.MemoryFills) {
    for (const auto &draw : mCompositionDrawScratch) {
      const auto &target = draw.Target;
      const uint32_t bytesPerPixel = target.ColorFormat == 0U ? 4U
                                     : target.ColorFormat == 1U ? 3U
                                                                : 2U;
      const uint64_t colorBytes = uint64_t{target.FramebufferWidth} *
                                  target.FramebufferHeight * bytesPerPixel;
      if (fill.StartPhysicalAddress != target.ColorPhysicalAddress ||
          uint64_t{fill.EndPhysicalAddress} - fill.StartPhysicalAddress < colorBytes)
        continue;
      const bool known = std::any_of(
          mCompositionResetScratch.begin(), mCompositionResetScratch.end(),
          [&](const auto &reset) {
            return reset.Target == target &&
                   reset.BeforeSubmissionId == fill.BeforeDrawSubmissionId;
          });
      if (!known)
        mCompositionResetScratch.push_back({target, fill.BeforeDrawSubmissionId});
    }
  }
  uint64_t compositionSequenceId = mNextCompositionSequenceId++;
  if (mNextCompositionSequenceId == 0U) {
    mNextCompositionSequenceId = 1U;
  }
  const bool presentation =
      kind != Oot3dPicaPresentationExecutionKind::DependencyFlush &&
      kind != Oot3dPicaPresentationExecutionKind::NativeFrame;
  {
    OptionalTimer timer(mTimings.Enabled,
                        mTimings.CompositionPublicationSeconds);
    if (!backend.PublishPicaCompositionSequence(
            {
                .SchemaVersion = Oot3d::Renderer::
                    kPicaCompositionSequenceSchemaVersion,
                .SequenceId = compositionSequenceId,
                .Draws = mCompositionDrawScratch,
                .Purpose =
                    presentation
                        ? Oot3d::Renderer::PicaCompositionPurpose::Presentation
                        : Oot3d::Renderer::PicaCompositionPurpose::NativeMemory,
                .TargetResets = mCompositionResetScratch,
            },
            error)) {
      ++mStats.CompositionPublicationFailures;
      return false;
    }
  }
  ++mStats.CompositionSequencesPublished;
  mStats.CompositionDrawReferencesPublished +=
      mCompositionDrawScratch.size();

  // Presentation work the backend needs ahead of native order (this sample's
  // shadow casters before their receivers) receives the draws it selects
  // first; every draw is still submitted below, in native order, exactly once.
  // Native-memory execution keeps its canonical sequence untouched.
  if (presentation) {
    OptionalTimer timer(mTimings.Enabled, mTimings.DrawPreparationSeconds);
    for (size_t index = 0; index < sample.Draws.size(); ++index) {
      if (!backend.WantsPicaSequenceDrawPreparation(
              mCompositionDrawScratch[index])) {
        continue;
      }
      const auto &drawView = sample.Draws[index];
      const auto overrides = drawView.Overrides();
      if (!PrepareOot3dPicaVulkanSequenceDraw(backend, *drawView.BasePlan,
                                              error, renderTargetNamespace,
                                              &overrides)) {
        return false;
      }
      ++mStats.PreparedDraws;
    }
  }

  ++mStats.ExecutionLists;
  switch (kind) {
  case Oot3dPicaPresentationExecutionKind::CurrentFrame:
    ++mStats.CurrentFrameLists;
    break;
  case Oot3dPicaPresentationExecutionKind::InterpolatedFrame:
    ++mStats.InterpolatedFrameLists;
    break;
  case Oot3dPicaPresentationExecutionKind::RepeatedFrame:
    ++mStats.RepeatedFrameLists;
    break;
  case Oot3dPicaPresentationExecutionKind::NativeFrame:
    ++mStats.NativeFrameLists;
    break;
  case Oot3dPicaPresentationExecutionKind::DependencyFlush:
    ++mStats.DependencyFlushes;
    break;
  }
  mStats.InterpolatedDraws += sample.InterpolatedDraws;

  size_t fillIndex = 0;
  size_t transferIndex = 0;
  bool topSnapshotExecuted = false;
  const auto fills = sample.MemoryFills;
  const auto transfers = sample.DisplayTransfers;
  const auto submitFill = [&](const Oot3dPicaMemoryFillSubmission &fill) {
    OptionalTimer timer(mTimings.Enabled, mTimings.MemoryFillSeconds);
    if (!SubmitOot3dPicaVulkanMemoryFill(backend, fill, error,
                                         renderTargetNamespace)) {
      return false;
    }
    ++mStats.MemoryFillsExecuted;
    return true;
  };
  const auto submitTransfer = [&](const Oot3dPicaDisplayTransferSubmission
                                      &transfer) {
    OptionalTimer timer(mTimings.Enabled, mTimings.SnapshotTransferSeconds);
    // Real execution publishes byte visibility and discharges PPF, including
    // guarded deferred RAM. Replayed/interpolated/restored transfers recreate
    // only the GPU version; they never publish a second guest write.
    const bool materializeGuest =
        kind == Oot3dPicaPresentationExecutionKind::DependencyFlush &&
        transfer.Transfer.TextureCopyBytes != 0U && transfer.SignalInterrupt;
    if (!SubmitOot3dPicaVulkanDisplayTransfer(backend, transfer, false, error,
                                              renderTargetNamespace,
                                              materializeGuest)) {
      return false;
    }
    mSnapshotCompletions[MakeSnapshotTarget(transfer, renderTargetNamespace)] =
        transfer.CompletionId;
    ++mStats.DisplaySnapshotsExecuted;
    topSnapshotExecuted =
        topSnapshotExecuted || SameTransfer(transfer, sample.TopTransfer);
    return true;
  };

  for (const auto &drawView : sample.Draws) {
    const auto &draw = *drawView.BasePlan;
    while (transferIndex < transfers.size() &&
           transfers[transferIndex].AfterDrawSubmissionId < draw.SubmissionId) {
      if (!submitTransfer(transfers[transferIndex++])) {
        return false;
      }
    }
    while (fillIndex < fills.size() &&
           fills[fillIndex].BeforeDrawSubmissionId <= draw.SubmissionId) {
      if (!submitFill(fills[fillIndex++])) {
        return false;
      }
    }
    const auto overrides = drawView.Overrides();
    {
      OptionalTimer timer(mTimings.Enabled, mTimings.DrawSubmissionSeconds);
      Oot3dPicaVulkanDrawBridgeTiming bridgeTiming;
      const bool submitted = SubmitOot3dPicaVulkanDrawPlan(
          backend, draw, error, renderTargetNamespace, &overrides,
          mTimings.Enabled ? &bridgeTiming : nullptr);
      if (mTimings.Enabled) {
        mTimings.DrawUniformPackingSeconds +=
            bridgeTiming.UniformPackingSeconds;
        mTimings.DrawViewConstructionSeconds +=
            bridgeTiming.ViewConstructionSeconds;
        mTimings.DrawBackendSubmissionSeconds +=
            bridgeTiming.BackendSubmissionSeconds;
      }
      if (!submitted) {
        return false;
      }
    }
    ++mStats.ExecutedDraws;
    while (transferIndex < transfers.size() &&
           transfers[transferIndex].AfterDrawSubmissionId ==
               draw.SubmissionId) {
      if (!submitTransfer(transfers[transferIndex++])) {
        return false;
      }
    }
  }
  while (fillIndex < fills.size()) {
    if (!submitFill(fills[fillIndex++])) {
      return false;
    }
  }
  while (transferIndex < transfers.size()) {
    if (!submitTransfer(transfers[transferIndex++])) {
      return false;
    }
  }

  if (kind == Oot3dPicaPresentationExecutionKind::DependencyFlush) {
    if (present) {
      SetError(error, "PICA dependency flush cannot present a scanout");
      return false;
    }
    mAccumulator.CompleteDependencyWork();
    return true;
  }
  if (!topSnapshotExecuted) {
    if (!submitTransfer(sample.TopTransfer)) {
      return false;
    }
  } else {
    ++mStats.RedundantTopSnapshotCopiesAvoided;
  }
  if (present) {
    {
      OptionalTimer timer(mTimings.Enabled, mTimings.PresentTransferSeconds);
      if (!SubmitOot3dPicaVulkanDisplayTransfer(
              backend, sample.TopTransfer, true, error,
              renderTargetNamespace)) {
        return false;
      }
    }
    ++mStats.PresentedTransfers;
  }
  return true;
}

bool Oot3dPicaPresentationScheduler::PresentExisting(
    Oot3d::Renderer::PicaRenderBackend &backend,
    const Oot3dPicaDisplayTransferSubmission &transfer,
    uint64_t renderTargetNamespace, std::string *error) {
  OptionalTimer timer(mTimings.Enabled, mTimings.PresentExistingSeconds);
  if (!HasSnapshot(transfer, renderTargetNamespace)) {
    SetError(error, "PICA display snapshot has not been executed");
    return false;
  }
  if (!SubmitOot3dPicaVulkanDisplayTransfer(backend, transfer, true, error,
                                            renderTargetNamespace)) {
    return false;
  }
  ++mStats.PresentedTransfers;
  ++mStats.ReusedSnapshots;
  return true;
}

bool Oot3dPicaPresentationScheduler::HasSnapshot(
    const Oot3dPicaDisplayTransferSubmission &transfer,
    uint64_t renderTargetNamespace) const {
  const auto found = mSnapshotCompletions.find(
      MakeSnapshotTarget(transfer, renderTargetNamespace));
  return found != mSnapshotCompletions.end() &&
         found->second == transfer.CompletionId;
}

void Oot3dPicaPresentationScheduler::SetTimingEnabled(bool enabled) noexcept {
  if (mTimings.Enabled == enabled) {
    return;
  }
  mTimings = {};
  mTimings.Enabled = enabled;
}

void Oot3dPicaPresentationScheduler::Reset() {
  const bool timingEnabled = mTimings.Enabled;
  mAccumulator = {};
  mPresentationSerial.reset();
  mExecutedDrawsThisPresentation.clear();
  mCompositionDrawScratch.clear();
  mSnapshotCompletions.clear();
  mTimings = {};
  mTimings.Enabled = timingEnabled;
  ++mStats.Resets;
}

size_t Oot3dPicaPresentationScheduler::PendingDrawCount() const {
  return mAccumulator.PendingDrawCount();
}

bool Oot3dPicaPresentationScheduler::HasPendingRawCopyCompletion() const {
  return mAccumulator.HasPendingRawCopyCompletion();
}

const Oot3dPicaPresentationSchedulerStats &
Oot3dPicaPresentationScheduler::Stats() const noexcept {
  return mStats;
}

const Oot3dPicaPresentationSchedulerTimings &
Oot3dPicaPresentationScheduler::Timings() const noexcept {
  return mTimings;
}

Oot3dPicaPresentationScheduler::SnapshotTarget
Oot3dPicaPresentationScheduler::MakeSnapshotTarget(
    const Oot3dPicaDisplayTransferSubmission &transfer,
    uint64_t renderTargetNamespace) {
  return {renderTargetNamespace, transfer.OutputPhysicalAddress};
}

} // namespace Oot3dNativeGame
