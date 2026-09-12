#include "fast/oot3d/hiz_depth_pyramid_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {
struct HiZPushConstants {
    uint32_t SourceWidth = 0, SourceHeight = 0;
    uint32_t DestinationWidth = 0, DestinationHeight = 0;
    float NearPlane = 0.0F, FarPlane = 0.0F;
    uint32_t Convention = 0, SourceIsRaw = 0;
};
static_assert(sizeof(HiZPushConstants) == 32U);

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders) {
    return shaders.Resolve(BuildHiZReductionComputeShader(), Renderer::SpirvStage::Compute, "oot3d_hiz_reduce.comp");
}
} // namespace

struct HiZDepthPyramidPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* DescriptorPool = nullptr;
    std::vector<nri::DescriptorSet*> DescriptorSets;
    std::vector<nri::Descriptor*> MipViews;
#endif
    VkImage Image = VK_NULL_HANDLE;
    VkImageView FullView = VK_NULL_HANDLE;
    HiZPyramidLayout Layout;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;
    bool Initialized = false;
    bool NriBarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker* WholeImageState = nullptr;
    ResourceStateTracker* MipStates = nullptr;
    uint32_t MipStateCount = 0U;
    std::vector<ResourceTransition> WriteTransitions;
#ifdef ENABLE_OOT3D_NRI
    std::vector<NriTextureTransitionDesc> ToCompute;
#endif
    std::string Reason = "Hi-Z pass is not initialized";

    [[nodiscard]] ResourceStateTracker* StateTrackerForMip(
        uint32_t mip) const noexcept {
        if (mip >= Layout.Mips.size()) return nullptr;
        if (Layout.Mips.size() == 1U) return WholeImageState;
        return MipStates != nullptr && MipStateCount == Layout.Mips.size()
            ? &MipStates[mip]
            : nullptr;
    }
};

HiZDepthPyramidPass::HiZDepthPyramidPass() : mImpl(std::make_unique<Impl>()) {}
HiZDepthPyramidPass::~HiZDepthPyramidPass() { Shutdown(); }

