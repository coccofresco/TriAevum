#include "fast/oot3d/smaa_1x_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_pica_texture_upload_pass.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/smaa_1x.h"
#include "fast/oot3d/smaa_lookup_data.h"

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
constexpr uint32_t kStageCount = 3U;
constexpr uint64_t kLookupUploadCapacity = 512U * 1024U;

struct SmaaPush {
    uint32_t Width = 0;
    uint32_t Height = 0;
    float InverseWidth = 0.0F;
    float InverseHeight = 0.0F;
};
static_assert(sizeof(SmaaPush) == 16U);

std::vector<uint32_t> Compile(Smaa1xStage stage, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        BuildSmaa1xComputeShader(stage), shaderc_compute_shader,
        name, options);
    if (result.GetCompilationStatus() !=
        shaderc_compilation_status_success) {
        throw std::runtime_error(
            std::string(name) + ": " + result.GetErrorMessage());
    }
    return { result.cbegin(), result.cend() };
}

} // namespace

struct Smaa1xPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    std::unique_ptr<NriPicaTextureUploadPass> LookupUpload =
        std::make_unique<NriPicaTextureUploadPass>();
    SmaaLookupData LookupData;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    std::array<nri::Pipeline*, kStageCount> Pipelines{};
    nri::Descriptor* LinearSampler = nullptr;
    nri::Descriptor* PointSampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kFrameSlots * kStageCount> Sets{};
#endif
    std::array<VkImage, 2> LookupImages{};
    std::array<VkImageView, 2> LookupViews{};
    std::array<VkImage, kStageCount> Images{};
    std::array<VkImageView, kStageCount> Views{};
    uint32_t Width = 0;
    uint32_t Height = 0;
    bool Ready = false;
    bool LookupsUploaded = false;
    bool NriBarriersUsed = false;
    bool NriLookupUploadUsed = false;
    SceneColorEncoding OutputEncoding = SceneColorEncoding::Unknown;
    EffectPassBarrierExecution LastBarrierExecution;
    ResourceStateTracker PrivateStates;
    ResourceStateTracker* OutputStates = nullptr;
    std::string Reason = "SMAA 1x pass is not initialized";
};

Smaa1xPass::Smaa1xPass() : mImpl(std::make_unique<Impl>()) {}
Smaa1xPass::~Smaa1xPass() { Shutdown(); }

