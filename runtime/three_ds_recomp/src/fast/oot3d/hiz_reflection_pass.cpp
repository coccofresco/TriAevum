#include "fast/oot3d/hiz_reflection_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/hiz_reflection.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace Fast::Oot3d {
namespace {
struct ReflectionPush {
    uint32_t Width, Height;
    float InvWidth, InvHeight, Strength, MaxDistance, Thickness, EdgeFade;
    uint32_t MaxSteps, HiZMipCount;
    float ProjectionScaleX, ProjectionScaleY;
    float ProjectionOffsetX, ProjectionOffsetY;
    float NearPlane, FarPlane, RoughnessBias, NormalSigma;
    uint32_t SurfaceOrientation;
};
static_assert(sizeof(ReflectionPush) == 76U);

// Water uniforms per frame slot (the dispatches of a slot read its buffer).
constexpr uint32_t kWaterSlots = 2U;
// Scene colours the trace keeps descriptor sets for (see ColorVariants).
constexpr uint32_t kColorVariants = 4U;

std::vector<uint32_t> Compile(Renderer::CachedPassShaderCompiler& shaders, const std::string& source, const char* name) {
    return shaders.Resolve(source, Renderer::SpirvStage::Compute, name);
}
} // namespace

struct HiZReflectionPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* RayPipeline = nullptr;
    nri::Pipeline* FilterPipeline = nullptr;
    nri::Descriptor* LinearSampler = nullptr;
    nri::Descriptor* NearestSampler = nullptr;
    nri::DescriptorPool* DescriptorPool = nullptr;
    std::array<nri::DescriptorSet*, kWaterSlots> RaySets{};
    std::array<nri::DescriptorSet*, kWaterSlots> FilterSets{};
    std::array<nri::Buffer*, kWaterSlots> WaterBuffers{};
    std::array<nri::Descriptor*, kWaterSlots> WaterViews{};
    std::array<void*, kWaterSlots> WaterMapped{};
    // The drawn water's map per slot (binding 9) and the map version it holds.
    std::array<nri::Buffer*, kWaterSlots> MapBuffers{};
    std::array<nri::Descriptor*, kWaterSlots> MapViews{};
    std::array<void*, kWaterSlots> MapMapped{};
    std::array<uint64_t, kWaterSlots> MapVersions{};
    // The descriptor sets of each scene colour read since the last
    // configuration, written once when the colour first appears. The title
    // alternates its colour target between frames; that must select sets,
    // not reconfigure (and wait for) the pass, and never rewrite a set that a
    // recorded frame still uses.
    struct ColorSets {
        VkImage Color = VK_NULL_HANDLE;
        std::array<nri::DescriptorSet*, kWaterSlots> Ray{};
        std::array<nri::DescriptorSet*, kWaterSlots> Filter{};
    };
    std::array<ColorSets, kColorVariants> ColorVariants{};
    uint32_t ColorVariantCount = 0;
#endif
    std::array<VkImage, 2> Images{};
    std::array<VkImageView, 2> Views{};
    std::array<VkImage, 4> Inputs{};
    std::array<VkFormat, 4> InputFormats{};
    uint32_t Width = 0, Height = 0;
    bool Initialized = false, NriBarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker PrivateStates;
    ResourceStateTracker* OutputStates = nullptr;
    std::string Reason = "SSR pass is not initialized";
};

HiZReflectionPass::HiZReflectionPass() : mImpl(std::make_unique<Impl>()) {}
HiZReflectionPass::~HiZReflectionPass() { Shutdown(); }

