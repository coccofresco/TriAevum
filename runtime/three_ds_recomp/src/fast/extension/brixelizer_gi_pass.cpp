#include "fast/extension/brixelizer_gi_prepare_shader.h"
#include "fast/extension/brixelizer_gi_pass.h"

#ifdef ENABLE_OOT3D_VULKAN

#include "fast/extension/brixelizer_scene.h"
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_interop_context.h"
#include "fast/extension/nri_storage_clear_binding.h"
#include "fast/extension/resource_state_tracker.h"

#if defined(ENABLE_OOT3D_NRI) && defined(ENABLE_OOT3D_BRIXELIZER_GI) && \
    __has_include(<FidelityFX/host/backends/vk/ffx_vk.h>) && __has_include(<FidelityFX/host/ffx_brixelizergi.h>)
#define OOT3D_HAS_BRIXELIZER_GI_PASS 1
#include "brixelizer_gi_access.h"
#include "nri_interop_internal.h"
#else
#define OOT3D_HAS_BRIXELIZER_GI_PASS 0
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace Fast::Extension {
namespace {

// Linear direct radiance, depth and world normals of one presented sample:
// the current inputs of the GI dispatch and, one sample later, its histories.
// The radiance is the guide's direct light only (native scene directionals,
// key, dynamic lights): ambient and fill are baked into OoT's colours and
// would be bounced twice.
// Rays that leave the scene receive nothing: sky and ambient light are baked
// into the native colour, not GI sources.


std::array<float, 16> MultiplyColumnMajor(const std::array<float, 16>& a, const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    for (size_t column = 0; column < 4; ++column)
        for (size_t row = 0; row < 4; ++row)
            for (size_t k = 0; k < 4; ++k) out[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return out;
}

VkImageCreateInfo ImageInfo(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1U};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return info;
}

} // namespace

struct BrixelizerGiInstance {
    BrixelizerGiInputs Inputs;
    GlobalIlluminationResolution Resolution = GlobalIlluminationResolution::Half;
    bool Specular = false;
    // Source weights of the light injected by the latest configuration.
    std::array<float, 4> SourceWeights{};
    // Prepared inputs, alternating: [Current] this sample, the other the history.
    std::array<NriOwnedTexture2D, 2> Lit{};
    std::array<NriOwnedTexture2D, 2> Depth{};
    std::array<NriOwnedTexture2D, 2> Normal{};
    // Specular GI of the latest sample (required by the dispatch even with
    // specular disabled).
    NriOwnedTexture2D SpecularOutput{};
    // GI of this surface's latest sample. The GI context runs once per
    // presented frame (its history and the backend's per-dispatch resource
    // views assume it); every composition of the surface copies this.
    NriOwnedTexture2D Output{};
    ResourceStateTracker States;
    // World from view of the latest trace (column-major), for the developer dump.
    std::array<float, 16> InverseView{};
    uint32_t Current = 0;
    bool HistoryPrepared = false;
    uint64_t LastUsedFrame = 0;
    uint64_t LastTracedFrame = 0;
    bool Traced = false;
#if OOT3D_HAS_BRIXELIZER_GI_PASS
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, 2> Sets{};
    FfxBrixelizerGIContext Context{};
    bool ContextCreated = false;
#endif
};

struct BrixelizerGiPass::Impl {
    VkDevice Device = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    BrixelizerScene* Scene = nullptr;
    std::vector<std::unique_ptr<BrixelizerGiInstance>> Instances;
    BrixelizerGiInstance* Active = nullptr;
    // First frame that may attempt a creation again after one failed.
    uint64_t CreationRetryFrame = 0;
    EffectPassBarrierExecution LastBarriers;
    BrixelizerGiStats Stats;
    NriStorageClearBinding Clear;
    bool Initialized = false;
    std::string Reason = "Brixelizer GI is not initialized";
    VkPhysicalDevice Physical = VK_NULL_HANDLE;

    // Developer dump, off unless OOT3D_GRAPHICS_BRIXELIZER_GI_DUMP=<dir> is set
    // (OOT3D_GRAPHICS_BRIXELIZER_GI_DUMP_FRAME=<n>, default 300): the diffuse
    // GI output and the prepared inputs (native depth, world normals from the
    // normal guide, linear direct radiance) of the first traced frame at or
    // after n, at the surface extent in storage order, for comparisons outside
    // the game. Never persisted; nothing changes when it is unset.
    struct DumpState {
        struct Part {
            const char* Name;
            const char* Format;
            VkDeviceSize Offset, Size;
        };
        std::string Directory;
        uint64_t Frame = 300;
        bool Recorded = false;
        uint64_t RecordedFrame = 0;
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        void* Mapped = nullptr;
        std::vector<Part> Parts;
        uint32_t Width = 0, Height = 0;
        std::array<float, 16> InverseView{};
        std::array<float, 3> Environment{};
        std::array<float, 4> SourceWeights{};
        GlobalIlluminationResolution Resolution = GlobalIlluminationResolution::Half;
    };
    std::unique_ptr<DumpState> Dump;

    void ReleaseDump() {
        if (!Dump) return;
        if (Dump->Mapped != nullptr) vkUnmapMemory(Device, Dump->Memory);
        if (Dump->Buffer != VK_NULL_HANDLE) vkDestroyBuffer(Device, Dump->Buffer, nullptr);
        if (Dump->Memory != VK_NULL_HANDLE) vkFreeMemory(Device, Dump->Memory, nullptr);
        Dump->Mapped = nullptr;
        Dump->Buffer = VK_NULL_HANDLE;
        Dump->Memory = VK_NULL_HANDLE;
    }
    // Writes a recorded dump once its frame's copies have executed (every frame
    // slot has been recorded again since), or unconditionally at shutdown.
    void FinishDump(uint64_t frameId, bool shutdown) {
        if (!Dump || !Dump->Recorded || Dump->Mapped == nullptr) return;
        if (!shutdown && frameId < Dump->RecordedFrame + 4U) return;
        std::filesystem::create_directories(Dump->Directory);
        bool ok = true;
        std::string parts;
        for (const auto& part : Dump->Parts) {
            std::ofstream file(Dump->Directory + "/brix_" + part.Name + ".bin", std::ios::binary);
            file.write(static_cast<const char*>(Dump->Mapped) + part.Offset, static_cast<std::streamsize>(part.Size));
            ok = ok && file.good();
            parts += std::string(parts.empty() ? "" : ", ") + "\"" + part.Name + "\": \"" + part.Format + "\"";
        }
        std::ofstream meta(Dump->Directory + "/brix_dump.json");
        const auto& m = Dump->InverseView;
        char text[2048];
        std::snprintf(text, sizeof(text),
                      "{\n  \"frame\": %llu,\n  \"extent\": [%u, %u],\n  \"parts\": {%s},\n"
                      "  \"normal_encoding\": \"world normal * 0.5 + 0.5 (from the normal guide)\",\n"
                      "  \"resolution\": \"%s\",\n  \"environment\": [%.9g, %.9g, %.9g],\n"
                      "  \"source_weights\": [%.9g, %.9g, %.9g, %.9g],\n"
                      "  \"world_from_view_column_major\": [%.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, "
                      "%.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g]\n}\n",
                      static_cast<unsigned long long>(Dump->RecordedFrame), Dump->Width, Dump->Height, parts.c_str(),
                      Dump->Resolution == GlobalIlluminationResolution::Quarter ? "Quarter" : "Half",
                      Dump->Environment[0], Dump->Environment[1], Dump->Environment[2], Dump->SourceWeights[0],
                      Dump->SourceWeights[1], Dump->SourceWeights[2], Dump->SourceWeights[3], m[0], m[1], m[2], m[3],
                      m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
        meta << text;
        ok = ok && meta.good();
        std::fprintf(stderr, "TRIAEVUM_GLOBAL_ILLUMINATION dump frame=%llu extent=%ux%u written=%d dir=%s\n",
                     static_cast<unsigned long long>(Dump->RecordedFrame), Dump->Width, Dump->Height, ok ? 1 : 0,
                     Dump->Directory.c_str());
        ReleaseDump();
        Dump.reset();
    }
#if OOT3D_HAS_BRIXELIZER_GI_PASS
    nri::CoreInterface* Core = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    nri::Pipeline* Pipeline = nullptr;

    void Destroy(BrixelizerGiInstance& instance) {
        if (instance.ContextCreated) {
            ffxBrixelizerGIContextDestroy(&instance.Context);
            instance.ContextCreated = false;
        }
        if (instance.Pool != nullptr) Core->DestroyDescriptorPool(instance.Pool);
        instance.Pool = nullptr;
        instance.Sets = {};
        for (auto* textures : {&instance.Lit, &instance.Depth, &instance.Normal})
            for (auto& texture : *textures)
                if (texture.Image != VK_NULL_HANDLE) Interop->DestroyOwnedTexture(texture.Image);
        if (instance.SpecularOutput.Image != VK_NULL_HANDLE)
            Interop->DestroyOwnedTexture(instance.SpecularOutput.Image);
        if (instance.Output.Image != VK_NULL_HANDLE) Interop->DestroyOwnedTexture(instance.Output.Image);
        instance.Lit = instance.Depth = instance.Normal = {};
        instance.SpecularOutput = instance.Output = {};
        instance.States.Clear();
    }
    void Create(BrixelizerGiInstance& instance, FfxInterface& backend) {
        const auto& in = instance.Inputs;
        const auto allocate = [&](NriOwnedTexture2D& texture, VkFormat format) {
            if (!Interop->CreateOwnedTexture2D(in.Width, in.Height, format, texture))
                throw std::runtime_error("Brixelizer GI image allocation failed");
        };
        for (uint32_t i = 0; i < 2; ++i) {
            allocate(instance.Lit[i], VK_FORMAT_R16G16B16A16_SFLOAT);
            allocate(instance.Depth[i], VK_FORMAT_R32_SFLOAT);
            allocate(instance.Normal[i], VK_FORMAT_R16G16B16A16_SFLOAT);
        }
        allocate(instance.SpecularOutput, VK_FORMAT_R16G16B16A16_SFLOAT);
        allocate(instance.Output, VK_FORMAT_R16G16B16A16_SFLOAT);
        for (const auto& [image, format] : {std::pair{in.SurfaceGuide, in.SurfaceGuideFormat},
                                            std::pair{in.Depth, in.DepthFormat},
                                            std::pair{in.NormalGuide, in.NormalGuideFormat}})
            if (!Interop->WrapTexture(image, format, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT, in.Width,
                                      in.Height))
                throw std::runtime_error("Brixelizer GI input wrapping failed");
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = 2;
        pool.textureMaxNum = 6;
        pool.storageTextureMaxNum = 6;
        auto* device = NriInteropAccess::Device(*Interop);
        if (Core->CreateDescriptorPool(*device, pool, instance.Pool) != nri::Result::SUCCESS ||
            Core->AllocateDescriptorSets(*instance.Pool, *Layout, 0, instance.Sets.data(), 2, 0) !=
                nri::Result::SUCCESS)
            throw std::runtime_error("Brixelizer GI descriptor allocation failed");
        for (uint32_t i = 0; i < 2; ++i) {
            std::array<nri::Descriptor*, 6> views{
                NriInteropAccess::TextureView(*Interop, in.SurfaceGuide, false),
                NriInteropAccess::TextureView(*Interop, in.Depth, false),
                NriInteropAccess::TextureView(*Interop, in.NormalGuide, false),
                NriInteropAccess::TextureView(*Interop, instance.Lit[i].Image, true),
                NriInteropAccess::TextureView(*Interop, instance.Depth[i].Image, true),
                NriInteropAccess::TextureView(*Interop, instance.Normal[i].Image, true)};
            for (auto* view : views)
                if (view == nullptr) throw std::runtime_error("Brixelizer GI descriptors unavailable");
            std::array<nri::UpdateDescriptorRangeDesc, 6> updates{};
            for (uint32_t range = 0; range < 6; ++range)
                updates[range] = {instance.Sets[i], range, 0, &views[range], 1};
            Core->UpdateDescriptorRanges(updates.data(), static_cast<uint32_t>(updates.size()));
        }
        FfxBrixelizerGIContextDescription description{};
        description.flags = instance.Specular ? static_cast<FfxBrixelizerGIFlags>(0)
                                              : FFX_BRIXELIZER_GI_FLAG_DISABLE_SPECULAR;
        description.internalResolution = instance.Resolution == GlobalIlluminationResolution::Quarter
                                             ? FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_25_PERCENT
                                             : FFX_BRIXELIZER_GI_INTERNAL_RESOLUTION_50_PERCENT;
        description.displaySize = {in.Width, in.Height};
        description.backendInterface = backend;
        if (const FfxErrorCode error = ffxBrixelizerGIContextCreate(&instance.Context, &description);
            error != FFX_OK) {
            // The SDK checks its arguments before it takes a backend context
            // and releases nothing after that. Its destroy frees the context and
            // whatever pipelines and resources were created (it skips the rest),
            // so a failed attempt never keeps one of the backend's GI contexts.
            if (error != FFX_ERROR_INVALID_POINTER && error != FFX_ERROR_INCOMPLETE_INTERFACE &&
                error != FFX_ERROR_INVALID_ARGUMENT)
                (void)ffxBrixelizerGIContextDestroy(&instance.Context);
            char code[16];
            std::snprintf(code, sizeof(code), "0x%08x", static_cast<unsigned>(error));
            throw std::runtime_error(std::string("Brixelizer GI context creation failed (FidelityFX error ") + code +
                                     ")");
        }
        instance.ContextCreated = true;
    }
#endif
};

BrixelizerGiPass::BrixelizerGiPass() : mImpl(std::make_unique<Impl>()) {}
BrixelizerGiPass::~BrixelizerGiPass() { Shutdown(); }

bool BrixelizerGiPass::Initialize(VkPhysicalDevice physicalDevice, VkDevice device, NriInteropContext& interop,
                                  BrixelizerScene& scene) {
    Shutdown();
    mImpl->Device = device;
    mImpl->Interop = &interop;
    mImpl->Scene = &scene;
    mImpl->Physical = physicalDevice;
    if (const char* dump = std::getenv("OOT3D_GRAPHICS_BRIXELIZER_GI_DUMP"); dump != nullptr && *dump != '\0') {
        mImpl->Dump = std::make_unique<Impl::DumpState>();
        mImpl->Dump->Directory = dump;
        if (const char* frame = std::getenv("OOT3D_GRAPHICS_BRIXELIZER_GI_DUMP_FRAME"); frame != nullptr)
            mImpl->Dump->Frame = std::strtoull(frame, nullptr, 10);
        std::fprintf(stderr, "TRIAEVUM_GLOBAL_ILLUMINATION developer dump=%s frame=%llu\n", dump,
                     static_cast<unsigned long long>(mImpl->Dump->Frame));
    }
#if !OOT3D_HAS_BRIXELIZER_GI_PASS
    (void)physicalDevice;
    mImpl->Reason = "Brixelizer GI is not built into this renderer";
    return false;
#else
    try {
        if (!interop.Available()) throw std::runtime_error(interop.UnavailableReason());
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R32_SFLOAT, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0U)
            throw std::runtime_error("R32F storage images are unavailable");
        mImpl->Core = NriInteropAccess::Core(interop);
        auto* nriDevice = NriInteropAccess::Device(interop);
        if (mImpl->Core == nullptr || nriDevice == nullptr)
            throw std::runtime_error("Brixelizer GI NRI device unavailable");
        std::array<nri::DescriptorRangeDesc, 6> ranges{};
        for (uint32_t i = 0; i < 6; ++i)
            ranges[i] = {i, 1, i < 3 ? nri::DescriptorType::TEXTURE : nri::DescriptorType::STORAGE_TEXTURE,
                         nri::StageBits::COMPUTE_SHADER};
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = static_cast<uint32_t>(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(float) * 16;
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mImpl->Core->CreatePipelineLayout(*nriDevice, layout, mImpl->Layout) != nri::Result::SUCCESS)
            throw std::runtime_error("Brixelizer GI layout creation failed");
        const auto code =
            interop.Shaders().Resolve(kBrixelizerGiPrepareShader, Renderer::SpirvStage::Compute, "brixelizer_gi_prepare.comp");
        nri::ComputePipelineDesc pipeline{};
        pipeline.pipelineLayout = mImpl->Layout;
        pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
        pipeline.shader.bytecode = code.data();
        pipeline.shader.size = code.size() * sizeof(uint32_t);
        pipeline.cache = NriInteropAccess::PipelineCache(interop);
        if (mImpl->Core->CreateComputePipeline(*nriDevice, pipeline, mImpl->Pipeline) != nri::Result::SUCCESS)
            throw std::runtime_error("Brixelizer GI prepare pipeline creation failed");
        if (!mImpl->Clear.Initialize(interop)) throw std::runtime_error(mImpl->Clear.UnavailableReason());
        mImpl->Initialized = true;
        mImpl->Reason.clear();
        return true;
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
#endif
}

bool BrixelizerGiPass::Configure(uint64_t frameId, const BrixelizerGiInputs& inputs,
                                 GlobalIlluminationResolution resolution, bool specular,
                                 const std::array<float, 4>& sourceWeights) {
    mImpl->Active = nullptr;
    if (!mImpl->Initialized || inputs.Width == 0U || inputs.Height == 0U || inputs.SurfaceGuide == VK_NULL_HANDLE ||
        inputs.Depth == VK_NULL_HANDLE || inputs.NormalGuide == VK_NULL_HANDLE ||
        inputs.MaterialGuide == VK_NULL_HANDLE || inputs.Motion == VK_NULL_HANDLE) {
        mImpl->Reason = "Brixelizer GI inputs are unavailable";
        return false;
    }
    bool sourceReset = false;
    for (auto& instance : mImpl->Instances) {
        if (instance->Inputs == inputs && instance->Resolution == resolution && instance->Specular == specular) {
            sourceReset = RestartsGlobalIlluminationHistory(instance->SourceWeights, sourceWeights);
            if (sourceReset) break;
            instance->SourceWeights = sourceWeights;
            instance->LastUsedFrame = frameId;
            mImpl->Active = instance.get();
            return true;
        }
    }
#if !OOT3D_HAS_BRIXELIZER_GI_PASS
    (void)frameId;
    return false;
#else
    FfxInterface* backend = BrixelizerSceneGiAccess::Backend(*mImpl->Scene);
    if (backend == nullptr) {
        mImpl->Reason = "the scene distance field is unavailable";
        return false;
    }
    // Reason still names the failure that deferred the attempt.
    if (frameId < mImpl->CreationRetryFrame) return false;
    // A resolution, specular or source change replaces the surface's instance; otherwise, when
    // full, the least recently used surface retires. Never one this frame's
    // command buffer already references.
    auto retired = std::find_if(mImpl->Instances.begin(), mImpl->Instances.end(),
                                [&](const auto& instance) { return instance->Inputs == inputs; });
    if (retired == mImpl->Instances.end() && mImpl->Instances.size() >= BrixelizerScene::kMaxGiSurfaces)
        retired = std::min_element(mImpl->Instances.begin(), mImpl->Instances.end(),
                                   [](const auto& a, const auto& b) { return a->LastUsedFrame < b->LastUsedFrame; });
    if (retired != mImpl->Instances.end()) {
        if ((*retired)->LastUsedFrame >= frameId) {
            mImpl->Reason = "Brixelizer GI has no free surface instance this frame";
            return false;
        }
        if (!mImpl->Interop->CompleteRecordedWork()) {
            mImpl->Reason = "Brixelizer GI retirement wait failed";
            return false;
        }
        mImpl->Destroy(**retired);
        mImpl->Instances.erase(retired);
        ++mImpl->Stats.InstancesRetired;
    }
    auto instance = std::make_unique<BrixelizerGiInstance>();
    instance->Inputs = inputs;
    instance->Resolution = resolution;
    instance->Specular = specular;
    instance->SourceWeights = sourceWeights;
    instance->LastUsedFrame = frameId;
    try {
        mImpl->Create(*instance, *backend);
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        mImpl->Destroy(*instance);
        mImpl->CreationRetryFrame = frameId + kCreationRetryFrames;
        ++mImpl->Stats.CreationFailures;
        return false;
    }
    mImpl->Active = instance.get();
    mImpl->Instances.push_back(std::move(instance));
    ++mImpl->Stats.InstancesCreated;
    if (sourceReset) ++mImpl->Stats.SourceResets;
    mImpl->Reason.clear();
    return true;
#endif
}

bool BrixelizerGiPass::Execute(VkCommandBuffer commandBuffer, uint32_t frameSlot, uint64_t frameId,
                               const TemporalViewState& temporal, const PerspectiveViewState& view,
                               PicaSurfaceCoordinates coordinates, const NriEffectGraphTransientImageBinding& output,
                               const NriEffectGraphTransientImageBinding* specularOutput,
                               const std::array<float, 3>& environment, const EffectPassBarrierPlan& barriers) {
    mImpl->LastBarriers = {};
#if !OOT3D_HAS_BRIXELIZER_GI_PASS
    (void)commandBuffer, (void)frameSlot, (void)frameId, (void)temporal, (void)view, (void)coordinates, (void)output,
        (void)specularOutput, (void)barriers;
    return false;
#else
    const auto declared = [&](const NriEffectGraphTransientImageBinding& binding, EffectResource resource) {
        const auto* barrier = barriers.Find(resource);
        return barrier != nullptr && barrier->Managed() && binding.Valid() && binding.Resource == resource &&
               binding.Format == VK_FORMAT_R16G16B16A16_SFLOAT && binding.Width == mImpl->Active->Inputs.Width &&
               binding.Height == mImpl->Active->Inputs.Height;
    };
    if (mImpl->Active == nullptr || !mImpl->Active->ContextCreated || commandBuffer == VK_NULL_HANDLE ||
        !barriers.Valid() || !declared(output, EffectResource::DiffuseIllumination) ||
        (specularOutput != nullptr && !declared(*specularOutput, EffectResource::SpecularIllumination)) ||
        mImpl->Active->Specular != (specularOutput != nullptr)) {
        mImpl->Reason = "Brixelizer GI output is not declared";
        return false;
    }
    auto& instance = *mImpl->Active;
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    if (command == nullptr) return false;
    mImpl->LastBarriers = barriers.BeginExecution();
    const auto privateTransition = [&](VkImage image, ResourceAccess access) {
        const auto change = instance.States.PlanTransition(reinterpret_cast<uintptr_t>(image), {access, 0});
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, image, change))
            throw std::runtime_error("Brixelizer GI transition failed");
        instance.States.Commit(change);
        mImpl->LastBarriers.RecordPrivateTransition(change);
    };
    const auto graphTransition = [&](const NriEffectGraphTransientImageBinding& target, ResourceAccess access) {
        const auto change =
            target.StateTracker->PlanTransition(reinterpret_cast<uintptr_t>(target.Texture.Image), {access, 0});
        if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, target.Texture.Image, change))
            throw std::runtime_error("Brixelizer GI output transition failed");
        target.StateTracker->Commit(change);
        mImpl->LastBarriers.RecordGraphTransition(change);
    };
    const auto copy = [&](const NriOwnedTexture2D& source, const NriEffectGraphTransientImageBinding& target) {
        privateTransition(source.Image, ResourceAccess::TransferRead);
        graphTransition(target, ResourceAccess::TransferWrite);
        auto* from = NriInteropAccess::Texture(*mImpl->Interop, source.Image);
        auto* to = NriInteropAccess::Texture(*mImpl->Interop, target.Texture.Image);
        if (from == nullptr || to == nullptr) throw std::runtime_error("Brixelizer GI copy textures unavailable");
        mImpl->Core->CmdCopyTexture(*command, *to, nullptr, *from, nullptr);
        graphTransition(target, barriers.Find(target.Resource)->CompletionAccess);
    };
    mImpl->FinishDump(frameId, false);
    // Developer dump: copies of the output (already a transfer source after
    // `copy`) and of the inputs this trace prepared, into one host buffer.
    const auto recordDump = [&]() {
        auto& d = *mImpl->Dump;
        d.Recorded = true;
        const uint32_t prepared = instance.Current ^ 1U; // Trace advanced Current after the dispatch
        const uint64_t texels = uint64_t(instance.Inputs.Width) * instance.Inputs.Height;
        const std::array<std::tuple<const char*, const char*, const NriOwnedTexture2D*, uint32_t>, 4> sources{{
            {"diffuse", "rgba16f", &instance.Output, 8U},
            {"normal", "rgba16f", &instance.Normal[prepared], 8U},
            {"depth", "r32f", &instance.Depth[prepared], 4U},
            {"lit", "rgba16f", &instance.Lit[prepared], 8U}}};
        VkDeviceSize total = 0;
        for (const auto& [name, format, texture, bytes] : sources) {
            d.Parts.push_back({name, format, total, texels * bytes});
            total += (texels * bytes + 255U) / 256U * 256U;
        }
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = total;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkMemoryRequirements requirements{};
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(mImpl->Physical, &memory);
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        uint32_t type = memory.memoryTypeCount;
        if (vkCreateBuffer(mImpl->Device, &info, nullptr, &d.Buffer) == VK_SUCCESS) {
            vkGetBufferMemoryRequirements(mImpl->Device, d.Buffer, &requirements);
            for (uint32_t t = 0; t < memory.memoryTypeCount && type == memory.memoryTypeCount; ++t)
                if ((requirements.memoryTypeBits & (1U << t)) && (memory.memoryTypes[t].propertyFlags & wanted) == wanted)
                    type = t;
        }
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (d.Buffer == VK_NULL_HANDLE || type == memory.memoryTypeCount ||
            vkAllocateMemory(mImpl->Device, &allocation, nullptr, &d.Memory) != VK_SUCCESS ||
            vkBindBufferMemory(mImpl->Device, d.Buffer, d.Memory, 0) != VK_SUCCESS ||
            vkMapMemory(mImpl->Device, d.Memory, 0, VK_WHOLE_SIZE, 0, &d.Mapped) != VK_SUCCESS) {
            std::fprintf(stderr, "TRIAEVUM_GLOBAL_ILLUMINATION dump failed: host memory\n");
            mImpl->ReleaseDump();
            return;
        }
        for (size_t i = 0; i < sources.size(); ++i) {
            const NriOwnedTexture2D& texture = *std::get<2>(sources[i]);
            if (texture.Image != instance.Output.Image) privateTransition(texture.Image, ResourceAccess::TransferRead);
            VkBufferImageCopy region{};
            region.bufferOffset = d.Parts[i].Offset;
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {instance.Inputs.Width, instance.Inputs.Height, 1U};
            vkCmdCopyImageToBuffer(commandBuffer, texture.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d.Buffer, 1,
                                   &region);
        }
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
        d.RecordedFrame = frameId;
        d.Width = instance.Inputs.Width;
        d.Height = instance.Inputs.Height;
        d.InverseView = instance.InverseView;
        d.Environment = environment;
        d.SourceWeights = instance.SourceWeights;
        d.Resolution = instance.Resolution;
    };
    if (instance.LastTracedFrame != frameId || !instance.Traced) {
        if (!Trace(instance, commandBuffer, frameSlot, frameId, temporal, view, coordinates, environment,
                   privateTransition))
            return false;
        instance.LastTracedFrame = frameId;
        instance.Traced = true;
    }
    try {
        copy(instance.Output, output);
        if (specularOutput != nullptr) copy(instance.SpecularOutput, *specularOutput);
        if (mImpl->Dump && !mImpl->Dump->Recorded && frameId >= mImpl->Dump->Frame)
            recordDump();
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }
    mImpl->Reason.clear();
    return true;