bool Smaa1xPass::Initialize(VkPhysicalDevice physicalDevice,
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
    mImpl->Reason = "NRI SMAA 1x support was not compiled";
    return false;
#else
    try {
        const auto requireStorage = [&](VkFormat format,
                                        const char* name) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(
                physicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U) {
                throw std::runtime_error(
                    std::string(name) +
                    " SMAA storage image is unavailable");
            }
        };
        requireStorage(VK_FORMAT_R8G8B8A8_UNORM, "RGBA8");
        requireStorage(VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16F");

        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error("NRI SMAA device is unavailable");

        const std::array<nri::DescriptorRangeDesc, 6> ranges{{
            {0, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {1, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {2, 1, nri::DescriptorType::TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {3, 1, nri::DescriptorType::STORAGE_TEXTURE,
             nri::StageBits::COMPUTE_SHADER},
            {4, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER},
            {5, 1, nri::DescriptorType::SAMPLER,
             nri::StageBits::COMPUTE_SHADER},
        }};
        nri::DescriptorSetDesc setDesc{};
        setDesc.ranges = ranges.data();
        setDesc.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(SmaaPush);
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
            throw std::runtime_error("NRI SMAA pipeline layout failed");
        }

        const std::array<Smaa1xStage, kStageCount> stages{
            Smaa1xStage::EdgeDetection,
            Smaa1xStage::BlendWeightCalculation,
            Smaa1xStage::NeighborhoodBlending,
        };
        const std::array<const char*, kStageCount> names{
            "oot3d_smaa_edges.comp",
            "oot3d_smaa_blend_weights.comp",
            "oot3d_smaa_neighborhood.comp",
        };
        for (uint32_t i = 0; i < kStageCount; ++i) {
            const auto shader = Compile(stages[i], names[i]);
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = mImpl->PipelineLayout;
            pipeline.shader.stage =
                nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = shader.data();
            pipeline.shader.size =
                shader.size() * sizeof(uint32_t);
            if (mImpl->Core->CreateComputePipeline(
                    *nriDevice, pipeline, mImpl->Pipelines[i]) !=
                nri::Result::SUCCESS) {
                throw std::runtime_error(
                    std::string("NRI ") + names[i] +
                    " pipeline failed");
            }
        }

        const auto createSampler = [&](
                                       nri::Filter filter,
                                       nri::Descriptor*& output) {
            nri::SamplerDesc sampler{};
            sampler.filters = {
                filter, filter, nri::Filter::NEAREST};
            sampler.addressModes = {
                nri::AddressMode::CLAMP_TO_EDGE,
                nri::AddressMode::CLAMP_TO_EDGE,
                nri::AddressMode::CLAMP_TO_EDGE};
            if (mImpl->Core->CreateSampler(
                    *nriDevice, sampler, output) !=
                nri::Result::SUCCESS) {
                throw std::runtime_error(
                    "NRI SMAA sampler creation failed");
            }
        };
        createSampler(nri::Filter::LINEAR, mImpl->LinearSampler);
        createSampler(nri::Filter::NEAREST, mImpl->PointSampler);

        mImpl->LookupData = DecodeSmaaLookupData();
        if (!mImpl->LookupData.Valid()) {
            throw std::runtime_error(
                "official SMAA lookup payload is invalid");
        }
        const std::array<uint32_t, 2> lookupWidths{
            SmaaLookupData::AreaWidth,
            SmaaLookupData::SearchWidth};
        const std::array<uint32_t, 2> lookupHeights{
            SmaaLookupData::AreaHeight,
            SmaaLookupData::SearchHeight};
        for (uint32_t i = 0; i < 2U; ++i) {
            NriOwnedTexture2DDesc desc{};
            desc.Width = lookupWidths[i];
            desc.Height = lookupHeights[i];
            desc.Format = VK_FORMAT_R8G8B8A8_UNORM;
            desc.Usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            NriOwnedTexture2D lookup;
            if (!interop.CreateOwnedTexture2D(desc, lookup)) {
                throw std::runtime_error(
                    "NRI SMAA lookup texture allocation failed");
            }
            mImpl->LookupImages[i] = lookup.Image;
            mImpl->LookupViews[i] = lookup.SampledView;
        }
        if (!mImpl->LookupUpload->Initialize(
                interop, kLookupUploadCapacity)) {
            throw std::runtime_error(
                mImpl->LookupUpload->UnavailableReason());
        }
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

bool Smaa1xPass::Configure(
    const NriEffectGraphTransientImageBinding& output) {
    if (!mImpl->Ready || !output.Valid() ||
        output.Resource != EffectResource::AntiAliasedColor ||
        output.Format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        output.Texture.SampledView == VK_NULL_HANDLE ||
        output.Texture.StorageView == VK_NULL_HANDLE ||
        (output.Usage & (VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_STORAGE_BIT)) !=
            (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) {
        return false;
    }
    if (mImpl->Images[2] == output.Texture.Image &&
        mImpl->Width == output.Width && mImpl->Height == output.Height &&
        mImpl->OutputStates == output.StateTracker) {
        return true;
    }
#ifndef ENABLE_OOT3D_NRI
    return false;
#else
    if (mImpl->Width != 0U &&
        vkDeviceWaitIdle(mImpl->Device) != VK_SUCCESS) {
        return false;
    }
    InvalidateScreenResources();
    try {
        const std::array<VkFormat, 2U> formats{
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
        };
        for (uint32_t i = 0; i < formats.size(); ++i) {
            NriOwnedTexture2D privateOutput;
            if (!mImpl->Interop->CreateOwnedTexture2D(
                    output.Width, output.Height, formats[i],
                    privateOutput)) {
                throw std::runtime_error(
                    "NRI SMAA screen texture allocation failed");
            }
            mImpl->Images[i] = privateOutput.Image;
            mImpl->Views[i] = privateOutput.SampledView;
        }
        mImpl->Images[2] = output.Texture.Image;
        mImpl->Views[2] = output.Texture.SampledView;
        mImpl->OutputStates = output.StateTracker;

        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum =
            kFrameSlots * kStageCount;
        pool.textureMaxNum =
            kFrameSlots * kStageCount * 3U;
        pool.storageTextureMaxNum =
            kFrameSlots * kStageCount;
        pool.samplerMaxNum =
            kFrameSlots * kStageCount * 2U;
        nri::Device* nriDevice =
            NriInteropAccess::Device(*mImpl->Interop);
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, pool, mImpl->Pool) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI SMAA descriptor pool failed");
        }
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->PipelineLayout, 0,
                mImpl->Sets.data(),
                static_cast<uint32_t>(mImpl->Sets.size()), 0) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI SMAA descriptor sets failed");
        }
        mImpl->Width = output.Width;
        mImpl->Height = output.Height;
        mImpl->PrivateStates.Clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        InvalidateScreenResources();
        return false;
    }
#endif
}

bool Smaa1xPass::Execute(
    VkCommandBuffer commandBuffer, uint32_t frameSlot,
    uint64_t frameId, VkImage colorImage,
    VkFormat colorFormat,
    SceneColorEncoding inputEncoding,
    const EffectPassBarrierPlan& barrierPlan) {
    const auto* outputBarrier = barrierPlan.Find(
        EffectResource::AntiAliasedColor);
    if (mImpl->Width == 0U || mImpl->OutputStates == nullptr ||
        colorImage == VK_NULL_HANDLE ||
        !IsKnownSceneColorEncoding(inputEncoding) ||
        !barrierPlan.Valid() || outputBarrier == nullptr ||
        !outputBarrier->Managed())
        return false;
#ifndef ENABLE_OOT3D_NRI
    (void)commandBuffer;
    (void)frameSlot;
    (void)frameId;
    (void)colorFormat;
    (void)inputEncoding;
    (void)barrierPlan;
    return false;
#else
    (void)commandBuffer;
    mImpl->NriBarriersUsed = false;
    mImpl->NriLookupUploadUsed = false;
    mImpl->LastBarrierExecution = barrierPlan.BeginExecution();
    if (!mImpl->LookupsUploaded) {
        const std::array<std::span<const uint8_t>, 2> pixels{
            std::span<const uint8_t>(
                mImpl->LookupData.AreaRgba8),
            std::span<const uint8_t>(
                mImpl->LookupData.SearchRgba8),
        };
        const std::array<uint32_t, 2> widths{
            SmaaLookupData::AreaWidth,
            SmaaLookupData::SearchWidth};
        const std::array<uint32_t, 2> heights{
            SmaaLookupData::AreaHeight,
            SmaaLookupData::SearchHeight};
        for (uint32_t i = 0; i < 2U; ++i) {
            NriPicaTextureUploadDesc upload{};
            upload.FrameIndex = frameSlot;
            upload.FrameId = frameId;
            upload.Image = mImpl->LookupImages[i];
            upload.Format = VK_FORMAT_R8G8B8A8_UNORM;
            upload.Width = widths[i];
            upload.Height = heights[i];
            upload.Pixels = pixels[i];
            if (!mImpl->LookupUpload->Execute(upload))
                return false;
        }
        mImpl->LookupsUploaded = true;
        mImpl->NriLookupUploadUsed = true;
        mImpl->LookupData = {};
    }

    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(
            *mImpl->Interop, frameSlot);
    if (command == nullptr ||
        !mImpl->Interop->WrapTexture(
            colorImage, colorFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            mImpl->Width, mImpl->Height)) {
        return false;
    }
    nri::Descriptor* colorView =
        NriInteropAccess::TextureView(
            *mImpl->Interop, colorImage, false);
    if (colorView == nullptr) return false;

    const uint32_t slot = frameSlot % kFrameSlots;
    const uint32_t setBase = slot * kStageCount;
    const auto updateSet = [&](
                               uint32_t stage,
                               const std::array<nri::Descriptor*, 3>&
                                   sampled,
                               VkImage destination) {
        nri::Descriptor* storage =
            NriInteropAccess::TextureView(
                *mImpl->Interop, destination, true);
        if (storage == nullptr) return false;
        std::array<nri::UpdateDescriptorRangeDesc, 6> updates{};
        for (uint32_t i = 0; i < sampled.size(); ++i) {
            updates[i] = {
                mImpl->Sets[setBase + stage],
                i, 0, &sampled[i], 1};
        }
        updates[3] = {
            mImpl->Sets[setBase + stage],
            3, 0, &storage, 1};
        updates[4] = {
            mImpl->Sets[setBase + stage],
            4, 0, &mImpl->LinearSampler, 1};
        updates[5] = {
            mImpl->Sets[setBase + stage],
            5, 0, &mImpl->PointSampler, 1};
        mImpl->Core->UpdateDescriptorRanges(
            updates.data(),
            static_cast<uint32_t>(updates.size()));
        return true;
    };
    nri::Descriptor* edgeView =
        NriInteropAccess::TextureView(
            *mImpl->Interop, mImpl->Images[0], false);
    nri::Descriptor* weightView =
        NriInteropAccess::TextureView(
            *mImpl->Interop, mImpl->Images[1], false);
    nri::Descriptor* areaView =
        NriInteropAccess::TextureView(
            *mImpl->Interop, mImpl->LookupImages[0], false);
    nri::Descriptor* searchView =
        NriInteropAccess::TextureView(
            *mImpl->Interop, mImpl->LookupImages[1], false);
    if (edgeView == nullptr || weightView == nullptr ||
        areaView == nullptr || searchView == nullptr)
        return false;
    if (!updateSet(
            0, {colorView, colorView, colorView},
            mImpl->Images[0]) ||
        !updateSet(
            1, {edgeView, areaView, searchView},
            mImpl->Images[1]) ||
        !updateSet(
            2, {colorView, weightView, colorView},
            mImpl->Images[2])) {
        return false;
    }

    std::array<NriTextureTransitionDesc, kStageCount>
        writeTransitions{};
    for (uint32_t i = 0; i < kStageCount; ++i) {
        const ResourceAccess dispatchAccess = i + 1U == kStageCount
            ? outputBarrier->DispatchAccess
            : ResourceAccess::ComputeWrite;
        ResourceStateTracker& states = i + 1U == kStageCount
            ? *mImpl->OutputStates
            : mImpl->PrivateStates;
        writeTransitions[i] = {
            mImpl->Images[i],
            states.PlanTransition(
                reinterpret_cast<uintptr_t>(
                    mImpl->Images[i]),
                {dispatchAccess, 0})};
    }
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, frameSlot,
            writeTransitions.data(),
            static_cast<uint32_t>(
                writeTransitions.size()))) {
        return false;
    }
    for (uint32_t stage = 0; stage < kStageCount; ++stage) {
        const auto& transition = writeTransitions[stage];
        if (stage + 1U == kStageCount) {
            mImpl->LastBarrierExecution.RecordGraphTransition(
                transition.Transition);
        } else {
            mImpl->LastBarrierExecution.RecordPrivateTransition(
                transition.Transition);
        }
        ResourceStateTracker& states = stage + 1U == kStageCount
            ? *mImpl->OutputStates
            : mImpl->PrivateStates;
        states.Commit(transition.Transition);
    }

    const SmaaPush push{
        mImpl->Width, mImpl->Height,
        1.0F / static_cast<float>(mImpl->Width),
        1.0F / static_cast<float>(mImpl->Height)};
    mImpl->Core->CmdSetDescriptorPool(
        *command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE,
        *mImpl->PipelineLayout);
    const nri::DispatchDesc dispatch{
        (mImpl->Width + 7U) / 8U,
        (mImpl->Height + 7U) / 8U, 1U};
    for (uint32_t stage = 0; stage < kStageCount; ++stage) {
        mImpl->Core->CmdSetPipeline(
            *command, *mImpl->Pipelines[stage]);
        const nri::SetDescriptorSetDesc set{
            0, mImpl->Sets[setBase + stage]};
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const nri::SetRootConstantsDesc constants{
            0, &push, sizeof(push)};
        mImpl->Core->CmdSetRootConstants(
            *command, constants);
        mImpl->Core->CmdDispatch(*command, dispatch);
        ResourceStateTracker& states = stage + 1U == kStageCount
            ? *mImpl->OutputStates
            : mImpl->PrivateStates;
        const auto readable =
            states.PlanTransition(
                reinterpret_cast<uintptr_t>(
                    mImpl->Images[stage]),
                {stage + 1U == kStageCount
                     ? outputBarrier->CompletionAccess
                     : ResourceAccess::ShaderRead,
                 0});
        if (!NriInteropAccess::CmdTextureBarrier(
                *mImpl->Interop, frameSlot,
                mImpl->Images[stage], readable)) {
            return false;
        }
        if (stage + 1U == kStageCount) {
            mImpl->LastBarrierExecution.RecordGraphTransition(readable);
        } else {
            mImpl->LastBarrierExecution.RecordPrivateTransition(readable);
        }
        states.Commit(readable);
    }
    mImpl->NriBarriersUsed = true;
    mImpl->OutputEncoding = inputEncoding;
    return true;
