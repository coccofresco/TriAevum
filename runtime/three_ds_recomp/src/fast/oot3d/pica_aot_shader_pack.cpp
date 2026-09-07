#include "fast/oot3d/pica_aot_shader_pack.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>

#include <nlohmann/json.hpp>

namespace Fast::Oot3d {
namespace {

constexpr std::array<uint8_t, 8> kMagic{
    'O', '3', 'P', 'S', 'A', 'O', 'T', 0};
constexpr uint32_t kSpirvMagic = 0x07230203U;
constexpr size_t kHeaderSize = 24U;
constexpr size_t kEntrySize = 56U;
constexpr uint64_t kMaximumPackBytes = 512ULL * 1024ULL * 1024ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

uint64_t HashBytes(std::span<const uint8_t> bytes, uint64_t seed) noexcept {
    uint64_t hash = seed;
    for (const uint8_t byte : bytes)
        hash = (hash ^ byte) * kFnvPrime;
    return hash == 0U ? 1U : hash;
}

void SetError(std::string* error, const std::string& message) {
    if (error != nullptr)
        *error = message;
}

void AppendU32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (uint32_t shift = 0; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<uint8_t>(value >> shift));
}

void AppendU64(std::vector<uint8_t>& bytes, uint64_t value) {
    for (uint32_t shift = 0; shift < 64U; shift += 8U)
        bytes.push_back(static_cast<uint8_t>(value >> shift));
}

bool ReadU32(std::span<const uint8_t> bytes, size_t offset,
             uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4U)
        return false;
    value = 0;
    for (uint32_t index = 0; index < 4U; ++index)
        value |= static_cast<uint32_t>(bytes[offset + index]) <<
                 (index * 8U);
    return true;
}

bool ReadU64(std::span<const uint8_t> bytes, size_t offset,
             uint64_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 8U)
        return false;
    value = 0;
    for (uint32_t index = 0; index < 8U; ++index)
        value |= static_cast<uint64_t>(bytes[offset + index]) <<
                 (index * 8U);
    return true;
}

nlohmann::json HexSet(const std::set<uint64_t>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const uint64_t value : values)
        result.push_back(FormatPicaAotShaderId(value));
    return result;
}

nlohmann::json IntegerSet(const std::set<uint64_t>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const uint64_t value : values)
        result.push_back(value);
    return result;
}

} // namespace

PicaAotShaderSourceIdentity IdentifyPicaAotShaderSource(
    std::string_view source) noexcept {
    const auto identity =
        ::Oot3d::Renderer::IdentifyPicaShaderSource(source);
    return {
        identity.Id,
        identity.SecondaryHash,
        identity.Size,
    };
}

std::string FormatPicaAotShaderId(uint64_t id) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(16) << std::setfill('0')
           << id;
    return stream.str();
}

std::string_view PicaAotShaderStageName(
    PicaAotShaderStage stage) noexcept {
    switch (stage) {
    case PicaAotShaderStage::Vertex:
        return "vertex";
    case PicaAotShaderStage::Fragment:
        return "fragment";
    case PicaAotShaderStage::NriFragment:
        return "nri_fragment";
    }
    return "unknown";
}

bool ParsePicaAotShaderStage(std::string_view name,
                             PicaAotShaderStage& stage) noexcept {
    if (name == "vertex") {
        stage = PicaAotShaderStage::Vertex;
        return true;
    }
    if (name == "fragment") {
        stage = PicaAotShaderStage::Fragment;
        return true;
    }
    if (name == "nri_fragment") {
        stage = PicaAotShaderStage::NriFragment;
        return true;
    }
    return false;
}

