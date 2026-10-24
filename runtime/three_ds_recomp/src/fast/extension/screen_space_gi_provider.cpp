#include "fast/extension/screen_space_gi_provider.h"

// Screen-space diffuse GI provider: own glue code around the title-neutral
// pass. Plumbing follows the repository's providers (per-surface instances,
// graph-declared output transitions through the arena's state tracker,
// recorded-work retirement before replacing resources, a lifecycle line on
// stderr); nothing here is a GI algorithm.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string_view>

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/extension/nri_effect_graph_transient_image_arena.h"
#include "fast/extension/nri_interop_context.h"
#ifdef ENABLE_OOT3D_NRI
#include "nri_interop_internal.h"
#endif
#endif

namespace Fast::Extension {

Renderer::DiffuseGi::PassSettings ScreenSpaceGiPassSettings(const GlobalIlluminationSettings& settings) {
    Renderer::DiffuseGi::PassSettings s = Renderer::DiffuseGi::DefaultPassSettings();
    const ScreenSpaceGiSettings& ss = settings.ScreenSpace;
    s.Gather.Radius = ss.Radius;
    s.Gather.Thickness = ss.Thickness;
    s.Gather.Sections = ss.Sections;
    s.Gather.Sectors = ss.Sectors;
    s.Gather.CandidatesPerSide = ss.RadialCandidates;
    // Metric radial spacing exponent 1.5 (M6: 1; M4: 2). Against the
    // EveryTexel truth of two captured sequences it removes most of the J
    // overshoot of exponent 1 (+2 % and +8 % -> -0.6 % and +1 %) and lowers
    // the J-weighted error at the same cost (docs, "M7").
    s.Gather.RadialExponent = 1.5;
    s.Temporal.Enabled = ss.Temporal;
    // FlowAndDepth: no surface takes the weaker moving-surface test (depth
    // band and normal only). On the captured sequences that test accepted
    // histories of other surfaces (moving receivers 8-12 % off their converged
    // value); every history tap now passes the camera-predicted surface test
    // (docs, "M7", open point 16).
    s.Temporal.MovingFlow = std::numeric_limits<double>::infinity();
    s.Filter.Levels = ss.FilterLevels;
    // M8: smooth-shaded receivers. The renderer's normal guide is the normal
    // the per-pixel lighting shades with; the GI lobe and the continuity of
    // every stage follow it (docs, "M8").
    Renderer::DiffuseGi::SetPassShadingNormals(s, true);
    // Highlight clamp (section 9): artistic, never in validation mode.
    if (ss.HighlightClamp > 0.0F && !ss.Validation) s.OutputLimit = ss.HighlightClamp;
    return s;
}

uint32_t ScreenSpaceGiWorkFactor(GlobalIlluminationResolution resolution) noexcept {
    return resolution == GlobalIlluminationResolution::Quarter ? 4U : 2U;
}

uint64_t ScreenSpaceGiSurfaceBytes(uint32_t width, uint32_t height, const GlobalIlluminationSettings& settings) {
    const auto extent =
        Renderer::DiffuseGi::MakeWorkScaleExtent(width, height, ScreenSpaceGiWorkFactor(settings.ScreenSpace.WorkScale));
    const auto plan = Renderer::DiffuseGi::PlanPassResources(extent, ScreenSpaceGiPassSettings(settings), {}, 256U);
    // The pass buffers (output guide included) and the RGBA16F output image.
    return plan.PassBytes() + 8ULL * width * height;
}

} // namespace Fast::Extension