bool HiZReflectionPass::Initialize(VkPhysicalDevice physicalDevice,
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
    mImpl->Reason = "NRI SSR support was not compiled";
    return false;
#else
    try {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("RGBA16F SSR storage is unavailable");
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI SSR device is unavailable");
        // 8: the scene colour (the filter composes water over it).
        const std::array<nri::DescriptorRangeDesc, 10> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {5, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER},
            {6, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER},
            {7, 1, nri::DescriptorType::CONSTANT_BUFFER, nri::StageBits::COMPUTE_SHADER},
            {8, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER},
            {9, 1, nri::DescriptorType::STRUCTURED_BUFFER, nri::StageBits::COMPUTE_SHADER}}};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(ReflectionPush);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(
                *nriDevice, layout, mImpl->PipelineLayout) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI SSR pipeline layout failed");
        const auto createPipeline = [&](const std::string& source,
                                        const char* name,
                                        nri::Pipeline*& output) {
            const auto shader = Compile(interop.Shaders(), source, name);
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = mImpl->PipelineLayout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = shader.data();
            pipeline.shader.size = shader.size() * sizeof(uint32_t);
            pipeline.cache = NriInteropAccess::PipelineCache(interop);
            if (mImpl->Core->CreateComputePipeline(
                    *nriDevice, pipeline, output) != nri::Result::SUCCESS)
                throw std::runtime_error(std::string("NRI ") + name +
                                         " pipeline failed");
        };
        createPipeline(BuildHiZReflectionComputeShader(),
                       "oot3d_hiz_reflection.comp", mImpl->RayPipeline);
        createPipeline(BuildHiZReflectionBilateralFilterShader(),
                       "oot3d_hiz_reflection_filter.comp",
                       mImpl->FilterPipeline);
        const auto createSampler = [&](nri::Filter filter,
                                       nri::Descriptor*& output) {
            nri::SamplerDesc sampler{};
            sampler.filters = {filter, filter, nri::Filter::NEAREST};
            sampler.mipMax = 16.0F;
            sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE,
                                    nri::AddressMode::CLAMP_TO_EDGE};
            if (mImpl->Core->CreateSampler(*nriDevice, sampler, output) !=
                nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR sampler failed");
        };
        createSampler(nri::Filter::LINEAR, mImpl->LinearSampler);
        createSampler(nri::Filter::NEAREST, mImpl->NearestSampler);
        for (uint32_t slot = 0; slot < kWaterSlots; ++slot) {
            nri::BufferDesc bufferDesc{};
            bufferDesc.size = sizeof(HiZWaterUniforms);
            bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
            if (mImpl->Core->CreateCommittedBuffer(*nriDevice, nri::MemoryLocation::HOST_UPLOAD, 0.0F, bufferDesc,
                                                   mImpl->WaterBuffers[slot]) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR water uniform creation failed");
            nri::BufferViewDesc viewDesc{};
            viewDesc.buffer = mImpl->WaterBuffers[slot];
            viewDesc.type = nri::BufferView::CONSTANT_BUFFER;
            viewDesc.size = sizeof(HiZWaterUniforms);
            if (mImpl->Core->CreateBufferView(viewDesc, mImpl->WaterViews[slot]) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR water uniform view creation failed");
            mImpl->WaterMapped[slot] =
                mImpl->Core->MapBuffer(*mImpl->WaterBuffers[slot], 0, sizeof(HiZWaterUniforms));
            if (mImpl->WaterMapped[slot] == nullptr)
                throw std::runtime_error("NRI SSR water uniform mapping failed");
            const HiZWaterUniforms none{};
            std::memcpy(mImpl->WaterMapped[slot], &none, sizeof(none));
            constexpr uint64_t mapBytes =
                uint64_t(Renderer3ds::kPicaWaterMapSize) * Renderer3ds::kPicaWaterMapSize * 4U * sizeof(float);
            nri::BufferDesc mapDesc{};
            mapDesc.size = mapBytes;
            mapDesc.structureStride = 4U * sizeof(float);
            mapDesc.usage = nri::BufferUsageBits::SHADER_RESOURCE;
            if (mImpl->Core->CreateCommittedBuffer(*nriDevice, nri::MemoryLocation::HOST_UPLOAD, 0.0F, mapDesc,
                                                   mImpl->MapBuffers[slot]) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR water map creation failed");
            nri::BufferViewDesc mapView{};
            mapView.buffer = mImpl->MapBuffers[slot];
            mapView.type = nri::BufferView::STRUCTURED_BUFFER;
            mapView.size = mapBytes;
            mapView.structureStride = 4U * sizeof(float);
            if (mImpl->Core->CreateBufferView(mapView, mImpl->MapViews[slot]) != nri::Result::SUCCESS)
                throw std::runtime_error("NRI SSR water map view creation failed");
            mImpl->MapMapped[slot] = mImpl->Core->MapBuffer(*mImpl->MapBuffers[slot], 0, mapBytes);
            if (mImpl->MapMapped[slot] == nullptr)
                throw std::runtime_error("NRI SSR water map mapping failed");
            mImpl->MapVersions[slot] = 0;
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

bool HiZReflectionPass::Configure(
    const NriEffectGraphTransientImageBinding& output,
    VkImage colorImage, VkFormat colorFormat,
    VkImage hiZImage, VkFormat hiZFormat,
    VkImage normalImage, VkFormat normalFormat,
    VkImage materialImage, VkFormat materialFormat) {
    const std::array<VkImage, 4> inputs{
        colorImage, hiZImage, normalImage, materialImage};
    const std::array<VkFormat, 4> formats{
        colorFormat, hiZFormat, normalFormat, materialFormat};
    if (!mImpl->Initialized || !output.Valid() ||
        output.Resource != EffectResource::ReflectionColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    for (VkImage image : inputs)
        if (image == VK_NULL_HANDLE) return false;
    if (mImpl->Width == output.Width && mImpl->Height == output.Height &&
        mImpl->Images[1] == output.Texture.Image &&
        mImpl->OutputStates == output.StateTracker &&
        mImpl->Inputs == inputs && mImpl->InputFormats == formats) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    // Writes the descriptor sets of one scene colour into the current pool.
    const auto writeColorSets = [&](Impl::ColorSets& variant, VkImage color, VkFormat colorFormat) {
        if (!mImpl->Interop->WrapTexture(color, colorFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT,
                                         output.Width, output.Height))
            throw std::runtime_error("NRI SSR input wrapping failed");
        std::array<nri::Descriptor*, 4> views{};
        for (uint32_t i = 0; i < views.size(); ++i) {
            views[i] = NriInteropAccess::TextureView(*mImpl->Interop, i == 0U ? color : inputs[i], false);
            if (views[i] == nullptr)
                throw std::runtime_error("NRI SSR input view failed");
        }
        std::array<nri::DescriptorSet*, 2U * kWaterSlots> sets{};
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->DescriptorPool, *mImpl->PipelineLayout, 0,
                sets.data(), 2U * kWaterSlots, 0) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI SSR descriptor sets failed");
        const auto updateSet = [&](nri::DescriptorSet* set,
                                   const std::array<nri::Descriptor*, 4>& sources,
                                   VkImage destinationImage, uint32_t slot) {
            nri::Descriptor* destination = NriInteropAccess::TextureView(
                *mImpl->Interop, destinationImage, true);
            if (destination == nullptr)
                throw std::runtime_error("NRI SSR storage view failed");
            std::array<nri::UpdateDescriptorRangeDesc, 10> updates{};
            for (uint32_t i = 0; i < sources.size(); ++i)
                updates[i] = {set, i, 0, &sources[i], 1};
            updates[4] = {set, 4, 0, &destination, 1};
            updates[5] = {set, 5, 0, &mImpl->LinearSampler, 1};
            updates[6] = {set, 6, 0, &mImpl->NearestSampler, 1};
            updates[7] = {set, 7, 0, &mImpl->WaterViews[slot], 1};
            updates[8] = {set, 8, 0, &views[0], 1};
            updates[9] = {set, 9, 0, &mImpl->MapViews[slot], 1};
            mImpl->Core->UpdateDescriptorRanges(
                updates.data(), static_cast<uint32_t>(updates.size()));
        };
        std::array<nri::Descriptor*, 4> filterInputs{
            NriInteropAccess::TextureView(*mImpl->Interop, mImpl->Images[0], false),
            views[1], views[2], views[3]};
        for (uint32_t slot = 0; slot < kWaterSlots; ++slot) {
            variant.Ray[slot] = sets[2U * slot];
            variant.Filter[slot] = sets[2U * slot + 1U];
            updateSet(variant.Ray[slot], views, mImpl->Images[0], slot);
            updateSet(variant.Filter[slot], filterInputs, mImpl->Images[1], slot);
        }
        variant.Color = color;
    };
    const auto selectColor = [&](uint32_t index) {
        mImpl->RaySets = mImpl->ColorVariants[index].Ray;
        mImpl->FilterSets = mImpl->ColorVariants[index].Filter;
        mImpl->Inputs[0] = mImpl->ColorVariants[index].Color;
    };
    if (mImpl->Width == output.Width && mImpl->Height == output.Height &&
        mImpl->Images[1] == output.Texture.Image && mImpl->OutputStates == output.StateTracker &&
        mImpl->InputFormats == formats && std::equal(inputs.begin() + 1, inputs.end(), mImpl->Inputs.begin() + 1)) {
        for (uint32_t i = 0; i < mImpl->ColorVariantCount; ++i)
            if (mImpl->ColorVariants[i].Color == inputs[0]) {
                selectColor(i);
                return true;
            }
        if (mImpl->ColorVariantCount < kColorVariants) {
            try {
                writeColorSets(mImpl->ColorVariants[mImpl->ColorVariantCount], inputs[0], formats[0]);
                selectColor(mImpl->ColorVariantCount++);
                return true;
            } catch (const std::exception& exception) {
                // Configure the pass anew below.
                mImpl->Reason = exception.what();
            }
        }
    }
    if (mImpl->Width != 0U && !mImpl->Interop->CompleteRecordedWork())
        return false;
    InvalidateScreenResources();
    try {
        NriOwnedTexture2D privateOutput;
        if (!mImpl->Interop->CreateOwnedTexture2D(
                output.Width, output.Height,
                VK_FORMAT_R16G16B16A16_SFLOAT, privateOutput)) {
            throw std::runtime_error("NRI SSR scratch allocation failed");
        }
        mImpl->Images[0] = privateOutput.Image;
        mImpl->Views[0] = privateOutput.SampledView;
        mImpl->Images[1] = output.Texture.Image;
        mImpl->Views[1] = output.Texture.SampledView;
        mImpl->OutputStates = output.StateTracker;
        std::array<nri::Descriptor*, 4> inputViews{};
        for (uint32_t i = 0; i < inputs.size(); ++i) {
            if (!mImpl->Interop->WrapTexture(
                    inputs[i], formats[i], VK_IMAGE_TYPE_2D,
                    VK_IMAGE_USAGE_SAMPLED_BIT,
                    output.Width, output.Height))
                throw std::runtime_error("NRI SSR input wrapping failed");
            inputViews[i] = NriInteropAccess::TextureView(
                *mImpl->Interop, inputs[i], false);
            if (inputViews[i] == nullptr)
                throw std::runtime_error("NRI SSR input view failed");
        }
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = 2U * kWaterSlots * kColorVariants;
        pool.samplerMaxNum = 4U * kWaterSlots * kColorVariants;
        pool.textureMaxNum = 10U * kWaterSlots * kColorVariants;
        pool.storageTextureMaxNum = 2U * kWaterSlots * kColorVariants;
        pool.constantBufferMaxNum = 2U * kWaterSlots * kColorVariants;
        pool.structuredBufferMaxNum = 2U * kWaterSlots * kColorVariants;
        nri::Device* device = NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *device, pool, mImpl->DescriptorPool) != nri::Result::SUCCESS)
            throw std::runtime_error("NRI SSR descriptor pool failed");
        (void)inputViews;
        writeColorSets(mImpl->ColorVariants[0], inputs[0], formats[0]);
        mImpl->ColorVariantCount = 1U;
        selectColor(0);
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        mImpl->Inputs = inputs;
        mImpl->InputFormats = formats;
        mImpl->PrivateStates.Clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool HiZReflectionPass::Execute(VkCommandBuffer commandBuffer,
                                uint32_t frameSlot,
                                uint32_t hiZMipCount,
                                const PerspectiveViewState& view,
                                const EffectsSettings& settings,
                                const EffectPassBarrierPlan& barrierPlan,
                                std::optional<PicaSurfaceCoordinates> coordinates,
                                const HiZWaterUniforms& water,
                                const Renderer3ds::PicaWaterMap* waterMap, uint64_t waterMapVersion) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::ReflectionColor);
    if (!barrierPlan.Valid() || mImpl->OutputStates == nullptr ||
        outputBarrier == nullptr ||
        !outputBarrier->Managed()) {
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer; (void)frameSlot; (void)hiZMipCount;
    (void)view; (void)settings;
    (void)barrierPlan; (void)water; (void)waterMap; (void)waterMapVersion;
    return false;
#else
    const uint32_t slot = frameSlot % kWaterSlots;
    if (mImpl->RaySets[slot] == nullptr || mImpl->FilterSets[slot] == nullptr) return false;
    std::memcpy(mImpl->WaterMapped[slot], &water, sizeof(water));
    // A new map version: its texels (no water as -1e30, never NaN).
    if (waterMap != nullptr && waterMap->Size == Renderer3ds::kPicaWaterMapSize &&
        mImpl->MapVersions[slot] != waterMapVersion) {
        auto* texels = static_cast<float*>(mImpl->MapMapped[slot]);
        for (size_t i = 0; i < waterMap->Texels.size(); ++i) {
            texels[4U * i] = std::isnan(waterMap->Texels[i][0]) ? -1.0e30F : waterMap->Texels[i][0];
            for (size_t c = 1U; c < 4U; ++c) texels[4U * i + c] = waterMap->Texels[i][c];
        }
        mImpl->MapVersions[slot] = waterMapVersion;
    }
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr) return false;
    const ReflectionPush push{
        mImpl->Width, mImpl->Height, 1.0F / mImpl->Width, 1.0F / mImpl->Height,
        settings.ReflectionStrength, settings.ReflectionMaxDistance,
        settings.ReflectionThickness, settings.ReflectionEdgeFade,
        settings.ReflectionMaxSteps, hiZMipCount,
        view.Projection[0], view.Projection[5], view.Projection[8],
        view.Projection[9], view.NearPlane, view.FarPlane,
        settings.ReflectionRoughnessBias, 16.0F,
        coordinates ? coordinates->ShaderFlags() : 0U};
    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginOutputExecution(
        EffectResource::ReflectionColor);
    const auto rawWrite = mImpl->PrivateStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[0]),
        {ResourceAccess::ComputeWrite, 0});
    const auto filteredWrite = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[1]),
        {outputBarrier->DispatchAccess, 0});
    const std::array<NriTextureTransitionDesc, 2> toCompute{{
        {mImpl->Images[0], rawWrite},
        {mImpl->Images[1], filteredWrite}}};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot, toCompute.data(),
            static_cast<uint32_t>(toCompute.size())))
        return false;
    mImpl->LastBarrierExecution.RecordPrivateTransition(rawWrite);
    mImpl->LastBarrierExecution.RecordGraphTransition(filteredWrite);
    mImpl->PrivateStates.Commit(rawWrite);
    mImpl->OutputStates->Commit(filteredWrite);
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->DescriptorPool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    const auto dispatch = [&](nri::Pipeline* pipeline,
                              nri::DescriptorSet* descriptorSet) {
        mImpl->Core->CmdSetPipeline(*command, *pipeline);
        const nri::SetDescriptorSetDesc set{0, descriptorSet};
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const nri::SetRootConstantsDesc constants{0, &push, sizeof(push)};
        mImpl->Core->CmdSetRootConstants(*command, constants);
        const nri::DispatchDesc groups{
            (mImpl->Width + 7U) / 8U, (mImpl->Height + 7U) / 8U, 1U};
        mImpl->Core->CmdDispatch(*command, groups);
    };
    dispatch(mImpl->RayPipeline, mImpl->RaySets[slot]);
    const auto rawRead = mImpl->PrivateStates.PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[0]),
        {ResourceAccess::ShaderRead, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Images[0], rawRead))
        return false;
    mImpl->LastBarrierExecution.RecordPrivateTransition(rawRead);
    mImpl->PrivateStates.Commit(rawRead);
    dispatch(mImpl->FilterPipeline, mImpl->FilterSets[slot]);
    const auto filteredRead = mImpl->OutputStates->PlanTransition(
        reinterpret_cast<uintptr_t>(mImpl->Images[1]),
        {outputBarrier->CompletionAccess, 0});
    if (!NriInteropAccess::CmdTextureBarrier(
            *mImpl->Interop, frameSlot, mImpl->Images[1], filteredRead))
        return false;
    mImpl->LastBarrierExecution.RecordGraphTransition(filteredRead);
    mImpl->OutputStates->Commit(filteredRead);
    mImpl->NriBarriersUsed = true;
    return true;
