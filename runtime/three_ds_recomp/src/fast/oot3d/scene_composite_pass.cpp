#include "fast/oot3d/scene_composite_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/scene_composite.h"
#include "fast/oot3d/toon_outline_shader.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <shaderc/shaderc.hpp>
#include <array>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {
constexpr uint32_t kFrameSlots = 2U;
struct CompositePush {
    uint32_t Width = 0, Height = 0, Cacao = 0, Reflections = 0;
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
};
static_assert(sizeof(CompositePush) == 80U);

std::vector<uint32_t> Compile() {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        BuildSceneCompositeComputeShader(), shaderc_compute_shader,
        "oot3d_scene_composite.comp", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(result.GetErrorMessage());
    return {result.cbegin(), result.cend()};
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
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kFrameSlots> Sets{};
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
        const std::array<nri::DescriptorRangeDesc, 12> ranges{
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
              { 11, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER } }
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
        const auto shader = Compile();
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
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
    if (mImpl->Width != 0U && vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    InvalidateScreenResources();
    try {
        mImpl->Image = output.Texture.Image;
        mImpl->View = output.Texture.SampledView;
        mImpl->StorageView = output.Texture.StorageView;
        mImpl->OutputStates = output.StateTracker;
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kFrameSlots;
        pool.samplerMaxNum = kFrameSlots;
        pool.textureMaxNum = 10U * kFrameSlots;
        pool.storageTextureMaxNum = kFrameSlots;
        nri::Device* device = NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(*device, pool, mImpl->Pool) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI scene composite pool failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->PipelineLayout, 0, mImpl->Sets.data(),
                kFrameSlots, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI scene composite sets failed");
        for (uint32_t slot = 0; slot < kFrameSlots; ++slot) {
            const nri::UpdateDescriptorRangeDesc update{
                mImpl->Sets[slot], 8, 0, &mImpl->Sampler, 1};
            mImpl->Core->UpdateDescriptorRanges(&update, 1);
        }
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool SceneCompositePass::Execute(VkCommandBuffer command, uint32_t frameSlot, VkImage colorImage, VkFormat colorFormat,
                                 VkImage cacaoImage, VkFormat cacaoFormat, VkImage reflectionImage,
                                 VkFormat reflectionFormat, VkImage materialImage, VkFormat materialFormat,
                                 VkImage normalImage, VkFormat normalFormat, VkImage transparentDepthImage,
                                 VkFormat transparentDepthFormat, VkImage ambientImage, VkFormat ambientFormat,
                                 VkImage depthImage, VkFormat depthFormat, VkImage fogImage, VkFormat fogFormat,
                                 VkImage outlineGeometryImage, VkFormat outlineGeometryFormat, bool cacaoEnabled,
                                 bool reflectionsEnabled, float reflectionStrength, uint32_t reflectionDebug,
                                 bool outlineEnabled, const ToonStyleSettings& outlineSettings,
                                 SceneColorEncoding inputEncoding, const EffectPassBarrierPlan& barrierPlan) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::CompositeColor);
    if (mImpl->Width == 0U || mImpl->OutputStates == nullptr ||
        colorImage == VK_NULL_HANDLE ||
        cacaoImage == VK_NULL_HANDLE || reflectionImage == VK_NULL_HANDLE ||
        materialImage == VK_NULL_HANDLE ||
        normalImage == VK_NULL_HANDLE ||
        transparentDepthImage == VK_NULL_HANDLE ||
        ambientImage == VK_NULL_HANDLE ||
        depthImage == VK_NULL_HANDLE ||
        !IsKnownSceneColorEncoding(inputEncoding) ||
        !barrierPlan.Valid() ||
        outputBarrier == nullptr || !outputBarrier->Managed())
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)command; (void)frameSlot; (void)colorFormat; (void)cacaoFormat;
    (void)reflectionFormat; (void)materialFormat; (void)normalFormat;
    (void)transparentDepthFormat;
    (void)ambientFormat;
    (void)depthFormat; (void)outlineEnabled;
    (void)outlineSettings; (void)inputEncoding;
    (void)cacaoEnabled;
    (void)reflectionsEnabled; (void)reflectionStrength; (void)reflectionDebug;
    (void)barrierPlan;
    return false;
#else
    nri::CommandBuffer* nriCommand =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (nriCommand == nullptr) return false;
    const std::array<VkImage, 10> images{ colorImage,  cacaoImage,          reflectionImage, materialImage,
                                          normalImage, depthImage,          ambientImage,    transparentDepthImage,
                                          fogImage,    outlineGeometryImage };
    const std::array<VkFormat, 10> formats{
        colorFormat, cacaoFormat,   reflectionFormat,       materialFormat, normalFormat,
        depthFormat, ambientFormat, transparentDepthFormat, fogFormat,      outlineGeometryFormat
    };
    std::array<nri::Descriptor*, 10> sampledDescriptors{};
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
    const uint32_t slot = frameSlot % kFrameSlots;
    std::array<nri::UpdateDescriptorRangeDesc, 11> updates{};
    for (uint32_t i = 0; i < 7U; ++i) {
        updates[i] = {mImpl->Sets[slot], i, 0,
                      &sampledDescriptors[i], 1};
    }
    updates[7] = {mImpl->Sets[slot], 7, 0, &outputDescriptor, 1};
    updates[8] = {mImpl->Sets[slot], 9, 0,
                  &sampledDescriptors[7], 1};
    updates[9] = {mImpl->Sets[slot], 10, 0, &sampledDescriptors[8], 1};
    updates[10] = { mImpl->Sets[slot], 11, 0, &sampledDescriptors[9], 1 };
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    const CompositePush push{
        mImpl->Width, mImpl->Height, static_cast<uint32_t>(cacaoEnabled),
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
        outlineSettings.OutlineOpacity};
    mImpl->Core->CmdSetDescriptorPool(*nriCommand, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *nriCommand, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*nriCommand, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc set{0, mImpl->Sets[slot]};
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
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->Pool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
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
           mImpl->Pool != nullptr && mImpl->Sampler != nullptr;
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

} // namespace Fast::Oot3d
#endif
