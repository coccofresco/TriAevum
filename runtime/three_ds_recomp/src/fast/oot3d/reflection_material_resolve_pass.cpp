#include "fast/oot3d/reflection_material_resolve_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/reflection_ibl.h"
#include "fast/oot3d/reflection_ibl_pass.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/temporal_history_manager.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif


#include <array>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;

struct ResolvePush {
    std::array<float, 16> InverseProjection{};
    uint32_t Width = 0;
    uint32_t Height = 0;
    float RoughnessBias = 0.0F;
    uint32_t SurfaceOrientation = 0;
};
static_assert(sizeof(ResolvePush) == 80U);

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders) {
    return shaders.Resolve(BuildReflectionMaterialResolveComputeShader(), Renderer::SpirvStage::Compute, "oot3d_reflection_material_resolve.comp");
}

} // namespace

struct ReflectionMaterialResolvePass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kFrameSlots> Sets{};
#endif
    NriOwnedTexture2D Output{};
    ResourceStateTracker* States = nullptr;
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Ready = false;
    bool BarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason =
        "reflection material resolve pass is not initialized";
};

ReflectionMaterialResolvePass::ReflectionMaterialResolvePass()
    : mImpl(std::make_unique<Impl>()) {}
ReflectionMaterialResolvePass::~ReflectionMaterialResolvePass() {
    Shutdown();
}

bool ReflectionMaterialResolvePass::Initialize(
    VkPhysicalDevice physicalDevice, VkDevice device,
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
    mImpl->Reason =
        "NRI reflection material resolve support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
            &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error(
                "reflection material resolve RGBA16F storage is unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error(
                "reflection material resolve NRI device is unavailable");

        const std::array<nri::DescriptorRangeDesc, 6> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {5, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER},
        }};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(ResolvePush);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags =
            nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->Layout) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection material resolve layout creation failed");

        const auto shader = Compile(interop.Shaders());
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->Layout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipeline, mImpl->Pipeline) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection material resolve pipeline creation failed");

        nri::SamplerDesc sampler{};
        sampler.filters = {
            nri::Filter::LINEAR, nri::Filter::LINEAR,
            nri::Filter::LINEAR};
        sampler.addressModes = {
            nri::AddressMode::CLAMP_TO_EDGE,
            nri::AddressMode::CLAMP_TO_EDGE,
            nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, sampler, mImpl->Sampler) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection material resolve sampler creation failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        const std::string reason = exception.what();
        Shutdown();
        mImpl->Reason = reason;
        return false;
    }
#endif
}

bool ReflectionMaterialResolvePass::Configure(
    const NriEffectGraphTransientImageBinding& output) {
    if (!mImpl->Ready || !output.Valid() ||
        output.Resource != EffectResource::ReflectionColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (mImpl->Output.Image == output.Texture.Image &&
        mImpl->Width == output.Width && mImpl->Height == output.Height &&
        mImpl->States == output.StateTracker) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0U &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS) {
        mImpl->Reason =
            "reflection material resolve resize wait failed";
        return false;
    }
    InvalidateScreenResources();
    try {
        mImpl->Output = output.Texture;
        mImpl->States = output.StateTracker;

        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kFrameSlots;
        pool.samplerMaxNum = kFrameSlots;
        pool.textureMaxNum = 4U * kFrameSlots;
        pool.storageTextureMaxNum = kFrameSlots;
        nri::Device* nriDevice =
            NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, pool, mImpl->Pool) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection material resolve descriptor pool failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->Layout, 0,
                mImpl->Sets.data(), kFrameSlots, 0) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection material resolve descriptor allocation failed");
        for (nri::DescriptorSet* set : mImpl->Sets) {
            const nri::UpdateDescriptorRangeDesc update{
                set, 5, 0, &mImpl->Sampler, 1};
            mImpl->Core->UpdateDescriptorRanges(&update, 1);
        }
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool ReflectionMaterialResolvePass::Execute(
    uint32_t frameIndex,
    VkImage reflectionImage, VkFormat reflectionFormat,
    VkImage normalImage, VkFormat normalFormat,
    VkImage materialImage, VkFormat materialFormat,
    VkImage brdfImage, VkFormat brdfFormat,
    const PerspectiveViewState& view, float roughnessBias,
    const EffectPassBarrierPlan& barrierPlan,
    std::optional<PicaSurfaceCoordinates> coordinates) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::ReflectionColor);
    if (mImpl->Width == 0U || mImpl->States == nullptr ||
        reflectionImage == VK_NULL_HANDLE ||
        normalImage == VK_NULL_HANDLE ||
        materialImage == VK_NULL_HANDLE ||
        brdfImage == VK_NULL_HANDLE || !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed()) {
        mImpl->Reason =
            "reflection material resolve inputs are unavailable";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)frameIndex;
    (void)reflectionFormat;
    (void)normalFormat;
    (void)materialFormat;
    (void)brdfFormat;
    (void)view;
    (void)roughnessBias;
    (void)barrierPlan;
    return false;
