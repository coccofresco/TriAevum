#include "triaevum/audio_service_adapter.h"
#include "triaevum/filesystem_service_adapter.h"
#include "triaevum/input_service_adapter.h"
#include "triaevum/pica_service_adapter.h"
#include "triaevum/runtime_session.h"
#include "triaevum/service_abi.h"
#include "triaevum_forge_content_filesystem.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace triaevum::module;

struct SmokeState {
  std::uint64_t RegisterWrites = 0U;
  std::uint64_t GspCommands = 0U;
  std::uint64_t FramebufferUpdates = 0U;
  std::uint64_t ForceBlackUpdates = 0U;
  std::uint64_t Flushes = 0U;
  std::uint64_t AudioSubmissions = 0U;
  std::uint64_t AudioFrames = 0U;
  std::uint64_t AudioStateChanges = 0U;
  std::uint64_t InputReads = 0U;
};

void TRIAEVUM_ABI_CALL Log(void *, TriAevumLogLevelV1 level,
                           const char *message, std::size_t messageSize) {
  std::cerr << "module[" << level << "]: ";
  if (message != nullptr) {
    std::cerr.write(message, static_cast<std::streamsize>(messageSize));
  }
  std::cerr << '\n';
}

std::uint64_t TRIAEVUM_ABI_CALL MonotonicTime(void *) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

class SmokePicaBackend final : public PicaServiceBackendV1 {
public:
  explicit SmokePicaBackend(SmokeState &state) : mState(state) {}

