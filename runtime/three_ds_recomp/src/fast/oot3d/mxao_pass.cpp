// TriAevum's side of the iMMERSE MXAO provider: loads the separately licensed
// module (triaevum/immerse_module.h) and serves it the renderer's NRI device,
// textures, programs and barriers. MXAO itself, its parameter mapping and its
// programs are in the module (fast/immerse, under its own licence); without
// the module's library MXAO is unavailable.
#include "fast/oot3d/mxao_pass.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/oot3d/immerse_module_loader.h"
#include "fast/oot3d/nri_effect_graph_transient_image_arena.h"
#include "fast/oot3d/nri_interop_context.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/temporal_history_manager.h"
#include "fast/renderer/pass_shader_artifact.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#ifdef ENABLE_OOT3D_NRI
#include "fast/oot3d/nri_interop_internal.h"
#endif
#include <algorithm>
#include <map>
#include <span>
#include <vector>

namespace Fast::Oot3d {
namespace {
// The pass's state, which the module's host table reaches as its context.
struct MxaoHostState {
    VkPhysicalDevice Physical = VK_NULL_HANDLE;
    NriInteropContext* Interop = nullptr;
    const TriaevumImmerseModule* Module = nullptr;
    TriaevumImmerseMxao* Instance = nullptr;
    TriaevumImmerseHost Host{};
    std::string GeometryGlsl;
    std::string DebugGlsl;
    // The module's SPIR-V packs, read once (the module stays loaded).
    std::map<const void*, std::pair<std::vector<Renderer::SpirvPackEntry>, std::vector<Renderer::PassShaderArtifact>>>
        Packs;
    std::vector<uint32_t> Spirv;
    NriOwnedTexture2D Output;
    ResourceStateTracker PrivateStates;
    ResourceStateTracker* OutputStates = nullptr;
    const EffectPassBarrierPlan* Barriers = nullptr;
    const EffectPassOutputBarrier* OutputBarrier = nullptr;
    EffectPassBarrierExecution LastBarriers;
    bool LastHistoryValid = false;
    std::string Reason = "iMMERSE MXAO is not initialized";
};
} // namespace

struct MxaoPass::Impl : MxaoHostState {};

#ifdef ENABLE_OOT3D_NRI
namespace {
MxaoHostState& Self(void* context) {
    return *static_cast<MxaoHostState*>(context);
}

TriaevumImmerseHost MakeHost(MxaoHostState& impl) {
    TriaevumImmerseHost host{};
    host.AbiVersion = TRIAEVUM_IMMERSE_ABI_VERSION;
    host.Context = static_cast<MxaoHostState*>(&impl);
    host.NriCore = NriInteropAccess::Core(*impl.Interop);
    host.NriDevice = NriInteropAccess::Device(*impl.Interop);
    host.NriPipelineCache = NriInteropAccess::PipelineCache(*impl.Interop);
    host.GeometryGuideGlsl = impl.GeometryGlsl.c_str();
    host.DebugGlsl = impl.DebugGlsl.c_str();
    host.SupportsSampledStorage = [](void* context, VkFormat format) -> int {
        constexpr VkFormatFeatureFlags required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(Self(context).Physical, format, &properties);
        return (properties.optimalTilingFeatures & required) == required;
    };
    host.ComputeSpirv = [](void* context, const void* pack, size_t packBytes, const char* source, const char* name,
                           size_t* words) -> const uint32_t* {
        auto& impl = Self(context);
        *words = 0;
        try {
            auto found = impl.Packs.find(pack);
            if (found == impl.Packs.end()) {
                auto entries =
                    Renderer::ReadSpirvPack({ static_cast<const unsigned char*>(pack), packBytes });
                auto artifacts = Renderer::PassShaderArtifactsFromPack(entries);
                found = impl.Packs.emplace(pack, std::make_pair(std::move(entries), std::move(artifacts))).first;
            }
            const auto builtin =
                Renderer::FindPassShaderArtifact(found->second.second, source, Renderer::SpirvStage::Compute, {});
            impl.Spirv = !builtin.empty() ? std::vector<uint32_t>(builtin.begin(), builtin.end())
                                          : impl.Interop->Shaders().Resolve(source, Renderer::SpirvStage::Compute, name);
        } catch (const std::exception&) {
            return nullptr;
        }
        *words = impl.Spirv.size();
        return impl.Spirv.data();
    };
    host.CreateTexture = [](void* context, uint32_t width, uint32_t height, VkFormat format, VkImage* image) -> int {
        NriOwnedTexture2D texture;
        if (!Self(context).Interop->CreateOwnedTexture2D(width, height, format, texture))
            return 0;
        *image = texture.Image;
        return 1;
    };
    host.DestroyTexture = [](void* context, VkImage image) { Self(context).Interop->DestroyOwnedTexture(image); };
    host.WrapTexture = [](void* context, VkImage image, VkFormat format, uint32_t width, uint32_t height) -> int {
        return Self(context).Interop->WrapTexture(image, format, VK_IMAGE_TYPE_2D, VK_IMAGE_USAGE_SAMPLED_BIT, width,
                                                  height);
    };
    host.TextureView = [](void* context, VkImage image, int storage) -> void* {
        return NriInteropAccess::TextureView(*Self(context).Interop, image, storage != 0);
    };
    host.CompleteRecordedWork = [](void* context) -> int { return Self(context).Interop->CompleteRecordedWork(); };
    host.CommandBuffer = [](void* context, uint32_t frameSlot) -> void* {
        return NriInteropAccess::CommandBuffer(*Self(context).Interop, frameSlot);
    };
    host.BeginOutput = [](void* context) {
        auto& impl = Self(context);
        impl.LastBarriers = impl.Barriers->BeginOutputExecution(EffectResource::AmbientOcclusion);
    };
    host.Transition = [](void* context, uint32_t frameSlot, VkImage image, uint32_t state) -> int {
        auto& impl = Self(context);
        const bool output = state == TRIAEVUM_IMMERSE_OUTPUT_DISPATCH || state == TRIAEVUM_IMMERSE_OUTPUT_COMPLETION;
        if (output && (impl.OutputStates == nullptr || impl.OutputBarrier == nullptr))
            return 0;
        auto& states = output ? *impl.OutputStates : impl.PrivateStates;
        const ResourceAccess access = state == TRIAEVUM_IMMERSE_OUTPUT_DISPATCH ? impl.OutputBarrier->DispatchAccess
                                      : state == TRIAEVUM_IMMERSE_OUTPUT_COMPLETION
                                          ? impl.OutputBarrier->CompletionAccess
                                      : state == TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE ? ResourceAccess::ComputeWrite
                                                                                       : ResourceAccess::ShaderRead;
        const auto change = states.PlanTransition(reinterpret_cast<uintptr_t>(image), { access, 0 });
        if (!NriInteropAccess::CmdTextureBarrier(*impl.Interop, frameSlot, image, change))
            return 0;
        states.Commit(change);
        if (output)
            impl.LastBarriers.RecordGraphTransition(change);
        else
            impl.LastBarriers.RecordPrivateTransition(change);
        return 1;
    };
    host.ClearPrivateStates = [](void* context) { Self(context).PrivateStates.Clear(); };
    return host;
}

TriaevumMxaoSettings ModuleSettings(const EffectsSettings& settings) {
    TriaevumMxaoSettings result{};
    result.Method = uint32_t(settings.MxaoMethod);
    result.Quality = settings.MxaoQuality;
    result.ShadingRate = settings.MxaoShadingRate;
    result.Normals = uint32_t(settings.MxaoNormals);
    result.Filter = settings.MxaoFilter;
    result.DebugView = uint32_t(settings.MxaoDebug);
    result.RadiusGrowsWithDistance = settings.MxaoRadiusGrowsWithDistance ? 1U : 0U;
    result.OriginalFade = settings.MxaoOriginalFade ? 1U : 0U;
    result.Relief = uint32_t(settings.AoRelief);
    result.Radius = settings.AoRadius;
    result.Strength = settings.AoStrength;
    result.FadeFrom = settings.AoFadeOutFrom;
    result.FadeTo = settings.AoFadeOutTo;
    result.ScreenRadius = settings.MxaoScreenRadius;
    result.FadeDistance = settings.MxaoFadeDistance;
    return result;
}
static_assert(uint32_t(MxaoDebugView::UnfilteredOcclusion) == TRIAEVUM_MXAO_DEBUG_UNFILTERED &&
              uint32_t(MxaoDebugView::Normals) == TRIAEVUM_MXAO_DEBUG_NORMALS &&
              uint32_t(MxaoDebugView::HistoryLength) == TRIAEVUM_MXAO_DEBUG_HISTORY_LENGTH &&
              uint32_t(MxaoDebugView::FaceDisagreement) + 1U == TRIAEVUM_MXAO_DEBUG_COUNT);
static_assert(uint32_t(MxaoNormalSource::Depth) == 0U && uint32_t(MxaoNormalSource::Geometry) == 2U);
} // namespace
#endif

MxaoPass::MxaoPass() : mImpl(std::make_unique<Impl>()) {
}
MxaoPass::~MxaoPass() {
    Shutdown();
}

bool MxaoPass::Initialize(VkPhysicalDevice physical, VkDevice, NriInteropContext& interop) {
    Shutdown();
#ifdef ENABLE_OOT3D_NRI
    mImpl->Physical = physical;
    mImpl->Interop = &interop;
    if (!interop.Available()) {
        mImpl->Reason = interop.UnavailableReason();
        return false;
    }
    mImpl->Module = ImmerseModule();
    if (mImpl->Module == nullptr) {
        mImpl->Reason = ImmerseModuleUnavailableReason();
        return false;
    }
    mImpl->GeometryGlsl = std::string(Renderer3ds::PicaGeometryGuideShaderLibrary());
    mImpl->DebugGlsl = std::string(AmbientOcclusionDebugShaderLibrary());
    mImpl->Host = MakeHost(*mImpl);
    char reason[256] = {};
    mImpl->Instance = mImpl->Module->CreateMxao(&mImpl->Host, reason, sizeof(reason));
    if (mImpl->Instance == nullptr) {
        mImpl->Reason = reason[0] != '\0' ? reason : "iMMERSE MXAO could not be created";
        return false;
    }
    mImpl->Reason.clear();
    return true;
#else
    (void)physical;
    (void)interop;
    mImpl->Reason = "NRI support was not compiled into this build";
    return false;
#endif
}

bool MxaoPass::Configure(const NriEffectGraphTransientImageBinding& output, VkImage depth, VkFormat depthFormat,
                         VkImage normal, VkFormat normalFormat, VkImage geometry, const MxaoMotionInput* motion) {
    constexpr auto usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    if (!Available() || !output.Valid() || output.Resource != EffectResource::AmbientOcclusion ||
        output.Format != VK_FORMAT_R32_SFLOAT || output.MipLevels != 1 || output.Layers != 1 ||
        output.Samples != VK_SAMPLE_COUNT_1_BIT || (output.Usage & usage) != usage ||
        output.Texture.SampledView == VK_NULL_HANDLE || output.Texture.StorageView == VK_NULL_HANDLE ||
        depth == VK_NULL_HANDLE || normal == VK_NULL_HANDLE || geometry == VK_NULL_HANDLE)
        return false;
    TriaevumMxaoTargets targets{};
    targets.Output = output.Texture.Image;
    targets.OutputIdentity = reinterpret_cast<uintptr_t>(output.StateTracker);
    targets.Width = output.Width;
    targets.Height = output.Height;
    targets.Depth = depth;
    targets.DepthFormat = depthFormat;
    targets.Normal = normal;
    targets.NormalFormat = normalFormat;
    targets.Geometry = geometry;
    if (motion != nullptr) {
        targets.Motion = motion->Image;
        targets.MotionFormat = motion->Format;
        targets.MotionWidth = motion->Width;
        targets.MotionHeight = motion->Height;
    }
    const bool configured = mImpl->Module->Configure(mImpl->Instance, &targets) != 0;
    mImpl->Reason = mImpl->Module->Reason(mImpl->Instance);
    // The output stays bound until the module replaces or releases it.
    if (configured) {
        mImpl->Output = output.Texture;
        mImpl->OutputStates = output.StateTracker;
    } else if (mImpl->Module->Output(mImpl->Instance) == VK_NULL_HANDLE) {
        mImpl->Output = {};
        mImpl->OutputStates = nullptr;
    }
    return configured;
}

bool MxaoPass::Execute(uint32_t frameSlot, uint64_t frameId, const PerspectiveViewState& view, bool wBuffer,
                       const EffectsSettings& settings, const EffectPassBarrierPlan& barriers,
                       PicaSurfaceCoordinates coordinates, const MxaoTemporalFrame* temporal) {
    mImpl->LastBarriers = {};
    mImpl->LastHistoryValid = false;
#ifdef ENABLE_OOT3D_NRI
    const auto* output = barriers.Find(EffectResource::AmbientOcclusion);
    if (!Available() || !barriers.Valid() || !output || !output->Managed() || !mImpl->OutputStates ||
        output->DispatchAccess != ResourceAccess::ComputeWrite ||
        output->CompletionAccess != ResourceAccess::ShaderRead || !output->DownstreamRead)
        return false;
    TriaevumMxaoFrame frame{};
    frame.FrameSlot = frameSlot;
    frame.FrameId = frameId;
    std::copy(view.Projection.begin(), view.Projection.end(), frame.Projection);
    frame.NearPlane = view.NearPlane;
    frame.FarPlane = view.FarPlane;
    frame.WBuffer = wBuffer ? 1U : 0U;
    const auto origin = coordinates.StorageToViewNdc({ 0, 0 });
    const auto x = coordinates.StorageToViewNdc({ 1, 0 });
    const auto y = coordinates.StorageToViewNdc({ 0, 1 });
    std::copy(origin.begin(), origin.end(), frame.NdcOrigin);
    std::copy(x.begin(), x.end(), frame.NdcX);
    std::copy(y.begin(), y.end(), frame.NdcY);
    frame.Settings = ModuleSettings(settings);
    if (temporal != nullptr && temporal->View != nullptr) {
        const auto& state = *temporal->View;
        frame.Temporal = 1U;
        frame.TemporalFrameId = temporal->FrameId;
        frame.ViewHistoryValid = state.HistoryValid ? 1U : 0U;
        frame.CameraCut = state.CameraCut ? 1U : 0U;
        std::copy(state.PreviousWorldToClip.begin(), state.PreviousWorldToClip.end(), frame.PreviousWorldToClip);
        std::copy(state.InverseCurrentWorldToClip.begin(), state.InverseCurrentWorldToClip.end(),
                  frame.InverseCurrentWorldToClip);
    }
    mImpl->Barriers = &barriers;
    mImpl->OutputBarrier = output;
    const bool executed = mImpl->Module->Execute(mImpl->Instance, &frame) != 0;
    mImpl->Barriers = nullptr;
    mImpl->OutputBarrier = nullptr;
    mImpl->LastHistoryValid = mImpl->Module->LastHistoryValid(mImpl->Instance) != 0;
    return executed;
#else
    (void)frameSlot, (void)frameId, (void)view, (void)wBuffer, (void)settings, (void)barriers, (void)coordinates,
        (void)temporal;
    return false;
#endif
}

void MxaoPass::InvalidateScreenResources() {
    if (mImpl->Instance != nullptr)
        mImpl->Module->InvalidateScreenResources(mImpl->Instance);
    mImpl->Output = {};
    mImpl->OutputStates = nullptr;
    mImpl->LastHistoryValid = false;
    mImpl->LastBarriers = {};
}

void MxaoPass::Shutdown() {
    if (mImpl->Instance != nullptr)
        mImpl->Module->DestroyMxao(mImpl->Instance);
    *mImpl = {};
}

bool MxaoPass::Available() const {
    return mImpl->Instance != nullptr;
}
VkImage MxaoPass::OutputImage() const {
    return mImpl->Output.Image;
}
VkImageView MxaoPass::OutputView() const {
    return mImpl->Output.SampledView;
}
bool MxaoPass::OutputOwnedByNri() const {
    return mImpl->Interop && mImpl->Interop->OwnsTexture(mImpl->Output.Image);
}
bool MxaoPass::LastHistoryValid() const {
    return mImpl->LastHistoryValid;
}
const EffectPassBarrierExecution& MxaoPass::LastBarrierExecution() const {
    return mImpl->LastBarriers;
}
const std::string& MxaoPass::UnavailableReason() const {
    return mImpl->Reason;
}

} // namespace Fast::Oot3d
#endif
