#include "oot3d_game_module_runtime.h"

#include "oot3d_game_module_ui_profile.h"

#include "oot3d_native_a32_ctr_host.h"
#include "oot3d_native_a32_dsp_memory_adapter.h"
#include "oot3d_native_a32_process_image.h"
#include "oot3d_native_a32_registry.h"
#include "triaevum/audio_service_client.h"
#include "triaevum/filesystem_service_client.h"
#include "triaevum/input_service_client.h"
#include "triaevum/pica_service_client.h"
#include "triaevum/sha256.h"
#include "triaevum_ctr_filesystem_client_bridge.h"
#include "triaevum_oot3d_pica_client_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

using Json = nlohmann::json;

constexpr uint64_t kArm11TicksPerSecond = 268111856ULL;
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000ULL;
constexpr uint64_t kDisplayRefreshRate = 60ULL;
constexpr uint64_t kMaximumSimulationStepNs = kNanosecondsPerSecond;
constexpr uint32_t kWholeAotBlockBudget = 1'000'000U;
constexpr uint32_t kMaximumDispatchSlices = 4096U;
constexpr size_t kMaximumProcessManifestBytes = 4U * 1024U * 1024U;
constexpr size_t kMaximumCodeBytes = 64U * 1024U * 1024U;
constexpr size_t kMaximumMemoryLeases = 4096U;

static_assert(NativeA32HidButtonMask(NativeA32HidButton::A) ==
              TRIAEVUM_INPUT_BUTTON_A_V1);
static_assert(NativeA32HidButtonMask(NativeA32HidButton::Start) ==
              TRIAEVUM_INPUT_BUTTON_START_V1);
static_assert(NativeA32HidButtonMask(NativeA32HidButton::DpadRight) ==
              TRIAEVUM_INPUT_BUTTON_DPAD_RIGHT_V1);
static_assert(NativeA32HidButtonMask(NativeA32HidButton::L) ==
              TRIAEVUM_INPUT_BUTTON_L_V1);
static_assert(NativeA32HidButtonMask(NativeA32HidButton::Y) ==
              TRIAEVUM_INPUT_BUTTON_Y_V1);
static_assert(NativeA32HidButtonMask(NativeA32HidButton::Zr) ==
              TRIAEVUM_INPUT_BUTTON_ZR_V1);

int16_t ScaleInputAxis(float value) noexcept {
  return static_cast<int16_t>(std::lround(
      std::clamp(value, -1.0F, 1.0F) *
      static_cast<float>(ThreeDsRecomp::Input::kNativeStickMaximum)));
}

uint16_t ScaleTouchAxis(float value, uint16_t extent) noexcept {
  return static_cast<uint16_t>(std::lround(std::clamp(value, 0.0F, 1.0F) *
                                           static_cast<float>(extent - 1U)));
}

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