#ifdef ENABLE_OOT3D_VULKAN
namespace Fast::Extension {

namespace DGI = Renderer::DiffuseGi;

struct ScreenSpaceGiProvider::Instance {
    ScreenSpaceGiSurface Surface;
    DGI::DiffuseGiPass Pass;
    VkDescriptorPool Pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> AdapterSets; // one per frame slot
    DGI::PassExtent Extent;
    uint64_t LastFrameId = 0;
    bool HasFrame = false;
    uint64_t LastUse = 0;
};

// Developer dump of one frame's pass inputs (see the header).
struct ScreenSpaceGiProvider::DumpState {
    std::string Directory;
    uint64_t Frame = 300;
    uint32_t Count = 1;  // consecutive frames (OOT3D_GRAPHICS_SSGI_DUMP_COUNT)
    uint32_t Recorded = 0;
    struct Part {
        const char* Name;
        DGI::PassResourceId Id;
        VkDeviceSize Offset, Size;
    };
    // One recorded frame: its host copy and the declarations of its frame.
    struct Record {
        uint64_t FrameId = 0;
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        void* Mapped = nullptr;
        std::vector<Part> Parts;
        DGI::PassExtent Extent;
        DGI::DepthCamera Camera;
        DGI::RigidTransform CurrentToPrevious;
        bool CameraCut = false;
        std::array<float, 2> Jitter{};
        uint32_t GuideWidth = 0, GuideHeight = 0;
    };
    std::vector<Record> Pending;
};

ScreenSpaceGiProvider::ScreenSpaceGiProvider() = default;
ScreenSpaceGiProvider::~ScreenSpaceGiProvider() { Shutdown(); }

bool ScreenSpaceGiProvider::Supported(VkPhysicalDevice physical, std::string& reason) {
    return DGI::DiffuseGiPassSupported(physical, reason);
}

void ScreenSpaceGiProvider::Initialize(VkPhysicalDevice physical, VkDevice device, NriInteropContext& interop,
                                       uint32_t framesInFlight) {
    Shutdown();
    mPhysical = physical;
    mDevice = device;
    mInterop = &interop;
    mFramesInFlight = std::max(framesInFlight, 1U);
    mReason = "pipelines not created";
    const auto env = [](const char* name) -> std::string_view {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string_view(value) : std::string_view();
    };
    mKernelTiming = env("OOT3D_GRAPHICS_SSGI_KERNEL_TIMING") == "1";
    // A/B against the M7 receivers (face-normal lobe and edge tests).
    mFaceReceivers = env("OOT3D_GRAPHICS_SSGI_RECEIVERS") == "face";
    const std::string_view factor = env("OOT3D_GRAPHICS_SSGI_WORK_FACTOR");
    mWorkFactorOverride = factor == "1" ? 1U : factor == "2" ? 2U : factor == "4" ? 4U : 0U;
    if (const std::string_view dump = env("OOT3D_GRAPHICS_SSGI_DUMP"); !dump.empty()) {
        mDump = std::make_unique<DumpState>();
        mDump->Directory = std::string(dump);
        if (const std::string_view frame = env("OOT3D_GRAPHICS_SSGI_DUMP_FRAME"); !frame.empty())
            mDump->Frame = std::strtoull(std::string(frame).c_str(), nullptr, 10);
        if (const std::string_view count = env("OOT3D_GRAPHICS_SSGI_DUMP_COUNT"); !count.empty())
            mDump->Count = std::max(1U, static_cast<uint32_t>(std::strtoul(std::string(count).c_str(), nullptr, 10)));
    }
    if (mKernelTiming || mWorkFactorOverride != 0U || mDump || mFaceReceivers)
        std::fprintf(stderr,
                     "TRIAEVUM_SCREEN_SPACE_GI developer kernel_timing=%d work_factor=%u dump=%s frame=%llu "
                     "receivers=%s\n",
                     mKernelTiming ? 1 : 0, mWorkFactorOverride, mDump ? mDump->Directory.c_str() : "-",
                     static_cast<unsigned long long>(mDump ? mDump->Frame : 0U), mFaceReceivers ? "face" : "shading");
}

bool ScreenSpaceGiProvider::Prepare() {
    if (mReady) return true;
    if (mFailed || mInterop == nullptr) return false;
    if (!mStarted) {
        mStarted = true;
        const auto start = std::chrono::steady_clock::now();
        std::string reason;
        if (!Supported(mPhysical, reason)) {
            mFailed = true;
            mReason = reason;
            return false;
        }
        // SPIR-V from the precompiled pack on this thread (the pass shader
        // compiler is not shared with the worker); pipelines on the worker.
        DGI::PassKernelBinaries binaries = DGI::DiffuseGiKernels::Resolve(mInterop->Shaders(), reason);
        std::vector<uint32_t> adapter;
        if (reason.empty()) {
            try {
                adapter = mInterop->Shaders().Resolve(BuildDiffuseGiAdapterShader(), Renderer::SpirvStage::Compute,
                                                      "oot3d_diffuse_gi_inputs.comp");
            } catch (const std::exception& error) {
                reason = error.what();
            }
        }
        if (!reason.empty()) {
            mFailed = true;
            mReason = "kernels unavailable: " + reason;
            return false;
        }
        mKernels = std::make_shared<DGI::DiffuseGiKernels>();
        mAdapter = std::make_shared<DiffuseGiAdapterKernel>();
        const DGI::PassDevice device{mPhysical, mDevice, VK_NULL_HANDLE};
        mCreation = std::async(std::launch::async, [kernels = mKernels, adapterKernel = mAdapter, device,
                                                    binaries = std::move(binaries), spirv = std::move(adapter)] {
            return kernels->Create(device, binaries) && adapterKernel->Create(device.Device, spirv);
        });
        mReason = "creating pipelines";
        // The frame's own share: resolving the pack modules and launching the worker.
        mStats.FrameMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    if (mCreation.valid() && mCreation.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const bool created = mCreation.get();
        if (!created) {
            mFailed = true;
            mReason = "pipeline creation failed: " +
                      (mKernels->Complete() ? std::string("adapter kernel") : mKernels->Reason());
            return false;
        }
        mReady = true;
        mReason.clear();
        mStats.PipelinesReady = true;
        mStats.PipelineMilliseconds = mKernels->CreationMilliseconds() + mAdapter->CreationMilliseconds();
        std::fprintf(stderr,
                     "TRIAEVUM_SCREEN_SPACE_GI pipelines_ready=1 worker_ms=%.1f gather_ms=%.1f upsample_ms=%.1f "
                     "adapter_ms=%.1f frame_ms=%.2f\n",
                     mStats.PipelineMilliseconds,
                     mKernels->KernelMilliseconds()[static_cast<size_t>(DGI::PassKernel::Gather)],
                     mKernels->KernelMilliseconds()[static_cast<size_t>(DGI::PassKernel::Upsample)],
                     mAdapter->CreationMilliseconds(), mStats.FrameMilliseconds);
    }
    return mReady;
}

ScreenSpaceGiProvider::Instance* ScreenSpaceGiProvider::InstanceFor(const ScreenSpaceGiFrame& frame) {
    for (auto& instance : mInstances)
        if (instance->Surface == frame.Surface) {
            instance->LastUse = ++mUseCounter;
            return instance.get();
        }
    if (mInstances.size() >= kMaxSurfaces) {
        // Retire the least recently used surface not composed in this frame.
        auto victim = mInstances.end();
        for (auto it = mInstances.begin(); it != mInstances.end(); ++it)
            if (!((*it)->HasFrame && (*it)->LastFrameId == frame.FrameId) &&
                (victim == mInstances.end() || (*it)->LastUse < (*victim)->LastUse))
                victim = it;
        if (victim == mInstances.end() || !mInterop->CompleteRecordedWork()) return nullptr;
        (*victim)->Pass.Shutdown();
        if ((*victim)->Pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mDevice, (*victim)->Pool, nullptr);
        mInstances.erase(victim);
        ++mStats.InstancesRetired;
    }
    auto instance = std::make_unique<Instance>();
    instance->Surface = frame.Surface;
    instance->LastUse = ++mUseCounter;
    if (!instance->Pass.Initialize(DGI::PassDevice{mPhysical, mDevice, VK_NULL_HANDLE}, mKernels)) {
        mReason = instance->Pass.UnavailableReason();
        return nullptr;
    }
    const std::array<VkDescriptorPoolSize, 2> sizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 5U * mFramesInFlight},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8U * mFramesInFlight},
    }};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = mFramesInFlight;
    pool.poolSizeCount = static_cast<uint32_t>(sizes.size());
    pool.pPoolSizes = sizes.data();
    if (vkCreateDescriptorPool(mDevice, &pool, nullptr, &instance->Pool) != VK_SUCCESS) {
        mReason = "adapter descriptor pool";
        return nullptr;
    }
    instance->AdapterSets.resize(mFramesInFlight, VK_NULL_HANDLE);
    const std::vector<VkDescriptorSetLayout> layouts(mFramesInFlight, mAdapter->SetLayout());
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = instance->Pool;
    allocation.descriptorSetCount = mFramesInFlight;
    allocation.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(mDevice, &allocation, instance->AdapterSets.data()) != VK_SUCCESS) {
        vkDestroyDescriptorPool(mDevice, instance->Pool, nullptr);
        mReason = "adapter descriptor sets";
        return nullptr;
    }
    instance->Pass.SetKernelTiming(mKernelTiming);
    mInstances.push_back(std::move(instance));
    ++mStats.InstancesCreated;
    std::fprintf(stderr, "TRIAEVUM_SCREEN_SPACE_GI frame=%llu instances_created=%llu retired=%llu\n",
                 static_cast<unsigned long long>(frame.FrameId), static_cast<unsigned long long>(mStats.InstancesCreated),
                 static_cast<unsigned long long>(mStats.InstancesRetired));
    return mInstances.back().get();
}

