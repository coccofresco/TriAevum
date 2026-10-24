#include "fast/extension/scene_composite_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/resource_state_tracker.h"
#include "fast/extension/scene_composite.h"
#include "fast/extension/toon_outline_shader.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace Fast::Extension {
namespace {
constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kDescriptorSetsPerPool = 64U;
struct CompositePush {
    uint32_t Width = 0, Height = 0, AmbientOcclusion = 0, Reflections = 0;
    float ReflectionStrength = 0.0F;
    uint32_t ReflectionDebug = 0;
    uint32_t Outline = 0;
    uint32_t InputLinear = 0;
    float InverseWidth = 0.0F;
    float InverseHeight = 0.0F;
    float OutlineWidth = 0.0F;
    float OutlineDepthSensitivity = 0.0F;
    float OutlineNormalSensitivity = 0.0F;
    float OutlineSoftness = 1.0F;
    float OutlineColorAlignment[2]{};
    float OutlineColor[3]{};
    float OutlineOpacity = 0.0F;
    uint32_t GlobalIllumination = 0;
    float DiffuseIntensity = 0.0F;
    uint32_t GlobalIlluminationDebug = 0;
    uint32_t AmbientOcclusionDebug = 0;
};
static_assert(sizeof(CompositePush) == 96U);

// Developer inspection (OOT3D_GRAPHICS_GI_DEBUG): 1 irradiance, 2 albedo,
// 3 coverage, 4 the added bounce light, 5 the direct radiance it bounces. Replaces the composed colour.
uint32_t GlobalIlluminationDebugView() {
    const char* value = std::getenv("OOT3D_GRAPHICS_GI_DEBUG");
    return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0U;
}

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders) {
    return shaders.Resolve(BuildSceneCompositeComputeShader(), Renderer::SpirvStage::Compute, "oot3d_scene_composite.comp");
}
} // namespace

struct SceneCompositePass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    struct DescriptorPoolBlock {
        nri::DescriptorPool* Pool = nullptr;
        std::array<nri::DescriptorSet*, kDescriptorSetsPerPool> Sets{};
    };
    struct FrameDescriptors {
        std::vector<DescriptorPoolBlock> Pools;
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        size_t Used = 0;
    };
    std::array<FrameDescriptors, kFrameSlots> Frames;

    bool AddDescriptorPool(FrameDescriptors& frame) {
        frame.Pools.emplace_back();
        auto& block = frame.Pools.back();
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kDescriptorSetsPerPool;
        pool.samplerMaxNum = kDescriptorSetsPerPool;
        pool.textureMaxNum = 12U * kDescriptorSetsPerPool;
        pool.storageTextureMaxNum = kDescriptorSetsPerPool;
        auto* device = NriInteropAccess::Device(*Interop);
        if (Core->CreateDescriptorPool(*device, pool, block.Pool) != nri::Result::SUCCESS ||
            Core->AllocateDescriptorSets(*block.Pool, *PipelineLayout, 0,
                block.Sets.data(), kDescriptorSetsPerPool, 0) != nri::Result::SUCCESS) {
            if (block.Pool != nullptr) Core->DestroyDescriptorPool(block.Pool);
            frame.Pools.pop_back();
            Reason = "NRI scene composite descriptor allocation failed";
            return false;
        }
        for (auto* set : block.Sets) {
            const nri::UpdateDescriptorRangeDesc update{set, 8, 0, &Sampler, 1};
            Core->UpdateDescriptorRanges(&update, 1);
        }
        return true;
    }
#endif
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkImageView StorageView = VK_NULL_HANDLE;
    uint32_t Width = 0, Height = 0;
    bool Ready = false;
    bool NriBarriersUsed = false;
    SceneColorEncoding OutputEncoding = SceneColorEncoding::Unknown;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker* OutputStates = nullptr;
    std::string Reason = "scene composite pass is not initialized";
};

SceneCompositePass::SceneCompositePass() : mImpl(std::make_unique<Impl>()) {}
SceneCompositePass::~SceneCompositePass() { Shutdown(); }

