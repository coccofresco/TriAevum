#include "oot3d/renderer/azahar_texture_pack.h"
#include "fast/renderer/content_hash.h"

#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace Oot3d::Renderer {
namespace {


bool IsPowerOfTwo(uint32_t value) noexcept {
    return value != 0U && (value & (value - 1U)) == 0U;
}

uint32_t Morton8(uint32_t x, uint32_t y) noexcept {
    return ((x & 0x1U) << 0U) | ((y & 0x1U) << 1U) | ((x & 0x2U) << 1U) | ((y & 0x2U) << 2U) | ((x & 0x4U) << 2U) |
           ((y & 0x4U) << 3U);
}

uint32_t AlignedTextureDimension(uint32_t value) noexcept {
    return std::max<uint32_t>(8U, (value + 7U) & ~7U);
}

void FlipRows(std::span<uint8_t> pixels, uint32_t width, uint32_t height, uint32_t bytesPerPixel) {
    const size_t rowBytes = static_cast<size_t>(width) * bytesPerPixel;
    if (pixels.size() != rowBytes * height) {
        throw std::runtime_error("texture row flip received an invalid byte count");
    }
    for (uint32_t row = 0; row < height / 2U; ++row) {
        auto first = pixels.begin() + static_cast<std::ptrdiff_t>(row * rowBytes);
        auto second = pixels.begin() + static_cast<std::ptrdiff_t>((height - row - 1U) * rowBytes);
        std::swap_ranges(first, first + rowBytes, second);
    }
}

std::vector<uint8_t> MakeLegacyHashBytes(const AzaharTextureRequest& request) {
    if (request.Width == 0U || request.Height == 0U || request.NativeFormat > 13U) {
        throw std::runtime_error("legacy Azahar hash received invalid texture metadata");
    }

    if (request.NativeFormat >= 5U) {
        const size_t expected = static_cast<size_t>(request.Width) * request.Height * 4U;
        if (request.NativeRgba8.size() != expected) {
            throw std::runtime_error("legacy Azahar hash requires decoded RGBA8 bytes");
        }
        std::vector<uint8_t> decoded(request.NativeRgba8.begin(), request.NativeRgba8.end());
        // Azahar's legacy path hashes its OpenGL-oriented detiled buffer.
        FlipRows(decoded, request.Width, request.Height, 4U);
        return decoded;
    }

    constexpr std::array<uint8_t, 5> bytesPerPixel{ 4U, 3U, 2U, 2U, 2U };
    const uint32_t pixelBytes = bytesPerPixel[request.NativeFormat];
    const uint32_t alignedWidth = AlignedTextureDimension(request.Width);
    std::vector<uint8_t> output(static_cast<size_t>(request.Width) * request.Height * pixelBytes);
    for (uint32_t y = 0; y < request.Height; ++y) {
        for (uint32_t x = 0; x < request.Width; ++x) {
            const uint32_t tile = (y / 8U) * (alignedWidth / 8U) + (x / 8U);
            const size_t source = static_cast<size_t>(tile * 64U + Morton8(x % 8U, y % 8U)) * pixelBytes;
            const size_t destination = (static_cast<size_t>(request.Height - y - 1U) * request.Width + x) * pixelBytes;
            if (source + pixelBytes > request.NativeBytes.size()) {
                throw std::runtime_error("legacy Azahar hash source is truncated");
            }
            std::copy_n(request.NativeBytes.begin() + static_cast<std::ptrdiff_t>(source), pixelBytes,
                        output.begin() + static_cast<std::ptrdiff_t>(destination));
        }
    }
    return output;
}

std::string Hex16(uint64_t value) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::filesystem::path ResolveDefaultUserDirectory() {
    if (const char* overridePath = std::getenv("OOT3D_AZAHAR_USER_DIRECTORY");
        overridePath != nullptr && *overridePath != '\0') {
        return std::filesystem::path(overridePath);
    }
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
        return std::filesystem::path(appData) / "Azahar";
    }
#else
    if (const char* xdgData = std::getenv("XDG_DATA_HOME"); xdgData != nullptr && *xdgData != '\0') {
        return std::filesystem::path(xdgData) / "azahar";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".local/share/azahar";
    }
#endif
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    return (error ? std::filesystem::path(".") : current) / "Azahar";
}

