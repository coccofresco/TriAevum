#include "fast/Fast3dWindow.h"
#include "fast/backends/gfx_rendering_api.h"
#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_screenshot.h"
#include "oot3d_demo_host_window_timing.h"
#include "oot3d_native_a32_dsp_hle.h"
#include "oot3d_native_control_config.h"
#include "ship/Context.h"
#include "ship/audio/Audio.h"
#include "ship/window/gui/Gui.h"
#include "triaevum/audio_service_adapter.h"
#include "triaevum/filesystem_service_adapter.h"
#include "triaevum/input_service_adapter.h"
#include "triaevum/runtime_session.h"
#include "triaevum_audio_player_backend.h"
#include "triaevum_forge_content_filesystem.h"
#include "triaevum_oot3d_input_backend.h"
#include "triaevum_oot3d_pica_host.h"
#include "triaevum_oot3d_pica_renderer.h"
#include "triaevum_runtime_layout.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

constexpr std::uint64_t kSimulationStepNanoseconds = 16'666'667ULL;

struct ModuleHostArguments {
  std::filesystem::path ExecutablePath;
  std::filesystem::path DataRoot;
  std::filesystem::path ActiveTitleState;
  std::filesystem::path TitleDirectory;
  std::filesystem::path ModulePath;
  std::filesystem::path CacheDirectory;
  std::filesystem::path ContentIndexPath;
  std::filesystem::path ControlConfigPath;
  std::filesystem::path ConfigurationPath;
  Args Host;
};

class ContextGuard final {
public:
  ~ContextGuard() {
    if (mInitialized) {
      DestroyContextForDemo();
    }
  }

  void Initialize(const Args &args) {
    InitContextForDemo(args);
    mInitialized = true;
  }

private:
  bool mInitialized = false;
};

void PrintUsage() {
  std::cerr
      << "usage: TriAevum [--title <prepared-title-directory>] "
         "[--active-title <active-title.json>] [--data-root <directory>] "
         "[--module <game.tam> --content <content.tap>] "
         "[--cache <directory>] [--resource-root <directory>] "
         "[--config <path>] [--renderer nri|vulkan|opengl] [--width <pixels>] "
         "[--height <pixels>] [--frames <count>] [--max-seconds <seconds>] "
         "[--controls <path>] [--screenshot <path>]\n";
}

template <typename Value>
bool ParseUnsigned(std::string_view text, Value *value) {
  static_assert(std::is_unsigned_v<Value>);
  if (value == nullptr || text.empty()) {
    return false;
  }
  Value parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
    return false;
  }
  *value = parsed;
  return true;
}

