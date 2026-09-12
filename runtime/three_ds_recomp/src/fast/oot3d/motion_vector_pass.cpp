#include "fast/oot3d/motion_vector_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/motion_vectors.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {
constexpr uint32_t kFrameSlots = 2U;
struct alignas(16) MotionUniforms {
    std::array<float, 16> InverseCurrent{};
    std::array<float, 16> Previous{};
    std::array<float, 4> Eye{};
    std::array<float, 4> Forward{};
    std::array<float, 4> DepthState{};
    std::array<float, 4> JitterState{};
    std::array<uint32_t, 4> Extent{};
};
static_assert(sizeof(MotionUniforms) == 208U);

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders, const std::string& source) {
    return shaders.Resolve(source, Renderer::SpirvStage::Compute, "oot3d_motion_vectors.comp");
}
} // namespace

struct MotionVectorPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* DescriptorPool = nullptr;
    std::array<nri::DescriptorSet*, kFrameSlots> Sets{};
    std::array<nri::Buffer*, kFrameSlots> UniformBuffers{};
    std::array<nri::Descriptor*, kFrameSlots> UniformViews{};
    std::array<void*, kFrameSlots> UniformMapped{};
#endif
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkImageView StorageView = VK_NULL_HANDLE;
    VkImage ReactiveImage = VK_NULL_HANDLE;
    VkImageView ReactiveView = VK_NULL_HANDLE;
    VkImageView ReactiveStorageView = VK_NULL_HANDLE;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkImage MaterialGuideImage = VK_NULL_HANDLE;
    VkImage RigidMotionGuideImage = VK_NULL_HANDLE;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Initialized = false;
    bool NriBarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker* MotionOutputStates = nullptr;
    ResourceStateTracker* ReactiveOutputStates = nullptr;
    std::string Reason = "motion vector pass is not initialized";
};

MotionVectorPass::MotionVectorPass() : mImpl(std::make_unique<Impl>()) {}
MotionVectorPass::~MotionVectorPass() { Shutdown(); }

bool MotionVectorPass::Initialize(VkPhysicalDevice physicalDevice,
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
    mImpl->Reason = "NRI motion compute support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice,
            VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("RGBA16F motion storage is unavailable");
        vkGetPhysicalDeviceFormatProperties(physicalDevice,
            VK_FORMAT_R16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("R16F reactive storage is unavailable");

        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI motion compute device is unavailable");

        const std::array<nri::DescriptorRangeDesc, 7> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::CONSTANT_BUFFER,
             nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {5, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {6, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER}}};
        nri::DescriptorSetDesc setDesc{};
        setDesc.registerSpace = 0;
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::PipelineLayoutDesc layoutDesc{};
        layoutDesc.descriptorSets = &setDesc;
        layoutDesc.descriptorSetNum = 1;
        layoutDesc.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layoutDesc, mImpl->PipelineLayout) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion pipeline layout creation failed");

        const std::vector<uint32_t> shader =
            Compile(interop.Shaders(), BuildCameraMotionComputeShader());
        nri::ComputePipelineDesc pipelineDesc{};
        pipelineDesc.pipelineLayout = mImpl->PipelineLayout;
        pipelineDesc.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipelineDesc.shader.bytecode = shader.data();
        pipelineDesc.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipelineDesc, mImpl->Pipeline) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion pipeline creation failed");

        nri::SamplerDesc samplerDesc{};
        samplerDesc.filters = {nri::Filter::NEAREST, nri::Filter::NEAREST,
                               nri::Filter::NEAREST};
        samplerDesc.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, samplerDesc, mImpl->Sampler) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion sampler creation failed");

        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            nri::BufferDesc bufferDesc{};
            bufferDesc.size = sizeof(MotionUniforms);
            bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
            if (mImpl->Core->CreateCommittedBuffer(
                    *nriDevice, nri::MemoryLocation::HOST_UPLOAD, 0.0F,
                    bufferDesc, mImpl->UniformBuffers[slot]) !=
                nri::Result::SUCCESS)
                throw std::runtime_error("NRI motion uniform creation failed");
            nri::BufferViewDesc viewDesc{};
            viewDesc.buffer = mImpl->UniformBuffers[slot];
            viewDesc.type = nri::BufferView::CONSTANT_BUFFER;
            viewDesc.size = sizeof(MotionUniforms);
            if (mImpl->Core->CreateBufferView(
                    viewDesc, mImpl->UniformViews[slot]) !=
                nri::Result::SUCCESS)
                throw std::runtime_error("NRI motion uniform view creation failed");
            mImpl->UniformMapped[slot] = mImpl->Core->MapBuffer(
                *mImpl->UniformBuffers[slot], 0, sizeof(MotionUniforms));
            if (mImpl->UniformMapped[slot] == nullptr)
                throw std::runtime_error("NRI motion uniform mapping failed");
        }

        nri::DescriptorPoolDesc poolDesc{};
        poolDesc.descriptorSetMaxNum = kFrameSlots;
        poolDesc.samplerMaxNum = kFrameSlots;
        poolDesc.constantBufferMaxNum = kFrameSlots;
        poolDesc.textureMaxNum = 3U * kFrameSlots;
        poolDesc.storageTextureMaxNum = 2U * kFrameSlots;
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, poolDesc, mImpl->DescriptorPool) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion descriptor pool creation failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->DescriptorPool, *mImpl->PipelineLayout, 0,
                mImpl->Sets.data(), kFrameSlots, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion descriptor allocation failed");
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
                {mImpl->Sets[slot], 3, 0, &mImpl->UniformViews[slot], 1},
                {mImpl->Sets[slot], 6, 0, &mImpl->Sampler, 1}}};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
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

