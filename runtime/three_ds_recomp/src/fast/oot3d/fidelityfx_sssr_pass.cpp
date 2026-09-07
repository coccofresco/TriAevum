#include "fast/oot3d/fidelityfx_sssr_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/normal_space_pass.h"

#include <bit>

#if __has_include(<FidelityFX/host/backends/vk/ffx_vk.h>) && \
    __has_include(<FidelityFX/host/ffx_sssr.h>)
#define OOT3D_HAS_FFX_SSSR 1
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_sssr.h>
#else
#define OOT3D_HAS_FFX_SSSR 0
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {

std::array<float, 16> MultiplyColumnMajor(
    const std::array<float, 16>& a,
    const std::array<float, 16>& b) {
    std::array<float, 16> result{};
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            for (size_t inner = 0; inner < 4; ++inner) {
                result[column * 4 + row] +=
                    a[inner * 4 + row] * b[column * 4 + inner];
            }
        }
    }
    return result;
}

VkImageCreateInfo ImageInfo(uint32_t width, uint32_t height,
                            VkFormat format, VkImageUsageFlags usage,
                            uint32_t layers = 1,
                            VkImageCreateFlags flags = 0,
                            uint32_t mipLevels = 1) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.flags = flags;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1U};
    info.mipLevels = mipLevels;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return info;
}

void ClearForShaderRead(VkCommandBuffer command, VkImage image,
                        VkImageAspectFlags aspect, uint32_t layers,
                        bool initialized, const VkClearColorValue& clear,
                        EffectPassBarrierExecution& barrierExecution) {
    VkImageMemoryBarrier toClear{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toClear.oldLayout = initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                    : VK_IMAGE_LAYOUT_UNDEFINED;
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = image;
    toClear.subresourceRange = {aspect, 0U, 1U, 0U, layers};
    toClear.srcAccessMask =
        initialized ? VK_ACCESS_SHADER_READ_BIT : 0U;
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(
        command,
        initialized ? (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                    : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
        1, &toClear);
    barrierExecution.RecordPrivateTransition({
        reinterpret_cast<uintptr_t>(image),
        {initialized ? ResourceAccess::ShaderRead
                     : ResourceAccess::Undefined, 0U},
        {ResourceAccess::TransferWrite, 0U}});
    const VkImageSubresourceRange range{aspect, 0U, 1U, 0U, layers};
    vkCmdClearColorImage(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);
    VkImageMemoryBarrier toRead = toClear;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        command, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toRead);
    barrierExecution.RecordPrivateTransition({
        reinterpret_cast<uintptr_t>(image),
        {ResourceAccess::TransferWrite, 0U},
        {ResourceAccess::ShaderRead, 0U}});
}

} // namespace

struct FidelityFxSssrPass::Impl {
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    NriOwnedTexture2D Output{};
    NormalSpacePass WorldNormals;
    VkImage Environment = VK_NULL_HANDLE;
    VkImage Brdf = VK_NULL_HANDLE;
    VkImageCreateInfo EnvironmentInfo{};
    VkImageCreateInfo BrdfInfo{};
    std::array<VkImage, 5> Inputs{};
    std::array<VkFormat, 5> InputFormats{};
    std::array<VkImageCreateInfo, 5> InputInfos{};
    VkImageCreateInfo OutputInfo{};
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Initialized = false;
    bool OutputInitialized = false;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason = "FidelityFX SSSR is not initialized";
#if OOT3D_HAS_FFX_SSSR
    std::vector<uint8_t> Scratch;
    VkDeviceContext DeviceContext{};
    FfxInterface Backend{};
    FfxSssrContext Context{};
    bool ContextCreated = false;
#endif
};

FidelityFxSssrPass::FidelityFxSssrPass()
    : mImpl(std::make_unique<Impl>()) {}
FidelityFxSssrPass::~FidelityFxSssrPass() { Shutdown(); }

