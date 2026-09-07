#include "oot3d_native_a32_savestate.h"

#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_a32_process.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_submission.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace Oot3dNativeGame {
namespace {

constexpr std::array<uint8_t, 8> kStateMagic{
    'O', 'O', 'T', '3', 'D', 'S', 'V', 0};
constexpr uint32_t kStateContainerVersion = 1U;
constexpr uint32_t kStateHeaderSize = 40U;
constexpr uint64_t kMaximumStatePayloadBytes = 1ULL << 30U;
constexpr uint64_t kMaximumPicaVisualReplayStateBytes = 256ULL << 20U;
constexpr size_t kMaximumPicaTextureCacheEntryCount = 4096U;
constexpr uint64_t kMaximumPicaTextureCacheBytes = 512ULL << 20U;
constexpr size_t kMaximumPicaColorTargetCount = 64U;
constexpr uint64_t kMaximumPicaColorTargetBytes = 512ULL << 20U;
constexpr size_t kMaximumPicaDisplayImageCount = 64U;
constexpr uint64_t kMaximumPicaDisplayImageBytes = 512ULL << 20U;

using PicaTextureSnapshotIdentity =
    std::tuple<uint64_t, uint64_t, uint32_t, uint16_t, uint16_t,
               uint8_t, uint8_t, uint8_t, uint8_t, bool, bool, bool,
               int16_t, uint8_t, uint8_t, bool>;

std::optional<uint64_t> PicaDecodedMipChainBytes(
    uint32_t width, uint32_t height, uint32_t levels) {
    if (width == 0U || height == 0U || levels == 0U) {
        return std::nullopt;
    }
    uint64_t bytes = 0U;
    for (uint32_t level = 0U; level < levels; ++level) {
        const uint64_t levelBytes =
            static_cast<uint64_t>(std::max(1U, width >> level)) *
            std::max(1U, height >> level) * 4U;
        if (bytes > std::numeric_limits<uint64_t>::max() - levelBytes) {
            return std::nullopt;
        }
        bytes += levelBytes;
    }
    return bytes;
}

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

template <typename T>
void AppendLittleEndian(std::vector<uint8_t>& output, T value) {
    static_assert(std::is_unsigned_v<T>);
    for (size_t index = 0; index < sizeof(T); ++index) {
        output.push_back(static_cast<uint8_t>(value >> (index * 8U)));
    }
}

template <typename T>
bool ReadLittleEndian(std::span<const uint8_t> bytes, size_t offset,
                      T& value) {
    static_assert(std::is_unsigned_v<T>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return false;
    }
    value = 0;
    for (size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<T>(bytes[offset + index]) << (index * 8U);
    }
    return true;
}

uint64_t Fnv1a64(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (const uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

double SecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                         start)
        .count();
}

bool CompatibilityMatches(const nlohmann::json& encoded,
                          const NativeA32SavestateCompatibility& expected,
                          std::string* error) {
    if (!encoded.is_object() ||
        encoded.value("runtime_state_abi", std::string{}) !=
            NativeA32SavestateRuntimeAbi) {
        SetError(error, "savestate runtime ABI is incompatible");
        return false;
    }
    if (encoded.value("code_bin_sha256", std::string{}) !=
        expected.CodeBinSha256) {
        SetError(error, "savestate code.bin identity is incompatible");
        return false;
    }
    if (encoded.value("process_name", std::string{}) !=
        expected.ProcessName) {
        SetError(error, "savestate process identity is incompatible");
        return false;
    }
    if (encoded.value("a32_source_snapshot_id", std::string{}) !=
        expected.A32SourceSnapshotId) {
        SetError(error, "savestate A32 source snapshot is incompatible");
        return false;
    }
    return true;
}

bool ReplaceStateFile(const std::filesystem::path& temporary,
                      const std::filesystem::path& destination,
                      std::string* error) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetError(error, "could not atomically replace savestate file (Win32 " +
                            std::to_string(GetLastError()) + ")");
        return false;
    }
    return true;
#else
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    if (renameError) {
        SetError(error, "could not atomically replace savestate file: " +
                            renameError.message());
        return false;
    }
    return true;
#endif
}

} // namespace