bool MotionVectorPass::Configure(
    const NriEffectGraphTransientImageBinding& motionOutput,
    const NriEffectGraphTransientImageBinding& reactiveOutput,
    VkImage depthImage, VkFormat depthFormat,
    VkImage materialGuideImage, VkFormat materialGuideFormat,
    VkImage rigidMotionGuideImage, VkFormat rigidMotionGuideFormat) {
    const auto validOutput = [](
        const NriEffectGraphTransientImageBinding& output,
        EffectResource resource, VkFormat format) {
        return output.Valid() && output.Resource == resource &&
               output.Format == format &&
               output.Texture.SampledView != VK_NULL_HANDLE &&
               output.Texture.StorageView != VK_NULL_HANDLE &&
               (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                                VK_IMAGE_USAGE_STORAGE_BIT)) ==
                   (VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT);
    };
    if (!mImpl->Initialized ||
        !validOutput(motionOutput, EffectResource::MotionVectors,
                     VK_FORMAT_R16G16B16A16_SFLOAT) ||
        !validOutput(reactiveOutput, EffectResource::ReactiveMask,
                     VK_FORMAT_R16_SFLOAT) ||
        motionOutput.Width != reactiveOutput.Width ||
        motionOutput.Height != reactiveOutput.Height ||
        motionOutput.Texture.Image == reactiveOutput.Texture.Image ||
        depthImage == VK_NULL_HANDLE || materialGuideImage == VK_NULL_HANDLE ||
        rigidMotionGuideImage == VK_NULL_HANDLE) {
        return false;
    }
    if (mImpl->Width == motionOutput.Width &&
        mImpl->Height == motionOutput.Height &&
        mImpl->Image == motionOutput.Texture.Image &&
        mImpl->ReactiveImage == reactiveOutput.Texture.Image &&
        mImpl->MotionOutputStates == motionOutput.StateTracker &&
        mImpl->ReactiveOutputStates == reactiveOutput.StateTracker &&
        mImpl->DepthImage == depthImage &&
        mImpl->MaterialGuideImage == materialGuideImage &&
        mImpl->RigidMotionGuideImage == rigidMotionGuideImage) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)depthFormat; (void)materialGuideFormat; (void)rigidMotionGuideFormat;
    return false;
