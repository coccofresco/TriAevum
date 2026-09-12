#include "fast/oot3d/nri_pica_display_copy_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"

#include <NRI.h>
#endif

#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Fast::Oot3d {
namespace {

constexpr uint32_t kFrameSlots = 2U;
constexpr uint32_t kMaximumCopiesPerFrame = 16U;
constexpr uint32_t kDescriptorSetCount =
    kFrameSlots * kMaximumCopiesPerFrame;

struct DisplayTransferPush {
    uint32_t SourceWidth = 0;
    uint32_t SourceHeight = 0;
    uint32_t DestinationWidth = 0;
    uint32_t DestinationHeight = 0;
    uint32_t HorizontalSamples = 1;
    uint32_t VerticalSamples = 1;
};
static_assert(sizeof(DisplayTransferPush) == 24U);

#ifdef ENABLE_OOT3D_NRI
std::vector<uint32_t> CompileDisplayTransferShader(Renderer::CachedPassShaderCompiler& shaders) {
    return shaders.Resolve(BuildPicaDisplayTransferComputeShader(), Renderer::SpirvStage::Compute, "oot3d_pica_display_transfer.comp");
}
#endif

} // namespace

struct NriPicaDisplayCopyPass::Impl {
    NriInteropContext* Interop = nullptr;
    ResourceStateTracker States;
    uint32_t LastBarrierCount = 0;
    std::string Reason =
        "NRI PICA display transfer is not initialized";
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* PipelineLayout = nullptr;
    nri::Pipeline* Pipeline = nullptr;
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kDescriptorSetCount> Sets{};
    std::array<uint64_t, kFrameSlots> FrameIds{};
    std::array<uint32_t, kFrameSlots> SetsUsed{};
#endif
};

NriPicaDisplayCopyPass::NriPicaDisplayCopyPass()
    : mImpl(std::make_unique<Impl>()) {}

NriPicaDisplayCopyPass::~NriPicaDisplayCopyPass() {
    Shutdown();
}

bool NriPicaDisplayCopyPass::Initialize(NriInteropContext& interop) {
    Shutdown();
    if (const char* enabled =
            std::getenv("OOT3D_GRAPHICS_NRI_PICA_DISPLAY_COPIES");
        enabled != nullptr && std::string_view(enabled) == "0") {
        mImpl->Reason =
            "NRI PICA display transfers disabled by environment";
        return false;
    }
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
#ifndef ENABLE_OOT3D_NRI
    mImpl->Reason = "NRI PICA display transfers were not compiled";
    return false;
#else
    try {
        nri::Device* device = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (device == nullptr || mImpl->Core == nullptr) {
            throw std::runtime_error(
                "NRI PICA display-transfer device is unavailable");
        }

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
        root.size = sizeof(DisplayTransferPush);
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
                *device, layout, mImpl->PipelineLayout) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI PICA display-transfer layout creation failed");
        }

        const auto shader = CompileDisplayTransferShader(interop.Shaders());
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->PipelineLayout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = shader.data();
        pipeline.shader.size = shader.size() * sizeof(uint32_t);
        if (mImpl->Core->CreateComputePipeline(
                *device, pipeline, mImpl->Pipeline) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI PICA display-transfer pipeline creation failed");
        }

        nri::SamplerDesc sampler{};
        sampler.filters = {nri::Filter::NEAREST, nri::Filter::NEAREST,
                           nri::Filter::NEAREST};
        sampler.addressModes = {nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE,
                                nri::AddressMode::CLAMP_TO_EDGE};
        if (mImpl->Core->CreateSampler(
                *device, sampler, mImpl->Sampler) !=
            nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI PICA display-transfer sampler creation failed");
        }

        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = kDescriptorSetCount;
        pool.samplerMaxNum = kDescriptorSetCount;
        pool.textureMaxNum = kDescriptorSetCount;
        pool.storageTextureMaxNum = kDescriptorSetCount;
        if (mImpl->Core->CreateDescriptorPool(
                *device, pool, mImpl->Pool) != nri::Result::SUCCESS ||
            mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->PipelineLayout, 0,
                mImpl->Sets.data(), kDescriptorSetCount, 0) !=
                nri::Result::SUCCESS) {
            throw std::runtime_error(
                "NRI PICA display-transfer descriptors failed");
        }
        for (uint32_t setIndex = 0; setIndex < kDescriptorSetCount;
             ++setIndex) {
            const nri::UpdateDescriptorRangeDesc update{
                mImpl->Sets[setIndex], 2, 0, &mImpl->Sampler, 1};
            mImpl->Core->UpdateDescriptorRanges(&update, 1);
        }
        mImpl->FrameIds.fill(std::numeric_limits<uint64_t>::max());

        mImpl->Interop = &interop;
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

