#include "oot3d_native_pica_frontend.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

constexpr uint32_t kHardwareRegisterPhysicalBase = 0x1EB00000U;
constexpr uint32_t kHardwareRegisterOffsetLimit = 0x00420000U;
constexpr size_t kMaximumWriteBytes = 0x80U;
constexpr size_t kMaximumCommandListBytes = 0x01000000U;
constexpr size_t kPicaRegisterCount = 0x300U;
constexpr uint16_t kPicaTriggerDraw = 0x22EU;
constexpr uint16_t kPicaTriggerDrawIndexed = 0x22FU;
constexpr uint16_t kPicaIrqRequest = 0x010U;
constexpr uint16_t kPicaIrqCompare = 0x020U;
constexpr uint16_t kPicaIrqMask = 0x030U;
constexpr uint16_t kPicaIrqAutoStop = 0x034U;
constexpr uint16_t kPicaGeometryShaderBase = 0x280U;
constexpr uint16_t kPicaVertexShaderBase = 0x2B0U;
constexpr uint16_t kPicaPipelineUseGeometryShader = 0x229U;
constexpr uint16_t kPicaDefaultAttributeIndex = 0x232U;
constexpr uint16_t kPicaDefaultAttributeDataBegin = 0x233U;
constexpr uint16_t kPicaDefaultAttributeDataEnd = 0x235U;
constexpr uint16_t kPicaGeometryShaderExclusiveConfiguration = 0x244U;
constexpr uint16_t kPicaFogLutOffset = 0x0E6U;
constexpr uint16_t kPicaFogLutDataBegin = 0x0E8U;
constexpr uint16_t kPicaFogLutDataEnd = 0x0EFU;
constexpr uint16_t kPicaProcTexLutConfig = 0x0AFU;
constexpr uint16_t kPicaProcTexLutDataBegin = 0x0B0U;
constexpr uint16_t kPicaProcTexLutDataEnd = 0x0B7U;
constexpr uint16_t kPicaLightingLutConfig = 0x1C5U;
constexpr uint16_t kPicaLightingLutDataBegin = 0x1C8U;
constexpr uint16_t kPicaLightingLutDataEnd = 0x1CFU;
constexpr std::array<uint32_t, 16> kExpandedParameterMasks{
    0x00000000U, 0x000000FFU, 0x0000FF00U, 0x0000FFFFU,
    0x00FF0000U, 0x00FF00FFU, 0x00FFFF00U, 0x00FFFFFFU,
    0xFF000000U, 0xFF0000FFU, 0xFF00FF00U, 0xFF00FFFFU,
    0xFFFF0000U, 0xFFFF00FFU, 0xFFFFFF00U, 0xFFFFFFFFU,
};

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

float DecodeFloat24(uint32_t raw) {
    constexpr uint32_t kMantissaBits = 16U;
    constexpr uint32_t kExponentBits = 7U;
    constexpr int32_t kExponentBiasAdjustment = 64;
    const uint32_t mantissa = raw & 0xFFFFU;
    uint32_t exponent = (raw >> kMantissaBits) & 0x7FU;
    const uint32_t sign = (raw >> 23U) << 31U;
    uint32_t ieee = sign;
    if ((raw & 0x7FFFFFU) != 0U) {
        exponent = exponent == ((1U << kExponentBits) - 1U)
                       ? 255U
                       : exponent + kExponentBiasAdjustment;
        ieee |= mantissa << 7U;
        ieee |= exponent << 23U;
    }
    return std::bit_cast<float>(ieee);
}

std::array<float, 4> DecodePackedFloat24(
    const std::array<uint32_t, 4>& words) {
    return {
        DecodeFloat24(words[2] & 0xFFFFFFU),
        DecodeFloat24(((words[1] & 0xFFFFU) << 8U) |
                      ((words[2] >> 24U) & 0xFFU)),
        DecodeFloat24(((words[0] & 0xFFU) << 16U) |
                      ((words[1] >> 16U) & 0xFFFFU)),
        DecodeFloat24(words[0] >> 8U),
    };
}

std::array<float, 4> DecodePackedFloat32(
    const std::array<uint32_t, 4>& words) {
    return {
        std::bit_cast<float>(words[3]), std::bit_cast<float>(words[2]),
        std::bit_cast<float>(words[1]), std::bit_cast<float>(words[0]),
    };
}

nlohmann::json EncodeShaderState(const Oot3dPicaShaderState& shader) {
    return {
        {"program", shader.Program.Values()},
        {"swizzles", shader.Swizzles.Values()},
        {"float_uniforms", shader.FloatUniforms},
        {"integer_uniforms", shader.IntegerUniforms},
        {"boolean_uniforms", shader.BooleanUniforms},
        {"program_word_count", shader.ProgramWordCount},
        {"swizzle_word_count", shader.SwizzleWordCount},
    };
}

