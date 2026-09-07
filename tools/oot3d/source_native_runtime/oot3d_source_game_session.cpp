#include "oot3d_source_game_session.h"

#include "oot3d_ctr_host_services.h"
#include "oot3d_ctr_pica_backend.h"
#include "oot3d_source_data_bindings.h"
#include "oot3d_source_dsp_mixer.h"
#include "oot3d_source_execution_stack.h"
#include "oot3d_source_host_pump.h"
#include "oot3d_source_pica_submission.h"
#include "oot3d_source_process_image.h"

#include <iostream>
#include <memory>
#include <string_view>

#ifdef OOT3D_SOURCE_PROCESS_PROFILE_AVAILABLE
#include "oot3d_source_process_profile.h"
#endif

#ifdef OOT3D_SOURCE_EXECUTABLE_CLOSURE
extern "C" void oot3d_register_target_function_surface();
extern "C" void oot3d_register_startup_target_function_overlay();
extern "C" void oot3d_process_entry(std::uint32_t, std::uint32_t);
#endif

namespace Oot3dSourceRuntime {
namespace {

#ifdef OOT3D_SOURCE_EXECUTABLE_CLOSURE
struct ProcessEntryContext {
    std::uint32_t Argument0 = 0;
};

void RegisterTargetSurface(void*) {
    oot3d_register_startup_target_function_overlay();
    oot3d_register_target_function_surface();
}

void RunProcessEntry(void* opaque) {
    const auto& context = *static_cast<ProcessEntryContext*>(opaque);
    oot3d_process_entry(context.Argument0, 0);
}

bool ReportInvocationFailure(const SourceExecutionResult& result,
                             std::string_view operation) {
    if (!result.Invoked) {
        std::cerr << operation << " was not invoked on the source stack\n";
        return false;
    }
    if (result.ThreadExitRequested) {
        std::cerr << operation << " requested target thread exit\n";
        return false;
    }
    if (result.Exception == nullptr) {
        return true;
    }
    try {
        std::rethrow_exception(result.Exception);
    } catch (const std::exception& exception) {
        std::cerr << operation << " failed: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << operation << " failed with an unknown exception\n";
    }
    return false;
}
#endif

} // namespace

std::optional<SourceGameLaunchOptions> ParseSourceGameLaunchOptions(
    int argc, char** argv) {
    SourceGameLaunchOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (index + 1 >= argc) {
            return std::nullopt;
        }
        const std::string value = argv[++index];
        if (option == "--code-bin") {
            options.CodeBin = value;
        } else if (option == "--romfs-image") {
            options.Fs.RomFsImagePath = value;
        } else if (option == "--romfs-offset") {
            options.Fs.RomFsImageOffset = std::stoull(value, nullptr, 0);
        } else if (option == "--romfs-size") {
            options.Fs.RomFsImageSize = std::stoull(value, nullptr, 0);
        } else if (option == "--savedata") {
            options.Fs.SaveDataDirectory = value;
        } else {
            return std::nullopt;
        }
    }
    if (options.CodeBin.empty()) {
        return std::nullopt;
    }
    if (!options.Fs.RomFsImagePath.empty() && options.Fs.RomFsImageSize == 0) {
        std::error_code error;
        const auto size =
            std::filesystem::file_size(options.Fs.RomFsImagePath, error);
        if (error || options.Fs.RomFsImageOffset > size) {
            return std::nullopt;
        }
        options.Fs.RomFsImageSize = size - options.Fs.RomFsImageOffset;
    }
    return options;
}

void PrintSourceGameUsage() {
    std::cerr << "usage: oot3d_source_game --code-bin <path> "
                 "[--romfs-image <path> [--romfs-offset <integer>] "
                 "[--romfs-size <integer>]] [--savedata <directory>]\n";
}

int RunSourceGameSession(SourceGameLaunchOptions options) {
#ifdef OOT3D_SOURCE_PROCESS_PROFILE_AVAILABLE
    std::string error;
    auto process = LoadDirectMappedSourceProcessImage(
        Oot3dSourceProcessGenerated::kDescriptor, options.CodeBin, &error);
    if (!process.has_value()) {
        std::cerr << error << '\n';
        return 1;
    }
    if (process->Memory.ResolveRead(process->EntryAddress, 4).size() != 4) {
        std::cerr << "source entrypoint is not mapped as readable memory\n";
        return 1;
    }
    ScopedSourceAddressSpace sourceAddressBinding(process->Memory);
    std::cout << "process image mounted: " << process->Memory.RegionCount()
              << " regions, entrypoint 0x" << std::hex << process->EntryAddress
              << std::dec << '\n';
    auto picaFrontend = std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    picaFrontend->SetDiagnosticHistoryEnabled(false);
    auto picaSubmission = std::make_unique<SourcePicaSubmission>(
        process->Memory, Oot3dSourceProcessGenerated::kDescriptor);
    picaFrontend->SetPacketSink(&picaSubmission->Queue());
    auto dspMixer = std::make_unique<SourceDspMixer>(
        process->Memory, Oot3dSourceProcessGenerated::kDescriptor);
    auto gpuBackend = std::make_unique<CtrPicaBackend>(*picaFrontend);

    SourceCtrRuntimeProfile ctrProfile;
    ctrProfile.Fs = std::move(options.Fs);
    ctrProfile.GpuBackend = gpuBackend.get();
    ctrProfile.Apt.StaticBufferTableAddress =
        process->PrimaryThread.ThreadPointer + 0x180U;
    ctrProfile.Dsp.StaticBufferTableAddress =
        process->PrimaryThread.ThreadPointer + 0x180U;
    auto ctrRuntime = std::make_unique<SourceCtrRuntime>(
        process->Memory, std::move(ctrProfile));
    ctrRuntime->Router().ConfigureThreadRuntime(process->Memory,
                                                process->PrimaryThread);
    ScopedCtrHostServices ctrBinding(process->Memory, process->PrimaryThread,
                                     ctrRuntime->Router());
    CtrHandle srvHandle = 0;
    if (oot3d_host_ctr_connect_to_port(&srvHandle, "srv:") < 0 ||
        !ctrRuntime->Router().Contains(srvHandle)) {
        std::cerr << "source CTR service manager is not reachable\n";
        return 1;
    }
    std::cout << "CTR core services ready: srv handle 0x" << std::hex
              << srvHandle << std::dec << '\n';

    if (options.PicaReady) {
        options.PicaReady(picaSubmission->Queue());
    }
    SourceHostPump hostPump(ctrRuntime->Hid(), *dspMixer,
                            std::move(options.InputProvider),
                            std::move(options.AudioSink));
    hostPump.Start();
    std::cout << "native HID and DSP host pump active\n";
#ifdef OOT3D_SOURCE_EXECUTABLE_CLOSURE
    SourceExecutionStack sourceStack;
    if (!sourceStack.Initialize(&error)) {
        std::cerr << "source execution stack unavailable: " << error << '\n';
        return 1;
    }
    if (!ReportInvocationFailure(
            sourceStack.Invoke(&RegisterTargetSurface, nullptr),
            "target function registry")) {
        return 1;
    }
    std::cout << "target function registry ready\n";
    ProcessEntryContext entryContext{process->PrimaryThread.Argument0};
    if (!ReportInvocationFailure(
            sourceStack.Invoke(&RunProcessEntry, &entryContext),
            "native process entry")) {
        return 1;
    }
#endif
    return 0;
#else
    (void)options;
    std::cerr << "source process profile is not bound to this build\n";
    return 1;
#endif
}

} // namespace Oot3dSourceRuntime
