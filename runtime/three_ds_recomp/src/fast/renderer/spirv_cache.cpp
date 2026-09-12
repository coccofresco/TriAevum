#include "fast/renderer/spirv_cache.h"
#include "fast/renderer/cache_file.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>

namespace Fast::Renderer {
namespace {
constexpr uint32_t kMagic = 0x32565053, kVersion = 2, kSpirvMagic = 0x07230203;
constexpr size_t kHeaderBytes = 40;
using Clock = std::chrono::steady_clock;
uint64_t Elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
}
uint64_t Hash(std::span<const uint8_t> bytes, uint64_t seed = 14695981039346656037ULL) {
    for (auto byte : bytes) seed = (seed ^ byte) * 1099511628211ULL;
    return seed;
}
std::span<const uint8_t> Bytes(std::string_view value) {
    return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}
void Append(std::vector<uint8_t>& bytes, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
uint64_t Read(std::span<const uint8_t> bytes, size_t offset, size_t count) {
    uint64_t result = 0;
    for (size_t i = 0; i < count; ++i) result |= uint64_t{bytes[offset + i]} << (8 * i);
    return result;
}
bool ValidWords(std::span<const uint32_t> words) {
    if (words.size() < 5 || words[0] != kSpirvMagic || words[3] == 0 || words[4] != 0) return false;
    for (size_t offset = 5; offset < words.size();) {
        const size_t count = words[offset] >> 16;
        if (count == 0 || count > words.size() - offset) return false;
        offset += count;
    }
    return true;
}
}

void SpirvCache::Configure(std::filesystem::path directory, std::string compilerContract) {
    mDirectory = directory.empty() ? directory : directory / "spirv-v2";
    mCompilerContract = std::move(compilerContract);
    mLastWriteError.clear();
    mStats = {};
}

std::filesystem::path SpirvCache::EntryPath(std::string_view source, SpirvStage stage) const {
    auto contract = mCompilerContract + "/" + std::to_string(static_cast<uint32_t>(stage));
    const auto first = Hash(Bytes(source), Hash(Bytes(contract)));
    const auto second = Hash(Bytes(contract), Hash(Bytes(source), 1099511628211ULL));
    std::ostringstream name;
    name << std::hex << first << '_' << second << ".spvc";
    return mDirectory / name.str();
}

bool SpirvCache::Load(const std::filesystem::path& path, std::string_view source,
                      SpirvStage stage, std::vector<uint32_t>& words) {
    words.clear();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return false;
    const auto reject = [&] { ++mStats.Rejected; return false; };
    if (size < kHeaderBytes || size > MaximumEntryBytes) return reject();
    std::ifstream input(path, std::ios::binary);
    std::array<uint8_t, kHeaderBytes> header{};
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) return reject();
    const auto count = Read(header, 20, 4);
    if (Read(header, 0, 4) != kMagic || Read(header, 4, 4) != kVersion ||
        Read(header, 8, 4) != static_cast<uint32_t>(stage) ||
        Read(header, 12, 4) != mCompilerContract.size() || Read(header, 16, 4) != source.size() ||
        count < 5 || Read(header, 24, 8) != size - kHeaderBytes ||
        mCompilerContract.size() + source.size() + count * 4 != size - kHeaderBytes) return reject();
    std::vector<uint8_t> data(static_cast<size_t>(size - kHeaderBytes));
    if (!input.read(reinterpret_cast<char*>(data.data()), data.size()) ||
        input.peek() != std::char_traits<char>::eof() || Hash(data) != Read(header, 32, 8)) return reject();
    const auto contractEnd = data.begin() + mCompilerContract.size();
    const auto sourceEnd = contractEnd + source.size();
    if (!std::equal(data.begin(), contractEnd, Bytes(mCompilerContract).begin()) ||
        !std::equal(contractEnd, sourceEnd, Bytes(source).begin())) return reject();
    std::vector<uint32_t> candidate;
    candidate.reserve(static_cast<size_t>(count));
    for (size_t offset = mCompilerContract.size() + source.size(); offset < data.size(); offset += 4)
        candidate.push_back(static_cast<uint32_t>(Read(data, offset, 4)));
    if (!ValidWords(candidate)) return reject();
    words = std::move(candidate);
    return true;
}

bool SpirvCache::Store(const std::filesystem::path& path, std::string_view source,
                       SpirvStage stage, const std::vector<uint32_t>& words) {
    const uint64_t size = mCompilerContract.size() + source.size() + uint64_t{words.size()} * 4;
    if (size > MaximumEntryBytes - kHeaderBytes) {
        mLastWriteError = "shader cache entry exceeds storage limit";
        return false;
    }
    std::vector<uint8_t> payload;
    payload.reserve(static_cast<size_t>(size));
    const auto contract = Bytes(mCompilerContract);
    payload.insert(payload.end(), contract.begin(), contract.end());
    const auto text = Bytes(source);
    payload.insert(payload.end(), text.begin(), text.end());
    for (auto word : words) Append(payload, word, 4);
    std::vector<uint8_t> header;
    Append(header, kMagic, 4); Append(header, kVersion, 4);
    Append(header, static_cast<uint32_t>(stage), 4);
    Append(header, mCompilerContract.size(), 4); Append(header, source.size(), 4);
    Append(header, words.size(), 4); Append(header, size, 8); Append(header, Hash(payload), 8);
    return WriteCacheFileAtomically(path, header, payload, &mLastWriteError);
}

std::vector<uint32_t> SpirvCache::Resolve(std::string_view source, SpirvStage stage, const Compile& compile) {
    ++mStats.Requests;
    const auto path = Enabled() ? EntryPath(source, stage) : std::filesystem::path{};
    std::vector<uint32_t> words;
    const auto readStart = Clock::now();
    const bool found = !path.empty() && Load(path, source, stage, words);
    mStats.ReadNanoseconds += Elapsed(readStart);
    if (found) { ++mStats.Hits; return words; }
    ++mStats.Misses;
    ++mStats.Compilations;
    const auto compileStart = Clock::now();
    try {
        words = compile();
        if (!ValidWords(words)) throw std::runtime_error("compiler returned malformed SPIR-V");
    } catch (...) {
        mStats.CompileNanoseconds += Elapsed(compileStart);
        ++mStats.CompilationFailures;
        throw;
    }
    mStats.CompileNanoseconds += Elapsed(compileStart);
    if (!path.empty()) {
        const auto writeStart = Clock::now();
        if (Store(path, source, stage, words)) ++mStats.Writes;
        else ++mStats.WriteFailures;
        mStats.WriteNanoseconds += Elapsed(writeStart);
    }
    return words;
}
}