bool DecodeShaderState(const nlohmann::json& encoded,
                       Oot3dPicaShaderState& shader) {
    shader.Program = encoded.at("program").get<
        std::array<uint32_t, Oot3dPicaShaderState::MaximumProgramWords>>();
    shader.Swizzles = encoded.at("swizzles").get<
        std::array<uint32_t, Oot3dPicaShaderState::MaximumSwizzleWords>>();
    shader.FloatUniforms = encoded.at("float_uniforms")
                               .get<decltype(shader.FloatUniforms)>();
    shader.IntegerUniforms = encoded.at("integer_uniforms")
                                 .get<decltype(shader.IntegerUniforms)>();
    shader.BooleanUniforms = encoded.at("boolean_uniforms")
                                 .get<decltype(shader.BooleanUniforms)>();
    shader.ProgramWordCount =
        encoded.at("program_word_count").get<size_t>();
    shader.SwizzleWordCount =
        encoded.at("swizzle_word_count").get<size_t>();
    return shader.ProgramWordCount <= shader.Program.size() &&
           shader.SwizzleWordCount <= shader.Swizzles.size();
}

} // namespace

uint64_t ComputeOot3dPicaLightingLutContentHash(
    const Oot3dPicaLightingLutState& state) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    uint64_t hash = kFnvOffset;
    for (const uint32_t word : state.PackedEntries) {
        for (size_t byte = 0; byte < sizeof(word); ++byte) {
            hash ^= static_cast<uint8_t>(word >> (byte * 8U));
            hash *= kFnvPrime;
        }
    }
    return hash == 0U ? 1U : hash;
}

Oot3dNativePicaFrontend::Oot3dNativePicaFrontend(
    Oot3dPicaPacketSink* packetSink)
    : mPacketSink(packetSink) {
    mPicaRegisters[kPicaIrqAutoStop] = 1U;
    mPicaRegisters[kPicaIrqMask] = 0xFFFFFFF0U;
    mPicaRegisters[kPicaIrqCompare] = 0x12345678U;
}

void Oot3dNativePicaFrontend::SetPacketSink(
    Oot3dPicaPacketSink* packetSink) {
    mPacketSink = packetSink;
}

void Oot3dNativePicaFrontend::SetDiagnosticHistoryEnabled(bool enabled) {
    mDiagnosticHistoryEnabled = enabled;
    if (!enabled) {
        mPendingGspCommands.clear();
        mPendingRegisterWrites.clear();
        mPendingDrawPackets.clear();
    }
}

void Oot3dNativePicaFrontend::SetCommandListCompositionDomain(
    Oot3dPicaCompositionDomain domain) noexcept {
    mCommandListCompositionDomain = domain;
}

bool Oot3dNativePicaFrontend::SetNextCommandListCompositionSpans(
    uint32_t commandListAddress, uint32_t commandListSize,
    std::span<const Oot3dPicaCommandListCompositionSpan> spans,
    std::string* error) {
    const uint64_t commandListEnd =
        static_cast<uint64_t>(commandListAddress) + commandListSize;
    if ((commandListAddress & 3U) != 0U || (commandListSize & 3U) != 0U ||
        commandListEnd > std::numeric_limits<uint32_t>::max()) {
        SetError(error, "composition span command list is invalid");
        return false;
    }

    std::vector<Oot3dPicaCommandListCompositionSpan> validated(
        spans.begin(), spans.end());
    std::sort(validated.begin(), validated.end(),
              [](const auto& left, const auto& right) {
                  return left.BeginAddress < right.BeginAddress;
              });
    uint32_t previousEnd = commandListAddress;
    for (const auto& span : validated) {
        if ((span.BeginAddress & 3U) != 0U ||
            (span.EndAddress & 3U) != 0U ||
            span.BeginAddress < commandListAddress ||
            span.EndAddress <= span.BeginAddress ||
            span.EndAddress > commandListEnd ||
            span.BeginAddress < previousEnd ||
            span.Attribution.Layer > Oot3dPicaCompositionLayer::Ui ||
            span.Attribution.Provenance >
                Oot3dPicaCompositionProvenance::NativeUiLifecycle) {
            SetError(error, "composition span range or attribution is invalid");
            return false;
        }
        previousEnd = span.EndAddress;
    }

    mCompositionSpanCommandListAddress = commandListAddress;
    mCompositionSpanCommandListSize = commandListSize;
    mNextCommandListCompositionSpans = std::move(validated);
    return true;
}