AzaharTexturePackConfiguration NormalizeConfiguration(AzaharTexturePackConfiguration configuration) {
    if (configuration.UserDirectory.empty()) {
        configuration.UserDirectory = ResolveDefaultUserDirectory();
    }
    configuration.UserDirectory = configuration.UserDirectory.lexically_normal();
    if (const char* loadDirectory = std::getenv("OOT3D_AZAHAR_LOAD_DIRECTORY");
        loadDirectory != nullptr && *loadDirectory != '\0') {
        configuration.LoadDirectory = std::filesystem::path(loadDirectory);
    }
    if (const char* dumpDirectory = std::getenv("OOT3D_AZAHAR_DUMP_DIRECTORY");
        dumpDirectory != nullptr && *dumpDirectory != '\0') {
        configuration.DumpDirectory = std::filesystem::path(dumpDirectory);
    }
    if (!configuration.LoadDirectory.empty()) {
        configuration.LoadDirectory = configuration.LoadDirectory.lexically_normal();
    }
    if (!configuration.DumpDirectory.empty()) {
        configuration.DumpDirectory = configuration.DumpDirectory.lexically_normal();
    }
    if (const char* titleId = std::getenv("OOT3D_AZAHAR_TITLE_ID"); titleId != nullptr && *titleId != '\0') {
        try {
            size_t parsed = 0;
            const uint64_t value = std::stoull(titleId, &parsed, 16);
            if (parsed == std::strlen(titleId)) {
                configuration.TitleId = value;
            }
        } catch (const std::exception&) {}
    }
    return configuration;
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("unable to open custom texture " + path.string());
    }
    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("custom texture has an invalid file size");
    }
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
        throw std::runtime_error("unable to read custom texture " + path.string());
    }
    return bytes;
}

void AppendBigEndian32(std::vector<uint8_t>& destination, uint32_t value) {
    destination.push_back(static_cast<uint8_t>(value >> 24U));
    destination.push_back(static_cast<uint8_t>(value >> 16U));
    destination.push_back(static_cast<uint8_t>(value >> 8U));
    destination.push_back(static_cast<uint8_t>(value));
}

void AppendPngChunk(std::vector<uint8_t>& png, const std::array<char, 4>& type, std::span<const uint8_t> payload) {
    if (payload.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("PNG chunk is too large");
    }
    AppendBigEndian32(png, static_cast<uint32_t>(payload.size()));
    const size_t crcOffset = png.size();
    png.insert(png.end(), type.begin(), type.end());
    png.insert(png.end(), payload.begin(), payload.end());
    uLong crc =
        crc32(0L, reinterpret_cast<const Bytef*>(png.data() + crcOffset), static_cast<uInt>(4U + payload.size()));
    AppendBigEndian32(png, static_cast<uint32_t>(crc));
}

void WritePng(const std::filesystem::path& destination, uint32_t width, uint32_t height,
              std::span<const uint8_t> rgba8) {
    const uint64_t rowBytes = static_cast<uint64_t>(width) * 4U;
    const uint64_t filteredSize = (rowBytes + 1U) * height;
    if (width == 0U || height == 0U || rowBytes * height != rgba8.size() ||
        filteredSize > std::numeric_limits<uLong>::max() || filteredSize > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("PNG encoder received invalid RGBA8 data");
    }

    std::vector<uint8_t> filtered(static_cast<size_t>(filteredSize));
    for (uint32_t row = 0; row < height; ++row) {
        const size_t destinationRow = static_cast<size_t>(row) * (rowBytes + 1U);
        filtered[destinationRow] = 0U;
        std::copy_n(rgba8.begin() + static_cast<std::ptrdiff_t>(row * rowBytes), static_cast<size_t>(rowBytes),
                    filtered.begin() + static_cast<std::ptrdiff_t>(destinationRow + 1U));
    }

    uLongf compressedSize = compressBound(static_cast<uLong>(filtered.size()));
    std::vector<uint8_t> compressed(compressedSize);
    const int result = compress2(compressed.data(), &compressedSize, filtered.data(),
                                 static_cast<uLong>(filtered.size()), Z_BEST_SPEED);
    if (result != Z_OK) {
        throw std::runtime_error("zlib failed while encoding PNG");
    }
    compressed.resize(compressedSize);

    std::vector<uint8_t> png{ 0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU };
    std::vector<uint8_t> header;
    header.reserve(13U);
    AppendBigEndian32(header, width);
    AppendBigEndian32(header, height);
    header.insert(header.end(), { 8U, 6U, 0U, 0U, 0U });
    AppendPngChunk(png, { 'I', 'H', 'D', 'R' }, header);
    AppendPngChunk(png, { 'I', 'D', 'A', 'T' }, compressed);
    AppendPngChunk(png, { 'I', 'E', 'N', 'D' }, std::span<const uint8_t>{});

    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        throw std::runtime_error("unable to create texture dump directory: " + error.message());
    }
    const auto temporary = std::filesystem::path(destination.string() + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output ||
            !output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()))) {
            std::filesystem::remove(temporary, error);
            throw std::runtime_error("unable to write texture dump " + destination.string());
        }
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            std::filesystem::remove(temporary, error);
            return;
        }
        std::filesystem::remove(temporary, error);
        throw std::runtime_error("unable to publish texture dump: " + error.message());
    }
}

