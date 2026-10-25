#pragma once

#include <optional>

// The native work Forge requests, as functions with the command lines of the
// desktop helpers (argv[0] is ignored). The desktop runs them as processes;
// the Android runtime library runs them in process (triaevum_forge_tool.cpp).

// The runtime's tool modes: --product-info, --verify-title-plugin PLUGIN,
// --game-language-info ROMFS [CODE_SHA256] and --prebuild-interior-sun ...
// Returns the exit status, or nullopt when the arguments launch the game.
std::optional<int> RunTriAevumToolMode(int argc, char** argv);

// oot3d_native_pica_aot_compiler: renderer pass cache or portable pack.
int TriAevumPassCompilerMain(int argc, char** argv);

// triaevum_nri_pipeline_prepare: the headless device pipeline job.
int TriAevumPipelinePrepareMain(int argc, char** argv);