void Oot3dNativePicaFrontend::ApplyShaderStageRegisterSideEffects(
    uint16_t registerId, uint32_t writeValue, uint16_t stageBase,
    Oot3dPicaShaderState& shader,
    PackedAttributeQueue& uniformQueue, bool mirrorToGeometryShader) {
    constexpr uint16_t kBooleanUniformOffset = 0x00U;
    constexpr uint16_t kIntegerUniformBeginOffset = 0x01U;
    constexpr uint16_t kIntegerUniformEndOffset = 0x04U;
    constexpr uint16_t kFloatUniformSetupOffset = 0x10U;
    constexpr uint16_t kFloatUniformDataBeginOffset = 0x11U;
    constexpr uint16_t kFloatUniformDataEndOffset = 0x18U;
    constexpr uint16_t kProgramOffsetOffset = 0x1BU;
    constexpr uint16_t kProgramDataBeginOffset = 0x1CU;
    constexpr uint16_t kProgramDataEndOffset = 0x23U;
    constexpr uint16_t kSwizzleOffsetOffset = 0x25U;
    constexpr uint16_t kSwizzleDataBeginOffset = 0x26U;
    constexpr uint16_t kSwizzleDataEndOffset = 0x2DU;

    const uint16_t offset = registerId - stageBase;
    const auto mirror = [&](const auto& apply) {
        if (mirrorToGeometryShader) {
            apply(mGeometryShader);
        }
    };
    if (offset == kBooleanUniformOffset) {
        for (size_t index = 0; index < shader.BooleanUniforms.size(); ++index) {
            shader.BooleanUniforms[index] = (writeValue & (1U << index)) != 0U;
        }
        mirror([&](Oot3dPicaShaderState& target) {
            target.BooleanUniforms = shader.BooleanUniforms;
        });
        return;
    }
    if (offset >= kIntegerUniformBeginOffset &&
        offset <= kIntegerUniformEndOffset) {
        const size_t index = offset - kIntegerUniformBeginOffset;
        shader.IntegerUniforms[index] = {
            static_cast<uint8_t>(writeValue),
            static_cast<uint8_t>(writeValue >> 8U),
            static_cast<uint8_t>(writeValue >> 16U),
            static_cast<uint8_t>(writeValue >> 24U),
        };
        mirror([&](Oot3dPicaShaderState& target) {
            target.IntegerUniforms[index] = shader.IntegerUniforms[index];
        });
        return;
    }
    if (offset >= kFloatUniformDataBeginOffset &&
        offset <= kFloatUniformDataEndOffset) {
        const uint32_t setup = mPicaRegisters[stageBase +
                                               kFloatUniformSetupOffset];
        const bool float32 = (setup & 0x80000000U) != 0U;
        uniformQueue.Words[uniformQueue.Count++] = writeValue;
        const size_t requiredWords = float32 ? 4U : 3U;
        if (uniformQueue.Count == requiredWords) {
            const size_t index = setup & 0x7FU;
            if (index < shader.FloatUniforms.size()) {
                shader.FloatUniforms[index] =
                    float32 ? DecodePackedFloat32(uniformQueue.Words)
                            : DecodePackedFloat24(uniformQueue.Words);
                mirror([&](Oot3dPicaShaderState& target) {
                    target.FloatUniforms[index] = shader.FloatUniforms[index];
                });
            }
            uniformQueue.Count = 0;
            mPicaRegisters[stageBase + kFloatUniformSetupOffset] =
                (setup & 0x80000000U) |
                static_cast<uint32_t>((index + 1U) & 0x7FU);
        }
        return;
    }
    if (offset >= kProgramDataBeginOffset &&
        offset <= kProgramDataEndOffset) {
        const size_t index = mPicaRegisters[stageBase + kProgramOffsetOffset];
        if (index < shader.Program.size()) {
            shader.Program[index] = writeValue;
            shader.ProgramWordCount = std::max(shader.ProgramWordCount, index + 1U);
            mirror([&](Oot3dPicaShaderState& target) {
                target.Program[index] = writeValue;
                target.ProgramWordCount =
                    std::max(target.ProgramWordCount, index + 1U);
            });
            mPicaRegisters[stageBase + kProgramOffsetOffset] =
                static_cast<uint32_t>(index + 1U);
        }
        return;
    }
    if (offset >= kSwizzleDataBeginOffset &&
        offset <= kSwizzleDataEndOffset) {
        const size_t index = mPicaRegisters[stageBase + kSwizzleOffsetOffset];
        if (index < shader.Swizzles.size()) {
            shader.Swizzles[index] = writeValue;
            shader.SwizzleWordCount =
                std::max(shader.SwizzleWordCount, index + 1U);
            mirror([&](Oot3dPicaShaderState& target) {
                target.Swizzles[index] = writeValue;
                target.SwizzleWordCount =
                    std::max(target.SwizzleWordCount, index + 1U);
            });
            mPicaRegisters[stageBase + kSwizzleOffsetOffset] =
                static_cast<uint32_t>(index + 1U);
        }
    }
}

