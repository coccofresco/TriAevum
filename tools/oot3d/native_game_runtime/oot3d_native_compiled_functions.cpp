#include "oot3d_native_compiled_functions.h"

#include "oot3d_native_a32_memory.h"
#include "oot3d_native_source_overlay.h"
#include "recomp/a32_vfp_scalar.h"
#include "oot3d_native_whole_aot_runtime.h"

#if defined(OOT3D_NATIVE_DIRECT_AOT_PLUGIN)
#include "oot3d_native_direct_aot.h"
#elif defined(OOT3D_NATIVE_GENERATED_WHOLE_AOT)
#include "oot3d_whole_aot_generated.h"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kPicaCommandWriterWriteRegisterRange = 0x00307BD8U;
constexpr uint32_t kPicaCommandHeaderLiteral = 0x00307C7CU;
constexpr uint32_t kPicaCommandWriterUploadVertexFloatUniforms = 0x00307C94U;
constexpr uint32_t kPicaMaterialStateEmitFramebufferAccess = 0x00313D6CU;
constexpr uint32_t kPicaFloatUniformConfigLiteral = 0x00307D84U;
constexpr uint32_t kPicaFloatUniformDataLiteral = 0x00307D88U;
constexpr uint32_t kRuntimeMemcpy = 0x00371738U;
constexpr uint32_t kMtx3x4Multiply = 0x0036C174U;
constexpr uint32_t kMtx3x4CopyIfDistinct = 0x00372224U;
constexpr uint32_t kMeshCommandPacketSubmit = 0x00466E2CU;
constexpr uint32_t kAudioEffectProcessFourChannelDelay = 0x004A022CU;
constexpr uint32_t kAudioEffectProcessStereoReverb = 0x004A0338U;
constexpr uint32_t kNngxCurrentCommandPointerLiteral = 0x002F9E60U;
constexpr uint32_t kPicaCommandStatsPointerLiteral = 0x002F9C9CU;
constexpr std::array kCompiledFunctionEntryPoints{
    kPicaCommandWriterWriteRegisterRange,
    kPicaCommandWriterUploadVertexFloatUniforms,
    kPicaMaterialStateEmitFramebufferAccess,
    kMtx3x4Multiply,
    kRuntimeMemcpy,
    kMtx3x4CopyIfDistinct,
    kMeshCommandPacketSubmit,
    kAudioEffectProcessFourChannelDelay,
    kAudioEffectProcessStereoReverb,
};
Oot3dCompiledFunctionStats gStats;
Oot3dWholeAotStats gWholeAotStats;
constexpr uint32_t kProfileCodeBegin = 0x00100000U;
constexpr uint32_t kProfileCodeEnd = 0x00500000U;
constexpr size_t kProfileCodeSlots =
    (kProfileCodeEnd - kProfileCodeBegin) / sizeof(uint32_t);
struct ExternalTargetCounts {
    uint32_t Calls = 0;
    uint32_t ManualCompiledCalls = 0;
    uint32_t TimingSamples = 0;
    uint64_t TimingSampleNanoseconds = 0;
};
std::vector<ExternalTargetCounts> gExternalTargetCounts;
uint64_t gExternalTimingCounter = 0;
constexpr uint64_t kExternalTimingSampleMask = 63U;

void RecordWholeAotExternalTarget(uint32_t entry,
                                  bool manualCompiled,
                                  uint64_t sampledNanoseconds) noexcept {
    if (gExternalTargetCounts.empty() || entry < kProfileCodeBegin ||
        entry >= kProfileCodeEnd || (entry & 3U) != 0U) {
        return;
    }
    auto& target =
        gExternalTargetCounts[(entry - kProfileCodeBegin) / sizeof(uint32_t)];
    ++target.Calls;
    target.ManualCompiledCalls += manualCompiled ? 1U : 0U;
    if (sampledNanoseconds != 0U) {
        ++target.TimingSamples;
        target.TimingSampleNanoseconds += sampledNanoseconds;
    }
}

constexpr uint32_t kFpscrRoundingModeMask = 3U << 22U;
constexpr uint32_t kFpscrExceptionEnableMask = 0x00009F00U;
constexpr uint32_t kFpscrInexact = 1U << 4U;

bool IsFastVfpValue(uint32_t bits) noexcept {
    const uint32_t exponent = bits & 0x7F800000U;
    const uint32_t fraction = bits & 0x007FFFFFU;
    return exponent != 0x7F800000U && (exponent != 0U || fraction == 0U);
}

bool FastVfpMultiply(uint32_t leftBits, uint32_t rightBits,
                     uint32_t* resultBits, uint32_t* exceptionFlags) noexcept {
    if (!IsFastVfpValue(leftBits) || !IsFastVfpValue(rightBits)) {
        return false;
    }
    const float left = std::bit_cast<float>(leftBits);
    const float right = std::bit_cast<float>(rightBits);
    const double exact = static_cast<double>(left) * static_cast<double>(right);
    const float result = static_cast<float>(exact);
    const uint32_t bits = std::bit_cast<uint32_t>(result);
    if (!IsFastVfpValue(bits)) {
        return false;
    }
    if (static_cast<double>(result) != exact) {
        *exceptionFlags |= kFpscrInexact;
    }
    *resultBits = bits;
    return true;
}

bool FastVfpMultiplyAccumulate(uint32_t accumulatorBits, uint32_t leftBits,
                              uint32_t rightBits, uint32_t* resultBits,
                              uint32_t* exceptionFlags) noexcept {
    uint32_t productBits = 0U;
    if (!IsFastVfpValue(accumulatorBits) ||
        !FastVfpMultiply(leftBits, rightBits, &productBits, exceptionFlags)) {
        return false;
    }
    const float accumulator = std::bit_cast<float>(accumulatorBits);
    const float product = std::bit_cast<float>(productBits);
    const double exact =
        static_cast<double>(accumulator) + static_cast<double>(product);
    const float result = static_cast<float>(exact);
    const uint32_t bits = std::bit_cast<uint32_t>(result);
    if (!IsFastVfpValue(bits)) {
        return false;
    }
    if (static_cast<double>(result) != exact) {
        *exceptionFlags |= kFpscrInexact;
    }
    *resultBits = bits;
    return true;
}

bool TryFastMtx3x4Multiply(const std::array<uint32_t, 12>& left,
                          const std::array<uint32_t, 12>& right,
                          uint32_t fpscr,
                          std::array<uint32_t, 12>* output,
                          uint32_t* exceptionFlags) noexcept {
    if ((fpscr &
         (kFpscrRoundingModeMask | kFpscrExceptionEnableMask)) != 0U) {
        return false;
    }
    uint32_t flags = 0U;
    for (size_t row = 0U; row < 3U; ++row) {
        const size_t leftBase = row * 4U;
        for (size_t component = 0U; component < 4U; ++component) {
            uint32_t value = 0U;
            const bool firstOk =
                component == 3U
                    ? FastVfpMultiplyAccumulate(
                          left[leftBase + 3U], right[3U], left[leftBase],
                          &value, &flags)
                    : FastVfpMultiply(right[component], left[leftBase],
                                      &value, &flags);
            if (!firstOk ||
                !FastVfpMultiplyAccumulate(
                    value, right[4U + component], left[leftBase + 1U],
                    &value, &flags) ||
                !FastVfpMultiplyAccumulate(
                    value, right[8U + component], left[leftBase + 2U],
                    &value, &flags)) {
                return false;
            }
            (*output)[leftBase + component] = value;
        }
    }
    *exceptionFlags = flags;
    return true;
}