  TriAevumModuleStatusV1
  WriteRegisters(std::uint32_t, std::span<const std::uint32_t>,
                 std::span<const std::uint32_t>) override {
    ++mState.RegisterWrites;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  SubmitGspCommand(std::uint64_t, std::uint32_t control,
                   const std::array<std::uint32_t, 7> &,
                   std::span<const std::uint32_t>, std::uint64_t *submissionId,
                   TriAevumPicaSubmissionFlagsV1 *flags) override {
    if (submissionId == nullptr || flags == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    ++mState.GspCommands;
    *submissionId = ++mLastSubmission;
    *flags = 0U;
    switch (control & 0xFFU) {
    case 1U:
      mPendingInterrupts.push_back(5U);
      break;
    case 4U:
      mPendingInterrupts.push_back(4U);
      break;
    default:
      break;
    }
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 SetFramebuffer(const PicaFramebufferV1 &) override {
    ++mState.FramebufferUpdates;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 SetLcdForceBlack(bool) override {
    ++mState.ForceBlackUpdates;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1
  TakeInterrupts(std::vector<std::uint8_t> *interrupts) override {
    if (interrupts == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    *interrupts = std::move(mPendingInterrupts);
    mPendingInterrupts.clear();
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 Flush(std::uint64_t,
                               std::uint64_t *completedSubmissionId) override {
    if (completedSubmissionId == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    ++mState.Flushes;
    *completedSubmissionId = mLastSubmission;
    return TRIAEVUM_MODULE_OK_V1;
  }

private:
  SmokeState &mState;
  std::uint64_t mLastSubmission = 0U;
  std::vector<std::uint8_t> mPendingInterrupts;
};

class SmokeAudioBackend final : public AudioServiceBackendV1 {
public:
  explicit SmokeAudioBackend(SmokeState &state) : mState(state) {}

  TriAevumModuleStatusV1 SubmitPcm(const AudioPcmSubmissionV1 &submission,
                                   std::uint32_t *acceptedFrameCount,
                                   std::uint32_t *queuedFrameCount) override {
    if (acceptedFrameCount == nullptr || queuedFrameCount == nullptr ||
        submission.sampleFormat != TRIAEVUM_AUDIO_S16_V1 ||
        submission.channelCount != 2U || submission.sampleRateHz != 32728U) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    ++mState.AudioSubmissions;
    mState.AudioFrames += submission.frameCount;
    *acceptedFrameCount = submission.frameCount;
    *queuedFrameCount = 0U;
    return TRIAEVUM_MODULE_OK_V1;
  }

  TriAevumModuleStatusV1 SetStreamState(std::uint32_t,
                                        TriAevumAudioStreamStateV1) override {
    ++mState.AudioStateChanges;
    return TRIAEVUM_MODULE_OK_V1;
  }

private:
  SmokeState &mState;
};

class SmokeInputBackend final : public InputServiceBackendV1 {
public:
  explicit SmokeInputBackend(SmokeState &state) : mState(state) {}

  TriAevumModuleStatusV1 ReadState(std::uint32_t playerIndex,
                                   InputStateV1 *state) override {
    if (playerIndex != 0U || state == nullptr) {
      return TRIAEVUM_MODULE_INVALID_ARGUMENT_V1;
    }
    ++mState.InputReads;
    state->sampleSequence = mState.InputReads;
    state->buttons =
        (mState.InputReads & 1U) != 0U ? TRIAEVUM_INPUT_BUTTON_A_V1 : 0U;
    state->leftStickX = 0.25F;
    state->accelerometerY = -1.0F;
    state->flags = TRIAEVUM_INPUT_ACCELEROMETER_VALID_V1;
    return TRIAEVUM_MODULE_OK_V1;
  }

private:
  SmokeState &mState;
};

bool ParseFrameCount(const char *value, std::uint32_t *frames) {
  if (value == nullptr || frames == nullptr) {
    return false;
  }
  try {
    const unsigned long parsed = std::stoul(value);
    if (parsed == 0UL || parsed > 3600UL) {
      return false;
    }
    *frames = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) {
    std::cerr << "usage: oot3d_game_module_smoke <game.tam> <cache-directory> "
                 "<content.tap> [frames]\n";
    return 2;
  }
  std::uint32_t frameCount = 120U;
  if (argc == 5 && !ParseFrameCount(argv[4], &frameCount)) {
    std::cerr << "frames must be in the range 1..3600\n";
    return 2;
  }

  SmokeState state;
  SmokePicaBackend backend(state);
  PicaHostServiceAdapterV1 picaService(backend);
  SmokeAudioBackend audioBackend(state);
  AudioHostServiceAdapterV1 audioService(audioBackend);
  SmokeInputBackend inputBackend(state);
  InputHostServiceAdapterV1 inputService(inputBackend);
  RuntimeSession session({nullptr, Log, MonotonicTime});
  RuntimeSessionError error;
  if (!session.Load(std::filesystem::path(argv[1]),
                    std::filesystem::path(argv[2]),
                    std::filesystem::path(argv[3]), &error)) {
    std::cerr << "module load failed: " << error.message << '\n';
    return 1;
  }
  std::string filesystemError;
  auto filesystemBackend =
      Oot3dNativeGame::CreateTriAevumForgeContentFilesystem(
          std::filesystem::path(argv[3]), {}, &filesystemError);
  if (filesystemBackend == nullptr) {
    std::cerr << "filesystem smoke backend failed: " << filesystemError << '\n';
    return 1;
  }
  FilesystemHostServiceAdapterV1 filesystemService(*filesystemBackend);
  if (session.RegisterService(TRIAEVUM_SERVICE_FILESYSTEM_V1,
                              FilesystemHostServiceAdapterV1::Invoke,
                              &filesystemService) !=
      ServiceRegistrationResult::Registered) {
    std::cerr << "filesystem smoke service registration failed\n";
    return 1;
  }
  if (session.RegisterService(TRIAEVUM_SERVICE_PICA_V1,
                              PicaHostServiceAdapterV1::Invoke, &picaService) !=
      ServiceRegistrationResult::Registered) {
    std::cerr << "PICA smoke service registration failed\n";
    return 1;
  }
  if (session.RegisterService(
          TRIAEVUM_SERVICE_AUDIO_V1, AudioHostServiceAdapterV1::Invoke,
          &audioService) != ServiceRegistrationResult::Registered) {
    std::cerr << "audio smoke service registration failed\n";
    return 1;
  }
  if (session.RegisterService(
          TRIAEVUM_SERVICE_INPUT_V1, InputHostServiceAdapterV1::Invoke,
          &inputService) != ServiceRegistrationResult::Registered) {
    std::cerr << "input smoke service registration failed\n";
    return 1;
  }
  if (!session.Initialize(&error)) {
    std::cerr << "module initialization failed: " << error.message << '\n';
    return 1;
  }

  for (std::uint32_t frame = 0U; frame < frameCount; ++frame) {
    const TriAevumFrameInputV1 input = {
        sizeof(TriAevumFrameInputV1),
        0U,
        static_cast<std::uint64_t>(frame + 1U) * 16'666'667ULL,
        16'666'667ULL,
        {nullptr, 0U},
    };
    const TriAevumModuleStatusV1 status = session.RunFrame(input);
    if (status != TRIAEVUM_MODULE_OK_V1) {
      std::cerr << "module frame " << frame << " failed with status " << status
                << '\n';
      return 1;
    }
  }

  std::vector<std::uint8_t> savedState;
  if (session.SaveState(&savedState) != TRIAEVUM_MODULE_OK_V1 ||
      savedState.empty() ||
      session.LoadState(savedState) != TRIAEVUM_MODULE_OK_V1) {
    std::cerr << "module save-state round trip failed\n";
    return 1;
  }

  const TriAevumGuestMemoryMapRequestV1 request = {
      sizeof(TriAevumGuestMemoryMapRequestV1),
      TRIAEVUM_GUEST_MEMORY_READ_V1,
      0x00100000U,
      4U,
  };
  TriAevumGuestMemoryViewV1 view{};
  if (session.MapGuestMemory(request, &view) != TRIAEVUM_MODULE_OK_V1 ||
      view.data == nullptr || view.size != request.byte_count ||
      session.UnmapGuestMemory(view.token, 0U) != TRIAEVUM_MODULE_OK_V1) {
    std::cerr << "module guest-memory lease round trip failed\n";
    return 1;
  }

  session.Stop();
  const std::uint64_t picaCalls = state.RegisterWrites + state.GspCommands +
                                  state.FramebufferUpdates +
                                  state.ForceBlackUpdates;
  const auto filesystemStats = filesystemBackend->Stats();
  if (picaCalls == 0U || state.Flushes != frameCount ||
      state.AudioSubmissions == 0U || state.AudioFrames == 0U ||
      state.AudioStateChanges == 0U || state.InputReads == 0U) {
    std::cerr << "module did not cross the PICA, audio and input host-service "
                 "boundaries\n";
    return 1;
  }
  std::cout << "TriAevum OoT3D module smoke passed: frames=" << frameCount
            << " pica_calls=" << picaCalls
            << " register_writes=" << state.RegisterWrites
            << " gsp_commands=" << state.GspCommands
            << " framebuffer_updates=" << state.FramebufferUpdates
            << " force_black_updates=" << state.ForceBlackUpdates
            << " audio_submissions=" << state.AudioSubmissions
            << " audio_frames=" << state.AudioFrames
            << " input_reads=" << state.InputReads
            << " fs_opens=" << filesystemStats.opens
            << " fs_reads=" << filesystemStats.reads
            << " fs_read_bytes=" << filesystemStats.readBytes
            << " state_bytes=" << savedState.size() << '\n';
  return 0;
}