void Oot3dNativePicaFrontend::ApplyShaderRegisterSideEffects(
    uint16_t registerId, uint32_t writeValue) {
    if (registerId >= kPicaLightingLutDataBegin &&
        registerId <= kPicaLightingLutDataEnd) {
        const uint32_t config = mPicaRegisters[kPicaLightingLutConfig];
        const size_t index = config & 0xFFU;
        const size_t table = (config >> 8U) & 0x1FU;
        if (table < Oot3dPicaLightingLutState::TableCount) {
            if (mLightingLuts.use_count() != 1) {
                mLightingLuts =
                    std::make_shared<Oot3dPicaLightingLutState>(
                        *mLightingLuts);
            }
            mLightingLuts->Entry(table, index) = writeValue;
            mLightingLuts->ContentHash = 0U;
            mLightingLuts->ContentHashAvailable = false;
        }
        mPicaRegisters[kPicaLightingLutConfig] =
            (config & 0xFFFFFF00U) |
            static_cast<uint32_t>((index + 1U) & 0xFFU);
        return;
    }
    if (registerId >= kPicaProcTexLutDataBegin &&
        registerId <= kPicaProcTexLutDataEnd) {
        const uint32_t config = mPicaRegisters[kPicaProcTexLutConfig];
        const uint32_t index = config & 0xFFU;
        const uint32_t table = (config >> 8U) & 0xFU;
        const auto writeLut = [index, writeValue](auto& lut) {
            lut[index % lut.size()] = writeValue;
        };
        switch (table) {
        case 0U:
            writeLut(mProcTexLuts.Noise);
            break;
        case 2U:
            writeLut(mProcTexLuts.ColorMap);
            break;
        case 3U:
            writeLut(mProcTexLuts.AlphaMap);
            break;
        case 4U:
            writeLut(mProcTexLuts.Color);
            break;
        case 5U:
            writeLut(mProcTexLuts.ColorDifference);
            break;
        default:
            break;
        }
        mPicaRegisters[kPicaProcTexLutConfig] =
            (config & 0xFFFFFF00U) | ((index + 1U) & 0xFFU);
        return;
    }
    if (registerId >= kPicaFogLutDataBegin &&
        registerId <= kPicaFogLutDataEnd) {
        const uint32_t offset =
            mPicaRegisters[kPicaFogLutOffset] & 0xFFFFU;
        mFogLut[offset % mFogLut.size()] = writeValue;
        mPicaRegisters[kPicaFogLutOffset] =
            (mPicaRegisters[kPicaFogLutOffset] & 0xFFFF0000U) |
            ((offset + 1U) & 0xFFFFU);
        return;
    }
    if (registerId == kPicaDefaultAttributeIndex) {
        mDefaultAttributeQueue.Count = 0;
        return;
    }
    if (registerId >= kPicaDefaultAttributeDataBegin &&
        registerId <= kPicaDefaultAttributeDataEnd) {
        mDefaultAttributeQueue.Words[mDefaultAttributeQueue.Count++] =
            writeValue;
        if (mDefaultAttributeQueue.Count == 3U) {
            const size_t index = mPicaRegisters[kPicaDefaultAttributeIndex];
            if (index < 15U) {
                mDefaultAttributes[index] =
                    DecodePackedFloat24(mDefaultAttributeQueue.Words);
                mPicaRegisters[kPicaDefaultAttributeIndex] =
                    static_cast<uint32_t>(index + 1U);
            }
            mDefaultAttributeQueue.Count = 0;
        }
        return;
    }
    const bool mirrorVertexState =
        (mPicaRegisters[kPicaGeometryShaderExclusiveConfiguration] & 1U) == 0U &&
        (mPicaRegisters[kPicaPipelineUseGeometryShader] & 3U) == 0U;
    if (registerId >= kPicaGeometryShaderBase &&
        registerId < kPicaGeometryShaderBase + 0x30U) {
        ApplyShaderStageRegisterSideEffects(
            registerId, writeValue, kPicaGeometryShaderBase, mGeometryShader,
            mGeometryUniformQueue, false);
    } else if (registerId >= kPicaVertexShaderBase &&
               registerId < kPicaVertexShaderBase + 0x30U) {
        ApplyShaderStageRegisterSideEffects(
            registerId, writeValue, kPicaVertexShaderBase, mVertexShader,
            mVertexUniformQueue, mirrorVertexState);
    }
}

