#pragma once

#include "oot3d_ctr_hid_producer.h"
#include "oot3d_source_ctr_runtime.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <span>

namespace Oot3dNativeGame {
class Oot3dNativePicaSubmissionQueue;
}

namespace Oot3dSourceRuntime {

struct SourceGameLaunchOptions {
    std::filesystem::path CodeBin;
    CtrFsProfile Fs;
    std::function<CtrHidState()> InputProvider;
    std::function<void(std::span<const std::int16_t>)> AudioSink;
    std::function<void(Oot3dNativeGame::Oot3dNativePicaSubmissionQueue&)>
        PicaReady;
};

std::optional<SourceGameLaunchOptions> ParseSourceGameLaunchOptions(
    int argc, char** argv);
void PrintSourceGameUsage();
int RunSourceGameSession(SourceGameLaunchOptions options);

} // namespace Oot3dSourceRuntime
