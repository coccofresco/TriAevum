// iMMERSE module of TriAevum (see LICENSE.md in this directory).
// iMMERSE MXAO, Copyright (c) Pascal Gilcher. All rights reserved.
// Not covered by TriAevum's GPL: this file stays under the original iMMERSE
// licence (LICENSE-iMMERSE.txt) and is used under the author's explicit
// permission, granted exclusively to TriAevum.
//
// The module's shared library: the NRI execution of MXAO and the texts of its
// F1 controls, behind the module interface triaevum/immerse_module.h. It
// reaches TriAevum only through that interface's host table.
#include "triaevum/immerse_module.h"
#include "mxao_pass_binaries.h"
#include "mxao_shader.h"
#include <NRI.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace Fast::Immerse;

// Descriptor sets, one per dispatch input/output pairing. The temporal sets
// come in pairs: the one that writes history image 0, then image 1.
enum : size_t {
    kDeinterleaveSet,
    kOcclusionSet,
    kFilterSet,
    kResolveRawSet,
    kResolveFilteredSet,
    kTemporalSet,
    kFilterAccumulatedSet = kTemporalSet + 2,
    kResolveAccumulatedSet = kFilterAccumulatedSet + 2,
    kSetCount = kResolveAccumulatedSet + 2
};
enum : size_t {
    kDeinterleaveProgram,
    kOcclusionProgram,
    kFilterProgram,
    kResolveProgram,
    kTemporalProgram,
    kDebugProgram,
    kProgramCount
};

struct MotionInput {
    VkImage Image = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    uint32_t Width = 0U;
    uint32_t Height = 0U;
    bool operator==(const MotionInput&) const = default;
};
} // namespace

struct TriaevumImmerseMxao {
    const TriaevumImmerseHost* Host = nullptr;
    nri::CoreInterface* Core = nullptr;
    nri::Device* Device = nullptr;
    nri::PipelineLayout* Layout = nullptr;
    std::array<nri::Pipeline*, kProgramCount> Pipelines{};
    nri::Descriptor* Sampler = nullptr;
    nri::DescriptorPool* Pool = nullptr;
    std::array<nri::DescriptorSet*, kSetCount> Sets{};
    VkImage TiledDepth = VK_NULL_HANDLE;
    VkImage Raw = VK_NULL_HANDLE;
    VkImage Filtered = VK_NULL_HANDLE;
    VkImage Output = VK_NULL_HANDLE;
    uint64_t OutputIdentity = 0;
    std::array<VkImage, 3> Inputs{};
    std::array<VkFormat, 3> Formats{};
    uint32_t Width = 0, Height = 0;
    // Temporal mode: the motion vectors and two accumulation images, written
    // alternately (the other one is the history).
    MotionInput Motion;
    std::array<VkImage, 2> History{};
    uint32_t NextHistory = 0;
    bool HistoryValid = false;
    uint64_t HistoryFrameId = 0;
    float HistoryFarPlane = 0.0F;
    std::array<float, 13> HistorySettings{};
    // Shading rate: the raw estimate holds every pixel once a full-rate frame
    // has written it; the frame identifier selects the rotating phase.
    bool RawComplete = false;
    bool LastHistoryValid = false;
    std::string Reason;
};