bool ScreenSpaceGiProvider::ConfigureInstance(Instance& instance, const ScreenSpaceGiFrame& frame,
                                              const GlobalIlluminationSettings& settings,
                                              const NriEffectGraphTransientImageBinding& output) {
    const DGI::PassExtent extent = DGI::MakeWorkScaleExtent(
        frame.Width, frame.Height,
        mWorkFactorOverride != 0U ? mWorkFactorOverride : ScreenSpaceGiWorkFactor(settings.ScreenSpace.WorkScale));
    DGI::PassSettings passSettings = ScreenSpaceGiPassSettings(settings);
    if (mFaceReceivers) DGI::SetPassShadingNormals(passSettings, false);
    DGI::PassOutputs outputs;
    outputs.Illumination = {output.Texture.Image, output.Texture.StorageView, output.Format, output.Width,
                            output.Height};
    const auto start = std::chrono::steady_clock::now();
    const bool configured = instance.Pass.Available();
    const bool replaces = configured && instance.Pass.ReplacesResources(extent, passSettings, {}, outputs);
    const bool allocates = !configured || replaces;
    if (replaces) {
        // Resources that recorded commands may still use are replaced only
        // after that work has executed.
        if (!mInterop->CompleteRecordedWork()) {
            mReason = "recorded work did not complete";
            return false;
        }
        instance.HasFrame = false;
        ++mStats.Reconfigurations;
    }
    if (!instance.Pass.Configure(extent, passSettings, {}, outputs)) {
        mReason = instance.Pass.UnavailableReason();
        return false;
    }
    if (allocates)
        mStats.AllocationMilliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    instance.Extent = extent;
    return true;
}