#endif
}

void HiZReflectionPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->DescriptorPool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->DescriptorPool);
    mImpl->DescriptorPool = nullptr;
    mImpl->RaySets = {};
    mImpl->FilterSets = {};
    mImpl->ColorVariants = {};
    mImpl->ColorVariantCount = 0;
#endif
    if (mImpl->Interop != nullptr)
        mImpl->Interop->DestroyOwnedTexture(mImpl->Images[0]);
    mImpl->Images = {};
    mImpl->Views = {};
    mImpl->Inputs = {};
    mImpl->InputFormats = {};
    mImpl->Width = mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->LastBarrierExecution = {};
    mImpl->PrivateStates.Clear();
    mImpl->OutputStates = nullptr;
}

void HiZReflectionPass::Shutdown() {
    InvalidateScreenResources();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->LinearSampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->LinearSampler);
        if (mImpl->NearestSampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->NearestSampler);
        if (mImpl->RayPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->RayPipeline);
        if (mImpl->FilterPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->FilterPipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
        for (uint32_t slot = 0; slot < kWaterSlots; ++slot) {
            if (mImpl->WaterMapped[slot] != nullptr && mImpl->WaterBuffers[slot] != nullptr)
                mImpl->Core->UnmapBuffer(*mImpl->WaterBuffers[slot]);
            if (mImpl->WaterViews[slot] != nullptr) mImpl->Core->DestroyDescriptor(mImpl->WaterViews[slot]);
            if (mImpl->WaterBuffers[slot] != nullptr) mImpl->Core->DestroyBuffer(mImpl->WaterBuffers[slot]);
            if (mImpl->MapMapped[slot] != nullptr && mImpl->MapBuffers[slot] != nullptr)
                mImpl->Core->UnmapBuffer(*mImpl->MapBuffers[slot]);
            if (mImpl->MapViews[slot] != nullptr) mImpl->Core->DestroyDescriptor(mImpl->MapViews[slot]);
            if (mImpl->MapBuffers[slot] != nullptr) mImpl->Core->DestroyBuffer(mImpl->MapBuffers[slot]);
        }
    }
#endif
    *mImpl = {};
    mImpl->Reason = "SSR pass is not initialized";
}

bool HiZReflectionPass::Available() const { return mImpl->Initialized; }
VkImage HiZReflectionPass::OutputImage() const { return mImpl->Images[1]; }
VkImageView HiZReflectionPass::OutputView() const { return mImpl->Views[1]; }
bool HiZReflectionPass::OutputsOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Images[0]) &&
           mImpl->Interop->OwnsTexture(mImpl->Images[1]);
}
bool HiZReflectionPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Initialized && mImpl->RayPipeline != nullptr &&
           mImpl->FilterPipeline != nullptr && mImpl->DescriptorPool != nullptr;
#else
    return false;
#endif
}
bool HiZReflectionPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}
const EffectPassBarrierExecution& HiZReflectionPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
const std::string& HiZReflectionPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