bool NriPicaDisplayCopyPass::Execute(
    const NriPicaDisplayCopyDesc& desc) {
    mImpl->LastBarrierCount = 0;
#ifndef ENABLE_OOT3D_NRI
    (void)desc;
    return false;
#else
    if (!Available() || !ValidateNriPicaDisplayCopyDesc(desc))
        return false;
    nri::CommandBuffer* command = NriInteropAccess::CommandBuffer(
        *mImpl->Interop, desc.FrameIndex);
    if (command == nullptr ||
        !mImpl->Interop->WrapTexture(
            desc.SourceImage, desc.Format, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            desc.Plan.SourceWidth, desc.Plan.SourceHeight) ||
        !mImpl->Interop->WrapTexture(
            desc.DestinationImage, desc.Format, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT,
            desc.Plan.DestinationWidth,
            desc.Plan.DestinationHeight)) {
        return false;
    }

    nri::Descriptor* sourceView = NriInteropAccess::TextureView(
        *mImpl->Interop, desc.SourceImage, false);
    nri::Descriptor* destinationView = NriInteropAccess::TextureView(
        *mImpl->Interop, desc.DestinationImage, true);
    if (sourceView == nullptr || destinationView == nullptr)
        return false;

    const uintptr_t sourceKey =
        reinterpret_cast<uintptr_t>(desc.SourceImage);
    const uintptr_t destinationKey =
        reinterpret_cast<uintptr_t>(desc.DestinationImage);
    const ResourceAccess sourceInitial = desc.SourceShaderRead
        ? ResourceAccess::ShaderRead
        : ResourceAccess::ColorAttachment;
    const auto seed = [this](uintptr_t key, ResourceAccess access) {
        mImpl->States.Forget(key);
        ResourceTransition state;
        state.Resource = key;
        state.After = {access, 0};
        mImpl->States.Commit(state);
    };
    seed(sourceKey, sourceInitial);
    seed(destinationKey,
         desc.DestinationInitialized
             ? ResourceAccess::ShaderRead
             : ResourceAccess::Undefined);

    const std::array<ResourceTransition, 2> toCompute{{
        mImpl->States.PlanTransition(
            sourceKey, {ResourceAccess::ShaderRead, 0}),
        mImpl->States.PlanTransition(
            destinationKey, {ResourceAccess::ComputeWrite, 0}),
    }};
    const std::array<NriTextureTransitionDesc, 2> toComputeDescs{{
        {desc.SourceImage, toCompute[0], 0, 1},
        {desc.DestinationImage, toCompute[1], 0, 1},
    }};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, desc.FrameIndex, toComputeDescs.data(),
            static_cast<uint32_t>(toComputeDescs.size()))) {
        return false;
    }
    for (const auto& transition : toCompute)
        mImpl->States.Commit(transition);

    const uint32_t slot = desc.FrameIndex % kFrameSlots;
    if (mImpl->FrameIds[slot] != desc.FrameId) {
        mImpl->FrameIds[slot] = desc.FrameId;
        mImpl->SetsUsed[slot] = 0U;
    }
    if (mImpl->SetsUsed[slot] >= kMaximumCopiesPerFrame) {
        return false;
    }
    nri::DescriptorSet* descriptorSet =
        mImpl->Sets[slot * kMaximumCopiesPerFrame +
                    mImpl->SetsUsed[slot]++];
    const std::array<nri::UpdateDescriptorRangeDesc, 2> updates{{
        {descriptorSet, 0, 0, &sourceView, 1},
        {descriptorSet, 1, 0, &destinationView, 1},
    }};
    mImpl->Core->UpdateDescriptorRanges(
        updates.data(), static_cast<uint32_t>(updates.size()));
    const DisplayTransferPush push{
        desc.Plan.SourceWidth,
        desc.Plan.SourceHeight,
        desc.Plan.DestinationWidth,
        desc.Plan.DestinationHeight,
        desc.Plan.HorizontalSamples,
        desc.Plan.VerticalSamples,
    };
    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    mImpl->Core->CmdSetPipelineLayout(
        *command, nri::BindPoint::COMPUTE, *mImpl->PipelineLayout);
    mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
    const nri::SetDescriptorSetDesc set{0, descriptorSet};
    mImpl->Core->CmdSetDescriptorSet(*command, set);
    const nri::SetRootConstantsDesc constants{0, &push, sizeof(push)};
    mImpl->Core->CmdSetRootConstants(*command, constants);
    const nri::DispatchDesc dispatch{
        (desc.Plan.DestinationWidth + 7U) / 8U,
        (desc.Plan.DestinationHeight + 7U) / 8U,
        1U,
    };
    mImpl->Core->CmdDispatch(*command, dispatch);

    const std::array<ResourceTransition, 2> fromCompute{{
        mImpl->States.PlanTransition(
            sourceKey, {sourceInitial, 0}),
        mImpl->States.PlanTransition(
            destinationKey, {ResourceAccess::ShaderRead, 0}),
    }};
    const std::array<NriTextureTransitionDesc, 2> fromComputeDescs{{
        {desc.SourceImage, fromCompute[0], 0, 1},
        {desc.DestinationImage, fromCompute[1], 0, 1},
    }};
    if (!NriInteropAccess::CmdTextureBarriers(
            *mImpl->Interop, desc.FrameIndex,
            fromComputeDescs.data(),
            static_cast<uint32_t>(fromComputeDescs.size()))) {
        return false;
    }
    for (const auto& transition : fromCompute)
        mImpl->States.Commit(transition);
    mImpl->LastBarrierCount = 4U;
    return true;
#endif
}

void NriPicaDisplayCopyPass::ForgetTexture(VkImage image) {
    if (image != VK_NULL_HANDLE)
        mImpl->States.Forget(reinterpret_cast<uintptr_t>(image));
}

void NriPicaDisplayCopyPass::Shutdown() {
    mImpl->States.Clear();
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Pool != nullptr)
            mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
        if (mImpl->Sampler != nullptr)
            mImpl->Core->DestroyDescriptor(mImpl->Sampler);
        if (mImpl->Pipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->PipelineLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->PipelineLayout);
    }
#endif
    *mImpl = {};
    mImpl->Reason =
        "NRI PICA display transfer is not initialized";
}

bool NriPicaDisplayCopyPass::Available() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Reason.empty() && mImpl->Interop != nullptr &&
           mImpl->Core != nullptr && mImpl->PipelineLayout != nullptr &&
           mImpl->Pipeline != nullptr && mImpl->Sampler != nullptr &&
           mImpl->Pool != nullptr && mImpl->Interop->Available();
#else
    return false;
#endif
}

uint32_t NriPicaDisplayCopyPass::LastBarrierCount() const {
    return mImpl->LastBarrierCount;
}

const std::string& NriPicaDisplayCopyPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
