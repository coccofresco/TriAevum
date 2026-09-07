#include "fast/oot3d/reflection_ibl_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"

#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif

#include <shaderc/shaderc.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Fast::Oot3d {
namespace {

struct EnvironmentPush {
    std::array<float, 4> Sky{};
    std::array<float, 4> Horizon{};
    std::array<float, 4> Ground{};
    uint32_t Extent = 0;
    uint32_t MipLevel = 0;
    uint32_t MipCount = 0;
    uint32_t SampleCount = 0;
};
static_assert(sizeof(EnvironmentPush) == 64U);

struct BrdfPush {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t SampleCount = 0;
    uint32_t Reserved = 0;
};
static_assert(sizeof(BrdfPush) == 16U);

std::vector<uint32_t> Compile(std::string source, const char* name) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    const auto result = compiler.CompileGlslToSpv(
        source, shaderc_compute_shader, name, options);
    if (result.GetCompilationStatus() !=
        shaderc_compilation_status_success)
        throw std::runtime_error(result.GetErrorMessage());
    return {result.cbegin(), result.cend()};
}

} // namespace

struct ReflectionIblPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
#ifdef ENABLE_OOT3D_NRI
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* EnvironmentLayout = nullptr;
    nri::PipelineLayout* BrdfLayout = nullptr;
    nri::Pipeline* EnvironmentPipeline = nullptr;
    nri::Pipeline* BrdfPipeline = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, EnvironmentMipCount> EnvironmentSets{};
    nri::DescriptorSet* BrdfSet = nullptr;
    std::array<nri::Descriptor*, EnvironmentMipCount> EnvironmentViews{};
#endif
    NriOwnedTexture2D Environment{};
    NriOwnedTexture2D Brdf{};
    ResourceStateTracker States;
    uint64_t ProfileSignature = 0;
    bool Ready = false;
    bool EnvironmentGenerated = false;
    bool BrdfGenerated = false;
    bool BarriersUsed = false;
    EffectPassBarrierExecution LastBarrierExecution;
    bool ProfileUpdated = false;
    bool PicaDerived = false;
    std::string Reason = "reflection IBL pass is not initialized";
};

ReflectionIblPass::ReflectionIblPass()
    : mImpl(std::make_unique<Impl>()) {}
ReflectionIblPass::~ReflectionIblPass() { Shutdown(); }

