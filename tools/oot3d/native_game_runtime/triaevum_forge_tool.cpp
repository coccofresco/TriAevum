// The Forge's native services on Android (FORGE_SPEC.md 4.4, point 6).
//
// An APK cannot run helper executables, so the Rust Forge (libtriaevum_forge)
// loads this runtime library and calls triaevum_forge_tool with the command
// line a desktop helper takes; argv[0] names the helper:
//
//   "runtime"           the runtime's tool modes (RunTriAevumToolMode)
//   "pass-compiler"     oot3d_native_pica_aot_compiler
//   "pipeline-prepare"  triaevum_nri_pipeline_prepare
//
// The helpers' standard output and error go to the two files the caller
// names, as their pipes did on the desktop; result files are written to the
// paths in the arguments. One call runs at a time. The installer process
// calls it, never a process that runs the game.

#include "triaevum_forge_tool.h"

#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <string_view>
#include <unistd.h>

namespace {

// Points descriptor `target` at a new file `path` for this object's lifetime.
class StreamRedirect {
public:
    StreamRedirect(int target, const char* path) : target_(target) {
        const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (file < 0)
            return;
        saved_ = dup(target);
        active_ = saved_ >= 0 && dup2(file, target) >= 0;
        close(file);
    }
    ~StreamRedirect() {
        if (saved_ < 0)
            return;
        if (active_)
            dup2(saved_, target_);
        close(saved_);
    }
    StreamRedirect(const StreamRedirect&) = delete;
    StreamRedirect& operator=(const StreamRedirect&) = delete;

    bool Active() const { return active_; }

private:
    int target_;
    int saved_ = -1;
    bool active_ = false;
};

void FlushStreams() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
}

int RunTool(int argc, char** argv) {
    const std::string_view tool = argv[0];
    if (tool == "runtime") {
        if (const auto status = RunTriAevumToolMode(argc, argv))
            return *status;
        std::cerr << "oot3d_native_game: not a Forge tool mode\n";
        return 2;
    }
    if (tool == "pass-compiler")
        return TriAevumPassCompilerMain(argc, argv);
    if (tool == "pipeline-prepare")
        return TriAevumPipelinePrepareMain(argc, argv);
    std::cerr << "triaevum_forge_tool: unknown tool\n";
    return 2;
}

} // namespace

// Returns the helper's exit status, or -1 when it could not run at all
// (invalid arguments, output files that cannot be created).
extern "C" __attribute__((visibility("default"))) int triaevum_forge_tool(
    int argc, char** argv, const char* stdout_path, const char* stderr_path) {
    if (argc < 1 || !argv || !argv[0] || !stdout_path || !stderr_path)
        return -1;
    static std::mutex busy;
    const std::lock_guard lock(busy);
    FlushStreams();
    int status = -1;
    {
        const StreamRedirect output(STDOUT_FILENO, stdout_path);
        const StreamRedirect errors(STDERR_FILENO, stderr_path);
        if (!output.Active() || !errors.Active())
            return -1;
        try {
            status = RunTool(argc, argv);
        } catch (const std::exception& exception) {
            // The runtime's own top-level message for a failed tool mode.
            std::cerr << "oot3d_native_game: " << exception.what() << '\n';
            status = 1;
        } catch (...) {
            std::cerr << "oot3d_native_game: unknown error\n";
            status = 1;
        }
        FlushStreams();
    }
    return status;
}
