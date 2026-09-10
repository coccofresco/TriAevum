#include "fast/oot3d/temporal_aa_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/temporal_aa.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {
constexpr uint32_t kFrameSlots = 2U;
struct TemporalPush {
    uint32_t Width = 0;
    uint32_t Height = 0;
    float InvWidth = 0.0F;
    float InvHeight = 0.0F;
    float HistoryWeight = 0.0F;
    float ClampExpansion = 0.0F;
    float Sharpness = 0.0F;
    uint32_t HistoryValid = 0;
};
static_assert(sizeof(TemporalPush) == 32U);

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders) {
    return shaders.Resolve(BuildTemporalAaComputeShader(), Renderer::SpirvStage::Compute, "oot3d_temporal_aa.comp");
}
} // namespace

struct TemporalAaPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kFrameSlots> Sets{};
#endif
    std::array<VkImage, 2> Images{};
    std::array<VkImageView, 2> Views{};
    std::array<VkImageView, 2> StorageViews{};
    std::array<bool, 2> ContentValid{};
    ResourceStateTracker HistoryStates;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t OutputIndex = 0;
    bool HistoryUsed = false;
    bool NriBarriersUsed = false;
    SceneColorEncoding OutputEncoding = SceneColorEncoding::Unknown;
    EffectPassBarrierExecution LastBarrierExecution;
    bool Ready = false;
    std::string Reason = "TAA pass is not initialized";
};

TemporalAaPass::TemporalAaPass() : mImpl(std::make_unique<Impl>()) {}
TemporalAaPass::~TemporalAaPass() { Shutdown(); }

bool TemporalAaPass::Initialize(VkPhysicalDevice physical, VkDevice device,
                                NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)physical;
    mImpl->Reason = "NRI TAA support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physical, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("RGBA16F TAA storage is unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI TAA device is unavailable");

        const std::array<nri::DescriptorRangeDesc, 5> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER}}};
        nri::DescriptorSetDesc setDesc{};
        setDesc.registerSpace = 0;
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc rootConstant{};
        rootConstant.registerIndex = 0;
        rootConstant.size = sizeof(TemporalPush);
        rootConstant.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layoutDesc{};
        layoutDesc.rootConstants = &rootConstant;
        layoutDesc.rootConstantNum = 1;
        layoutDesc.descriptorSets = &setDesc;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layoutDesc, mImpl->PipelineLayout) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI TAA pipeline layout creation failed");

        const std::vector<uint32_t> shader = Compile(interop.Shaders());
        nri::ComputePipelineDesc pipelineDesc{};
        pipelineDesc.pipelineLayout = mImpl->PipelineLayout;
        pipelineDesc.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipelineDesc.shader.bytecode = shader.data();
        pipelineDesc.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipelineDesc, mImpl->Pipeline) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI TAA pipeline creation failed");

        nri::SamplerDesc samplerDesc{};
        samplerDesc.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR,
                               nri::Filter::NEAREST};
        samplerDesc.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, samplerDesc, mImpl->Sampler) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI TAA sampler creation failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool TemporalAaPass::Configure(uint32_t width, uint32_t height) {
    if (!mImpl->Ready || width == 0U || height == 0U) return false;
    if (mImpl->Width == width && mImpl->Height == height) return true;
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0U && vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    InvalidateScreenResources();
    try {
        for (uint32_t i = 0; i < mImpl->Images.size(); ++i) {
            NriOwnedTexture2D history;
            if (!mImpl->Interop->CreateOwnedTexture2D(
                    width, height, VK_FORMAT_R16G16B16A16_SFLOAT, history))
                throw std::runtime_error("NRI TAA history allocation failed");
            mImpl->Images[i] = history.Image;
            mImpl->Views[i] = history.SampledView;
            mImpl->StorageViews[i] = history.StorageView;
        }
        nri::Device* nriDevice = NriInteropAccess::Device(*mImpl->Interop);
        nri::DescriptorPoolDesc poolDesc{};
        poolDesc.descriptorSetMaxNum = kFrameSlots;
        poolDesc.samplerMaxNum = kFrameSlots;
        poolDesc.textureMaxNum = 3U * kFrameSlots;
        poolDesc.storageTextureMaxNum = kFrameSlots;
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, poolDesc, mImpl->Pool) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI TAA descriptor pool creation failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->PipelineLayout, 0,
                mImpl->Sets.data(), kFrameSlots, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI TAA descriptor allocation failed");
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            const nri::UpdateDescriptorRangeDesc samplerUpdate{
                mImpl->Sets[slot], 4, 0, &mImpl->Sampler, 1};
            mImpl->Core->UpdateDescriptorRanges(&samplerUpdate, 1);
        }
        mImpl->Width = width;
        mImpl->Height = height;
        mImpl->ContentValid = {};
        mImpl->HistoryStates.Clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool TemporalAaPass::Execute(
    VkCommandBuffer command, uint32_t frameSlot, uint64_t renderedFrameId,
    VkImage currentColor, VkFormat currentColorFormat,
    VkImage motionSurface, VkFormat motionFormat, bool historyValid,
    float historyWeight, float clampExpansion, float sharpness,
    SceneColorEncoding inputEncoding,
    const EffectPassBarrierPlan& barrierPlan) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::TemporalColor);
    if (currentColor == VK_NULL_HANDLE || motionSurface == VK_NULL_HANDLE ||
        mImpl->Width == 0U ||
        !IsKnownSceneColorEncoding(inputEncoding) ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed())
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)command; (void)frameSlot; (void)renderedFrameId;
    (void)currentColorFormat; (void)motionFormat; (void)historyValid;
    (void)historyWeight; (void)clampExpansion; (void)sharpness;
    (void)inputEncoding;
    (void)barrierPlan;
    return false;
