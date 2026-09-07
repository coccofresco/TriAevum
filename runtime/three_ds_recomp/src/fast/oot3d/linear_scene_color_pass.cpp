#include "fast/oot3d/linear_scene_color_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"

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

struct LinearSceneColorPush {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t DecodeSrgb = 0;
    uint32_t Reserved = 0;
};
static_assert(sizeof(LinearSceneColorPush) == 16U);

std::vector<uint32_t> Compile() {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        BuildLinearSceneColorComputeShader(), shaderc_compute_shader,
        "oot3d_linear_scene_color.comp", options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(result.GetErrorMessage());
    return {result.cbegin(), result.cend()};
}

} // namespace

struct LinearSceneColorPass::Impl {
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
    NriOwnedTexture2D Output{};
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Ready = false;
    bool NriBarriersUsed = false;
    bool DecodedSrgb = false;
    ResourceStateTracker* OutputStates = nullptr;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason = "linear scene color pass is not initialized";
};

LinearSceneColorPass::LinearSceneColorPass()
    : mImpl(std::make_unique<Impl>()) {}
LinearSceneColorPass::~LinearSceneColorPass() { Shutdown(); }

bool LinearSceneColorPass::Initialize(
    VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)physical;
    mImpl->Reason = "NRI linear scene color support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physical, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U) {
            throw std::runtime_error(
                "RGBA16F linear scene color storage is unavailable");
        }
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error(
                "NRI linear scene color device is unavailable");

        const std::array<nri::DescriptorRangeDesc, 3> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER},
        }};
        nri::DescriptorSetDesc setDesc{};
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(LinearSceneColorPush);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &setDesc;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags =
            nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->PipelineLayout) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI linear scene color layout creation failed");
        }

        const auto shader = Compile();
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *nriDevice, pipeline, mImpl->Pipeline) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI linear scene color pipeline creation failed");
        }

        nri::SamplerDesc sampler{};
        sampler.filters = {nri::Filter::NEAREST, nri::Filter::NEAREST,
                           nri::Filter::NEAREST};
        sampler.addressModes = {
            nri::AddressMode::CLAMP_TO_EDGE,
            nri::AddressMode::CLAMP_TO_EDGE,
            nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, sampler, mImpl->Sampler) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI linear scene color sampler creation failed");
        }
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool LinearSceneColorPass::Configure(
    const NriEffectGraphTransientImageBinding& output) {
    if (!mImpl->Ready || !output.Valid() ||
        output.Resource != EffectResource::LinearWorkingColor ||
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
        mImpl->OutputStates == output.StateTracker) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0U &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    InvalidateScreenResources();
    try {
        mImpl->Output = output.Texture;
        mImpl->OutputStates = output.StateTracker;
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kFrameSlots;
        pool.samplerMaxNum = kFrameSlots;
        pool.textureMaxNum = kFrameSlots;
        pool.storageTextureMaxNum = kFrameSlots;
        nri::Device* device =
            NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *device, pool, mImpl->Pool) != nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI linear scene color descriptor pool failed");
        }
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->PipelineLayout, 0,
                mImpl->Sets.data(), kFrameSlots, 0) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI linear scene color descriptor allocation failed");
        }
        for (nri::DescriptorSet* set : mImpl->Sets) {
            const nri::UpdateDescriptorRangeDesc update{
                set, 2, 0, &mImpl->Sampler, 1};
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

bool LinearSceneColorPass::Execute(
    VkCommandBuffer commandBuffer, uint32_t frameSlot,
    VkImage sourceImage, VkFormat sourceFormat,
    uint32_t sourceWidth, uint32_t sourceHeight,
    SceneColorEncoding sourceEncoding,
    const EffectPassBarrierPlan& barrierPlan) {
    const auto colorPolicy = BuildLinearSceneColorPolicy(sourceEncoding);
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::LinearWorkingColor);
    if (mImpl->Width == 0U || mImpl->OutputStates == nullptr ||
        sourceImage == VK_NULL_HANDLE || sourceWidth == 0U ||
        sourceHeight == 0U || !colorPolicy.Valid ||
        !barrierPlan.Valid() || outputBarrier == nullptr ||
        !outputBarrier->Managed()) {
        mImpl->Reason = "linear scene color inputs are unavailable";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer;
    (void)frameSlot;
    (void)sourceFormat;
    (void)sourceWidth;
    (void)sourceHeight;
    (void)sourceEncoding;
    (void)barrierPlan;
    return false;
#else
    (void)commandBuffer;
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr) {
        mImpl->Reason = "linear scene color NRI command is unavailable";
        return false;
    }
    if (!mImpl->Interop->WrapTexture(
            sourceImage, sourceFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT, sourceWidth, sourceHeight)) {
        mImpl->Reason = "linear scene color source wrapping failed";
        return false;
    }
    nri::Descriptor* source = NriInteropAccess::TextureView(
        *mImpl->Interop, sourceImage, false);
    nri::Descriptor* output = NriInteropAccess::TextureView(
        *mImpl->Interop, mImpl->Output.Image, true);
    if (source == nullptr || output == nullptr) {
        mImpl->Reason = "linear scene color views are unavailable";
        return false;
    }

    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::LinearWorkingColor);
    const auto writable = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Output.Image),
        {outputBarrier->DispatchAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Output.Image, writable)) {
        mImpl->Reason = "linear scene color write transition failed";
        return false;
    }
    mImpl->LastBarrierExecution.RecordGraphTransition(writable);
    mImpl->OutputStates->Commit(writable);

    const uint32_t slot = frameSlot % kFrameSlots;
    const std::array<nri::Descriptor*, 2> descriptors{source, output};
    const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
        {mImpl->Sets[slot], 0, 0, &descriptors[0], 1},
        {mImpl->Sets[slot], 1, 0, &descriptors[1], 1},
    }};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    const LinearSceneColorPush push{
        mImpl->Width, mImpl->Height,
        static_cast<uint32_t>(colorPolicy.DecodeSrgb), 0U};
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
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

    const auto readable = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Output.Image),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Output.Image, readable)) {
        mImpl->Reason = "linear scene color read transition failed";
        return false;
    }
    mImpl->LastBarrierExecution.RecordGraphTransition(readable);
    mImpl->OutputStates->Commit(readable);
    mImpl->NriBarriersUsed = true;
    mImpl->DecodedSrgb = colorPolicy.DecodeSrgb;
    mImpl->Reason.clear();
    return true;
#endif
}

void LinearSceneColorPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->Pool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
    mImpl->Output = {};
    mImpl->OutputStates = nullptr;
    mImpl->Width = 0;
    mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->DecodedSrgb = false;
    mImpl->LastBarrierExecution = {};
}

void LinearSceneColorPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    mImpl = std::make_unique<Impl>();
}

bool LinearSceneColorPass::Available() const { return mImpl->Ready; }
VkImage LinearSceneColorPass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView LinearSceneColorPass::OutputView() const {
    return mImpl->Output.SampledView;
}
bool LinearSceneColorPass::OutputOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Output.Image);
}
bool LinearSceneColorPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Ready && mImpl->Pipeline != nullptr &&
           mImpl->Pool != nullptr && mImpl->Sampler != nullptr;
#else
    return false;
#endif
}
bool LinearSceneColorPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
const EffectPassBarrierExecution&
LinearSceneColorPass::LastBarrierExecution() const {
    return mImpl->LastBarrierExecution;
}
bool LinearSceneColorPass::DecodedSrgbLastExecute() const {
    return mImpl->DecodedSrgb;
}
const std::string& LinearSceneColorPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
