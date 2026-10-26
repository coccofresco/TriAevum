#include "fast/extension/azahar_texture_pack_import.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <process.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Fast::Extension {
namespace {

namespace fs = std::filesystem;

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::string Hex16(uint64_t value) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

int CurrentProcessId() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

// Symbolic links and, on Windows, junctions and every other reparse point.
bool IsLinkOrReparsePoint(const fs::path& path, const fs::file_status& status) {
    if (fs::is_symlink(status)) return true;
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    (void)path;
    return false;
#endif
}

bool IsHexDigit(char character) {
    return std::isxdigit(static_cast<unsigned char>(character)) != 0;
}
bool IsDecimalDigit(char character) {
    return std::isdigit(static_cast<unsigned char>(character)) != 0;
}

// Azahar/Citra names: tex1_<width>x<height>_<hash>..., case-insensitive.
bool HasAzaharTextureStem(const fs::path& file) {
    const auto stem = Lowercase(PathToUtf8(file.stem()));
    constexpr std::string_view prefix = "tex1_";
    if (stem.rfind(prefix, 0) != 0) return false;
    size_t at = prefix.size();
    const auto digits = [&](auto accept) {
        const size_t start = at;
        while (at < stem.size() && accept(stem[at])) ++at;
        return at > start;
    };
    if (!digits(IsDecimalDigit) || at >= stem.size() || stem[at++] != 'x') return false;
    if (!digits(IsDecimalDigit) || at >= stem.size() || stem[at++] != '_') return false;
    return digits(IsHexDigit);
}

constexpr uint32_t kMaximumPngSide = 16384U;
constexpr uint64_t kMaximumJsonBytes = 16ULL * 1024ULL * 1024ULL;
constexpr size_t kMaximumDepth = 32U;

// The PNG signature, then IHDR with a width and height in 1..16384; the
// refusal text otherwise.
std::optional<std::string> PngHeaderProblem(const fs::path& file, const std::string& shown) {
    std::ifstream input(file, std::ios::binary);
    std::array<unsigned char, 24> header{};
    constexpr std::array<unsigned char, 8> signature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (!input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size())) ||
        !std::equal(signature.begin(), signature.end(), header.begin()) || header[12] != 'I' || header[13] != 'H' ||
        header[14] != 'D' || header[15] != 'R') {
        return shown + " is not a PNG image";
    }
    const auto bigEndian = [&](size_t offset) {
        return (uint32_t{header[offset]} << 24U) | (uint32_t{header[offset + 1]} << 16U) |
               (uint32_t{header[offset + 2]} << 8U) | uint32_t{header[offset + 3]};
    };
    const uint32_t width = bigEndian(16), height = bigEndian(20);
    if (width < 1U || width > kMaximumPngSide || height < 1U || height > kMaximumPngSide) {
        return shown + " is " + std::to_string(width) + "x" + std::to_string(height) + " pixels (at most " +
               std::to_string(kMaximumPngSide) + " per side)";
    }
    return std::nullopt;
}

std::optional<nlohmann::json> ReadJsonObject(const fs::path& file, std::string* problem) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        *problem = "it cannot be read";
        return std::nullopt;
    }
    try {
        auto document = nlohmann::json::parse(input);
        if (!document.is_object()) {
            *problem = "its root is not an object";
            return std::nullopt;
        }
        return document;
    } catch (const std::exception& exception) {
        *problem = exception.what();
        return std::nullopt;
    }
}

// A name every platform can store (Forge refuses the same ones).
bool IsStorableComponent(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ') return false;
    return std::none_of(name.begin(), name.end(), [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return value < 0x20U || value == 0x7FU || character == '<' || character == '>' || character == ':' ||
               character == '"' || character == '|' || character == '?' || character == '*' || character == '\\' ||
               character == '/';
    });
}

