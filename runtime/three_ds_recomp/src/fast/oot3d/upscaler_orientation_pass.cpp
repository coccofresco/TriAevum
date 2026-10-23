#include "fast/oot3d/upscaler_orientation_pass.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/upscaler_orientation.h"
#include "fast/oot3d/upscaler_descriptor_key.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace Fast::Oot3d {

namespace {
// Descriptor sets per distinct source set: the graph may hand alternate images
// to frames in flight. Beyond this many, every set is rebuilt.
constexpr uint32_t kMaximumSets = 8U;

bool SrgbFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return true;
    default:
        return false;
    }
}

struct Binding {
    VkImage Image = VK_NULL_HANDLE;
    bool Storage = false;
};
} // namespace

struct UpscalerOrientationPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* InputLayout = nullptr;
    nri::Pipeline* InputPipeline = nullptr;
    nri::PipelineLayout* OutputLayout = nullptr;
    nri::Pipeline* OutputPipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::map<std::vector<uint64_t>, nri::DescriptorSet*> Sets;
#endif
    NriOwnedTexture2D Color, Depth, Motion, Reactive, Output;
    uint32_t InputWidth = 0U, InputHeight = 0U;
    uint32_t OutputWidth = 0U, OutputHeight = 0U;
    bool Ready = false;
    ResourceStateTracker States;
#ifdef ENABLE_OOT3D_NRI
    std::string Reason = "upscaler orientation is not initialized";
#else
    std::string Reason = "upscaler orientation requires NRI";
#endif

#ifdef ENABLE_OOT3D_NRI
    nri::DescriptorSet* FindOrBuildSet(bool input, const std::vector<Binding>& images);
#endif
};

UpscalerOrientationPass::UpscalerOrientationPass() : mImpl(std::make_unique<Impl>()) {
}
UpscalerOrientationPass::~UpscalerOrientationPass() {
    Shutdown();
}