bool SceneCompositePass::Initialize(VkPhysicalDevice physical, VkDevice device,
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
    mImpl->Reason = "NRI scene composite support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physical, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("RGBA16F composite storage unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI scene composite device unavailable");
        const std::array<nri::DescriptorRangeDesc, 14> ranges{
            { { 0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 3, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 4, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 5, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 6, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 7, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 8, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER },
              { 9, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 10, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 11, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 12, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 13, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER } }
        };
        nri::DescriptorSetDesc setDesc{};
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(CompositePush);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &setDesc;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->PipelineLayout) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI scene composite layout failed");
        const auto shader = Compile(interop.Shaders());
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
        pipeline.cache = NriInteropAccess::PipelineCache(interop);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI scene composite pipeline failed");
        nri::SamplerDesc sampler{};
        sampler.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR,
                           nri::Filter::NEAREST};
        sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI scene composite sampler failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool SceneCompositePass::Configure(
    const NriEffectGraphTransientImageBinding& output) {
    if (!mImpl->Ready || !output.Valid() ||
        output.Resource != EffectResource::CompositeColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (mImpl->Image == output.Texture.Image &&
        mImpl->Width == output.Width && mImpl->Height == output.Height) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    InvalidateScreenResources();
    mImpl->Image = output.Texture.Image;
    mImpl->View = output.Texture.SampledView;
    mImpl->StorageView = output.Texture.StorageView;
    mImpl->OutputStates = output.StateTracker;
    mImpl->Width = output.Width;
    mImpl->Height = output.Height;
    return true;
#endif
}

bool SceneCompositePass::Execute(VkCommandBuffer command, uint32_t frameSlot, uint64_t frameId, VkImage colorImage, VkFormat colorFormat,
                                 VkImage aoImage, VkFormat aoFormat, VkImage reflectionImage,
                                 VkFormat reflectionFormat, VkImage materialImage, VkFormat materialFormat,
                                 VkImage normalImage, VkFormat normalFormat, VkImage transparentDepthImage,
                                 VkFormat transparentDepthFormat, VkImage ambientImage, VkFormat ambientFormat,
                                 VkImage depthImage, VkFormat depthFormat, VkImage fogImage, VkFormat fogFormat,
                                 VkImage outlineGeometryImage, VkFormat outlineGeometryFormat,
                                 VkImage diffuseIlluminationImage, VkFormat diffuseIlluminationFormat,
                                 VkImage diffuseAlbedoImage, VkFormat diffuseAlbedoFormat,
                                 bool globalIlluminationEnabled, float diffuseIntensity, bool aoEnabled,
                                 uint32_t aoDebugView, bool reflectionsEnabled, float reflectionStrength, uint32_t reflectionDebug,
                                 bool outlineEnabled, const ToonStyleSettings& outlineSettings,
                                 SceneColorEncoding inputEncoding, const EffectPassBarrierPlan& barrierPlan) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::CompositeColor);
    if (mImpl->Width == 0U || mImpl->OutputStates == nullptr ||
        colorImage == VK_NULL_HANDLE ||
        aoImage == VK_NULL_HANDLE || reflectionImage == VK_NULL_HANDLE ||
        materialImage == VK_NULL_HANDLE ||
        normalImage == VK_NULL_HANDLE ||
        transparentDepthImage == VK_NULL_HANDLE ||
        ambientImage == VK_NULL_HANDLE ||
        depthImage == VK_NULL_HANDLE ||
        diffuseIlluminationImage == VK_NULL_HANDLE || diffuseAlbedoImage == VK_NULL_HANDLE ||
        !IsKnownSceneColorEncoding(inputEncoding) ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed())
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)command; (void)frameSlot; (void)colorFormat; (void)aoFormat;
    (void)frameId;
    (void)reflectionFormat; (void)materialFormat; (void)normalFormat;
    (void)transparentDepthFormat;
    (void)ambientFormat;
    (void)depthFormat; (void)outlineEnabled;
    (void)outlineSettings; (void)inputEncoding;
    (void)aoEnabled; (void)aoDebugView;
    (void)reflectionsEnabled; (void)reflectionStrength; (void)reflectionDebug;
    (void)barrierPlan; (void)diffuseIlluminationFormat; (void)diffuseAlbedoFormat;
    (void)globalIlluminationEnabled; (void)diffuseIntensity; 
    return false;