bool ParsePositiveDouble(std::string_view text, double *value) {
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0U;
    const double parsed = std::stod(std::string(text), &consumed);
    if (consumed != text.size() || !(parsed > 0.0)) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseArguments(int argc, char **argv, ModuleHostArguments *arguments) {
  if (arguments == nullptr) {
    return false;
  }
  if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
    return false;
  }
  arguments->ExecutablePath = argv[0];
  arguments->Host.ApplicationName = "TriAevum";
  arguments->Host.ApplicationId = "triaevum";
  arguments->Host.Renderer = "nri";
  arguments->Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
  arguments->Host.FixedDeltaSeconds = 1.0 / 60.0;
  arguments->Host.AudioSampleRate =
      Oot3dNativeGame::NativeA32DspHle::NativeSampleRate;
  arguments->Host.AudioSampleLength =
      Oot3dNativeGame::NativeA32DspHle::SamplesPerFrame;

  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    const auto takeValue = [&]() -> std::string_view {
      if (++index >= argc) {
        return {};
      }
      return argv[index];
    };
    if (option == "--title") {
      arguments->TitleDirectory = takeValue();
    } else if (option == "--active-title") {
      arguments->ActiveTitleState = takeValue();
    } else if (option == "--data-root") {
      arguments->DataRoot = takeValue();
    } else if (option == "--module") {
      arguments->ModulePath = takeValue();
    } else if (option == "--cache") {
      arguments->CacheDirectory = takeValue();
    } else if (option == "--content") {
      arguments->ContentIndexPath = takeValue();
    } else if (option == "--resource-root") {
      arguments->Host.ResourceRoot = takeValue();
    } else if (option == "--renderer") {
      arguments->Host.Renderer = std::string(takeValue());
      if (arguments->Host.Renderer == "nri" ||
          arguments->Host.Renderer == "vulkan") {
        arguments->Host.BackendId =
            Fast::WindowBackend::FAST3D_SDL_OOT3D_VULKAN;
      } else if (arguments->Host.Renderer == "opengl") {
        arguments->Host.BackendId = Fast::WindowBackend::FAST3D_SDL_OPENGL;
      } else {
        return false;
      }
    } else if (option == "--width") {
      if (!ParseUnsigned(takeValue(), &arguments->Host.Width) ||
          arguments->Host.Width == 0U) {
        return false;
      }
    } else if (option == "--height") {
      if (!ParseUnsigned(takeValue(), &arguments->Host.Height) ||
          arguments->Host.Height == 0U) {
        return false;
      }
    } else if (option == "--frames") {
      if (!ParseUnsigned(takeValue(), &arguments->Host.FrameLimit) ||
          arguments->Host.FrameLimit == 0U) {
        return false;
      }
    } else if (option == "--max-seconds") {
      if (!ParsePositiveDouble(takeValue(), &arguments->Host.MaxSeconds)) {
        return false;
      }
    } else if (option == "--controls") {
      arguments->ControlConfigPath = takeValue();
    } else if (option == "--config") {
      arguments->ConfigurationPath = takeValue();
    } else if (option == "--screenshot") {
      arguments->Host.ScreenshotPath = takeValue();
    } else {
      return false;
    }
  }

  Oot3dNativeGame::TriAevumRuntimePathOverrides overrides;
  overrides.ExecutablePath = arguments->ExecutablePath;
  overrides.DataRoot = arguments->DataRoot;
  overrides.ActiveTitleState = arguments->ActiveTitleState;
  overrides.TitleDirectory = arguments->TitleDirectory;
  overrides.ModulePath = arguments->ModulePath;
  overrides.CacheDirectory = arguments->CacheDirectory;
  overrides.ContentIndexPath = arguments->ContentIndexPath;
  overrides.ResourceRoot = arguments->Host.ResourceRoot;
  overrides.ControlConfigPath = arguments->ControlConfigPath;
  overrides.ConfigurationPath = arguments->ConfigurationPath;
  const auto layout =
      Oot3dNativeGame::ResolveTriAevumRuntimeLayout(overrides);
  arguments->DataRoot = layout.DataRoot;
  arguments->TitleDirectory = layout.TitleDirectory;
  arguments->ModulePath = layout.ModulePath;
  arguments->CacheDirectory = layout.CacheDirectory;
  arguments->ContentIndexPath = layout.ContentIndexPath;
  arguments->Host.ResourceRoot = layout.ResourceRoot;
  arguments->ControlConfigPath = layout.ControlConfigPath;
  arguments->ConfigurationPath = layout.ConfigurationPath;
  arguments->Host.ConfigurationPath = layout.ConfigurationPath.string();
  if (!arguments->Host.ScreenshotPath.empty() &&
      arguments->Host.FrameLimit != 0U) {
    arguments->Host.ScreenshotStartFrame = arguments->Host.FrameLimit - 1U;
  }
  return true;
}

void ValidateInputPath(const std::filesystem::path &path, bool directory,
                       const char *label) {
  const bool valid = directory ? std::filesystem::is_directory(path)
                               : std::filesystem::is_regular_file(path);
  if (!valid) {
    throw std::runtime_error(std::string(label) +
                             " does not exist: " + path.string());
  }
}

void TRIAEVUM_ABI_CALL Log(void *, TriAevumLogLevelV1 level,
                           const char *message, std::size_t messageSize) {
  const char *prefix = level >= TRIAEVUM_LOG_ERROR_V1 ? "error" : "module";
  std::cerr << '[' << prefix << "] "
            << std::string_view(message == nullptr ? "" : message,
                                message == nullptr ? 0U : messageSize)
            << '\n';
}

std::uint64_t TRIAEVUM_ABI_CALL MonotonicTime(void *) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void RequireModuleStatus(TriAevumModuleStatusV1 status, const char *action) {
  if (status != TRIAEVUM_MODULE_OK_V1) {
    throw std::runtime_error(std::string(action) +
                             " failed with module status " +
                             std::to_string(status));
  }
}

class RuntimeSessionStopGuard {
public:
  explicit RuntimeSessionStopGuard(
      triaevum::module::RuntimeSession &session) noexcept
      : mSession(session) {}

  RuntimeSessionStopGuard(const RuntimeSessionStopGuard &) = delete;
  RuntimeSessionStopGuard &operator=(const RuntimeSessionStopGuard &) = delete;

  ~RuntimeSessionStopGuard() { mSession.Stop(); }

private:
  triaevum::module::RuntimeSession &mSession;
};