bool UpscalerOrientationPass::Initialize(VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop) {
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
                throw std::runtime_error("upscaler orientation requires sampled/storage R32F, RGBA16F and R16F");
        }
        auto* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("upscaler orientation NRI device unavailable");
        const auto build = [&](uint32_t sampled, uint32_t storage, const std::string& source, const char* name,
                               nri::PipelineLayout*& layout, nri::Pipeline*& pipeline) {
            // One range per shader binding (a multi-descriptor range is an array).
            std::vector<nri::DescriptorRangeDesc> ranges;
            for (uint32_t binding = 0; binding < sampled; ++binding)
                ranges.push_back({ binding, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER });
            for (uint32_t binding = sampled; binding < sampled + storage; ++binding)
                ranges.push_back({ binding, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER });
            ranges.push_back({ sampled + storage, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER });
            nri::DescriptorSetDesc set{};
            set.ranges = ranges.data();
            set.rangeNum = uint32_t(ranges.size());
            nri::RootConstantDesc root{};
            root.size = sizeof(UpscalerOrientationConstants);
            root.shaderStages = nri::StageBits::COMPUTE_SHADER;
            nri::PipelineLayoutDesc desc{};
            desc.descriptorSets = &set;
            desc.descriptorSetNum = 1;
            desc.rootConstants = &root;
            desc.rootConstantNum = 1;
            desc.shaderStages = nri::StageBits::COMPUTE_SHADER;
            desc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
            if (mImpl->Core->CreatePipelineLayout(*nriDevice, desc, layout) != nri::Result::SUCCESS)
                throw std::runtime_error("upscaler orientation pipeline layout failed");
            const auto spirv = interop.Shaders().Resolve(source, Renderer::SpirvStage::Compute, name);
            nri::ComputePipelineDesc compute{};
            compute.pipelineLayout = layout;
            compute.shader.stage = nri::StageBits::COMPUTE_SHADER;
            compute.shader.bytecode = spirv.data();
            compute.shader.size = spirv.size() * sizeof(uint32_t);
            compute.cache = NriInteropAccess::PipelineCache(interop);
            if (mImpl->Core->CreateComputePipeline(*nriDevice, compute, pipeline) != nri::Result::SUCCESS)
                throw std::runtime_error("upscaler orientation pipeline failed");
        };
        build(4U, 4U, BuildUpscalerUprightInputShader(), "oot3d_upscaler_upright_inputs.comp", mImpl->InputLayout,
              mImpl->InputPipeline);
        build(1U, 1U, BuildUpscalerUprightOutputShader(), "oot3d_upscaler_upright_output.comp", mImpl->OutputLayout,
              mImpl->OutputPipeline);
        nri::SamplerDesc sampler{};
        sampler.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        sampler.addressModes = { nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE,
                                 nri::AddressMode::CLAMP_TO_EDGE };
        if (mImpl->Core->CreateSampler(*nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("upscaler orientation sampler failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& error) {
        mImpl->Reason = error.what();
        return false;
    }
#endif
}

#ifdef ENABLE_OOT3D_NRI
nri::DescriptorSet* UpscalerOrientationPass::Impl::FindOrBuildSet(bool input, const std::vector<Binding>& images) {
    auto& impl = *this;
    std::vector<UpscalerDescriptorBinding> bindings;
    bindings.reserve(images.size());
    for (const auto& image : images)
        bindings.push_back({reinterpret_cast<uint64_t>(image.Image),
                            impl.Interop->TextureGeneration(image.Image), image.Storage});
    auto key = BuildUpscalerDescriptorKey(input, bindings);
    if (!key) return nullptr;
    if (const auto found = impl.Sets.find(*key); found != impl.Sets.end())
        return found->second;
    if (impl.Pool == nullptr || impl.Sets.size() >= kMaximumSets) {
        // Recorded frames may still read the sets about to go.
        if (impl.Pool != nullptr && !impl.Interop->CompleteRecordedWork())
            return nullptr;
        if (impl.Pool != nullptr)
            impl.Core->DestroyDescriptorPool(impl.Pool);
        impl.Pool = nullptr;
        impl.Sets.clear();
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kMaximumSets;
        pool.textureMaxNum = 4U * kMaximumSets;
        pool.storageTextureMaxNum = 4U * kMaximumSets;
        pool.samplerMaxNum = kMaximumSets;
        if (impl.Core->CreateDescriptorPool(*NriInteropAccess::Device(*impl.Interop), pool, impl.Pool) !=
            nri::Result::SUCCESS)
            return nullptr;
    }
    nri::DescriptorSet* set = nullptr;
    if (impl.Core->AllocateDescriptorSets(*impl.Pool, input ? *impl.InputLayout : *impl.OutputLayout, 0, &set, 1, 0) !=
        nri::Result::SUCCESS)
        return nullptr;
    std::vector<nri::Descriptor*> views;
    for (const auto& image : images) {
        auto* view = NriInteropAccess::TextureView(*impl.Interop, image.Image, image.Storage);
        if (view == nullptr)
            return nullptr;
        views.push_back(view);
    }
    views.push_back(impl.Sampler);
    std::vector<nri::UpdateDescriptorRangeDesc> updates;
    for (uint32_t range = 0; range < views.size(); ++range)
        updates.push_back({ set, range, 0, &views[range], 1 });
    impl.Core->UpdateDescriptorRanges(updates.data(), uint32_t(updates.size()));
    impl.Sets.emplace(std::move(*key), set);
    return set;
}
#endif

bool UpscalerOrientationPass::TurnInputs(uint32_t frameSlot, const Inputs& inputs, PicaSurfaceCoordinates coordinates,
                                         EffectPassBarrierExecution& barriers) {
    if (!Available() || inputs.Color == VK_NULL_HANDLE || inputs.Depth == VK_NULL_HANDLE ||
        inputs.Motion == VK_NULL_HANDLE || inputs.Reactive == VK_NULL_HANDLE || inputs.Width == 0U ||
        inputs.Height == 0U || SrgbFormat(inputs.ColorFormat))
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)coordinates;
    (void)barriers;
    return false;
#else
    auto& impl = *mImpl;
    const auto upright = UprightExtent(inputs.Width, inputs.Height);
    if (impl.InputWidth != upright[0] || impl.InputHeight != upright[1]) {
        if (impl.InputWidth != 0U && !impl.Interop->CompleteRecordedWork())
            return false;
        for (auto* texture : { &impl.Color, &impl.Depth, &impl.Motion, &impl.Reactive }) {
            impl.States.Forget(reinterpret_cast<uintptr_t>(texture->Image));
            impl.Interop->DestroyOwnedTexture(texture->Image);
            *texture = {};
        }
        impl.Sets.clear();
        if (impl.Pool != nullptr)
            impl.Core->DestroyDescriptorPool(impl.Pool);
        impl.Pool = nullptr;
        impl.InputWidth = impl.InputHeight = 0U;
        if (!impl.Interop->CreateOwnedTexture2D(upright[0], upright[1], VK_FORMAT_R16G16B16A16_SFLOAT, impl.Color) ||
            !impl.Interop->CreateOwnedTexture2D(upright[0], upright[1], VK_FORMAT_R32_SFLOAT, impl.Depth) ||
            !impl.Interop->CreateOwnedTexture2D(upright[0], upright[1], VK_FORMAT_R16G16B16A16_SFLOAT, impl.Motion) ||
            !impl.Interop->CreateOwnedTexture2D(upright[0], upright[1], VK_FORMAT_R16_SFLOAT, impl.Reactive)) {
            impl.Reason = "upscaler orientation allocation failed";
            return false;
        }
        impl.InputWidth = upright[0];
        impl.InputHeight = upright[1];
    }
    const std::array sources{ inputs.Color, inputs.Depth, inputs.Motion, inputs.Reactive };
    const std::array formats{ inputs.ColorFormat, inputs.DepthFormat, inputs.MotionFormat, inputs.ReactiveFormat };
    for (size_t i = 0; i < sources.size(); ++i)
        if (!impl.Interop->WrapTexture(sources[i], formats[i], VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                       inputs.Width, inputs.Height))
            return false;
    const std::array outputs{ impl.Color.Image, impl.Depth.Image, impl.Motion.Image, impl.Reactive.Image };
    std::vector<Binding> images;
    for (const VkImage image : sources)
        images.push_back({ image, false });
    for (const VkImage image : outputs)
        images.push_back({ image, true });
    auto* set = impl.FindOrBuildSet(true, images);
    auto* command = NriInteropAccess::CommandBuffer(*impl.Interop, frameSlot);
    if (set == nullptr || command == nullptr)
        return false;
    const auto transition = [&](VkImage image, ResourceAccess access) {
        const auto change = impl.States.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*impl.Interop, frameSlot, image, change))
            return false;
        impl.States.Commit(change);
        barriers.RecordPrivateTransition(change);
        return true;
    };
    for (const VkImage image : outputs)
        if (!transition(image, ResourceAccess::ComputeWrite))
            return false;
    const UpscalerOrientationConstants push{ upright[0], upright[1], inputs.Width, inputs.Height,
                                             coordinates.FlipY ? 1U : 0U };
    impl.Core->CmdSetDescriptorPool(*command, *impl.Pool);
    impl.Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *impl.InputLayout);
    impl.Core->CmdSetPipeline(*command, *impl.InputPipeline);
    impl.Core->CmdSetDescriptorSet(*command, { 0, set });
    impl.Core->CmdSetRootConstants(*command, { 0, &push, sizeof(push) });
    impl.Core->CmdDispatch(*command, { (upright[0] + 7U) / 8U, (upright[1] + 7U) / 8U, 1 });
    for (const VkImage image : outputs)
        if (!transition(image, ResourceAccess::ShaderRead))
            return false;
    return true;
