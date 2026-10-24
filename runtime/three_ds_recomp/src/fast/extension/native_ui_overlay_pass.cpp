#include "fast/extension/native_ui_overlay_pass.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/native_ui_overlay.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <stdexcept>

namespace Fast::Extension {
namespace {
// Descriptor sets are written for each use from a ring per frame in flight, as
// the display copy does: a set is rewritten only once its frame completed, so
// the inputs (display images, a provider's per-frame output) may change freely.
constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kMaximumOverlaysPerFrame = 4U;
constexpr uint32_t kDescriptorSetCount = kFrameSlots * kMaximumOverlaysPerFrame;
} // namespace

struct NativeUiOverlayPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kDescriptorSetCount> Sets{};
#endif
    std::array<uint64_t, kFrameSlots> FrameIds{};
    std::array<uint32_t, kFrameSlots> SetsUsed{};
    NriOwnedTexture2D Output;
    uint32_t Width = 0U, Height = 0U;
    bool Ready = false;
    ResourceStateTracker States;
    EffectPassBarrierExecution LastBarriers;
#ifdef ENABLE_OOT3D_NRI
    std::string Reason = "native UI overlay is not initialized";
#else
    std::string Reason = "native UI overlay requires NRI";
#endif
};

NativeUiOverlayPass::NativeUiOverlayPass() : mImpl(std::make_unique<Impl>()) {
}
NativeUiOverlayPass::~NativeUiOverlayPass() {
    Shutdown();
}