// pack.json's "textures" values name the files it maps (a name or a list).
void CollectMappedFileNames(const nlohmann::json& pack, std::set<std::string>& names) {
    const auto textures = pack.find("textures");
    if (textures == pack.end() || !textures->is_object()) return;
    const auto add = [&](const nlohmann::json& value) {
        if (!value.is_string()) return;
        const auto name = Lowercase(PathToUtf8(PathFromUtf8(value.get<std::string>()).filename()));
        if (!name.empty()) names.insert(name);
    };
    for (const auto& item : textures->items()) {
        if (item.value().is_array()) {
            for (const auto& value : item.value()) add(value);
        } else {
            add(item.value());
        }
    }
}

bool StartsWithComponents(const fs::path& path, const fs::path& prefix) {
    auto value = path.begin();
    for (auto part = prefix.begin(); part != prefix.end(); ++part, ++value) {
        if (value == path.end() || *value != *part) return false;
    }
    return true;
}

fs::path RelativeBelow(const fs::path& path, const fs::path& prefix) {
    auto value = path.begin();
    for (auto part = prefix.begin(); part != prefix.end(); ++part) ++value;
    fs::path relative;
    for (; value != path.end(); ++value) relative /= *value;
    return relative;
}

struct SourceFile {
    fs::path Relative;
    uint64_t Bytes = 0;
};

enum class FileKind : uint8_t { Png, NotLoadedYet, Json, Skipped };

struct PackFile {
    fs::path Source;
    fs::path Relative;
    uint64_t Bytes = 0;
    FileKind Kind = FileKind::Skipped;
};

// A relative path for messages, with forward slashes.
std::string GenericUtf8(const fs::path& path) {
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

AzaharTexturePackImportResult Refuse(std::string message) {
    AzaharTexturePackImportResult result;
    result.Message = std::move(message);
    return result;
}

std::string Plural(size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1U ? one : many);
}

std::atomic<unsigned> gImportSerial{0U};

// Removes the staging folder unless the swap consumed it.
struct StagingGuard {
    fs::path Path;
    ~StagingGuard() {
        if (Path.empty()) return;
        std::error_code ignored;
        fs::remove_all(Path, ignored);
    }
};

} // namespace

fs::path PathFromUtf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string PathToUtf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::optional<uint64_t> ParseAzaharTitleFolderName(const std::string& name) {
    if (name.size() != 16U || !std::all_of(name.begin(), name.end(), IsHexDigit)) return std::nullopt;
    if (Lowercase(name.substr(0, 8)) != "00040000") return std::nullopt;
    return std::stoull(name, nullptr, 16);
}