bool SaveNativeA32State(
    const std::filesystem::path& path,
    const NativeA32SavestateCompatibility& compatibility,
    const NativeA32SavestateRuntimeState& runtime,
    const NativeA32Process& process,
    const NativeA32CtrHostServices& host,
    const Oot3dNativePicaFrontend& picaFrontend,
    const Oot3dNativePicaSubmissionQueue& submissionQueue,
    const NativeA32DspHle& dspHle,
    NativeA32SavestateIoResult* result,
    std::string* error) {
    try {
        if (result != nullptr) {
            *result = {};
        }
        if (path.empty()) {
            SetError(error, "savestate output path is empty");
            return false;
        }
        if (!submissionQueue.IsQuiescent()) {
            SetError(error,
                     "savestate requires a quiescent native PICA boundary");
            return false;
        }
        const auto captureStarted = std::chrono::steady_clock::now();
        if (runtime.PicaVisualReplayState.size() >
                kMaximumPicaVisualReplayStateBytes ||
            runtime.PicaVisualReplayStateAvailable !=
                !runtime.PicaVisualReplayState.empty()) {
            SetError(error,
                     "savestate native PICA visual replay state is invalid");
            return false;
        }
        nlohmann::json encodedPicaTextureCache = nlohmann::json::array();
        uint64_t picaTextureCacheBytes = 0U;
        std::set<PicaTextureSnapshotIdentity> picaTextureCacheIdentities;
        if (runtime.PicaTextureCache.size() >
            kMaximumPicaTextureCacheEntryCount) {
            SetError(error,
                     "savestate has too many native PICA cached textures");
            return false;
        }
        for (const auto& texture : runtime.PicaTextureCache) {
            const auto expectedBytes = PicaDecodedMipChainBytes(
                texture.ImageWidth, texture.ImageHeight,
                texture.MipLevels);
            const uint8_t imageFormat =
                static_cast<uint8_t>(texture.ImageFormat);
            picaTextureCacheBytes += texture.PixelBytes.size();
            if (texture.SourceWidth == 0U || texture.SourceHeight == 0U ||
                texture.ImageWidth == 0U || texture.ImageHeight == 0U ||
                texture.MipLevels == 0U || !expectedBytes.has_value() ||
                imageFormat > static_cast<uint8_t>(
                                  Oot3d::Renderer::
                                      PicaTextureSnapshotFormat::R32Uint) ||
                (texture.ImageFormat ==
                         Oot3d::Renderer::
                             PicaTextureSnapshotFormat::R32Uint &&
                 (texture.NativeType != 2U ||
                  texture.CustomReplacement)) ||
                (texture.ImageFormat ==
                         Oot3d::Renderer::
                             PicaTextureSnapshotFormat::R32Uint &&
                 texture.MipLevels != 1U) ||
                *expectedBytes != texture.PixelBytes.size() ||
                picaTextureCacheBytes > kMaximumPicaTextureCacheBytes ||
                !picaTextureCacheIdentities
                     .insert({texture.ContentHash,
                              texture.ReplacementGeneration,
                              texture.PhysicalAddress,
                              texture.SourceWidth,
                              texture.SourceHeight,
                              texture.NativeFormat,
                              texture.NativeType,
                              texture.NativeWrapS,
                              texture.NativeWrapT,
                              texture.MinLinear,
                              texture.MagLinear,
                              texture.MipLinear,
                              texture.LodBiasRaw,
                              texture.MinMipLevel,
                              texture.MaxMipLevel,
                              texture.CustomReplacement})
                     .second) {
                SetError(error,
                         "savestate native PICA cached texture is invalid");
                return false;
            }
            encodedPicaTextureCache.push_back(
                {{"content_hash", texture.ContentHash},
                 {"replacement_generation",
                  texture.ReplacementGeneration},
                 {"physical_address", texture.PhysicalAddress},
                 {"source_width", texture.SourceWidth},
                 {"source_height", texture.SourceHeight},
                 {"native_format", texture.NativeFormat},
                 {"native_type", texture.NativeType},
                 {"native_wrap_s", texture.NativeWrapS},
                 {"native_wrap_t", texture.NativeWrapT},
                 {"min_linear", texture.MinLinear},
                 {"mag_linear", texture.MagLinear},
                 {"mip_linear", texture.MipLinear},
                 {"lod_bias_raw", texture.LodBiasRaw},
                 {"min_mip_level", texture.MinMipLevel},
                 {"max_mip_level", texture.MaxMipLevel},
                 {"custom_replacement", texture.CustomReplacement},
                 {"image_width", texture.ImageWidth},
                 {"image_height", texture.ImageHeight},
                 {"mip_levels", texture.MipLevels},
                 {"image_format", imageFormat},
                 {"custom_replacement_hash",
                  texture.CustomReplacementHash},
                 {"custom_replacement_pending",
                  texture.CustomReplacementPending},
                 {"custom_replacement_ready",
                  texture.CustomReplacementReady},
                 {"pixel_bytes",
                  nlohmann::json::binary(texture.PixelBytes)}});
        }
        nlohmann::json encodedPicaColorTargets = nlohmann::json::array();
        uint64_t picaColorTargetBytes = 0U;
        std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint16_t,
                            uint16_t, uint8_t, uint8_t, uint16_t>>
            picaColorTargetIdentities;
        if (runtime.PicaColorTargets.size() >
            kMaximumPicaColorTargetCount) {
            SetError(error, "savestate has too many native PICA color targets");
            return false;
        }
        for (const auto& target : runtime.PicaColorTargets) {
            const uint64_t expectedBytes =
                static_cast<uint64_t>(target.ImageWidth) *
                target.ImageHeight * 4U;
            const uint64_t expectedPixels =
                static_cast<uint64_t>(target.ImageWidth) *
                target.ImageHeight;
            const uint64_t expectedMotionBytes = expectedPixels * 8U;
            std::vector<uint8_t> encodedDepth;
            encodedDepth.reserve(target.DepthValues.size() *
                                 sizeof(float));
            bool validDepthValues = true;
            for (const float value : target.DepthValues) {
                if (!std::isfinite(value) || value < 0.0F ||
                    value > 1.0F) {
                    validDepthValues = false;
                    break;
                }
                uint32_t bits = 0U;
                static_assert(sizeof(bits) == sizeof(value));
                std::memcpy(&bits, &value, sizeof(bits));
                AppendLittleEndian(encodedDepth, bits);
            }
            picaColorTargetBytes +=
                target.ColorRgba8.size() +
                target.NormalGuideRgba8.size() +
                target.MaterialGuideRgba8.size() +
                target.RigidMotionGuideRgba16FloatLe.size() +
                target.AmbientGuideRgba8.size() +
                target.ShadowR32UintLe.size() + encodedDepth.size() +
                target.StencilValues.size();
            if (target.ColorPhysicalAddress == 0U ||
                target.FramebufferWidth == 0U ||
                target.FramebufferHeight == 0U ||
                target.ImageWidth == 0U || target.ImageHeight == 0U ||
                target.SampleCount != 1U ||
                expectedBytes != target.ColorRgba8.size() ||
                expectedBytes != target.NormalGuideRgba8.size() ||
                expectedBytes != target.MaterialGuideRgba8.size() ||
                expectedMotionBytes !=
                    target.RigidMotionGuideRgba16FloatLe.size() ||
                expectedBytes != target.AmbientGuideRgba8.size() ||
                expectedBytes != target.ShadowR32UintLe.size() ||
                expectedPixels != target.DepthValues.size() ||
                (!target.StencilValues.empty() &&
                 expectedPixels != target.StencilValues.size()) ||
                !validDepthValues ||
                picaColorTargetBytes > kMaximumPicaColorTargetBytes ||
                !picaColorTargetIdentities
                     .insert({target.RenderTargetNamespace,
                              target.ColorPhysicalAddress,
                              target.DepthPhysicalAddress,
                              target.FramebufferWidth,
                              target.FramebufferHeight,
                              target.FramebufferColorFormat,
                              target.FramebufferDepthFormat,
                              target.RenderScalePermille})
                     .second) {
                SetError(error,
                         "savestate native PICA color target is invalid");
                return false;
            }
            encodedPicaColorTargets.push_back(
                {{"render_target_namespace",
                  target.RenderTargetNamespace},
                 {"color_physical_address",
                  target.ColorPhysicalAddress},
                 {"depth_physical_address",
                  target.DepthPhysicalAddress},
                 {"framebuffer_width", target.FramebufferWidth},
                 {"framebuffer_height", target.FramebufferHeight},
                 {"framebuffer_color_format",
                  target.FramebufferColorFormat},
                 {"framebuffer_depth_format",
                  target.FramebufferDepthFormat},
                 {"render_scale_permille",
                  target.RenderScalePermille},
                 {"image_width", target.ImageWidth},
                 {"image_height", target.ImageHeight},
                 {"sample_count", target.SampleCount},
                 {"w_buffering", target.WBuffering},
                 {"color_rgba8",
                  nlohmann::json::binary(target.ColorRgba8)},
                 {"normal_guide_rgba8",
                  nlohmann::json::binary(target.NormalGuideRgba8)},
                 {"material_guide_rgba8",
                  nlohmann::json::binary(target.MaterialGuideRgba8)},
                 {"rigid_motion_guide_rgba16_float_le",
                  nlohmann::json::binary(
                      target.RigidMotionGuideRgba16FloatLe)},
                 {"ambient_guide_rgba8",
                  nlohmann::json::binary(target.AmbientGuideRgba8)},
                 {"shadow_r32_uint_le",
                  nlohmann::json::binary(target.ShadowR32UintLe)},
                 {"depth_float32_le",
                  nlohmann::json::binary(encodedDepth)},
                 {"stencil_u8",
                  nlohmann::json::binary(target.StencilValues)}});
        }
        const auto& presentation = runtime.PicaPresentationState;
        if (presentation.DisplayImages.size() >
            kMaximumPicaDisplayImageCount) {
            SetError(error,
                     "savestate has too many native PICA display images");
            return false;
        }
        nlohmann::json encodedPicaDisplayImages = nlohmann::json::array();
        uint64_t picaDisplayImageBytes = 0U;
        std::set<std::pair<uint64_t, uint32_t>> picaDisplayIdentities;
        for (const auto& image : presentation.DisplayImages) {
            const uint64_t expectedBytes =
                static_cast<uint64_t>(image.ImageWidth) *
                image.ImageHeight * 4U;
            picaDisplayImageBytes += image.ColorRgba8.size();
            const bool validDepthTarget =
                !image.HasDepthTarget ||
                picaColorTargetIdentities.contains(
                    {image.DepthTargetNamespace,
                     image.DepthTargetColorPhysicalAddress,
                     image.DepthTargetDepthPhysicalAddress,
                     image.DepthTargetFramebufferWidth,
                     image.DepthTargetFramebufferHeight,
                     image.DepthTargetFramebufferColorFormat,
                     image.DepthTargetFramebufferDepthFormat,
                     image.DepthTargetRenderScalePermille});
            if (image.OutputPhysicalAddress == 0U ||
                image.ImageWidth == 0U || image.ImageHeight == 0U ||
                (image.Initialized
                     ? expectedBytes != image.ColorRgba8.size()
                     : !image.ColorRgba8.empty()) ||
                picaDisplayImageBytes > kMaximumPicaDisplayImageBytes ||
                !validDepthTarget ||
                !picaDisplayIdentities
                     .insert({image.RenderTargetNamespace,
                              image.OutputPhysicalAddress})
                     .second) {
                SetError(error,
                         "savestate native PICA display image is invalid");
                return false;
            }
            encodedPicaDisplayImages.push_back(
                {{"render_target_namespace",
                  image.RenderTargetNamespace},
                 {"output_physical_address",
                  image.OutputPhysicalAddress},
                 {"image_width", image.ImageWidth},
                 {"image_height", image.ImageHeight},
                 {"initialized", image.Initialized},
                 {"color_rgba8",
                  nlohmann::json::binary(image.ColorRgba8)},
                 {"has_depth_target", image.HasDepthTarget},
                 {"depth_target_namespace",
                  image.DepthTargetNamespace},
                 {"depth_target_color_physical_address",
                  image.DepthTargetColorPhysicalAddress},
                 {"depth_target_depth_physical_address",
                  image.DepthTargetDepthPhysicalAddress},
                 {"depth_target_framebuffer_width",
                  image.DepthTargetFramebufferWidth},
                 {"depth_target_framebuffer_height",
                  image.DepthTargetFramebufferHeight},
                 {"depth_target_framebuffer_color_format",
                  image.DepthTargetFramebufferColorFormat},
                 {"depth_target_framebuffer_depth_format",
                  image.DepthTargetFramebufferDepthFormat},
                 {"depth_target_render_scale_permille",
                  image.DepthTargetRenderScalePermille}});
        }
        nlohmann::json encodedLastPresentedTransfer{
            {"available",
             presentation.LastPresentedTransfer.has_value()}};
        if (presentation.LastPresentedTransfer.has_value()) {
            const auto& transfer = *presentation.LastPresentedTransfer;
            const auto display = std::find_if(
                presentation.DisplayImages.begin(),
                presentation.DisplayImages.end(),
                [&](const auto& image) {
                    return image.RenderTargetNamespace ==
                               transfer.RenderTargetNamespace &&
                           image.OutputPhysicalAddress ==
                               transfer.OutputPhysicalAddress &&
                           image.Initialized;
                });
            if (transfer.CompletionId == 0U ||
                transfer.InputPhysicalAddress == 0U ||
                transfer.OutputPhysicalAddress == 0U ||
                transfer.InputWidth == 0U || transfer.InputHeight == 0U ||
                transfer.OutputWidth == 0U || transfer.OutputHeight == 0U ||
                static_cast<uint8_t>(transfer.PresentationMode) > 1U ||
                display == presentation.DisplayImages.end()) {
                SetError(error,
                         "savestate native PICA last presentation is invalid");
                return false;
            }
            encodedLastPresentedTransfer.update(
                {{"completion_id", transfer.CompletionId},
                 {"render_target_namespace",
                  transfer.RenderTargetNamespace},
                 {"input_physical_address",
                  transfer.InputPhysicalAddress},
                 {"output_physical_address",
                  transfer.OutputPhysicalAddress},
                 {"input_width", transfer.InputWidth},
                 {"input_height", transfer.InputHeight},
                 {"output_width", transfer.OutputWidth},
                 {"output_height", transfer.OutputHeight},
                 {"flags", transfer.Flags},
                 {"present", transfer.Present},
                 {"presentation_mode",
                  static_cast<uint8_t>(
                      transfer.PresentationMode)}});
        }
        nlohmann::json document{
            {"format", "oot3d_native_a32_savestate_v1"},
            {"compatibility",
             {{"runtime_state_abi", NativeA32SavestateRuntimeAbi},
              {"code_bin_sha256", compatibility.CodeBinSha256},
              {"process_name", compatibility.ProcessName},
              {"a32_source_snapshot_id",
               compatibility.A32SourceSnapshotId}}},
            {"runtime",
             {{"frame_count", runtime.FrameCount},
              {"refresh_tick_remainder", runtime.RefreshTickRemainder},
              {"next_vblank_tick", runtime.NextVblankTick},
              {"process_run_kind", runtime.ProcessRunKind},
              {"gameplay_clock",
               {{"available", runtime.GameplayClockAvailable},
                {"timing_mode",
                 static_cast<uint32_t>(runtime.GameplayTiming)},
                {"simulation_tick",
                 runtime.GameplayClock.SimulationTick},
                {"previous_logical_frame",
                 runtime.GameplayClock.PreviousLogicalFrame},
                {"current_logical_frame",
                 runtime.GameplayClock.CurrentLogicalFrame}}},
              {"presentation_scheduler",
               {{"available", runtime.PresentationSchedulerAvailable},
                {"guest_refresh_phase",
                 runtime.PresentationScheduler.GuestRefreshPhase},
                {"visual_sample_phase",
                 runtime.PresentationScheduler.VisualSamplePhase},
                {"started", runtime.PresentationScheduler.Started}}},
              {"frame_rate_policy_temporal_state",
               {{"available",
                 runtime.FrameRatePolicyTemporalStateAvailable},
                {"frame_pacing_state_address",
                 runtime.FrameRatePolicyTemporalState
                     .FramePacingStateAddress},
                {"frame_pacing_decision_pending",
                 runtime.FrameRatePolicyTemporalState
                     .FramePacingDecisionPending}}},
              {"topscreen_temporal_state",
               {{"available", runtime.TopScreenTemporalStateAvailable},
                {"profile_active",
                 runtime.TopScreenTemporalState.ProfileActive},
                {"pause_page_redraw_active",
                 runtime.TopScreenTemporalState.PausePageRedrawActive},
                {"pause_page_redraw_delay_calls",
                 runtime.TopScreenTemporalState.PausePageRedrawDelayCalls},
                {"pause_draw_native_transition_latched",
                 runtime.TopScreenTemporalState
                     .PauseDrawNativeTransitionLatched},
                {"pause_draw_suppression_delay_armed",
                 runtime.TopScreenTemporalState
                     .PauseDrawSuppressionDelayArmed},
                {"pause_draw_suppression_delay_commands",
                 runtime.TopScreenTemporalState
                     .PauseDrawSuppressionDelayCommands},
                {"pause_route_previous_runtime_mode",
                 runtime.TopScreenTemporalState
                     .PauseRoutePreviousRuntimeMode},
                {"pause_route_transition_phase",
                 runtime.TopScreenTemporalState.PauseRouteTransitionPhase},
                {"pause_route_remaining_calls",
                 runtime.TopScreenTemporalState.PauseRouteRemainingCalls},
                {"aot_pause_route_previous_runtime_mode",
                 runtime.TopScreenTemporalState
                     .AotPauseRoutePreviousRuntimeMode},
                {"aot_pause_route_transition_phase",
                 runtime.TopScreenTemporalState
                     .AotPauseRouteTransitionPhase},
                {"aot_pause_route_remaining_calls",
                 runtime.TopScreenTemporalState
                     .AotPauseRouteRemainingCalls},
                {"touch_coordinate_runtime_scene_latch",
                 runtime.TopScreenTemporalState
                     .TouchCoordinateRuntimeSceneLatch}}},
              {"native_pica_visual_replay_state",
               {{"available", runtime.PicaVisualReplayStateAvailable},
                {"payload", nlohmann::json::binary(
                                runtime.PicaVisualReplayState)}}},
              {"native_pica_texture_cache",
               {{"available", runtime.PicaTextureCacheAvailable},
                {"entries", std::move(encodedPicaTextureCache)}}},
              {"native_pica_color_targets",
               {{"available", runtime.PicaColorTargetsAvailable},
                {"targets", std::move(encodedPicaColorTargets)}}},
              {"native_pica_presentation_state",
               {{"available",
                 runtime.PicaPresentationStateAvailable},
                {"display_images",
                 std::move(encodedPicaDisplayImages)},
                {"last_presented_transfer",
                 std::move(encodedLastPresentedTransfer)}}}}},
            {"process", process.CaptureState()},
            {"ctr_host", host.CaptureState()},
            {"pica_frontend", picaFrontend.CaptureState()},
            {"pica_submission", submissionQueue.CaptureState()},
            {"dsp_hle", dspHle.CaptureState()},
        };
        const double captureSeconds = SecondsSince(captureStarted);
        const auto encodeStarted = std::chrono::steady_clock::now();
        auto& memoryWriteGeneration =
            document.at("process").at("memory").at("write_generation");
        const uint64_t savedMemoryWriteGeneration =
            memoryWriteGeneration.get<uint64_t>();
        memoryWriteGeneration = 1U;
        const uint64_t semanticFingerprint = Fnv1a64(
            nlohmann::json::to_msgpack(document));
        memoryWriteGeneration = savedMemoryWriteGeneration;
        const std::vector<uint8_t> payload =
            nlohmann::json::to_msgpack(document);
        const double encodeSeconds = SecondsSince(encodeStarted);
        if (payload.empty() || payload.size() > kMaximumStatePayloadBytes) {
            SetError(error, "savestate payload exceeds the supported size");
            return false;
        }

        std::vector<uint8_t> header;
        header.reserve(kStateHeaderSize);
        header.insert(header.end(), kStateMagic.begin(), kStateMagic.end());
        AppendLittleEndian(header, kStateContainerVersion);
        AppendLittleEndian(header, kStateHeaderSize);
        AppendLittleEndian(header, static_cast<uint64_t>(payload.size()));
        AppendLittleEndian(header, Fnv1a64(payload));
        AppendLittleEndian(header, uint64_t{0});
        if (header.size() != kStateHeaderSize) {
            SetError(error, "internal savestate header size mismatch");
            return false;
        }

        const auto ioStarted = std::chrono::steady_clock::now();
        std::error_code directoryError;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(),
                                                directoryError);
        }
        if (directoryError) {
            SetError(error, "could not create savestate directory: " +
                                directoryError.message());
            return false;
        }
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary,
                                 std::ios::binary | std::ios::trunc);
            if (!output ||
                !output.write(reinterpret_cast<const char*>(header.data()),
                              static_cast<std::streamsize>(header.size())) ||
                !output.write(reinterpret_cast<const char*>(payload.data()),
                              static_cast<std::streamsize>(payload.size()))) {
                SetError(error, "could not write savestate file");
                return false;
            }
            output.flush();
            if (!output) {
                SetError(error, "could not flush savestate file");
                return false;
            }
        }
        if (!ReplaceStateFile(temporary, path, error)) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return false;
        }
        if (result != nullptr) {
            result->PayloadBytes = payload.size();
            result->FileBytes = header.size() + payload.size();
            result->SemanticFingerprint = semanticFingerprint;
            result->CaptureSeconds = captureSeconds;
            result->EncodeSeconds = encodeSeconds;
            result->IoSeconds = SecondsSince(ioStarted);
        }
        return true;
    } catch (const std::exception& exception) {
        SetError(error,
                 std::string("savestate capture failed: ") +
                     exception.what());
        return false;
    }
}