bool Oot3dNativePicaFrontend::WriteHardwareRegisters(
    uint32_t baseOffset, std::span<const uint32_t> values,
    std::string* error) {
    if ((baseOffset & 3U) != 0 || baseOffset >= kHardwareRegisterOffsetLimit) {
        SetError(error, "PICA hardware register offset is invalid");
        return false;
    }
    if (values.size_bytes() > kMaximumWriteBytes) {
        SetError(error, "PICA hardware register write exceeds 0x80 bytes");
        return false;
    }
    if (values.size() >
        (std::numeric_limits<uint32_t>::max() - baseOffset) / 4U) {
        SetError(error, "PICA hardware register write range overflows");
        return false;
    }

    std::vector<Oot3dPicaHardwareRegisterWrite> writes;
    writes.reserve(values.size());
    for (size_t index = 0; index < values.size(); ++index) {
        const uint32_t offset =
            baseOffset + static_cast<uint32_t>(index * sizeof(uint32_t));
        writes.push_back({offset, kHardwareRegisterPhysicalBase + offset,
                          values[index]});
    }
    if (mPacketSink != nullptr) {
        for (const auto& write : writes) {
            if (!mPacketSink->SubmitHardwareRegisterWrite(write, error)) {
                return false;
            }
        }
    }
    for (const auto& write : writes) {
        mHardwareRegisters[write.Offset] = write.Value;
        mPendingWrites.push_back(write);
    }
    mHardwareWriteCount += writes.size();
    return true;
}

bool Oot3dNativePicaFrontend::WriteHardwareRegistersWithMask(
    uint32_t baseOffset, std::span<const uint32_t> values,
    std::span<const uint32_t> masks, std::string* error) {
    if (values.size() != masks.size()) {
        SetError(error, "PICA hardware register value/mask sizes differ");
        return false;
    }
    std::vector<uint32_t> merged;
    merged.reserve(values.size());
    for (size_t index = 0; index < values.size(); ++index) {
        const uint32_t offset =
            baseOffset + static_cast<uint32_t>(index * sizeof(uint32_t));
        const uint32_t previous = ReadHardwareRegister(offset).value_or(0U);
        merged.push_back((previous & ~masks[index]) |
                         (values[index] & masks[index]));
    }
    return WriteHardwareRegisters(baseOffset, merged, error);
}