#endif
}

void Smaa1xPass::InvalidateScreenResources() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr && mImpl->Pool != nullptr)
        mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
    mImpl->Pool = nullptr;
    mImpl->Sets = {};
#endif
    if (mImpl->Interop != nullptr) {
        for (uint32_t index = 0U; index < 2U; ++index)
            mImpl->Interop->DestroyOwnedTexture(mImpl->Images[index]);
    }
    mImpl->Images = {};
    mImpl->Views = {};
    mImpl->Width = mImpl->Height = 0;
    mImpl->NriBarriersUsed = false;
    mImpl->OutputEncoding = SceneColorEncoding::Unknown;
    mImpl->LastBarrierExecution = {};
    mImpl->PrivateStates.Clear();
    mImpl->OutputStates = nullptr;
}

void Smaa1xPass::Shutdown() {
    InvalidateScreenResources();
    if (mImpl->LookupUpload != nullptr)
        mImpl->LookupUpload->Shutdown();
    if (mImpl->Interop != nullptr) {
        for (VkImage image : mImpl->LookupImages)
            mImpl->Interop->DestroyOwnedTexture(image);
    }
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->LinearSampler != nullptr)
            mImpl->Core->DestroyDescriptor(
                mImpl->LinearSampler);
        if (mImpl->PointSampler != nullptr)
            mImpl->Core->DestroyDescriptor(
                mImpl->PointSampler);
        for (nri::Pipeline* pipeline : mImpl->Pipelines) {
            if (pipeline != nullptr)
                mImpl->Core->DestroyPipeline(pipeline);
        }
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(
                mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason = "SMAA 1x pass is not initialized";
}