bool HiZDepthPyramidPass::Initialize(VkPhysicalDevice physicalDevice,
                                     VkDevice device,
                                     NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)physicalDevice;
    mImpl->Reason = "NRI Hi-Z support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R32_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("R32F Hi-Z storage is unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI Hi-Z device is unavailable");
        const std::array<nri::DescriptorRangeDesc, 3> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER}}};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(HiZPushConstants);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->PipelineLayout) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI Hi-Z pipeline layout failed");
        const auto shader = Compile(interop.Shaders());
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI Hi-Z pipeline failed");
        nri::SamplerDesc sampler{};
        sampler.filters = {nri::Filter::NEAREST, nri::Filter::NEAREST,
                           nri::Filter::NEAREST};
        sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE};
        sampler.mipMax = 16.0F;
        if (mImpl->Core->CreateSampler(
                *nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI Hi-Z sampler failed");
        mImpl->Initialized = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool HiZDepthPyramidPass::Configure(
    const NriEffectGraphTransientImageBinding& output,
    VkImage depthImage, VkFormat depthFormat) {
    const VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    const uint32_t expectedMipCount =
        ResolveHiZMipCount(output.Width, output.Height);
    if (!mImpl->Initialized || !output.Valid() ||
        output.Resource != EffectResource::HierarchicalDepth ||
        output.Format != VK_FORMAT_R32_SFLOAT || output.Layers != 1U ||
        output.Samples != VK_SAMPLE_COUNT_1_BIT ||
        (output.Usage & requiredUsage) != requiredUsage ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.MipLevels != expectedMipCount ||
        depthImage == VK_NULL_HANDLE) {
        mImpl->Reason =
            "Hi-Z graph output contract is incomplete or incompatible";
        return false;
    }
    for (uint32_t mip = 0U; mip < output.MipLevels; ++mip) {
        if (output.StateTrackerForMip(mip) == nullptr) {
            mImpl->Reason = "Hi-Z graph output has incomplete mip state";
            return false;
        }
    }
#ifndef ENABLE_OOT3D_NRI
    (void)output;
    (void)depthFormat;
    return false;
#else
    const bool outputMatches = !mImpl->Layout.Mips.empty() &&
        mImpl->Layout.Mips.front().Width == output.Width &&
        mImpl->Layout.Mips.front().Height == output.Height &&
        mImpl->Layout.Mips.size() == output.MipLevels &&
        mImpl->Image == output.Texture.Image &&
        mImpl->FullView == output.Texture.SampledView &&
        mImpl->WholeImageState == output.StateTracker &&
        mImpl->MipStates == output.MipStateTrackers &&
        mImpl->MipStateCount == output.MipStateTrackerCount;
    if (outputMatches && mImpl->DepthImage == depthImage &&
        mImpl->DepthFormat == depthFormat) {
        mImpl->Reason.clear();
        return true;
    }
    if (mImpl->Image != VK_NULL_HANDLE &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    if (outputMatches) {
        if (!mImpl->Interop->WrapTexture(
                depthImage, depthFormat, VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                output.Width, output.Height))
            return false;
        nri::Descriptor* depth = NriInteropAccess::TextureView(
            *mImpl->Interop, depthImage, false);
        if (depth == nullptr) return false;
        const nri::UpdateDescriptorRangeDesc update{
            mImpl->DescriptorSets.front(), 0, 0, &depth, 1};
        mImpl->Core->UpdateDescriptorRanges(&update, 1);
        mImpl->DepthImage = depthImage;
        mImpl->DepthFormat = depthFormat;
        mImpl->Reason.clear();
        return true;
    }
    InvalidateScreenResources();
    try {
        mImpl->Layout = BuildHiZPyramidLayout(
            output.Width, output.Height);
        const uint32_t mipCount = static_cast<uint32_t>(mImpl->Layout.Mips.size());
        mImpl->Image = output.Texture.Image;
        mImpl->FullView = output.Texture.SampledView;
        mImpl->WholeImageState = output.StateTracker;
        mImpl->MipStates = output.MipStateTrackers;
        mImpl->MipStateCount = output.MipStateTrackerCount;
        if (!mImpl->Interop->WrapTexture(
                depthImage, depthFormat, VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                output.Width, output.Height))
            throw std::runtime_error("NRI Hi-Z depth wrapping failed");
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = mipCount;
        pool.samplerMaxNum = mipCount;
        pool.textureMaxNum = mipCount;
        pool.storageTextureMaxNum = mipCount;
        nri::Device* device = NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *device, pool, mImpl->DescriptorPool) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI Hi-Z descriptor pool failed");
        mImpl->DescriptorSets.resize(mipCount);
        mImpl->MipViews.reserve(mipCount * 2U - 1U);
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->DescriptorPool, *mImpl->PipelineLayout, 0,
                mImpl->DescriptorSets.data(), mipCount, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI Hi-Z descriptor sets failed");
        nri::Descriptor* depth = NriInteropAccess::TextureView(
            *mImpl->Interop, depthImage, false);
        for (uint32_t mip = 0; mip < mipCount; ++mip) {
            nri::Descriptor* source = mip == 0U ? depth
                : NriInteropAccess::CreateTextureView(
                      *mImpl->Interop, mImpl->Image, false, mip - 1U);
            if (source == nullptr)
                throw std::runtime_error("NRI Hi-Z source mip view failed");
            if (mip != 0U) mImpl->MipViews.push_back(source);
            nri::Descriptor* destination = NriInteropAccess::CreateTextureView(
                *mImpl->Interop, mImpl->Image, true, mip);
            if (destination == nullptr)
                throw std::runtime_error(
                    "NRI Hi-Z destination mip view failed");
            mImpl->MipViews.push_back(destination);
            const std::array<nri::UpdateDescriptorRangeDesc, 3> updates{{
                {mImpl->DescriptorSets[mip], 0, 0, &source, 1},
                {mImpl->DescriptorSets[mip], 1, 0, &destination, 1},
                {mImpl->DescriptorSets[mip], 2, 0, &mImpl->Sampler, 1}}};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
        }
        mImpl->DepthImage = depthImage;
        mImpl->DepthFormat = depthFormat;
        mImpl->WriteTransitions.resize(mipCount);
        mImpl->ToCompute.resize(mipCount);
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool HiZDepthPyramidPass::Execute(VkCommandBuffer commandBuffer,
                                  uint32_t frameSlot, float nearPlane,
                                  float farPlane, DepthConvention convention,
                                  const EffectPassBarrierPlan& barrierPlan) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::HierarchicalDepth);
    if (MipCount() == 0U || nearPlane <= 0.0F || farPlane <= nearPlane ||
        !barrierPlan.Valid() || outputBarrier == nullptr ||
        !outputBarrier->Managed())
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer; (void)frameSlot; (void)convention;
    (void)barrierPlan;
    return false;
#else
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr) return false;
    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::HierarchicalDepth, MipCount());
    auto& writeTransitions = mImpl->WriteTransitions;
    auto& toCompute = mImpl->ToCompute;
    for (uint32_t mip = 0; mip < MipCount(); ++mip) {
        ResourceStateTracker* states = mImpl->StateTrackerForMip(mip);
        if (states == nullptr) return false;
        writeTransitions[mip] = states->PlanTransition(
            reinterpret_cast<uintptr_t>(mImpl->Image),
            {outputBarrier->DispatchAccess, 0});
        toCompute[mip] = {
            mImpl->Image, writeTransitions[mip], mip, 1};
    }
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, toCompute.data(),
            static_cast<uint32_t>(toCompute.size())))
        return false;
    for (uint32_t mip = 0; mip < MipCount(); ++mip) {
        mImpl->LastBarrierExecution.RecordGraphTransition(
            writeTransitions[mip]);
        mImpl->StateTrackerForMip(mip)->Commit(writeTransitions[mip]);
    }
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->DescriptorPool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    for (uint32_t mip = 0; mip < MipCount(); ++mip) {
        const auto source = mip == 0U ? mImpl->Layout.Mips[0]
                                      : mImpl->Layout.Mips[mip - 1U];
        const auto destination = mImpl->Layout.Mips[mip];
        const HiZPushConstants push{
            source.Width, source.Height, destination.Width, destination.Height,
            nearPlane, farPlane, static_cast<uint32_t>(convention),
            mip == 0U ? 1U : 0U};
        const nri::SetDescriptorSetDesc set{0, mImpl->DescriptorSets[mip]};
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const nri::SetRootConstantsDesc constants{0, &push, sizeof(push)};
        mImpl->Core->CmdSetRootConstants(*command, constants);
        const nri::DispatchDesc dispatch{
            (destination.Width + 7U) / 8U,
            (destination.Height + 7U) / 8U, 1U};
        mImpl->Core->CmdDispatch(*command, dispatch);
        ResourceStateTracker* states = mImpl->StateTrackerForMip(mip);
        if (states == nullptr) return false;
        const auto readable = states->PlanTransition(
            reinterpret_cast<uintptr_t>(mImpl->Image),
            {outputBarrier->CompletionAccess, 0});
        if (!NriInteropAccess::CmdTextureBarrier(
                *mImpl->Interop, frameSlot, mImpl->Image, readable, mip, 1))
            return false;
        mImpl->LastBarrierExecution.RecordGraphTransition(readable);
        states->Commit(readable);
    }
    mImpl->NriBarriersUsed = true;
    return true;