struct PackState {
    bool ConfigurationFound = false;
    bool SkipMipmaps = false;
    bool FlipPngFiles = true;
    bool UseNewHash = true;
    std::unordered_map<std::string, std::vector<uint64_t>> MappedHashes;
};

PackState ReadPackState(const std::filesystem::path& packPath, std::string* error) {
    PackState state;
    std::error_code existsError;
    if (!std::filesystem::exists(packPath, existsError) || existsError) {
        // These are Azahar's exact no-pack legacy defaults.
        state.SkipMipmaps = true;
        state.FlipPngFiles = true;
        state.UseNewHash = false;
        return state;
    }
    try {
        std::ifstream input(packPath);
        if (!input) {
            throw std::runtime_error("unable to open pack.json");
        }
        const nlohmann::json document = nlohmann::json::parse(input);
        if (!document.is_object()) {
            throw std::runtime_error("pack.json root is not an object");
        }
        state.ConfigurationFound = true;
        if (const auto options = document.find("options"); options != document.end() && options->is_object()) {
            if (const auto value = options->find("skip_mipmap"); value != options->end() && value->is_boolean()) {
                state.SkipMipmaps = value->get<bool>();
            }
            if (const auto value = options->find("flip_png_files"); value != options->end() && value->is_boolean()) {
                state.FlipPngFiles = value->get<bool>();
            }
            if (const auto value = options->find("use_new_hash"); value != options->end() && value->is_boolean()) {
                state.UseNewHash = value->get<bool>();
            }
        }
        if (const auto textures = document.find("textures"); textures != document.end() && textures->is_object()) {
            for (const auto& item : textures->items()) {
                try {
                    size_t parsed = 0;
                    const uint64_t hash = std::stoull(item.key(), &parsed, 16);
                    if (parsed != item.key().size()) {
                        continue;
                    }
                    const auto addMapping = [&](const std::string& path) {
                        const std::string filename = Lowercase(std::filesystem::path(path).filename().string());
                        if (!filename.empty()) {
                            state.MappedHashes[filename].push_back(hash);
                        }
                    };
                    if (item.value().is_string()) {
                        addMapping(item.value().get<std::string>());
                    } else if (item.value().is_array()) {
                        for (const auto& path : item.value()) {
                            if (path.is_string()) {
                                addMapping(path.get<std::string>());
                            }
                        }
                    }
                } catch (const std::exception&) {}
            }
        }
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = "unable to parse " + packPath.string() + ": " + exception.what();
        }
        state = {};
        state.SkipMipmaps = true;
        state.FlipPngFiles = true;
        state.UseNewHash = false;
    }
    return state;
}

void EnsureDumpPack(const std::filesystem::path& dumpDirectory, bool useNewHash) {
    std::error_code error;
    std::filesystem::create_directories(dumpDirectory, error);
    if (error) {
        throw std::runtime_error("unable to create Azahar dump directory: " + error.message());
    }
    const auto packPath = dumpDirectory / "pack.json";
    if (std::filesystem::exists(packPath, error)) {
        return;
    }
    if (error) {
        throw std::runtime_error("unable to inspect Azahar pack.json: " + error.message());
    }
    const nlohmann::ordered_json document{
        { "author", "citra" },
        { "version", "1.0.0" },
        { "description", "A graphics pack" },
        { "options",
          {
              { "skip_mipmap", false },
              { "flip_png_files", true },
              { "use_new_hash", useNewHash },
          } },
    };
    std::ofstream output(packPath);
    if (!output) {
        throw std::runtime_error("unable to create Azahar pack.json");
    }
    output << document.dump(4);
    if (!output) {
        throw std::runtime_error("unable to write Azahar pack.json");
    }
}

struct IndexedTexture {
    std::filesystem::path Path;
    bool FlipPngFiles = true;
};

std::optional<uint64_t> ParseFilenameHash(const std::filesystem::path& path) {
    const std::string stem = path.stem().string();
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int format = 0;
    unsigned long long hash = 0;
    if (std::sscanf(stem.c_str(), "tex1_%ux%u_%llX_%u", &width, &height, &hash, &format) != 4) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(hash);
}

