#include "fast/oot3d/nri_upscaler_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/upscaler_orientation.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif

namespace Fast::Oot3d {

bool NriUpscalerPass::Initialize(VkPhysicalDevice physicalDevice,
                                 VkDevice device,
                                 NriInteropContext& interop) {
    Shutdown();
    mDevice = device;
    mInterop = &interop;
    // Optional: without it DLSS reconstructs the surface as stored.
    (void)mOrientation.Initialize(physicalDevice, device, interop);
    if (!interop.NisAvailable()) {
        mUnavailableReason = interop.NisUnavailableReason();
        return false;
    }
    mUnavailableReason.clear();
    return true;
}

void NriUpscalerPass::Shutdown() {
    ReleaseOutput();
    mOrientation.Shutdown();
    mDevice = VK_NULL_HANDLE;
    mInterop = nullptr;
    mUnavailableReason = "NRI upscaler is not initialized";
}

void NriUpscalerPass::ReleaseOutput() {
    mOutputImage = VK_NULL_HANDLE;
    mOutputView = VK_NULL_HANDLE;
    mOutputWidth = 0;
    mOutputHeight = 0;
    mNriBarriersUsed = false;
    mLastBarrierExecution = {};
    mOutputStates = nullptr;
    mOutputEncoding = SceneColorEncoding::Unknown;
}

bool NriUpscalerPass::Configure(
    const NriEffectGraphTransientImageBinding& output) {
    if (!Available() || !output.Valid() ||
        output.Resource != EffectResource::UpscaledColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (mOutputImage == output.Texture.Image &&
        mOutputWidth == output.Width && mOutputHeight == output.Height &&
        mOutputStates == output.StateTracker) {
        return true;
    }
    ReleaseOutput();
    mOutputImage = output.Texture.Image;
    mOutputView = output.Texture.SampledView;
    mOutputWidth = output.Width;
    mOutputHeight = output.Height;
    mOutputStates = output.StateTracker;
    return true;
}

bool NriUpscalerPass::Execute(
    VkCommandBuffer commandBuffer, uint32_t frameIndex,
    VkImage inputImage, VkFormat inputFormat,
    uint32_t inputWidth, uint32_t inputHeight,
    UpscalerQuality quality, float sharpness,
    const EffectPassBarrierPlan& barrierPlan,
    SceneColorEncoding inputEncoding) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::UpscaledColor);
    if (!Available() || mOutputImage == VK_NULL_HANDLE ||
        mOutputStates == nullptr ||
        inputImage == VK_NULL_HANDLE ||
        !IsKnownSceneColorEncoding(inputEncoding) ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed())
        return false;
    (void)commandBuffer;
    mNriBarriersUsed = false;
    mLastBarrierExecution = barrierPlan.BeginExecution();
    if (!BeginOutput(frameIndex, *outputBarrier)) return false;

    const bool dispatched = mInterop->DispatchNis(
        frameIndex, inputImage, inputFormat, inputWidth, inputHeight,
        mOutputImage, VK_FORMAT_R16G16B16A16_SFLOAT,
        mOutputWidth, mOutputHeight, quality, sharpness, inputEncoding);

    const bool completed = EndOutput(frameIndex, *outputBarrier);
    if (completed && dispatched) mOutputEncoding = inputEncoding;
    return completed && dispatched;
}

bool NriUpscalerPass::ExecuteFsr(VkCommandBuffer commandBuffer,
                                 NriTemporalUpscaleDispatchDesc desc,
                                 const EffectPassBarrierPlan& barrierPlan) {
    return ExecuteTemporal(commandBuffer, desc, UpscalerProvider::Fsr, barrierPlan, std::nullopt);
}

bool NriUpscalerPass::ExecuteDlss(VkCommandBuffer commandBuffer, NriTemporalUpscaleDispatchDesc desc,
                                  const EffectPassBarrierPlan& barrierPlan,
                                  std::optional<PicaSurfaceCoordinates> upright) {
    return ExecuteTemporal(commandBuffer, desc, UpscalerProvider::Dlss, barrierPlan, upright);
}

bool NriUpscalerPass::LastDispatchUpright() const {
    return mLastDispatchUpright;
}

