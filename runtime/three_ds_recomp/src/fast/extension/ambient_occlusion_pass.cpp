#include "fast/extension/ambient_occlusion_pass.h"
#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/resource_state_tracker.h"
#include "fast/renderer/gtao_shader.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <algorithm>
#include <array>
#include <stdexcept>

namespace Fast::Extension {
struct AmbientOcclusionPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    std::array<nri::Pipeline*, 2> Pipelines{};
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, 2> Sets{};
#endif
    NriOwnedTexture2D Scratch;
    NriOwnedTexture2D Output;
    std::array<VkImage, 2> Inputs{};
    std::array<VkFormat, 2> Formats{};
    uint32_t Width = 0, Height = 0;
    bool Ready = false;
    ResourceStateTracker PrivateStates;
    ResourceStateTracker* OutputStates = nullptr;
    EffectPassBarrierExecution LastBarriers;
    std::string Reason = "NRI ambient occlusion is not initialized";
};
AmbientOcclusionPass::AmbientOcclusionPass() : mImpl(std::make_unique<Impl>()) {
}
AmbientOcclusionPass::~AmbientOcclusionPass() {
    Shutdown();
}

bool AmbientOcclusionPass::Initialize(VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)physical;
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) !=
            (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            throw std::runtime_error("NRI AO requires sampled/storage RG16F");
        auto* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI AO device unavailable");
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
        root.size = sizeof(Renderer::GtaoConstants);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(*nriDevice, layout, mImpl->Layout) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI AO pipeline layout failed");
        const std::array sources{ Renderer::BuildMultiscaleGtaoShader(), Renderer::BuildMultiscaleGtaoResolveShader() };
        constexpr std::array names{ "multiscale_gtao.comp", "multiscale_gtao_resolve.comp" };
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto spirv = interop.Shaders().Resolve(sources[i], Renderer::SpirvStage::Compute, names[i]);
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = mImpl->Layout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = spirv.data();
            pipeline.shader.size = spirv.size() * sizeof(uint32_t);
            pipeline.cache = NriInteropAccess::PipelineCache(interop);
            if (mImpl->Core->CreateComputePipeline(*nriDevice, pipeline, mImpl->Pipelines[i]) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI AO pipeline failed");
        }
        nri::SamplerDesc sampler{};
        sampler.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        sampler.addressModes = { nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE,
                                 nri::AddressMode::CLAMP_TO_EDGE };
        if (mImpl->Core->CreateSampler(*nriDevice, sampler, mImpl->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI AO sampler failed");
        mImpl->Ready = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& error) {
        mImpl->Reason = error.what();
        return false;
    }
#endif
}

bool AmbientOcclusionPass::Configure(const NriEffectGraphTransientImageBinding& output, VkImage depth,
                                     VkFormat depthFormat, VkImage normal, VkFormat normalFormat) {
    const std::array inputs{ depth, normal };
    const std::array formats{ depthFormat, normalFormat };
    constexpr auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    if (!Available() || !output.Valid() || output.Resource != EffectResource::AmbientOcclusion ||
        output.Format != VK_FORMAT_R32_SFLOAT || output.MipLevels != 1 || output.Layers != 1 ||
        output.Samples != VK_SAMPLE_COUNT_1_BIT || (output.Usage & usage) != usage ||
        output.Texture.SampledView == VK_NULL_HANDLE || output.Texture.StorageView == VK_NULL_HANDLE ||
        depth == VK_NULL_HANDLE || normal == VK_NULL_HANDLE)
        return false;
    if (mImpl->Width == output.Width && mImpl->Height == output.Height && mImpl->Output.Image == output.Texture.Image &&
        mImpl->OutputStates == output.StateTracker && mImpl->Inputs == inputs && mImpl->Formats == formats)
        return true;
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0 && !mImpl->Interop->CompleteRecordedWork())
        return false;
    InvalidateScreenResources();
    try {
        if (!mImpl->Interop->CreateOwnedTexture2D((output.Width + 1) / 2, (output.Height + 1) / 2,
                                                  VK_FORMAT_R16G16_SFLOAT, mImpl->Scratch))
            throw std::runtime_error("NRI AO scratch allocation failed");
        mImpl->Output = output.Texture;
        mImpl->OutputStates = output.StateTracker;
        std::array<nri::Descriptor*, 2> sources{};
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!mImpl->Interop->WrapTexture(inputs[i], formats[i], VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                             output.Width, output.Height))
                throw std::runtime_error("NRI AO input wrapping failed");
            sources[i] = NriInteropAccess::TextureView(*mImpl->Interop, inputs[i], false);
            if (!sources[i])
                throw std::runtime_error("NRI AO input view unavailable");
        }
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = 2;
        pool.textureMaxNum = 6;
        pool.storageTextureMaxNum = 2;
        pool.samplerMaxNum = 2;
        auto* device = NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(*device, pool, mImpl->Pool) != nri::Result::SUCCESS ||
            mImpl->Core->AllocateDescriptorSets(*mImpl->Pool, *mImpl->Layout, 0, mImpl->Sets.data(), 2, 0) !=
                nri::Result::SUCCESS)
            throw std::runtime_error("NRI AO descriptor allocation failed");
        auto* raw = NriInteropAccess::TextureView(*mImpl->Interop, mImpl->Scratch.Image, false);
        for (size_t i = 0; i < 2; ++i) {
            auto* destination = NriInteropAccess::TextureView(
                *mImpl->Interop, i == 0 ? mImpl->Scratch.Image : mImpl->Output.Image, true);
            // The horizon program does not read binding 2. Bind an input rather
            // than its write target so even unused descriptors have a valid layout.
            auto* intermediate = i == 0 ? sources[0] : raw;
            if (!destination || !intermediate)
                throw std::runtime_error("NRI AO output view unavailable");
            auto* set = mImpl->Sets[i];
            const std::array<nri::UpdateDescriptorRangeDesc, 5> updates{ { { set, 0, 0, &sources[0], 1 },
                                                                           { set, 1, 0, &sources[1], 1 },
                                                                           { set, 2, 0, &intermediate, 1 },
                                                                           { set, 3, 0, &destination, 1 },
                                                                           { set, 4, 0, &mImpl->Sampler, 1 } } };
            mImpl->Core->UpdateDescriptorRanges(updates.data(), uint32_t(updates.size()));
        }
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        mImpl->Inputs = inputs;
        mImpl->Formats = formats;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& error) {
        mImpl->Reason = error.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool AmbientOcclusionPass::Execute(uint32_t frameSlot, const PerspectiveViewState& view, bool wBuffer,
                                   const EffectsSettings& settings, const EffectPassBarrierPlan& barriers,
                                   PicaSurfaceCoordinates coordinates) {
    mImpl->LastBarriers = {};
    const auto* output = barriers.Find(EffectResource::AmbientOcclusion);
    if (!barriers.Valid() || !output || !output->Managed() || !mImpl->OutputStates ||
        output->DispatchAccess != ResourceAccess::ComputeWrite ||
        output->CompletionAccess != ResourceAccess::ShaderRead || !output->DownstreamRead || view.NearPlane <= 0 ||
        view.FarPlane <= view.NearPlane)
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameSlot;
    (void)settings;
    (void)coordinates;
    (void)wBuffer;
    return false;
#else
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (!command || !mImpl->Sets[0] || !mImpl->Sets[1])
        return false;
    Renderer::GtaoConstants push;
    push.Width = mImpl->Width;
    push.Height = mImpl->Height;
    push.Quality = settings.AoQuality;
    const auto zero = coordinates.StorageToViewNdc({ 0, 0 });
    const auto x = coordinates.StorageToViewNdc({ 1, 0 });
    const auto y = coordinates.StorageToViewNdc({ 0, 1 });
    push.ViewX = { (x[0] - zero[0]) / view.Projection[0], (y[0] - zero[0]) / view.Projection[0],
                   (zero[0] + view.Projection[8]) / view.Projection[0], 0 };
    push.ViewY = { (x[1] - zero[1]) / view.Projection[5], (y[1] - zero[1]) / view.Projection[5],
                   (zero[1] + view.Projection[9]) / view.Projection[5], 0 };
    push.Depth = { wBuffer ? view.NearPlane : view.NearPlane * view.FarPlane, view.FarPlane,
                   view.FarPlane - view.NearPlane, wBuffer ? 1.0F : 0.0F };
    push.RadiusFade = { settings.AoDetailRadius, settings.AoRadius, settings.AoFadeOutFrom, settings.AoFadeOutTo };
    push.Response = { settings.AoDetailStrength, settings.AoStrength, settings.AoShadowPower, settings.AoShadowClamp };
    // Decoding and renormalizing RGB8 introduces at most sqrt(3)/(255-sqrt(3))
    // angular error. Use its conservative 2/255 bound, not a scene-dependent bias.
    const float normalError =
        mImpl->Formats[1] == VK_FORMAT_R8G8B8A8_UNORM || mImpl->Formats[1] == VK_FORMAT_B8G8R8A8_UNORM ? 2.0F / 255.0F
        : mImpl->Formats[1] == VK_FORMAT_R16G16B16A16_SFLOAT                                           ? 1.0F / 1024.0F
                                                                                                       : 0.0F;
    push.Filter = { settings.AoSharpness, normalError * normalError, 0, 0 };
    mImpl->LastBarriers = barriers.BeginOutputExecution(EffectResource::AmbientOcclusion);
    const auto transition = [&](VkImage image, ResourceStateTracker& states, ResourceAccess access, bool graph) {
        const auto change = states.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, image, change))
            return false;
        states.Commit(change);
        if (graph)
            mImpl->LastBarriers.RecordGraphTransition(change);
        else
            mImpl->LastBarriers.RecordPrivateTransition(change);
        return true;
    };
    if (!transition(mImpl->Scratch.Image, mImpl->PrivateStates, ResourceAccess::ComputeWrite, false) ||
        !transition(mImpl->Output.Image, *mImpl->OutputStates, output->DispatchAccess, true))
        return false;
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mImpl->Layout);
    const auto dispatch = [&](uint32_t index, uint32_t width, uint32_t height) {
        mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipelines[index]);
        const nri::SetDescriptorSetDesc set{ 0, mImpl->Sets[index] };
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const nri::SetRootConstantsDesc constants{ 0, &push, sizeof(push) };
        mImpl->Core->CmdSetRootConstants(*command, constants);
        const uint32_t groupWidth = index == 0 ? 8U : 16U;
        mImpl->Core->CmdDispatch(*command, { (width + groupWidth - 1) / groupWidth, (height + 7) / 8, 1 });
    };
    dispatch(0, (mImpl->Width + 1) / 2, (mImpl->Height + 1) / 2);
    if (!transition(mImpl->Scratch.Image, mImpl->PrivateStates, ResourceAccess::ShaderRead, false))
        return false;
    dispatch(1, mImpl->Width, mImpl->Height);
    return transition(mImpl->Output.Image, *mImpl->OutputStates, output->CompletionAccess, true);