std::unordered_map<uint64_t, IndexedTexture> IndexTexturePack(const std::filesystem::path& loadDirectory,
                                                              const PackState& pack, size_t& unsupportedFiles,
                                                              std::string* error) {
    std::unordered_map<uint64_t, IndexedTexture> index;
    unsupportedFiles = 0U;
    std::error_code directoryError;
    if (!std::filesystem::exists(loadDirectory, directoryError) || directoryError) {
        if (directoryError && error != nullptr) {
            *error = "unable to inspect Azahar texture pack: " + directoryError.message();
        }
        return index;
    }
    std::vector<std::filesystem::path> files;
    std::error_code scanError;
    for (std::filesystem::recursive_directory_iterator
             iterator(loadDirectory, std::filesystem::directory_options::skip_permission_denied, scanError),
         end;
         iterator != end && !scanError; iterator.increment(scanError)) {
        if (iterator->is_regular_file()) {
            files.push_back(iterator->path());
        }
    }
    if (scanError && error != nullptr) {
        *error = "unable to scan Azahar texture pack: " + scanError.message();
    }
    std::sort(files.begin(), files.end());
    for (const auto& path : files) {
        const std::string extension = Lowercase(path.extension().string());
        if (extension != ".png" && extension != ".dds" && extension != ".ktx") {
            continue;
        }
        const std::filesystem::path materialStem = path.stem();
        if (Lowercase(materialStem.extension().string()) == ".norm") {
            continue;
        }

        std::vector<uint64_t> hashes;
        const std::string filename = Lowercase(path.filename().string());
        if (const auto mapped = pack.MappedHashes.find(filename); mapped != pack.MappedHashes.end()) {
            hashes = mapped->second;
        }
        if (const auto parsed = ParseFilenameHash(path);
            parsed.has_value() && std::find(hashes.begin(), hashes.end(), *parsed) == hashes.end()) {
            hashes.push_back(*parsed);
        }
        if (hashes.empty()) {
            continue;
        }
        if (extension != ".png") {
            ++unsupportedFiles;
            continue;
        }
        for (const uint64_t hash : hashes) {
            index.try_emplace(hash, IndexedTexture{ path, pack.FlipPngFiles });
        }
    }
    return index;
}

std::shared_ptr<const AzaharTextureReplacement> DecodeReplacement(const IndexedTexture& indexed, uint64_t hash) {
    const auto input = ReadBinaryFile(indexed.Path);
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* decoded =
        stbi_load_from_memory(input.data(), static_cast<int>(input.size()), &width, &height, &channels, 4);
    if (decoded == nullptr || width <= 0 || height <= 0) {
        const std::string reason =
            stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown PNG decode failure";
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        throw std::runtime_error("unable to decode custom texture " + indexed.Path.string() + ": " + reason);
    }
    const uint64_t byteCount = static_cast<uint64_t>(width) * height * 4U;
    if (byteCount > std::numeric_limits<size_t>::max()) {
        stbi_image_free(decoded);
        throw std::runtime_error("custom texture dimensions are too large");
    }
    auto pixels = std::make_shared<std::vector<uint8_t>>(decoded, decoded + static_cast<size_t>(byteCount));
    stbi_image_free(decoded);

    // Azahar flips PNGs into its bottom-left OpenGL staging layout. The
    // native NRI/Vulkan decoder already matches the on-disk dump layout,
    // so only packs that explicitly disable Azahar's flip need conversion.
    if (!indexed.FlipPngFiles) {
        FlipRows(*pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), 4U);
    }

    auto replacement = std::make_shared<AzaharTextureReplacement>();
    replacement->Width = static_cast<uint32_t>(width);
    replacement->Height = static_cast<uint32_t>(height);
    replacement->NativeHash = hash;
    replacement->Rgba8 = std::move(pixels);
    replacement->SourcePath = indexed.Path;
    return replacement;
}

struct DumpJob {
    uint64_t Hash = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    std::filesystem::path Destination;
    std::vector<uint8_t> Rgba8;
};

struct LoadJob {
    uint64_t Hash = 0;
    uint64_t Generation = 0;
    IndexedTexture Texture;
};

} // namespace

uint64_t AzaharCityHash64(std::span<const uint8_t> bytes) noexcept {
    return ::Fast::Renderer::ContentHash64(bytes);
}

uint64_t ComputeAzaharTextureHash(const AzaharTextureRequest& request, bool useNewHash, std::string* error) {
    if (error != nullptr) {
        error->clear();
    }
    try {
        if (request.NativeBytes.empty()) {
            throw std::runtime_error("Azahar texture hash requires native PICA bytes");
        }
        if (useNewHash) {
            return AzaharCityHash64(request.NativeBytes);
        }
        const auto legacy = MakeLegacyHashBytes(request);
        return AzaharCityHash64(legacy);
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return 0U;
    }
}

std::string MakeAzaharTextureFilename(uint16_t width, uint16_t height, uint64_t hash, uint8_t nativeFormat,
                                      uint32_t mipLevel) {
    return "tex1_" + std::to_string(width) + "x" + std::to_string(height) + "_" + Hex16(hash) + "_" +
           std::to_string(nativeFormat) + "_mip" + std::to_string(mipLevel) + ".png";
}

