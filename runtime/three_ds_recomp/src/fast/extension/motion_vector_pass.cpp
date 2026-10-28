#include "fast/extension/motion_vector_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/motion_vectors.h"
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Extension {
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

// The graph executions of one frame that read motion (a native boundary
// before its presentation, then the presentation an upscaler reconstructs)
// each write their own transient outputs. Every such configuration keeps its
// own descriptor sets and uniforms, so alternating between them rewrites no
// set that recorded work may still read. Only a configuration beyond these,
// or one replacing a configuration recorded since the last completed
// boundary, rewrites a set, and only that completes the recorded work first
// (it drains the GPU, so doing it per execution serializes CPU and GPU).
constexpr uint32_t kBindingConfigurations = 4U;

struct MotionBindingKey {
    VkImage Output = VK_NULL_HANDLE;
    VkImage Reactive = VK_NULL_HANDLE;
    VkImage Depth = VK_NULL_HANDLE;
    VkImage MaterialGuide = VK_NULL_HANDLE;
    VkImage RigidMotionGuide = VK_NULL_HANDLE;
    const ResourceStateTracker* MotionStates = nullptr;
    const ResourceStateTracker* ReactiveStates = nullptr;
    // The interop's wrap generation of each image above, so a handle that a
    // destroyed image's replacement reuses never matches a stale set.
    std::array<uint64_t, 5> Generations{};
    uint32_t Width = 0;
    uint32_t Height = 0;

    [[nodiscard]] bool Bound() const { return Width != 0U; }
    friend bool operator==(const MotionBindingKey&, const MotionBindingKey&) = default;
};
} // namespace

struct MotionVectorPass::Impl {
    struct Binding {
        MotionBindingKey Key;
        VkImageView View = VK_NULL_HANDLE;
        VkImageView StorageView = VK_NULL_HANDLE;
        VkImageView ReactiveView = VK_NULL_HANDLE;
        VkImageView ReactiveStorageView = VK_NULL_HANDLE;
        ResourceStateTracker* MotionOutputStates = nullptr;
        ResourceStateTracker* ReactiveOutputStates = nullptr;
        uint64_t LastUse = 0;
        // Recorded since this pass last completed the recorded work: its sets
        // may still be read, so rewriting them needs that completion first.
        bool Recorded = false;
#ifdef ENABLE_OOT3D_NRI
        std::array<nri::DescriptorSet*, kFrameSlots> Sets{};
        std::array<nri::Buffer*, kFrameSlots> UniformBuffers{};
        std::array<nri::Descriptor*, kFrameSlots> UniformViews{};
        std::array<void*, kFrameSlots> UniformMapped{};
#endif
    };
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* DescriptorPool = nullptr;
#endif
    std::array<Binding, kBindingConfigurations> Bindings{};
    // The configuration the last Configure selected; Execute records it.
    uint32_t Active = kBindingConfigurations;
    uint64_t UseClock = 0;
    bool Initialized = false;
    bool NriBarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason = "motion vector pass is not initialized";

    [[nodiscard]] const Binding* ActiveBinding() const {
        return Active < Bindings.size() && Bindings[Active].Key.Bound() ? &Bindings[Active] : nullptr;
    }
    [[nodiscard]] Binding* ActiveBinding() {
        return Active < Bindings.size() && Bindings[Active].Key.Bound() ? &Bindings[Active] : nullptr;
    }
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
        pipelineDesc.cache = NriInteropAccess::PipelineCache(interop);
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

