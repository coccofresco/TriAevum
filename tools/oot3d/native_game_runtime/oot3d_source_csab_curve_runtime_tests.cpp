#include "oot3d_source_csab_curve_runtime.h"

#include "oot3d_a32_generated.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d/csab_curve_eval.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCodeBase = 0x00100000U;
constexpr uint32_t kDataBase = 0x00010000U;
constexpr uint32_t kStateAddress = kDataBase + 0x100U;
constexpr uint32_t kCurveAddress = kDataBase + 0x200U;
constexpr uint32_t kStackTop = kDataBase + 0xF000U;
constexpr uint32_t kReturnAddress = 0x60000000U;

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void AppendPod(std::vector<uint8_t>* bytes, const T& value) {
    const auto* begin = reinterpret_cast<const uint8_t*>(&value);
    bytes->insert(bytes->end(), begin, begin + sizeof(value));
}

template <typename Key>
std::vector<uint8_t> BuildCurve(
    uint8_t interpolationType, int32_t duration,
    std::span<const Key> keys) {
    Oot3dSourceCsabCurve::Oot3dAnimationCurveHeader header{};
    header.interpolationType = interpolationType;
    header.keyCount = static_cast<int32_t>(keys.size());
    header.duration = duration;

    std::vector<uint8_t> bytes;
    bytes.reserve(sizeof(header) + keys.size_bytes());
    AppendPod(&bytes, header);
    for (const auto& key : keys) {
        AppendPod(&bytes, key);
    }
    return bytes;
}

void SeedMemory(
    Oot3dNativeGame::NativeA32Memory* memory,
    std::span<const uint8_t> codeBin,
    std::span<const uint8_t> curve, uint8_t loop) {
    std::string error;
    Expect(
        memory->MapRegion(
            {"code.bin", kCodeBase, codeBin.size(), false, true,
             codeBin},
            &error),
        "map CSAB code.bin: " + error);
    Expect(
        memory->MapRegion(
            {"csab-test", kDataBase, 0x10000U, true, false, {}},
            &error),
        "map CSAB test memory: " + error);
    Expect(
        memory->Write32(kStateAddress, kCurveAddress) &&
            memory->Write8(kStateAddress + 4U, loop) &&
            memory->WriteBytes(kCurveAddress, curve),
        "seed CSAB test memory");
}

std::vector<uint8_t> ReadBinary(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    Expect(
        static_cast<bool>(stream),
        "cannot open CSAB code.bin: " + path.string());
    const std::streampos end = stream.tellg();
    Expect(end >= 0, "cannot determine CSAB code.bin size");
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    Expect(
        bytes.empty() ||
            static_cast<bool>(stream.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()))),
        "cannot read complete CSAB code.bin");
    return bytes;
}