namespace {
void InvalidateScreenResources(TriaevumImmerseMxao* mxao) {
    const auto* host = mxao->Host;
    if (mxao->Pool)
        mxao->Core->DestroyDescriptorPool(mxao->Pool);
    mxao->Pool = nullptr;
    mxao->Sets = {};
    for (const VkImage image : { mxao->TiledDepth, mxao->Raw, mxao->Filtered, mxao->History[0], mxao->History[1] })
        if (image != VK_NULL_HANDLE)
            host->DestroyTexture(host->Context, image);
    mxao->TiledDepth = mxao->Raw = mxao->Filtered = VK_NULL_HANDLE;
    mxao->History = {};
    mxao->Output = VK_NULL_HANDLE;
    mxao->OutputIdentity = 0;
    mxao->Width = mxao->Height = 0;
    mxao->Inputs = {};
    mxao->Formats = {};
    mxao->Motion = {};
    mxao->NextHistory = 0;
    mxao->HistoryValid = false;
    mxao->LastHistoryValid = false;
    mxao->RawComplete = false;
    host->ClearPrivateStates(host->Context);
}

void Destroy(TriaevumImmerseMxao* mxao) {
    if (mxao == nullptr)
        return;
    InvalidateScreenResources(mxao);
    if (mxao->Sampler)
        mxao->Core->DestroyDescriptor(mxao->Sampler);
    for (auto* pipeline : mxao->Pipelines)
        if (pipeline)
            mxao->Core->DestroyPipeline(pipeline);
    if (mxao->Layout)
        mxao->Core->DestroyPipelineLayout(mxao->Layout);
    delete mxao;
}

TriaevumImmerseMxao* Create(const TriaevumImmerseHost* host, char* reason, size_t reasonBytes) {
    auto* mxao = new TriaevumImmerseMxao;
    mxao->Host = host;
    try {
        if (host == nullptr || host->AbiVersion != TRIAEVUM_IMMERSE_ABI_VERSION)
            throw std::runtime_error("iMMERSE module interface version mismatch");
        for (const VkFormat format : { VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT })
            if (!host->SupportsSampledStorage(host->Context, format))
                throw std::runtime_error("MXAO requires sampled/storage RG16F, R32F and RGBA16F");
        mxao->Core = static_cast<nri::CoreInterface*>(const_cast<void*>(host->NriCore));
        mxao->Device = static_cast<nri::Device*>(host->NriDevice);
        if (mxao->Device == nullptr || mxao->Core == nullptr)
            throw std::runtime_error("MXAO NRI device unavailable");
        const std::array<nri::DescriptorRangeDesc, 8> ranges{
            { { 0, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 1, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 2, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 3, 1, nri::DescriptorType::STORAGE_TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 4, 1, nri::DescriptorType::SAMPLER, nri::StageBits::COMPUTE_SHADER },
              { 5, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 6, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER },
              { 7, 1, nri::DescriptorType::TEXTURE, nri::StageBits::COMPUTE_SHADER } }
        };
        nri::DescriptorSetDesc set{};
        set.ranges = ranges.data();
        set.rangeNum = uint32_t(ranges.size());
        nri::RootConstantDesc root{};
        root.size = sizeof(MxaoConstants);
        root.shaderStages = nri::StageBits::COMPUTE_SHADER;
        nri::PipelineLayoutDesc layout{};
        layout.descriptorSets = &set;
        layout.descriptorSetNum = 1;
        layout.rootConstants = &root;
        layout.rootConstantNum = 1;
        layout.shaderStages = nri::StageBits::COMPUTE_SHADER;
        layout.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;
        if (mxao->Core->CreatePipelineLayout(*mxao->Device, layout, mxao->Layout) != nri::Result::SUCCESS)
            throw std::runtime_error("MXAO pipeline layout failed");
        const auto sources = BuildMxaoPassShaders(host->DebugGlsl, host->GeometryGuideGlsl);
        static_assert(std::tuple_size_v<decltype(sources)> == kProgramCount);
        constexpr std::array<const char*, kProgramCount> names{
            "immerse_mxao_deinterleave.comp", "immerse_mxao_occlusion.comp", "immerse_mxao_filter.comp",
            "immerse_mxao_resolve.comp", "immerse_mxao_temporal.comp", "immerse_mxao_debug.comp" };
        const auto pack = MxaoPassPack();
        for (size_t i = 0; i < sources.size(); ++i) {
            size_t words = 0;
            const uint32_t* spirv =
                host->ComputeSpirv(host->Context, pack.data(), pack.size(), sources[i].c_str(), names[i], &words);
            if (spirv == nullptr || words == 0)
                throw std::runtime_error("MXAO program unavailable");
            nri::ComputePipelineDesc pipeline{};
            pipeline.pipelineLayout = mxao->Layout;
            pipeline.shader.stage = nri::StageBits::COMPUTE_SHADER;
            pipeline.shader.bytecode = spirv;
            pipeline.shader.size = words * sizeof(uint32_t);
            pipeline.cache = static_cast<nri::PipelineCache*>(host->NriPipelineCache);
            if (mxao->Core->CreateComputePipeline(*mxao->Device, pipeline, mxao->Pipelines[i]) != nri::Result::SUCCESS)
                throw std::runtime_error("MXAO pipeline failed");
        }
        nri::SamplerDesc sampler{};
        sampler.filters = { nri::Filter::NEAREST, nri::Filter::NEAREST, nri::Filter::NEAREST };
        sampler.addressModes = { nri::AddressMode::CLAMP_TO_EDGE, nri::AddressMode::CLAMP_TO_EDGE,
                                 nri::AddressMode::CLAMP_TO_EDGE };
        if (mxao->Core->CreateSampler(*mxao->Device, sampler, mxao->Sampler) != nri::Result::SUCCESS)
            throw std::runtime_error("MXAO sampler failed");
        return mxao;
    } catch (const std::exception& error) {
        if (reason != nullptr && reasonBytes != 0) {
            const size_t length = std::min(std::strlen(error.what()), reasonBytes - 1);
            std::memcpy(reason, error.what(), length);
            reason[length] = '\0';
        }
        if (mxao->Core == nullptr)
            delete mxao;
        else
            Destroy(mxao);
        return nullptr;
    }
}

int Configure(TriaevumImmerseMxao* mxao, const TriaevumMxaoTargets* targets) {
    const auto& target = *targets;
    const auto* host = mxao->Host;
    const std::array inputs{ target.Depth, target.Normal, target.Geometry };
    const std::array formats{ target.DepthFormat, target.NormalFormat, VK_FORMAT_R32G32B32A32_UINT };
    if (target.Output == VK_NULL_HANDLE || target.Depth == VK_NULL_HANDLE || target.Normal == VK_NULL_HANDLE ||
        target.Geometry == VK_NULL_HANDLE)
        return 0;
    const MotionInput temporal = target.Motion != VK_NULL_HANDLE && target.MotionWidth != 0U && target.MotionHeight != 0U
                                ? MotionInput{ target.Motion, target.MotionFormat, target.MotionWidth, target.MotionHeight }
                                : MotionInput{};
    if (mxao->Width == target.Width && mxao->Height == target.Height && mxao->Output == target.Output &&
        mxao->OutputIdentity == target.OutputIdentity && mxao->Inputs == inputs && mxao->Formats == formats &&
        mxao->Motion == temporal)
        return 1;
    if (mxao->Width != 0 && !host->CompleteRecordedWork(host->Context))
        return 0;
    InvalidateScreenResources(mxao);
    try {
        const uint32_t tiledWidth = MxaoTiledExtent(target.Width);
        const uint32_t tiledHeight = MxaoTiledExtent(target.Height);
        if (!host->CreateTexture(host->Context, tiledWidth, tiledHeight, VK_FORMAT_R32_SFLOAT, &mxao->TiledDepth) ||
            !host->CreateTexture(host->Context, target.Width, target.Height, VK_FORMAT_R16G16_SFLOAT, &mxao->Raw) ||
            !host->CreateTexture(host->Context, target.Width, target.Height, VK_FORMAT_R16G16_SFLOAT,
                                 &mxao->Filtered))
            throw std::runtime_error("MXAO scratch allocation failed");
        const bool accumulates = temporal.Image != VK_NULL_HANDLE;
        if (accumulates)
            for (auto& history : mxao->History)
                if (!host->CreateTexture(host->Context, target.Width, target.Height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         &history))
                    throw std::runtime_error("MXAO history allocation failed");
        mxao->Output = target.Output;
        mxao->OutputIdentity = target.OutputIdentity;
        const auto view = [&](VkImage image, bool storage) {
            auto* descriptor = static_cast<nri::Descriptor*>(host->TextureView(host->Context, image, storage ? 1 : 0));
            if (!descriptor)
                throw std::runtime_error("MXAO view unavailable");
            return descriptor;
        };
        std::array<nri::Descriptor*, 3> sources{};
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (!host->WrapTexture(host->Context, inputs[i], formats[i], target.Width, target.Height))
                throw std::runtime_error("MXAO input wrapping failed");
            sources[i] = static_cast<nri::Descriptor*>(host->TextureView(host->Context, inputs[i], 0));
            if (!sources[i])
                throw std::runtime_error("MXAO input view unavailable");
        }
        // Programs that do not read the motion vectors or a history still
        // bind a valid input there; none of them writes the native depth.
        nri::Descriptor* motionView = sources[0];
        if (accumulates) {
            if (!host->WrapTexture(host->Context, temporal.Image, temporal.Format, temporal.Width, temporal.Height))
                throw std::runtime_error("MXAO motion wrapping failed");
            motionView = static_cast<nri::Descriptor*>(host->TextureView(host->Context, temporal.Image, 0));
            if (!motionView)
                throw std::runtime_error("MXAO motion view unavailable");
        }
        const size_t setCount = accumulates ? size_t(kSetCount) : size_t(kTemporalSet);
        nri::DescriptorPoolDesc pool{};
        pool.descriptorSetMaxNum = uint32_t(setCount);
        pool.textureMaxNum = uint32_t(6 * setCount);
        pool.storageTextureMaxNum = uint32_t(setCount);
        pool.samplerMaxNum = uint32_t(setCount);
        if (mxao->Core->CreateDescriptorPool(*mxao->Device, pool, mxao->Pool) != nri::Result::SUCCESS ||
            mxao->Core->AllocateDescriptorSets(*mxao->Pool, *mxao->Layout, 0, mxao->Sets.data(), uint32_t(setCount),
                                               0) != nri::Result::SUCCESS)
            throw std::runtime_error("MXAO descriptor allocation failed");
        struct SetBinding {
            nri::Descriptor* Intermediate = nullptr;
            nri::Descriptor* Destination = nullptr;
            nri::Descriptor* History = nullptr;
        };
        // The deinterleave program does not read binding 2; bind an input so
        // the unused descriptor still has a valid layout.
        std::array<SetBinding, kSetCount> bindings{ {
            { sources[0], view(mxao->TiledDepth, true), sources[0] },
            { view(mxao->TiledDepth, false), view(mxao->Raw, true), sources[0] },
            { view(mxao->Raw, false), view(mxao->Filtered, true), sources[0] },
            { view(mxao->Raw, false), view(mxao->Output, true), sources[0] },
            { view(mxao->Filtered, false), view(mxao->Output, true), sources[0] },
        } };
        if (accumulates) {
            for (size_t written = 0; written < 2; ++written) {
                const VkImage accumulation = mxao->History[written];
                bindings[kTemporalSet + written] = { view(mxao->Raw, false), view(accumulation, true),
                                                     view(mxao->History[1 - written], false) };
                bindings[kFilterAccumulatedSet + written] = { view(accumulation, false), view(mxao->Filtered, true),
                                                              sources[0] };
                bindings[kResolveAccumulatedSet + written] = { view(accumulation, false), view(mxao->Output, true),
                                                               sources[0] };
            }
        }
        for (size_t i = 0; i < setCount; ++i) {
            auto* set = mxao->Sets[i];
            const auto& binding = bindings[i];
            const std::array<nri::UpdateDescriptorRangeDesc, 8> updates{ { { set, 0, 0, &sources[0], 1 },
                                                                           { set, 1, 0, &sources[1], 1 },
                                                                           { set, 2, 0, &binding.Intermediate, 1 },
                                                                           { set, 3, 0, &binding.Destination, 1 },
                                                                           { set, 4, 0, &mxao->Sampler, 1 },
                                                                           { set, 5, 0, &motionView, 1 },
                                                                           { set, 6, 0, &binding.History, 1 },
                                                                           { set, 7, 0, &sources[2], 1 } } };
            mxao->Core->UpdateDescriptorRanges(updates.data(), uint32_t(updates.size()));
        }
        mxao->Width = target.Width;
        mxao->Height = target.Height;
        mxao->Inputs = inputs;
        mxao->Formats = formats;
        mxao->Motion = temporal;
        mxao->Reason.clear();
        return 1;
    } catch (const std::exception& error) {
        mxao->Reason = error.what();
        InvalidateScreenResources(mxao);
        return 0;
    }
}