bool WritePicaAotShaderPack(
    const std::filesystem::path& path,
    uint32_t descriptorSchemaVersion,
    std::span<const PicaAotShaderBinary> shaders,
    std::string* error) {
    if (path.empty() || descriptorSchemaVersion == 0U || shaders.empty()) {
        SetError(error, "PICA AOT shader pack input is incomplete");
        return false;
    }
    if (shaders.size() > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "PICA AOT shader pack has too many entries");
        return false;
    }

    std::vector<PicaAotShaderBinary> ordered(shaders.begin(),
                                             shaders.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left,
                                                 const auto& right) {
        return std::tie(left.Stage, left.Source) <
               std::tie(right.Stage, right.Source);
    });
    std::vector<uint8_t> bytes;
    bytes.reserve(kHeaderSize + ordered.size() * kEntrySize);
    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    AppendU32(bytes, kPicaAotShaderPackVersion);
    AppendU32(bytes, descriptorSchemaVersion);
    AppendU32(bytes, static_cast<uint32_t>(ordered.size()));
    AppendU32(bytes, 0U);
    const size_t tableOffset = bytes.size();
    bytes.resize(bytes.size() + ordered.size() * kEntrySize, 0U);

    std::set<std::pair<PicaAotShaderStage,
                       PicaAotShaderSourceIdentity>> identities;
    for (size_t index = 0; index < ordered.size(); ++index) {
        const auto& shader = ordered[index];
        if (shader.Stage < PicaAotShaderStage::Vertex ||
            shader.Stage > PicaAotShaderStage::NriFragment ||
            shader.Source.Id == 0U || shader.Source.SecondaryHash == 0U ||
            shader.Source.Size == 0U || shader.Spirv.empty() ||
            shader.Spirv.size() >
                std::numeric_limits<uint32_t>::max() ||
            shader.Spirv.front() != kSpirvMagic ||
            !identities.insert({shader.Stage, shader.Source}).second) {
            SetError(error,
                     "PICA AOT shader pack contains an invalid entry");
            return false;
        }
        const uint64_t byteOffset = bytes.size();
        const auto spirvBytes = std::as_bytes(
            std::span(shader.Spirv.data(), shader.Spirv.size()));
        const uint64_t binaryHash = HashBytes(
            {reinterpret_cast<const uint8_t*>(spirvBytes.data()),
             spirvBytes.size()},
            1469598103934665603ULL);
        bytes.insert(bytes.end(),
                     reinterpret_cast<const uint8_t*>(spirvBytes.data()),
                     reinterpret_cast<const uint8_t*>(spirvBytes.data()) +
                         spirvBytes.size());
        if (bytes.size() > kMaximumPackBytes) {
            SetError(error, "PICA AOT shader pack exceeds its size limit");
            return false;
        }

        std::vector<uint8_t> entry;
        entry.reserve(kEntrySize);
        AppendU32(entry, static_cast<uint32_t>(shader.Stage));
        AppendU32(entry, 0U);
        AppendU64(entry, shader.Source.Id);
        AppendU64(entry, shader.Source.SecondaryHash);
        AppendU64(entry, shader.Source.Size);
        AppendU32(entry,
                  static_cast<uint32_t>(shader.Spirv.size()));
        AppendU32(entry, 0U);
        AppendU64(entry, byteOffset);
        AppendU64(entry, binaryHash);
        std::copy(entry.begin(), entry.end(),
                  bytes.begin() + tableOffset + index * kEntrySize);
    }

    std::error_code filesystemError;
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path(),
                                            filesystemError);
    if (filesystemError) {
        SetError(error, "could not create PICA AOT pack directory: " +
                            filesystemError.message());
        return false;
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            SetError(error, "could not write PICA AOT shader pack");
            return false;
        }
    }
    std::filesystem::remove(path, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        SetError(error, "could not publish PICA AOT shader pack: " +
                            filesystemError.message());
        return false;
    }
    return true;
}