        constexpr uint32_t setCount = kBindingConfigurations * kFrameSlots;
        nri::DescriptorPoolDesc poolDesc{};
        poolDesc.descriptorSetMaxNum = setCount;
        poolDesc.samplerMaxNum = setCount;
        poolDesc.constantBufferMaxNum = setCount;
        poolDesc.textureMaxNum = 3U * setCount;
        poolDesc.storageTextureMaxNum = 2U * setCount;
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, poolDesc, mImpl->DescriptorPool) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI motion descriptor pool creation failed");

        for (auto& binding : mImpl->Bindings) {
            for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
                nri::BufferDesc bufferDesc{};
                bufferDesc.size = sizeof(MotionUniforms);
                bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
                if (mImpl->Core->CreateCommittedBuffer(
                        *nriDevice, nri::MemoryLocation::HOST_UPLOAD, 0.0F,
                        bufferDesc, binding.UniformBuffers[slot]) !=
                    nri::Result::SUCCESS)
                    throw std::runtime_error("NRI motion uniform creation failed");
                nri::BufferViewDesc viewDesc{};
                viewDesc.buffer = binding.UniformBuffers[slot];
                viewDesc.type = nri::BufferView::CONSTANT_BUFFER;
                viewDesc.size = sizeof(MotionUniforms);
                if (mImpl->Core->CreateBufferView(
                        viewDesc, binding.UniformViews[slot]) !=
                    nri::Result::SUCCESS)
                    throw std::runtime_error("NRI motion uniform view creation failed");
                binding.UniformMapped[slot] = mImpl->Core->MapBuffer(
                    *binding.UniformBuffers[slot], 0, sizeof(MotionUniforms));
                if (binding.UniformMapped[slot] == nullptr)
                    throw std::runtime_error("NRI motion uniform mapping failed");
            }
            if (mImpl->Core->AllocateDescriptorSets(
                    *mImpl->DescriptorPool, *mImpl->PipelineLayout, 0,
                    binding.Sets.data(), kFrameSlots, 0) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI motion descriptor allocation failed");
            for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
                const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
                    {binding.Sets[slot], 3, 0, &binding.UniformViews[slot], 1},
                    {binding.Sets[slot], 6, 0, &mImpl->Sampler, 1}}};
                mImpl->Core->UpdateDescriptorRanges(
                    updates.data(), static_cast<uint32_t>(updates.size()));
            }
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
#ifndef ENABLE_OOT3D_NRI
    (void)depthFormat; (void)materialGuideFormat; (void)rigidMotionGuideFormat;
    return false;
#else
    // Wrapping is idempotent; it precedes the key so the key carries every
    // image's wrap generation.
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
        mImpl->Reason = "NRI motion input wrapping failed";
        return false;
    }
    const auto generation = [this](VkImage image) { return mImpl->Interop->TextureGeneration(image); };
    const MotionBindingKey key{
        motionOutput.Texture.Image, reactiveOutput.Texture.Image,
        depthImage, materialGuideImage, rigidMotionGuideImage,
        motionOutput.StateTracker, reactiveOutput.StateTracker,
        {generation(motionOutput.Texture.Image), generation(reactiveOutput.Texture.Image),
         generation(depthImage), generation(materialGuideImage), generation(rigidMotionGuideImage)},
        motionOutput.Width, motionOutput.Height};
    for (uint32_t index = 0; index < kBindingConfigurations; ++index) {
        if (mImpl->Bindings[index].Key == key) {
            mImpl->Active = index;
            return true;
        }
    }
    // A configuration not recorded since the last completed boundary can be
    // rewritten in place: an unbound one first, then the least recently used.
    uint32_t chosen = kBindingConfigurations;
    const auto better = [&](uint32_t candidate) {
        if (chosen == kBindingConfigurations) return true;
        const auto& a = mImpl->Bindings[candidate];
        const auto& b = mImpl->Bindings[chosen];
        if (a.Key.Bound() != b.Key.Bound()) return !a.Key.Bound();
        return a.LastUse < b.LastUse;
    };
    for (uint32_t index = 0; index < kBindingConfigurations; ++index) {
        if (!mImpl->Bindings[index].Recorded && better(index)) chosen = index;
    }
    if (chosen == kBindingConfigurations) {
        // Every configuration may still be read by recorded work.
        if (!mImpl->Interop->CompleteRecordedWork())
            return false;
        for (auto& binding : mImpl->Bindings) binding.Recorded = false;
        for (uint32_t index = 0; index < kBindingConfigurations; ++index) {
            if (better(index)) chosen = index;
        }
    }
    auto& binding = mImpl->Bindings[chosen];
    binding.Key = {};
    mImpl->Active = kBindingConfigurations;
    try {
        nri::Descriptor* depth =
            NriInteropAccess::TextureView(*mImpl->Interop, depthImage, false);
        nri::Descriptor* material = NriInteropAccess::TextureView(
            *mImpl->Interop, materialGuideImage, false);
        nri::Descriptor* output = NriInteropAccess::TextureView(
            *mImpl->Interop, motionOutput.Texture.Image, true);
        nri::Descriptor* rigid = NriInteropAccess::TextureView(
            *mImpl->Interop, rigidMotionGuideImage, false);
        nri::Descriptor* reactive = NriInteropAccess::TextureView(
            *mImpl->Interop, reactiveOutput.Texture.Image, true);
        if (depth == nullptr || material == nullptr || output == nullptr ||
            rigid == nullptr || reactive == nullptr)
            throw std::runtime_error("NRI motion texture views are unavailable");
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            const std::array<nri::UpdateDescriptorRangeDesc, 5> updates{{
                {binding.Sets[slot], 0, 0, &depth, 1},
                {binding.Sets[slot], 1, 0, &material, 1},
                {binding.Sets[slot], 2, 0, &output, 1},
                {binding.Sets[slot], 4, 0, &rigid, 1},
                {binding.Sets[slot], 5, 0, &reactive, 1}}};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
        }
        binding.View = motionOutput.Texture.SampledView;
        binding.StorageView = motionOutput.Texture.StorageView;
        binding.ReactiveView = reactiveOutput.Texture.SampledView;
        binding.ReactiveStorageView = reactiveOutput.Texture.StorageView;
        binding.MotionOutputStates = motionOutput.StateTracker;
        binding.ReactiveOutputStates = reactiveOutput.StateTracker;
        binding.Key = key;
        mImpl->Active = chosen;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        binding.Key = {};
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
    (void)barrierPlan; (void)slot; (void)coordinates;
    return false;
