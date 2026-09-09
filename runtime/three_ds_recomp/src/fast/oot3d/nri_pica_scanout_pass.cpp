#include "fast/oot3d/nri_pica_scanout_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#include <Extensions/NRIHelper.h>
#include <NRI.h>
#endif

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {
namespace {
constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kDrawsPerFrame = 8U;

std::vector<uint32_t> Compile(const std::string& source,
                              shaderc_shader_kind kind,
                              const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result =
        compiler.CompileGlslToSpv(source, kind, name, options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        throw std::runtime_error(result.GetErrorMessage());
    return {result.cbegin(), result.cend()};
}
} // namespace

struct NriPicaScanoutPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    VkFormat TargetFormat = VK_FORMAT_UNDEFINED;
    ResourceStateTracker TargetStates;
    EffectPassBarrierExecution LastBarrierExecution;
    std::string Reason = "NRI PICA scanout is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* BasePipeline = nullptr;
    nri::Pipeline* OverlayPipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    struct FrameDescriptors {
        nri::DescriptorPool* Pool = nullptr;
        std::array<nri::DescriptorSet*, kDrawsPerFrame> Sets{};
        uint64_t FrameId = std::numeric_limits<uint64_t>::max();
        uint32_t Used = 0;
    };
    std::array<FrameDescriptors, kFrameSlots> Frames{};
#endif
};

NriPicaScanoutPass::NriPicaScanoutPass()
    : mImpl(std::make_unique<Impl>()) {}
NriPicaScanoutPass::~NriPicaScanoutPass() { Shutdown(); }

bool NriPicaScanoutPass::Initialize(
    VkDevice device, NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (const char* enabled = std::getenv("OOT3D_GRAPHICS_NRI_SCANOUT");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason = "NRI PICA scanout disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    if (!interop.DynamicRenderingAvailable()) {
        mImpl->Reason =
            "NRI PICA scanout requires VK_KHR_dynamic_rendering";
        return false;
    }
    if (device == VK_NULL_HANDLE) {
        mImpl->Reason = "NRI PICA scanout device is invalid";
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI PICA scanout support was not compiled";
    return false;
#else
    try {
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI scanout device unavailable");

        std::array<nri::DescriptorRangeDesc, 12> ranges{};
        for (uint32_t binding = 0; binding < 11U; ++binding) {
            ranges[binding] = {
                binding, 1, nri::DescriptorType::TEXTURE,
                nri::StageBits::FRAGMENT_SHADER};
        }
        ranges[11] = { 11, 1, nri::DescriptorType::SAMPLER, nri::StageBits::FRAGMENT_SHADER };
        nri::DescriptorSetDesc setDesc{};
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(PicaScanoutPushConstants);
        root.shaderStages = nri::StageBits::FRAGMENT_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &setDesc;
        layout.descriptorSetNum = 1;
        layout.shaderStages =
            nri::StageBits::VERTEX_SHADER |
            nri::StageBits::FRAGMENT_SHADER;
        layout.flags =
            nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->PipelineLayout) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI scanout layout creation failed");

        nri::SamplerDesc sampler{};
        sampler.filters = {nri::Filter::LINEAR, nri::Filter::LINEAR,
                           nri::Filter::NEAREST};
        sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *nriDevice, sampler, mImpl->Sampler) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI scanout sampler creation failed");

        for (auto& frame : mImpl->Frames) {
            nri::DescriptorPoolDesc pool{};
            pool.descriptorSetMaxNum = kDrawsPerFrame;
            pool.textureMaxNum = 11U * kDrawsPerFrame;
            pool.samplerMaxNum = kDrawsPerFrame;
            if (mImpl->Core->CreateDescriptorPool(
                    *nriDevice, pool, frame.Pool) != nri::Result::SUCCESS)
                throw std::runtime_error(
                    "NRI scanout descriptor pool creation failed");
            if (mImpl->Core->AllocateDescriptorSets(
                    *frame.Pool, *mImpl->PipelineLayout, 0,
                    frame.Sets.data(), kDrawsPerFrame, 0) !=
                nri::Result::SUCCESS)
                throw std::runtime_error(
                    "NRI scanout descriptor allocation failed");
            for (nri::DescriptorSet* set : frame.Sets) {
                const nri::UpdateDescriptorRangeDesc update{ set, 11, 0, &mImpl->Sampler, 1 };
                mImpl->Core->UpdateDescriptorRanges(&update, 1);
            }
        }
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool NriPicaScanoutPass::Configure(VkFormat targetFormat) {
    if (mImpl->Reason.size() != 0U ||
        targetFormat == VK_FORMAT_UNDEFINED)
        return false;
    if (mImpl->TargetFormat == targetFormat &&
        PipelineOwnedByNri())
        return true;
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->TargetFormat != VK_FORMAT_UNDEFINED &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS)
        return false;
    if (mImpl->BasePipeline != nullptr)
        mImpl->Core->DestroyPipeline(mImpl->BasePipeline);
    if (mImpl->OverlayPipeline != nullptr)
        mImpl->Core->DestroyPipeline(mImpl->OverlayPipeline);
    mImpl->BasePipeline = nullptr;
    mImpl->OverlayPipeline = nullptr;
    try {
        const auto vertex = Compile(
            BuildPicaScanoutVertexShader(), shaderc_vertex_shader,
            "oot3d_nri_pica_scanout.vert");
        const auto fragment = Compile(
            BuildPicaScanoutFragmentShader(true), shaderc_fragment_shader,
            "oot3d_nri_pica_scanout.frag");
        const std::array<nri::ShaderDesc, 2> shaders{{
            {nri::StageBits::VERTEX_SHADER, vertex.data(),
             vertex.size() * sizeof(uint32_t)},
            {nri::StageBits::FRAGMENT_SHADER, fragment.data(),
             fragment.size() * sizeof(uint32_t)}}};
        nri::ColorAttachmentDesc color{};
        color.format = nri::nriConvertVKFormatToNRI(targetFormat);
        color.colorWriteMask = nri::ColorWriteBits::RGBA;
        if (color.format == nri::Format::UNKNOWN)
            throw std::runtime_error("NRI scanout target format unsupported");
        nri::MultisampleDesc multisample{};
        multisample.sampleMask = 0xFFFFFFFFU;
        multisample.sampleNum = 1;
        nri::GraphicsPipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.inputAssembly.topology = nri::Topology::TRIANGLE_LIST;
        pipeline.rasterization.fillMode = nri::FillMode::SOLID;
        pipeline.rasterization.cullMode = nri::CullMode::NONE;
        pipeline.rasterization.frontCounterClockwise = true;
        pipeline.multisample = &multisample;
        pipeline.outputMerger.colors = &color;
        pipeline.outputMerger.colorNum = 1;
        pipeline.shaders = shaders.data();
        pipeline.shaderNum = static_cast<uint32_t>(shaders.size());
        nri::Device* nriDevice =
            NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateGraphicsPipeline(
                *nriDevice, pipeline, mImpl->BasePipeline) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI base scanout pipeline failed");
        color.blendEnabled = true;
        color.colorBlend = {nri::BlendFactor::SRC_ALPHA,
                            nri::BlendFactor::ONE_MINUS_SRC_ALPHA,
                            nri::BlendOp::ADD};
        color.alphaBlend = {nri::BlendFactor::ONE,
                            nri::BlendFactor::ONE_MINUS_SRC_ALPHA,
                            nri::BlendOp::ADD};
        if (mImpl->Core->CreateGraphicsPipeline(
                *nriDevice, pipeline, mImpl->OverlayPipeline) !=
            nri::Result::SUCCESS)
            throw std::runtime_error("NRI overlay scanout pipeline failed");
        mImpl->TargetFormat = targetFormat;
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool NriPicaScanoutPass::Execute(
    const NriPicaScanoutDesc& desc,
    const EffectPassBarrierPlan& barrierPlan) {
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    (void)barrierPlan;
    return false;
#else
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::PresentationOutput);
    if (!PipelineOwnedByNri() ||
        desc.TargetFormat != mImpl->TargetFormat ||
        desc.CommandBuffer == VK_NULL_HANDLE ||
        desc.TargetImage == VK_NULL_HANDLE ||
        desc.TargetView == VK_NULL_HANDLE ||
        desc.TargetWidth == 0U || desc.TargetHeight == 0U ||
        !barrierPlan.Valid() || outputBarrier == nullptr ||
        !outputBarrier->Managed())
        return false;
    for (VkImage image : desc.InputImages)
        if (image == VK_NULL_HANDLE) return false;

    const uint32_t slot = desc.FrameSlot % kFrameSlots;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(
        *mImpl->Interop, desc.FrameSlot);
    auto& frame = mImpl->Frames[slot];
    if (command == nullptr || frame.Pool == nullptr) return false;
    if (frame.FrameId != desc.FrameId) {
        frame.FrameId = desc.FrameId;
        frame.Used = 0;
    }
    if (frame.Used >= frame.Sets.size()) return false;

    if (!mImpl->Interop->WrapTexture(
            desc.TargetImage, desc.TargetFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, desc.TargetWidth,
            desc.TargetHeight))
        return false;
    nri::Descriptor* colorAttachment =
        NriInteropAccess::ColorAttachmentView(
            *mImpl->Interop, desc.TargetImage);
    if (colorAttachment == nullptr) return false;

    std::array<nri::Descriptor*, 11> textures{};
    for (uint32_t i = 0; i < textures.size(); ++i) {
        if (!mImpl->Interop->WrapTexture(
                desc.InputImages[i], desc.InputFormats[i],
                VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                desc.Push.InvWidth > 0.0F
                    ? static_cast<uint32_t>(1.0F / desc.Push.InvWidth)
                    : 1U,
                desc.Push.InvHeight > 0.0F
                    ? static_cast<uint32_t>(1.0F / desc.Push.InvHeight)
                    : 1U))
            return false;
        textures[i] = NriInteropAccess::TextureView(
            *mImpl->Interop, desc.InputImages[i], false);
        if (textures[i] == nullptr) return false;
    }

    const uintptr_t target =
        reinterpret_cast<uintptr_t>(desc.TargetImage);
    if (!desc.Overlay) {
        mImpl->TargetStates.Forget(target);
    } else if (!mImpl->TargetStates.Find(target).has_value()) {
        mImpl->TargetStates.Commit(
            {target, {}, {ResourceAccess::Present, 0}});
    }
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    const auto writable = mImpl->TargetStates.PlanTransition(
        target, {outputBarrier->DispatchAccess, 0});
    // The host waits for swapchain acquisition at COLOR_ATTACHMENT. Chain the
    // layout transition to that same stage before clearing/reusing the image;
    // a NONE -> COLOR transition could otherwise run while it is still shown.
    const NriTextureTransitionDesc acquiredTarget{
        desc.TargetImage, writable, 0, 0, nri::StageBits::COLOR_ATTACHMENT};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, desc.FrameSlot, &acquiredTarget, 1))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(writable);
    mImpl->TargetStates.Commit(writable);

    nri::DescriptorSet* set = frame.Sets[frame.Used++];
    std::array<nri::UpdateDescriptorRangeDesc, 11> updates{};
    for (uint32_t i = 0; i < updates.size(); ++i)
        updates[i] = {set, i, 0, &textures[i], 1};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));

    nri::AttachmentDesc attachment{};
    attachment.descriptor = colorAttachment;
    attachment.clearValue.color.f = {
        desc.ClearColor[0], desc.ClearColor[1],
        desc.ClearColor[2], desc.ClearColor[3]};
    attachment.loadOp =
        desc.Overlay ? nri::LoadOp::LOAD : nri::LoadOp::CLEAR;
    attachment.storeOp = nri::StoreOp::STORE;
    attachment.resolveOp = nri::ResolveOp::AVERAGE;
    nri::RenderingDesc rendering{};
    rendering.colors = &attachment;
    rendering.colorNum = 1;
    mImpl->Core->CmdSetDescriptorPool(*command, *frame.Pool);
    mImpl->Core->CmdBeginRendering(*command, rendering);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::GRAPHICS, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(
        *command, desc.Overlay ? *mImpl->OverlayPipeline
                               : *mImpl->BasePipeline);
    const nri::SetDescriptorSetDesc setDesc{
        0, set, nri::BindPoint::GRAPHICS};
    mImpl->Core->CmdSetDescriptorSet(*command, setDesc);
    const nri::SetRootConstantsDesc constants{
        0, &desc.Push, sizeof(desc.Push)};
    mImpl->Core->CmdSetRootConstants(*command, constants);
    const nri::Viewport viewport{
        desc.ViewportX, desc.ViewportY,
        desc.ViewportWidth, desc.ViewportHeight,
        0.0F, 1.0F, true};
    const nri::Rect scissor{
        static_cast<int16_t>(std::clamp(
            desc.ScissorX,
            static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
            static_cast<int32_t>(std::numeric_limits<int16_t>::max()))),
        static_cast<int16_t>(std::clamp(
            desc.ScissorY,
            static_cast<int32_t>(std::numeric_limits<int16_t>::min()),
            static_cast<int32_t>(std::numeric_limits<int16_t>::max()))),
        static_cast<nri::Dim_t>(desc.ScissorWidth),
        static_cast<nri::Dim_t>(desc.ScissorHeight)};
    mImpl->Core->CmdSetViewports(*command, &viewport, 1);
    mImpl->Core->CmdSetScissors(*command, &scissor, 1);
    const nri::DrawDesc draw{3, 1, 0, 0};
    mImpl->Core->CmdDraw(*command, draw);
    mImpl->Core->CmdEndRendering(*command);

    const auto presentable = mImpl->TargetStates.PlanTransition(
        target, {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, desc.FrameSlot, desc.TargetImage, presentable))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(presentable);
    mImpl->TargetStates.Commit(presentable);
    return true;
