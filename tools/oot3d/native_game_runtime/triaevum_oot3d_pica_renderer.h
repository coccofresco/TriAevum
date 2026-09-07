#pragma once

#include "oot3d/renderer/pica_render_backend.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "triaevum_oot3d_pica_batch.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace Oot3dNativeGame {

class TriAevumOot3dPicaHost;

struct TriAevumOot3dPicaRendererOptions {
    std::uint64_t RenderTargetNamespace = 0U;
    std::function<bool(const Oot3dPicaDisplayTransferSubmission&)> ShouldPresentTransfer;
};

// Title adapter from ordered native PICA work to the common 3DS renderer
// contract. The backend may be Vulkan, OpenGL, or a conformance backend.
class TriAevumOot3dPicaRenderer final : public TriAevumOot3dPicaBatchSink {
  public:
    using InterruptPublisher = std::function<bool(Oot3dPicaInterruptId, std::string*)>;

    TriAevumOot3dPicaRenderer(Oot3d::Renderer::PicaRenderBackend& backend, InterruptPublisher publishInterrupt,
                              TriAevumOot3dPicaRendererOptions options = {});
    TriAevumOot3dPicaRenderer(Oot3d::Renderer::PicaRenderBackend& backend, TriAevumOot3dPicaHost& host,
                              TriAevumOot3dPicaRendererOptions options = {});

    bool Consume(Oot3dPicaSubmissionBatch batch, std::string* error = nullptr);
    bool DrainPending(std::string* error = nullptr);
    bool PollCompletions(std::string* error = nullptr);

    std::size_t PendingCompletionCount() const noexcept;
    std::uint64_t RetainedScanoutPresentationCount() const noexcept;
    const TriAevumOot3dPicaBatchStats& Stats() const noexcept;

  private:
    bool SubmitDraw(Oot3dPicaDrawSubmission& draw, std::string* error) override;
    bool SubmitDisplayTransfer(const Oot3dPicaDisplayTransferSubmission& transfer, std::string* error) override;
    bool SubmitMemoryFill(const Oot3dPicaMemoryFillSubmission& fill, std::string* error) override;
    bool QueueCompletion(std::uint64_t completionId, Oot3dPicaInterruptId interrupt, std::string* error) override;
    bool PresentConfiguredScanout(std::string* error);

    Oot3d::Renderer::PicaRenderBackend& mBackend;
    TriAevumOot3dPicaHost* mSourceHost = nullptr;
    InterruptPublisher mPublishInterrupt;
    TriAevumOot3dPicaRendererOptions mOptions;
    TriAevumOot3dPicaBatchDispatcher mDispatcher;
    Oot3dPicaVulkanShaderSourceCache mShaderCache;
    std::unordered_map<std::uint32_t, Oot3dPicaDisplayTransferSubmission> mDisplayTransfersByOutput;
    std::optional<std::uint32_t> mPresentedOutputDuringConsume;
    std::uint64_t mRetainedScanoutPresentationCount = 0U;
    std::unordered_map<std::uint64_t, Oot3dPicaInterruptId> mPendingCompletions;
    std::deque<Oot3dPicaInterruptId> mReadyInterrupts;
};

} // namespace Oot3dNativeGame