int Execute(TriaevumImmerseMxao* mxao, const TriaevumMxaoFrame* frame) {
    const auto& f = *frame;
    const auto& settings = f.Settings;
    const auto* host = mxao->Host;
    mxao->LastHistoryValid = false;
    if (f.NearPlane <= 0 || f.FarPlane <= f.NearPlane)
        return 0;
    auto* command = static_cast<nri::CommandBuffer*>(host->CommandBuffer(host->Context, f.FrameSlot));
    const bool accumulates = mxao->Motion.Image != VK_NULL_HANDLE;
    const size_t setCount = accumulates ? size_t(kSetCount) : size_t(kTemporalSet);
    if (!command || std::any_of(mxao->Sets.begin(), mxao->Sets.begin() + ptrdiff_t(setCount),
                                [](auto* set) { return set == nullptr; }))
        return 0;
    MxaoConstants push;
    push.Width = mxao->Width;
    push.Height = mxao->Height;
    push.Method = std::min<uint32_t>(settings.Method, 3U);
    const uint32_t quality = std::min<uint32_t>(settings.Quality, kMxaoQualityPresets - 1U);
    // The original's options: normal source, screen radius, fade and shading
    // rate. A skipped pixel needs an estimate from an earlier frame. Face
    // normals come from the geometry guide, from depth where it has none.
    const bool depthNormals = settings.Normals == 0U;
    const bool geometryNormals = settings.Normals == 2U;
    const bool screenRadius = settings.RadiusGrowsWithDistance != 0U;
    const bool originalFade = settings.OriginalFade != 0U;
    const uint32_t shadingRate = mxao->RawComplete ? std::min<uint32_t>(settings.ShadingRate, 2U) : 0U;
    // From the frame, not from executions: a frame that runs the pass for
    // more than one surface keeps one phase, and captures are reproducible.
    const uint32_t shadingPhase = uint32_t(f.FrameId & 3U);
    const uint32_t options = (depthNormals ? kMxaoDepthNormals : 0U) | (geometryNormals ? kMxaoGeometryNormals : 0U) |
                             (screenRadius ? kMxaoScreenRadius : 0U) | (originalFade ? kMxaoOriginalFade : 0U) |
                             (shadingRate << kMxaoShadingRateShift) | (shadingPhase << kMxaoShadingPhaseShift);
    const auto& projection = f.Projection;
    push.ViewX = { (f.NdcX[0] - f.NdcOrigin[0]) / projection[0], (f.NdcY[0] - f.NdcOrigin[0]) / projection[0],
                   (f.NdcOrigin[0] + projection[8]) / projection[0], 0 };
    push.ViewY = { (f.NdcX[1] - f.NdcOrigin[1]) / projection[5], (f.NdcY[1] - f.NdcOrigin[1]) / projection[5],
                   (f.NdcOrigin[1] + projection[9]) / projection[5], 0 };
    const bool wBuffer = f.WBuffer != 0U;
    push.Depth = { wBuffer ? f.NearPlane : f.NearPlane * f.FarPlane, f.FarPlane, f.FarPlane - f.NearPlane,
                   wBuffer ? 1.0F : 0.0F };
    // The original's thickness heuristic for the visibility bitmask.
    const float radius = std::max(settings.Radius, 1.0e-4F);
    push.RadiusFade = { radius, std::log(1.0F + radius) / 3.0F, originalFade ? settings.FadeDistance : settings.FadeFrom,
                        originalFade ? 0.0F : settings.FadeTo };
    // Decoding and renormalizing RGB8 normals introduces at most
    // sqrt(3)/(255-sqrt(3)) angular error; use the GTAO kernels' 2/255 bound.
    const float normalError =
        mxao->Formats[1] == VK_FORMAT_R8G8B8A8_UNORM || mxao->Formats[1] == VK_FORMAT_B8G8R8A8_UNORM ? 2.0F / 255.0F
        : mxao->Formats[1] == VK_FORMAT_R16G16B16A16_SFLOAT                                          ? 1.0F / 1024.0F
                                                                                                     : 0.0F;
    // The original's screen radius: 3% of the width per unit of its Sample Radius.
    push.Response = { settings.Strength, 1.0F / f.FarPlane, normalError * normalError,
                      screenRadius ? settings.ScreenRadius * 0.03F : 0.0F };
    // Temporal mode: the history continues only from the directly preceding
    // frame of the same view and settings; the jitter phase rotates per frame.
    const bool temporalFrame = accumulates && f.Temporal != 0U;
    const std::array<float, 13> historySettings{ float(push.Method), float(quality), radius, settings.Strength,
                                                 settings.FadeFrom, settings.FadeTo, wBuffer ? 1.0F : 0.0F,
                                                 normalError, float(settings.Normals),
                                                 screenRadius ? settings.ScreenRadius : 0.0F,
                                                 originalFade ? 1.0F : 0.0F, settings.FadeDistance,
                                                 float(settings.Relief) };
    uint32_t phase = 0U;
    if (temporalFrame) {
        const bool continuous = mxao->HistoryValid && f.ViewHistoryValid != 0U && f.CameraCut == 0U &&
                                f.TemporalFrameId == mxao->HistoryFrameId + 1U &&
                                mxao->HistorySettings == historySettings;
        push.Temporal = { continuous ? 1.0F : 0.0F, kMxaoTemporalHistoryLength, kMxaoTemporalDepthTolerance,
                          kMxaoTemporalMovingHistoryLength };
        if (continuous) {
            std::array<float, 16> previous{}, inverse{}, view{};
            std::copy(std::begin(f.PreviousWorldToClip), std::end(f.PreviousWorldToClip), previous.begin());
            std::copy(std::begin(f.InverseCurrentWorldToClip), std::end(f.InverseCurrentWorldToClip), inverse.begin());
            std::copy(std::begin(projection), std::end(projection), view.begin());
            push.PreviousDepth = MxaoPreviousDepthRow(previous, inverse, view, mxao->HistoryFarPlane);
        }
        phase = MxaoTemporalPhase(f.TemporalFrameId);
        mxao->LastHistoryValid = continuous;
    }
    host->BeginOutput(host->Context);
    const auto transition = [&](VkImage image, uint32_t state) {
        return host->Transition(host->Context, f.FrameSlot, image, state) != 0;
    };
    mxao->Core->CmdSetDescriptorPool(*command, *mxao->Pool);
    mxao->Core->CmdSetPipelineLayout(*command, nri::BindPoint::COMPUTE, *mxao->Layout);
    const auto dispatch = [&](size_t program, size_t set, uint32_t flags, uint32_t width, uint32_t height,
                              uint32_t group) {
        push.Flags = quality | (phase << kMxaoJitterPhaseShift) | options | flags;
        mxao->Core->CmdSetPipeline(*command, *mxao->Pipelines[program]);
        const nri::SetDescriptorSetDesc descriptors{ 0, mxao->Sets[set] };
        mxao->Core->CmdSetDescriptorSet(*command, descriptors);
        const nri::SetRootConstantsDesc constants{ 0, &push, sizeof(push) };
        mxao->Core->CmdSetRootConstants(*command, constants);
        mxao->Core->CmdDispatch(*command, { (width + group - 1) / group, (height + group - 1) / group, 1 });
    };
    const uint32_t tiledWidth = MxaoTiledExtent(mxao->Width);
    const uint32_t tiledHeight = MxaoTiledExtent(mxao->Height);
    if (!transition(mxao->TiledDepth, TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE))
        return 0;
    dispatch(kDeinterleaveProgram, kDeinterleaveSet, 0, tiledWidth, tiledHeight, 8);
    if (!transition(mxao->TiledDepth, TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ) ||
        !transition(mxao->Raw, TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE))
        return 0;
    dispatch(kOcclusionProgram, kOcclusionSet, 0, tiledWidth, tiledHeight, 16);
    mxao->RawComplete = true;
    if (!transition(mxao->Raw, TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ))
        return 0;
    // The filter reads this frame's raw estimate, or its accumulation.
    size_t filterSet = kFilterSet;
    size_t resolveSet = kResolveRawSet;
    if (temporalFrame) {
        const uint32_t written = mxao->NextHistory;
        if (!transition(mxao->History[written], TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE) ||
            !transition(mxao->History[1U - written], TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ))
            return 0;
        dispatch(kTemporalProgram, kTemporalSet + written, 0, mxao->Width, mxao->Height, 8);
        if (!transition(mxao->History[written], TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ))
            return 0;
        filterSet = kFilterAccumulatedSet + written;
        resolveSet = kResolveAccumulatedSet + written;
        mxao->NextHistory = 1U - written;
        mxao->HistoryValid = true;
        mxao->HistoryFrameId = f.TemporalFrameId;
        mxao->HistoryFarPlane = f.FarPlane;
        mxao->HistorySettings = historySettings;
    } else {
        mxao->HistoryValid = false;
    }
    // Filter quality 2 runs both regression iterations, 1 only the mirrored
    // one, 0 publishes the raw estimate. The unfiltered debug view publishes
    // the filter's input; the other views replace the resolve.
    const uint32_t debug = settings.DebugView;
    const uint32_t filter = debug == TRIAEVUM_MXAO_DEBUG_UNFILTERED ? 0U : std::min<uint32_t>(settings.Filter, 2U);
    const size_t inputSet = resolveSet;
    if (filter == 2U) {
        if (!transition(mxao->Filtered, TRIAEVUM_IMMERSE_PRIVATE_COMPUTE_WRITE))
            return 0;
        dispatch(kFilterProgram, filterSet, 0, mxao->Width, mxao->Height, 8);
        if (!transition(mxao->Filtered, TRIAEVUM_IMMERSE_PRIVATE_SHADER_READ))
            return 0;
        resolveSet = kResolveFilteredSet;
    }
    if (!transition(mxao->Output, TRIAEVUM_IMMERSE_OUTPUT_DISPATCH))
        return 0;
    if (debug >= TRIAEVUM_MXAO_DEBUG_NORMALS) {
        // The history view reads the accumulation, the others the raw estimate.
        const uint32_t view = debug - TRIAEVUM_MXAO_DEBUG_NORMALS + 1U;
        const bool history = debug == TRIAEVUM_MXAO_DEBUG_HISTORY_LENGTH && temporalFrame;
        dispatch(kDebugProgram, history ? inputSet : kResolveRawSet, view << kMxaoDebugViewShift, mxao->Width,
                 mxao->Height, 8);
    } else {
        dispatch(kResolveProgram, resolveSet, filter == 0U ? kMxaoFilterCopy : kMxaoFilterMirrored, mxao->Width,
                 mxao->Height, 8);
    }
    return transition(mxao->Output, TRIAEVUM_IMMERSE_OUTPUT_COMPLETION) ? 1 : 0;
}