#else
    nri::CommandBuffer* nriCommand =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (nriCommand == nullptr) return false;
    if (!mImpl->Interop->WrapTexture(
            currentColor, currentColorFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT, mImpl->Width, mImpl->Height) ||
        !mImpl->Interop->WrapTexture(
            motionSurface, motionFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT, mImpl->Width, mImpl->Height))
        return false;

    const uint32_t write = static_cast<uint32_t>(renderedFrameId & 1U);
    const uint32_t read = 1U - write;
    const uint32_t slot = frameSlot % kFrameSlots;
    mImpl->HistoryUsed = historyValid && mImpl->ContentValid[read];
    (void)command;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    ResourceTransition readTransition{};
    const auto writeTransition = mImpl->HistoryStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[write]),
        {outputBarrier->DispatchAccess, 0});
    std::array<NriTextureTransitionDesc, 2> beginTransitions{};
    uint32_t beginTransitionCount = 0U;
    if (mImpl->HistoryUsed) {
        readTransition = mImpl->HistoryStates.PlanTransition(
            reinterpret_cast<uintptr_t>(mImpl->Images[read]),
            {ResourceAccess::ShaderRead, 0});
        beginTransitions[beginTransitionCount++] = {
            mImpl->Images[read], readTransition};
    }
    beginTransitions[beginTransitionCount++] = {
        mImpl->Images[write], writeTransition};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, beginTransitions.data(),
            beginTransitionCount))
        return false;
    if (mImpl->HistoryUsed) {
        mImpl->LastBarrierExecution.RecordPrivateTransition(readTransition);
        mImpl->HistoryStates.Commit(readTransition);
    }
    mImpl->LastBarrierExecution.RecordGraphTransition(writeTransition);
    mImpl->HistoryStates.Commit(writeTransition);

    nri::Descriptor* current = NriInteropAccess::TextureView(
        *mImpl->Interop, currentColor, false);
    nri::Descriptor* motion = NriInteropAccess::TextureView(
        *mImpl->Interop, motionSurface, false);
    nri::Descriptor* history = mImpl->HistoryUsed
        ? NriInteropAccess::TextureView(*mImpl->Interop,
                                       mImpl->Images[read], false)
        : current;
    nri::Descriptor* output = NriInteropAccess::TextureView(
        *mImpl->Interop, mImpl->Images[write], true);
    if (current == nullptr || motion == nullptr || history == nullptr ||
        output == nullptr)
        return false;
    const std::array<nri::UpdateDescriptorRangeDesc, 4> updates{{
        {mImpl->Sets[slot], 0, 0, &current, 1},
        {mImpl->Sets[slot], 1, 0, &motion, 1},
        {mImpl->Sets[slot], 2, 0, &history, 1},
        {mImpl->Sets[slot], 3, 0, &output, 1}}};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));

    const TemporalPush push{
        mImpl->Width, mImpl->Height, 1.0F / mImpl->Width,
        1.0F / mImpl->Height, historyWeight, clampExpansion, sharpness,
        mImpl->HistoryUsed ? 1U : 0U};
    mImpl->Core->CmdSetDescriptorPool(*nriCommand, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *nriCommand, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*nriCommand, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc setDesc{0, mImpl->Sets[slot]};
    mImpl->Core->CmdSetDescriptorSet(*nriCommand, setDesc);
    const nri::SetRootConstantsDesc rootConstants{0, &push, sizeof(push)};
    mImpl->Core->CmdSetRootConstants(*nriCommand, rootConstants);
    const nri::DispatchDesc dispatch{
        (mImpl->Width + 7U) / 8U, (mImpl->Height + 7U) / 8U, 1U};
    mImpl->Core->CmdDispatch(*nriCommand, dispatch);

    const auto finishTransition = mImpl->HistoryStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[write]),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Images[write],
            finishTransition))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(finishTransition);
    mImpl->HistoryStates.Commit(finishTransition);
    mImpl->NriBarriersUsed = true;
    mImpl->ContentValid[write] = true;
    mImpl->OutputIndex = write;
    mImpl->OutputEncoding = inputEncoding;
    return true;
