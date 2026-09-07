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

void Oot3dPicaPresentationScheduler::BeginPresentation(
    uint64_t presentationSerial) {
  if (mPresentationSerial == presentationSerial) {
    return;
  }
  mPresentationSerial = presentationSerial;
  mExecutedDrawsThisPresentation.clear();
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
  if (sample.MemoryFills == nullptr || sample.DisplayTransfers == nullptr ||
      sample.Draws.empty()) {
    SetError(error, "PICA presentation sample is incomplete");
    return false;
  }

  {
    OptionalTimer validationTimer(mTimings.Enabled,
                                  mTimings.ValidationSeconds);
    std::unordered_set<uint64_t> sampleDraws;
    sampleDraws.reserve(sample.Draws.size());
    for (const auto &draw : sample.Draws) {
      if (draw.BasePlan == nullptr ||
          !sampleDraws.insert(draw.BasePlan->SubmissionId).second ||
          mExecutedDrawsThisPresentation.contains(
              draw.BasePlan->SubmissionId)) {
        ++mStats.DuplicateDrawAttempts;
        SetError(error,
                 "PICA draw would execute more than once in one presentation");
        return false;
      }
    }
    mExecutedDrawsThisPresentation.insert(sampleDraws.begin(),
                                          sampleDraws.end());
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
        },
    });
  }
  uint64_t compositionSequenceId = mNextCompositionSequenceId++;
  if (mNextCompositionSequenceId == 0U) {
    mNextCompositionSequenceId = 1U;
  }
  {
    OptionalTimer timer(mTimings.Enabled,
                        mTimings.CompositionPublicationSeconds);
    if (!backend.PublishPicaCompositionSequence(
            {
                .SchemaVersion = Oot3d::Renderer::
                    kPicaCompositionSequenceSchemaVersion,
                .SequenceId = compositionSequenceId,
                .Draws = mCompositionDrawScratch,
            },
            error)) {
      ++mStats.CompositionPublicationFailures;
      return false;
    }
  }
  ++mStats.CompositionSequencesPublished;
  mStats.CompositionDrawReferencesPublished +=
      mCompositionDrawScratch.size();

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
  case Oot3dPicaPresentationExecutionKind::DependencyFlush:
    ++mStats.DependencyFlushes;
    break;
  }
  mStats.InterpolatedDraws += sample.InterpolatedDraws;

  size_t fillIndex = 0;
  size_t transferIndex = 0;
  bool topSnapshotExecuted = false;
  const auto &fills = *sample.MemoryFills;
  const auto &transfers = *sample.DisplayTransfers;
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
    if (!SubmitOot3dPicaVulkanDisplayTransfer(backend, transfer, false, error,
                                              renderTargetNamespace)) {
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