#else
    nri::CommandBuffer* nriCommand =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (nriCommand == nullptr) return false;
    const std::array<VkImage, 12> images{ colorImage,  aoImage,          reflectionImage, materialImage,
                                          normalImage, depthImage,          ambientImage,    transparentDepthImage,
                                          fogImage,    outlineGeometryImage, diffuseIlluminationImage,
                                          diffuseAlbedoImage };
    const std::array<VkFormat, 12> formats{
        colorFormat, aoFormat,   reflectionFormat,       materialFormat, normalFormat,
        depthFormat, ambientFormat, transparentDepthFormat, fogFormat,      outlineGeometryFormat,
        diffuseIlluminationFormat, diffuseAlbedoFormat
    };
    std::array<nri::Descriptor*, 12> sampledDescriptors{};
    for (uint32_t i = 0; i < images.size(); ++i) {
        if (!mImpl->Interop->WrapTexture(
                images[i], formats[i], VK_IMAGE_TYPE_2D,
                VK_IMAGE_USAGE_SAMPLED_BIT, mImpl->Width, mImpl->Height))
            return false;
        sampledDescriptors[i] = NriInteropAccess::TextureView(
            *mImpl->Interop, images[i], false);
    }
    nri::Descriptor* outputDescriptor = NriInteropAccess::TextureView(
        *mImpl->Interop, mImpl->Image, true);
    for (nri::Descriptor* descriptor : sampledDescriptors)
        if (descriptor == nullptr) return false;
    if (outputDescriptor == nullptr) return false;

    (void)command;
    auto& frame = mImpl->Frames[frameSlot % kFrameSlots];
    if (frame.FrameId != frameId) {
        frame.FrameId = frameId;
        frame.Used = 0;
    }
    const size_t poolIndex = frame.Used / kDescriptorSetsPerPool;
    // Recorded invocations retain immutable sets until this slot's fence passes.
    if (poolIndex == frame.Pools.size() && !mImpl->AddDescriptorPool(frame))
        return false;
    auto& descriptorPool = frame.Pools[poolIndex];
    auto* descriptorSet = descriptorPool.Sets[frame.Used++ % kDescriptorSetsPerPool];
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    const auto writeTransition = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Image),
        {outputBarrier->DispatchAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Image, writeTransition))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(writeTransition);
    mImpl->OutputStates->Commit(writeTransition);
    std::array<nri::UpdateDescriptorRangeDesc, 13> updates{};
    for (uint32_t i = 0; i < 7U; ++i) {
        updates[i] = {descriptorSet, i, 0,
                      &sampledDescriptors[i], 1};
    }
    updates[7] = {descriptorSet, 7, 0, &outputDescriptor, 1};
    updates[8] = {descriptorSet, 9, 0,
                  &sampledDescriptors[7], 1};
    updates[9] = {descriptorSet, 10, 0, &sampledDescriptors[8], 1};
    updates[10] = {descriptorSet, 11, 0, &sampledDescriptors[9], 1};
    updates[11] = {descriptorSet, 12, 0, &sampledDescriptors[10], 1};
    updates[12] = {descriptorSet, 13, 0, &sampledDescriptors[11], 1};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    const CompositePush push{
        mImpl->Width, mImpl->Height, static_cast<uint32_t>(aoEnabled),
        static_cast<uint32_t>(reflectionsEnabled), reflectionStrength,
        reflectionDebug, static_cast<uint32_t>(outlineEnabled),
        static_cast<uint32_t>(SceneColorIsLinear(inputEncoding)),
        1.0F / static_cast<float>(mImpl->Width),
        1.0F / static_cast<float>(mImpl->Height),
        ToonOutlineRenderWidth(outlineSettings.OutlineWidth, mImpl->Width, mImpl->Height),
        outlineSettings.OutlineDepthSensitivity,
        outlineSettings.OutlineNormalSensitivity,
        outlineSettings.OutlineSoftness,
        {},
        {outlineSettings.OutlineTint[0],
         outlineSettings.OutlineTint[1],
         outlineSettings.OutlineTint[2]},
        outlineSettings.OutlineOpacity,
        static_cast<uint32_t>(globalIlluminationEnabled),
        diffuseIntensity,
        globalIlluminationEnabled ? GlobalIlluminationDebugView() : 0U,
        aoEnabled ? aoDebugView : 0U};
    mImpl->Core->CmdSetDescriptorPool(*nriCommand, *descriptorPool.Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *nriCommand, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*nriCommand, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc set{0, descriptorSet};
    mImpl->Core->CmdSetDescriptorSet(*nriCommand, set);
    const nri::SetRootConstantsDesc constants{0, &push, sizeof(push)};
    mImpl->Core->CmdSetRootConstants(*nriCommand, constants);
    const nri::DispatchDesc dispatch{
        (mImpl->Width + 7U) / 8U, (mImpl->Height + 7U) / 8U, 1U};
    mImpl->Core->CmdDispatch(*nriCommand, dispatch);
    const auto readTransition = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Image),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Image, readTransition))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(readTransition);
    mImpl->OutputStates->Commit(readTransition);
    mImpl->NriBarriersUsed = true;
    mImpl->OutputEncoding = inputEncoding;
    return true;
#endif
}

void SceneCompositePass::InvalidateScreenResources() {
    mImpl->Image = VK_NULL_HANDLE;
    mImpl->View = VK_NULL_HANDLE;
    mImpl->StorageView = VK_NULL_HANDLE;
    mImpl->OutputStates = nullptr;
    mImpl->Width = mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->OutputEncoding = SceneColorEncoding::Unknown;
    mImpl->LastBarrierExecution = {};
}

void SceneCompositePass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        for (auto& frame : mImpl->Frames)
            for (const auto& block : frame.Pools)
                mImpl->Core->DestroyDescriptorPool(block.Pool);
        if (mImpl->Sampler != nullptr) mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr) mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "scene composite pass is not initialized";
}

bool SceneCompositePass::Available() const { return mImpl->Ready; }
VkImage SceneCompositePass::OutputImage() const { return mImpl->Image; }
VkImageView SceneCompositePass::OutputView() const { return mImpl->View; }
bool SceneCompositePass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr && mImpl->Interop->OwnsTexture(mImpl->Image);
}
bool SceneCompositePass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Ready && mImpl->Pipeline != nullptr &&
           mImpl->Sampler != nullptr;
#else
    return false;
#endif
}
bool SceneCompositePass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
SceneColorEncoding SceneCompositePass::OutputEncoding() const {
    return mImpl->OutputEncoding;
}
const EffectPassBarrierExecution& SceneCompositePass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& SceneCompositePass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Extension
#endif