bool Oot3dNativePicaFrontend::SubmitGspCommand(
    const Oot3dGspCommandPacket& command,
    std::span<const uint32_t> commandListWords, std::string* error,
    bool* displayTransferDeferredToGpu, bool* memoryFillDeferredToGpu,
    bool* displayTransferCpuCopySuppressed) {
    if (displayTransferDeferredToGpu != nullptr) {
        *displayTransferDeferredToGpu = false;
    }
    if (memoryFillDeferredToGpu != nullptr) {
        *memoryFillDeferredToGpu = false;
    }
    if (displayTransferCpuCopySuppressed != nullptr) {
        *displayTransferCpuCopySuppressed = false;
    }
    const uint32_t commandId = command.Control & 0xFFU;
    if (commandId > static_cast<uint32_t>(Oot3dGspCommandId::CacheFlush)) {
        SetError(error, "GSP command ID is unsupported");
        return false;
    }
    if (commandId !=
        static_cast<uint32_t>(Oot3dGspCommandId::SubmitCommandList)) {
        if (!commandListWords.empty()) {
            SetError(error, "non-submit GSP command has command-list data");
            return false;
        }
        if (commandId ==
                static_cast<uint32_t>(Oot3dGspCommandId::DisplayTransfer) &&
            mPacketSink != nullptr &&
            !mPacketSink->SubmitDisplayTransfer(
                {command.Parameters[0], command.Parameters[1],
                 command.Parameters[2], command.Parameters[3],
                 command.Parameters[4]},
                displayTransferDeferredToGpu, error,
                displayTransferCpuCopySuppressed)) {
            return false;
        }
        if (commandId ==
                static_cast<uint32_t>(Oot3dGspCommandId::MemoryFill) &&
            mPacketSink != nullptr) {
            Oot3dPicaMemoryFillCommand fill;
            fill.Fills[0] = {command.Parameters[0], command.Parameters[2],
                             command.Parameters[1],
                             static_cast<uint16_t>(command.Parameters[6])};
            fill.Fills[1] = {
                command.Parameters[3], command.Parameters[5],
                command.Parameters[4],
                static_cast<uint16_t>(command.Parameters[6] >> 16U)};
            if (!mPacketSink->SubmitMemoryFill(
                    fill, memoryFillDeferredToGpu, error)) {
                return false;
            }
        }
        if (mDiagnosticHistoryEnabled) {
            mPendingGspCommands.push_back(command);
        }
        return true;
    }

    const uint32_t commandListAddress = command.Parameters[0];
    const uint32_t commandListSize = command.Parameters[1];
    if ((commandListSize & 3U) != 0 ||
        commandListSize > kMaximumCommandListBytes ||
        commandListWords.size_bytes() != commandListSize) {
        SetError(error, "GSP submit command-list size is invalid");
        return false;
    }

    std::vector<Oot3dPicaCommandListCompositionSpan> compositionSpans;
    if (mCompositionSpanCommandListAddress == commandListAddress &&
        mCompositionSpanCommandListSize == commandListSize) {
        compositionSpans = std::move(mNextCommandListCompositionSpans);
    }
    mCompositionSpanCommandListAddress = 0U;
    mCompositionSpanCommandListSize = 0U;
    mNextCommandListCompositionSpans.clear();

    const auto resolveComposition =
        [&](uint32_t offsetWords) -> Oot3dPicaCompositionAttribution {
        if (mCommandListCompositionDomain ==
            Oot3dPicaCompositionDomain::Ui) {
            return {Oot3dPicaCompositionLayer::Ui,
                    Oot3dPicaCompositionProvenance::NativeUiLifecycle,
                    0U, 0U};
        }
        if (mCommandListCompositionDomain !=
            Oot3dPicaCompositionDomain::Scene) {
            return {};
        }
        const uint64_t address = static_cast<uint64_t>(commandListAddress) +
                                 static_cast<uint64_t>(offsetWords) * 4U;
        const auto found = std::upper_bound(
            compositionSpans.begin(), compositionSpans.end(), address,
            [](uint64_t value,
               const Oot3dPicaCommandListCompositionSpan& span) {
                return value < span.BeginAddress;
            });
        if (found == compositionSpans.begin()) {
            return {};
        }
        const auto& candidate = *std::prev(found);
        return address < candidate.EndAddress
                   ? candidate.Attribution
                   : Oot3dPicaCompositionAttribution{};
    };

    size_t cursor = 0;
    bool stopRequested = false;
    bool drawSubmitted = false;
    while (cursor < commandListWords.size()) {
        if (stopRequested) {
            break;
        }
        if ((cursor & 1U) != 0) {
            ++cursor;
        }
        if (cursor == commandListWords.size()) {
            break;
        }
        if (cursor + 2U > commandListWords.size()) {
            SetError(error, "PICA command list ends before its header");
            return false;
        }
        const uint32_t valueOffset = static_cast<uint32_t>(cursor);
        const uint32_t value = commandListWords[cursor++];
        const uint32_t header = commandListWords[cursor++];
        const uint16_t registerId = static_cast<uint16_t>(header);
        const uint8_t parameterMask =
            static_cast<uint8_t>((header >> 16U) & 0xFU);
        const uint32_t extraDataLength = (header >> 20U) & 0xFFU;
        const bool grouped = (header >> 31U) != 0;

        const auto applyWrite = [&](uint16_t id, uint32_t writeValue,
                                    uint32_t offsetWords) -> bool {
            const bool supported = id < kPicaRegisterCount;
            uint32_t finalValue = writeValue;
            if (supported) {
                const uint32_t mask =
                    kExpandedParameterMasks[parameterMask];
                finalValue = (mPicaRegisters[id] & ~mask) |
                             (writeValue & mask);
                mPicaRegisters[id] = finalValue;
                ApplyShaderRegisterSideEffects(id, finalValue);
            }
            if (mDiagnosticHistoryEnabled) {
                mPendingRegisterWrites.push_back(
                    {commandListAddress, offsetWords, id, parameterMask,
                     supported, writeValue, finalValue});
            }
            if (id == kPicaTriggerDraw ||
                id == kPicaTriggerDrawIndexed) {
                if (!mLightingLuts->ContentHashAvailable) {
                    mLightingLuts->ContentHash =
                        ComputeOot3dPicaLightingLutContentHash(*mLightingLuts);
                    mLightingLuts->ContentHashAvailable = true;
                }
                Oot3dPicaDrawPacket draw(
                    commandListAddress, offsetWords,
                    mCommandListCompositionDomain,
                    resolveComposition(offsetWords),
                    id == kPicaTriggerDrawIndexed, mPicaRegisters,
                    mVertexShader, mGeometryShader, mFogLut, mProcTexLuts,
                    mLightingLuts, mDefaultAttributes);
                if (mPacketSink != nullptr &&
                    !mPacketSink->SubmitDrawPacket(draw, error)) {
                    return false;
                }
                drawSubmitted = true;
                if (mDiagnosticHistoryEnabled) {
                    mPendingDrawPackets.push_back(std::move(draw));
                }
            }
            if (id == kPicaIrqRequest && supported) {
                const uint32_t request = mPicaRegisters[kPicaIrqRequest];
                const uint32_t compare = mPicaRegisters[kPicaIrqCompare];
                const bool byteMatches =
                    (request & 0xFFU) == (compare & 0xFFU) ||
                    ((request >> 8U) & 0xFFU) ==
                        ((compare >> 8U) & 0xFFU) ||
                    ((request >> 16U) & 0xFFU) ==
                        ((compare >> 16U) & 0xFFU) ||
                    ((request >> 24U) & 0xFFU) ==
                        ((compare >> 24U) & 0xFFU);
                if (byteMatches) {
                    if (!drawSubmitted) {
                        mPendingInterrupts.push_back(
                            Oot3dPicaInterruptId::P3d);
                    } else if (mPacketSink != nullptr &&
                               !mPacketSink->SubmitInterruptAfterGpuWork(
                                   Oot3dPicaInterruptId::P3d, error)) {
                        return false;
                    }
                    stopRequested =
                        mPicaRegisters[kPicaIrqAutoStop] != 0U;
                }
            }
            return true;
        };
        if (!applyWrite(registerId, value, valueOffset)) {
            return false;
        }
        if (extraDataLength > commandListWords.size() - cursor) {
            SetError(error, "PICA command list extra data is truncated");
            return false;
        }
        for (uint32_t index = 0; index < extraDataLength; ++index) {
            const uint32_t extraOffset = static_cast<uint32_t>(cursor);
            const uint16_t extraRegister = static_cast<uint16_t>(
                registerId + (grouped ? index + 1U : 0U));
            if (!applyWrite(extraRegister, commandListWords[cursor++],
                            extraOffset)) {
                return false;
            }
        }
    }
    if (mDiagnosticHistoryEnabled) {
        mPendingGspCommands.push_back(command);
    }
    return true;
}

