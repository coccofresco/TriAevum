#include "fast/oot3d/hiz_reflection_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/hiz_reflection.h"
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
struct ReflectionPush {
    uint32_t Width, Height;
    float InvWidth, InvHeight, Strength, MaxDistance, Thickness, EdgeFade;
    uint32_t MaxSteps, HiZMipCount;
    float ProjectionScaleX, ProjectionScaleY;
    float ProjectionOffsetX, ProjectionOffsetY;
    float NearPlane, FarPlane, RoughnessBias, NormalSigma;
    uint32_t SurfaceOrientation;
};
static_assert(sizeof(ReflectionPush) == 76U);

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders, const std::string& source, const char* name) {
    return shaders.Resolve(source, Renderer::SpirvStage::Compute, name);
}
} // namespace

struct HiZReflectionPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* RayPipeline = nullptr;
    nri::Pipeline* FilterPipeline = nullptr;
    nri::Descriptor* LinearSampler = nullptr;
    nri::Descriptor* NearestSampler = nullptr;
    nri::DescriptorPool* DescriptorPool = nullptr;
    nri::DescriptorSet* RaySet = nullptr;
    nri::DescriptorSet* FilterSet = nullptr;
#endif
    std::array<VkImage, 2> Images{};
    std::array<VkImageView, 2> Views{};
    std::array<VkImage, 4> Inputs{};
    std::array<VkFormat, 4> InputFormats{};
    uint32_t Width = 0, Height = 0;
    bool Initialized = false, NriBarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker PrivateStates;
    ResourceStateTracker* OutputStates = nullptr;
    std::string Reason = "SSR pass is not initialized";
};

HiZReflectionPass::HiZReflectionPass() : mImpl(std::make_unique<Impl>()) {}
HiZReflectionPass::~HiZReflectionPass() { Shutdown(); }