#endif
}

bool UpscalerOrientationPass::BeginOutput(uint32_t frameSlot, uint32_t width, uint32_t height,
                                          EffectPassBarrierExecution& barriers) {
    if (!Available() || width == 0U || height == 0U)
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)barriers;
    return false;
#else
    auto& impl = *mImpl;
    const auto upright = UprightExtent(width, height);
    if (impl.OutputWidth != upright[0] || impl.OutputHeight != upright[1]) {
        if (impl.OutputWidth != 0U && !impl.Interop->CompleteRecordedWork())
            return false;
        impl.States.Forget(reinterpret_cast<uintptr_t>(impl.Output.Image));
        impl.Interop->DestroyOwnedTexture(impl.Output.Image);
        impl.Output = {};
        impl.Sets.clear();
        if (impl.Pool != nullptr)
            impl.Core->DestroyDescriptorPool(impl.Pool);
        impl.Pool = nullptr;
        impl.OutputWidth = impl.OutputHeight = 0U;
        if (!impl.Interop->CreateOwnedTexture2D(upright[0], upright[1], VK_FORMAT_R16G16B16A16_SFLOAT, impl.Output)) {
            impl.Reason = "upscaler orientation output allocation failed";
            return false;
        }
        impl.OutputWidth = upright[0];
        impl.OutputHeight = upright[1];
    }
    const auto change =
        impl.States.PlanTransition(reinterpret_cast<uintptr_t>(impl.Output.Image), { ResourceAccess::ComputeWrite, 0 });
    if (!NriInteropAccess::CmdTextureBarrier(*impl.Interop, frameSlot, impl.Output.Image, change))
        return false;
    impl.States.Commit(change);
    barriers.RecordPrivateTransition(change);
    return true;