class AzaharTexturePackRuntime::Impl {
  public:
    Impl() {
        // Constructor-body startup guarantees every queue and stop flag exists
        // before WorkerMain can inspect them.
        mWorker = std::thread([this] { WorkerMain(); });
    }

    ~Impl() {
        {
            std::scoped_lock lock(mMutex);
            mStopping = true;
        }
        mWorkAvailable.notify_all();
        if (mWorker.joinable()) {
            mWorker.join();
        }
    }

    void Configure(AzaharTexturePackConfiguration configuration) {
        configuration = NormalizeConfiguration(std::move(configuration));
        std::scoped_lock lock(mMutex);
        if (mConfigured && configuration == mConfiguration) {
            return;
        }
        mConfiguration = std::move(configuration);
        if (mConfigured) {
            ++mGeneration;
        } else {
            mGeneration = 1U;
            mConfigured = true;
        }
        try {
            RebuildLocked();
        } catch (const std::exception& exception) {
            mIndex.clear();
            mReplacementCache.clear();
            mLastError = exception.what();
        }
    }

    void Reload() {
        std::scoped_lock lock(mMutex);
        if (!mConfigured) {
            return;
        }
        ++mGeneration;
        try {
            RebuildLocked();
        } catch (const std::exception& exception) {
            mIndex.clear();
            mReplacementCache.clear();
            mLastError = exception.what();
        }
    }

    std::shared_ptr<const AzaharTextureReplacement> ResolveAndMaybeDump(const AzaharTextureRequest& request) {
        bool dumpEnabled = false;
        bool loadEnabled = false;
        bool dumpUsesNewHash = true;
        bool loadUsesNewHash = true;
        uint64_t generation = 0;
        std::filesystem::path dumpDirectory;
        {
            std::scoped_lock lock(mMutex);
            if (!mConfigured) {
                return nullptr;
            }
            dumpEnabled = mConfiguration.DumpTextures;
            loadEnabled = mConfiguration.LoadCustomTextures;
            dumpUsesNewHash = mDumpUsesNewHash;
            loadUsesNewHash = mPack.UseNewHash;
            generation = mGeneration;
            dumpDirectory = mDumpDirectory;
        }
        if (!dumpEnabled && !loadEnabled) {
            return nullptr;
        }

        std::string hashError;
        std::optional<uint64_t> modernHash;
        std::optional<uint64_t> legacyHash;
        const auto resolveHash = [&](bool useNewHash) {
            std::optional<uint64_t>& cached = useNewHash ? modernHash : legacyHash;
            if (!cached.has_value()) {
                const uint64_t computed = ComputeAzaharTextureHash(request, useNewHash, &hashError);
                if (!hashError.empty()) {
                    return std::optional<uint64_t>{};
                }
                cached = computed;
            }
            return cached;
        };

        if (dumpEnabled) {
            const auto dumpHash = resolveHash(dumpUsesNewHash);
            if (dumpHash.has_value()) {
                try {
                    QueueDump(request, *dumpHash, generation, dumpDirectory);
                } catch (const std::exception& exception) { SetError(exception.what()); }
            }
        }
        if (!loadEnabled) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return nullptr;
        }

