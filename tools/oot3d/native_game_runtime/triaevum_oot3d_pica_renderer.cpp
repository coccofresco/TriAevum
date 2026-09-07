#include "triaevum_oot3d_pica_renderer.h"

#include "oot3d_native_pica_vulkan_bridge.h"
#include "triaevum_oot3d_pica_host.h"

#include <array>
#include <utility>

namespace Oot3dNativeGame {
namespace {

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

TriAevumOot3dPicaRendererOptions BindHostScanout(
    TriAevumOot3dPicaHost& host,
    TriAevumOot3dPicaRendererOptions options) {
    if (!options.ShouldPresentTransfer) {
        options.ShouldPresentTransfer = [&host](const auto& transfer) {
            return host.Scanout().ShouldPresent(
                0U, transfer.Transfer.OutputAddress);
        };
    }
    return options;
}

} // namespace

TriAevumOot3dPicaRenderer::TriAevumOot3dPicaRenderer(Oot3d::Renderer::PicaRenderBackend& backend,
                                                     InterruptPublisher publishInterrupt,
                                                     TriAevumOot3dPicaRendererOptions options)
    : mBackend(backend), mPublishInterrupt(std::move(publishInterrupt)), mOptions(std::move(options)) {
}

TriAevumOot3dPicaRenderer::TriAevumOot3dPicaRenderer(Oot3d::Renderer::PicaRenderBackend& backend,
                                                     TriAevumOot3dPicaHost& host,
                                                     TriAevumOot3dPicaRendererOptions options)
    : TriAevumOot3dPicaRenderer(
          backend,
          [&host](Oot3dPicaInterruptId interrupt, std::string*) {
              host.PublishCompletedInterrupt(interrupt);
              return true;
          },
          BindHostScanout(host, std::move(options))) {
    mSourceHost = &host;
}

bool TriAevumOot3dPicaRenderer::Consume(Oot3dPicaSubmissionBatch batch, std::string* error) {
    if (!mPublishInterrupt) {
        return Fail(error, "PICA renderer has no interrupt publisher");
    }
    mPresentedOutputDuringConsume.reset();
    if (!PollCompletions(error) || !mDispatcher.Dispatch(std::move(batch), *this, error)) {
        return false;
    }
    return PresentConfiguredScanout(error) && PollCompletions(error);
}

bool TriAevumOot3dPicaRenderer::DrainPending(std::string* error) {
    if (mSourceHost == nullptr) {
        return Fail(error, "PICA renderer has no module host source");
    }
    return Consume(mSourceHost->TakePendingBatch(), error);
}

bool TriAevumOot3dPicaRenderer::PollCompletions(std::string* error) {
    for (const std::uint64_t completionId : mBackend.TakePicaCompletions()) {
        const auto found = mPendingCompletions.find(completionId);
        if (found == mPendingCompletions.end()) {
            return Fail(error, "renderer returned an unknown PICA completion ID");
        }
        mReadyInterrupts.push_back(found->second);
        mPendingCompletions.erase(found);
    }
    while (!mReadyInterrupts.empty()) {
        if (!mPublishInterrupt(mReadyInterrupts.front(), error)) {
            return false;
        }
        mReadyInterrupts.pop_front();
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::size_t TriAevumOot3dPicaRenderer::PendingCompletionCount() const noexcept {
    return mPendingCompletions.size() + mReadyInterrupts.size();
}

std::uint64_t TriAevumOot3dPicaRenderer::RetainedScanoutPresentationCount() const noexcept {
    return mRetainedScanoutPresentationCount;
}

const TriAevumOot3dPicaBatchStats& TriAevumOot3dPicaRenderer::Stats() const noexcept {
    return mDispatcher.Stats();
}

bool TriAevumOot3dPicaRenderer::SubmitDraw(Oot3dPicaDrawSubmission& draw, std::string* error) {
    Oot3dPicaVulkanDrawPlan plan;
    return BuildOot3dPicaVulkanDrawPlanAndConsumeResources(draw, plan, error, &mShaderCache) &&
           SubmitOot3dPicaVulkanDrawPlan(mBackend, plan, error, mOptions.RenderTargetNamespace);
}

bool TriAevumOot3dPicaRenderer::SubmitDisplayTransfer(const Oot3dPicaDisplayTransferSubmission& transfer,
                                                      std::string* error) {
    const bool present = mOptions.ShouldPresentTransfer && mOptions.ShouldPresentTransfer(transfer);
    if (!SubmitOot3dPicaVulkanDisplayTransfer(mBackend, transfer, present, error, mOptions.RenderTargetNamespace)) {
        return false;
    }
    mDisplayTransfersByOutput.insert_or_assign(transfer.Transfer.OutputAddress, transfer);
    if (present) {
        mPresentedOutputDuringConsume = transfer.Transfer.OutputAddress;
    }
    return true;
}

bool TriAevumOot3dPicaRenderer::PresentConfiguredScanout(std::string* error) {
    if (mSourceHost == nullptr || mSourceHost->Scanout().LcdForceBlack()) {
        return true;
    }
    const auto framebuffer = mSourceHost->Scanout().Framebuffer(0U);
    if (!framebuffer.has_value()) {
        return true;
    }
    const std::array<std::uint32_t, 2> addresses{ framebuffer->addressLeft, framebuffer->addressRight };
    for (const std::uint32_t address : addresses) {
        if (address == 0U || mPresentedOutputDuringConsume == address) {
            continue;
        }
        const auto transfer = mDisplayTransfersByOutput.find(address);
        if (transfer == mDisplayTransfersByOutput.end()) {
            continue;
        }
        if (!SubmitOot3dPicaVulkanDisplayTransfer(mBackend, transfer->second, true, error,
                                                  mOptions.RenderTargetNamespace)) {
            return false;
        }
        mPresentedOutputDuringConsume = address;
        ++mRetainedScanoutPresentationCount;
        break;
    }
    return true;
}

bool TriAevumOot3dPicaRenderer::SubmitMemoryFill(const Oot3dPicaMemoryFillSubmission& fill, std::string* error) {
    return SubmitOot3dPicaVulkanMemoryFill(mBackend, fill, error, mOptions.RenderTargetNamespace);
}

bool TriAevumOot3dPicaRenderer::QueueCompletion(std::uint64_t completionId, Oot3dPicaInterruptId interrupt,
                                                std::string* error) {
    if (completionId == 0U || mPendingCompletions.contains(completionId)) {
        return Fail(error, "PICA renderer completion ID is invalid or pending");
    }
    if (!mBackend.QueuePicaCompletion(completionId, error)) {
        return false;
    }
    mPendingCompletions.emplace(completionId, interrupt);
    return true;
}

} // namespace Oot3dNativeGame