#endif
}

bool UpscalerOrientationPass::TurnOutput(uint32_t frameSlot, VkImage target, uint32_t width, uint32_t height,
                                         PicaSurfaceCoordinates coordinates, EffectPassBarrierExecution& barriers) {
    const auto upright = UprightExtent(width, height);
    if (!Available() || target == VK_NULL_HANDLE || mImpl->Output.Image == VK_NULL_HANDLE ||
        mImpl->OutputWidth != upright[0] || mImpl->OutputHeight != upright[1])
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)coordinates;
    (void)barriers;
    return false;
#else
    auto& impl = *mImpl;
    if (!impl.Interop->WrapTexture(target, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TYPE_2D,
                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, width, height))
        return false;
    auto* set = impl.FindOrBuildSet(false, { { impl.Output.Image, false }, { target, true } });
    auto* command = NriInteropAccess::CommandBuffer(*impl.Interop, frameSlot);
    if (set == nullptr || command == nullptr)
        return false;
    const auto change =
        impl.States.PlanTransition(reinterpret_cast<uintptr_t>(impl.Output.Image), { ResourceAccess::ShaderRead, 0 });
    if (!NriInteropAccess::CmdTextureBarrier(*impl.Interop, frameSlot, impl.Output.Image, change))
        return false;
    impl.States.Commit(change);
    barriers.RecordPrivateTransition(change);
    const UpscalerOrientationConstants push{ width, height, upright[0], upright[1], coordinates.FlipY ? 1U : 0U };
    impl.Core->CmdSetDescriptorPool(*command, *impl.Pool);
    impl.Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *impl.OutputLayout);
    impl.Core->CmdSetPipeline(*command, *impl.OutputPipeline);
    impl.Core->CmdSetDescriptorSet(*command, { 0, set });
    impl.Core->CmdSetRootConstants(*command, { 0, &push, sizeof(push) });
    impl.Core->CmdDispatch(*command, { (width + 7U) / 8U, (height + 7U) / 8U, 1 });
    return true;
#endif
}

void UpscalerOrientationPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core && mImpl->Pool)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets.clear();
#endif
    if (mImpl->Interop) {
        for (auto* texture : { &mImpl->Color, &mImpl->Depth, &mImpl->Motion, &mImpl->Reactive, &mImpl->Output })
            mImpl->Interop->DestroyOwnedTexture(texture->Image);
    }
    mImpl->Color = mImpl->Depth = mImpl->Motion = mImpl->Reactive = mImpl->Output = {};
    mImpl->InputWidth = mImpl->InputHeight = mImpl->OutputWidth = mImpl->OutputHeight = 0U;
    mImpl->States.Clear();
}

void UpscalerOrientationPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core) {
        if (mImpl->Sampler)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        for (auto* pipeline : { mImpl->InputPipeline, mImpl->OutputPipeline })
            if (pipeline)
                mImpl->Core->DestroyPipeline(pipeline);
        for (auto* layout : { mImpl->InputLayout, mImpl->OutputLayout })
            if (layout)
                mImpl->Core->DestroyPipelineLayout(layout);
    }
#endif
    *mImpl = {};
}

bool UpscalerOrientationPass::Available() const {
    return mImpl->Ready;
}
VkImage UpscalerOrientationPass::ColorImage() const {
    return mImpl->Color.Image;
}
VkImage UpscalerOrientationPass::DepthImage() const {
    return mImpl->Depth.Image;
}
VkImage UpscalerOrientationPass::MotionImage() const {
    return mImpl->Motion.Image;
}
VkImage UpscalerOrientationPass::ReactiveImage() const {
    return mImpl->Reactive.Image;
}
VkImage UpscalerOrientationPass::OutputImage() const {
    return mImpl->Output.Image;
}
const std::string& UpscalerOrientationPass::UnavailableReason() const {
    return mImpl->Reason;
}
} // namespace Fast::Oot3d
#endif