        const auto loadHash = resolveHash(loadUsesNewHash);
        if (!loadHash.has_value()) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return nullptr;
        }

        IndexedTexture indexed;
        {
            std::scoped_lock lock(mMutex);
            if (generation != mGeneration) {
                return nullptr;
            }
            if (const auto cached = mReplacementCache.find(*loadHash); cached != mReplacementCache.end()) {
                return cached->second;
            }
            const auto found = mIndex.find(*loadHash);
            if (found == mIndex.end()) {
                return nullptr;
            }
            indexed = found->second;
        }

        try {
            const auto replacement = DecodeReplacement(indexed, *loadHash);
            std::scoped_lock lock(mMutex);
            if (generation != mGeneration || !mConfiguration.LoadCustomTextures) {
                return nullptr;
            }
            const auto [entry, inserted] = mReplacementCache.try_emplace(*loadHash, replacement);
            return entry->second;
        } catch (const std::exception& exception) {
            SetError(exception.what());
            return nullptr;
        }
    }

    AzaharTextureResolveResult ResolveOrQueue(
        const AzaharTextureRequest& request) {
        bool dumpEnabled = false;
        bool loadEnabled = false;
        bool dumpUsesNewHash = true;
        bool loadUsesNewHash = true;
        uint64_t generation = 0;
        std::filesystem::path dumpDirectory;
        {
            std::scoped_lock lock(mMutex);
            if (!mConfigured) {
                return {};
            }
            dumpEnabled = mConfiguration.DumpTextures;
            loadEnabled = mConfiguration.LoadCustomTextures;
            dumpUsesNewHash = mDumpUsesNewHash;
            loadUsesNewHash = mPack.UseNewHash;
            generation = mGeneration;
            dumpDirectory = mDumpDirectory;
        }
        if (!dumpEnabled && !loadEnabled) {
            return {};
        }

        std::string hashError;
        std::optional<uint64_t> modernHash;
        std::optional<uint64_t> legacyHash;
        const auto resolveHash = [&](bool useNewHash) {
            std::optional<uint64_t>& cached =
                useNewHash ? modernHash : legacyHash;
            if (!cached.has_value()) {
                const uint64_t computed =
                    ComputeAzaharTextureHash(
                        request, useNewHash, &hashError);
                if (!hashError.empty()) {
                    return std::optional<uint64_t>{};
                }
                cached = computed;
            }
            return cached;
        };

        if (dumpEnabled) {
            const auto dumpHash = resolveHash(dumpUsesNewHash);
            if (dumpHash.has_value()) {
                try {
                    QueueDump(
                        request, *dumpHash, generation,
                        dumpDirectory);
                } catch (const std::exception& exception) {
                    SetError(exception.what());
                }
            }
        }
        if (!loadEnabled) {
            if (!hashError.empty()) {
                SetError(hashError);
                return {
                    AzaharTextureResolveState::Failed, 0U, nullptr};
            }
            return {};
        }

        const auto loadHash = resolveHash(loadUsesNewHash);
        if (!loadHash.has_value()) {
            if (!hashError.empty()) {
                SetError(hashError);
            }
            return {
                AzaharTextureResolveState::Failed, 0U, nullptr};
        }

        std::scoped_lock lock(mMutex);
        if (generation != mGeneration ||
            !mConfiguration.LoadCustomTextures) {
            return {
                AzaharTextureResolveState::Disabled,
                *loadHash, nullptr};
        }
        if (const auto cached = mReplacementCache.find(*loadHash);
            cached != mReplacementCache.end()) {
            return {
                AzaharTextureResolveState::Ready,
                *loadHash, cached->second};
        }
        const auto indexed = mIndex.find(*loadHash);
        if (indexed == mIndex.end()) {
            return {
                AzaharTextureResolveState::Missing,
                *loadHash, nullptr};
        }
        if (mFailedLoadHashes.contains(*loadHash)) {
            return {
                AzaharTextureResolveState::Failed,
                *loadHash, nullptr};
        }
        if (const auto pending =
                mPendingLoadGenerations.find(*loadHash);
            pending != mPendingLoadGenerations.end() &&
            pending->second == generation) {
            return {
                AzaharTextureResolveState::Pending,
                *loadHash, nullptr};
        }

        mPendingLoadGenerations.insert_or_assign(
            *loadHash, generation);
        mLoadJobs.push_back({
            *loadHash, generation, indexed->second});
        ++mPendingLoads;
        mWorkAvailable.notify_one();
        return {
            AzaharTextureResolveState::Pending,
            *loadHash, nullptr};
    }

    AzaharTextureResolveResult PollQueued(
        uint64_t nativeHash) const {
        std::scoped_lock lock(mMutex);
        if (!mConfigured ||
            !mConfiguration.LoadCustomTextures) {
            return {
                AzaharTextureResolveState::Disabled,
                nativeHash, nullptr};
        }
        if (const auto cached =
                mReplacementCache.find(nativeHash);
            cached != mReplacementCache.end()) {
            return {
                AzaharTextureResolveState::Ready,
                nativeHash, cached->second};
        }
        if (mFailedLoadHashes.contains(nativeHash)) {
            return {
                AzaharTextureResolveState::Failed,
                nativeHash, nullptr};
        }
        if (const auto pending =
                mPendingLoadGenerations.find(nativeHash);
            pending != mPendingLoadGenerations.end() &&
            pending->second == mGeneration) {
            return {
                AzaharTextureResolveState::Pending,
                nativeHash, nullptr};
        }
        return {
            mIndex.contains(nativeHash)
                ? AzaharTextureResolveState::Failed
                : AzaharTextureResolveState::Missing,
            nativeHash, nullptr};
    }

    AzaharTexturePackStatus Snapshot() const {
        std::scoped_lock lock(mMutex);
        AzaharTexturePackStatus status;
        status.DumpTextures = mConfiguration.DumpTextures;
        status.LoadCustomTextures = mConfiguration.LoadCustomTextures;
        status.PackConfigurationFound = mPack.ConfigurationFound;
        status.PackUsesNewHash = mPack.UseNewHash;
        status.Generation = mGeneration;
        status.IndexedTextures = mIndex.size();
        status.LoadedTextures = mReplacementCache.size();
        status.DumpedTextures = mDumpedCount;
        status.PendingDumps = mPendingDumps;
        status.PendingLoads = mPendingLoads;
        status.FailedLoads = mFailedLoadHashes.size();
        status.UnsupportedTextureFiles = mUnsupportedFiles;
        status.LoadDirectory = mLoadDirectory;
        status.DumpDirectory = mDumpDirectory;
        status.LastError = mLastError;
        return status;
    }

    uint64_t Generation() const {
        std::scoped_lock lock(mMutex);
        return mGeneration;
    }

    void WaitForPendingDumps() {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [&] { return mPendingDumps == 0U; });
    }

    void WaitForPendingLoads() {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [&] { return mPendingLoads == 0U; });
    }

  private:
    void RebuildLocked() {
        mIndex.clear();
        mReplacementCache.clear();
        mFailedLoadHashes.clear();
        mPendingLoadGenerations.clear();
        mDumpedHashes.clear();
        mLastError.clear();
        mUnsupportedFiles = 0U;
        const std::string title = Hex16(mConfiguration.TitleId);
        mLoadDirectory = mConfiguration.LoadDirectory.empty()
                             ? mConfiguration.UserDirectory / "load" / "textures" / title
                             : mConfiguration.LoadDirectory;
        mDumpDirectory = mConfiguration.DumpDirectory.empty()
                             ? mConfiguration.UserDirectory / "dump" / "textures" / title
                             : mConfiguration.DumpDirectory;
        mPack = ReadPackState(mLoadDirectory / "pack.json", &mLastError);
        std::string dumpPackError;
        const auto dumpPack = ReadPackState(mDumpDirectory / "pack.json", &dumpPackError);
        mDumpUsesNewHash = dumpPack.ConfigurationFound ? dumpPack.UseNewHash : true;
        if (mLastError.empty() && !dumpPackError.empty()) {
            mLastError = std::move(dumpPackError);
        }

        if (mConfiguration.LoadCustomTextures) {
            mIndex = IndexTexturePack(mLoadDirectory, mPack, mUnsupportedFiles, &mLastError);
        }
        if (mConfiguration.DumpTextures) {
            try {
                EnsureDumpPack(mDumpDirectory, mDumpUsesNewHash);
            } catch (const std::exception& exception) { mLastError = exception.what(); }
        }
    }

    void QueueDump(const AzaharTextureRequest& request, uint64_t hash, uint64_t generation,
                   const std::filesystem::path& dumpDirectory) {
        if (!IsPowerOfTwo(request.Width) || !IsPowerOfTwo(request.Height)) {
            return;
        }
        const size_t expected = static_cast<size_t>(request.Width) * request.Height * 4U;
        if (request.NativeRgba8.size() != expected) {
            SetError("Azahar texture dump requires decoded RGBA8 bytes");
            return;
        }
        const auto destination = dumpDirectory / MakeAzaharTextureFilename(request.Width, request.Height, hash,
                                                                           request.NativeFormat, request.MipLevel);

        std::scoped_lock lock(mMutex);
        if (generation != mGeneration || !mConfiguration.DumpTextures || mDumpedHashes.contains(hash)) {
            return;
        }
        mDumpedHashes.insert(hash);
        std::error_code existsError;
        if (std::filesystem::exists(destination, existsError)) {
            return;
        }
        if (existsError) {
            mDumpedHashes.erase(hash);
            mLastError = "unable to inspect texture dump path: " + existsError.message();
            return;
        }
        DumpJob job;
        job.Hash = hash;
        job.Width = request.Width;
        job.Height = request.Height;
        job.Destination = destination;
        job.Rgba8.assign(request.NativeRgba8.begin(), request.NativeRgba8.end());
        mDumpJobs.push_back(std::move(job));
        ++mPendingDumps;
        mWorkAvailable.notify_one();
    }

    void SetError(std::string error) {
        std::scoped_lock lock(mMutex);
        mLastError = std::move(error);
    }

    void WorkerMain() {
        for (;;) {
            LoadJob loadJob;
            DumpJob job;
            bool load = false;
            {
                std::unique_lock lock(mMutex);
                mWorkAvailable.wait(lock, [&] {
                    return mStopping || !mLoadJobs.empty() ||
                           !mDumpJobs.empty();
                });
                if (mLoadJobs.empty() && mDumpJobs.empty()) {
                    if (mStopping) {
                        return;
                    }
                    continue;
                }
                if (!mLoadJobs.empty()) {
                    load = true;
                    loadJob = std::move(mLoadJobs.front());
                    mLoadJobs.pop_front();
                } else {
                    job = std::move(mDumpJobs.front());
                    mDumpJobs.pop_front();
                }
            }

            if (load) {
                std::shared_ptr<const AzaharTextureReplacement>
                    replacement;
                std::string failure;
                try {
                    replacement = DecodeReplacement(
                        loadJob.Texture, loadJob.Hash);
                } catch (const std::exception& exception) {
                    failure = exception.what();
                }
                {
                    std::scoped_lock lock(mMutex);
                    const auto pending =
                        mPendingLoadGenerations.find(loadJob.Hash);
                    const bool currentJob =
                        pending != mPendingLoadGenerations.end() &&
                        pending->second == loadJob.Generation;
                    if (currentJob) {
                        if (replacement != nullptr &&
                            loadJob.Generation == mGeneration &&
                            mConfiguration.LoadCustomTextures) {
                            mReplacementCache.insert_or_assign(
                                loadJob.Hash,
                                std::move(replacement));
                            mFailedLoadHashes.erase(loadJob.Hash);
                        } else if (!failure.empty() &&
                                   loadJob.Generation ==
                                       mGeneration) {
                            mFailedLoadHashes.insert(loadJob.Hash);
                            mLastError = std::move(failure);
                        }
                        mPendingLoadGenerations.erase(pending);
                    }
                    if (mPendingLoads > 0U) {
                        --mPendingLoads;
                    }
                    if (mPendingLoads == 0U) {
                        mIdle.notify_all();
                    }
                }
                continue;
            }

            bool success = false;
            std::string failure;
            try {
                WritePng(job.Destination, job.Width, job.Height, job.Rgba8);
                success = true;
            } catch (const std::exception& exception) { failure = exception.what(); }

            {
                std::scoped_lock lock(mMutex);
                if (success) {
                    ++mDumpedCount;
                } else {
                    mDumpedHashes.erase(job.Hash);
                    mLastError = std::move(failure);
                }
                if (mPendingDumps > 0U) {
                    --mPendingDumps;
                }
                if (mPendingDumps == 0U) {
                    mIdle.notify_all();
                }
            }
        }
    }

    mutable std::mutex mMutex;
    std::condition_variable mWorkAvailable;
    std::condition_variable mIdle;
    std::thread mWorker;
    bool mStopping = false;
    bool mConfigured = false;
    AzaharTexturePackConfiguration mConfiguration;
    uint64_t mGeneration = 1U;
    std::filesystem::path mLoadDirectory;
    std::filesystem::path mDumpDirectory;
    PackState mPack;
    bool mDumpUsesNewHash = true;
    std::unordered_map<uint64_t, IndexedTexture> mIndex;
    std::unordered_map<uint64_t, std::shared_ptr<const AzaharTextureReplacement>> mReplacementCache;
    std::unordered_map<uint64_t, uint64_t> mPendingLoadGenerations;
    std::unordered_set<uint64_t> mFailedLoadHashes;
    std::unordered_set<uint64_t> mDumpedHashes;
    std::deque<LoadJob> mLoadJobs;
    std::deque<DumpJob> mDumpJobs;
    size_t mPendingLoads = 0U;
    size_t mPendingDumps = 0U;
    size_t mDumpedCount = 0U;
    size_t mUnsupportedFiles = 0U;
    std::string mLastError;
};

