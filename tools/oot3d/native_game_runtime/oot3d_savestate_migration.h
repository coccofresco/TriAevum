#pragma once

// The save-state folder's move to the managed data tree (UX plan WP-05C).
// Windows kept save states beside the executable (<P>/savestates); an
// installation whose launch profile names --savestate-directory (D/savestates)
// uses the data folder instead. On the first such launch the states are
// copied there, so nothing the player saved disappears:
//
// - only when savestates.json has no chosen folder (its `Directory` is absent
//   or empty: the writer stores "" for the default), no --quick-state fixes
//   the file, the earlier folder exists and differs from the new one, and no
//   completed record of this migration is in config/migrations.json;
// - every file (states and anything beside them) is copied through a
//   temporary file and compared byte for byte with its source before it gets
//   its name; an existing file is never overwritten (identical: counted as
//   present; different: left out and listed as a conflict);
// - only after every copy verified: `LastState` is rewritten to the copy, then
//   the completion record is written; a chosen `Directory` is never touched;
// - the earlier folder and its files stay, with README-moved.txt;
// - anything that fails (a copy, a preference or record write) leaves the
//   earlier folder the session's default and is retried on the next launch,
//   where identical files are not copied again.
//
// Nothing is ever deleted.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

inline constexpr const char *kSavestateMigrationId = "savestates_to_data_folder";
inline constexpr const char *kMigrationsFormat = "triaevum_migrations_v1";
inline constexpr const char *kSavestateMovedReadme = "README-moved.txt";

struct SavestateMigrationRequest {
    // config/savestates.json (SavestateLibrary's preferences).
    std::filesystem::path PreferencesPath;
    // config/migrations.json.
    std::filesystem::path MigrationsPath;
    // The earlier default folder.
    std::filesystem::path LegacyDirectory;
    // The managed default folder (--savestate-directory).
    std::filesystem::path ManagedDirectory;
    // --quick-state pins one file: nothing is moved.
    bool FixedSlot = false;
};

enum class SavestateMigrationStatus {
    // Nothing to move (or already moved, or a folder was chosen).
    NotNeeded,
    // Copied and verified; LastState and the record are written.
    Completed,
    // The earlier folder stays the default for this session.
    Failed,
};

struct SavestateMigrationResult {
    SavestateMigrationStatus Status = SavestateMigrationStatus::NotNeeded;
    size_t CopiedFiles = 0;
    // Identical files already in the managed folder (not copied again).
    size_t PresentFiles = 0;
    std::uintmax_t CopiedBytes = 0;
    // Relative paths whose managed copy differs: never overwritten.
    std::vector<std::filesystem::path> Conflicts;
    bool LastStateRewritten = false;
    // The session's default save-state folder: the managed folder, or the
    // earlier one while the move has not completed.
    std::filesystem::path DefaultDirectory;
    // One line for the log.
    std::string Message;
};

[[nodiscard]] SavestateMigrationResult MigrateLegacySavestates(const SavestateMigrationRequest &request);

[[nodiscard]] const char *SavestateMigrationStatusName(SavestateMigrationStatus status) noexcept;

} // namespace Oot3dNativeGame