void ComputeSoftMtx3x4Multiply(const std::array<uint32_t, 12>& left,
                               const std::array<uint32_t, 12>& right,
                               uint32_t fpscr,
                               std::array<uint32_t, 12>* output,
                               uint32_t* exceptionFlags) noexcept {
    uint32_t flags = 0U;
    for (size_t row = 0U; row < 3U; ++row) {
        const size_t leftBase = row * 4U;
        for (size_t component = 0U; component < 4U; ++component) {
            oot3d::recomp::a32::VfpBinary32Result value{};
            if (component == 3U) {
                value = oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                    left[leftBase + 3U], right[3U], left[leftBase], fpscr);
            } else {
                value = oot3d::recomp::a32::VfpBinary32Multiply(
                    right[component], left[leftBase], fpscr);
            }
            flags |= value.exception_flags;
            value = oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                value.value, right[4U + component], left[leftBase + 1U],
                fpscr);
            flags |= value.exception_flags;
            value = oot3d::recomp::a32::VfpBinary32MultiplyAccumulate(
                value.value, right[8U + component], left[leftBase + 2U],
                fpscr);
            flags |= value.exception_flags;
            (*output)[leftBase + component] = value.value;
        }
    }
    *exceptionFlags = flags;
}

bool RangesOverlap(uint32_t left, size_t leftSize, uint32_t right,
                   size_t rightSize) noexcept {
    if (leftSize == 0U || rightSize == 0U) {
        return false;
    }
    const uint64_t leftEnd = static_cast<uint64_t>(left) + leftSize;
    const uint64_t rightEnd = static_cast<uint64_t>(right) + rightSize;
    return static_cast<uint64_t>(left) < rightEnd &&
           static_cast<uint64_t>(right) < leftEnd;
}

uint32_t LoadU32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) |
           (static_cast<uint32_t>(bytes[3]) << 24U);
}

