#include "fast/oot3d/cacao_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"

#include <cstdlib>
#include <cstring>

#ifdef ENABLE_OOT3D_CACAO
#define FFX_CACAO_ENABLE_VULKAN
#include <ffx_cacao.h>
#include <ffx_cacao_impl.h>
#endif

namespace Fast::Oot3d {

struct CacaoPass::Impl {
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    VkImage Output = VK_NULL_HANDLE;
    VkImageView OutputView = VK_NULL_HANDLE;
    VkImageView OutputStorageView = VK_NULL_HANDLE;
    ResourceStateTracker* OutputStates = nullptr;
    uint32_t Width = 0;
    uint32_t Height = 0;
    VkImageView DepthView = VK_NULL_HANDLE;
    VkImageView NormalView = VK_NULL_HANDLE;
    CacaoQuality Quality = CacaoQuality::Low;
    CacaoNormalSource NormalSource =
        CacaoNormalSource::ReconstructedFromDepth;
    bool ScreenConfigured = false;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason = "FidelityFX CACAO support was not compiled into this build";
#ifdef ENABLE_OOT3D_CACAO
    FFX_CACAO_VkContext* Context = nullptr;
#endif
};

CacaoPass::CacaoPass() : mImpl(std::make_unique<Impl>()) {}
CacaoPass::~CacaoPass() { Shutdown(); }

bool CacaoPass::Initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                           NriInteropContext& interop) {
    Shutdown();
#ifdef ENABLE_OOT3D_CACAO
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);
    if (!features.shaderStorageImageExtendedFormats) {
        mImpl->Reason = "CACAO requires shaderStorageImageExtendedFormats";
        return false;
    }
    mImpl->Context = static_cast<FFX_CACAO_VkContext*>(
        std::malloc(FFX_CACAO_VkGetContextSize()));
    if (!mImpl->Context) {
        mImpl->Reason = "CACAO context allocation failed";
        return false;
    }
    FFX_CACAO_VkCreateInfo info{};
    info.physicalDevice = physicalDevice;
    info.device = device;
    if (FFX_CACAO_VkInitContext(mImpl->Context, &info) != FFX_CACAO_STATUS_OK) {
        std::free(mImpl->Context);
        mImpl->Context = nullptr;
        mImpl->Reason = "FFX_CACAO_VkInitContext failed";
        return false;
    }
    mImpl->Reason.clear();
    return true;
#else
    (void)physicalDevice; (void)device; (void)interop;
    return false;
#endif
}