bool LoadNativeA32State(
    const std::filesystem::path& path,
    const NativeA32SavestateCompatibility& compatibility,
    NativeA32SavestateRuntimeState& runtime,
    NativeA32Process& process,
    NativeA32CtrHostServices& host,
    Oot3dNativePicaFrontend& picaFrontend,
    Oot3dNativePicaSubmissionQueue& submissionQueue,
    NativeA32DspHle& dspHle,
    NativeA32SavestateIoResult* result,
    std::string* error) {
    try {
        if (result != nullptr) {
            *result = {};
        }
        if (!submissionQueue.IsQuiescent()) {
            SetError(error,
                     "savestate load requires a quiescent native PICA boundary");
            return false;
        }
        const auto ioStarted = std::chrono::steady_clock::now();
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            SetError(error, "could not open savestate file: " + path.string());
            return false;
        }
        const std::streamsize fileSize = input.tellg();
        if (fileSize < static_cast<std::streamsize>(kStateHeaderSize) ||
            static_cast<uint64_t>(fileSize) >
                kStateHeaderSize + kMaximumStatePayloadBytes) {
            SetError(error, "savestate file size is invalid");
            return false;
        }
        std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
        input.seekg(0, std::ios::beg);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
            SetError(error, "could not read savestate file");
            return false;
        }
        const auto file = std::span<const uint8_t>(bytes);
        if (!std::equal(kStateMagic.begin(), kStateMagic.end(), file.begin())) {
            SetError(error, "savestate file magic is invalid");
            return false;
        }
        uint32_t version = 0;
        uint32_t headerSize = 0;
        uint64_t payloadSize = 0;
        uint64_t payloadChecksum = 0;
        if (!ReadLittleEndian(file, 8U, version) ||
            !ReadLittleEndian(file, 12U, headerSize) ||
            !ReadLittleEndian(file, 16U, payloadSize) ||
            !ReadLittleEndian(file, 24U, payloadChecksum) ||
            version != kStateContainerVersion ||
            headerSize != kStateHeaderSize || payloadSize == 0U ||
            payloadSize > kMaximumStatePayloadBytes ||
            payloadSize != file.size() - headerSize) {
            SetError(error, "savestate container header is invalid");
            return false;
        }
        const auto payload = file.subspan(headerSize, payloadSize);
        if (Fnv1a64(payload) != payloadChecksum) {
            SetError(error, "savestate payload checksum does not match");
            return false;
        }
        const double ioSeconds = SecondsSince(ioStarted);
        const auto decodeStarted = std::chrono::steady_clock::now();
        const auto document = nlohmann::json::from_msgpack(payload);
        if (!document.is_object() ||
            document.value("format", std::string{}) !=
                "oot3d_native_a32_savestate_v1" ||
            !CompatibilityMatches(document.at("compatibility"),
                                  compatibility, error)) {
            return false;
        }
        const double decodeSeconds = SecondsSince(decodeStarted);
        NativeA32SavestateRuntimeState restoredRuntime;
        const auto& encodedRuntime = document.at("runtime");
        restoredRuntime.FrameCount =
            encodedRuntime.at("frame_count").get<uint32_t>();
        restoredRuntime.RefreshTickRemainder =
            encodedRuntime.at("refresh_tick_remainder").get<uint64_t>();
        restoredRuntime.NextVblankTick =
            encodedRuntime.at("next_vblank_tick").get<uint64_t>();
        restoredRuntime.ProcessRunKind =
            encodedRuntime.at("process_run_kind").get<uint32_t>();
        const auto encodedGameplayClock =
            encodedRuntime.find("gameplay_clock");
        if (encodedGameplayClock != encodedRuntime.end()) {
            restoredRuntime.GameplayClockAvailable =
                encodedGameplayClock->value("available", false);
            const uint32_t timingMode =
                encodedGameplayClock->value("timing_mode", 0U);
            if (timingMode >
                static_cast<uint32_t>(GameplayTimingMode::Enhanced60)) {
                SetError(error,
                         "savestate gameplay timing mode is invalid");
                return false;
            }
            restoredRuntime.GameplayTiming =
                static_cast<GameplayTimingMode>(timingMode);
            restoredRuntime.GameplayClock.SimulationTick =
                encodedGameplayClock->value("simulation_tick", 0ULL);
            restoredRuntime.GameplayClock.PreviousLogicalFrame =
                encodedGameplayClock->value(
                    "previous_logical_frame", 0.0);
            restoredRuntime.GameplayClock.CurrentLogicalFrame =
                encodedGameplayClock->value(
                    "current_logical_frame", 0.0);
        }
        const auto encodedPresentationScheduler =
            encodedRuntime.find("presentation_scheduler");
        if (encodedPresentationScheduler != encodedRuntime.end()) {
            restoredRuntime.PresentationSchedulerAvailable =
                encodedPresentationScheduler->value("available", false);
            restoredRuntime.PresentationScheduler.GuestRefreshPhase =
                encodedPresentationScheduler->value(
                    "guest_refresh_phase", 0.0);
            restoredRuntime.PresentationScheduler.VisualSamplePhase =
                encodedPresentationScheduler->value(
                    "visual_sample_phase", 0.0);
            restoredRuntime.PresentationScheduler.Started =
                encodedPresentationScheduler->value("started", false);
        }
        const auto encodedFrameRatePolicyTemporalState =
            encodedRuntime.find("frame_rate_policy_temporal_state");
        if (encodedFrameRatePolicyTemporalState != encodedRuntime.end()) {
            restoredRuntime.FrameRatePolicyTemporalStateAvailable =
                encodedFrameRatePolicyTemporalState->value(
                    "available", false);
            restoredRuntime.FrameRatePolicyTemporalState
                .FramePacingStateAddress =
                encodedFrameRatePolicyTemporalState->value(
                    "frame_pacing_state_address", 0U);
            restoredRuntime.FrameRatePolicyTemporalState
                .FramePacingDecisionPending =
                encodedFrameRatePolicyTemporalState->value(
                    "frame_pacing_decision_pending", true);
        }
        const auto encodedTopScreenTemporalState =
            encodedRuntime.find("topscreen_temporal_state");
        if (encodedTopScreenTemporalState != encodedRuntime.end()) {
            auto& state = restoredRuntime.TopScreenTemporalState;
            restoredRuntime.TopScreenTemporalStateAvailable =
                encodedTopScreenTemporalState->value("available", false);
            state.ProfileActive = encodedTopScreenTemporalState->value(
                "profile_active", false);
            state.PausePageRedrawActive =
                encodedTopScreenTemporalState->value(
                    "pause_page_redraw_active", false);
            state.PausePageRedrawDelayCalls =
                encodedTopScreenTemporalState->value(
                    "pause_page_redraw_delay_calls", uint16_t{0});
            state.PauseDrawNativeTransitionLatched =
                encodedTopScreenTemporalState->value(
                    "pause_draw_native_transition_latched", false);
            state.PauseDrawSuppressionDelayArmed =
                encodedTopScreenTemporalState->value(
                    "pause_draw_suppression_delay_armed", false);
            state.PauseDrawSuppressionDelayCommands =
                encodedTopScreenTemporalState->value(
                    "pause_draw_suppression_delay_commands", uint16_t{0});
            state.PauseRoutePreviousRuntimeMode =
                encodedTopScreenTemporalState->value(
                    "pause_route_previous_runtime_mode", 0U);
            state.PauseRouteTransitionPhase =
                encodedTopScreenTemporalState->value(
                    "pause_route_transition_phase", uint8_t{0});
            state.PauseRouteRemainingCalls =
                encodedTopScreenTemporalState->value(
                    "pause_route_remaining_calls", uint16_t{0});
            state.AotPauseRoutePreviousRuntimeMode =
                encodedTopScreenTemporalState->value(
                    "aot_pause_route_previous_runtime_mode", 0U);
            state.AotPauseRouteTransitionPhase =
                encodedTopScreenTemporalState->value(
                    "aot_pause_route_transition_phase", uint8_t{0});
            state.AotPauseRouteRemainingCalls =
                encodedTopScreenTemporalState->value(
                    "aot_pause_route_remaining_calls", uint16_t{0});
            state.TouchCoordinateRuntimeSceneLatch =
                encodedTopScreenTemporalState->value(
                    "touch_coordinate_runtime_scene_latch", false);
        }
        const auto encodedPicaVisualReplayState =
            encodedRuntime.find("native_pica_visual_replay_state");
        if (encodedPicaVisualReplayState != encodedRuntime.end()) {
            restoredRuntime.PicaVisualReplayStateAvailable =
                encodedPicaVisualReplayState->value("available", false);
            const auto& payload =
                encodedPicaVisualReplayState->at("payload");
            if (!payload.is_binary() ||
                payload.get_binary().size() >
                    kMaximumPicaVisualReplayStateBytes) {
                SetError(error,
                         "savestate native PICA visual replay payload is invalid");
                return false;
            }
            restoredRuntime.PicaVisualReplayState.assign(
                payload.get_binary().begin(),
                payload.get_binary().end());
            if (restoredRuntime.PicaVisualReplayStateAvailable !=
                    !restoredRuntime.PicaVisualReplayState.empty()) {
                SetError(error,
                         "savestate native PICA visual replay state is incomplete");
                return false;
            }
        }
        const auto encodedPicaTextureCache =
            encodedRuntime.find("native_pica_texture_cache");
        if (encodedPicaTextureCache != encodedRuntime.end()) {
            restoredRuntime.PicaTextureCacheAvailable =
                encodedPicaTextureCache->value("available", false);
            const auto& entries =
                encodedPicaTextureCache->at("entries");
            if (!entries.is_array() ||
                entries.size() > kMaximumPicaTextureCacheEntryCount) {
                SetError(error,
                         "savestate native PICA texture cache list is invalid");
                return false;
            }
            uint64_t totalBytes = 0U;
            std::set<PicaTextureSnapshotIdentity> identities;
            for (const auto& encoded : entries) {
                Oot3d::Renderer::PicaTextureCacheEntrySnapshot texture;
                texture.ContentHash =
                    encoded.at("content_hash").get<uint64_t>();
                texture.ReplacementGeneration = encoded.at(
                    "replacement_generation").get<uint64_t>();
                texture.PhysicalAddress =
                    encoded.at("physical_address").get<uint32_t>();
                texture.SourceWidth =
                    encoded.at("source_width").get<uint16_t>();
                texture.SourceHeight =
                    encoded.at("source_height").get<uint16_t>();
                texture.NativeFormat =
                    encoded.at("native_format").get<uint8_t>();
                texture.NativeType =
                    encoded.at("native_type").get<uint8_t>();
                texture.NativeWrapS =
                    encoded.at("native_wrap_s").get<uint8_t>();
                texture.NativeWrapT =
                    encoded.at("native_wrap_t").get<uint8_t>();
                texture.MinLinear =
                    encoded.at("min_linear").get<bool>();
                texture.MagLinear =
                    encoded.at("mag_linear").get<bool>();
                texture.MipLinear =
                    encoded.value("mip_linear", false);
                texture.LodBiasRaw =
                    encoded.value("lod_bias_raw", int16_t{0});
                texture.MinMipLevel =
                    encoded.value("min_mip_level", uint8_t{0});
                texture.MaxMipLevel =
                    encoded.value("max_mip_level", uint8_t{0});
                texture.CustomReplacement =
                    encoded.at("custom_replacement").get<bool>();
                texture.ImageWidth =
                    encoded.at("image_width").get<uint32_t>();
                texture.ImageHeight =
                    encoded.at("image_height").get<uint32_t>();
                texture.MipLevels =
                    encoded.value("mip_levels", uint8_t{1});
                const uint8_t imageFormat =
                    encoded.at("image_format").get<uint8_t>();
                texture.ImageFormat = static_cast<
                    Oot3d::Renderer::PicaTextureSnapshotFormat>(
                        imageFormat);
                texture.CustomReplacementHash = encoded.at(
                    "custom_replacement_hash").get<uint64_t>();
                texture.CustomReplacementPending = encoded.at(
                    "custom_replacement_pending").get<bool>();
                texture.CustomReplacementReady = encoded.at(
                    "custom_replacement_ready").get<bool>();
                const auto& pixels = encoded.at("pixel_bytes");
                if (!pixels.is_binary()) {
                    SetError(error,
                             "savestate native PICA texture payload is invalid");
                    return false;
                }
                texture.PixelBytes.assign(pixels.get_binary().begin(),
                                          pixels.get_binary().end());
                const auto expectedBytes = PicaDecodedMipChainBytes(
                    texture.ImageWidth, texture.ImageHeight,
                    texture.MipLevels);
                totalBytes += texture.PixelBytes.size();
                if (texture.SourceWidth == 0U ||
                    texture.SourceHeight == 0U ||
                    texture.ImageWidth == 0U ||
                    texture.ImageHeight == 0U ||
                    texture.MipLevels == 0U ||
                    !expectedBytes.has_value() ||
                    imageFormat > static_cast<uint8_t>(
                                      Oot3d::Renderer::
                                          PicaTextureSnapshotFormat::R32Uint) ||
                    (texture.ImageFormat ==
                             Oot3d::Renderer::
                                 PicaTextureSnapshotFormat::R32Uint &&
                     (texture.NativeType != 2U ||
                      texture.CustomReplacement)) ||
                    (texture.ImageFormat ==
                             Oot3d::Renderer::
                                 PicaTextureSnapshotFormat::R32Uint &&
                     texture.MipLevels != 1U) ||
                    *expectedBytes != texture.PixelBytes.size() ||
                    totalBytes > kMaximumPicaTextureCacheBytes ||
                    !identities
                         .insert({texture.ContentHash,
                                  texture.ReplacementGeneration,
                                  texture.PhysicalAddress,
                                  texture.SourceWidth,
                                  texture.SourceHeight,
                                  texture.NativeFormat,
                                  texture.NativeType,
                                  texture.NativeWrapS,
                                  texture.NativeWrapT,
                                  texture.MinLinear,
                                  texture.MagLinear,
                                  texture.MipLinear,
                                  texture.LodBiasRaw,
                                  texture.MinMipLevel,
                                  texture.MaxMipLevel,
                                  texture.CustomReplacement})
                         .second) {
                    SetError(error,
                             "savestate native PICA cached texture is invalid");
                    return false;
                }
                restoredRuntime.PicaTextureCache.push_back(
                    std::move(texture));
            }
        }
        const auto encodedPicaColorTargets =
            encodedRuntime.find("native_pica_color_targets");
        if (encodedPicaColorTargets != encodedRuntime.end()) {
            restoredRuntime.PicaColorTargetsAvailable =
                encodedPicaColorTargets->value("available", false);
            const auto& targets = encodedPicaColorTargets->at("targets");
            if (!targets.is_array() ||
                targets.size() > kMaximumPicaColorTargetCount) {
                SetError(error,
                         "savestate native PICA color target list is invalid");
                return false;
            }
            uint64_t totalBytes = 0U;
            std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint16_t,
                                uint16_t, uint8_t, uint8_t, uint16_t>>
                identities;
            for (const auto& encoded : targets) {
                Oot3d::Renderer::PicaRenderTargetColorSnapshot target;
                target.RenderTargetNamespace = encoded.at(
                    "render_target_namespace").get<uint64_t>();
                target.ColorPhysicalAddress = encoded.at(
                    "color_physical_address").get<uint32_t>();
                target.DepthPhysicalAddress = encoded.at(
                    "depth_physical_address").get<uint32_t>();
                target.FramebufferWidth = encoded.at(
                    "framebuffer_width").get<uint16_t>();
                target.FramebufferHeight = encoded.at(
                    "framebuffer_height").get<uint16_t>();
                target.FramebufferColorFormat = encoded.at(
                    "framebuffer_color_format").get<uint8_t>();
                target.FramebufferDepthFormat = encoded.at(
                    "framebuffer_depth_format").get<uint8_t>();
                target.RenderScalePermille = encoded.at(
                    "render_scale_permille").get<uint16_t>();
                target.ImageWidth =
                    encoded.at("image_width").get<uint32_t>();
                target.ImageHeight =
                    encoded.at("image_height").get<uint32_t>();
                target.SampleCount =
                    encoded.at("sample_count").get<uint8_t>();
                target.WBuffering = encoded.value("w_buffering", false);
                const auto& color = encoded.at("color_rgba8");
                if (!color.is_binary()) {
                    SetError(error,
                             "savestate native PICA color payload is invalid");
                    return false;
                }
                target.ColorRgba8.assign(color.get_binary().begin(),
                                         color.get_binary().end());
                const uint64_t expectedBytes =
                    static_cast<uint64_t>(target.ImageWidth) *
                    target.ImageHeight * 4U;
                const uint64_t expectedPixels =
                    static_cast<uint64_t>(target.ImageWidth) *
                    target.ImageHeight;
                const uint64_t expectedMotionBytes = expectedPixels * 8U;
                const std::array<const char*, 5> guideNames{
                    "normal_guide_rgba8",
                    "material_guide_rgba8",
                    "rigid_motion_guide_rgba16_float_le",
                    "ambient_guide_rgba8",
                    "shadow_r32_uint_le"};
                const size_t guideFieldCount = std::count_if(
                    guideNames.begin(), guideNames.end(),
                    [&](const char* name) {
                        return encoded.contains(name);
                    });
                if (guideFieldCount == 0U) {
                    target.NormalGuideRgba8.resize(
                        static_cast<size_t>(expectedBytes));
                    target.MaterialGuideRgba8.assign(
                        static_cast<size_t>(expectedBytes), 0U);
                    target.RigidMotionGuideRgba16FloatLe.assign(
                        static_cast<size_t>(expectedMotionBytes), 0U);
                    target.AmbientGuideRgba8.resize(
                        static_cast<size_t>(expectedBytes));
                    target.ShadowR32UintLe.assign(
                        static_cast<size_t>(expectedBytes), 0xFFU);
                    for (size_t index = 0;
                         index < static_cast<size_t>(expectedPixels);
                         ++index) {
                        const size_t offset = index * 4U;
                        target.NormalGuideRgba8[offset + 0U] = 0x80U;
                        target.NormalGuideRgba8[offset + 1U] = 0x80U;
                        target.NormalGuideRgba8[offset + 2U] = 0xFFU;
                        target.NormalGuideRgba8[offset + 3U] = 0U;
                        target.AmbientGuideRgba8[offset + 0U] = 0xFFU;
                        target.AmbientGuideRgba8[offset + 1U] = 0xFFU;
                        target.AmbientGuideRgba8[offset + 2U] = 0xFFU;
                        target.AmbientGuideRgba8[offset + 3U] = 0U;
                    }
                } else if (guideFieldCount != guideNames.size()) {
                    SetError(error,
                             "savestate native PICA guide payload is incomplete");
                    return false;
                } else {
                    const auto loadBinary =
                        [&](const char* name,
                            std::vector<uint8_t>& destination) {
                            const auto& payload = encoded.at(name);
                            if (!payload.is_binary()) {
                                return false;
                            }
                            destination.assign(
                                payload.get_binary().begin(),
                                payload.get_binary().end());
                            return true;
                        };
                    if (!loadBinary("normal_guide_rgba8",
                                    target.NormalGuideRgba8) ||
                        !loadBinary("material_guide_rgba8",
                                    target.MaterialGuideRgba8) ||
                        !loadBinary(
                            "rigid_motion_guide_rgba16_float_le",
                            target.RigidMotionGuideRgba16FloatLe) ||
                        !loadBinary("ambient_guide_rgba8",
                                    target.AmbientGuideRgba8) ||
                        !loadBinary("shadow_r32_uint_le",
                                    target.ShadowR32UintLe)) {
                        SetError(error,
                                 "savestate native PICA guide payload is invalid");
                        return false;
                    }
                }
                bool validDepthValues = true;
                uint64_t depthPayloadBytes = 0U;
                const auto encodedDepth =
                    encoded.find("depth_float32_le");
                const auto encodedStencil = encoded.find("stencil_u8");
                if (encodedDepth == encoded.end() &&
                    encodedStencil == encoded.end()) {
                    target.DepthValues.assign(
                        static_cast<size_t>(expectedPixels), 1.0F);
                } else if (encodedDepth == encoded.end() ||
                           encodedStencil == encoded.end() ||
                           !encodedDepth->is_binary() ||
                           !encodedStencil->is_binary()) {
                    SetError(error,
                             "savestate native PICA depth payload is invalid");
                    return false;
                } else {
                    const auto& depthBytes = encodedDepth->get_binary();
                    const auto& stencilBytes =
                        encodedStencil->get_binary();
                    validDepthValues =
                        depthBytes.size() == expectedBytes;
                    target.DepthValues.resize(
                        static_cast<size_t>(expectedPixels));
                    const std::span<const uint8_t> depthSpan(
                        depthBytes.data(), depthBytes.size());
                    for (size_t index = 0;
                         index < target.DepthValues.size(); ++index) {
                        uint32_t bits = 0U;
                        if (!ReadLittleEndian(
                                depthSpan, index * sizeof(uint32_t),
                                bits)) {
                            validDepthValues = false;
                            break;
                        }
                        std::memcpy(&target.DepthValues[index], &bits,
                                    sizeof(bits));
                        if (!std::isfinite(target.DepthValues[index]) ||
                            target.DepthValues[index] < 0.0F ||
                            target.DepthValues[index] > 1.0F) {
                            validDepthValues = false;
                            break;
                        }
                    }
                    target.StencilValues.assign(stencilBytes.begin(),
                                                stencilBytes.end());
                    depthPayloadBytes = depthBytes.size() +
                                        stencilBytes.size();
                }
                totalBytes += target.ColorRgba8.size() +
                              target.NormalGuideRgba8.size() +
                              target.MaterialGuideRgba8.size() +
                              target.RigidMotionGuideRgba16FloatLe.size() +
                              target.AmbientGuideRgba8.size() +
                              target.ShadowR32UintLe.size() +
                              depthPayloadBytes;
                if (target.ColorPhysicalAddress == 0U ||
                    target.FramebufferWidth == 0U ||
                    target.FramebufferHeight == 0U ||
                    target.ImageWidth == 0U || target.ImageHeight == 0U ||
                    target.SampleCount != 1U ||
                    expectedBytes != target.ColorRgba8.size() ||
                    expectedBytes != target.NormalGuideRgba8.size() ||
                    expectedBytes != target.MaterialGuideRgba8.size() ||
                    expectedMotionBytes !=
                        target.RigidMotionGuideRgba16FloatLe.size() ||
                    expectedBytes != target.AmbientGuideRgba8.size() ||
                    expectedBytes != target.ShadowR32UintLe.size() ||
                    !validDepthValues ||
                    (!target.StencilValues.empty() &&
                     target.StencilValues.size() != expectedPixels) ||
                    totalBytes > kMaximumPicaColorTargetBytes ||
                    !identities
                         .insert({target.RenderTargetNamespace,
                                  target.ColorPhysicalAddress,
                                  target.DepthPhysicalAddress,
                                  target.FramebufferWidth,
                                  target.FramebufferHeight,
                                  target.FramebufferColorFormat,
                                  target.FramebufferDepthFormat,
                                  target.RenderScalePermille})
                         .second) {
                    SetError(error,
                             "savestate native PICA color target is invalid");
                    return false;
                }
                restoredRuntime.PicaColorTargets.push_back(
                    std::move(target));
            }
        }
        const auto encodedPicaPresentationState =
            encodedRuntime.find("native_pica_presentation_state");
        if (encodedPicaPresentationState != encodedRuntime.end()) {
            restoredRuntime.PicaPresentationStateAvailable =
                encodedPicaPresentationState->value("available", false);
            const auto& images =
                encodedPicaPresentationState->at("display_images");
            if (!images.is_array() ||
                images.size() > kMaximumPicaDisplayImageCount) {
                SetError(error,
                         "savestate native PICA display image list is invalid");
                return false;
            }
            std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint16_t,
                                uint16_t, uint8_t, uint8_t, uint16_t>>
                targetIdentities;
            for (const auto& target : restoredRuntime.PicaColorTargets) {
                targetIdentities.insert(
                    {target.RenderTargetNamespace,
                     target.ColorPhysicalAddress,
                     target.DepthPhysicalAddress,
                     target.FramebufferWidth,
                     target.FramebufferHeight,
                     target.FramebufferColorFormat,
                     target.FramebufferDepthFormat,
                     target.RenderScalePermille});
            }
            uint64_t totalBytes = 0U;
            std::set<std::pair<uint64_t, uint32_t>> identities;
            for (const auto& encoded : images) {
                Oot3d::Renderer::PicaDisplayImageColorSnapshot image;
                image.RenderTargetNamespace = encoded.at(
                    "render_target_namespace").get<uint64_t>();
                image.OutputPhysicalAddress = encoded.at(
                    "output_physical_address").get<uint32_t>();
                image.ImageWidth =
                    encoded.at("image_width").get<uint32_t>();
                image.ImageHeight =
                    encoded.at("image_height").get<uint32_t>();
                image.Initialized =
                    encoded.at("initialized").get<bool>();
                const auto& color = encoded.at("color_rgba8");
                if (!color.is_binary()) {
                    SetError(error,
                             "savestate native PICA display payload is invalid");
                    return false;
                }
                image.ColorRgba8.assign(color.get_binary().begin(),
                                        color.get_binary().end());
                image.HasDepthTarget =
                    encoded.at("has_depth_target").get<bool>();
                image.DepthTargetNamespace = encoded.at(
                    "depth_target_namespace").get<uint64_t>();
                image.DepthTargetColorPhysicalAddress = encoded.at(
                    "depth_target_color_physical_address").get<uint32_t>();
                image.DepthTargetDepthPhysicalAddress = encoded.at(
                    "depth_target_depth_physical_address").get<uint32_t>();
                image.DepthTargetFramebufferWidth = encoded.at(
                    "depth_target_framebuffer_width").get<uint16_t>();
                image.DepthTargetFramebufferHeight = encoded.at(
                    "depth_target_framebuffer_height").get<uint16_t>();
                image.DepthTargetFramebufferColorFormat = encoded.at(
                    "depth_target_framebuffer_color_format").get<uint8_t>();
                image.DepthTargetFramebufferDepthFormat = encoded.at(
                    "depth_target_framebuffer_depth_format").get<uint8_t>();
                image.DepthTargetRenderScalePermille = encoded.at(
                    "depth_target_render_scale_permille").get<uint16_t>();
                const uint64_t expectedBytes =
                    static_cast<uint64_t>(image.ImageWidth) *
                    image.ImageHeight * 4U;
                totalBytes += image.ColorRgba8.size();
                const bool validDepthTarget =
                    !image.HasDepthTarget ||
                    targetIdentities.contains(
                        {image.DepthTargetNamespace,
                         image.DepthTargetColorPhysicalAddress,
                         image.DepthTargetDepthPhysicalAddress,
                         image.DepthTargetFramebufferWidth,
                         image.DepthTargetFramebufferHeight,
                         image.DepthTargetFramebufferColorFormat,
                         image.DepthTargetFramebufferDepthFormat,
                         image.DepthTargetRenderScalePermille});
                if (image.OutputPhysicalAddress == 0U ||
                    image.ImageWidth == 0U || image.ImageHeight == 0U ||
                    (image.Initialized
                         ? expectedBytes != image.ColorRgba8.size()
                         : !image.ColorRgba8.empty()) ||
                    totalBytes > kMaximumPicaDisplayImageBytes ||
                    !validDepthTarget ||
                    !identities
                         .insert({image.RenderTargetNamespace,
                                  image.OutputPhysicalAddress})
                         .second) {
                    SetError(error,
                             "savestate native PICA display image is invalid");
                    return false;
                }
                restoredRuntime.PicaPresentationState.DisplayImages.push_back(
                    std::move(image));
            }
            const auto& encodedLast =
                encodedPicaPresentationState->at(
                    "last_presented_transfer");
            if (encodedLast.value("available", false)) {
                Oot3d::Renderer::PicaDisplayTransferView transfer;
                transfer.CompletionId =
                    encodedLast.at("completion_id").get<uint64_t>();
                transfer.RenderTargetNamespace = encodedLast.at(
                    "render_target_namespace").get<uint64_t>();
                transfer.InputPhysicalAddress = encodedLast.at(
                    "input_physical_address").get<uint32_t>();
                transfer.OutputPhysicalAddress = encodedLast.at(
                    "output_physical_address").get<uint32_t>();
                transfer.InputWidth =
                    encodedLast.at("input_width").get<uint16_t>();
                transfer.InputHeight =
                    encodedLast.at("input_height").get<uint16_t>();
                transfer.OutputWidth =
                    encodedLast.at("output_width").get<uint16_t>();
                transfer.OutputHeight =
                    encodedLast.at("output_height").get<uint16_t>();
                transfer.Flags = encodedLast.at("flags").get<uint32_t>();
                transfer.Present = encodedLast.at("present").get<bool>();
                const uint8_t presentationMode = encodedLast.at(
                    "presentation_mode").get<uint8_t>();
                transfer.PresentationMode = static_cast<
                    Oot3d::Renderer::PicaPresentationMode>(
                        presentationMode);
                const auto display = std::find_if(
                    restoredRuntime.PicaPresentationState
                        .DisplayImages.begin(),
                    restoredRuntime.PicaPresentationState
                        .DisplayImages.end(),
                    [&](const auto& image) {
                        return image.RenderTargetNamespace ==
                                   transfer.RenderTargetNamespace &&
                               image.OutputPhysicalAddress ==
                                   transfer.OutputPhysicalAddress &&
                               image.Initialized;
                    });
                if (transfer.CompletionId == 0U ||
                    transfer.InputPhysicalAddress == 0U ||
                    transfer.OutputPhysicalAddress == 0U ||
                    transfer.InputWidth == 0U ||
                    transfer.InputHeight == 0U ||
                    transfer.OutputWidth == 0U ||
                    transfer.OutputHeight == 0U ||
                    presentationMode > 1U ||
                    display == restoredRuntime.PicaPresentationState
                                   .DisplayImages.end()) {
                    SetError(error,
                             "savestate native PICA last presentation is invalid");
                    return false;
                }
                restoredRuntime.PicaPresentationState
                    .LastPresentedTransfer = transfer;
            }
        }
        if (restoredRuntime.RefreshTickRemainder >= 60U ||
            restoredRuntime.ProcessRunKind > 3U ||
            restoredRuntime.TopScreenTemporalState
                    .PauseRouteTransitionPhase > 2U ||
            restoredRuntime.TopScreenTemporalState
                    .AotPauseRouteTransitionPhase > 2U ||
            !std::isfinite(
                restoredRuntime.GameplayClock.PreviousLogicalFrame) ||
            !std::isfinite(
                restoredRuntime.GameplayClock.CurrentLogicalFrame) ||
            restoredRuntime.GameplayClock.PreviousLogicalFrame < 0.0 ||
            restoredRuntime.GameplayClock.CurrentLogicalFrame <
                restoredRuntime.GameplayClock.PreviousLogicalFrame ||
            !std::isfinite(
                restoredRuntime.PresentationScheduler.GuestRefreshPhase) ||
            !std::isfinite(
                restoredRuntime.PresentationScheduler.VisualSamplePhase)) {
            SetError(error, "savestate runtime clock state is invalid");
            return false;
        }

        const auto restoreStarted = std::chrono::steady_clock::now();
        const auto rollbackProcess = process.CaptureState();
        const auto rollbackHost = host.CaptureState();
        const auto rollbackFrontend = picaFrontend.CaptureState();
        const auto rollbackSubmission = submissionQueue.CaptureState();
        const auto rollbackDsp = dspHle.CaptureState();
        const auto rollback = [&]() {
            std::string ignored;
            process.RestoreState(rollbackProcess, &ignored);
            host.RestoreState(rollbackHost, process, &ignored);
            picaFrontend.RestoreState(rollbackFrontend, &ignored);
            submissionQueue.RestoreState(rollbackSubmission, &ignored);
            dspHle.RestoreState(rollbackDsp, &ignored);
        };

        if (!process.RestoreState(document.at("process"), error) ||
            !host.RestoreState(document.at("ctr_host"), process, error) ||
            !picaFrontend.RestoreState(document.at("pica_frontend"), error) ||
            !submissionQueue.RestoreState(document.at("pica_submission"),
                                          error) ||
            !dspHle.RestoreState(document.at("dsp_hle"), error)) {
            rollback();
            return false;
        }
        runtime = restoredRuntime;
        if (result != nullptr) {
            result->PayloadBytes = payloadSize;
            result->FileBytes = file.size();
            result->IoSeconds = ioSeconds;
            result->DecodeSeconds = decodeSeconds;
            result->RestoreSeconds = SecondsSince(restoreStarted);
        }
        return true;
    } catch (const std::exception& exception) {
        SetError(error,
                 std::string("savestate load failed: ") + exception.what());
        return false;
    }
}

} // namespace Oot3dNativeGame