bool ReflectionIblPass::Initialize(
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
    mImpl->Reason = "NRI reflection IBL support was not compiled";
    return false;
#else
    try {
        for (VkFormat format : {
                 VK_FORMAT_R16G16B16A16_SFLOAT,
                 VK_FORMAT_R16G16_SFLOAT}) {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(
                physicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures &
                 VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U) {
                throw std::runtime_error(
                    "reflection IBL storage formats are unavailable");
            }
        }
        nri::Device* nriDevice = NriInteropAccess::Device(interop);
        mImpl->Core = NriInteropAccess::Core(interop);
        if (nriDevice == nullptr || mImpl->Core == nullptr)
            throw std::runtime_error(
                "reflection IBL NRI device is unavailable");

        const auto createLayout = [&](
                                      uint32_t rootSize,
                                      nri::PipelineLayout*& output) {
            const nri::DescriptorRangeDesc range{
                0, 1, nri::DescriptorType::STORAGE_TEXTURE,
                nri::StageBits::COMPUTE_SHADER};
            nri::DescriptorSetDesc set{};
            set.ranges = &range;
            set.rangeNum = 1;
            nri::RootConstantDesc root{};
            root.size = rootSize;
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
                    *nriDevice, layout, output) != nri::Result::SUCCESS)
                throw std::runtime_error(
                    "reflection IBL pipeline layout creation failed");
        };
        createLayout(sizeof(EnvironmentPush), mImpl->EnvironmentLayout);
        createLayout(sizeof(BrdfPush), mImpl->BrdfLayout);

        const auto createPipeline = [&](
                                        nri::PipelineLayout* layout,
                                        std::string source,
                                        const char* name,
                                        nri::Pipeline*& output) {
            const auto shader = Compile(std::move(source), name);
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = layout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = shader.data();
            pipeline.shader.size = shader.size() * sizeof(uint32_t);
            if (mImpl->Core->CreateComputePipeline(
                    *nriDevice, pipeline, output) !=
                nri::Result::SUCCESS)
                throw std::runtime_error(
                    "reflection IBL compute pipeline creation failed");
        };
        createPipeline(
            mImpl->EnvironmentLayout,
            BuildReflectionEnvironmentComputeShader(),
            "oot3d_reflection_environment.comp",
            mImpl->EnvironmentPipeline);
        createPipeline(
            mImpl->BrdfLayout, BuildReflectionBrdfComputeShader(),
            "oot3d_reflection_brdf.comp", mImpl->BrdfPipeline);

        NriOwnedTexture2DDesc environment{};
        environment.Width = EnvironmentSize;
        environment.Height = EnvironmentSize;
        environment.Format = VK_FORMAT_R16G16B16A16_SFLOAT;
        environment.Usage =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        environment.MipLevels = EnvironmentMipCount;
        environment.Layers = 6U;
        environment.CubeSampledView = true;
        if (!interop.CreateOwnedTexture2D(
                environment, mImpl->Environment))
            throw std::runtime_error(
                "reflection environment cube allocation failed");

        NriOwnedTexture2DDesc brdf{};
        brdf.Width = BrdfSize;
        brdf.Height = BrdfSize;
        brdf.Format = VK_FORMAT_R16G16_SFLOAT;
        brdf.Usage =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        if (!interop.CreateOwnedTexture2D(brdf, mImpl->Brdf))
            throw std::runtime_error(
                "reflection BRDF LUT allocation failed");

        nri::Texture* environmentTexture =
            NriInteropAccess::Texture(interop, mImpl->Environment.Image);
        mImpl->EnvironmentViews[0] =
            NriInteropAccess::TextureView(
                interop, mImpl->Environment.Image, true);
        if (environmentTexture == nullptr ||
            mImpl->EnvironmentViews[0] == nullptr)
            throw std::runtime_error(
                "reflection environment views are unavailable");
        for (uint32_t mip = 1; mip < EnvironmentMipCount; ++mip) {
            nri::TextureViewDesc view{};
            view.texture = environmentTexture;
            view.type = nri::TextureView::STORAGE_TEXTURE_ARRAY;
            view.format = nri::Format::RGBA16_SFLOAT;
            view.mipOffset = static_cast<nri::Dim_t>(mip);
            view.mipNum = 1;
            view.layerNum = 6;
            if (mImpl->Core->CreateTextureView(
                    view, mImpl->EnvironmentViews[mip]) !=
                nri::Result::SUCCESS)
                throw std::runtime_error(
                    "reflection environment mip view creation failed");
        }

        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = EnvironmentMipCount + 1U;
        pool.storageTextureMaxNum = EnvironmentMipCount + 1U;
        if (mImpl->Core->CreateDescriptorPool(
                *nriDevice, pool, mImpl->Pool) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection IBL descriptor pool creation failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->EnvironmentLayout, 0,
                mImpl->EnvironmentSets.data(), EnvironmentMipCount, 0) !=
            nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection environment descriptor allocation failed");
        if (mImpl->Core->AllocateDescriptorSets(
                *mImpl->Pool, *mImpl->BrdfLayout, 0,
                &mImpl->BrdfSet, 1, 0) != nri::Result::SUCCESS)
            throw std::runtime_error(
                "reflection BRDF descriptor allocation failed");
        for (uint32_t mip = 0; mip < EnvironmentMipCount; ++mip) {
            const nri::UpdateDescriptorRangeDesc update{
                mImpl->EnvironmentSets[mip], 0, 0,
                &mImpl->EnvironmentViews[mip], 1};
            mImpl->Core->UpdateDescriptorRanges(&update, 1);
        }
        nri::Descriptor* brdfView = NriInteropAccess::TextureView(
            interop, mImpl->Brdf.Image, true);
        const nri::UpdateDescriptorRangeDesc brdfUpdate{
            mImpl->BrdfSet, 0, 0, &brdfView, 1};
        mImpl->Core->UpdateDescriptorRanges(&brdfUpdate, 1);

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