bool PicaAotShaderPack::Load(const std::filesystem::path& path,
                             std::string* error) {
    Clear();
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        SetError(error, "could not open PICA AOT shader pack: " +
                            path.string());
        return false;
    }
    const std::streamoff length = input.tellg();
    if (length < static_cast<std::streamoff>(kHeaderSize) ||
        static_cast<uint64_t>(length) > kMaximumPackBytes) {
        SetError(error, "PICA AOT shader pack size is invalid");
        return false;
    }
    input.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        SetError(error, "could not read PICA AOT shader pack");
        return false;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        SetError(error, "PICA AOT shader pack magic is invalid");
        return false;
    }
    uint32_t version = 0;
    uint32_t schema = 0;
    uint32_t entryCount = 0;
    if (!ReadU32(bytes, 8U, version) || !ReadU32(bytes, 12U, schema) ||
        !ReadU32(bytes, 16U, entryCount) ||
        version != kPicaAotShaderPackVersion || schema == 0U ||
        entryCount == 0U ||
        entryCount > (bytes.size() - kHeaderSize) / kEntrySize) {
        SetError(error, "PICA AOT shader pack header is invalid");
        return false;
    }

    if (bytes.size() % sizeof(uint32_t) != 0U) {
        SetError(error, "PICA AOT shader pack is not word aligned");
        return false;
    }
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    for (size_t index = 0; index < words.size(); ++index) {
        if (!ReadU32(bytes, index * sizeof(uint32_t), words[index])) {
            SetError(error, "PICA AOT shader pack word decode failed");
            return false;
        }
    }
    std::map<Key, Range> entries;
    const uint64_t payloadOffset =
        kHeaderSize + static_cast<uint64_t>(entryCount) * kEntrySize;
    for (uint32_t index = 0; index < entryCount; ++index) {
        const size_t base = kHeaderSize + index * kEntrySize;
        uint32_t stageValue = 0;
        uint64_t id = 0;
        uint64_t secondary = 0;
        uint64_t sourceSize = 0;
        uint32_t wordCount = 0;
        uint64_t byteOffset = 0;
        uint64_t expectedHash = 0;
        if (!ReadU32(bytes, base, stageValue) ||
            !ReadU64(bytes, base + 8U, id) ||
            !ReadU64(bytes, base + 16U, secondary) ||
            !ReadU64(bytes, base + 24U, sourceSize) ||
            !ReadU32(bytes, base + 32U, wordCount) ||
            !ReadU64(bytes, base + 40U, byteOffset) ||
            !ReadU64(bytes, base + 48U, expectedHash) ||
            stageValue < static_cast<uint32_t>(
                             PicaAotShaderStage::Vertex) ||
            stageValue > static_cast<uint32_t>(
                             PicaAotShaderStage::NriFragment) ||
            id == 0U || secondary == 0U || sourceSize == 0U ||
            wordCount == 0U || byteOffset % 4U != 0U ||
            byteOffset < payloadOffset ||
            byteOffset > bytes.size() ||
            static_cast<uint64_t>(wordCount) * 4U >
                bytes.size() - byteOffset) {
            SetError(error, "PICA AOT shader pack entry is invalid");
            return false;
        }
        const auto binary = std::span(
            bytes.data() + static_cast<size_t>(byteOffset),
            static_cast<size_t>(wordCount) * 4U);
        if (HashBytes(binary, 1469598103934665603ULL) != expectedHash ||
            words[static_cast<size_t>(byteOffset) / 4U] != kSpirvMagic) {
            SetError(error, "PICA AOT shader binary checksum is invalid");
            return false;
        }
        const Key key{
            static_cast<PicaAotShaderStage>(stageValue),
            {id, secondary, sourceSize},
        };
        if (!entries.emplace(
                 key, Range{static_cast<size_t>(byteOffset) / 4U,
                            wordCount})
                 .second) {
            SetError(error, "PICA AOT shader pack has duplicate entries");
            return false;
        }
    }
    mPath = path;
    mDescriptorSchemaVersion = schema;
    mWords = std::move(words);
    mEntries = std::move(entries);
    return true;
}

void PicaAotShaderPack::Clear() {
    mPath.clear();
    mDescriptorSchemaVersion = 0;
    mWords.clear();
    mEntries.clear();
}

std::span<const uint32_t> PicaAotShaderPack::Find(
    PicaAotShaderStage stage, std::string_view source) const noexcept {
    return Find(stage, IdentifyPicaAotShaderSource(source));
}

std::span<const uint32_t> PicaAotShaderPack::Find(
    PicaAotShaderStage stage,
    const PicaAotShaderSourceIdentity& source) const noexcept {
    const auto found = mEntries.find({stage, source});
    if (found == mEntries.end() ||
        found->second.WordOffset > mWords.size() ||
        found->second.WordCount >
            mWords.size() - found->second.WordOffset) {
        return {};
    }
    return {mWords.data() + found->second.WordOffset,
            found->second.WordCount};
}

bool PicaAotShaderPack::Loaded() const noexcept {
    return !mEntries.empty();
}

uint32_t PicaAotShaderPack::DescriptorSchemaVersion() const noexcept {
    return mDescriptorSchemaVersion;
}

size_t PicaAotShaderPack::EntryCount() const noexcept {
    return mEntries.size();
}

const std::filesystem::path& PicaAotShaderPack::Path() const noexcept {
    return mPath;
}

bool PicaEffectiveShaderInventory::Configure(
    const std::filesystem::path& path, std::string* error) {
    Clear();
    if (path.empty()) {
        SetError(error, "effective shader inventory path is empty");
        return false;
    }
    mPath = path;
    return true;
}