#else
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameIndex);
    if (command == nullptr) {
        mImpl->Reason =
            "reflection material resolve NRI command is unavailable";
        return false;
    }
    std::array<float, 16> inverseProjection{};
    if (!InvertTemporalMatrix(view.Projection, inverseProjection)) {
        mImpl->Reason =
            "reflection material resolve projection is not invertible";
        return false;
    }
    const std::array<VkImage, 4> images{
        reflectionImage, normalImage, materialImage, brdfImage};
    const std::array<VkFormat, 4> formats{
        reflectionFormat, normalFormat, materialFormat, brdfFormat};
    const std::array<uint32_t, 4> widths{
        mImpl->Width, mImpl->Width, mImpl->Width,
        ReflectionIblPass::BrdfSize};
    const std::array<uint32_t, 4> heights{
        mImpl->Height, mImpl->Height, mImpl->Height,
        ReflectionIblPass::BrdfSize};
    std::array<nri::Descriptor*, 5> descriptors{};
    for (size_t i = 0; i < images.size(); ++i) {
        if (!mImpl->Interop->WrapTexture(
                images[i], formats[i], VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT, widths[i], heights[i])) {
            mImpl->Reason =
                "reflection material resolve input wrapping failed";
            return false;
        }
        descriptors[i] = NriInteropAccess::TextureView(
            *mImpl->Interop, images[i], false);
    }
    descriptors[4] = NriInteropAccess::TextureView(
        *mImpl->Interop, mImpl->Output.Image, true);
    for (nri::Descriptor* descriptor : descriptors) {
        if (descriptor == nullptr) {
            mImpl->Reason =
                "reflection material resolve views are unavailable";
            return false;
        }
    }

    mImpl->BarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::ReflectionColor);
    const auto writable = mImpl->States->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Output.Image),
        {outputBarrier->DispatchAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameIndex, mImpl->Output.Image,
            writable)) {
        mImpl->Reason =
            "reflection material resolve write transition failed";
        return false;
    }
    mImpl->LastBarrierExecution.RecordGraphTransition(writable);
    mImpl->States->Commit(writable);

    const uint32_t slot = frameIndex % kFrameSlots;
    std::array<nri::UpdateDescriptorRangeDesc, 5> updates{};
    for (uint32_t i = 0; i < updates.size(); ++i)
        updates[i] = {
            mImpl->Sets[slot], i, 0, &descriptors[i], 1};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    ResolvePush push{};
    push.InverseProjection = inverseProjection;
    push.SurfaceOrientation = coordinates ? coordinates->ShaderFlags() : 0U;
    push.Width = mImpl->Width;
    push.Height = mImpl->Height;
    push.RoughnessBias = roughnessBias;
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc set{0, mImpl->Sets[slot]};
    mImpl->Core->CmdSetDescriptorSet(*command, set);
    const nri::SetRootConstantsDesc constants{
        0, &push, sizeof(push)};
    mImpl->Core->CmdSetRootConstants(*command, constants);
    const nri::DispatchDesc dispatch{
        (mImpl->Width + 7U) / 8U,
        (mImpl->Height + 7U) / 8U, 1U};
    mImpl->Core->CmdDispatch(*command, dispatch);

    const auto readable = mImpl->States->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Output.Image),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameIndex, mImpl->Output.Image,
            readable)) {
        mImpl->Reason =
            "reflection material resolve read transition failed";
        return false;
    }
    mImpl->LastBarrierExecution.RecordGraphTransition(readable);
    mImpl->States->Commit(readable);
    mImpl->BarriersUsed = true;
    mImpl->Reason.clear();
    return true;
#endif
}

void ReflectionMaterialResolvePass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->Pool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
    mImpl->Output = {};
    mImpl->States = nullptr;
    mImpl->Width = 0;
    mImpl->Height = 0;
    mImpl->BarriersUsed = false;
    mImpl->LastBarrierExecution = {};
}

void ReflectionMaterialResolvePass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->Layout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
    }
#endif
    mImpl = std::make_unique<Impl>();
}

bool ReflectionMaterialResolvePass::Available() const {
    return mImpl->Ready;
}
VkImage ReflectionMaterialResolvePass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView ReflectionMaterialResolvePass::OutputView() const {
    return mImpl->Output.SampledView;
}
bool ReflectionMaterialResolvePass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Output.Image);
}
bool ReflectionMaterialResolvePass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Ready && mImpl->Pipeline != nullptr &&
           mImpl->Pool != nullptr && mImpl->Sampler != nullptr;
#else
    return false;
#endif
}
bool ReflectionMaterialResolvePass::BarriersOwnedByNri() const {
    return mImpl->BarriersUsed;
}
const EffectPassBarrierExecution&
ReflectionMaterialResolvePass::LastBarrierExecution() const {
    return mImpl->LastBarrierExecution;
}
const std::string&
ReflectionMaterialResolvePass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