bool HiZReflectionPass::Initialize(VkPhysicalDevice physicalDevice,
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
    mImpl->Reason = "NRI SSR support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("RGBA16F SSR storage is unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI SSR device is unavailable");
        const std::array<nri::DescriptorRangeDesc, 7> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {5, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER},
            {6, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER}}};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(ReflectionPush);
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
            throw std::runtime_error("NRI SSR pipeline layout failed");
        const auto createPipeline = [&](const std::string& source,
                                        const char* name,
                                        nri::Pipeline*& output) {
            const auto shader = Compile(interop.Shaders(), source, name);
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = mImpl->PipelineLayout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = shader.data();
            pipeline.shader.size = shader.size() * sizeof(uint32_t);
            if (mImpl->Core->CreateComputePipeline(
                    *nriDevice, pipeline, output) != nri::Result::SUCCESS)
                throw std::runtime_error(std::string("NRI ") + name +
                                         " pipeline failed");
        };
        createPipeline(BuildHiZReflectionComputeShader(),
                       "oot3d_hiz_reflection.comp", mImpl->RayPipeline);
        createPipeline(BuildHiZReflectionBilateralFilterShader(),
                       "oot3d_hiz_reflection_filter.comp",
                       mImpl->FilterPipeline);
        const auto createSampler = [&](nri::Filter filter,
                                       nri::Descriptor*& output) {
            nri::SamplerDesc sampler{};
            sampler.filters = {filter, filter, nri::Filter::NEAREST};
            sampler.mipMax = 16.0F;
            sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE};
            if (mImpl->Core->CreateSampler(*nriDevice, sampler, output) !=
                nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR sampler failed");
        };
        createSampler(nri::Filter::LINEAR, mImpl->LinearSampler);
        createSampler(nri::Filter::NEAREST, mImpl->NearestSampler);
        mImpl->Initialized = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool HiZReflectionPass::Configure(
    const NriEffectGraphTransientImageBinding& output,
    VkImage colorImage, VkFormat colorFormat,
    VkImage hiZImage, VkFormat hiZFormat,
    VkImage normalImage, VkFormat normalFormat,
    VkImage materialImage, VkFormat materialFormat) {
    const std::array<VkImage, 4> inputs{
        colorImage, hiZImage, normalImage, materialImage};
    const std::array<VkFormat, 4> formats{
        colorFormat, hiZFormat, normalFormat, materialFormat};
    if (!mImpl->Initialized || !output.Valid() ||
        output.Resource != EffectResource::ReflectionColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    for (VkImage image : inputs)
        if (image == VK_NULL_HANDLE) return false;
    if (mImpl->Width == output.Width && mImpl->Height == output.Height &&
        mImpl->Images[1] == output.Texture.Image &&
        mImpl->OutputStates == output.StateTracker &&
        mImpl->Inputs == inputs && mImpl->InputFormats == formats) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0U && vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    InvalidateScreenResources();
    try {
        NriOwnedTexture2D privateOutput;
        if (!mImpl->Interop->CreateOwnedTexture2D(
                output.Width, output.Height,
                VK_FORMAT_R16G16B16A16_SFLOAT, privateOutput)) {
            throw std::runtime_error("NRI SSR scratch allocation failed");
        }
        mImpl->Images[0] = privateOutput.Image;
        mImpl->Views[0] = privateOutput.SampledView;
        mImpl->Images[1] = output.Texture.Image;
        mImpl->Views[1] = output.Texture.SampledView;
        mImpl->OutputStates = output.StateTracker;
        std::array<nri::Descriptor*, 4> inputViews{};
        for (uint32_t i = 0; i < inputs.size(); ++i) {
            if (!mImpl->Interop->WrapTexture(
                    inputs[i], formats[i], VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    output.Width, output.Height))
                throw std::runtime_error("NRI SSR input wrapping failed");
            inputViews[i] = NriInteropAccess::TextureView(
                *mImpl->Interop, inputs[i], false);
            if (inputViews[i] == nullptr)
                throw std::runtime_error("NRI SSR input view failed");
        }
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = 2;
        pool.samplerMaxNum = 4;
        pool.textureMaxNum = 8;
        pool.storageTextureMaxNum = 2;
        nri::Device* device = NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *device, pool, mImpl->DescriptorPool) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI SSR descriptor pool failed");
        std::array<nri::DescriptorSet*, 2> sets{};
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->DescriptorPool, *mImpl->PipelineLayout, 0,
                sets.data(), 2, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI SSR descriptor sets failed");
        mImpl->RaySet = sets[0];
        mImpl->FilterSet = sets[1];
        const auto updateSet = [&](nri::DescriptorSet* set,
                                   const std::array<nri::Descriptor*, 4>& sources,
                                   VkImage destinationImage) {
            nri::Descriptor* destination = NriInteropAccess::TextureView(
                *mImpl->Interop, destinationImage, true);
            if (destination == nullptr)
                throw std::runtime_error("NRI SSR storage view failed");
            std::array<nri::UpdateDescriptorRangeDesc, 7> updates{};
            for (uint32_t i = 0; i < sources.size(); ++i)
                updates[i] = {set, i, 0, &sources[i], 1};
            updates[4] = {set, 4, 0, &destination, 1};
            updates[5] = {set, 5, 0, &mImpl->LinearSampler, 1};
            updates[6] = {set, 6, 0, &mImpl->NearestSampler, 1};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
        };
        updateSet(mImpl->RaySet, inputViews, mImpl->Images[0]);
        std::array<nri::Descriptor*, 4> filterInputs{
            NriInteropAccess::TextureView(*mImpl->Interop,
                                          mImpl->Images[0], false),
            inputViews[1], inputViews[2], inputViews[3]};
        updateSet(mImpl->FilterSet, filterInputs, mImpl->Images[1]);
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        mImpl->Inputs = inputs;
        mImpl->InputFormats = formats;
        mImpl->PrivateStates.Clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool HiZReflectionPass::Execute(VkCommandBuffer commandBuffer,
                                uint32_t frameSlot,
                                uint32_t hiZMipCount,
                                const PerspectiveViewState& view,
                                const EffectsSettings& settings,
                                const EffectPassBarrierPlan& barrierPlan,
                                std::optional<PicaSurfaceCoordinates> coordinates) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::ReflectionColor);
    if (!barrierPlan.Valid() || mImpl->OutputStates == nullptr ||
        outputBarrier == nullptr ||
        !outputBarrier->Managed()) {
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer; (void)frameSlot; (void)hiZMipCount;
    (void)view; (void)settings;
    (void)barrierPlan;
    return false;
#else
    if (mImpl->RaySet == nullptr || mImpl->FilterSet == nullptr) return false;
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr) return false;
    const ReflectionPush push{
        mImpl->Width, mImpl->Height, 1.0F / mImpl->Width, 1.0F / mImpl->Height,
        settings.ReflectionStrength, settings.ReflectionMaxDistance,
        settings.ReflectionThickness, settings.ReflectionEdgeFade,
        settings.ReflectionMaxSteps, hiZMipCount,
        view.Projection[0], view.Projection[5], view.Projection[8],
        view.Projection[9], view.NearPlane, view.FarPlane,
        settings.ReflectionRoughnessBias, 16.0F,
        coordinates ? coordinates->ShaderFlags() : 0U};
    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::ReflectionColor);
    const auto rawWrite = mImpl->PrivateStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[0]),
        {ResourceAccess::ComputeWrite, 0});
    const auto filteredWrite = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[1]),
        {outputBarrier->DispatchAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> toCompute{{
        {mImpl->Images[0], rawWrite},
        {mImpl->Images[1], filteredWrite}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, toCompute.data(),
            static_cast<uint32_t>(toCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordPrivateTransition(rawWrite);
    mImpl->LastBarrierExecution.RecordGraphTransition(filteredWrite);
    mImpl->PrivateStates.Commit(rawWrite);
    mImpl->OutputStates->Commit(filteredWrite);
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->DescriptorPool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    const auto dispatch = [&](nri::Pipeline* pipeline,
                              nri::DescriptorSet* descriptorSet) {
        mImpl->Core->CmdSetPipeline(*command, *pipeline);
        const nri::SetDescriptorSetDesc set{0, descriptorSet};
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const nri::SetRootConstantsDesc constants{0, &push, sizeof(push)};
        mImpl->Core->CmdSetRootConstants(*command, constants);
        const nri::DispatchDesc groups{
            (mImpl->Width + 7U) / 8U, (mImpl->Height + 7U) / 8U, 1U};
        mImpl->Core->CmdDispatch(*command, groups);
    };
    dispatch(mImpl->RayPipeline, mImpl->RaySet);
    const auto rawRead = mImpl->PrivateStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[0]),
        {ResourceAccess::ShaderRead, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Images[0], rawRead))
        return false;
    mImpl->LastBarrierExecution.RecordPrivateTransition(rawRead);
    mImpl->PrivateStates.Commit(rawRead);
    dispatch(mImpl->FilterPipeline, mImpl->FilterSet);
    const auto filteredRead = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[1]),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Images[1], filteredRead))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(filteredRead);
    mImpl->OutputStates->Commit(filteredRead);
    mImpl->NriBarriersUsed = true;
    return true;