#endif
}

void HiZDepthPyramidPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->DescriptorPool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->DescriptorPool);
    mImpl->DescriptorPool = nullptr;
    mImpl->DescriptorSets.clear();
    if (mImpl->Interop != nullptr) {
        for (nri::Descriptor* view : mImpl->MipViews) {
            NriInteropAccess::DestroyTextureView(
                *mImpl->Interop, mImpl->Image, view);
        }
    }
    mImpl->MipViews.clear();
#endif
    mImpl->Image = VK_NULL_HANDLE;
    mImpl->FullView = VK_NULL_HANDLE;
    mImpl->DepthImage = VK_NULL_HANDLE;
    mImpl->DepthFormat = VK_FORMAT_UNDEFINED;
    mImpl->Layout = {};
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = {};
    mImpl->WholeImageState = nullptr;
    mImpl->MipStates = nullptr;
    mImpl->MipStateCount = 0U;
    mImpl->WriteTransitions.clear();
#ifdef ENABLE_OOT3D_NRI
    mImpl->ToCompute.clear();
#endif
}

void HiZDepthPyramidPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Sampler != nullptr) mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr) mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "Hi-Z pass is not initialized";
}

bool HiZDepthPyramidPass::Available() const { return mImpl->Initialized; }
VkImage HiZDepthPyramidPass::OutputImage() const { return mImpl->Image; }
VkImageView HiZDepthPyramidPass::OutputView() const { return mImpl->FullView; }
uint32_t HiZDepthPyramidPass::MipCount() const {
    return static_cast<uint32_t>(mImpl->Layout.Mips.size());
}
bool HiZDepthPyramidPass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr && mImpl->Interop->OwnsTexture(mImpl->Image);
}
bool HiZDepthPyramidPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->Pipeline != nullptr &&
           mImpl->DescriptorPool != nullptr && mImpl->Sampler != nullptr;
#else
    return false;
#endif
}
bool HiZDepthPyramidPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
const EffectPassBarrierExecution& HiZDepthPyramidPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& HiZDepthPyramidPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