bool ScreenSpaceGiProvider::Transition(uint32_t frameSlot, const NriEffectGraphTransientImageBinding& output,
                                       ResourceAccess access) {
#ifdef ENABLE_OOT3D_NRI
    const ResourceTransition transition =
        output.StateTracker->PlanTransition(reinterpret_cast<uintptr_t>(output.Texture.Image), {access, 0});
    if (!NriInteropAccess::CmdTextureBarrier(*mInterop, frameSlot, output.Texture.Image, transition)) return false;
    mBarriers.RecordGraphTransition(transition);
    output.StateTracker->Commit(transition);
    return true;
#else
    (void)frameSlot;
    (void)output;
    (void)access;
    return false;
#endif
}

bool ScreenSpaceGiProvider::Execute(VkCommandBuffer command, const ScreenSpaceGiFrame& frame,
                                    const GlobalIlluminationSettings& settings,
                                    const NriEffectGraphTransientImageBinding& output,
                                    const EffectPassBarrierPlan& barriers) {
    mBarriers = barriers.BeginExecution();
    if (!Prepare()) return false;
    FinishDump(frame.FrameId);
    const auto* declared = barriers.Find(EffectResource::DiffuseIllumination);
    if (declared == nullptr || !declared->Managed() || !output.Valid() || output.StateTracker == nullptr ||
        output.Texture.StorageView == VK_NULL_HANDLE || frame.View == nullptr || frame.Temporal == nullptr) {
        mReason = "GlobalIllumination output or view undeclared";
        return false;
    }
    Instance* instance = InstanceFor(frame);
    if (instance == nullptr || !ConfigureInstance(*instance, frame, settings, output)) return false;
    const bool republish = instance->HasFrame && instance->LastFrameId == frame.FrameId;
    if (!Transition(frame.FrameSlot, output, declared->DispatchAccess)) {
        mReason = "output transition failed";
        return false;
    }
    DGI::PassImageState illumination, accessibility;
    DGI::PassOutputAccess access;
    access.ExternalTransitions = true;
    DGI::PassBarrierRecord record;
    bool executed = false;
    if (republish) {
        executed = instance->Pass.RepublishOutputs(command, illumination, accessibility, access, record);
        if (executed) ++mStats.Republications;
    } else {
        const DGI::DepthCamera camera =
            DiffuseGiGuideCamera(*frame.View, frame.Coordinates, frame.WBuffer, frame.JitterUv);
        const bool shading = instance->Pass.Buffer(DGI::PassResourceId::ShadingNormal).Buffer != VK_NULL_HANDLE;
        const DiffuseGiAdapterParams params = MakeDiffuseGiAdapterParams(instance->Extent, camera, shading);
        DGI::PassFrame passFrame;
        passFrame.Camera = camera;
        passFrame.NormalSource = DGI::GeometricNormalSource::RendererFaceNormal;
        passFrame.FrameIndex = frame.FrameId;
        bool cut = !frame.Temporal->HistoryValid || frame.Temporal->CameraCut;
        if (!cut) {
            if (const auto transform = DiffuseGiCurrentToPrevious(frame.View->Projection, *frame.Temporal))
                passFrame.CurrentToPrevious = *transform;
            else {
                cut = true;
                ++mStats.CameraTransformFallbacks;
            }
        }
        passFrame.CameraCut = cut;
        passFrame.JitterX = frame.JitterUv[0] * static_cast<double>(frame.Width);
        passFrame.JitterY = frame.JitterUv[1] * static_cast<double>(frame.Height);
        passFrame.Motion = {DGI::MotionDirection::CurrentToPrevious, DGI::MotionUnits::NormalizedViewportUv,
                            DGI::JitterInMotion::Included};
        passFrame.Validation = DGI::HistoryValidation::FlowAndDepth;
        const VkDescriptorSet set = instance->AdapterSets[frame.FrameSlot % instance->AdapterSets.size()];
        executed = RecordDiffuseGiAdapter(mDevice, command, *mAdapter, set, frame.Guides, instance->Pass, params) &&
                   instance->Pass.Execute(command, passFrame, illumination, accessibility, access, record);
        if (executed && mDump && mDump->Recorded < mDump->Count && frame.FrameId >= mDump->Frame)
            RecordDump(command, *instance, frame, passFrame);
        if (executed && mKernelTiming && instance->Pass.Stats().Executions % 60U == 0U) {
            const auto& t = instance->Pass.LastKernelTimings();
            if (t.Valid)
                std::fprintf(stderr,
                             "TRIAEVUM_SCREEN_SPACE_GI kernel_ms frame=%llu total=%.3f normal=%.3f level0=%.3f "
                             "reduce=%.3f gather=%.3f pack=%.3f temporal=%.3f filter=%.3f output=%.3f upsample=%.3f\n",
                             static_cast<unsigned long long>(frame.FrameId), t.Total, t.Milliseconds[0],
                             t.Milliseconds[1], t.Milliseconds[2], t.Milliseconds[3], t.Milliseconds[4],
                             t.Milliseconds[5], t.Milliseconds[6], t.Milliseconds[7], t.Milliseconds[8]);
        }
        if (executed) {
            instance->LastFrameId = frame.FrameId;
            instance->HasFrame = true;
            ++mStats.Executions;
            mStats.LastResetReasons = instance->Pass.Stats().LastResetReasons;
            if (mStats.LastResetReasons != 0U) ++mStats.ResetFrames;
        } else {
            mReason = instance->Pass.UnavailableReason().empty() ? "adapter inputs unavailable"
                                                                 : instance->Pass.UnavailableReason();
        }
    }
    // The pass's private barriers, counted with the graph's.
    for (uint32_t k = 0; k < record.PrivateBarriers; ++k)
        mBarriers.RecordPrivateTransition({0U, {ResourceAccess::ComputeWrite, 0U}, {ResourceAccess::ShaderRead, 0U}});
    const bool completed = Transition(frame.FrameSlot, output, declared->CompletionAccess);
    UpdateStats();
    return executed && completed;
}