bool FidelityFxSssrPass::Initialize(VkPhysicalDevice physicalDevice,
                                    VkDevice device,
                                    NriInteropContext& interop) {
    Shutdown();
    mImpl->PhysicalDevice = physicalDevice;
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#if !OOT3D_HAS_FFX_SSSR
    mImpl->Reason = "FidelityFX SSSR support was not compiled";
    return false;
#else
    try {
        mImpl->WorldNormals.Initialize(interop);
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U) {
            throw std::runtime_error(
                "FidelityFX SSSR RGBA16F storage is unavailable");
        }
        constexpr size_t contextCount = FFX_SSSR_CONTEXT_COUNT;
        mImpl->Scratch.resize(
            ffxGetScratchMemorySizeVK(physicalDevice, contextCount));
        mImpl->DeviceContext = {
            device, physicalDevice, vkGetDeviceProcAddr};
        const FfxDevice ffxDevice =
            ffxGetDeviceVK(&mImpl->DeviceContext);
        if (ffxGetInterfaceVK(
                &mImpl->Backend, ffxDevice, mImpl->Scratch.data(),
                mImpl->Scratch.size(), contextCount) != FFX_OK) {
            throw std::runtime_error(
                "FidelityFX SSSR Vulkan backend initialization failed");
        }
        mImpl->Initialized = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool FidelityFxSssrPass::SetIblResources(
    VkImage environmentImage, uint32_t environmentSize,
    uint32_t environmentMipCount, VkImage brdfImage,
    uint32_t brdfSize) {
    if (!mImpl->Initialized || environmentImage == VK_NULL_HANDLE ||
        brdfImage == VK_NULL_HANDLE || environmentSize == 0U ||
        environmentMipCount == 0U || brdfSize == 0U) {
        mImpl->Reason =
            "FidelityFX SSSR IBL resources are unavailable";
        return false;
    }
    mImpl->Environment = environmentImage;
    mImpl->EnvironmentInfo = ImageInfo(
        environmentSize, environmentSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        6U, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        environmentMipCount);
    mImpl->Brdf = brdfImage;
    mImpl->BrdfInfo = ImageInfo(
        brdfSize, brdfSize, VK_FORMAT_R16G16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    mImpl->Reason.clear();
    return true;
}

bool FidelityFxSssrPass::Configure(
    uint32_t width, uint32_t height,
    VkImage colorImage, VkFormat colorFormat,
    VkImage depthImage, VkFormat depthFormat,
    VkImage motionImage, VkFormat motionFormat,
    VkImage normalImage, VkFormat normalFormat,
    VkImage materialImage, VkFormat materialFormat) {
    const std::array<VkImage, 5> inputs{
        colorImage, depthImage, motionImage, normalImage, materialImage};
    const std::array<VkFormat, 5> formats{
        colorFormat, depthFormat, motionFormat, normalFormat, materialFormat};
    if (!mImpl->Initialized || mImpl->Environment == VK_NULL_HANDLE ||
        mImpl->Brdf == VK_NULL_HANDLE || width == 0U || height == 0U)
        return false;
    for (VkImage image : inputs)
        if (image == VK_NULL_HANDLE) return false;
    if (mImpl->Width == width && mImpl->Height == height &&
        mImpl->Inputs == inputs && mImpl->InputFormats == formats)
        return true;
#if !OOT3D_HAS_FFX_SSSR
    return false;
#else
    if ((mImpl->ContextCreated || mImpl->Output.Image != VK_NULL_HANDLE) &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS) {
        mImpl->Reason = "FidelityFX SSSR resize wait failed";
        return false;
    }
    InvalidateScreenResources();
    try {
        NriOwnedTexture2DDesc outputDesc{};
        outputDesc.Width = width;
        outputDesc.Height = height;
        outputDesc.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        outputDesc.Usage = VK_IMAGE_USAGE_STORAGE_BIT |
                           VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (!mImpl->Interop->CreateOwnedTexture2D(
                outputDesc, mImpl->Output)) {
            throw std::runtime_error(
                "FidelityFX SSSR output allocation failed");
        }
        mImpl->OutputInfo = ImageInfo(
            width, height, outputDesc.Format, outputDesc.Usage);
        mImpl->WorldNormals.Configure(width, height, normalImage, normalFormat);
        mImpl->InputInfos = {
            ImageInfo(width, height, colorFormat,
                      VK_IMAGE_USAGE_SAMPLED_BIT),
            ImageInfo(width, height, depthFormat,
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),
            ImageInfo(width, height, motionFormat,
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT),
            ImageInfo(width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_STORAGE_BIT),
            ImageInfo(width, height, materialFormat,
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)};
        FfxSssrContextDescription description{};
        description.renderSize = {width, height};
        description.normalsHistoryBufferFormat =
            ffxGetSurfaceFormatVK(VK_FORMAT_R16G16B16A16_SFLOAT);
        description.backendInterface = mImpl->Backend;
        if (ffxSssrContextCreate(&mImpl->Context, &description) != FFX_OK) {
            throw std::runtime_error(
                "FidelityFX SSSR context creation failed");
        }
        mImpl->ContextCreated = true;
        mImpl->Width = width;
        mImpl->Height = height;
        mImpl->Inputs = inputs;
        mImpl->InputFormats = formats;
        mImpl->OutputInitialized = false;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool FidelityFxSssrPass::Execute(
    VkCommandBuffer commandBuffer,
    const TemporalViewState& temporal,
    const PerspectiveViewState& view,
    const EffectsSettings& settings, uint32_t frameSlot,
    std::optional<PicaSurfaceCoordinates> coordinates) {
#if !OOT3D_HAS_FFX_SSSR
    (void)commandBuffer;
    (void)temporal;
    (void)view;
    (void)settings;
    return false;
#else
    if (!mImpl->ContextCreated || commandBuffer == VK_NULL_HANDLE)
        return false;
    mImpl->LastBarrierExecution = {};
    std::array<float, 16> inverseProjection{};
    if (!InvertTemporalMatrix(view.Projection, inverseProjection)) {
        mImpl->Reason =
            "FidelityFX SSSR projection matrix is not invertible";
        return false;
    }
    const auto viewMatrix = MultiplyColumnMajor(
        inverseProjection, temporal.CurrentWorldToClip);
    std::array<float, 16> inverseView{};
    if (!InvertTemporalMatrix(viewMatrix, inverseView)) {
        mImpl->Reason = "FidelityFX SSSR view matrix is not invertible";
        return false;
    }
    const auto storageProjection = coordinates ? coordinates->FidelityFxClip(view.Projection) : view.Projection;
    const auto storageWorldToClip = coordinates ? coordinates->FidelityFxClip(temporal.CurrentWorldToClip)
                                                : temporal.CurrentWorldToClip;
    const auto storagePreviousWorldToClip = coordinates ? coordinates->FidelityFxClip(temporal.PreviousWorldToClip)
                                                        : temporal.PreviousWorldToClip;
    std::array<float, 16> storageInverseProjection{}, storageInverseWorldToClip{};
    if (!InvertTemporalMatrix(storageProjection, storageInverseProjection) ||
        !InvertTemporalMatrix(storageWorldToClip, storageInverseWorldToClip)) {
        mImpl->Reason = "FidelityFX SSSR storage projection is not invertible";
        return false;
    }
    mImpl->WorldNormals.Execute(frameSlot, inverseView, mImpl->LastBarrierExecution);

    VkClearColorValue transparent{};
    ClearForShaderRead(commandBuffer, mImpl->Output.Image,
                       VK_IMAGE_ASPECT_COLOR_BIT, 1U,
                       mImpl->OutputInitialized, transparent,
                       mImpl->LastBarrierExecution);
    mImpl->OutputInitialized = true;

    const auto resource = [](VkImage image,
                             const VkImageCreateInfo& info,
                             const wchar_t* name,
                             FfxResourceUsage usage =
                                 FFX_RESOURCE_USAGE_READ_ONLY) {
        const FfxResourceDescription description =
            ffxGetImageResourceDescriptionVK(image, info, usage);
        return ffxGetResourceVK(
            reinterpret_cast<void*>(image), description, name,
            FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    };
    FfxSssrDispatchDescription dispatch{};
    dispatch.commandList = ffxGetCommandListVK(commandBuffer);
    dispatch.color = resource(
        mImpl->Inputs[0], mImpl->InputInfos[0], L"OOT3D_SSSR_Color");
    dispatch.depth = resource(
        mImpl->Inputs[1], mImpl->InputInfos[1], L"OOT3D_SSSR_Depth");
    dispatch.motionVectors = resource(
        mImpl->Inputs[2], mImpl->InputInfos[2], L"OOT3D_SSSR_Motion");
    dispatch.normal = resource(
        mImpl->WorldNormals.OutputImage(), mImpl->InputInfos[3], L"OOT3D_SSSR_WorldNormal");
    dispatch.materialParameters = resource(
        mImpl->Inputs[4], mImpl->InputInfos[4], L"OOT3D_SSSR_Material");
    dispatch.environmentMap = resource(
        mImpl->Environment, mImpl->EnvironmentInfo,
        L"OOT3D_SSSR_PicaEnvironment");
    dispatch.brdfTexture = resource(
        mImpl->Brdf, mImpl->BrdfInfo, L"OOT3D_SSSR_GgxBrdf");
    dispatch.output = resource(
        mImpl->Output.Image, mImpl->OutputInfo, L"OOT3D_SSSR_Output",
        FFX_RESOURCE_USAGE_UAV);
    std::memcpy(dispatch.invViewProjection,
                storageInverseWorldToClip.data(),
                sizeof(dispatch.invViewProjection));
    std::memcpy(dispatch.projection, storageProjection.data(),
                sizeof(dispatch.projection));
    std::memcpy(dispatch.invProjection, storageInverseProjection.data(),
                sizeof(dispatch.invProjection));
    std::memcpy(dispatch.view, viewMatrix.data(), sizeof(dispatch.view));
    std::memcpy(dispatch.invView, inverseView.data(),
                sizeof(dispatch.invView));
    std::memcpy(dispatch.prevViewProjection,
                storagePreviousWorldToClip.data(),
                sizeof(dispatch.prevViewProjection));
    dispatch.renderSize = {mImpl->Width, mImpl->Height};
    dispatch.motionVectorScale = {1.0F, 1.0F};
    dispatch.iblFactor = 1.0F;
    dispatch.normalUnPackMul = 2.0F;
    dispatch.normalUnPackAdd = -1.0F;
    dispatch.roughnessChannel = 1U;
    dispatch.isRoughnessPerceptual = true;
    dispatch.temporalStabilityFactor =
        temporal.HistoryValid ? 0.85F : 0.0F;
    dispatch.depthBufferThickness = std::clamp(
        settings.ReflectionThickness * 0.001F, 0.001F, 0.03F);
    dispatch.roughnessThreshold = std::clamp(
        0.8F - settings.ReflectionRoughnessBias * 0.5F, 0.05F, 0.99F);
    dispatch.varianceThreshold = 0.001F;
    dispatch.maxTraversalIntersections = std::clamp<uint32_t>(
        settings.ReflectionMaxSteps, 8U, 64U);
    dispatch.minTraversalOccupancy = 4U;
    dispatch.mostDetailedMip = 0U;
    dispatch.samplesPerQuad = 1U;
    dispatch.temporalVarianceGuidedTracingEnabled =
        temporal.HistoryValid ? 1U : 0U;
    if (ffxSssrContextDispatch(&mImpl->Context, &dispatch) != FFX_OK) {
        mImpl->Reason = "FidelityFX SSSR dispatch failed";
        return false;
    }
    mImpl->Reason.clear();
    return true;
#endif
}

void FidelityFxSssrPass::InvalidateScreenResources() {
#if OOT3D_HAS_FFX_SSSR
    if (mImpl->ContextCreated) {
        ffxSssrContextDestroy(&mImpl->Context);
        std::memset(&mImpl->Context, 0, sizeof(mImpl->Context));
        mImpl->ContextCreated = false;
    }
#endif
    mImpl->WorldNormals.Invalidate();
    if (mImpl->Interop != nullptr &&
        mImpl->Output.Image != VK_NULL_HANDLE) {
        mImpl->Interop->DestroyOwnedTexture(mImpl->Output.Image);
    }
    mImpl->Output = {};
    mImpl->Inputs = {};
    mImpl->InputFormats = {};
    mImpl->InputInfos = {};
    mImpl->OutputInfo = {};
    mImpl->Width = 0;
    mImpl->Height = 0;
    mImpl->OutputInitialized = false;
    mImpl->LastBarrierExecution = {};
}

void FidelityFxSssrPass::Shutdown() {
    InvalidateScreenResources();
    mImpl = std::make_unique<Impl>();
}

bool FidelityFxSssrPass::Available() const {
    return mImpl->Initialized;
}
VkImage FidelityFxSssrPass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView FidelityFxSssrPass::OutputView() const {
    return mImpl->Output.SampledView;
}
bool FidelityFxSssrPass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Output.Image);
}
bool FidelityFxSssrPass::FidelityFxBackendActive() const {
#if OOT3D_HAS_FFX_SSSR
    return mImpl->ContextCreated;
#else
    return false;
#endif
}
bool FidelityFxSssrPass::IblResourcesConfigured() const {
    return mImpl->Environment != VK_NULL_HANDLE &&
           mImpl->Brdf != VK_NULL_HANDLE;
}
const EffectPassBarrierExecution& FidelityFxSssrPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& FidelityFxSssrPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