std::string Hex(std::span<const uint8_t> bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result(bytes.size() * 2U, '0');
  for (size_t index = 0; index < bytes.size(); ++index) {
    result[index * 2U] = digits[bytes[index] >> 4U];
    result[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  return result;
}

bool IsSha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

struct VerifiedModuleContent {
  NativeA32ProcessImageManifest Manifest;
  std::vector<uint8_t> CodeBytes;
};

struct IndexedInput {
  uint64_t Bytes = 0U;
  std::string Sha256;
};

const Json &RequiredObject(const Json &parent, const char *field) {
  const auto &value = parent.at(field);
  if (!value.is_object()) {
    throw std::runtime_error(std::string(field) + " must be an object");
  }
  return value;
}

uint64_t RequiredUnsigned(const Json &parent, const char *field) {
  const auto &value = parent.at(field);
  if (!value.is_number_unsigned()) {
    throw std::runtime_error(std::string(field) + " must be unsigned");
  }
  return value.get<uint64_t>();
}

std::string RequiredString(const Json &parent, const char *field) {
  const auto &value = parent.at(field);
  if (!value.is_string()) {
    throw std::runtime_error(std::string(field) + " must be a string");
  }
  return value.get<std::string>();
}

IndexedInput ReadIndexedInput(const Json &input) {
  RequiredString(input, "path");
  IndexedInput indexed{RequiredUnsigned(input, "bytes"),
                       RequiredString(input, "sha256")};
  if (indexed.Bytes == 0U || !IsSha256(indexed.Sha256)) {
    throw std::runtime_error("private input descriptor is invalid");
  }
  return indexed;
}

std::vector<uint8_t>
ReadContentFile(triaevum::module::FilesystemServiceClientV1 &filesystem,
                std::string_view logicalPath, uint64_t maximumBytes) {
  std::vector<uint8_t> bytes;
  if (filesystem.ReadAll(TRIAEVUM_FILESYSTEM_CONTENT_V1, logicalPath,
                         maximumBytes, &bytes) != TRIAEVUM_MODULE_OK_V1 ||
      bytes.empty()) {
    throw std::runtime_error("private content file is unavailable: " +
                             std::string(logicalPath));
  }
  return bytes;
}

VerifiedModuleContent VerifyContent(const TriAevumHostApiV1 &host,
                                    std::string_view encoded) {
  triaevum::module::FilesystemServiceClientV1 filesystem(&host);
  if (!filesystem.IsAvailable()) {
    throw std::runtime_error("TriAevum filesystem host service is unavailable");
  }
  Json content = Json::parse(encoded.begin(), encoded.end());
  if (!content.is_object() ||
      content.value("format", "") != "triaevum_content_index_v1" ||
      content.value("private_local_artifact", false) != true ||
      content.value("redistributable", true) != false) {
    throw std::runtime_error("unsupported private content index");
  }
  const Json &inputs = RequiredObject(content, "inputs");
  const Json &codeInput = RequiredObject(inputs, "code");
  const Json &exheaderInput = RequiredObject(inputs, "exheader");
  const Json &romfsInput = RequiredObject(inputs, "romfs");
  const Json &processDescriptor = RequiredObject(content, "process_manifest");
  const IndexedInput indexedProcess = ReadIndexedInput(processDescriptor);
  const IndexedInput indexedCode = ReadIndexedInput(codeInput);
  const IndexedInput indexedExheader = ReadIndexedInput(exheaderInput);
  const IndexedInput indexedRomFs = ReadIndexedInput(romfsInput);
  const auto processBytes = ReadContentFile(filesystem, "process_manifest",
                                            kMaximumProcessManifestBytes);
  if (processBytes.size() != indexedProcess.Bytes ||
      Hex(triaevum::module::detail::Sha256(processBytes)) !=
          indexedProcess.Sha256) {
    throw std::runtime_error("process manifest does not match content.tap");
  }

  const Json processDocument = Json::parse(processBytes);
  const Json &processSource = RequiredObject(processDocument, "source");
  const std::string codeHash = RequiredString(processSource, "code_bin_sha256");
  const std::string exheaderHash =
      RequiredString(processSource, "exheader_sha256");
  if (codeHash != Hex(kOot3dGameModuleIdentity) ||
      indexedCode.Sha256 != codeHash ||
      indexedExheader.Sha256 != exheaderHash) {
    throw std::runtime_error(
        "content code identity does not match this game module");
  }

  std::string manifestError;
  auto manifest =
      LoadNativeA32ProcessImageManifest(processBytes, &manifestError);
  if (!manifest.has_value()) {
    throw std::runtime_error("invalid process manifest: " + manifestError);
  }
  RequiredString(processSource, "exheader_path");
  auto codeBytes =
      ReadContentFile(filesystem, "inputs/code", kMaximumCodeBytes);
  const auto exheaderBytes = ReadContentFile(filesystem, "inputs/exheader",
                                             kMaximumProcessManifestBytes);
  triaevum::module::FilesystemStatV1 romfsStat;
  if (codeBytes.size() != indexedCode.Bytes ||
      codeBytes.size() != manifest->CodeBinSize ||
      Hex(triaevum::module::detail::Sha256(codeBytes)) != codeHash) {
    throw std::runtime_error("code.bin changed after Forge verification");
  }
  if (exheaderBytes.size() != indexedExheader.Bytes ||
      Hex(triaevum::module::detail::Sha256(exheaderBytes)) != exheaderHash) {
    throw std::runtime_error("exheader changed after Forge verification");
  }
  if (filesystem.Stat(TRIAEVUM_FILESYSTEM_CONTENT_V1, "inputs/romfs",
                      &romfsStat) != TRIAEVUM_MODULE_OK_V1 ||
      romfsStat.flags != TRIAEVUM_FILESYSTEM_STAT_REGULAR_FILE_V1 ||
      romfsStat.size != indexedRomFs.Bytes ||
      romfsStat.size != manifest->RomFsImageFileSize) {
    throw std::runtime_error("RomFS changed after Forge verification");
  }
  return {std::move(*manifest), std::move(codeBytes)};
}

NativeA32CtrHostConfig
BuildHostConfig(const NativeA32ProcessImageManifest &manifest,
                NativeA32CtrPicaBridge *picaBridge,
                NativeA32CtrFilesystem *filesystem) {
  NativeA32CtrHostConfig config;
  config.ResourceLimitValues = manifest.ResourceLimitValues;
  config.ResourceCurrentValues = manifest.ResourceCurrentValues;
  config.LinearHeapBaseAddress = manifest.LinearHeapBaseAddress;
  config.LinearHeapSize = manifest.LinearHeapSize;
  config.HeapBaseAddress = manifest.HeapBaseAddress;
  config.HeapSize = manifest.HeapSize;
  config.RomFsImageOffset = manifest.RomFsImageOffset;
  config.RomFsImageSize = manifest.RomFsImageSize;
  config.Filesystem = filesystem;
  config.PicaBridge = picaBridge;
  return config;
}

} // namespace

struct Oot3dGameModuleRuntime::Impl {
  struct MemoryLease {
    uint32_t Address = 0U;
    uint32_t Bytes = 0U;
    TriAevumGuestMemoryAccessV1 Access = 0U;
  };

  Impl(const TriAevumHostApiV1 &host, VerifiedModuleContent content)
      : Host(host), Manifest(std::move(content.Manifest)),
        CodeBytes(std::move(content.CodeBytes)), PicaClient(&Host),
        AudioClient(&Host), InputClient(&Host), FilesystemClient(&Host),
        PicaBridge(PicaClient), FilesystemBridge(FilesystemClient),
        HostServices(BuildHostConfig(Manifest, &PicaBridge, &FilesystemBridge)),
        Process(Oot3dNativeA32Registry(), HostServices),
        DspHle(BuildNativeA32DspPhysicalRegions(Manifest)) {}

  bool Mount(std::string *error) {
    if (!PicaClient.IsAvailable()) {
      SetError(error, "TriAevum PICA host service is unavailable");
      return false;
    }
    if (!AudioClient.IsAvailable() ||
        AudioClient.SetStreamState(0U, TRIAEVUM_AUDIO_STREAM_PLAYING_V1) !=
            TRIAEVUM_MODULE_OK_V1) {
      SetError(error, "TriAevum audio host service is unavailable");
      return false;
    }
    if (!InputClient.IsAvailable()) {
      SetError(error, "TriAevum input host service is unavailable");
      return false;
    }
    if (!FilesystemClient.IsAvailable()) {
      SetError(error, "TriAevum filesystem host service is unavailable");
      return false;
    }
    if (!MountNativeA32ProcessImage(Process, Manifest, CodeBytes, error)) {
      return false;
    }
    if (!UiProfile.ApplyAfterMount(Process, error)) {
      return false;
    }
    const auto &uiStats = UiProfile.Stats();
    std::ostringstream uiMessage;
    uiMessage << "TopScreen profile active: contracts="
              << uiStats.LayoutContractsChecked
              << " words=" << uiStats.LayoutWordsWritten
              << " changed=" << uiStats.LayoutWordsChanged
              << " rebound_streams=" << uiStats.RuntimeStreamsRebound
              << " rebound_quads=" << uiStats.RuntimeQuadsRebound;
    const std::string uiText = uiMessage.str();
    Host.log(Host.host_context, TRIAEVUM_LOG_INFO_V1, uiText.data(),
             uiText.size());
    CodeBytes.clear();
    CodeBytes.shrink_to_fit();
    TargetTick = HostServices.SystemTicks();
    ScheduleNextVblank();
    return true;
  }

  bool RunUntilWait(std::string *error) {
    for (uint32_t slice = 0U; slice < kMaximumDispatchSlices; ++slice) {
      LastRunResult = Process.Run(kWholeAotBlockBudget);
      if (LastRunResult.Kind == NativeA32ProcessRunKind::Yielded) {
        continue;
      }
      if (LastRunResult.Kind == NativeA32ProcessRunKind::Faulted) {
        SetError(error, "native title process faulted: " + LastRunResult.Error);
        return false;
      }
      return true;
    }
    SetError(error, "native title process exceeded its dispatch budget");
    return false;
  }

  bool AdvanceClockTo(uint64_t deadline, std::string *error) {
    if (deadline < HostServices.SystemTicks()) {
      SetError(error, "native title clock deadline moved backwards");
      return false;
    }
    while (LastRunResult.Kind != NativeA32ProcessRunKind::Terminated) {
      const auto wakeTick = HostServices.NextSleepWakeTick();
      if (!wakeTick.has_value() || *wakeTick > deadline) {
        break;
      }
      const uint64_t current = HostServices.SystemTicks();
      HostServices.AdvanceSystemTicks(*wakeTick > current ? *wakeTick - current
                                                          : 0U);
      if (!RunUntilWait(error)) {
        return false;
      }
    }
    if (HostServices.SystemTicks() < deadline) {
      HostServices.AdvanceSystemTicks(deadline - HostServices.SystemTicks());
    }
    return true;
  }

  TriAevumModuleStatusV1 DrainDspEvents(std::string *error) {
    const uint32_t frames = HostServices.TakePendingDspAudioFrames();
    for (uint32_t frame = 0U; frame < frames; ++frame) {
      std::vector<int16_t> samples;
      if (!DspHle.ProcessFrame(Process.Memory(), samples, error)) {
        return TRIAEVUM_MODULE_TITLE_ERROR_V1;
      }
      triaevum::module::AudioSubmissionResultV1 submitted;
      const TriAevumModuleStatusV1 audioStatus =
          AudioClient.SubmitInterleavedS16(
              0U, 2U, NativeA32DspHle::NativeSampleRate, samples, &submitted);
      if (audioStatus != TRIAEVUM_MODULE_OK_V1 ||
          submitted.acceptedFrameCount != NativeA32DspHle::SamplesPerFrame) {
        SetError(error, "native DSP PCM was rejected by the audio host");
        return audioStatus == TRIAEVUM_MODULE_OK_V1
                   ? TRIAEVUM_MODULE_HOST_ERROR_V1
                   : audioStatus;
      }
      if (!HostServices.SignalDspAudioFrame()) {
        SetError(error, "native DSP frame event could not be signaled");
        return TRIAEVUM_MODULE_TITLE_ERROR_V1;
      }
      if (!RunUntilWait(error)) {
        return TRIAEVUM_MODULE_TITLE_ERROR_V1;
      }
    }
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 ReadHidState(NativeA32HidState *hid,
                                      std::string *error) {
    if (hid == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    triaevum::module::InputStateV1 input;
    const TriAevumModuleStatusV1 status = InputClient.ReadState(0U, &input);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      SetError(error, "TriAevum input state could not be read");
      return status;
    }
    NativeA32HidState resolved;
    resolved.Buttons = static_cast<uint32_t>(input.buttons);
    resolved.CirclePadX = ScaleInputAxis(input.leftStickX);
    resolved.CirclePadY = ScaleInputAxis(input.leftStickY);
    if ((input.flags & TRIAEVUM_INPUT_TOUCH_VALID_V1) != 0U) {
      resolved.TouchX = ScaleTouchAxis(input.touchX, NativeA32TouchWidth);
      resolved.TouchY = ScaleTouchAxis(input.touchY, NativeA32TouchHeight);
      resolved.TouchPressed =
          (input.flags & TRIAEVUM_INPUT_TOUCH_PRESSED_V1) != 0U;
    }
    if ((input.flags & TRIAEVUM_INPUT_GYROSCOPE_VALID_V1) != 0U) {
      resolved.GyroscopeDegreesPerSecond = {input.gyroscopeX, input.gyroscopeY,
                                            input.gyroscopeZ};
      resolved.GyroscopeValid = true;
    }
    if ((input.flags & TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1) != 0U) {
      resolved.Accelerometer = {input.accelerometerX, input.accelerometerY,
                                input.accelerometerZ};
      resolved.AccelerometerValid = true;
    }
    *hid = resolved;
    return TRIAEVUM_MODULE_OK_V1;
  }

  bool PumpPicaInterrupts(std::string *error) {
    std::vector<uint8_t> interrupts;
    if (!PicaBridge.TakePendingInterrupts(&interrupts, error)) {
      return false;
    }
    for (const uint8_t interrupt : interrupts) {
      if (!HostServices.SignalPicaInterrupt(
              Process.Memory(), static_cast<Oot3dPicaInterruptId>(interrupt))) {
        SetError(error,
                 "native PICA interrupt could not enter the relay queue");
        return false;
      }
    }
    return interrupts.empty() || RunUntilWait(error);
  }

  void ScheduleNextVblank() {
    RefreshTickRemainder += kArm11TicksPerSecond;
    NextVblankTick += RefreshTickRemainder / kDisplayRefreshRate;
    RefreshTickRemainder %= kDisplayRefreshRate;
  }

  std::optional<uint64_t> ConvertStepToTicks(uint64_t nanoseconds) {
    if (nanoseconds == 0U || nanoseconds > kMaximumSimulationStepNs) {
      return std::nullopt;
    }
    const uint64_t seconds = nanoseconds / kNanosecondsPerSecond;
    const uint64_t remainderNs = nanoseconds % kNanosecondsPerSecond;
    const uint64_t numerator =
        remainderNs * kArm11TicksPerSecond + TickFraction;
    TickFraction = numerator % kNanosecondsPerSecond;
    return seconds * kArm11TicksPerSecond + numerator / kNanosecondsPerSecond;
  }

  std::vector<uint8_t> CaptureState(std::string *error) const {
    if (!Leases.empty()) {
      SetError(error, "guest memory leases are active during state capture");
      return {};
    }
    try {
      const Json state = {
          {"format", "triaevum_oot3d_module_state_v1"},
          {"source_identity_sha256", Hex(kOot3dGameModuleIdentity)},
          {"process", Process.CaptureState()},
          {"ctr_host", HostServices.CaptureState()},
          {"dsp_hle", DspHle.CaptureState()},
          {"timing",
           {{"target_tick", TargetTick},
            {"next_vblank_tick", NextVblankTick},
            {"refresh_tick_remainder", RefreshTickRemainder},
            {"tick_fraction", TickFraction},
            {"host_frame_sequence", HostFrameSequence},
            {"pica_submission_sequence", PicaBridge.SubmissionSequence()}}},
      };
      return Json::to_msgpack(state);
    } catch (const std::exception &exception) {
      SetError(error, std::string("state capture failed: ") + exception.what());
      return {};
    }
  }

  TriAevumHostApiV1 Host{};
  NativeA32ProcessImageManifest Manifest;
  std::vector<uint8_t> CodeBytes;
  triaevum::module::PicaServiceClientV1 PicaClient;
  triaevum::module::AudioServiceClientV1 AudioClient;
  triaevum::module::InputServiceClientV1 InputClient;
  triaevum::module::FilesystemServiceClientV1 FilesystemClient;
  TriAevumOot3dPicaClientBridge PicaBridge;
  TriAevumCtrFilesystemClientBridge FilesystemBridge;
  NativeA32CtrHostServices HostServices;
  NativeA32Process Process;
  NativeA32DspHle DspHle;
  Oot3dGameModuleUiProfileRuntime UiProfile;
  NativeA32ProcessRunResult LastRunResult{};
  uint64_t TargetTick = 0U;
  uint64_t NextVblankTick = 0U;
  uint64_t RefreshTickRemainder = 0U;
  uint64_t TickFraction = 0U;
  uint64_t HostFrameSequence = 0U;
  uint64_t NextLeaseToken = 1U;
  std::unordered_map<uint64_t, MemoryLease> Leases;
  std::vector<uint8_t> CachedState;
};

std::unique_ptr<Oot3dGameModuleRuntime>
Oot3dGameModuleRuntime::Create(const TriAevumHostApiV1 &host,
                               std::string_view privateContentIndex,
                               std::string *error) {
  try {
    if (host.struct_size < sizeof(TriAevumHostApiV1) ||
        host.abi_version != TRIAEVUM_RUNTIME_ABI_V1 || host.log == nullptr ||
        host.monotonic_time_ns == nullptr || host.invoke_service == nullptr ||
        privateContentIndex.empty()) {
      throw std::runtime_error("invalid TriAevum module initialization");
    }
    auto content = VerifyContent(host, privateContentIndex);
    auto impl = std::make_unique<Impl>(host, std::move(content));
    if (!impl->Mount(error)) {
      return nullptr;
    }
    return std::unique_ptr<Oot3dGameModuleRuntime>(
        new Oot3dGameModuleRuntime(std::move(impl)));
  } catch (const std::exception &exception) {
    SetError(error, exception.what());
    return nullptr;
  }
}

Oot3dGameModuleRuntime::Oot3dGameModuleRuntime(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl)) {}

Oot3dGameModuleRuntime::~Oot3dGameModuleRuntime() = default;

TriAevumModuleStatusV1 Oot3dGameModuleRuntime::Start(std::string *error) {
  if (mImpl == nullptr || !mImpl->RunUntilWait(error)) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  if (mImpl->LastRunResult.Kind == NativeA32ProcessRunKind::Terminated) {
    SetError(error, "native title process terminated during startup");
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
Oot3dGameModuleRuntime::RunFrame(const TriAevumFrameInputV1 &input,
                                 std::string *error) {
  if (mImpl == nullptr || input.struct_size < sizeof(TriAevumFrameInputV1)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (mImpl->LastRunResult.Kind == NativeA32ProcessRunKind::Terminated) {
    SetError(error, "native title process has terminated");
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  const auto stepTicks = mImpl->ConvertStepToTicks(input.simulation_step_ns);
  if (!stepTicks.has_value() ||
      *stepTicks > std::numeric_limits<uint64_t>::max() - mImpl->TargetTick) {
    SetError(error, "invalid module simulation step");
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  mImpl->TargetTick += *stepTicks;
  if (!mImpl->PumpPicaInterrupts(error)) {
    return TRIAEVUM_MODULE_HOST_ERROR_V1;
  }
  while (mImpl->NextVblankTick <= mImpl->TargetTick) {
    if (!mImpl->AdvanceClockTo(mImpl->NextVblankTick, error)) {
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    const TriAevumModuleStatusV1 audioStatus = mImpl->DrainDspEvents(error);
    if (audioStatus != TRIAEVUM_MODULE_OK_V1) {
      return audioStatus;
    }
    NativeA32HidState inputState;
    const TriAevumModuleStatusV1 inputStatus =
        mImpl->ReadHidState(&inputState, error);
    if (inputStatus != TRIAEVUM_MODULE_OK_V1) {
      return inputStatus;
    }
    const auto hid = mImpl->HostServices.AdvanceHidToCurrentTick(
        mImpl->Process.Memory(), inputState);
    if (hid.Status == NativeA32CtrHidUpdateStatus::Failed) {
      SetError(error, "native HID state could not be updated");
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    if (hid.EventsSignaled && !mImpl->RunUntilWait(error)) {
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    if (!mImpl->HostServices.SignalVBlank(mImpl->Process.Memory()) ||
        !mImpl->RunUntilWait(error)) {
      SetError(error, "native VBlank dispatch failed");
      return TRIAEVUM_MODULE_TITLE_ERROR_V1;
    }
    mImpl->ScheduleNextVblank();
  }
  if (!mImpl->AdvanceClockTo(mImpl->TargetTick, error)) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  const TriAevumModuleStatusV1 audioStatus = mImpl->DrainDspEvents(error);
  if (audioStatus != TRIAEVUM_MODULE_OK_V1) {
    return audioStatus;
  }
  uint64_t completedSubmission = 0U;
  const TriAevumModuleStatusV1 flush =
      mImpl->PicaClient.Flush(++mImpl->HostFrameSequence, &completedSubmission);
  if (flush != TRIAEVUM_MODULE_OK_V1) {
    SetError(error, "TriAevum PICA frame flush failed");
    return flush;
  }
  return mImpl->PumpPicaInterrupts(error) ? TRIAEVUM_MODULE_OK_V1
                                          : TRIAEVUM_MODULE_HOST_ERROR_V1;
}

size_t Oot3dGameModuleRuntime::StateSize(std::string *error) {
  if (mImpl == nullptr) {
    return 0U;
  }
  mImpl->CachedState = mImpl->CaptureState(error);
  return mImpl->CachedState.size();
}

TriAevumModuleStatusV1
Oot3dGameModuleRuntime::SaveState(TriAevumMutableBytesV1 destination,
                                  size_t *writtenSize, std::string *error) {
  if (mImpl == nullptr || writtenSize == nullptr ||
      (destination.data == nullptr && destination.size != 0U)) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  mImpl->CachedState.clear();
  auto state = mImpl->CaptureState(error);
  if (state.empty()) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  if (destination.size < state.size()) {
    *writtenSize = state.size();
    return TRIAEVUM_MODULE_RESPONSE_TOO_SMALL_V1;
  }
  std::memcpy(destination.data, state.data(), state.size());
  *writtenSize = state.size();
  mImpl->CachedState.clear();
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
Oot3dGameModuleRuntime::LoadState(TriAevumReadOnlyBytesV1 source,
                                  std::string *error) {
  if (mImpl == nullptr || source.data == nullptr || source.size == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if (!mImpl->Leases.empty()) {
    SetError(error, "guest memory leases are active during state load");
    return TRIAEVUM_MODULE_SERVICE_BUSY_V1;
  }
  try {
    const Json state =
        Json::from_msgpack(source.data, source.data + source.size);
    if (!state.is_object() ||
        state.value("format", "") != "triaevum_oot3d_module_state_v1" ||
        state.value("source_identity_sha256", "") !=
            Hex(kOot3dGameModuleIdentity)) {
      throw std::runtime_error("module state identity is invalid");
    }
    const Json rollbackProcess = mImpl->Process.CaptureState();
    const Json rollbackHost = mImpl->HostServices.CaptureState();
    const Json rollbackDsp = mImpl->DspHle.CaptureState();
    const uint64_t rollbackTargetTick = mImpl->TargetTick;
    const uint64_t rollbackNextVblankTick = mImpl->NextVblankTick;
    const uint64_t rollbackRefreshTickRemainder = mImpl->RefreshTickRemainder;
    const uint64_t rollbackTickFraction = mImpl->TickFraction;
    const uint64_t rollbackHostFrameSequence = mImpl->HostFrameSequence;
    const uint64_t rollbackPicaSubmissionSequence =
        mImpl->PicaBridge.SubmissionSequence();
    const NativeA32ProcessRunResult rollbackRunResult = mImpl->LastRunResult;
    const auto &timing = state.at("timing");
    std::string restoreError;
    const auto rollback = [&]() {
      std::string ignored;
      mImpl->Process.RestoreState(rollbackProcess, &ignored);
      mImpl->HostServices.RestoreState(rollbackHost, mImpl->Process, &ignored);
      mImpl->DspHle.RestoreState(rollbackDsp, &ignored);
      mImpl->TargetTick = rollbackTargetTick;
      mImpl->NextVblankTick = rollbackNextVblankTick;
      mImpl->RefreshTickRemainder = rollbackRefreshTickRemainder;
      mImpl->TickFraction = rollbackTickFraction;
      mImpl->HostFrameSequence = rollbackHostFrameSequence;
      mImpl->PicaBridge.RestoreSubmissionSequence(
          rollbackPicaSubmissionSequence);
      mImpl->LastRunResult = rollbackRunResult;
    };
    try {
      if (!mImpl->Process.RestoreState(state.at("process"), &restoreError) ||
          !mImpl->HostServices.RestoreState(state.at("ctr_host"),
                                            mImpl->Process, &restoreError)) {
        throw std::runtime_error("module state restore failed: " +
                                 restoreError);
      }
      if (state.contains("dsp_hle") &&
          !mImpl->DspHle.RestoreState(state.at("dsp_hle"), &restoreError)) {
        throw std::runtime_error("module DSP state restore failed: " +
                                 restoreError);
      }
      mImpl->TargetTick = timing.at("target_tick").get<uint64_t>();
      mImpl->NextVblankTick = timing.at("next_vblank_tick").get<uint64_t>();
      mImpl->RefreshTickRemainder =
          timing.at("refresh_tick_remainder").get<uint64_t>();
      mImpl->TickFraction = timing.at("tick_fraction").get<uint64_t>();
      mImpl->HostFrameSequence =
          timing.at("host_frame_sequence").get<uint64_t>();
      mImpl->PicaBridge.RestoreSubmissionSequence(
          timing.at("pica_submission_sequence").get<uint64_t>());
      if (mImpl->TargetTick != mImpl->HostServices.SystemTicks() ||
          mImpl->NextVblankTick <= mImpl->TargetTick ||
          mImpl->RefreshTickRemainder >= kDisplayRefreshRate ||
          mImpl->TickFraction >= kNanosecondsPerSecond) {
        throw std::runtime_error("module state timing is invalid");
      }
      mImpl->LastRunResult = {};
      mImpl->LastRunResult.Kind = NativeA32ProcessRunKind::Waiting;
    } catch (...) {
      rollback();
      throw;
    }
    return TRIAEVUM_MODULE_OK_V1;
  } catch (const std::exception &exception) {
    SetError(error, std::string("state load failed: ") + exception.what());
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
}

TriAevumModuleStatusV1 Oot3dGameModuleRuntime::MapGuestMemory(
    const TriAevumGuestMemoryMapRequestV1 &request,
    TriAevumGuestMemoryViewV1 *view) {
  constexpr TriAevumGuestMemoryAccessV1 validAccess =
      TRIAEVUM_GUEST_MEMORY_READ_V1 | TRIAEVUM_GUEST_MEMORY_WRITE_V1;
  if (mImpl == nullptr || view == nullptr ||
      request.struct_size < sizeof(TriAevumGuestMemoryMapRequestV1) ||
      request.byte_count == 0U || request.access == 0U ||
      (request.access & ~validAccess) != 0U ||
      mImpl->Leases.size() >= kMaximumMemoryLeases) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  uint8_t *pointer = nullptr;
  if ((request.access & TRIAEVUM_GUEST_MEMORY_WRITE_V1) != 0U) {
    pointer = mImpl->Process.Memory().GetWritePointer(request.guest_address,
                                                      request.byte_count);
  } else {
    pointer = const_cast<uint8_t *>(mImpl->Process.Memory().GetReadPointer(
        request.guest_address, request.byte_count));
  }
  const auto version = mImpl->Process.Memory().RangeWriteGeneration(
      request.guest_address, request.byte_count);
  if (pointer == nullptr || !version.has_value()) {
    return TRIAEVUM_MODULE_TITLE_ERROR_V1;
  }
  uint64_t token = mImpl->NextLeaseToken++;
  while (token == 0U || mImpl->Leases.contains(token)) {
    token = mImpl->NextLeaseToken++;
  }
  mImpl->Leases.emplace(token,
                        Impl::MemoryLease{request.guest_address,
                                          request.byte_count, request.access});
  *view = {sizeof(TriAevumGuestMemoryViewV1),
           request.access,
           pointer,
           request.byte_count,
           *version,
           token};
  return TRIAEVUM_MODULE_OK_V1;
}

TriAevumModuleStatusV1
Oot3dGameModuleRuntime::UnmapGuestMemory(uint64_t token, uint32_t flags) {
  if (mImpl == nullptr || token == 0U ||
      (flags & ~TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) != 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  const auto lease = mImpl->Leases.find(token);
  if (lease == mImpl->Leases.end()) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  if ((flags & TRIAEVUM_GUEST_MEMORY_UNMAP_WRITTEN_V1) != 0U &&
      (lease->second.Access & TRIAEVUM_GUEST_MEMORY_WRITE_V1) == 0U) {
    return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
  }
  mImpl->Leases.erase(lease);
  return TRIAEVUM_MODULE_OK_V1;
}

} // namespace Oot3dNativeGame