bool ScreenSpaceGiProvider::ClearOutput(VkCommandBuffer command, const ScreenSpaceGiFrame& frame,
                                        const GlobalIlluminationSettings& settings,
                                        const NriEffectGraphTransientImageBinding& output,
                                        const EffectPassBarrierPlan& barriers) {
    mBarriers = barriers.BeginExecution();
    if (!mReady) return false;
    const auto* declared = barriers.Find(EffectResource::DiffuseIllumination);
    if (declared == nullptr || !declared->Managed() || !output.Valid() || output.StateTracker == nullptr ||
        output.Texture.StorageView == VK_NULL_HANDLE)
        return false;
    Instance* instance = InstanceFor(frame);
    if (instance == nullptr || !ConfigureInstance(*instance, frame, settings, output)) return false;
    if (!Transition(frame.FrameSlot, output, declared->DispatchAccess)) return false;
    DGI::PassImageState illumination, accessibility;
    DGI::PassOutputAccess access;
    access.ExternalTransitions = true;
    DGI::PassBarrierRecord record;
    const bool cleared = instance->Pass.ClearOutputs(command, illumination, accessibility, access, record);
    instance->HasFrame = false;
    for (uint32_t k = 0; k < record.PrivateBarriers; ++k)
        mBarriers.RecordPrivateTransition({0U, {ResourceAccess::ComputeWrite, 0U}, {ResourceAccess::ShaderRead, 0U}});
    const bool completed = Transition(frame.FrameSlot, output, declared->CompletionAccess);
    if (cleared) ++mStats.Clears;
    UpdateStats();
    return cleared && completed;
}