bool Smaa1xPass::Available() const {
    return mImpl->Ready;
}

VkImage Smaa1xPass::OutputImage() const {
    return mImpl->Images[2];
}

VkImageView Smaa1xPass::OutputView() const {
    return mImpl->Views[2];
}

bool Smaa1xPass::OutputsOwnedByNri() const {
    if (mImpl->Interop == nullptr) return false;
    for (VkImage image : mImpl->Images) {
        if (!mImpl->Interop->OwnsTexture(image))
            return false;
    }
    return mImpl->Images[0] != VK_NULL_HANDLE;
}

bool Smaa1xPass::LookupsOwnedByNri() const {
    return mImpl->Interop != nullptr &&
        mImpl->LookupImages[0] != VK_NULL_HANDLE &&
        mImpl->Interop->OwnsTexture(mImpl->LookupImages[0]) &&
        mImpl->Interop->OwnsTexture(mImpl->LookupImages[1]);
}

bool Smaa1xPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    if (!mImpl->Ready || mImpl->Pool == nullptr ||
        mImpl->PipelineLayout == nullptr ||
        mImpl->LinearSampler == nullptr ||
        mImpl->PointSampler == nullptr) {
        return false;
    }
    for (nri::Pipeline* pipeline : mImpl->Pipelines) {
        if (pipeline == nullptr) return false;
    }
    return true;
#else
    return false;
#endif
}

bool Smaa1xPass::BarriersOwnedByNri() const {
    return mImpl->NriBarriersUsed;
}

SceneColorEncoding Smaa1xPass::OutputEncoding() const {
    return mImpl->OutputEncoding;
}

bool Smaa1xPass::LookupUploadOwnedByNri() const {
    return mImpl->LookupsUploaded &&
        (mImpl->NriLookupUploadUsed ||
         (mImpl->LookupUpload != nullptr &&
          mImpl->LookupUpload->Available()));
}

const EffectPassBarrierExecution& Smaa1xPass::LastBarrierExecution() const {
    return mImpl->LastBarrierExecution;
}

const std::string& Smaa1xPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