bool ReflectionIblPass::Execute(
    uint32_t frameIndex,
    const ReflectionEnvironmentProfile& profile) {
    mImpl->ProfileUpdated = false;
    mImpl->LastBarrierExecution = {};
    if (!mImpl->Ready) return false;
#ifndef ENABLE_OOT3D_NRI
    (void)frameIndex;
    (void)profile;
    return false;
#else
    nri::CommandBuffer* command =
        NriInteropAccess::CommandBuffer(*mImpl->Interop, frameIndex);
    if (command == nullptr) {
        mImpl->Reason =
            "reflection IBL NRI command buffer is unavailable";
        return false;
    }
    const bool updateEnvironment =
        !mImpl->EnvironmentGenerated ||
        mImpl->ProfileSignature != profile.Signature;
    const bool updateBrdf = !mImpl->BrdfGenerated;
    if (!updateEnvironment && !updateBrdf) {
        mImpl->PicaDerived = profile.PicaDerived;
        mImpl->Reason.clear();
        return true;
    }

    const auto transition = [&](VkImage image, ResourceAccess access) {
        const auto planned = mImpl->States.PlanTransition(
            reinterpret_cast<uintptr_t>(image), {access, 0});
        if (!NriInteropAccess::CmdTextureBarrier(
                *mImpl->Interop, frameIndex, image, planned))
            return false;
        mImpl->LastBarrierExecution.RecordPrivateTransition(planned);
        mImpl->States.Commit(planned);
        return true;
    };
    if ((updateEnvironment &&
         !transition(
             mImpl->Environment.Image, ResourceAccess::ComputeWrite)) ||
        (updateBrdf &&
         !transition(mImpl->Brdf.Image, ResourceAccess::ComputeWrite))) {
        mImpl->Reason = "reflection IBL write transition failed";
        return false;
    }

    mImpl->Core->CmdSetDescriptorPool(*command, *mImpl->Pool);
    if (updateEnvironment) {
        mImpl->Core->CmdSetPipelineLayout(
            *command, nri::BindPoint::COMPUTE,
            *mImpl->EnvironmentLayout);
        mImpl->Core->CmdSetPipeline(
            *command, *mImpl->EnvironmentPipeline);
        for (uint32_t mip = 0; mip < EnvironmentMipCount; ++mip) {
            const uint32_t extent =
                std::max(EnvironmentSize >> mip, 1U);
            EnvironmentPush push{};
            std::copy(profile.Sky.begin(), profile.Sky.end(),
                      push.Sky.begin());
            std::copy(profile.Horizon.begin(), profile.Horizon.end(),
                      push.Horizon.begin());
            std::copy(profile.Ground.begin(), profile.Ground.end(),
                      push.Ground.begin());
            push.Sky[3] = push.Horizon[3] = push.Ground[3] = 1.0F;
            push.Extent = extent;
            push.MipLevel = mip;
            push.MipCount = EnvironmentMipCount;
            push.SampleCount = 64U;
            const nri::SetDescriptorSetDesc set{
                0, mImpl->EnvironmentSets[mip]};
            mImpl->Core->CmdSetDescriptorSet(*command, set);
            const nri::SetRootConstantsDesc constants{
                0, &push, sizeof(push)};
            mImpl->Core->CmdSetRootConstants(*command, constants);
            const nri::DispatchDesc dispatch{
                (extent + 7U) / 8U, (extent + 7U) / 8U, 6U};
            mImpl->Core->CmdDispatch(*command, dispatch);
        }
    }
    if (updateBrdf) {
        mImpl->Core->CmdSetPipelineLayout(
            *command, nri::BindPoint::COMPUTE, *mImpl->BrdfLayout);
        mImpl->Core->CmdSetPipeline(*command, *mImpl->BrdfPipeline);
        const nri::SetDescriptorSetDesc set{0, mImpl->BrdfSet};
        mImpl->Core->CmdSetDescriptorSet(*command, set);
        const BrdfPush push{BrdfSize, BrdfSize, 256U, 0U};
        const nri::SetRootConstantsDesc constants{
            0, &push, sizeof(push)};
        mImpl->Core->CmdSetRootConstants(*command, constants);
        const nri::DispatchDesc dispatch{
            (BrdfSize + 7U) / 8U, (BrdfSize + 7U) / 8U, 1U};
        mImpl->Core->CmdDispatch(*command, dispatch);
    }

    if ((updateEnvironment &&
         !transition(mImpl->Environment.Image,
                     ResourceAccess::ShaderRead)) ||
        (updateBrdf &&
         !transition(mImpl->Brdf.Image, ResourceAccess::ShaderRead))) {
        mImpl->Reason = "reflection IBL read transition failed";
        return false;
    }
    mImpl->EnvironmentGenerated |= updateEnvironment;
    mImpl->BrdfGenerated |= updateBrdf;
    mImpl->ProfileSignature = profile.Signature;
    mImpl->ProfileUpdated = updateEnvironment;
    mImpl->PicaDerived = profile.PicaDerived;
    mImpl->BarriersUsed = true;
    mImpl->Reason.clear();
    return true;
#endif
}

