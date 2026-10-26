#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Custom texture pack import for F1's Materials > Texture packs (UX plan
// WP-07). The rules are shared with Forge's import (Enhancements page) and
// must stay identical: I:/_triaevum_work/ux-wp07/TEXTURE_IMPORT_RULES.md.
// Everything here blocks on the file system or a dialog: call it from a
// worker thread, never from frame code.
namespace Fast::Extension {

struct AzaharTexturePackImportLimits {
    size_t MaximumFiles = 200000U;
    uint64_t MaximumFileBytes = 256ULL * 1024ULL * 1024ULL;
    uint64_t MaximumTotalBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;
};

struct AzaharTexturePackImportRequest {
    // The folder the user chose.
    std::filesystem::path Source;
    // The managed load folder, D/textures/load/<title id>
    // (AzaharTextureHostDefaults::LoadDirectory). It is replaced as a whole.
    std::filesystem::path Destination;
    // The installed game's program ID, used to pick among title folders.
    uint64_t InstalledTitleId = 0;
    AzaharTexturePackImportLimits Limits;
};

struct AzaharTexturePackImportResult {
    bool Imported = false;
    // One English line for F1: what was imported, or why nothing was.
    std::string Message;
    std::filesystem::path Destination;
    // .png textures the runtime loads (tex1_... names or pack.json
    // mappings); DDS/KTX copied but not loaded by the runtime yet; files
    // that are not part of a pack (readme.txt, thumbs.db, ...), not copied.
    size_t Textures = 0;
    size_t NotLoadedYet = 0;
    size_t Skipped = 0;
    uint64_t CopiedBytes = 0;
    // The title folder the pack came from, when it had one.
    std::optional<uint64_t> SourceTitleId;
};

// "0004000000033500" style: exactly 16 hex digits starting with 00040000.
[[nodiscard]] std::optional<uint64_t> ParseAzaharTitleFolderName(const std::string& name);

// Copies a pack into Request.Destination: staging sibling, verification,
// then a swap that keeps the previous pack on every failure.
[[nodiscard]] AzaharTexturePackImportResult ImportAzaharTexturePack(const AzaharTexturePackImportRequest& request);

// The platform folder dialog: Windows IFileOpenDialog (FOS_PICKFOLDERS, on
// the calling thread as a COM STA); Linux zenity, then kdialog. Returns the
// chosen folder as UTF-8, or nothing when cancelled or unavailable (then
// *error says why, empty on a plain cancel).
[[nodiscard]] std::optional<std::string> PickFolderWithNativeDialog(const std::string& title, std::string* error);

// False where no native folder dialog exists (Android); *reason explains.
[[nodiscard]] bool NativeFolderDialogAvailable(std::string* reason);

// UTF-8 text from and to file system paths (F1 strings are UTF-8).
[[nodiscard]] std::filesystem::path PathFromUtf8(const std::string& text);
[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);

} // namespace Fast::Extension
