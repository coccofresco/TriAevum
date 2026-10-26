#pragma once

#include <string>
#include <string_view>

// How the game reports the error that ends it (UX plan WP-03).
//
// Every error exit writes one "TRIAEVUM_FATAL <message>" line to the error
// output: Forge captures it (desktop Play: data/logs/game-stderr.log;
// Android: logs/native-stderr.log) and shows it in its diagnostics. An
// interactive launch also shows a native dialog with the message and the log
// folder; scripted runs (timelines, frame limits, captures, benchmarks), tool
// modes and TRIAEVUM_NO_FATAL_DIALOG=1 never do, so automation is never
// blocked by a modal window. Android shows no dialog: the installer's
// Diagnostics screen reads the error output.
namespace TriAevum {

inline constexpr std::string_view kFatalLinePrefix = "TRIAEVUM_FATAL ";

// Whether an error exit of the command line argv shows a dialog, given the
// value of TRIAEVUM_NO_FATAL_DIALOG (nullptr when unset).
[[nodiscard]] bool FatalDialogWanted(int argc, const char* const* argv, const char* noDialog);
// The same for this process's environment.
[[nodiscard]] bool FatalDialogWanted(int argc, const char* const* argv);

// The "TRIAEVUM_FATAL <message>" line (one line, without a newline).
[[nodiscard]] std::string FatalLine(std::string_view message);

// The dialog's text: the message (bounded), the log folder (empty when the
// error came before the log was opened) and what to do.
[[nodiscard]] std::string FatalDialogText(std::string_view message, std::string_view logDirectory);

inline constexpr const char* kFatalDialogTitle = "TriAevum could not continue";

} // namespace TriAevum