AzaharTexturePackImportResult ImportAzaharTexturePack(const AzaharTexturePackImportRequest& request) {
    const auto& limits = request.Limits;
    if (request.Destination.empty()) return Refuse("Start the game from Forge to use the managed texture folder");
    fs::path source = request.Source.lexically_normal();
    if (!source.has_filename() && source.has_parent_path()) source = source.parent_path();
    std::error_code error;
    if (source.empty() || !fs::is_directory(source, error)) return Refuse("Choose a folder that exists");
    const fs::path destination = request.Destination.lexically_normal();

    // 1. Walk the chosen folder once: links, special files and folders
    //    nested too deep refuse the import; files are listed.
    std::vector<SourceFile> files;
    fs::recursive_directory_iterator iterator(source, fs::directory_options::none, error), end;
    if (error) return Refuse("Cannot read " + PathToUtf8(source) + ": " + error.message());
    for (; iterator != end; iterator.increment(error)) {
        if (error) return Refuse("Cannot read " + PathToUtf8(source) + ": " + error.message());
        const auto& path = iterator->path();
        const auto relative = path.lexically_relative(source);
        const auto shown = GenericUtf8(relative);
        const auto status = iterator->symlink_status(error);
        if (error) return Refuse("Cannot read " + shown + ": " + error.message());
        if (IsLinkOrReparsePoint(path, status) || (!fs::is_directory(status) && !fs::is_regular_file(status))) {
            return Refuse(shown + " is a link or a special file; texture packs cannot contain links");
        }
        if (fs::is_directory(status)) {
            if (static_cast<size_t>(iterator.depth()) + 1U > kMaximumDepth) {
                return Refuse(shown + " is more than " + std::to_string(kMaximumDepth) + " folders deep");
            }
            continue;
        }
        const auto bytes = iterator->file_size(error);
        if (error) return Refuse("Cannot read " + shown + ": " + error.message());
        files.push_back({relative, bytes});
    }

    // 2. Where the pack is. Title folders: the first title-id component of
    //    each file's folders. One title: its folder; several: the installed
    //    title's; none: the chosen folder itself when it is named after a
    //    title, else a flat pack without its common top folder.
    std::set<std::pair<fs::path, uint64_t>> roots;
    for (const auto& file : files) {
        fs::path root;
        const auto parent = file.Relative.parent_path();
        for (const auto& part : parent) {
            root /= part;
            if (const auto title = ParseAzaharTitleFolderName(PathToUtf8(part))) {
                roots.insert({root, *title});
                break;
            }
        }
    }
    fs::path packRoot;
    std::optional<uint64_t> sourceTitle;
    if (roots.empty()) {
        sourceTitle = ParseAzaharTitleFolderName(PathToUtf8(source.filename()));
        if (!sourceTitle && !files.empty()) {
            packRoot = files.front().Relative.parent_path();
            for (const auto& file : files) {
                while (!packRoot.empty() && !StartsWithComponents(file.Relative.parent_path(), packRoot))
                    packRoot = packRoot.parent_path();
            }
        }
    } else {
        std::set<uint64_t> titles;
        for (const auto& [root, title] : roots) titles.insert(title);
        std::vector<std::pair<fs::path, uint64_t>> chosen;
        for (const auto& entry : roots)
            if (titles.size() == 1U || entry.second == request.InstalledTitleId) chosen.push_back(entry);
        if (chosen.empty()) {
            std::string ids;
            for (const auto title : titles) ids += (ids.empty() ? "" : ", ") + Hex16(title);
            return Refuse("The pack has folders for several games (" + ids + ") but none for the installed game " +
                          Hex16(request.InstalledTitleId));
        }
        if (chosen.size() != 1U) {
            return Refuse("The pack has more than one folder for " + Hex16(chosen.front().second) +
                          "; import one of them");
        }
        packRoot = chosen.front().first;
        sourceTitle = chosen.front().second;
    }

    // 3. Check the pack's files before copying anything.
    std::vector<PackFile> pack;
    std::set<std::string> mappedNames;
    uint64_t totalBytes = 0;
    AzaharTexturePackImportResult result;
    for (const auto& file : files) {
        if (!StartsWithComponents(file.Relative, packRoot) ||
            std::distance(file.Relative.begin(), file.Relative.end()) <=
                std::distance(packRoot.begin(), packRoot.end()))
            continue;
        PackFile entry{source / file.Relative, RelativeBelow(file.Relative, packRoot), file.Bytes};
        const auto shown = GenericUtf8(entry.Relative);
        const auto extension = Lowercase(PathToUtf8(entry.Relative.extension()));
        if (extension == ".png") entry.Kind = FileKind::Png;
        else if (extension == ".dds" || extension == ".ktx") entry.Kind = FileKind::NotLoadedYet;
        else if (extension == ".json") entry.Kind = FileKind::Json;
        if (entry.Kind == FileKind::Skipped) {
            ++result.Skipped;
            continue;
        }
        for (const auto& part : entry.Relative) {
            if (!IsStorableComponent(PathToUtf8(part)))
                return Refuse(shown + " has a name that cannot be stored on this system");
        }
        if (entry.Bytes > limits.MaximumFileBytes) {
            return Refuse(shown + " is larger than " + std::to_string(limits.MaximumFileBytes) + " bytes");
        }
        if (pack.size() + 1U > limits.MaximumFiles) {
            return Refuse("The pack has more than " + std::to_string(limits.MaximumFiles) + " files");
        }
        totalBytes += entry.Bytes;
        if (totalBytes > limits.MaximumTotalBytes) {
            return Refuse("The pack is larger than " + std::to_string(limits.MaximumTotalBytes) + " bytes");
        }
        if (entry.Kind == FileKind::Png) {
            if (auto problem = PngHeaderProblem(entry.Source, shown)) return Refuse(std::move(*problem));
        }
        if (entry.Kind == FileKind::Json) {
            if (entry.Bytes > kMaximumJsonBytes) {
                return Refuse(shown + " is larger than " + std::to_string(kMaximumJsonBytes) + " bytes");
            }
            std::string problem;
            const auto document = ReadJsonObject(entry.Source, &problem);
            if (!document) return Refuse(shown + " is not a JSON object" + (problem.empty() ? "" : ": " + problem));
            if (Lowercase(GenericUtf8(entry.Relative)) == "pack.json") CollectMappedFileNames(*document, mappedNames);
        }
        pack.push_back(std::move(entry));
    }
    const auto loads = [&](const PackFile& file) {
        return file.Kind == FileKind::Png && (HasAzaharTextureStem(file.Relative) ||
                                              mappedNames.contains(Lowercase(PathToUtf8(file.Relative.filename()))));
    };
    const size_t loadable = static_cast<size_t>(std::count_if(pack.begin(), pack.end(), loads));
    if (loadable == 0U) return Refuse("This does not look like a texture pack: it has no tex1_..._....png textures");

    // 4. Copy into a staging sibling of the destination and verify it.
    const fs::path parent = destination.parent_path();
    fs::create_directories(parent, error);
    if (error) return Refuse("Cannot create " + PathToUtf8(parent) + ": " + error.message());
    const auto suffix = [&] {
        return std::to_string(CurrentProcessId()) + "-" + std::to_string(gImportSerial.fetch_add(1U) + 1U);
    };
    StagingGuard staging;
    for (int attempt = 0; attempt < 64 && staging.Path.empty(); ++attempt) {
        const auto candidate = parent / (".import-" + suffix());
        if (fs::create_directory(candidate, error)) staging.Path = candidate;
        else if (error) return Refuse("Cannot create a staging folder in " + PathToUtf8(parent) + ": " + error.message());
    }
    if (staging.Path.empty()) return Refuse("Cannot create a staging folder in " + PathToUtf8(parent));
    uint64_t copiedBytes = 0;
    for (const auto& file : pack) {
        const auto target = staging.Path / file.Relative;
        const auto shown = GenericUtf8(file.Relative);
        fs::create_directories(target.parent_path(), error);
        if (error) return Refuse("Cannot copy " + shown + ": " + error.message() + "; the current pack was kept");
        fs::copy_file(file.Source, target, fs::copy_options::none, error);
        if (error) return Refuse("Cannot copy " + shown + ": " + error.message() + "; the current pack was kept");
        const auto copied = fs::file_size(target, error);
        if (error || copied != fs::file_size(file.Source, error)) {
            return Refuse("Cannot verify " + shown + " after copying; the current pack was kept");
        }
        copiedBytes += copied;
        if (copied > limits.MaximumFileBytes) {
            return Refuse(shown + " is larger than " + std::to_string(limits.MaximumFileBytes) + " bytes");
        }
        if (copiedBytes > limits.MaximumTotalBytes) {
            return Refuse("The pack is larger than " + std::to_string(limits.MaximumTotalBytes) + " bytes");
        }
        if (file.Kind == FileKind::Png) {
            if (auto problem = PngHeaderProblem(target, shown)) return Refuse(std::move(*problem));
        }
        if (file.Kind == FileKind::NotLoadedYet) ++result.NotLoadedYet;
    }
    result.Textures = loadable;

    // 5. Swap: current -> .previous-*, staging -> destination, drop previous.
    const bool hadPrevious = fs::symlink_status(destination, error).type() != fs::file_type::not_found;
    fs::path previous;
    if (hadPrevious) {
        previous = parent / (".previous-" + suffix());
        fs::rename(destination, previous, error);
        if (error) {
            return Refuse("Cannot replace " + PathToUtf8(destination) + ": " + error.message() +
                          "; the current pack was kept");
        }
    }
    fs::rename(staging.Path, destination, error);
    if (error) {
        const auto failure = error.message();
        if (hadPrevious) fs::rename(previous, destination, error);
        return Refuse("Cannot store " + PathToUtf8(destination) + ": " + failure + "; the current pack was kept");
    }
    staging.Path.clear();
    if (hadPrevious) fs::remove_all(previous, error);

    result.Imported = true;
    result.Destination = destination;
    result.CopiedBytes = copiedBytes;
    result.SourceTitleId = sourceTitle;
    result.Message = "Imported " + Plural(result.Textures, "texture", "textures") + " into " + PathToUtf8(destination);
    if (result.NotLoadedYet != 0U)
        result.Message += "; " + Plural(result.NotLoadedYet, "DDS/KTX file is", "DDS/KTX files are") + " not loaded yet";
    if (result.Skipped != 0U)
        result.Message += "; " + Plural(result.Skipped, "other file was", "other files were") + " skipped";
    if (sourceTitle && *sourceTitle != request.InstalledTitleId) {
        result.Message += ". The pack was made for " + Hex16(*sourceTitle) +
                          "; Azahar matches textures by content, so it is used for " +
                          Hex16(request.InstalledTitleId);
    }
    return result;
}

