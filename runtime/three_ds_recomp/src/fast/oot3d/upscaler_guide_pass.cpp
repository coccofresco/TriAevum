#include "fast/oot3d/upscaler_guide_pass.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/upscaler_guides.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <stdexcept>

namespace Fast::Oot3d {

struct UpscalerGuidePass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    nri::DescriptorSet* Set = nullptr;
#endif
    NriOwnedTexture2D Depth;
    NriOwnedTexture2D Motion;
    NriOwnedTexture2D Reactive;
    Guides Sources;
    uint32_t Width = 0U, Height = 0U;
    bool Ready = false;
    ResourceStateTracker States;
    EffectPassBarrierExecution LastBarriers;
#ifdef ENABLE_OOT3D_NRI
    std::string Reason = "upscaler guide resampling is not initialized";
#else
    std::string Reason = "upscaler guide resampling requires NRI";
#endif
};

namespace {
bool SameGuides(const UpscalerGuidePass::Guides& lhs, const UpscalerGuidePass::Guides& rhs) {
    return lhs.Depth == rhs.Depth && lhs.DepthFormat == rhs.DepthFormat && lhs.Motion == rhs.Motion &&
           lhs.MotionFormat == rhs.MotionFormat && lhs.Reactive == rhs.Reactive &&
           lhs.ReactiveFormat == rhs.ReactiveFormat && lhs.Width == rhs.Width && lhs.Height == rhs.Height;
}
} // namespace

UpscalerGuidePass::UpscalerGuidePass() : mImpl(std::make_unique<Impl>()) {
}
UpscalerGuidePass::~UpscalerGuidePass() {
    Shutdown();
}

bool UpscalerGuidePass::Initialize(VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop) {
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
        for (const VkFormat format : { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16_SFLOAT }) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(physical, format, &properties);
            if ((properties.optimalTilingFeatures & required) != required)
                throw std::runtime_error("upscaler guides require sampled/storage R32F, RGBA16F and R16F");
        }
        auto* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("upscaler guide NRI device unavailable");
        // One range per shader binding (a multi-descriptor range is an array).
        const std::array<nri::DescriptorRangeDesc, 7> ranges{
            { { 0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 3, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 4, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 5, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 6, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER } }
        };
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = uint32_t(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(UpscalerGuideConstants);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(*nriDevice, layout, mImpl->Layout) != nri::Result::SUCCESS)
            throw std::runtime_error("upscaler guide pipeline layout failed");
        const auto spirv = interop.Shaders().Resolve(BuildUpscalerGuideResampleShader(), Renderer::SpirvStage::Compute,
                                                     "oot3d_upscaler_guides.comp");
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->Layout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = spirv.data();
        pipeline.shader.size = spirv.size() * sizeof(uint32_t);
        pipeline.cache = NriInteropAccess::PipelineCache(interop);
        if (mImpl->Core->CreateComputePipeline(*nriDevice, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
            throw std::runtime_error("upscaler guide pipeline failed");
        nri::SamplerDesc sampler{};
        sampler.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        sampler.addressModes = { nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE,
                                 nri::AddressMode::CLAMP_TO_EDGE };
        if (mImpl->Core->CreateSampler(*nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("upscaler guide sampler failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& error) {
        mImpl->Reason = error.what();
        return false;
    }
#endif
}

bool UpscalerGuidePass::Execute(uint32_t frameSlot, const Guides& guides, uint32_t width, uint32_t height,
                                const std::array<float, 2>& motionCorrection) {
    mImpl->LastBarriers = {};
    if (!Available() || guides.Depth == VK_NULL_HANDLE || guides.Motion == VK_NULL_HANDLE ||
        guides.Reactive == VK_NULL_HANDLE || guides.Width == 0U || guides.Height == 0U || width == 0U || height == 0U)
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)motionCorrection;
    return false;
#else
    if (mImpl->Set == nullptr || mImpl->Width != width || mImpl->Height != height ||
        !SameGuides(mImpl->Sources, guides)) {
        // Recorded frames may still read the previous images or descriptors.
        if (mImpl->Width != 0U && !mImpl->Interop->CompleteRecordedWork())
            return false;
        InvalidateScreenResources();
        try {
            if (!mImpl->Interop->CreateOwnedTexture2D(width, height, VK_FORMAT_R32_SFLOAT, mImpl->Depth) ||
                !mImpl->Interop->CreateOwnedTexture2D(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, mImpl->Motion) ||
                !mImpl->Interop->CreateOwnedTexture2D(width, height, VK_FORMAT_R16_SFLOAT, mImpl->Reactive))
                throw std::runtime_error("upscaler guide allocation failed");
            const std::array sources{ guides.Depth, guides.Motion, guides.Reactive };
            const std::array formats{ guides.DepthFormat, guides.MotionFormat, guides.ReactiveFormat };
            std::array<nri::Descriptor*, 3> sampled{};
            for (size_t i = 0; i < sources.size(); ++i) {
                if (!mImpl->Interop->WrapTexture(sources[i], formats[i], VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                                 guides.Width, guides.Height))
                    throw std::runtime_error("upscaler guide input wrapping failed");
                sampled[i] = NriInteropAccess::TextureView(*mImpl->Interop, sources[i], false);
                if (sampled[i] == nullptr)
                    throw std::runtime_error("upscaler guide input view unavailable");
            }
            std::array<nri::Descriptor*, 3> storage{};
            const std::array outputs{ mImpl->Depth.Image, mImpl->Motion.Image, mImpl->Reactive.Image };
            for (size_t i = 0; i < outputs.size(); ++i) {
                storage[i] = NriInteropAccess::TextureView(*mImpl->Interop, outputs[i], true);
                if (storage[i] == nullptr)
                    throw std::runtime_error("upscaler guide output view unavailable");
            }
            nri::DescriptorPoolDesc pool{};
            pool.descriptorSetMaxNum = 1;
            pool.textureMaxNum = 3;
            pool.storageTextureMaxNum = 3;
            pool.samplerMaxNum = 1;
            auto* device = NriInteropAccess::Device(*mImpl->Interop);
            if (mImpl->Core->CreateDescriptorPool(*device, pool, mImpl->Pool) != nri::Result::SUCCESS ||
                mImpl->Core->AllocateDescriptorSets(*mImpl->Pool, *mImpl->Layout, 0, &mImpl->Set, 1, 0) !=
                    nri::Result::SUCCESS)
                throw std::runtime_error("upscaler guide descriptor allocation failed");
            const std::array<nri::UpdateDescriptorRangeDesc, 7> updates{ {
                { mImpl->Set, 0, 0, &sampled[0], 1 },
                { mImpl->Set, 1, 0, &sampled[1], 1 },
                { mImpl->Set, 2, 0, &sampled[2], 1 },
                { mImpl->Set, 3, 0, &storage[0], 1 },
                { mImpl->Set, 4, 0, &storage[1], 1 },
                { mImpl->Set, 5, 0, &storage[2], 1 },
                { mImpl->Set, 6, 0, &mImpl->Sampler, 1 },
            } };
            mImpl->Core->UpdateDescriptorRanges(updates.data(), uint32_t(updates.size()));
            mImpl->Sources = guides;
            mImpl->Width = width;
            mImpl->Height = height;
        } catch (const std::exception& error) {
            mImpl->Reason = error.what();
            InvalidateScreenResources();
            return false;
        }
    }
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr)
        return false;
    const auto transition = [&](VkImage image, ResourceAccess access) {
        const auto change = mImpl->States.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, image, change))
            return false;
        mImpl->States.Commit(change);
        mImpl->LastBarriers.RecordPrivateTransition(change);
        return true;
    };
    const std::array outputs{ mImpl->Depth.Image, mImpl->Motion.Image, mImpl->Reactive.Image };
    for (const VkImage image : outputs)
        if (!transition(image, ResourceAccess::ComputeWrite))
            return false;
    const UpscalerGuideConstants push{ width, height, guides.Width, guides.Height,
                                       { motionCorrection[0], motionCorrection[1] } };
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc descriptors{ 0, mImpl->Set };
    mImpl->Core->CmdSetDescriptorSet(*command, descriptors);
    const nri::SetRootConstantsDesc constants{ 0, &push, sizeof(push) };
    mImpl->Core->CmdSetRootConstants(*command, constants);
    mImpl->Core->CmdDispatch(*command, { (width + 7U) / 8U, (height + 7U) / 8U, 1 });
    for (const VkImage image : outputs)
        if (!transition(image, ResourceAccess::ShaderRead))
            return false;
    return true;
