#pragma once

// Where the game's messages go before its log folder is known (UX plan
// WP-05C). The runtime log opens once the launch profile has named the log
// folder (--log-directory, D/logs); an error before that (an unreadable
// profile, a refused start) is written to a bounded spool, so a game with no
// console (Windows GUI subsystem, Android) still leaves a trace:
//
// - the spool lives in the installation's writable state folder (Windows
//   <exe>/data, elsewhere the launch profile's folder: the activation root or
//   Android's app storage), never in a package folder that may be read-only
//   (AppImage, Flatpak); without one, in <temp>/TriAevum;
// - each spool file is bounded (at the limit it moves to <name>.1 and a new
//   one starts);
// - once the log folder is validated, the spool is appended to
//   <log folder>/early-errors.log and removed;
// - stderr keeps every message as the last fallback.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace TriAevum {

inline constexpr const char *kEarlyLogSpoolName = "triaevum-early.log";
inline constexpr const char *kEarlyLogName = "early-errors.log";
inline constexpr std::uintmax_t kEarlyLogLimit = 256U * 1024U;

// Creates the folder and checks that a file can be written in it.
bool PrepareWritableDirectory(const std::filesystem::path &directory, std::string *error);

// `<temp>/TriAevum`, or empty when the system has no temporary folder.
[[nodiscard]] std::filesystem::path TemporaryDiagnosticDirectory();

// The spool file: in `stateDirectory` when it is writable, else in
// TemporaryDiagnosticDirectory(); empty when neither is.
[[nodiscard]] std::filesystem::path EarlyLogSpoolPath(const std::filesystem::path &stateDirectory);

// Appends one timestamped line, keeping the file below `limit` bytes.
bool AppendEarlyLog(const std::filesystem::path &file, std::string_view message,
                    std::uintmax_t limit = kEarlyLogLimit);

// Appends the spool (and its .1) to `logDirectory`/early-errors.log, then
// removes them. True when no spool is left.
bool HandOffEarlyLog(const std::filesystem::path &spool, const std::filesystem::path &logDirectory,
                     std::string *error);

// The process's spool: chosen once (SetEarlyLogStateDirectory), written by
// EarlyLog. Each message also goes to stderr.
void SetEarlyLogStateDirectory(const std::filesystem::path &stateDirectory);
[[nodiscard]] std::filesystem::path EarlyLogSpool();
void EarlyLog(std::string_view message);

} // namespace TriAevum