bool NativeFolderDialogAvailable(std::string* reason) {
#if defined(_WIN32)
    (void)reason;
    return true;
#elif defined(__ANDROID__)
    if (reason) *reason = "Folder dialogs are not available on Android; type the folder path instead";
    return false;
#elif defined(__linux__)
    (void)reason;
    return true;
#else
    if (reason) *reason = "Folder dialogs are not available on this platform; type the folder path instead";
    return false;
#endif
}

#if defined(_WIN32)
std::optional<std::string> PickFolderWithNativeDialog(const std::string& title, std::string* error) {
    if (error) error->clear();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        if (error) *error = "The folder dialog could not start";
        return std::nullopt;
    }
    std::optional<std::string> chosen;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        const auto wideTitle = PathFromUtf8(title).wstring();
        dialog->SetTitle(wideTitle.c_str());
        hr = dialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                    chosen = PathToUtf8(fs::path(path));
                    CoTaskMemFree(path);
                }
                item->Release();
            }
            if (!chosen && error) *error = "The chosen folder is not on the file system";
        } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED) && error) {
            *error = "The folder dialog failed";
        }
        dialog->Release();
    } else if (error) {
        *error = "The folder dialog could not start";
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return chosen;
}
#elif defined(__linux__) && !defined(__ANDROID__)
namespace {
// Runs a dialog command; nullopt with *missing when the tool is absent.
std::optional<std::string> RunDialogCommand(const std::string& command, bool* missing, bool* cancelled) {
    *missing = false;
    *cancelled = false;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        *missing = true;
        return std::nullopt;
    }
    std::string output;
    std::array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) output += buffer.data();
    const int status = pclose(pipe);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) == 127 || WEXITSTATUS(status) == 126) {
        *missing = true;
        return std::nullopt;
    }
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
    if (WEXITSTATUS(status) != 0 || output.empty()) {
        *cancelled = true;
        return std::nullopt;
    }
    return output;
}
} // namespace

std::optional<std::string> PickFolderWithNativeDialog(const std::string& title, std::string* error) {
    if (error) error->clear();
    std::string safeTitle;
    for (const char character : title)
        if (character != '\'' && character != '\\') safeTitle += character;
    bool missing = false, cancelled = false;
    auto chosen = RunDialogCommand("zenity --file-selection --directory --title='" + safeTitle + "' 2>/dev/null",
                                   &missing, &cancelled);
    if (chosen || cancelled) return chosen;
    chosen = RunDialogCommand("kdialog --getexistingdirectory \"$HOME\" --title '" + safeTitle + "' 2>/dev/null",
                              &missing, &cancelled);
    if (chosen || cancelled) return chosen;
    if (error) *error = "No folder dialog was found: install zenity or kdialog, or type the folder path";
    return std::nullopt;
}
#else
std::optional<std::string> PickFolderWithNativeDialog(const std::string&, std::string* error) {
    NativeFolderDialogAvailable(error);
    return std::nullopt;
}
#endif

} // namespace Fast::Extension