void PicaEffectiveShaderInventory::Observe(
    PicaAotShaderStage stage, std::string_view source,
    uint32_t descriptorSchemaVersion,
    uint64_t canonicalPipelineId, uint64_t effectiveShaderKey,
    uint64_t settingsRevision) {
    if (!Enabled() || source.empty())
        return;
    if (descriptorSchemaVersion == 0U) {
        mDescriptorSchemaMismatch = true;
    } else if (mDescriptorSchemaVersion == 0U) {
        mDescriptorSchemaVersion = descriptorSchemaVersion;
    } else if (mDescriptorSchemaVersion != descriptorSchemaVersion) {
        mDescriptorSchemaMismatch = true;
    }
    const auto identity = IdentifyPicaAotShaderSource(source);
    auto [found, inserted] = mEntries.try_emplace(
        std::pair{stage, identity.Id});
    auto& entry = found->second;
    if (inserted) {
        entry.Stage = stage;
        entry.Identity = identity;
        entry.Source.assign(source);
    } else if (entry.Identity != identity || entry.Source != source) {
        return;
    }
    ++entry.ObservationCount;
    if (canonicalPipelineId != 0U)
        entry.CanonicalPipelineIds.insert(canonicalPipelineId);
    if (effectiveShaderKey != 0U)
        entry.EffectiveShaderKeys.insert(effectiveShaderKey);
    entry.SettingsRevisions.insert(settingsRevision);
}

bool PicaEffectiveShaderInventory::Finish(std::string* error) {
    if (!Enabled() || mFinished)
        return !mPath.empty();
    if (mEntries.empty()) {
        SetError(error, "effective shader inventory is empty");
        return false;
    }
    if (mDescriptorSchemaVersion == 0U || mDescriptorSchemaMismatch) {
        SetError(error,
                 "effective shader inventory descriptor schema is invalid");
        return false;
    }
    nlohmann::json shaders = nlohmann::json::array();
    for (const auto& [key, entry] : mEntries) {
        shaders.push_back({
            {"stage", PicaAotShaderStageName(entry.Stage)},
            {"source_id", FormatPicaAotShaderId(entry.Identity.Id)},
            {"secondary_hash",
             FormatPicaAotShaderId(entry.Identity.SecondaryHash)},
            {"source_size", entry.Identity.Size},
            {"observation_count", entry.ObservationCount},
            {"canonical_pipeline_ids",
             HexSet(entry.CanonicalPipelineIds)},
            {"effective_shader_keys",
             HexSet(entry.EffectiveShaderKeys)},
            {"settings_revisions",
             IntegerSet(entry.SettingsRevisions)},
            {"source", entry.Source},
        });
    }
    const nlohmann::json inventory = {
        {"format", "oot3d_pica_effective_shader_inventory_v1"},
        {"descriptor_schema_version", mDescriptorSchemaVersion},
        {"shader_count", shaders.size()},
        {"shaders", std::move(shaders)},
    };
    std::error_code filesystemError;
    if (!mPath.parent_path().empty())
        std::filesystem::create_directories(mPath.parent_path(),
                                            filesystemError);
    if (filesystemError) {
        SetError(error, "could not create shader inventory directory: " +
                            filesystemError.message());
        return false;
    }
    const auto temporary = mPath.string() + ".tmp";
    {
        std::ofstream output(temporary,
                             std::ios::binary | std::ios::trunc);
        output << inventory.dump(2) << '\n';
        if (!output) {
            SetError(error, "could not write effective shader inventory");
            return false;
        }
    }
    std::filesystem::remove(mPath, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(temporary, mPath, filesystemError);
    if (filesystemError) {
        SetError(error, "could not publish effective shader inventory: " +
                            filesystemError.message());
        return false;
    }
    mFinished = true;
    return true;
}

void PicaEffectiveShaderInventory::Clear() {
    mPath.clear();
    mEntries.clear();
    mDescriptorSchemaVersion = 0;
    mDescriptorSchemaMismatch = false;
    mFinished = false;
}

bool PicaEffectiveShaderInventory::Enabled() const noexcept {
    return !mPath.empty() && !mFinished;
}

size_t PicaEffectiveShaderInventory::EntryCount() const noexcept {
    return mEntries.size();
}

const std::filesystem::path&
PicaEffectiveShaderInventory::Path() const noexcept {
    return mPath;
}

} // namespace Fast::Oot3d