bool NativeUiOverlayPass::Initialize(VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop) {
    Shutdown();
#ifndef ENABLE_OOT3D_NRI
    (void)physical;
    (void)device;
    (void)interop;
    return false;
#else
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    try {
        constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures & required) != required)
            throw std::runtime_error("native UI overlay requires sampled/storage RGBA16F");
        auto* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("native UI overlay NRI device unavailable");
        // One range per shader binding (a multi-descriptor range is an array).
        const std::array<nri::DescriptorRangeDesc, 5> ranges{
            { { 0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 3, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 4, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER } }
        };
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = uint32_t(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(NativeUiOverlayConstants);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(*nriDevice, layout, mImpl->Layout) != nri::Result::SUCCESS)
            throw std::runtime_error("native UI overlay pipeline layout failed");
        const auto spirv = interop.Shaders().Resolve(BuildNativeUiOverlayShader(), Renderer::SpirvStage::Compute,
                                                     "oot3d_native_ui_overlay.comp");
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->Layout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = spirv.data();
        pipeline.shader.size = spirv.size() * sizeof(uint32_t);
        pipeline.cache = NriInteropAccess::PipelineCache(interop);
        if (mImpl->Core->CreateComputePipeline(*nriDevice, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
            throw std::runtime_error("native UI overlay pipeline failed");
        nri::SamplerDesc sampler{};
        sampler.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        sampler.addressModes = { nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE,
                                 nri::AddressMode::CLAMP_TO_EDGE };
        if (mImpl->Core->CreateSampler(*nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("native UI overlay sampler failed");
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kDescriptorSetCount;
        pool.textureMaxNum = 3U * kDescriptorSetCount;
        pool.storageTextureMaxNum = kDescriptorSetCount;
        pool.samplerMaxNum = kDescriptorSetCount;
        if (mImpl->Core->CreateDescriptorPool(*nriDevice, pool, mImpl->Pool) != nri::Result::SUCCESS ||
            mImpl->Core->AllocateDescriptorSets(*mImpl->Pool, *mImpl->Layout, 0, mImpl->Sets.data(),
                                                kDescriptorSetCount, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("native UI overlay descriptors failed");
        for (nri::DescriptorSet* descriptorSet : mImpl->Sets) {
            const nri::UpdateDescriptorRangeDesc update{ descriptorSet, 4, 0, &mImpl->Sampler, 1 };
            mImpl->Core->UpdateDescriptorRanges(&update, 1);
        }
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& error) {
        mImpl->Reason = error.what();
        return false;
    }
#endif
}

bool NativeUiOverlayPass::Execute(uint32_t frameSlot, uint64_t frameId, const Inputs& inputs) {
    mImpl->LastBarriers = {};
    if (!Available() || inputs.DisplayUi == VK_NULL_HANDLE || inputs.DisplayScene == VK_NULL_HANDLE ||
        inputs.Upscaled == VK_NULL_HANDLE || inputs.DisplayWidth == 0U || inputs.DisplayHeight == 0U ||
        inputs.Width == 0U || inputs.Height == 0U)
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)frameId;
    return false;
#else
    if (mImpl->Width != inputs.Width || mImpl->Height != inputs.Height) {
        // Recorded frames may still read the previous image.
        if (mImpl->Width != 0U && !mImpl->Interop->CompleteRecordedWork())
            return false;
        InvalidateScreenResources();
        if (!mImpl->Interop->CreateOwnedTexture2D(inputs.Width, inputs.Height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                  mImpl->Output)) {
            mImpl->Reason = "native UI overlay allocation failed";
            return false;
        }
        mImpl->Width = inputs.Width;
        mImpl->Height = inputs.Height;
    }
    const std::array sources{ inputs.DisplayUi, inputs.DisplayScene, inputs.Upscaled };
    const std::array formats{ inputs.DisplayUiFormat, inputs.DisplaySceneFormat, inputs.UpscaledFormat };
    const std::array widths{ inputs.DisplayWidth, inputs.DisplayWidth, inputs.Width };
    const std::array heights{ inputs.DisplayHeight, inputs.DisplayHeight, inputs.Height };
    std::array<nri::Descriptor*, 3> sampled{};
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!mImpl->Interop->WrapTexture(sources[i], formats[i], VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                         widths[i], heights[i]))
            return false;
        sampled[i] = NriInteropAccess::TextureView(*mImpl->Interop, sources[i], false);
        if (sampled[i] == nullptr)
            return false;
    }
    nri::Descriptor* storage = NriInteropAccess::TextureView(*mImpl->Interop, mImpl->Output.Image, true);
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (storage == nullptr || command == nullptr)
        return false;
    const uint32_t slot = frameSlot % kFrameSlots;
    if (mImpl->FrameIds[slot] != frameId) {
        mImpl->FrameIds[slot] = frameId;
        mImpl->SetsUsed[slot] = 0U;
    }
    if (mImpl->SetsUsed[slot] >= kMaximumOverlaysPerFrame)
        return false;
    nri::DescriptorSet* descriptorSet = mImpl->Sets[slot * kMaximumOverlaysPerFrame + mImpl->SetsUsed[slot]++];
    const std::array<nri::UpdateDescriptorRangeDesc, 4> updates{ {
        { descriptorSet, 0, 0, &sampled[0], 1 },
        { descriptorSet, 1, 0, &sampled[1], 1 },
        { descriptorSet, 2, 0, &sampled[2], 1 },
        { descriptorSet, 3, 0, &storage, 1 },
    } };
    mImpl->Core->UpdateDescriptorRanges(updates.data(), uint32_t(updates.size()));
    const auto transition = [&](ResourceAccess access) {
        const VkImage image = mImpl->Output.Image;
        const auto change = mImpl->States.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, image, change))
            return false;
        mImpl->States.Commit(change);
        mImpl->LastBarriers.RecordPrivateTransition(change);
        return true;
    };
    if (!transition(ResourceAccess::ComputeWrite))
        return false;
    NativeUiOverlayConstants push{};
    push.Width = inputs.Width;
    push.Height = inputs.Height;
    push.DisplayWidth = inputs.DisplayWidth;
    push.DisplayHeight = inputs.DisplayHeight;
    push.LinearOutput = inputs.LinearOutput ? 1U : 0U;
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc descriptors{ 0, descriptorSet };
    mImpl->Core->CmdSetDescriptorSet(*command, descriptors);
    const nri::SetRootConstantsDesc constants{ 0, &push, sizeof(push) };
    mImpl->Core->CmdSetRootConstants(*command, constants);
    mImpl->Core->CmdDispatch(*command, { (inputs.Width + 7U) / 8U, (inputs.Height + 7U) / 8U, 1 });
    return transition(ResourceAccess::ShaderRead);
#endif
}

void NativeUiOverlayPass::InvalidateScreenResources() {
    if (mImpl->Interop)
        mImpl->Interop->DestroyOwnedTexture(mImpl->Output.Image);
    mImpl->Output = {};
    mImpl->Width = mImpl->Height = 0U;
    mImpl->States.Clear();
    mImpl->LastBarriers = {};
}

void NativeUiOverlayPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core) {
        if (mImpl->Pool)
            mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
        if (mImpl->Sampler)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->Layout)
            mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
    }
#endif
    *mImpl = {};
}

bool NativeUiOverlayPass::Available() const {
    return mImpl->Ready;
}
VkImage NativeUiOverlayPass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView NativeUiOverlayPass::OutputView() const {
    return mImpl->Output.SampledView;
}
const EffectPassBarrierExecution& NativeUiOverlayPass::LastBarrierExecution() const {
    return mImpl->LastBarriers;
}
const std::string& NativeUiOverlayPass::UnavailableReason() const {
    return mImpl->Reason;
}
} // namespace Fast::Extension
#endif