#endif
}

void NriPicaScanoutPass::ResetTargets() {
    mImpl->TargetStates.Clear();
    mImpl->LastBarrierExecution = {};
#ifdef ENABLE_OOT3D_NRI
    for (auto& frame : mImpl->Frames) {
        frame.FrameId = std::numeric_limits<uint64_t>::max();
        frame.Used = 0;
    }
#endif
}

void NriPicaScanoutPass::Shutdown() {
    ResetTargets();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        for (auto& frame : mImpl->Frames)
            if (frame.Pool != nullptr)
                mImpl->Core->DestroyDescriptorPool(frame.Pool);
        if (mImpl->BasePipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->BasePipeline);
        if (mImpl->OverlayPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->OverlayPipeline);
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "NRI PICA scanout is not initialized";
}

bool NriPicaScanoutPass::Available() const {
    return mImpl->Reason.empty() && PipelineOwnedByNri();
}
bool NriPicaScanoutPass::PipelineOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->BasePipeline != nullptr &&
           mImpl->OverlayPipeline != nullptr &&
           mImpl->PipelineLayout != nullptr;
#else
    return false;
#endif
}
bool NriPicaScanoutPass::DescriptorsOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Sampler != nullptr &&
           mImpl->Frames[0].Pool != nullptr &&
           mImpl->Frames[1].Pool != nullptr;
#else
    return false;
#endif
}
const EffectPassBarrierExecution& NriPicaScanoutPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& NriPicaScanoutPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