std::optional<uint32_t> Oot3dNativePicaFrontend::ReadHardwareRegister(
    uint32_t offset) const {
    const auto found = mHardwareRegisters.find(offset);
    return found == mHardwareRegisters.end()
               ? std::nullopt
               : std::optional<uint32_t>(found->second);
}

size_t Oot3dNativePicaFrontend::HardwareWriteCount() const {
    return mHardwareWriteCount;
}

std::span<const Oot3dPicaHardwareRegisterWrite>
Oot3dNativePicaFrontend::PendingWrites() const {
    return mPendingWrites;
}

std::vector<Oot3dPicaHardwareRegisterWrite>
Oot3dNativePicaFrontend::TakePendingWrites() {
    auto result = std::move(mPendingWrites);
    mPendingWrites.clear();
    return result;
}

std::optional<uint32_t> Oot3dNativePicaFrontend::ReadPicaRegister(
    uint16_t registerId) const {
    return registerId < mPicaRegisters.size()
               ? std::optional<uint32_t>(mPicaRegisters[registerId])
               : std::nullopt;
}

std::span<const Oot3dGspCommandPacket>
Oot3dNativePicaFrontend::PendingGspCommands() const {
    return mPendingGspCommands;
}

std::span<const Oot3dPicaRegisterWrite>
Oot3dNativePicaFrontend::PendingRegisterWrites() const {
    return mPendingRegisterWrites;
}

std::span<const Oot3dPicaDrawPacket>
Oot3dNativePicaFrontend::PendingDrawPackets() const {
    return mPendingDrawPackets;
}

std::vector<Oot3dPicaInterruptId>
Oot3dNativePicaFrontend::TakePendingInterrupts() {
    auto result = std::move(mPendingInterrupts);
    mPendingInterrupts.clear();
    return result;
}

nlohmann::json Oot3dNativePicaFrontend::CaptureState() const {
    std::vector<std::pair<uint32_t, uint32_t>> hardwareRegisters(
        mHardwareRegisters.begin(), mHardwareRegisters.end());
    std::sort(hardwareRegisters.begin(), hardwareRegisters.end());
    nlohmann::json interrupts = nlohmann::json::array();
    for (const auto interrupt : mPendingInterrupts) {
        interrupts.push_back(static_cast<uint32_t>(interrupt));
    }
    return {
        {"format", "oot3d_native_pica_frontend_state_v1"},
        {"hardware_registers", std::move(hardwareRegisters)},
        {"pica_registers", mPicaRegisters},
        {"vertex_shader", EncodeShaderState(mVertexShader)},
        {"geometry_shader", EncodeShaderState(mGeometryShader)},
        {"fog_lut", mFogLut},
        {"proctex_luts",
         {{"noise", mProcTexLuts.Noise},
          {"color_map", mProcTexLuts.ColorMap},
          {"alpha_map", mProcTexLuts.AlphaMap},
          {"color", mProcTexLuts.Color},
          {"color_difference", mProcTexLuts.ColorDifference}}},
        {"lighting_luts", mLightingLuts->PackedEntries},
        {"default_attributes", mDefaultAttributes},
        {"vertex_uniform_queue",
         {{"words", mVertexUniformQueue.Words},
          {"count", mVertexUniformQueue.Count}}},
        {"geometry_uniform_queue",
         {{"words", mGeometryUniformQueue.Words},
          {"count", mGeometryUniformQueue.Count}}},
        {"default_attribute_queue",
         {{"words", mDefaultAttributeQueue.Words},
          {"count", mDefaultAttributeQueue.Count}}},
        {"pending_interrupts", std::move(interrupts)},
        {"hardware_write_count", mHardwareWriteCount},
        {"diagnostic_history_enabled", mDiagnosticHistoryEnabled},
    };
}