#endif
}

void HiZReflectionPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->DescriptorPool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->DescriptorPool);
    mImpl->DescriptorPool = nullptr;
    mImpl->RaySet = nullptr;
    mImpl->FilterSet = nullptr;
#endif
    if (mImpl->Interop != nullptr)
        mImpl->Interop->DestroyOwnedTexture(mImpl->Images[0]);
    mImpl->Images = {};
    mImpl->Views = {};
    mImpl->Inputs = {};
    mImpl->InputFormats = {};
    mImpl->Width = mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = {};
    mImpl->PrivateStates.Clear();
    mImpl->OutputStates = nullptr;
}

void HiZReflectionPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->LinearSampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->LinearSampler);
        if (mImpl->NearestSampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->NearestSampler);
        if (mImpl->RayPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->RayPipeline);
        if (mImpl->FilterPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->FilterPipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "SSR pass is not initialized";
}

bool HiZReflectionPass::Available() const { return mImpl->Initialized; }
VkImage HiZReflectionPass::OutputImage() const { return mImpl->Images[1]; }
VkImageView HiZReflectionPass::OutputView() const { return mImpl->Views[1]; }
bool HiZReflectionPass::OutputsOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Images[0]) &&
           mImpl->Interop->OwnsTexture(mImpl->Images[1]);
}
bool HiZReflectionPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->RayPipeline != nullptr &&
           mImpl->FilterPipeline != nullptr && mImpl->DescriptorPool != nullptr;
#else
    return false;
#endif
}
bool HiZReflectionPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
const EffectPassBarrierExecution& HiZReflectionPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& HiZReflectionPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