void ScreenSpaceGiProvider::RecordDump(VkCommandBuffer command, Instance& instance, const ScreenSpaceGiFrame& frame,
                                       const DGI::PassFrame& passFrame) {
    DumpState& d = *mDump;
    ++d.Recorded;
    DumpState::Record r;
    VkDeviceSize total = 0;
    for (const auto& [name, id] : std::initializer_list<std::pair<const char*, DGI::PassResourceId>>{
             {"depth", DGI::PassResourceId::Depth},       {"class", DGI::PassResourceId::Class},
             {"radiance", DGI::PassResourceId::Radiance}, {"normal", DGI::PassResourceId::Normal},
             {"motion", DGI::PassResourceId::Motion},     {"shading", DGI::PassResourceId::ShadingNormal}}) {
        const auto view = instance.Pass.Buffer(id);
        if (view.Size == 0U) continue;
        r.Parts.push_back({name, id, total, view.Size});
        total += (view.Size + 255U) / 256U * 256U;
    }
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = total;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(mDevice, &info, nullptr, &r.Buffer) != VK_SUCCESS) return;
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(mDevice, r.Buffer, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(mPhysical, &memory);
    uint32_t type = memory.memoryTypeCount;
    const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t t = 0; t < memory.memoryTypeCount && type == memory.memoryTypeCount; ++t)
        if ((requirements.memoryTypeBits & (1U << t)) && (memory.memoryTypes[t].propertyFlags & wanted) == wanted)
            type = t;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (type == memory.memoryTypeCount || vkAllocateMemory(mDevice, &allocation, nullptr, &r.Memory) != VK_SUCCESS ||
        vkBindBufferMemory(mDevice, r.Buffer, r.Memory, 0) != VK_SUCCESS ||
        vkMapMemory(mDevice, r.Memory, 0, VK_WHOLE_SIZE, 0, &r.Mapped) != VK_SUCCESS) {
        std::fprintf(stderr, "TRIAEVUM_SCREEN_SPACE_GI dump failed: host memory\n");
        if (r.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, r.Buffer, nullptr);
        if (r.Memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, r.Memory, nullptr);
        return;
    }
    // The adapter's writes (and the pass's reads) -> these copies.
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier,
                         0, nullptr, 0, nullptr);
    for (const auto& part : r.Parts) {
        const VkBufferCopy region{0, part.Offset, part.Size};
        vkCmdCopyBuffer(command, instance.Pass.Buffer(part.Id).Buffer, r.Buffer, 1, &region);
    }
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr,
                         0, nullptr);
    r.FrameId = frame.FrameId;
    r.Extent = instance.Extent;
    r.Camera = passFrame.Camera;
    r.CurrentToPrevious = passFrame.CurrentToPrevious;
    r.CameraCut = passFrame.CameraCut;
    r.Jitter = frame.JitterUv;
    r.GuideWidth = frame.Width;
    r.GuideHeight = frame.Height;
    d.Pending.push_back(std::move(r));
}