void RunModuleHost(const ModuleHostArguments &arguments) {
  std::filesystem::create_directories(arguments.CacheDirectory);
  std::filesystem::create_directories(arguments.ConfigurationPath.parent_path());
  ValidateInputPath(arguments.ModulePath, false, "private module");
  ValidateInputPath(arguments.CacheDirectory, true, "private module cache");
  ValidateInputPath(arguments.ContentIndexPath, false, "private content index");
  ValidateInputPath(arguments.Host.ResourceRoot, true, "runtime resources");

  triaevum::module::RuntimeSession session({nullptr, Log, MonotonicTime});
  triaevum::module::RuntimeSessionError sessionError;
  if (!session.Load(arguments.ModulePath, arguments.CacheDirectory,
                    arguments.ContentIndexPath, &sessionError)) {
    throw std::runtime_error("private module load failed: " +
                             sessionError.message);
  }

  std::string filesystemError;
  auto filesystemBackend =
      Oot3dNativeGame::CreateTriAevumForgeContentFilesystem(
          arguments.ContentIndexPath, {}, &filesystemError);
  if (filesystemBackend == nullptr) {
    throw std::runtime_error("filesystem host creation failed: " +
                             filesystemError);
  }
  triaevum::module::FilesystemHostServiceAdapterV1 filesystemService(
      *filesystemBackend);
  if (session.RegisterService(
          TRIAEVUM_SERVICE_FILESYSTEM_V1,
          triaevum::module::FilesystemHostServiceAdapterV1::Invoke,
          &filesystemService) !=
      triaevum::module::ServiceRegistrationResult::Registered) {
    throw std::runtime_error("filesystem host-service registration failed");
  }

  std::string picaError;
  auto picaHost = Oot3dNativeGame::TriAevumOot3dPicaHost::Create(
      session, {}, true, &picaError);
  if (picaHost == nullptr) {
    throw std::runtime_error("PICA host creation failed: " + picaError);
  }
  if (picaHost->Register(session) !=
      triaevum::module::ServiceRegistrationResult::Registered) {
    throw std::runtime_error("PICA host-service registration failed");
  }

  ContextGuard context;
  context.Initialize(arguments.Host);
  auto *shipContext = Ship::Context::GetRawInstance();
  auto gui = shipContext != nullptr && shipContext->GetWindow() != nullptr
                 ? shipContext->GetWindow()->GetGui()
                 : nullptr;
  if (gui == nullptr) {
    throw std::runtime_error("runtime GUI initialization failed");
  }
  auto audio = shipContext != nullptr ? shipContext->GetAudio() : nullptr;
  auto audioPlayer = audio != nullptr ? audio->GetAudioPlayer() : nullptr;
  Oot3dNativeGame::TriAevumAudioPlayerBackend audioBackend(audioPlayer);
  triaevum::module::AudioHostServiceAdapterV1 audioService(audioBackend);
  if (session.RegisterService(
          TRIAEVUM_SERVICE_AUDIO_V1,
          triaevum::module::AudioHostServiceAdapterV1::Invoke, &audioService) !=
      triaevum::module::ServiceRegistrationResult::Registered) {
    throw std::runtime_error("audio host-service registration failed");
  }
  auto inputConfig = Oot3dNativeGame::NativeControlPreset(
      Oot3dNativeGame::NativeControlProfile::Custom);
  inputConfig.CaptureMouseInGameplay = false;
  inputConfig.NativeAimSource = Oot3dNativeGame::NativeMotionSource::Automatic;
  inputConfig.FreeCameraSource = Oot3dNativeGame::NativeMotionSource::Automatic;
  if (!arguments.ControlConfigPath.empty()) {
    std::string inputError;
    if (!Oot3dNativeGame::LoadNativeControlConfig(arguments.ControlConfigPath,
                                                  &inputConfig, &inputError)) {
      throw std::runtime_error("control configuration failed: " + inputError);
    }
  }
  Oot3dNativeGame::TriAevumOot3dInputBackend inputBackend(
      std::move(inputConfig));
  triaevum::module::InputHostServiceAdapterV1 inputService(inputBackend);
  if (session.RegisterService(
          TRIAEVUM_SERVICE_INPUT_V1,
          triaevum::module::InputHostServiceAdapterV1::Invoke, &inputService) !=
      triaevum::module::ServiceRegistrationResult::Registered) {
    throw std::runtime_error("input host-service registration failed");
  }
  auto &window = GetActiveFast3dWindowForDemo();
  window.SetTargetFps(60);
  window.SetTextureFilter(Fast::FILTER_LINEAR);
  auto &api = GetActiveRenderingApiForDemo(window);
  std::uint64_t immediatePresentationCount = 0U;
  Oot3dNativeGame::TriAevumOot3dPicaRendererOptions rendererOptions;
  rendererOptions.ShouldPresentTransfer =
      [&picaHost, &immediatePresentationCount](const auto &transfer) {
        const bool present = picaHost->Scanout().ShouldPresent(
            0U, transfer.Transfer.OutputAddress);
        immediatePresentationCount += present ? 1U : 0U;
        return present;
      };
  Oot3dNativeGame::TriAevumOot3dPicaRenderer picaRenderer(
      api, *picaHost, std::move(rendererOptions));
  RuntimeSessionStopGuard sessionStopGuard(session);

  if (!session.Initialize(&sessionError)) {
    throw std::runtime_error("private module initialization failed: " +
                             sessionError.message);
  }

  WindowDemoTimingState timing;
  FramebufferScreenshotState screenshot;
  std::uint32_t frameCount = 0U;
  std::uint64_t presentationTimeNs = 0U;
  while (window.IsRunning()) {
    WindowDemoFrameTiming frameTiming;
    if (!PrepareNextWindowDemoFrame(arguments.Host, window, timing,
                                    frameTiming)) {
      continue;
    }

    const std::uint32_t width = std::max<std::uint32_t>(1U, window.GetWidth());
    const std::uint32_t height =
        std::max<std::uint32_t>(1U, window.GetHeight());
    window.GetMouseStateManager()->StartFrame();
    gui->StartDraw();
    window.StartFrame();
    if (!inputBackend.Poll(window, frameTiming.DeltaSeconds)) {
      window.Close();
      break;
    }
    api.UpdateFramebufferParameters(0, width, height, 1U, false, true, true,
                                    true);
    api.StartFrame();
    api.StartDrawToFramebuffer(0, 1.0F);

    if (!picaRenderer.PollCompletions(&picaError)) {
      throw std::runtime_error("PICA completion polling failed: " + picaError);
    }
    presentationTimeNs += kSimulationStepNanoseconds;
    const TriAevumFrameInputV1 input = {
        sizeof(TriAevumFrameInputV1),
        0U,
        presentationTimeNs,
        kSimulationStepNanoseconds,
        {nullptr, 0U},
    };
    RequireModuleStatus(session.RunFrame(input), "private module frame");
    if (!picaRenderer.DrainPending(&picaError)) {
      throw std::runtime_error("PICA batch submission failed: " + picaError);
    }

    gui->EndDraw();
    MaybeWriteFramebufferScreenshot(arguments.Host, api, width, height,
                                    frameCount, screenshot);
    window.EndFrame();
    if (!picaRenderer.PollCompletions(&picaError)) {
      throw std::runtime_error("PICA completion publication failed: " +
                               picaError);
    }

    ++frameCount;
    ApplyWindowDemoFrameLimit(arguments.Host, window, frameCount);
  }

  const auto &stats = picaRenderer.Stats();
  const auto &audioStats = audioBackend.Stats();
  const auto &inputStats = inputBackend.Stats();
  const auto filesystemStats = filesystemBackend->Stats();
  const auto topFramebuffer = picaHost->Scanout().Framebuffer(0U);
  std::cout << "TriAevum module host completed: frames=" << frameCount
            << " batches=" << stats.Batches << " draws=" << stats.Draws
            << " transfers=" << stats.DisplayTransfers
            << " immediate_presentations=" << immediatePresentationCount
            << " retained_presentations="
            << picaRenderer.RetainedScanoutPresentationCount()
            << " fills=" << stats.MemoryFills
            << " completions=" << stats.Completions
            << " audio_submissions=" << audioStats.Submissions
            << " audio_frames=" << audioStats.AcceptedFrames
            << " audio_bytes=" << audioStats.AcceptedBytes
            << " audio_queued=" << audioStats.LastQueuedFrames
            << " input_polls=" << inputStats.HostPolls
            << " input_reads=" << inputStats.ServiceReads
            << " fs_opens=" << filesystemStats.opens
            << " fs_reads=" << filesystemStats.reads
            << " fs_read_bytes=" << filesystemStats.readBytes
            << " fs_writes=" << filesystemStats.writes
            << " scanout_revision=" << picaHost->Scanout().Revision()
            << " top_framebuffer="
            << (topFramebuffer.has_value() ? topFramebuffer->addressLeft : 0U)
            << " force_black=" << (picaHost->Scanout().LcdForceBlack() ? 1 : 0)
            << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    ModuleHostArguments arguments;
    if (!ParseArguments(argc, argv, &arguments)) {
      PrintUsage();
      return 2;
    }
    RunModuleHost(arguments);
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "TriAevum: " << exception.what() << '\n';
    return 1;
  }
}