bool NriUpscalerPass::ExecuteTemporal(VkCommandBuffer commandBuffer, NriTemporalUpscaleDispatchDesc desc,
                                      UpscalerProvider provider, const EffectPassBarrierPlan& barrierPlan,
                                      std::optional<PicaSurfaceCoordinates> upright) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::UpscaledColor);
    const bool providerAvailable = provider == UpscalerProvider::Fsr
        ? FsrAvailable() : DlssAvailable();
    if (!providerAvailable || mOutputImage == VK_NULL_HANDLE ||
        mOutputStates == nullptr ||
        desc.InputImage == VK_NULL_HANDLE ||
        !IsKnownSceneColorEncoding(desc.InputEncoding) ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed())
        return false;
    (void)commandBuffer;
    mNriBarriersUsed = false;
    mLastBarrierExecution = barrierPlan.BeginExecution();
    mLastDispatchUpright = false;
    // The upright output first: a turn is all or nothing.
    const bool turn = upright.has_value() && mOrientation.Available() &&
                      mOrientation.BeginOutput(desc.FrameIndex, mOutputWidth, mOutputHeight, mLastBarrierExecution) &&
                      mOrientation.TurnInputs(desc.FrameIndex,
                                              { desc.InputImage, desc.InputFormat, desc.DepthImage, desc.DepthFormat,
                                                desc.MotionImage, desc.MotionFormat, desc.ReactiveImage,
                                                desc.ReactiveFormat, desc.InputWidth, desc.InputHeight },
                                              *upright, mLastBarrierExecution);
    if (turn) {
        desc.InputImage = mOrientation.ColorImage();
        desc.InputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.DepthImage = mOrientation.DepthImage();
        desc.DepthFormat = VK_FORMAT_R32_SFLOAT;
        desc.MotionImage = mOrientation.MotionImage();
        desc.MotionFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
        desc.ReactiveImage = mOrientation.ReactiveImage();
        desc.ReactiveFormat = VK_FORMAT_R16_SFLOAT;
        const auto input = UprightExtent(desc.InputWidth, desc.InputHeight);
        desc.InputWidth = input[0];
        desc.InputHeight = input[1];
        desc.JitterPixels = UprightOffset(desc.JitterPixels, *upright);
    }
    if (!BeginOutput(desc.FrameIndex, *outputBarrier)) return false;

    desc.OutputImage = turn ? mOrientation.OutputImage() : mOutputImage;
    desc.OutputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    const auto output =
        turn ? UprightExtent(mOutputWidth, mOutputHeight) : std::array<uint32_t, 2>{ mOutputWidth, mOutputHeight };
    desc.OutputWidth = output[0];
    desc.OutputHeight = output[1];
    bool dispatched = provider == UpscalerProvider::Fsr ? mInterop->DispatchFsr(desc) : mInterop->DispatchDlss(desc);
    if (dispatched && turn)
        dispatched = mOrientation.TurnOutput(desc.FrameIndex, mOutputImage, mOutputWidth, mOutputHeight, *upright,
                                             mLastBarrierExecution);
    mLastDispatchUpright = dispatched && turn;

    const bool completed = EndOutput(desc.FrameIndex, *outputBarrier);
    if (completed && dispatched) mOutputEncoding = desc.InputEncoding;
    return completed && dispatched;
}

bool NriUpscalerPass::BeginOutput(
    uint32_t frameIndex,
    const EffectPassOutputBarrier& outputBarrier) {
#ifdef ENABLE_OOT3D_NRI
    const auto transition = mOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mOutputImage),
        {outputBarrier.DispatchAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mInterop, frameIndex, mOutputImage, transition))
        return false;
    mLastBarrierExecution.RecordGraphTransition(transition);
    mOutputStates->Commit(transition);
    return true;
#else
    (void)frameIndex;
    (void)outputBarrier;
    return false;
#endif
}

bool NriUpscalerPass::EndOutput(
    uint32_t frameIndex,
    const EffectPassOutputBarrier& outputBarrier) {
#ifdef ENABLE_OOT3D_NRI
    const auto transition = mOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mOutputImage),
        {outputBarrier.CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mInterop, frameIndex, mOutputImage, transition))
        return false;
    mLastBarrierExecution.RecordGraphTransition(transition);
    mOutputStates->Commit(transition);
    mNriBarriersUsed = true;
    return true;
#else
    (void)frameIndex;
    (void)outputBarrier;
    return false;
#endif
}

void NriUpscalerPass::InvalidateScreenResources() {
    ReleaseOutput();
    mOrientation.InvalidateScreenResources();
}
bool NriUpscalerPass::Available() const {
    return mDevice != VK_NULL_HANDLE && mInterop != nullptr &&
           mUnavailableReason.empty();
}
bool NriUpscalerPass::FsrAvailable() const {
    return mDevice != VK_NULL_HANDLE && mInterop != nullptr &&
           mInterop->FsrAvailable();
}
bool NriUpscalerPass::DlssAvailable() const {
    return mDevice != VK_NULL_HANDLE && mInterop != nullptr &&
           mInterop->DlssAvailable();
}
const std::string& NriUpscalerPass::UnavailableReason() const {
    return mUnavailableReason;
}
VkImage NriUpscalerPass::OutputImage() const { return mOutputImage; }
VkImageView NriUpscalerPass::OutputView() const { return mOutputView; }
uint32_t NriUpscalerPass::OutputWidth() const { return mOutputWidth; }
uint32_t NriUpscalerPass::OutputHeight() const { return mOutputHeight; }
bool NriUpscalerPass::OutputOwnedByNri() const {
    return mInterop != nullptr && mInterop->OwnsTexture(mOutputImage);
}
bool NriUpscalerPass::BarriersOwnedByNri() const {
    return mNriBarriersUsed;
}
SceneColorEncoding NriUpscalerPass::OutputEncoding() const {
    return mOutputEncoding;
}
const EffectPassBarrierExecution& NriUpscalerPass::LastBarrierExecution()
    const {
    return mLastBarrierExecution;
}

} // namespace Fast::Oot3d

#endif