#endif
}

void UpscalerGuidePass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core && mImpl->Pool)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Set = nullptr;
#endif
    if (mImpl->Interop) {
        mImpl->Interop->DestroyOwnedTexture(mImpl->Depth.Image);
        mImpl->Interop->DestroyOwnedTexture(mImpl->Motion.Image);
        mImpl->Interop->DestroyOwnedTexture(mImpl->Reactive.Image);
    }
    mImpl->Depth = {};
    mImpl->Motion = {};
    mImpl->Reactive = {};
    mImpl->Sources = {};
    mImpl->Width = mImpl->Height = 0U;
    mImpl->States.Clear();
    mImpl->LastBarriers = {};
}

void UpscalerGuidePass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core) {
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

bool UpscalerGuidePass::Available() const {
    return mImpl->Ready;
}
VkImage UpscalerGuidePass::DepthImage() const {
    return mImpl->Depth.Image;
}
VkImageView UpscalerGuidePass::DepthView() const {
    return mImpl->Depth.SampledView;
}
VkImage UpscalerGuidePass::MotionImage() const {
    return mImpl->Motion.Image;
}
VkImageView UpscalerGuidePass::MotionView() const {
    return mImpl->Motion.SampledView;
}
VkImage UpscalerGuidePass::ReactiveImage() const {
    return mImpl->Reactive.Image;
}
VkImageView UpscalerGuidePass::ReactiveView() const {
    return mImpl->Reactive.SampledView;
}
const EffectPassBarrierExecution& UpscalerGuidePass::LastBarrierExecution() const {
    return mImpl->LastBarriers;
}
const std::string& UpscalerGuidePass::UnavailableReason() const {
    return mImpl->Reason;
}
} // namespace Fast::Oot3d
#endif