bool CacaoPass::Configure(
    const NriEffectGraphTransientImageBinding& output,
    VkImageView depthView, VkImageView picaNormalGuideView,
    const CacaoSettings& settings) {
#ifdef ENABLE_OOT3D_CACAO
    if (!Available()) return false;
    constexpr VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    if (!output.Valid() ||
        output.Resource != EffectResource::AmbientOcclusion ||
        output.Format != VK_FORMAT_R32_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        output.MipLevels != 1U || output.Layers != 1U ||
        output.Samples != VK_SAMPLE_COUNT_1_BIT ||
        (output.Usage & requiredUsage) != requiredUsage ||
        depthView == VK_NULL_HANDLE) {
        mImpl->Reason = "CACAO graph output contract is invalid";
        return false;
    }
    const uint32_t width = output.Width;
    const uint32_t height = output.Height;
    const auto normalInput = ResolveCacaoNormalInput(
        settings.Quality, picaNormalGuideView != VK_NULL_HANDLE);
    const VkImageView normalView = normalInput.UsesPicaGuide()
        ? picaNormalGuideView
        : VK_NULL_HANDLE;
    const bool resourcesMatch = mImpl->ScreenConfigured &&
        width == mImpl->Width && height == mImpl->Height &&
        output.Texture.Image == mImpl->Output &&
        output.Texture.SampledView == mImpl->OutputView &&
        output.Texture.StorageView == mImpl->OutputStorageView &&
        output.StateTracker == mImpl->OutputStates &&
        depthView == mImpl->DepthView && normalView == mImpl->NormalView &&
        settings.Quality == mImpl->Quality &&
        normalInput.Source == mImpl->NormalSource;
    if (!resourcesMatch) {
        // Screen resources may still be referenced by CACAO commands submitted
        // in an older frame. A scene transition is rare; synchronize here
        // before replacing descriptors/images rather than risking use-after-free.
        if (mImpl->ScreenConfigured &&
            vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS) {
            return false;
        }
        InvalidateScreenResources();

        mImpl->Output = output.Texture.Image;
        mImpl->OutputView = output.Texture.SampledView;
        mImpl->OutputStorageView = output.Texture.StorageView;
        mImpl->OutputStates = output.StateTracker;

        FFX_CACAO_VkScreenSizeInfo screen{};
        screen.width = width;
        screen.height = height;
        screen.depthView = depthView;
        screen.normalsView = normalView;
        screen.output = mImpl->Output;
        screen.outputView = mImpl->OutputStorageView;
        screen.useDownsampledSsao = settings.Quality == CacaoQuality::Low
            ? FFX_CACAO_TRUE : FFX_CACAO_FALSE;
        if (FFX_CACAO_VkInitScreenSizeDependentResources(
                mImpl->Context, &screen) != FFX_CACAO_STATUS_OK) {
            mImpl->Reason =
                "FFX_CACAO_VkInitScreenSizeDependentResources failed";
            InvalidateScreenResources();
            return false;
        }
        mImpl->Width = width;
        mImpl->Height = height;
        mImpl->DepthView = depthView;
        mImpl->NormalView = normalView;
        mImpl->Quality = settings.Quality;
        mImpl->NormalSource = normalInput.Source;
        mImpl->ScreenConfigured = true;
    }
    FFX_CACAO_Settings nativeSettings = FFX_CACAO_DEFAULT_SETTINGS;
    nativeSettings.qualityLevel = settings.Quality == CacaoQuality::Low
                                      ? FFX_CACAO_QUALITY_LOW
                                      : FFX_CACAO_QUALITY_MEDIUM;
    nativeSettings.generateNormals = normalInput.UsesPicaGuide()
        ? FFX_CACAO_FALSE
        : FFX_CACAO_TRUE;
    nativeSettings.radius = settings.Radius;
    nativeSettings.shadowMultiplier = settings.Strength;
    nativeSettings.shadowPower = settings.ShadowPower;
    nativeSettings.shadowClamp = settings.ShadowClamp;
    nativeSettings.horizonAngleThreshold = settings.HorizonAngleThreshold;
    nativeSettings.fadeOutFrom = settings.FadeOutFrom;
    nativeSettings.fadeOutTo = settings.FadeOutTo;
    nativeSettings.blurPassCount = settings.BlurPassCount;
    nativeSettings.sharpness = settings.Sharpness;
    nativeSettings.detailShadowStrength = settings.DetailStrength;
    if (FFX_CACAO_VkUpdateSettings(mImpl->Context, &nativeSettings) !=
        FFX_CACAO_STATUS_OK) {
        mImpl->Reason = "FFX_CACAO_VkUpdateSettings failed";
        return false;
    }
    mImpl->Reason.clear();
    return true;
#else
    (void)output;
    (void)depthView;
    (void)picaNormalGuideView; (void)settings;
    return false;
#endif
}

void CacaoPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_CACAO
    if (mImpl->ScreenConfigured && mImpl->Context) {
        FFX_CACAO_VkDestroyScreenSizeDependentResources(mImpl->Context);
    }
#endif
    mImpl->Width = 0;
    mImpl->Height = 0;
    mImpl->DepthView = VK_NULL_HANDLE;
    mImpl->NormalView = VK_NULL_HANDLE;
    mImpl->NormalSource = CacaoNormalSource::ReconstructedFromDepth;
    mImpl->Output = VK_NULL_HANDLE;
    mImpl->OutputView = VK_NULL_HANDLE;
    mImpl->OutputStorageView = VK_NULL_HANDLE;
    mImpl->OutputStates = nullptr;
    mImpl->ScreenConfigured = false;
    mImpl->LastBarrierExecution = {};
}