bool Oot3dNativePicaFrontend::RestoreState(const nlohmann::json& state,
                                           std::string* error) {
    try {
        if (!state.is_object() ||
            state.value("format", std::string{}) !=
                "oot3d_native_pica_frontend_state_v1") {
            SetError(error, "native PICA frontend state format is invalid");
            return false;
        }
        std::unordered_map<uint32_t, uint32_t> hardwareRegisters;
        for (const auto& entry : state.at("hardware_registers")) {
            const auto pair = entry.get<std::pair<uint32_t, uint32_t>>();
            if (!hardwareRegisters.emplace(pair).second) {
                SetError(error,
                         "native PICA hardware register state is duplicated");
                return false;
            }
        }
        decltype(mPicaRegisters) picaRegisters =
            state.at("pica_registers").get<decltype(mPicaRegisters)>();
        Oot3dPicaShaderState vertexShader;
        Oot3dPicaShaderState geometryShader;
        if (!DecodeShaderState(state.at("vertex_shader"), vertexShader) ||
            !DecodeShaderState(state.at("geometry_shader"),
                               geometryShader)) {
            SetError(error, "native PICA shader state is invalid");
            return false;
        }
        auto lightingLuts =
            std::make_shared<Oot3dPicaLightingLutState>();
        if (const auto iterator = state.find("lighting_luts");
            iterator != state.end()) {
            lightingLuts->PackedEntries =
                iterator->get<decltype(lightingLuts->PackedEntries)>();
        }
        lightingLuts->ContentHash =
            ComputeOot3dPicaLightingLutContentHash(*lightingLuts);
        lightingLuts->ContentHashAvailable = true;
        const auto decodeQueue = [](const nlohmann::json& encoded,
                                    PackedAttributeQueue& queue) {
            queue.Words =
                encoded.at("words").get<decltype(queue.Words)>();
            queue.Count = encoded.at("count").get<size_t>();
            return queue.Count <= queue.Words.size();
        };
        PackedAttributeQueue vertexQueue;
        PackedAttributeQueue geometryQueue;
        PackedAttributeQueue defaultQueue;
        if (!decodeQueue(state.at("vertex_uniform_queue"), vertexQueue) ||
            !decodeQueue(state.at("geometry_uniform_queue"),
                         geometryQueue) ||
            !decodeQueue(state.at("default_attribute_queue"),
                         defaultQueue)) {
            SetError(error, "native PICA packed attribute queue is invalid");
            return false;
        }
        std::vector<Oot3dPicaInterruptId> interrupts;
        for (const auto& encoded : state.at("pending_interrupts")) {
            const uint32_t raw = encoded.get<uint32_t>();
            if (raw > static_cast<uint32_t>(Oot3dPicaInterruptId::Dma)) {
                SetError(error, "native PICA interrupt state is invalid");
                return false;
            }
            interrupts.push_back(static_cast<Oot3dPicaInterruptId>(raw));
        }

        mHardwareRegisters = std::move(hardwareRegisters);
        mPicaRegisters = picaRegisters;
        mVertexShader = std::move(vertexShader);
        mGeometryShader = std::move(geometryShader);
        mFogLut = state.at("fog_lut").get<decltype(mFogLut)>();
        mProcTexLuts = {};
        if (const auto iterator = state.find("proctex_luts");
            iterator != state.end()) {
            mProcTexLuts.Noise =
                iterator->at("noise").get<decltype(mProcTexLuts.Noise)>();
            mProcTexLuts.ColorMap = iterator->at("color_map")
                                           .get<decltype(mProcTexLuts.ColorMap)>();
            mProcTexLuts.AlphaMap = iterator->at("alpha_map")
                                           .get<decltype(mProcTexLuts.AlphaMap)>();
            mProcTexLuts.Color =
                iterator->at("color").get<decltype(mProcTexLuts.Color)>();
            mProcTexLuts.ColorDifference =
                iterator->at("color_difference")
                    .get<decltype(mProcTexLuts.ColorDifference)>();
        }
        mLightingLuts = std::move(lightingLuts);
        mDefaultAttributes = state.at("default_attributes")
                                 .get<decltype(mDefaultAttributes)>();
        mVertexUniformQueue = vertexQueue;
        mGeometryUniformQueue = geometryQueue;
        mDefaultAttributeQueue = defaultQueue;
        mPendingInterrupts = std::move(interrupts);
        mHardwareWriteCount =
            state.at("hardware_write_count").get<size_t>();
        mDiagnosticHistoryEnabled =
            state.at("diagnostic_history_enabled").get<bool>();
        mPendingWrites.clear();
        mPendingGspCommands.clear();
        mPendingRegisterWrites.clear();
        mPendingDrawPackets.clear();
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = std::string("native PICA frontend state decode failed: ") +
                     exception.what();
        }
        return false;
    }
}

} // namespace Oot3dNativeGame