#else
    const auto* motionBarrier = barrierPlan.Find(
        EffectResource::MotionVectors);
    const auto* reactiveBarrier = barrierPlan.Find(
        EffectResource::ReactiveMask);
    auto* binding = mImpl->ActiveBinding();
    if (!barrierPlan.Valid() || motionBarrier == nullptr ||
        reactiveBarrier == nullptr || !motionBarrier->Managed() ||
        !reactiveBarrier->Managed() || binding == nullptr ||
        binding->MotionOutputStates == nullptr ||
        binding->ReactiveOutputStates == nullptr) {
        return false;
    }
    nri::CommandBuffer* nriCommand =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (binding->Sets[slot] == nullptr ||
        binding->UniformMapped[slot] == nullptr || nriCommand == nullptr)
        return false;
    std::array<float, 3> forward{view.At[0] - view.Eye[0],
                                 view.At[1] - view.Eye[1],
                                 view.At[2] - view.Eye[2]};
    const float length = std::sqrt(forward[0] * forward[0] +
                                   forward[1] * forward[1] +
                                   forward[2] * forward[2]);
    if (length <= 1.0e-6F) return false;
    for (float& value : forward) value /= length;
    const VkImage image = binding->Key.Output;
    const VkImage reactiveImage = binding->Key.Reactive;
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
    uniforms.Extent = {binding->Key.Width, binding->Key.Height,
        coordinates ? coordinates->ShaderFlags() : 0U, 0U};
    std::memcpy(binding->UniformMapped[slot], &uniforms, sizeof(uniforms));

    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    const auto motionWrite = binding->MotionOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(image),
        {motionBarrier->DispatchAccess, 0});
    const auto reactiveWrite = binding->ReactiveOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(reactiveImage),
        {reactiveBarrier->DispatchAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> toCompute{{
        {image, motionWrite},
        {reactiveImage, reactiveWrite}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, toCompute.data(),
            static_cast<uint32_t>(toCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(motionWrite);
    mImpl->LastBarrierExecution.RecordGraphTransition(reactiveWrite);
    binding->MotionOutputStates->Commit(motionWrite);
    binding->ReactiveOutputStates->Commit(reactiveWrite);

    binding->Recorded = true;
    binding->LastUse = ++mImpl->UseClock;
    mImpl->Core->CmdSetDescriptorPool(*nriCommand, *mImpl->DescriptorPool);
    mImpl->Core->CmdSetPipelineLayout(
        *nriCommand, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*nriCommand, *mImpl->Pipeline);
    nri::SetDescriptorSetDesc setDesc{0, binding->Sets[slot]};
    mImpl->Core->CmdSetDescriptorSet(*nriCommand, setDesc);
    nri::DispatchDesc dispatch{};
    dispatch.x = (binding->Key.Width + 7U) / 8U;
    dispatch.y = (binding->Key.Height + 7U) / 8U;
    dispatch.z = 1U;
    mImpl->Core->CmdDispatch(*nriCommand, dispatch);

    const auto motionComplete = binding->MotionOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(image),
        {motionBarrier->CompletionAccess, 0});
    const auto reactiveComplete =
        binding->ReactiveOutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(reactiveImage),
        {reactiveBarrier->CompletionAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> fromCompute{{
        {image, motionComplete},
        {reactiveImage, reactiveComplete}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, fromCompute.data(),
            static_cast<uint32_t>(fromCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(motionComplete);
    mImpl->LastBarrierExecution.RecordGraphTransition(reactiveComplete);
    binding->MotionOutputStates->Commit(motionComplete);
    binding->ReactiveOutputStates->Commit(reactiveComplete);
    mImpl->NriBarriersUsed = true;
    return true;
#endif
}

void MotionVectorPass::InvalidateScreenResources() {
    // The images may be destroyed and their handles reused: forget every
    // configuration. Their sets stay allocated; a set recorded since the last
    // completed boundary is rewritten only after completing it again.
    for (auto& binding : mImpl->Bindings) {
        binding.Key = {};
        binding.View = VK_NULL_HANDLE;
        binding.StorageView = VK_NULL_HANDLE;
        binding.ReactiveView = VK_NULL_HANDLE;
        binding.ReactiveStorageView = VK_NULL_HANDLE;
        binding.MotionOutputStates = nullptr;
        binding.ReactiveOutputStates = nullptr;
        binding.LastUse = 0;
    }
    mImpl->Active = kBindingConfigurations;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = {};
}

void MotionVectorPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->DescriptorPool != nullptr)
            mImpl->Core->DestroyDescriptorPool(mImpl->DescriptorPool);
        for (auto& binding : mImpl->Bindings) {
            for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
                if (binding.UniformMapped[slot] != nullptr &&
                    binding.UniformBuffers[slot] != nullptr)
                    mImpl->Core->UnmapBuffer(*binding.UniformBuffers[slot]);
                if (binding.UniformViews[slot] != nullptr)
                    mImpl->Core->DestroyDescriptor(binding.UniformViews[slot]);
                if (binding.UniformBuffers[slot] != nullptr)
                    mImpl->Core->DestroyBuffer(binding.UniformBuffers[slot]);
            }
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
VkImage MotionVectorPass::OutputImage() const {
    const auto* binding = mImpl->ActiveBinding();
    return binding != nullptr ? binding->Key.Output : VK_NULL_HANDLE;
}
VkImageView MotionVectorPass::OutputView() const {
    const auto* binding = mImpl->ActiveBinding();
    return binding != nullptr ? binding->View : VK_NULL_HANDLE;
}
VkImage MotionVectorPass::ReactiveImage() const {
    const auto* binding = mImpl->ActiveBinding();
    return binding != nullptr ? binding->Key.Reactive : VK_NULL_HANDLE;
}
VkImageView MotionVectorPass::ReactiveView() const {
    const auto* binding = mImpl->ActiveBinding();
    return binding != nullptr ? binding->ReactiveView : VK_NULL_HANDLE;
}
bool MotionVectorPass::OutputsOwnedByNri() const {
    const VkImage image = OutputImage();
    const VkImage reactive = ReactiveImage();
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(image) &&
           mImpl->Interop->OwnsTexture(reactive);
}
bool MotionVectorPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->Pipeline != nullptr &&
           mImpl->DescriptorPool != nullptr &&
           mImpl->Bindings[0].UniformBuffers[0] != nullptr;
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

} // namespace Fast::Extension
#endif