void ReflectionIblPass::InvalidateProfile() {
    mImpl->ProfileSignature = 0;
    mImpl->EnvironmentGenerated = false;
    mImpl->ProfileUpdated = false;
    mImpl->PicaDerived = false;
}

void ReflectionIblPass::Shutdown() {
#ifdef ENABLE_OOT3D_NRI
    if (mImpl->Core != nullptr) {
        if (mImpl->Pool != nullptr)
            mImpl->Core->DestroyDescriptorPool(mImpl->Pool);
        for (uint32_t mip = 1; mip < EnvironmentMipCount; ++mip) {
            if (mImpl->EnvironmentViews[mip] != nullptr)
                mImpl->Core->DestroyDescriptor(
                    mImpl->EnvironmentViews[mip]);
        }
        if (mImpl->EnvironmentPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->EnvironmentPipeline);
        if (mImpl->BrdfPipeline != nullptr)
            mImpl->Core->DestroyPipeline(mImpl->BrdfPipeline);
        if (mImpl->EnvironmentLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(
                mImpl->EnvironmentLayout);
        if (mImpl->BrdfLayout != nullptr)
            mImpl->Core->DestroyPipelineLayout(mImpl->BrdfLayout);
    }
#endif
    if (mImpl->Interop != nullptr) {
        if (mImpl->Environment.Image != VK_NULL_HANDLE)
            mImpl->Interop->DestroyOwnedTexture(
                mImpl->Environment.Image);
        if (mImpl->Brdf.Image != VK_NULL_HANDLE)
            mImpl->Interop->DestroyOwnedTexture(mImpl->Brdf.Image);
    }
    mImpl = std::make_unique<Impl>();
}

bool ReflectionIblPass::Available() const { return mImpl->Ready; }
VkImage ReflectionIblPass::EnvironmentImage() const {
    return mImpl->Environment.Image;
}
VkImage ReflectionIblPass::BrdfImage() const {
    return mImpl->Brdf.Image;
}
bool ReflectionIblPass::EnvironmentOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Environment.Image);
}
bool ReflectionIblPass::BrdfOwnedByNri() const {
    return mImpl->Interop != nullptr &&
           mImpl->Interop->OwnsTexture(mImpl->Brdf.Image);
}
bool ReflectionIblPass::ComputeOwnedByNri() const {
#ifdef ENABLE_OOT3D_NRI
    return mImpl->Ready && mImpl->EnvironmentPipeline != nullptr &&
           mImpl->BrdfPipeline != nullptr && mImpl->Pool != nullptr;
#else
    return false;
#endif
}
bool ReflectionIblPass::BarriersOwnedByNri() const {
    return mImpl->BarriersUsed;
}
const EffectPassBarrierExecution& ReflectionIblPass::LastBarrierExecution()
    const {
    return mImpl->LastBarrierExecution;
}
bool ReflectionIblPass::ProfileUpdatedLastExecute() const {
    return mImpl->ProfileUpdated;
}
bool ReflectionIblPass::PicaDerivedLastExecute() const {
    return mImpl->PicaDerived;
}
const std::string& ReflectionIblPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d

#endif