void CompareCase(
    std::span<const uint8_t> codeBin,
    uint32_t entry, std::span<const uint8_t> curve,
    float frame, uint8_t loop = 0U,
    uint32_t fpscr = 0x03C00010U) {
    Oot3dNativeGame::NativeA32Memory armMemory;
    SeedMemory(&armMemory, codeBin, curve, loop);
    auto sourceMemory = armMemory;

    oot3d::recomp::a32::GuestState armState{};
    armState.r[0] = kStateAddress;
    armState.r[4] = 0x44444444U;
    armState.r[5] = 0x55555555U;
    armState.r[6] = 0x66666666U;
    armState.r[7] = 0x77777777U;
    armState.r[13] = kStackTop;
    armState.r[14] = kReturnAddress;
    armState.r[15] = entry;
    armState.vfp[0] = std::bit_cast<uint32_t>(frame);
    armState.fpscr = fpscr;
    auto sourceState = armState;

    const auto armResult = oot3d::recomp::a32::Dispatch(
        oot3d::recomp::GetA32GeneratedRegistry(), entry,
        armState, armMemory, nullptr, nullptr, 10000U);
    if (armResult.kind !=
            oot3d::recomp::a32::ExitKind::MissingBlock ||
        armResult.pc != kReturnAddress) {
        std::cerr << "CSAB ARM return mismatch: entry=0x" << std::hex
                  << entry << " frame_bits=0x"
                  << std::bit_cast<uint32_t>(frame)
                  << " kind=" << static_cast<uint32_t>(armResult.kind)
                  << " pc=0x" << armResult.pc
                  << " detail=0x" << armResult.detail
                  << std::dec << '\n';
    }
    Expect(
        armResult.kind ==
                oot3d::recomp::a32::ExitKind::MissingBlock &&
            armResult.pc == kReturnAddress,
        "ARM CSAB evaluator did not return to the sentinel");

    Oot3dNativeGame::SourceCsabCurveRuntime runtime(sourceMemory);
    oot3d::recomp::a32::ExecutionResult sourceResult{};
    uint32_t blocksConsumed = 0U;
    Expect(
        runtime.Execute(
            entry, sourceState, sourceMemory, &sourceResult,
            &blocksConsumed),
        "source CSAB evaluator declined a valid curve: " +
            runtime.LastError());
    Expect(
        sourceResult.kind ==
                oot3d::recomp::a32::ExitKind::Branch &&
            sourceResult.pc == kReturnAddress &&
            blocksConsumed == 1U,
        "source CSAB evaluator returned an invalid dispatch result");
    if (sourceState.vfp[0] != armState.vfp[0]) {
        std::cerr << "CSAB result mismatch: entry=0x" << std::hex
                  << entry << " frame_bits=0x"
                  << std::bit_cast<uint32_t>(frame)
                  << " source=0x" << sourceState.vfp[0]
                  << " arm=0x" << armState.vfp[0] << std::dec << '\n';
    }
    Expect(
        sourceState.vfp[0] == armState.vfp[0],
        "source/ARM CSAB result bits differ");
    if (sourceState.fpscr != armState.fpscr) {
        std::cerr << "CSAB FPSCR mismatch: entry=0x" << std::hex
                  << entry << " frame_bits=0x"
                  << std::bit_cast<uint32_t>(frame)
                  << " source=0x" << sourceState.fpscr
                  << " arm=0x" << armState.fpscr << std::dec << '\n';
    }
    Expect(
        sourceState.fpscr == armState.fpscr,
        "source/ARM CSAB FPSCR differs");
    for (uint32_t reg = 4U; reg <= 11U; ++reg) {
        Expect(
            sourceState.r[reg] == armState.r[reg],
            "source/ARM CSAB callee-saved register differs");
    }
    if (sourceMemory.ContentFingerprint() !=
        armMemory.ContentFingerprint()) {
        for (uint32_t address = kStackTop - 80U;
             address < kStackTop; address += 4U) {
            uint32_t sourceWord = 0U;
            uint32_t armWord = 0U;
            if (sourceMemory.Read32(address, &sourceWord) &&
                armMemory.Read32(address, &armWord) &&
                sourceWord != armWord) {
                std::cerr << "CSAB stack mismatch: address=0x"
                          << std::hex << address
                          << " source=0x" << sourceWord
                          << " arm=0x" << armWord << std::dec << '\n';
            }
        }
    }
    Expect(
        sourceState.r[13] == armState.r[13] &&
            sourceMemory.ContentFingerprint() ==
                armMemory.ContentFingerprint(),
        "source/ARM CSAB stack or memory differs");
}

void TestF32Curves(std::span<const uint8_t> codeBin) {
    using Key =
        Oot3dSourceCsabCurve::Oot3dAnimationCurveF32LinearKey;
    const std::array linear{
        Key{0, 1.25F},
        Key{10, 6.5F},
    };
    const auto linearCurve = BuildCurve<Key>(1U, 10, linear);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveF32Entry,
        linearCurve, 3.25F);

    const std::array step{
        Key{0, -2.0F},
        Key{5, 4.0F},
        Key{12, 9.0F},
    };
    const auto stepCurve = BuildCurve<Key>(3U, 12, step);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveF32Entry,
        stepCurve, 8.0F);

    using HermiteKey =
        Oot3dSourceCsabCurve::Oot3dAnimationCurveF32Key;
    const std::array hermite{
        HermiteKey{0, 2.0F, 0.0F, 0.375F},
        HermiteKey{9, -3.25F, -0.625F, 0.0F},
    };
    const auto hermiteCurve =
        BuildCurve<HermiteKey>(2U, 9, hermite);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveF32Entry,
        hermiteCurve, 4.5F);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveF32Entry,
        hermiteCurve, -1.0F, 1U);
}

void TestS16Curves(std::span<const uint8_t> codeBin) {
    using Key =
        Oot3dSourceCsabCurve::Oot3dAnimationCurveS16Key;
    const std::array single{
        Key{0, 0x2000, 0, 0},
    };
    const auto singleCurve = BuildCurve<Key>(1U, 0, single);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveS16Entry,
        singleCurve, 0.0F);

    const std::array hermite{
        Key{0, 0x3000, 0, 0x0800},
        Key{10, -0x3000, -0x1000, 0},
    };
    const auto hermiteCurve = BuildCurve<Key>(2U, 10, hermite);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveS16Entry,
        hermiteCurve, 6.0F);
    CompareCase(
        codeBin,
        Oot3dNativeGame::kSourceCsabCurveS16Entry,
        hermiteCurve, 11.0F, 1U);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error(
                "usage: oot3d_source_csab_curve_runtime_tests "
                "<code.bin>");
        }
        const auto codeBin = ReadBinary(argv[1]);
        TestF32Curves(codeBin);
        TestS16Curves(codeBin);
        std::cout << "source_csab_curve_runtime_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "source_csab_curve_runtime_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