#endif
}

void TemporalAaPass::ResetHistory() {
    mImpl->ContentValid = {};
    mImpl->HistoryUsed = false;
}

void TemporalAaPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->Pool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
#endif
    if (mImpl->Interop != nullptr)
        for (VkImage image : mImpl->Images)
            mImpl->Interop->DestroyOwnedTexture(image);
#ifdef ENABLE_OOT3D_NRI
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
    mImpl->Images = {};
    mImpl->Views = {};
    mImpl->StorageViews = {};
    mImpl->ContentValid = {};
    mImpl->HistoryStates.Clear();
    mImpl->Width = mImpl->Height = 0;
    mImpl->OutputIndex = 0;
    mImpl->HistoryUsed = false;
    mImpl->NriBarriersUsed = false;
    mImpl->OutputEncoding = SceneColorEncoding::Unknown;
    mImpl->LastBarrierExecution = {};
}

void TemporalAaPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "TAA pass is not initialized";
}

bool TemporalAaPass::Available() const { return mImpl->Ready; }
VkImage TemporalAaPass::OutputImage() const {
    return mImpl->Images[mImpl->OutputIndex];
}
std::array<VkImage, 2> TemporalAaPass::OutputImages() const {
    return mImpl->Images;
}
VkImageView TemporalAaPass::OutputView() const {
    return mImpl->Views[mImpl->OutputIndex];
}
bool TemporalAaPass::HistoryUsedLastExecute() const {
    return mImpl->HistoryUsed;
}
bool TemporalAaPass::HistoryOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Images[0]) &&
           mImpl->Interop->OwnsTexture(mImpl->Images[1]);
}
bool TemporalAaPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Ready && mImpl->Pipeline != nullptr &&
           mImpl->Pool != nullptr && mImpl->Sampler != nullptr;
#else
    return false;
#endif
}
bool TemporalAaPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
SceneColorEncoding TemporalAaPass::OutputEncoding() const {
    return mImpl->OutputEncoding;
}
const EffectPassBarrierExecution& TemporalAaPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& TemporalAaPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