#else
    if (mImpl->Width != 0 && vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    InvalidateScreenResources();
    try {
        if (!mImpl->Interop->WrapTexture(
                depthImage, depthFormat, VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                motionOutput.Width, motionOutput.Height) ||
            !mImpl->Interop->WrapTexture(
                materialGuideImage, materialGuideFormat, VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                motionOutput.Width, motionOutput.Height) ||
            !mImpl->Interop->WrapTexture(
                rigidMotionGuideImage, rigidMotionGuideFormat,
                VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                motionOutput.Width, motionOutput.Height)) {
            throw std::runtime_error("NRI motion input wrapping failed");
        }
        mImpl->Image = motionOutput.Texture.Image;
        mImpl->View = motionOutput.Texture.SampledView;
        mImpl->StorageView = motionOutput.Texture.StorageView;
        mImpl->ReactiveImage = reactiveOutput.Texture.Image;
        mImpl->ReactiveView = reactiveOutput.Texture.SampledView;
        mImpl->ReactiveStorageView = reactiveOutput.Texture.StorageView;
        mImpl->MotionOutputStates = motionOutput.StateTracker;
        mImpl->ReactiveOutputStates = reactiveOutput.StateTracker;

        nri::Descriptor* depth =
            NriInteropAccess::TextureView(*mImpl->Interop, depthImage, false);
        nri::Descriptor* material = NriInteropAccess::TextureView(
            *mImpl->Interop, materialGuideImage, false);
        nri::Descriptor* output =
            NriInteropAccess::TextureView(*mImpl->Interop, mImpl->Image, true);
        nri::Descriptor* rigid = NriInteropAccess::TextureView(
            *mImpl->Interop, rigidMotionGuideImage, false);
        nri::Descriptor* reactive = NriInteropAccess::TextureView(
            *mImpl->Interop, mImpl->ReactiveImage, true);
        if (depth == nullptr || material == nullptr || output == nullptr ||
            rigid == nullptr || reactive == nullptr)
            throw std::runtime_error("NRI motion texture views are unavailable");
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            const std::array<nri::UpdateDescriptorRangeDesc, 5> updates{{
                {mImpl->Sets[slot], 0, 0, &depth, 1},
                {mImpl->Sets[slot], 1, 0, &material, 1},
                {mImpl->Sets[slot], 2, 0, &output, 1},
                {mImpl->Sets[slot], 4, 0, &rigid, 1},
                {mImpl->Sets[slot], 5, 0, &reactive, 1}}};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
        }
        mImpl->Width = motionOutput.Width;
        mImpl->Height = motionOutput.Height;
        mImpl->DepthImage = depthImage;
        mImpl->MaterialGuideImage = materialGuideImage;
        mImpl->RigidMotionGuideImage = rigidMotionGuideImage;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool MotionVectorPass::Execute(VkCommandBuffer commandBuffer,
                               uint32_t frameSlot,
                               const TemporalViewState& temporal,
                               const PerspectiveViewState& view,
                               DepthConvention convention,
                               const EffectPassBarrierPlan& barrierPlan,
                               std::optional<PicaSurfaceCoordinates> coordinates) {
    const uint32_t slot = frameSlot % kFrameSlots;
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer; (void)temporal; (void)view; (void)convention;
    (void)barrierPlan;
    return false;
#else
    const auto* motionBarrier = barrierPlan.Find(
        EffectResource::MotionVectors);
    const auto* reactiveBarrier = barrierPlan.Find(
        EffectResource::ReactiveMask);
    if (!barrierPlan.Valid() || motionBarrier == nullptr ||
        reactiveBarrier == nullptr || !motionBarrier->Managed() ||
        !reactiveBarrier->Managed() ||
        mImpl->MotionOutputStates == nullptr ||
        mImpl->ReactiveOutputStates == nullptr) {
        return false;
    }
    nri::CommandBuffer* nriCommand =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (mImpl->Sets[slot] == nullptr ||
        mImpl->UniformMapped[slot] == nullptr || nriCommand == nullptr)
        return false;
    std::array<float, 3> forward{view.At[0] - view.Eye[0],
                                 view.At[1] - view.Eye[1],
                                 view.At[2] - view.Eye[2]};
    const float length = std::sqrt(forward[0] * forward[0] +
                                   forward[1] * forward[1] +
                                   forward[2] * forward[2]);
    if (length <= 1.0e-6F) return false;
    for (float& value : forward) value /= length;
    MotionUniforms uniforms{};
    uniforms.InverseCurrent = temporal.InverseCurrentWorldToClip;
    uniforms.Previous = temporal.PreviousWorldToClip;
    uniforms.Eye = {view.Eye[0], view.Eye[1], view.Eye[2], 1.0F};
    uniforms.Forward = {forward[0], forward[1], forward[2], 0.0F};
    uniforms.DepthState = {view.NearPlane, view.FarPlane,
        static_cast<float>(convention), temporal.HistoryValid ? 1.0F : 0.0F};
    uniforms.JitterState = {
        temporal.CurrentJitterUv[0], temporal.CurrentJitterUv[1],
        temporal.PreviousJitterUv[0], temporal.PreviousJitterUv[1]};
    uniforms.Extent = {mImpl->Width, mImpl->Height,
        coordinates ? coordinates->ShaderFlags() : 0U, 0U};
    std::memcpy(mImpl->UniformMapped[slot], &uniforms, sizeof(uniforms));

    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    const auto motionWrite = mImpl->MotionOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Image),
        {motionBarrier->DispatchAccess, 0});
    const auto reactiveWrite = mImpl->ReactiveOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->ReactiveImage),
        {reactiveBarrier->DispatchAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> toCompute{{
        {mImpl->Image, motionWrite},
        {mImpl->ReactiveImage, reactiveWrite}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, toCompute.data(),
            static_cast<uint32_t>(toCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(motionWrite);
    mImpl->LastBarrierExecution.RecordGraphTransition(reactiveWrite);
    mImpl->MotionOutputStates->Commit(motionWrite);
    mImpl->ReactiveOutputStates->Commit(reactiveWrite);

    mImpl->Core->CmdSetDescriptorPool(*nriCommand, *mImpl->DescriptorPool);
    mImpl->Core->CmdSetPipelineLayout(
        *nriCommand, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*nriCommand, *mImpl->Pipeline);
    nri::SetDescriptorSetDesc setDesc{0, mImpl->Sets[slot]};
    mImpl->Core->CmdSetDescriptorSet(*nriCommand, setDesc);
    nri::DispatchDesc dispatch{};
    dispatch.x = (mImpl->Width + 7U) / 8U;
    dispatch.y = (mImpl->Height + 7U) / 8U;
    dispatch.z = 1U;
    mImpl->Core->CmdDispatch(*nriCommand, dispatch);

    const auto motionComplete = mImpl->MotionOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Image),
        {motionBarrier->CompletionAccess, 0});
    const auto reactiveComplete =
        mImpl->ReactiveOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->ReactiveImage),
        {reactiveBarrier->CompletionAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> fromCompute{{
        {mImpl->Image, motionComplete},
        {mImpl->ReactiveImage, reactiveComplete}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, fromCompute.data(),
            static_cast<uint32_t>(fromCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(motionComplete);
    mImpl->LastBarrierExecution.RecordGraphTransition(reactiveComplete);
    mImpl->MotionOutputStates->Commit(motionComplete);
    mImpl->ReactiveOutputStates->Commit(reactiveComplete);
    mImpl->NriBarriersUsed = true;
    return true;
#endif
}

void MotionVectorPass::InvalidateScreenResources() {
    mImpl->Image = VK_NULL_HANDLE;
    mImpl->View = VK_NULL_HANDLE;
    mImpl->StorageView = VK_NULL_HANDLE;
    mImpl->ReactiveImage = VK_NULL_HANDLE;
    mImpl->ReactiveView = VK_NULL_HANDLE;
    mImpl->ReactiveStorageView = VK_NULL_HANDLE;
    mImpl->DepthImage = VK_NULL_HANDLE;
    mImpl->MaterialGuideImage = VK_NULL_HANDLE;
    mImpl->RigidMotionGuideImage = VK_NULL_HANDLE;
    mImpl->Width = mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = {};
    mImpl->MotionOutputStates = nullptr;
    mImpl->ReactiveOutputStates = nullptr;
}

void MotionVectorPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->DescriptorPool != nullptr)
            mImpl->Core->DestroyDescriptorPool(mImpl->DescriptorPool);
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            if (mImpl->UniformMapped[slot] != nullptr &&
                mImpl->UniformBuffers[slot] != nullptr)
                mImpl->Core->UnmapBuffer(*mImpl->UniformBuffers[slot]);
            if (mImpl->UniformViews[slot] != nullptr)
                mImpl->Core->DestroyDescriptor(mImpl->UniformViews[slot]);
            if (mImpl->UniformBuffers[slot] != nullptr)
                mImpl->Core->DestroyBuffer(mImpl->UniformBuffers[slot]);
        }
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "motion vector pass is not initialized";
}

bool MotionVectorPass::Available() const { return mImpl->Initialized; }
VkImage MotionVectorPass::OutputImage() const { return mImpl->Image; }
VkImageView MotionVectorPass::OutputView() const { return mImpl->View; }
VkImage MotionVectorPass::ReactiveImage() const { return mImpl->ReactiveImage; }
VkImageView MotionVectorPass::ReactiveView() const { return mImpl->ReactiveView; }
bool MotionVectorPass::OutputsOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Image) &&
           mImpl->Interop->OwnsTexture(mImpl->ReactiveImage);
}
bool MotionVectorPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->Pipeline != nullptr &&
           mImpl->DescriptorPool != nullptr && mImpl->UniformBuffers[0] != nullptr;
#else
    return false;
#endif
}
bool MotionVectorPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
const EffectPassBarrierExecution& MotionVectorPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& MotionVectorPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