bool CacaoPass::Execute(VkCommandBuffer commandBuffer,
                        const std::array<float, 16>& projection,
                        const EffectPassBarrierPlan& barrierPlan,
                        std::optional<PicaSurfaceCoordinates> coordinates) {
    mImpl->LastBarrierExecution = {};
#ifdef ENABLE_OOT3D_CACAO
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::AmbientOcclusion);
    if (!mImpl->ScreenConfigured || mImpl->OutputStates == nullptr ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed() ||
        outputBarrier->DispatchAccess != ResourceAccess::ComputeWrite ||
        outputBarrier->CompletionAccess != ResourceAccess::ShaderRead ||
        !outputBarrier->DownstreamRead) {
        return false;
    }
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::AmbientOcclusion);
    FFX_CACAO_Matrix4x4 matrix{};
    const auto storageProjection = coordinates ? coordinates->CacaoProjection(projection) : projection;
    std::memcpy(&matrix, storageProjection.data(), sizeof(matrix));
    FFX_CACAO_Matrix4x4 normalsToView{};
    const FFX_CACAO_Matrix4x4* normalsToViewPtr = nullptr;
    if (mImpl->NormalSource == CacaoNormalSource::PicaViewSpaceGuide) {
        const auto transform = coordinates ? coordinates->CacaoNormalTransform(projection[11])
                                           : CacaoViewSpaceNormalTransform(projection);
        std::memcpy(&normalsToView, transform.data(), sizeof(normalsToView));
        normalsToViewPtr = &normalsToView;
    }
    if (FFX_CACAO_VkDraw(mImpl->Context, commandBuffer, &matrix,
                         normalsToViewPtr) != FFX_CACAO_STATUS_OK)
        return false;

    // The pinned FidelityFX provider performs these exact public-output
    // transitions inside FFX_CACAO_VkDraw. Account for them without emitting
    // duplicate host barriers; provider-private scratch barriers stay private.
    const uintptr_t output = reinterpret_cast<uintptr_t>(mImpl->Output);
    mImpl->LastBarrierExecution.RecordGraphTransition({
        output,
        {ResourceAccess::Undefined, 0U},
        {outputBarrier->DispatchAccess, 0U},
    });
    mImpl->LastBarrierExecution.RecordGraphTransition({
        output,
        {outputBarrier->DispatchAccess, 0U},
        {outputBarrier->CompletionAccess, 0U},
    });
    mImpl->OutputStates->Commit({
        output,
        {ResourceAccess::Undefined, 0U},
        {outputBarrier->DispatchAccess, 0U},
    });
    mImpl->OutputStates->Commit({
        output,
        {outputBarrier->DispatchAccess, 0U},
        {outputBarrier->CompletionAccess, 0U},
    });
    return true;
#else
    (void)commandBuffer; (void)projection; (void)barrierPlan;
    return false;
#endif
}

void CacaoPass::Shutdown() {
#ifdef ENABLE_OOT3D_CACAO
    InvalidateScreenResources();
    if (mImpl->Context) { FFX_CACAO_VkDestroyContext(mImpl->Context); std::free(mImpl->Context); }
    mImpl->Context = nullptr;
#endif
    mImpl->PhysicalDevice = VK_NULL_HANDLE; mImpl->Device = VK_NULL_HANDLE;
    mImpl->Output = VK_NULL_HANDLE; mImpl->OutputView = VK_NULL_HANDLE;
    mImpl->OutputStorageView = VK_NULL_HANDLE;
    mImpl->OutputStates = nullptr;
    mImpl->Interop = nullptr; mImpl->ScreenConfigured = false;
}

bool CacaoPass::Available() const {
#ifdef ENABLE_OOT3D_CACAO
    return mImpl->Context != nullptr;
#else
    return false;
#endif
}
VkImage CacaoPass::OutputImage() const { return mImpl->Output; }
VkImageView CacaoPass::OutputView() const { return mImpl->OutputView; }
bool CacaoPass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr && mImpl->Interop->OwnsTexture(mImpl->Output);
}
CacaoNormalSource CacaoPass::NormalSource() const {
    return mImpl->NormalSource;
}
bool CacaoPass::UsesPicaNormalGuide() const {
    return mImpl->NormalSource == CacaoNormalSource::PicaViewSpaceGuide;
}
const EffectPassBarrierExecution& CacaoPass::LastBarrierExecution() const {
    return mImpl->LastBarrierExecution;
}
const std::string& CacaoPass::UnavailableReason() const { return mImpl->Reason; }

} // namespace Fast::Oot3d

#endif