#endif
}

#if OOT3D_HAS_BRIXELIZER_GI_PASS
template <typename Transition>
bool BrixelizerGiPass::Trace(BrixelizerGiInstance& instance, VkCommandBuffer commandBuffer, uint32_t frameSlot,
                             uint64_t frameId, const TemporalViewState& temporal, const PerspectiveViewState& view,
                             PicaSurfaceCoordinates coordinates, const std::array<float, 3>& environment,
                             const Transition& privateTransition) {
    auto* command = NriInteropAccess::CommandBuffer(*mImpl->Interop, frameSlot);
    // view = projection^-1 * worldToClip; the storage orientation rotates
    // clip XY only (as for SSSR), so the view basis stays unrotated.
    std::array<float, 16> inverseProjection{}, inverseView{};
    if (!InvertTemporalMatrix(view.Projection, inverseProjection)) {
        mImpl->Reason = "Brixelizer GI projection is not invertible";
        return false;
    }
    const auto viewMatrix = MultiplyColumnMajor(inverseProjection, temporal.CurrentWorldToClip);
    const auto previousView = MultiplyColumnMajor(inverseProjection, temporal.PreviousWorldToClip);
    if (!InvertTemporalMatrix(viewMatrix, inverseView)) {
        mImpl->Reason = "Brixelizer GI view is not invertible";
        return false;
    }
    instance.InverseView = inverseView;
    FfxBrixelizerGIDispatchDescription dispatch{};
    if (!BrixelizerSceneGiAccess::Bind(*mImpl->Scene, commandBuffer, frameId, environment, dispatch)) {
        mImpl->Reason = "the scene distance field has no data yet";
        return false;
    }
    const uint32_t current = instance.Current;
    // Without a prepared history the current sample stands in for it.
    const uint32_t history = instance.HistoryPrepared ? current ^ 1U : current;
    try {
        for (auto* texture : {&instance.Lit[current], &instance.Depth[current], &instance.Normal[current]})
            privateTransition(texture->Image, ResourceAccess::ComputeWrite);
        mImpl->Core->CmdSetDescriptorPool(*command, *instance.Pool);
        mImpl->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mImpl->Layout);
        mImpl->Core->CmdSetPipeline(*command, *mImpl->Pipeline);
        mImpl->Core->CmdSetDescriptorSet(*command, {0, instance.Sets[current]});
        mImpl->Core->CmdSetRootConstants(*command, {0, inverseView.data(), sizeof(inverseView)});
        mImpl->Core->CmdDispatch(*command,
                                 {(instance.Inputs.Width + 7) / 8, (instance.Inputs.Height + 7) / 8, 1});
        for (auto* texture : {&instance.Lit[current], &instance.Depth[current], &instance.Normal[current],
                              &instance.Lit[history], &instance.Depth[history], &instance.Normal[history]})
            privateTransition(texture->Image, ResourceAccess::ShaderRead);
        privateTransition(instance.SpecularOutput.Image, ResourceAccess::ComputeWrite);
        privateTransition(instance.Output.Image, ResourceAccess::ComputeWrite);
    } catch (const std::exception& exception) {
        mImpl->Reason = exception.what();
        return false;
    }

    const auto& in = instance.Inputs;
    const auto resource = [](VkImage image, const VkImageCreateInfo& info, const wchar_t* name,
                             FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ) {
        return ffxGetResourceVK(reinterpret_cast<void*>(image), ffxGetImageResourceDescriptionVK(image, info), name,
                                state);
    };
    const auto owned = [&](const NriOwnedTexture2D& texture, VkFormat format, const wchar_t* name,
                           FfxResourceStates state = FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ) {
        return resource(texture.Image,
                        ImageInfo(in.Width, in.Height, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT),
                        name, state);
    };
    dispatch.depth = owned(instance.Depth[current], VK_FORMAT_R32_SFLOAT, L"OOT3D_GI_Depth");
    dispatch.historyDepth = owned(instance.Depth[history], VK_FORMAT_R32_SFLOAT, L"OOT3D_GI_HistoryDepth");
    dispatch.normal = owned(instance.Normal[current], VK_FORMAT_R16G16B16A16_SFLOAT, L"OOT3D_GI_Normal");
    dispatch.historyNormal =
        owned(instance.Normal[history], VK_FORMAT_R16G16B16A16_SFLOAT, L"OOT3D_GI_HistoryNormal");
    dispatch.prevLitOutput = owned(instance.Lit[history], VK_FORMAT_R16G16B16A16_SFLOAT, L"OOT3D_GI_PrevLit");
    dispatch.roughness = resource(in.MaterialGuide,
                                  ImageInfo(in.Width, in.Height, in.MaterialGuideFormat, VK_IMAGE_USAGE_SAMPLED_BIT),
                                  L"OOT3D_GI_Material");
    dispatch.motionVectors = resource(in.Motion,
                                      ImageInfo(in.Width, in.Height, in.MotionFormat,
                                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT),
                                      L"OOT3D_GI_Motion");
    dispatch.outputDiffuseGI = owned(instance.Output, VK_FORMAT_R16G16B16A16_SFLOAT, L"OOT3D_GI_Diffuse",
                                     FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.outputSpecularGI = owned(instance.SpecularOutput, VK_FORMAT_R16G16B16A16_SFLOAT, L"OOT3D_GI_Specular",
                                      FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    const auto projection = coordinates.FidelityFxClip(view.Projection);
    std::memcpy(dispatch.view, viewMatrix.data(), sizeof(dispatch.view));
    std::memcpy(dispatch.projection, projection.data(), sizeof(dispatch.projection));
    std::memcpy(dispatch.prevView, previousView.data(), sizeof(dispatch.prevView));
    std::memcpy(dispatch.prevProjection, projection.data(), sizeof(dispatch.prevProjection));
    dispatch.cameraPosition[0] = inverseView[12];
    dispatch.cameraPosition[1] = inverseView[13];
    dispatch.cameraPosition[2] = inverseView[14];
    dispatch.startCascade = BrixelizerScene::kMergedCascadeBase;
    dispatch.endCascade = BrixelizerScene::kMergedCascadeBase + BrixelizerScene::kCascadeCount - 1;
    // Native world units, relative to the finest voxel. A push-off below one
    // voxel let rays re-hit their own surface (Link lit his surroundings with
    // his own colour); a tight solve epsilon missed thin native geometry.
    dispatch.rayPushoff = BrixelizerScene::kBaseVoxelSize;
    dispatch.sdfSolveEps = BrixelizerScene::kBaseVoxelSize * 0.5F;
    dispatch.specularRayPushoff = dispatch.rayPushoff;
    dispatch.specularSDFSolveEps = dispatch.sdfSolveEps;
    dispatch.tMin = 0.0F;
    dispatch.tMax = 1.0e5F;
    dispatch.normalsUnpackMul = 2.0F;
    dispatch.normalsUnpackAdd = -1.0F;
    dispatch.isRoughnessPerceptual = true;
    dispatch.roughnessChannel = 1U;
    dispatch.roughnessThreshold = 0.9F;
    dispatch.environmentMapIntensity = 1.0F;
    dispatch.motionVectorScale = {1.0F, 1.0F};
    if (ffxBrixelizerGIContextDispatch(&instance.Context, &dispatch, ffxGetCommandListVK(commandBuffer)) != FFX_OK) {
        mImpl->Reason = "Brixelizer GI dispatch failed";
        return false;
    }
    instance.HistoryPrepared = true;
    instance.Current ^= 1U;
    return true;
}
#endif

bool BrixelizerGiPass::ClearOutput(uint32_t frameSlot, const NriEffectGraphTransientImageBinding& output,
                                   const NriEffectGraphTransientImageBinding* specularOutput,
                                   const EffectPassBarrierPlan& barriers) {
    mImpl->LastBarriers = {};
#if !OOT3D_HAS_BRIXELIZER_GI_PASS
    (void)frameSlot, (void)output, (void)specularOutput, (void)barriers;
    return false;
#else
    if (!mImpl->Initialized || !barriers.Valid()) return false;
    mImpl->LastBarriers = barriers.BeginExecution();
    const auto clear = [&](const NriEffectGraphTransientImageBinding& target, EffectResource resource) {
        const auto* barrier = barriers.Find(resource);
        if (barrier == nullptr || !barrier->Managed() || !target.Valid() || target.Resource != resource)
            return false;
        const auto transition = [&](ResourceAccess access) {
            const auto change =
                target.StateTracker->PlanTransition(reinterpret_cast<uintptr_t>(target.Texture.Image), {access, 0});
            if (!NriInteropAccess::CmdTextureBarrier(*mImpl->Interop, frameSlot, target.Texture.Image, change))
                return false;
            target.StateTracker->Commit(change);
            mImpl->LastBarriers.RecordGraphTransition(change);
            return true;
        };
        return transition(ResourceAccess::StorageClear) &&
               mImpl->Clear.ClearUint32(frameSlot, target.Texture.Image, 0U) &&
               transition(barrier->CompletionAccess);
    };
    return clear(output, EffectResource::DiffuseIllumination) &&
           (specularOutput == nullptr || clear(*specularOutput, EffectResource::SpecularIllumination));
#endif
}

void BrixelizerGiPass::InvalidateScreenResources() {
#if OOT3D_HAS_BRIXELIZER_GI_PASS
    for (auto& instance : mImpl->Instances) mImpl->Destroy(*instance);
#endif
    mImpl->Instances.clear();
    mImpl->Active = nullptr;
    mImpl->LastBarriers = {};
}

void BrixelizerGiPass::ResetHistory() {
    if (mImpl->Instances.empty()) return;
    if (!mImpl->Interop->CompleteRecordedWork())
        throw std::runtime_error("Brixelizer GI reset wait failed");
    InvalidateScreenResources();
    ++mImpl->Stats.WorldResets;
}

void BrixelizerGiPass::Shutdown() {
    mImpl->FinishDump(0U, true);
    mImpl->ReleaseDump();
    InvalidateScreenResources();
    mImpl->Clear.Shutdown();
#if OOT3D_HAS_BRIXELIZER_GI_PASS
    if (mImpl->Core != nullptr) {
        if (mImpl->Pipeline != nullptr) mImpl->Core->DestroyPipeline(mImpl->Pipeline);
        if (mImpl->Layout != nullptr) mImpl->Core->DestroyPipelineLayout(mImpl->Layout);
    }
#endif
    mImpl = std::make_unique<Impl>();
}

bool BrixelizerGiPass::Available() const { return mImpl->Initialized; }
const BrixelizerGiStats& BrixelizerGiPass::Stats() const { return mImpl->Stats; }
const EffectPassBarrierExecution& BrixelizerGiPass::LastBarrierExecution() const { return mImpl->LastBarriers; }
const std::string& BrixelizerGiPass::UnavailableReason() const { return mImpl->Reason; }

} // namespace Fast::Extension

#endif