void ScreenSpaceGiProvider::FinishDump(uint64_t frameId) {
    if (!mDump) return;
    DumpState& d = *mDump;
    for (auto it = d.Pending.begin(); it != d.Pending.end();) {
        DumpState::Record& r = *it;
        // Every frame slot has been recorded again since: the copy has executed.
        if (frameId < r.FrameId + mFramesInFlight + 1U) {
            ++it;
            continue;
        }
        // One directory per frame when several are dumped.
        const std::string directory =
            d.Count > 1U ? d.Directory + "/f" + std::to_string(r.FrameId) : d.Directory;
        std::filesystem::create_directories(directory);
        bool ok = true;
        for (const auto& part : r.Parts) {
            std::ofstream file(directory + "/ssgi_" + part.Name + ".bin", std::ios::binary);
            file.write(static_cast<const char*>(r.Mapped) + part.Offset, static_cast<std::streamsize>(part.Size));
            ok = ok && file.good();
        }
        std::ofstream meta(directory + "/ssgi_dump.json");
        char text[2048];
        const auto& R = r.CurrentToPrevious.R;
        const auto& T = r.CurrentToPrevious.T;
        std::snprintf(text, sizeof(text),
                      "{\n  \"frame\": %llu,\n  \"guide\": [%u, %u],\n  \"buffer\": [%u, %u],\n  \"work_factor\": %u,\n"
                      "  \"work_phase\": %u,\n  \"encoding\": %d,\n  \"near\": %.9g,\n  \"far\": %.9g,\n"
                      "  \"view_x\": [%.17g, %.17g, %.17g],\n  \"view_y\": [%.17g, %.17g, %.17g],\n"
                      "  \"jitter_uv\": [%.9g, %.9g],\n  \"camera_cut\": %d,\n"
                      "  \"current_to_previous_r\": [%.17g, %.17g, %.17g, %.17g, %.17g, %.17g, %.17g, %.17g, %.17g],\n"
                      "  \"current_to_previous_t\": [%.17g, %.17g, %.17g]\n}\n",
                      static_cast<unsigned long long>(r.FrameId), r.GuideWidth, r.GuideHeight, r.Extent.BufferWidth,
                      r.Extent.BufferHeight, r.Extent.WorkFactor, r.Extent.WorkPhase,
                      static_cast<int>(r.Camera.Encoding), r.Camera.Near, r.Camera.Far, r.Camera.ViewX[0],
                      r.Camera.ViewX[1], r.Camera.ViewX[2], r.Camera.ViewY[0], r.Camera.ViewY[1], r.Camera.ViewY[2],
                      r.Jitter[0], r.Jitter[1], r.CameraCut ? 1 : 0, R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7],
                      R[8], T[0], T[1], T[2]);
        meta << text;
        ok = ok && meta.good();
        std::fprintf(stderr, "TRIAEVUM_SCREEN_SPACE_GI dump frame=%llu work=%ux%u factor=%u written=%d dir=%s\n",
                     static_cast<unsigned long long>(r.FrameId), r.Extent.BufferWidth, r.Extent.BufferHeight,
                     r.Extent.WorkFactor, ok ? 1 : 0, directory.c_str());
        vkUnmapMemory(mDevice, r.Memory);
        vkDestroyBuffer(mDevice, r.Buffer, nullptr);
        vkFreeMemory(mDevice, r.Memory, nullptr);
        it = d.Pending.erase(it);
    }
}