void StoreU32(uint8_t* bytes, uint32_t value) noexcept {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t Add32(uint32_t left, uint32_t right) noexcept {
    return left + right;
}

uint32_t Sub32(uint32_t left, uint32_t right) noexcept {
    return left - right;
}

uint32_t MultiplyLow32(uint32_t left, uint32_t right) noexcept {
    return static_cast<uint32_t>(static_cast<uint64_t>(left) * right);
}

uint32_t ArithmeticShiftRight(uint32_t value, uint32_t shift) noexcept {
    if (shift == 0U) {
        return value;
    }
    const uint32_t shifted = value >> shift;
    if ((value & 0x80000000U) == 0U) {
        return shifted;
    }
    return shifted | (~uint32_t{0} << (32U - shift));
}

uint32_t ScaleSignedSample(uint32_t value, uint32_t coefficient) noexcept {
    if ((value & 0x80000000U) == 0U) {
        return ArithmeticShiftRight(MultiplyLow32(coefficient, value), 7U);
    }
    const uint32_t magnitude = Sub32(0U, value);
    return Sub32(
        0U, ArithmeticShiftRight(MultiplyLow32(coefficient, magnitude), 7U));
}

bool AddressRangeFits(uint32_t address, size_t size) noexcept {
    return static_cast<uint64_t>(address) + size <= (uint64_t{1} << 32U);
}

uint32_t IndexedAddress(uint32_t base, uint32_t index) noexcept {
    return Add32(base, index << 2U);
}

bool MutableRangeDoesNotOverlapMetadata(uint32_t address, size_t size,
                                        uint32_t stateAddress,
                                        size_t stateSize,
                                        uint32_t channelsAddress) noexcept {
    return !RangesOverlap(address, size, stateAddress, stateSize) &&
           !RangesOverlap(address, size, channelsAddress, 16U);
}

bool CompleteNativeCall(
    uint32_t entry,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::ExecutionResult* result,
    size_t bytesProcessed) {
    state.r[15] = state.r[14];
    *result = {
        oot3d::recomp::a32::ExitKind::Branch,
        state.r[15],
        oot3d::recomp::a32::FallbackReason::None,
        entry,
    };
    ++gStats.Calls;
    gStats.BytesProcessed += bytesProcessed;
    return true;
}

bool ExecutePicaCommandWriterWriteRegisterRange(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    const int32_t count = static_cast<int32_t>(state.r[2]);
    if (count < 1) {
        return CompleteNativeCall(
            kPicaCommandWriterWriteRegisterRange, state, result, 0U);
    }

    uint32_t mask = 0U;
    uint32_t valuesAddress = 0U;
    uint32_t commandAddress = 0U;
    uint32_t headerBase = 0U;
    if (!memory.Read32(state.r[13], &mask) ||
        !memory.Read32(state.r[13] + 4U, &valuesAddress) ||
        !memory.Read32(state.r[0] + 8U, &commandAddress) ||
        !memory.Read32(kPicaCommandHeaderLiteral, &headerBase)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const size_t valueBytes = static_cast<size_t>(count) * 4U;
    const size_t wordCount = (static_cast<size_t>(count) + 2U) & ~size_t{1};
    const size_t commandBytes = wordCount * 4U;
    constexpr uint64_t addressSpaceSize = uint64_t{1} << 32U;
    if (static_cast<uint64_t>(valuesAddress) + valueBytes > addressSpaceSize ||
        static_cast<uint64_t>(commandAddress) + commandBytes >
            addressSpaceSize ||
        RangesOverlap(valuesAddress, valueBytes, commandAddress,
                      commandBytes) ||
        !memory.IsWritable(commandAddress, commandBytes) ||
        !memory.IsWritable(state.r[0] + 8U, sizeof(uint32_t))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    thread_local std::vector<uint8_t> command;
    command.assign(commandBytes, 0U);
    if (!memory.ReadBytes(valuesAddress,
                          std::span<uint8_t>(command.data(), 4U)) ||
        (count > 1 &&
         !memory.ReadBytes(
             valuesAddress + 4U,
             std::span<uint8_t>(command.data() + 8U, valueBytes - 4U)))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    const uint32_t header =
        (headerBase + (static_cast<uint32_t>(count) << 20U)) |
        state.r[1] | (mask << 16U) |
        (state.r[3] != 0U ? 0x80000000U : 0U);
    command[4] = static_cast<uint8_t>(header);
    command[5] = static_cast<uint8_t>(header >> 8U);
    command[6] = static_cast<uint8_t>(header >> 16U);
    command[7] = static_cast<uint8_t>(header >> 24U);
    if (!memory.WriteBytes(commandAddress, command) ||
        !memory.Write32(state.r[0] + 8U,
                        commandAddress + static_cast<uint32_t>(commandBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    return CompleteNativeCall(
        kPicaCommandWriterWriteRegisterRange, state, result, commandBytes);
}

void StoreU32(std::vector<uint8_t>& bytes, size_t offset,
              uint32_t value) {
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1U] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<uint8_t>(value >> 24U);
}

bool ExecutePicaCommandWriterUploadVertexFloatUniforms(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    const int32_t count = static_cast<int32_t>(state.r[2]);
    if (count < 1) {
        return CompleteNativeCall(
            kPicaCommandWriterUploadVertexFloatUniforms, state, result, 0U);
    }

    const size_t valueBytes = static_cast<size_t>(count) * 16U;
    const size_t commandBytes = valueBytes + 16U;
    const uint32_t valuesAddress = state.r[3];
    uint32_t commandAddress = 0U;
    uint32_t configLiteral = 0U;
    uint32_t dataLiteral = 0U;
    constexpr uint64_t addressSpaceSize = uint64_t{1} << 32U;
    if (static_cast<uint64_t>(valuesAddress) + valueBytes > addressSpaceSize ||
        !memory.Read32(state.r[0] + 8U, &commandAddress) ||
        !memory.Read32(kPicaFloatUniformConfigLiteral, &configLiteral) ||
        !memory.Read32(kPicaFloatUniformDataLiteral, &dataLiteral) ||
        static_cast<uint64_t>(commandAddress) + commandBytes >
            addressSpaceSize ||
        RangesOverlap(valuesAddress, valueBytes, commandAddress,
                      commandBytes) ||
        !memory.IsMapped(valuesAddress, valueBytes) ||
        !memory.IsWritable(commandAddress, commandBytes) ||
        !memory.IsWritable(state.r[0] + 8U, sizeof(uint32_t))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    thread_local std::vector<uint8_t> values;
    thread_local std::vector<uint8_t> command;
    values.resize(valueBytes);
    if (!memory.ReadBytes(valuesAddress, values)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    command.assign(commandBytes, 0U);
    StoreU32(command, 0U, state.r[1] | 0x80000000U);
    StoreU32(command, 4U, configLiteral);
    StoreU32(command, 12U,
             ((static_cast<uint32_t>(count) * 4U - 1U) << 20U) |
                 dataLiteral);
    size_t outputOffset = 8U;
    for (size_t vectorIndex = 0U;
         vectorIndex < static_cast<size_t>(count); ++vectorIndex) {
        const size_t inputOffset = vectorIndex * 16U;
        for (size_t component = 0U; component < 4U; ++component) {
            const size_t sourceOffset =
                inputOffset + (3U - component) * sizeof(uint32_t);
            std::copy_n(values.begin() +
                            static_cast<std::ptrdiff_t>(sourceOffset),
                        sizeof(uint32_t),
                        command.begin() +
                            static_cast<std::ptrdiff_t>(outputOffset));
            outputOffset += sizeof(uint32_t);
            if (vectorIndex == 0U && component == 0U) {
                outputOffset += sizeof(uint32_t);
            }
        }
    }
    if (!memory.WriteBytes(commandAddress, command) ||
        !memory.Write32(state.r[0] + 8U,
                        commandAddress + static_cast<uint32_t>(commandBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    return CompleteNativeCall(kPicaCommandWriterUploadVertexFloatUniforms,
                              state, result, commandBytes);
}

void SetCmpFlags(oot3d::recomp::a32::GuestState& state,
                 uint32_t left, uint32_t right) noexcept {
    using namespace oot3d::recomp::a32;
    const uint32_t result = left - right;
    const bool overflow = ((left ^ right) & (left ^ result) & 0x80000000U) != 0U;
    state.cpsr =
        (state.cpsr & ~(kFlagN | kFlagZ | kFlagC | kFlagV)) |
        ((result & 0x80000000U) != 0U ? kFlagN : 0U) |
        (result == 0U ? kFlagZ : 0U) |
        (left >= right ? kFlagC : 0U) |
        (overflow ? kFlagV : 0U);
}

bool ExecutePicaMaterialStateEmitFramebufferAccess(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    constexpr uint16_t kFramebufferAccessType = 0x6030U;
    constexpr uint16_t kAlternativeAccessType = 0x6051U;
    constexpr uint32_t kCommandHeader = 0x803F0112U;
    constexpr size_t kCommandBytes = 6U * sizeof(uint32_t);

    const uint32_t materialAddress = state.r[0];
    const uint8_t* material = memory.GetReadPointer(materialAddress, 0x14U);
    if (material == nullptr) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const uint16_t type = static_cast<uint16_t>(material[0x0CU]) |
                          static_cast<uint16_t>(material[0x0DU] << 8U);
    const uint16_t flags = static_cast<uint16_t>(material[0x0EU]) |
                           static_cast<uint16_t>(material[0x0FU] << 8U);
    const uint8_t accessMode = material[0x10U];
    const uint8_t accessOverride = material[0x11U];
    const uint8_t colorAccess = material[0x12U];
    const uint8_t depthAccess = material[0x13U];

    uint32_t ownerAddress = 0U;
    uint32_t commandAddress = 0U;
    if (!memory.Read32(materialAddress, &ownerAddress) ||
        !memory.Read32(ownerAddress + 8U, &commandAddress) ||
        !AddressRangeFits(commandAddress, kCommandBytes) ||
        !memory.IsWritable(commandAddress, kCommandBytes) ||
        !memory.IsWritable(ownerAddress + 8U, sizeof(uint32_t))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const uint16_t lowAccessFlags = flags & 0xFU;
    const bool framebufferType = type == kFramebufferAccessType;
    const bool emitColorRead =
        !framebufferType ||
        (lowAccessFlags != 0U &&
         (accessMode != 0U || ((0xFU & ~flags) != 0U) ||
          accessOverride != 0U));
    const bool emitColorWrite = !framebufferType || lowAccessFlags != 0U;
    const bool emitDepthRead =
        type == kAlternativeAccessType ||
        (framebufferType && colorAccess != 0U &&
         (depthAccess != 0U || lowAccessFlags != 0U));
    const bool emitDepthWrite =
        framebufferType && colorAccess != 0U && depthAccess != 0U;

    std::array<uint8_t, kCommandBytes> command{};
    StoreU32(command.data() + 0U, emitColorRead ? 0xFU : 0U);
    StoreU32(command.data() + 4U, kCommandHeader);
    StoreU32(command.data() + 8U, emitColorWrite ? 0xFU : 0U);
    StoreU32(command.data() + 12U, emitDepthRead ? 0x2U : 0U);
    StoreU32(command.data() + 16U, emitDepthWrite ? 0x2U : 0U);
    StoreU32(command.data() + 20U, 0U);
    if (!memory.WriteBytes(commandAddress, command) ||
        !memory.Write32(ownerAddress + 8U,
                        commandAddress + static_cast<uint32_t>(kCommandBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    state.r[0] = ownerAddress;
    state.r[1] = commandAddress + static_cast<uint32_t>(kCommandBytes);
    state.r[2] = kFramebufferAccessType;
    state.r[3] = framebufferType && lowAccessFlags != 0U
                     ? (accessMode != 0U ? accessMode : 0xFU)
                     : 0U;
    state.r[12] = 0U;
    if (!framebufferType) {
        SetCmpFlags(state, type, kFramebufferAccessType);
    } else if (colorAccess == 0U) {
        SetCmpFlags(state, 0U, 0U);
    } else {
        SetCmpFlags(state, depthAccess, 0U);
    }
    return CompleteNativeCall(kPicaMaterialStateEmitFramebufferAccess,
                              state, result, kCommandBytes);
}

bool ExecuteRuntimeMemcpy(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    const uint32_t destination = state.r[0];
    const uint32_t source = state.r[1];
    const size_t size = state.r[2];
    constexpr uint64_t addressSpaceSize = uint64_t{1} << 32U;
    if (static_cast<uint64_t>(destination) + size > addressSpaceSize ||
        static_cast<uint64_t>(source) + size > addressSpaceSize) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    if (destination == source) {
        state.r[0] = destination + static_cast<uint32_t>(size);
        state.r[1] = source + static_cast<uint32_t>(size);
        return CompleteNativeCall(kRuntimeMemcpy, state, result, size);
    }
    if (RangesOverlap(destination, size, source, size)) {
        ++gStats.RetainedArmFallbacks;
        ++gStats.MemcpyOverlapFallbacks;
        gStats.MemcpyOverlapBytes += size;
        if (destination > source) {
            ++gStats.MemcpyDestinationAfterSourceFallbacks;
        } else {
            ++gStats.MemcpyDestinationBeforeSourceFallbacks;
        }
        return false;
    }
    if (size != 0U) {
        const uint8_t* sourceBytes = memory.GetReadPointer(source, size);
        if (sourceBytes == nullptr || !memory.IsWritable(destination, size) ||
            !memory.WriteBytes(
                destination, std::span<const uint8_t>(sourceBytes, size))) {
            ++gStats.RetainedArmFallbacks;
            return false;
        }
    }
    state.r[0] = destination + static_cast<uint32_t>(size);
    state.r[1] = source + static_cast<uint32_t>(size);
    return CompleteNativeCall(kRuntimeMemcpy, state, result, size);
}

bool ExecuteMtx3x4Multiply(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    constexpr size_t matrixWords = 12U;
    constexpr size_t matrixBytes = matrixWords * sizeof(uint32_t);
    if (!memory.IsMapped(state.r[1], matrixBytes) ||
        !memory.IsMapped(state.r[2], matrixBytes) ||
        !memory.IsWritable(state.r[0], matrixBytes)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    std::array<uint32_t, matrixWords> left{};
    std::array<uint32_t, matrixWords> right{};
    std::array<uint32_t, matrixWords> output{};
    if (!memory.ReadBytes(
            state.r[1],
            std::span<uint8_t>(reinterpret_cast<uint8_t*>(left.data()),
                               matrixBytes)) ||
        !memory.ReadBytes(
            state.r[2],
            std::span<uint8_t>(reinterpret_cast<uint8_t*>(right.data()),
                               matrixBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    uint32_t exceptionFlags = 0U;
    if (TryFastMtx3x4Multiply(left, right, state.fpscr, &output,
                             &exceptionFlags)) {
        ++gStats.Mtx3x4FastCalls;
        if (!gExternalTargetCounts.empty() &&
            gStats.Mtx3x4ValidationCalls < 4096U) {
            std::array<uint32_t, matrixWords> reference{};
            uint32_t referenceFlags = 0U;
            ComputeSoftMtx3x4Multiply(left, right, state.fpscr, &reference,
                                      &referenceFlags);
            ++gStats.Mtx3x4ValidationCalls;
            gStats.Mtx3x4ValueMismatches += output != reference ? 1U : 0U;
            gStats.Mtx3x4FlagMismatches +=
                exceptionFlags != referenceFlags ? 1U : 0U;
        }
    } else {
        ++gStats.Mtx3x4SoftCalls;
        ComputeSoftMtx3x4Multiply(left, right, state.fpscr, &output,
                                  &exceptionFlags);
    }
    if (!memory.WriteBytes(
            state.r[0],
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(output.data()),
                matrixBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    std::copy(output.begin(), output.end(), state.vfp.begin());
    std::copy_n(right.begin() + 8, 4U, state.vfp.begin() + 12);
    state.fpscr |= exceptionFlags;
    state.r[1] = state.r[0] + 16U;
    state.r[2] += 32U;
    return CompleteNativeCall(kMtx3x4Multiply, state, result, matrixBytes);
}

bool ExecuteMtx3x4CopyIfDistinct(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    constexpr size_t matrixBytes = 12U * sizeof(uint32_t);
    if (state.r[0] == state.r[1]) {
        return CompleteNativeCall(kMtx3x4CopyIfDistinct, state, result, 0U);
    }
    if (!memory.IsMapped(state.r[1], matrixBytes) ||
        !memory.IsWritable(state.r[0], matrixBytes)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    std::array<uint8_t, matrixBytes> matrix{};
    if (!memory.ReadBytes(state.r[1], matrix) ||
        !memory.WriteBytes(state.r[0], matrix)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    return CompleteNativeCall(kMtx3x4CopyIfDistinct, state, result,
                              matrixBytes);
}

bool ExecuteMeshCommandPacketSubmit(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    const uint32_t packetAddress = state.r[0];
    uint32_t packetSize = 0U;
    uint32_t activeBuffer = 0U;
    uint32_t commandPointerGlobal = 0U;
    uint32_t statsPointerGlobal = 0U;
    uint32_t destinationAddress = 0U;
    uint32_t statsValue = 0U;
    if (!memory.Read32(packetAddress + 0x10U, &packetSize) ||
        !memory.Read32(packetAddress + 0x14U, &activeBuffer) ||
        !memory.Read32(kNngxCurrentCommandPointerLiteral,
                       &commandPointerGlobal) ||
        !memory.Read32(kPicaCommandStatsPointerLiteral,
                       &statsPointerGlobal) ||
        !memory.Read32(commandPointerGlobal, &destinationAddress) ||
        !memory.Read32(statsPointerGlobal, &statsValue)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    const uint32_t packetSlotAddress =
        packetAddress + (activeBuffer << 2U) + 8U;
    uint32_t sourceAddress = 0U;
    if (!memory.Read32(packetSlotAddress, &sourceAddress)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const int32_t signedSize = static_cast<int32_t>(packetSize);
    const int32_t pairCount = signedSize / 8;
    const size_t copiedBytes =
        pairCount > 0 ? static_cast<size_t>(pairCount) * 8U : 0U;
    constexpr uint64_t addressSpaceSize = uint64_t{1} << 32U;
    const size_t sourceReadBytes = copiedBytes == 0U ? 0U : copiedBytes + 4U;
    if ((copiedBytes != 0U &&
         (static_cast<uint64_t>(sourceAddress) + sourceReadBytes >
              addressSpaceSize ||
          static_cast<uint64_t>(destinationAddress) + copiedBytes >
              addressSpaceSize ||
          RangesOverlap(sourceAddress, sourceReadBytes, destinationAddress,
                        copiedBytes) ||
          !memory.IsMapped(sourceAddress, sourceReadBytes) ||
          !memory.IsWritable(destinationAddress, copiedBytes))) ||
        !memory.IsWritable(statsPointerGlobal, sizeof(uint32_t)) ||
        (copiedBytes != 0U &&
         RangesOverlap(destinationAddress, copiedBytes, statsPointerGlobal,
                       sizeof(uint32_t)))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const uint8_t* packet = sourceReadBytes == 0U
                                ? nullptr
                                : memory.GetReadPointer(sourceAddress,
                                                        sourceReadBytes);
    if (sourceReadBytes != 0U && packet == nullptr) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    if (copiedBytes != 0U &&
        !memory.WriteBytes(
            destinationAddress,
            std::span<const uint8_t>(packet, copiedBytes))) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    const uint32_t updatedStats = statsValue + packetSize;
    if (!memory.Write32(statsPointerGlobal, updatedStats)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    state.r[0] = updatedStats;
    state.r[1] = statsPointerGlobal;
    state.r[2] = statsValue;
    return CompleteNativeCall(kMeshCommandPacketSubmit, state, result,
                              copiedBytes);
}

bool ExecuteAudioEffectProcessFourChannelDelay(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    constexpr size_t stateSize = 0x57U;
    constexpr size_t channelPointerBytes = 4U * sizeof(uint32_t);
    constexpr size_t samplesPerChannel = 160U;
    constexpr size_t sampleBytes = samplesPerChannel * sizeof(uint32_t);
    const uint32_t stateAddress = state.r[0];
    const uint32_t channelsAddress = state.r[1];
    const uint8_t* stateRead = memory.GetReadPointer(stateAddress, stateSize);
    if (stateRead == nullptr) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    if (stateRead[0x56U] == 0U) {
        return CompleteNativeCall(kAudioEffectProcessFourChannelDelay, state,
                                  result, 0U);
    }
    const uint8_t* channelPointers =
        memory.GetReadPointer(channelsAddress, channelPointerBytes);

    const uint32_t channelCount = stateRead[0x55U];
    if (channelPointers == nullptr || channelCount > 4U ||
        !memory.IsWritable(stateAddress, stateSize)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    struct ChannelView {
        uint32_t InputAddress = 0U;
        uint32_t DelayAddress = 0U;
        uint8_t* Input = nullptr;
        uint8_t* Delay = nullptr;
    };
    std::array<ChannelView, 4> channels{};
    const uint32_t delayIndex = LoadU32(stateRead + 0x40U);
    const uint32_t firstSample = MultiplyLow32(delayIndex, 160U);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        auto& view = channels[channel];
        view.InputAddress = LoadU32(channelPointers + channel * 4U);
        const uint32_t delayBase =
            LoadU32(stateRead + 0x1CU + channel * 4U);
        view.DelayAddress = IndexedAddress(delayBase, firstSample);
        if (!AddressRangeFits(view.InputAddress, sampleBytes) ||
            !AddressRangeFits(view.DelayAddress, sampleBytes) ||
            !memory.IsWritable(view.InputAddress, sampleBytes) ||
            !memory.IsWritable(view.DelayAddress, sampleBytes) ||
            !MutableRangeDoesNotOverlapMetadata(
                view.InputAddress, sampleBytes, stateAddress, stateSize,
                channelsAddress) ||
            !MutableRangeDoesNotOverlapMetadata(
                view.DelayAddress, sampleBytes, stateAddress, stateSize,
                channelsAddress)) {
            ++gStats.RetainedArmFallbacks;
            return false;
        }
    }

    uint8_t* stateWrite = memory.GetWritePointer(stateAddress, stateSize);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        auto& view = channels[channel];
        view.Input = memory.GetWritePointer(view.InputAddress, sampleBytes);
        view.Delay = memory.GetWritePointer(view.DelayAddress, sampleBytes);
        if (view.Input == nullptr || view.Delay == nullptr) {
            ++gStats.RetainedArmFallbacks;
            return false;
        }
    }

    const uint32_t delayCoefficient = LoadU32(stateRead + 0x44U);
    const uint32_t inputCoefficient = LoadU32(stateRead + 0x48U);
    const uint32_t feedbackCoefficient = LoadU32(stateRead + 0x4CU);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        auto& view = channels[channel];
        uint8_t* feedbackState = stateWrite + 0x2CU + channel * 4U;
        for (size_t sample = 0U; sample < samplesPerChannel; ++sample) {
            const size_t offset = sample * sizeof(uint32_t);
            const uint32_t delayed = LoadU32(view.Delay + offset);
            const uint32_t damped =
                ScaleSignedSample(delayed, delayCoefficient);
            const uint32_t input = LoadU32(view.Input + offset);
            const uint32_t difference = Sub32(input, damped);
            const uint32_t accumulated = Add32(
                MultiplyLow32(inputCoefficient, difference),
                MultiplyLow32(feedbackCoefficient,
                              LoadU32(feedbackState)));
            const uint32_t filtered = ArithmeticShiftRight(accumulated, 7U);
            StoreU32(feedbackState, filtered);
            StoreU32(view.Delay + offset, filtered);
            StoreU32(view.Input + offset, delayed);
        }
    }
    const uint32_t nextIndex = Add32(delayIndex, 1U);
    const uint32_t delayLength = LoadU32(stateRead + 0x3CU);
    StoreU32(stateWrite + 0x40U,
             nextIndex >= delayLength ? 0U : nextIndex);
    return CompleteNativeCall(
        kAudioEffectProcessFourChannelDelay, state, result,
        static_cast<size_t>(channelCount) * sampleBytes);
}

bool ExecuteAudioEffectProcessStereoReverb(
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    constexpr size_t stateSize = 0x101U;
    constexpr size_t channelPointerBytes = 4U * sizeof(uint32_t);
    constexpr size_t samplesPerChannel = 160U;
    constexpr size_t sampleBytes = samplesPerChannel * sizeof(uint32_t);
    const uint32_t stateAddress = state.r[0];
    const uint32_t channelsAddress = state.r[1];
    const uint8_t* stateRead = memory.GetReadPointer(stateAddress, stateSize);
    if (stateRead == nullptr) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }
    if (stateRead[0x100U] == 0U) {
        return CompleteNativeCall(kAudioEffectProcessStereoReverb, state,
                                  result, 0U);
    }
    const uint8_t* channelPointers =
        memory.GetReadPointer(channelsAddress, channelPointerBytes);
    if (channelPointers == nullptr ||
        !memory.IsWritable(stateAddress, stateSize)) {
        ++gStats.RetainedArmFallbacks;
        return false;
    }

    const uint32_t primaryCursor = LoadU32(stateRead + 0x9CU);
    const uint32_t secondaryCursor = LoadU32(stateRead + 0xA4U);
    const uint32_t delayACursor = LoadU32(stateRead + 0xB0U);
    const uint32_t delayBCursor = LoadU32(stateRead + 0xB4U);
    const uint32_t feedbackCursor = LoadU32(stateRead + 0xC4U);
    struct ChannelView {
        std::array<uint32_t, 6> Addresses{};
        std::array<uint8_t*, 6> Buffers{};
    };
    std::array<ChannelView, 2> channels{};
    for (uint32_t channel = 0U; channel < channels.size(); ++channel) {
        auto& view = channels[channel];
        view.Addresses = {
            LoadU32(channelPointers + channel * 4U),
            IndexedAddress(LoadU32(stateRead + 0x38U + channel * 4U),
                           primaryCursor),
            IndexedAddress(LoadU32(stateRead + 0x48U + channel * 4U),
                           secondaryCursor),
            IndexedAddress(LoadU32(stateRead + 0x58U + channel * 8U),
                           delayACursor),
            IndexedAddress(LoadU32(stateRead + 0x5CU + channel * 8U),
                           delayBCursor),
            IndexedAddress(LoadU32(stateRead + 0x78U + channel * 4U),
                           feedbackCursor),
        };
        for (const uint32_t address : view.Addresses) {
            if (!AddressRangeFits(address, sampleBytes) ||
                !memory.IsWritable(address, sampleBytes) ||
                !MutableRangeDoesNotOverlapMetadata(
                    address, sampleBytes, stateAddress, stateSize,
                    channelsAddress)) {
                ++gStats.RetainedArmFallbacks;
                return false;
            }
        }
    }

    uint8_t* stateWrite = memory.GetWritePointer(stateAddress, stateSize);
    for (auto& channel : channels) {
        for (size_t buffer = 0U; buffer < channel.Buffers.size(); ++buffer) {
            channel.Buffers[buffer] =
                memory.GetWritePointer(channel.Addresses[buffer], sampleBytes);
            if (channel.Buffers[buffer] == nullptr) {
                ++gStats.RetainedArmFallbacks;
                return false;
            }
        }
    }

    const uint32_t delayACoefficient = LoadU32(stateRead + 0xB8U);
    const uint32_t delayBCoefficient = LoadU32(stateRead + 0xBCU);
    const uint32_t feedbackCoefficient = LoadU32(stateRead + 0xC8U);
    const uint32_t primaryCoefficient = LoadU32(stateRead + 0xDCU);
    const uint32_t outputCoefficient = LoadU32(stateRead + 0xE0U);
    const uint32_t dampingCoefficient = LoadU32(stateRead + 0xE8U);
    for (uint32_t channel = 0U; channel < channels.size(); ++channel) {
        auto& buffers = channels[channel].Buffers;
        uint8_t* channelState = stateWrite + 0xCCU + channel * 4U;
        for (size_t sample = 0U; sample < samplesPerChannel; ++sample) {
            const size_t offset = sample * sizeof(uint32_t);
            const uint32_t input = LoadU32(buffers[0] + offset);
            const uint32_t primary = LoadU32(buffers[1] + offset);
            StoreU32(buffers[1] + offset, input);
            const uint32_t secondary = LoadU32(buffers[2] + offset);
            StoreU32(buffers[2] + offset, input);
            const uint32_t weightedPrimary =
                MultiplyLow32(primary, primaryCoefficient);

            const uint32_t delayA = LoadU32(buffers[3] + offset);
            StoreU32(buffers[3] + offset,
                     Add32(ScaleSignedSample(delayA, delayACoefficient),
                           secondary));
            const uint32_t delayB = LoadU32(buffers[4] + offset);
            StoreU32(buffers[4] + offset,
                     Add32(secondary,
                           ScaleSignedSample(delayB, delayBCoefficient)));

            const uint32_t oldFeedback = LoadU32(buffers[5] + offset);
            const uint32_t mixedFeedback = Add32(
                Sub32(delayA, delayB),
                ScaleSignedSample(oldFeedback, feedbackCoefficient));
            StoreU32(buffers[5] + offset, mixedFeedback);
            const uint32_t feedbackDifference =
                Sub32(oldFeedback,
                      ScaleSignedSample(mixedFeedback, feedbackCoefficient));
            const uint32_t dampingInput =
                Add32(LoadU32(channelState), feedbackDifference);
            const uint32_t filtered = Sub32(
                feedbackDifference,
                ArithmeticShiftRight(
                    MultiplyLow32(dampingCoefficient, dampingInput), 7U));
            StoreU32(channelState, filtered);
            const uint32_t output = ArithmeticShiftRight(
                Add32(MultiplyLow32(filtered, outputCoefficient),
                      weightedPrimary),
                7U);
            StoreU32(buffers[0] + offset, output);
        }
    }

    const auto advanceCursor = [&](size_t cursorOffset,
                                   size_t lengthOffset,
                                   uint32_t cursor) {
        const uint32_t advanced = Add32(cursor, 160U);
        StoreU32(stateWrite + cursorOffset,
                 advanced < LoadU32(stateRead + lengthOffset) ? advanced
                                                              : 0U);
    };
    advanceCursor(0x9CU, 0x98U, primaryCursor);
    advanceCursor(0xA4U, 0xA0U, secondaryCursor);
    advanceCursor(0xB0U, 0xA8U, delayACursor);
    advanceCursor(0xB4U, 0xACU, delayBCursor);
    advanceCursor(0xC4U, 0xC0U, feedbackCursor);
    return CompleteNativeCall(kAudioEffectProcessStereoReverb, state, result,
                              channels.size() * 6U * sampleBytes);
}

bool ExecuteOot3dManualCompiledFunction(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result) {
    switch (pc) {
    case kPicaCommandWriterWriteRegisterRange:
        return ExecutePicaCommandWriterWriteRegisterRange(state, memory, result);
    case kPicaCommandWriterUploadVertexFloatUniforms:
        return ExecutePicaCommandWriterUploadVertexFloatUniforms(
            state, memory, result);
    case kPicaMaterialStateEmitFramebufferAccess:
        return ExecutePicaMaterialStateEmitFramebufferAccess(
            state, memory, result);
    case kRuntimeMemcpy:
        return ExecuteRuntimeMemcpy(state, memory, result);
    case kMtx3x4Multiply:
        return ExecuteMtx3x4Multiply(state, memory, result);
    case kMtx3x4CopyIfDistinct:
        return ExecuteMtx3x4CopyIfDistinct(state, memory, result);
    case kMeshCommandPacketSubmit:
        return ExecuteMeshCommandPacketSubmit(state, memory, result);
    case kAudioEffectProcessFourChannelDelay:
        return ExecuteAudioEffectProcessFourChannelDelay(state, memory, result);
    case kAudioEffectProcessStereoReverb:
        return ExecuteAudioEffectProcessStereoReverb(state, memory, result);
    default:
        return false;
    }
}

Oot3dWholeAotFlow ExecuteWholeAotExternalCall(
    uint32_t entry,
    Oot3dWholeAotFrame& frame,
    NativeA32Memory& memory) {
    auto& state = frame.Guest;
    state.r[15] = entry;
    oot3d::recomp::a32::ExecutionResult result{};
    if (ExecuteOot3dSourceOverlay(entry, state, memory, &result)) {
        switch (result.kind) {
        case oot3d::recomp::a32::ExitKind::Branch:
        case oot3d::recomp::a32::ExitKind::Fallthrough:
            return Oot3dAotReturned(result.pc);
        case oot3d::recomp::a32::ExitKind::Svc:
            return {Oot3dWholeAotFlowKind::Svc, result.pc, result.detail};
        case oot3d::recomp::a32::ExitKind::BlockLimit:
            return {Oot3dWholeAotFlowKind::BlockLimit, result.pc,
                    result.detail};
        case oot3d::recomp::a32::ExitKind::MemoryFault:
            return {Oot3dWholeAotFlowKind::MemoryFault, result.pc,
                    result.detail};
        default:
            break;
        }
    }
    const bool sampleTiming =
        !gExternalTargetCounts.empty() &&
        ((++gExternalTimingCounter & kExternalTimingSampleMask) == 0U);
    const auto timingStart =
        sampleTiming ? std::chrono::steady_clock::now()
                     : std::chrono::steady_clock::time_point{};
    const bool manualCompiled =
        ExecuteOot3dManualCompiledFunction(entry, state, memory, &result);
    const uint64_t sampledNanoseconds =
        sampleTiming
            ? static_cast<uint64_t>(
                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now() - timingStart)
                      .count())
            : 0U;
    RecordWholeAotExternalTarget(entry, manualCompiled, sampledNanoseconds);
    if (!manualCompiled) {
        return Oot3dAotBranch(entry);
    }
    switch (result.kind) {
    case oot3d::recomp::a32::ExitKind::Branch:
    case oot3d::recomp::a32::ExitKind::Fallthrough:
        return Oot3dAotReturned(result.pc);
    case oot3d::recomp::a32::ExitKind::MemoryFault:
        return {Oot3dWholeAotFlowKind::MemoryFault, result.pc, result.detail};
    default:
        return {Oot3dWholeAotFlowKind::Unsupported, result.pc, result.detail};
    }
}

Oot3dWholeAotFlow ContinueWholeAotExternalCallInGuest(
    uint32_t entry, Oot3dWholeAotFrame&, NativeA32Memory&) {
    return Oot3dAotBranch(entry);
}

struct SourceOverlayBlockEntryContext {
    oot3d::recomp::a32::BlockEntryCallback Original = nullptr;
    void* OriginalUser = nullptr;
    NativeA32Memory* Memory = nullptr;
};

bool IsMaterializedDirectCallBoundary(
    uint32_t entry, const oot3d::recomp::a32::GuestState& state,
    const NativeA32Memory& memory) {
    const uint32_t returnAddress = state.r[14];
    if (returnAddress < 4U) {
        return false;
    }
    const uint32_t callAddress = returnAddress - 4U;
    uint32_t instruction = 0U;
    if (!memory.ReadFast<uint32_t>(callAddress, &instruction)) {
        return false;
    }
    const uint32_t condition = instruction >> 28U;
    const bool armBranchWithLink =
        condition != 0xFU && (instruction & 0x0F000000U) == 0x0B000000U;
    if (!armBranchWithLink) {
        return false;
    }
    int64_t displacement =
        static_cast<int64_t>(instruction & 0x00FFFFFFU) << 2U;
    if ((displacement & 0x02000000LL) != 0) {
        displacement -= 0x04000000LL;
    }
    const uint32_t target = static_cast<uint32_t>(
        static_cast<int64_t>(callAddress) + 8LL + displacement);
    return target == entry;
}

void SourceOverlayBlockEntry(
    uint32_t pc, oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory, void* user) {
    auto& context = *static_cast<SourceOverlayBlockEntryContext*>(user);
    const auto entries = Oot3dSourceOverlayEntryPoints();
    if (!Oot3dSourceOverlayEntryActive(pc) && context.Memory != nullptr &&
        std::binary_search(entries.begin(), entries.end(), pc) &&
        IsMaterializedDirectCallBoundary(pc, state, *context.Memory)) {
        Oot3dWholeAotExitAt(pc, state);
    }
    if (context.Original != nullptr) {
        context.Original(pc, state, memory, context.OriginalUser);
    }
}

struct Oot3dBlockEntrySelection final {
    std::span<const uint32_t> Entries;
    const Oot3dAotBlockEntryFilter* Filter = nullptr;
};

Oot3dBlockEntrySelection MergeSourceOverlayBlockEntries(
    const uint32_t* entries, size_t entryCount) {
    const auto overlayEntries = Oot3dSourceOverlayEntryPoints();
    struct Cache {
        const uint32_t* Entries = nullptr;
        size_t EntryCount = 0U;
        const uint32_t* OverlayEntries = nullptr;
        size_t OverlayEntryCount = 0U;
        std::vector<uint32_t> Merged;
        Oot3dAotBlockEntryFilter Filter;
        bool Initialized = false;
    };
    thread_local Cache cache;
    if (!cache.Initialized || cache.Entries != entries ||
        cache.EntryCount != entryCount ||
        cache.OverlayEntries != overlayEntries.data() ||
        cache.OverlayEntryCount != overlayEntries.size()) {
        cache.Initialized = true;
        cache.Entries = entries;
        cache.EntryCount = entryCount;
        cache.OverlayEntries = overlayEntries.data();
        cache.OverlayEntryCount = overlayEntries.size();
        cache.Merged.clear();
        if (entryCount != 0U) {
            cache.Merged.assign(entries, entries + entryCount);
        }
        cache.Merged.insert(cache.Merged.end(), overlayEntries.begin(),
                            overlayEntries.end());
        std::sort(cache.Merged.begin(), cache.Merged.end());
        cache.Merged.erase(
            std::unique(cache.Merged.begin(), cache.Merged.end()),
            cache.Merged.end());
        cache.Filter = {};
        for (const uint32_t pc : cache.Merged) {
            cache.Filter.Insert(pc);
        }
    }
    return {
        cache.Merged,
        cache.Merged.empty() ? nullptr : &cache.Filter,
    };
}

} // namespace

bool Oot3dWholeAotAvailable() noexcept {
#if defined(OOT3D_NATIVE_GENERATED_WHOLE_AOT)
    return !Oot3dWholeAotEntryPoints().empty();
#else
    return false;
#endif
}

std::span<const uint32_t> Oot3dCompiledFunctionEntryPoints(
    bool includeWholeAot, bool includeManualCompiledFunctions) noexcept {
#if defined(OOT3D_NATIVE_GENERATED_WHOLE_AOT)
    const auto buildEntries = [](bool wholeAot, bool manual) {
        std::vector<uint32_t> result;
        if (manual) {
            result.insert(result.end(), kCompiledFunctionEntryPoints.begin(),
                          kCompiledFunctionEntryPoints.end());
        }
        if (wholeAot) {
            const auto entries = Oot3dWholeAotEntryPoints();
            result.insert(result.end(), entries.begin(), entries.end());
        }
        const auto sourceOverlay = Oot3dSourceOverlayEntryPoints();
        result.insert(result.end(), sourceOverlay.begin(),
                      sourceOverlay.end());
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    };
    static const std::vector<uint32_t> entries = buildEntries(true, true);
    static const std::vector<uint32_t> wholeAotEntries =
        buildEntries(true, false);
    static const std::vector<uint32_t> manualEntries =
        buildEntries(false, true);
    static const std::vector<uint32_t> overlayEntries =
        buildEntries(false, false);
    if (includeWholeAot && includeManualCompiledFunctions) {
        return entries;
    }
    if (includeWholeAot) {
        return wholeAotEntries;
    }
    if (includeManualCompiledFunctions) {
        return manualEntries;
    }
    return overlayEntries;
#else
    static const std::vector<uint32_t> entries = [] {
        std::vector<uint32_t> result(kCompiledFunctionEntryPoints.begin(),
                                     kCompiledFunctionEntryPoints.end());
        const auto sourceOverlay = Oot3dSourceOverlayEntryPoints();
        result.insert(result.end(), sourceOverlay.begin(),
                      sourceOverlay.end());
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }();
    static_cast<void>(includeWholeAot);
    if (includeManualCompiledFunctions) {
        return entries;
    }
    return Oot3dSourceOverlayEntryPoints();
#endif
}

void ResetOot3dCompiledFunctionStats() noexcept {
    gStats = {};
    gWholeAotStats = {};
    ResetOot3dSourceOverlayStats();
    gExternalTimingCounter = 0U;
    if (!gExternalTargetCounts.empty()) {
        std::fill(gExternalTargetCounts.begin(), gExternalTargetCounts.end(),
                  ExternalTargetCounts{});
    }
}

Oot3dCompiledFunctionStats GetOot3dCompiledFunctionStats() noexcept {
    auto result = gStats;
    result.WholeAotCalls = gWholeAotStats.Calls;
    result.WholeAotDirectCalls = gWholeAotStats.DirectCalls;
    result.WholeAotIndirectCalls = gWholeAotStats.IndirectCalls;
    result.WholeAotResolvedIndirectCalls =
        gWholeAotStats.ResolvedIndirectCalls;
    result.WholeAotExternalCalls = gWholeAotStats.ExternalCalls;
    result.WholeAotSvcExits = gWholeAotStats.SvcExits;
    result.WholeAotBlockLimitExits = gWholeAotStats.BlockLimitExits;
    result.WholeAotMemoryFaults = gWholeAotStats.MemoryFaults;
    result.WholeAotUnsupportedExits = gWholeAotStats.UnsupportedExits;
    return result;
}

void SetOot3dCompiledFunctionProfilingEnabled(bool enabled) {
    if (!enabled) {
        gExternalTargetCounts.clear();
        gExternalTargetCounts.shrink_to_fit();
        return;
    }
    gExternalTargetCounts.assign(kProfileCodeSlots, ExternalTargetCounts{});
}

std::vector<Oot3dWholeAotExternalTarget>
GetOot3dWholeAotExternalTargets(size_t maximumTargets) {
    std::vector<Oot3dWholeAotExternalTarget> targets;
    if (maximumTargets == 0U) {
        return targets;
    }
    for (size_t slot = 0; slot < gExternalTargetCounts.size(); ++slot) {
        const auto& counts = gExternalTargetCounts[slot];
        if (counts.Calls == 0U) {
            continue;
        }
        targets.push_back({
            kProfileCodeBegin +
                static_cast<uint32_t>(slot * sizeof(uint32_t)),
            counts.Calls,
            counts.ManualCompiledCalls,
            counts.TimingSamples,
            counts.TimingSampleNanoseconds,
        });
    }
    const auto compare = [](const Oot3dWholeAotExternalTarget& left,
                            const Oot3dWholeAotExternalTarget& right) {
        return left.Calls != right.Calls ? left.Calls > right.Calls
                                         : left.Entry < right.Entry;
    };
    if (targets.size() > maximumTargets) {
        std::partial_sort(
            targets.begin(),
            targets.begin() + static_cast<std::ptrdiff_t>(maximumTargets),
            targets.end(), compare);
        targets.resize(maximumTargets);
    } else {
        std::sort(targets.begin(), targets.end(), compare);
    }
    return targets;
}

bool ExecuteOot3dCompiledFunction(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    NativeA32Memory& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    uint32_t blockBudget,
    uint32_t* blocksConsumed,
    oot3d::recomp::a32::BlockEntryCallback blockEntry,
    void* blockEntryUser,
    const uint32_t* blockEntryPcs,
    size_t blockEntryPcCount,
    bool skipFirstBlockEntry,
    bool enableManualCompiledFunctions,
    bool enableWholeAot,
    uint32_t stopPc) {
    if (result == nullptr) {
        return false;
    }
    if (blocksConsumed != nullptr) {
        *blocksConsumed = 1U;
    }
    if (ExecuteOot3dSourceOverlay(pc, state, memory, result, blockBudget,
                                  blocksConsumed)) {
        return true;
    }
    if (enableManualCompiledFunctions &&
        ExecuteOot3dManualCompiledFunction(pc, state, memory, result)) {
        return true;
    }
#if defined(OOT3D_NATIVE_GENERATED_WHOLE_AOT)
    if (!enableWholeAot) {
#if defined(OOT3D_WHOLE_AOT_PRODUCT_MODE)
        *result = {
            oot3d::recomp::a32::ExitKind::Unsupported,
            pc,
            oot3d::recomp::a32::FallbackReason::Unsupported,
            0x57414F54U,
        };
        return true;
#else
        return false;
#endif
    }
    const auto overlayEntries = Oot3dSourceOverlayEntryPoints();
    SourceOverlayBlockEntryContext overlayBlockContext{
        blockEntry, blockEntryUser, &memory};
    const auto blockEntrySelection = MergeSourceOverlayBlockEntries(
        blockEntryPcs, blockEntryPcCount);
    const bool routeOverlayBlocks = !overlayEntries.empty();
    return ExecuteOot3dWholeAotFunction(
        pc, state, memory, result, &gWholeAotStats,
        enableManualCompiledFunctions
            ? ExecuteWholeAotExternalCall
            : ContinueWholeAotExternalCallInGuest,
        blockBudget, blocksConsumed,
        routeOverlayBlocks ? SourceOverlayBlockEntry : blockEntry,
        routeOverlayBlocks ? static_cast<void*>(&overlayBlockContext)
                           : blockEntryUser,
        blockEntrySelection.Entries.data(),
        blockEntrySelection.Entries.size(), blockEntrySelection.Filter,
        skipFirstBlockEntry, stopPc);
#else
    static_cast<void>(enableWholeAot);
    return false;
#endif
}

bool ExecuteOot3dCompiledFunction(
    uint32_t pc,
    oot3d::recomp::a32::GuestState& state,
    oot3d::recomp::a32::MemoryBus& memory,
    oot3d::recomp::a32::ExecutionResult* result,
    void*) {
    auto* nativeMemory = dynamic_cast<NativeA32Memory*>(&memory);
    return nativeMemory != nullptr &&
           ExecuteOot3dCompiledFunction(pc, state, *nativeMemory, result);
}

} // namespace Oot3dNativeGame