AzaharTexturePackRuntime::AzaharTexturePackRuntime() : mImpl(std::make_unique<Impl>()) {
}

AzaharTexturePackRuntime::~AzaharTexturePackRuntime() = default;

AzaharTexturePackRuntime& AzaharTexturePackRuntime::Instance() {
    static AzaharTexturePackRuntime runtime;
    return runtime;
}

void AzaharTexturePackRuntime::Configure(AzaharTexturePackConfiguration configuration) {
    mImpl->Configure(std::move(configuration));
}

void AzaharTexturePackRuntime::Reload() {
    mImpl->Reload();
}

std::shared_ptr<const AzaharTextureReplacement>
AzaharTexturePackRuntime::ResolveAndMaybeDump(const AzaharTextureRequest& request) {
    return mImpl->ResolveAndMaybeDump(request);
}

AzaharTextureResolveResult
AzaharTexturePackRuntime::ResolveOrQueue(
    const AzaharTextureRequest& request) {
    return mImpl->ResolveOrQueue(request);
}

AzaharTextureResolveResult
AzaharTexturePackRuntime::PollQueued(
    uint64_t nativeHash) const {
    return mImpl->PollQueued(nativeHash);
}

AzaharTexturePackStatus AzaharTexturePackRuntime::Snapshot() const {
    return mImpl->Snapshot();
}

uint64_t AzaharTexturePackRuntime::Generation() const {
    return mImpl->Generation();
}

void AzaharTexturePackRuntime::WaitForPendingDumps() {
    mImpl->WaitForPendingDumps();
}

void AzaharTexturePackRuntime::WaitForPendingLoads() {
    mImpl->WaitForPendingLoads();
}

} // namespace Oot3d::Renderer