#endif
}
void AmbientOcclusionPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core && mImpl->Pool)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
    if (mImpl->Interop)
        mImpl->Interop->DestroyOwnedTexture(mImpl->Scratch.Image);
    mImpl->Scratch = {};
    mImpl->Output = {};
    mImpl->OutputStates = nullptr;
    mImpl->Width = mImpl->Height = 0;
    mImpl->Inputs = {};
    mImpl->Formats = {};
    mImpl->PrivateStates.Clear();
    mImpl->LastBarriers = {};
}
void AmbientOcclusionPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core) {
        if (mImpl->Sampler)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        for (auto* pipeline : mImpl->Pipelines)
            if (pipeline)
                mImpl->Core->DestroyPipeline(pipeline);
        if (mImpl->Layout)
            mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
    }
#endif
    *mImpl = {};
}
bool AmbientOcclusionPass::Available() const {
    return mImpl->Ready;
}
VkImage AmbientOcclusionPass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView AmbientOcclusionPass::OutputView() const {
    return mImpl->Output.SampledView;
}
bool AmbientOcclusionPass::OutputOwnedByNri() const {
    return mImpl->Interop && mImpl->Interop->OwnsTexture(mImpl->Output.Image);
}
const EffectPassBarrierExecution& AmbientOcclusionPass::LastBarrierExecution() const {
    return mImpl->LastBarriers;
}
const std::string& AmbientOcclusionPass::UnavailableReason() const {
    return mImpl->Reason;
}
} // namespace Fast::Extension
#endif