VkImageView ScreenSpaceGiProvider::SampledDepthView(VkImage image, VkFormat format, uint32_t width,
                                                    uint32_t height) {
#ifdef ENABLE_OOT3D_NRI
    if (mInterop == nullptr || image == VK_NULL_HANDLE ||
        !mInterop->WrapTexture(image, format, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT, width, height))
        return VK_NULL_HANDLE;
    nri::Descriptor* view = NriInteropAccess::TextureView(*mInterop, image, false);
    nri::CoreInterface* core = NriInteropAccess::Core(*mInterop);
    if (view == nullptr || core == nullptr) return VK_NULL_HANDLE;
    return reinterpret_cast<VkImageView>(core->GetDescriptorNativeObject(view));
#else
    (void)image;
    (void)format;
    (void)width;
    (void)height;
    return VK_NULL_HANDLE;
#endif
}

void ScreenSpaceGiProvider::ResetHistory() noexcept {
    for (auto& instance : mInstances) {
        instance->Pass.ResetHistory();
        instance->HasFrame = false;
    }
    ++mStats.HistoryResets;
}

void ScreenSpaceGiProvider::InvalidateScreenResources() noexcept {
    for (auto& instance : mInstances) {
        instance->Pass.Shutdown();
        if (instance->Pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(mDevice, instance->Pool, nullptr);
        ++mStats.InstancesRetired;
    }
    mInstances.clear();
    UpdateStats();
}

void ScreenSpaceGiProvider::Shutdown() noexcept {
    if (mCreation.valid()) mCreation.wait();
    mCreation = {};
    if (mDevice != VK_NULL_HANDLE) InvalidateScreenResources();
    mInstances.clear();
    if (mDump && mDevice != VK_NULL_HANDLE)
        for (auto& r : mDump->Pending) {
            if (r.Mapped != nullptr) vkUnmapMemory(mDevice, r.Memory);
            if (r.Buffer != VK_NULL_HANDLE) vkDestroyBuffer(mDevice, r.Buffer, nullptr);
            if (r.Memory != VK_NULL_HANDLE) vkFreeMemory(mDevice, r.Memory, nullptr);
        }
    mDump.reset();
    mAdapter.reset();
    mKernels.reset();
    mInterop = nullptr;
    mStarted = mReady = mFailed = false;
    mStats.PipelinesReady = false;
    mReason = "not initialized";
}

void ScreenSpaceGiProvider::UpdateStats() noexcept {
    mStats.Instances = static_cast<uint32_t>(mInstances.size());
    mStats.BufferBytes = 0;
    for (const auto& instance : mInstances) mStats.BufferBytes += instance->Pass.Stats().BufferBytes;
}

} // namespace Fast::Extension
#endif