// The texts of the F1 controls: the original's options, preset names and
// descriptions.
const char* const kQualityNames[] = { "Low", "Medium", "High", "Very high", "Ultra", "Extreme", "IDGAF" };
const char* const kShadingRates[] = { "Full rate", "Half rate", "Quarter rate" };
const char* const kMethodNames[] = { "Ground truth", "Solid angle", "Visibility bitmask",
                                     "Visibility bitmask, solid angle" };
const char* const kNormalNames[] = { "From depth (original)", "Renderer normals", "Face normals" };
const char* const kDebugViews[] = { "Off", "Ambient occlusion", "Unfiltered occlusion", "Normals in use",
                                    "Renderer normals", "Depth normals", "Normal disagreement", "Depth",
                                    "Edge weight", "Fade", "Temporal history", "Face normals",
                                    "Face vs depth normals" };
static_assert(std::size(kDebugViews) == TRIAEVUM_MXAO_DEBUG_COUNT);

const TriaevumMxaoUi kUi{
    { "MXAO quality", kQualityNames, 7,
      "Global quality control, main performance knob. Higher radii might require higher quality." },
    { "MXAO shading rate", kShadingRates, 3,
      "Full: every pixel each frame. Half / Quarter: 50% / 25% of the pixels each frame; the others keep "
      "their previous estimate, which trails moving content." },
    { "MXAO method", kMethodNames, 4,
      "Ground truth: high contrast. Solid angle: smoother. Visibility bitmask: highest quality, slower. "
      "Bitmask with solid angle: like the bitmask, smoother." },
    { "MXAO normals", kNormalNames, 3,
      "From depth: the plane of the depth samples MXAO tests, as the original computes it. Renderer normals: "
      "interpolated vertex normals, PBR normal maps and texture relief (like Launchpad's smooth normals); "
      "where they tilt away from the surface, as across sharp corners, the surface occludes itself and "
      "darkens. Face normals (default): the exact plane of each visible triangle, the depth plane without "
      "its reconstruction across silhouettes." },
    "Increase radius with distance",
    { "MXAO sample radius", 0.5F, 10.0F, "%.2f",
      "The original's sample radius: a constant screen radius (3% of the width per unit), so the "
      "world radius grows with distance." },
    { "MXAO radius", 0.05F, 100.0F, "%.2f",
      "World radius in native scene units: higher means more large-scale occlusion with less "
      "fine-scale detail." },
    { "MXAO amount", 0.0F, 2.0F, "%.2f", "Intensity of the AO. Above 1 the original switches to a gamma response." },
    "MXAO filter quality",
    2,
    "MXAO temporal accumulation",
    "Each frame samples other directions and the scene's motion vectors carry the "
    "result over frames: a lower quality preset gives a stable image.",
    "AO texture relief bends only the renderer normals.",
    "Advanced AO",
    "Original fade",
    "The original's exponential Fade Out Distance, over depth normalized by the far "
    "plane, instead of the fade start and end.",
    { "Fade out distance", 0.0F, 1.0F, "%.3f", nullptr },
    { "Fade start", 0.0F, 100000.0F, "%.1f", nullptr },
    { "Fade end", 0.01F, 100000.0F, "%.1f", nullptr },
    { "MXAO debug view", kDebugViews, 13,
      "Replaces the image. Ambient occlusion is the original's Show Raw AO; the others show the depth and "
      "normals the renderer passes and the estimate's terms. Normals: view space (X right, Y up, Z toward "
      "the viewer). Normal disagreement (renderer vs depth): black = equal, white = 90 degrees apart. Face "
      "vs depth normals: black = equal, white = 10 degrees or more apart. Not saved." },
    "Credits",
    { "MXAO: iMMERSE by Pascal Gilcher (martysmods.com). Thanks to the author.",
      "Copyright (c) Pascal Gilcher, all rights reserved. Licence exception: used and modified "
      "with the author's permission, granted to TriAevum only; it stays under its original "
      "licence, outside TriAevum's GPL." },
};

const TriaevumImmerseModule kModule{
    TRIAEVUM_IMMERSE_ABI_VERSION,
    &kUi,
    Create,
    Destroy,
    Configure,
    Execute,
    [](TriaevumImmerseMxao* mxao) { InvalidateScreenResources(mxao); },
    [](const TriaevumImmerseMxao* mxao) { return mxao->Output; },
    [](const TriaevumImmerseMxao* mxao) { return mxao->LastHistoryValid ? 1 : 0; },
    [](const TriaevumImmerseMxao* mxao) { return mxao->Reason.c_str(); },
};
} // namespace

extern "C" TRIAEVUM_IMMERSE_EXPORT const TriaevumImmerseModule* triaevum_immerse_module(uint32_t abiVersion) {
    return abiVersion == TRIAEVUM_IMMERSE_ABI_VERSION ? &kModule : nullptr;
}
