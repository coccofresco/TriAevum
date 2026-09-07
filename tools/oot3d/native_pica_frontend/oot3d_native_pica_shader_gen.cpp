#include "oot3d_native_pica_shader_gen.h"

#include "nihstro/shader_bytecode.h"
#include "video_core/shader/generator/glsl_shader_decompiler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <vector>

namespace Oot3dNativeGame {
namespace {

// Preserve the PICA multiplication semantics for NaN/zero combinations. This
// is part of the translated program identity because it changes generated
// shader code, not a runtime presentation preference.
constexpr bool kAccuratePicaMultiplication = true;

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

uint64_t HashWords(uint64_t hash, const uint32_t* words, size_t count) {
    constexpr uint64_t kFnvPrime = 1099511628211ULL;
    for (size_t index = 0; index < count; ++index) {
        uint32_t word = words[index];
        for (size_t byte = 0; byte < sizeof(word); ++byte) {
            hash ^= static_cast<uint8_t>(word >> (byte * 8U));
            hash *= kFnvPrime;
        }
    }
    return hash;
}

std::string InputRegisterName(uint32_t index) {
    return "pica_input" + std::to_string(index);
}

std::string OutputRegisterName(uint32_t index) {
    return "pica_output" + std::to_string(index);
}

size_t StreamOffset(std::ostringstream& stream) {
    return static_cast<size_t>(stream.tellp());
}

bool PreviousScopedIdentifier(std::string_view token) {
    return token == "uniforms" || token == "conditional_code" ||
           token == "address_registers" ||
           token == "get_offset_register" || token == "exec_shader" ||
           token == "sanitize_mul" || token == "pica_position" ||
           token == "pica_ndc_z" || token == "pica_raw_primary_color" ||
           token.starts_with("pica_output") ||
           token.starts_with("reg_tmp") || token.starts_with("sub_");
}

std::string PreviousScopedSource(std::string_view source) {
    std::string result;
    result.reserve(source.size() + source.size() / 8U);
    for (size_t index = 0; index < source.size();) {
        const unsigned char value =
            static_cast<unsigned char>(source[index]);
        if (std::isalpha(value) || source[index] == '_') {
            const size_t start = index++;
            while (index < source.size()) {
                const unsigned char next =
                    static_cast<unsigned char>(source[index]);
                if (!std::isalnum(next) && source[index] != '_') {
                    break;
                }
                ++index;
            }
            const std::string_view token =
                source.substr(start, index - start);
            result.append(token);
            if (PreviousScopedIdentifier(token)) {
                result.append("_previous");
            }
        } else {
            result.push_back(source[index++]);
        }
    }
    return result;
}

struct SemanticSource {
    bool Present = false;
    uint8_t Register = 0;
    uint8_t Component = 0;
};

struct ArithmeticInstructionView {
    nihstro::OpCode::Id Opcode = nihstro::OpCode::Id::NOP;
    nihstro::Instruction Instruction{0U};
    nihstro::SwizzlePattern Swizzle{0U};
    nihstro::DestRegister Destination{0U};
    nihstro::SourceRegister Source1{0U};
    nihstro::SourceRegister Source2{0U};
    bool HasSource2 = false;
    uint8_t AddressRegister = 0U;
};

std::optional<ArithmeticInstructionView> DecodeArithmeticInstruction(
    const Oot3dPicaDrawPacket& packet, size_t offset) {
    if (offset >= packet.VertexShader.ProgramWordCount) {
        return std::nullopt;
    }
    const nihstro::Instruction instruction{
        packet.VertexShader.Program[offset]};
    const auto opcode = instruction.opcode.Value();
    const auto& info = opcode.GetInfo();
    if (info.type != nihstro::OpCode::Type::Arithmetic) {
        return std::nullopt;
    }
    const size_t swizzleOffset =
        instruction.common.operand_desc_id.Value();
    if (swizzleOffset >= packet.VertexShader.SwizzleWordCount) {
        return std::nullopt;
    }
    const bool inverted =
        (info.subtype & nihstro::OpCode::Info::SrcInversed) != 0U;
    return ArithmeticInstructionView{
        opcode.EffectiveOpCode(),
        instruction,
        nihstro::SwizzlePattern{
            packet.VertexShader.Swizzles[swizzleOffset]},
        instruction.common.dest.Value(),
        instruction.common.GetSrc1(inverted),
        instruction.common.GetSrc2(inverted),
        (info.subtype & nihstro::OpCode::Info::Src2) != 0U,
        static_cast<uint8_t>(
            instruction.common.address_register_index.Value())};
}

uint8_t SourceComponent(const ArithmeticInstructionView& instruction,
                        uint8_t source, uint8_t component) {
    const auto selector =
        source == 1U
            ? instruction.Swizzle.GetSelectorSrc1(component)
            : instruction.Swizzle.GetSelectorSrc2(component);
    return static_cast<uint8_t>(selector);
}

bool SourceNegated(const ArithmeticInstructionView& instruction,
                   uint8_t source) {
    return source == 1U ? instruction.Swizzle.negate_src1.Value()
                        : instruction.Swizzle.negate_src2.Value();
}

bool SourceIdentity(const ArithmeticInstructionView& instruction,
                    uint8_t source) {
    for (uint8_t component = 0U; component < 4U; ++component) {
        if (SourceComponent(instruction, source, component) != component) {
            return false;
        }
    }
    return true;
}

bool IsRegister(const nihstro::SourceRegister& source,
                nihstro::RegisterType type, uint8_t index) {
    return source.GetRegisterType() == type &&
           source.GetIndex() == static_cast<int>(index);
}

bool IsRegister(const nihstro::DestRegister& destination,
                nihstro::RegisterType type, uint8_t index) {
    return destination.GetRegisterType() == type &&
           destination.GetIndex() == static_cast<int>(index);
}

struct CoordinateFeed {
    uint8_t Temporary = 0xffU;
    uint8_t Component = 0xffU;
    bool Present = false;
};

std::optional<std::array<CoordinateFeed, 2U>> ResolveCoordinateOutputFeed(
    const Oot3dPicaDrawPacket& packet,
    const std::array<SemanticSource, 24U>& semantics) {
    if (!semantics[12U].Present || !semantics[13U].Present) {
        return std::nullopt;
    }
    std::array<CoordinateFeed, 2U> result{};
    for (size_t offset = 0U;
         offset < packet.VertexShader.ProgramWordCount; ++offset) {
        const auto instruction =
            DecodeArithmeticInstruction(packet, offset);
        if (!instruction.has_value() ||
            instruction->Opcode != nihstro::OpCode::Id::MOV ||
            instruction->AddressRegister != 0U ||
            SourceNegated(*instruction, 1U) ||
            instruction->Source1.GetRegisterType() !=
                nihstro::RegisterType::Temporary) {
            continue;
        }
        for (uint8_t coordinateComponent = 0U;
             coordinateComponent < result.size(); ++coordinateComponent) {
            const auto& semantic = semantics[12U + coordinateComponent];
            if (!IsRegister(instruction->Destination,
                            nihstro::RegisterType::Output,
                            semantic.Register) ||
                !instruction->Swizzle.DestComponentEnabled(
                    semantic.Component)) {
                continue;
            }
            const CoordinateFeed candidate{
                static_cast<uint8_t>(instruction->Source1.GetIndex()),
                SourceComponent(*instruction, 1U, semantic.Component),
                true};
            if (result[coordinateComponent].Present &&
                (result[coordinateComponent].Temporary !=
                     candidate.Temporary ||
                 result[coordinateComponent].Component !=
                     candidate.Component)) {
                return std::nullopt;
            }
            result[coordinateComponent] = candidate;
        }
    }
    if (!result[0].Present || !result[1].Present ||
        result[0].Temporary != result[1].Temporary ||
        result[0].Component == result[1].Component) {
        return std::nullopt;
    }
    return result;
}

struct MatrixFeed {
    uint8_t Uniform = 0xffU;
    uint8_t WorkingTemporary = 0xffU;
    bool Present = false;
};

std::optional<MatrixFeed> DecodeMatrixFeed(
    const Oot3dPicaDrawPacket& packet, size_t offset,
    const CoordinateFeed& coordinate) {
    const auto instruction = DecodeArithmeticInstruction(packet, offset);
    if (!instruction.has_value() ||
        instruction->Opcode != nihstro::OpCode::Id::DP4 ||
        instruction->AddressRegister != 0U ||
        !IsRegister(instruction->Destination,
                    nihstro::RegisterType::Temporary,
                    coordinate.Temporary) ||
        !instruction->Swizzle.DestComponentEnabled(
            coordinate.Component) ||
        SourceNegated(*instruction, 1U) ||
        SourceNegated(*instruction, 2U) ||
        !SourceIdentity(*instruction, 1U) ||
        !SourceIdentity(*instruction, 2U)) {
        return std::nullopt;
    }
    const auto source1Type = instruction->Source1.GetRegisterType();
    const auto source2Type = instruction->Source2.GetRegisterType();
    const bool source1Uniform =
        source1Type == nihstro::RegisterType::FloatUniform;
    const bool source2Uniform =
        source2Type == nihstro::RegisterType::FloatUniform;
    const bool source1Temporary =
        source1Type == nihstro::RegisterType::Temporary;
    const bool source2Temporary =
        source2Type == nihstro::RegisterType::Temporary;
    if (!((source1Uniform && source2Temporary) ||
          (source2Uniform && source1Temporary))) {
        return std::nullopt;
    }
    const auto& uniform =
        source1Uniform ? instruction->Source1 : instruction->Source2;
    const auto& temporary =
        source1Temporary ? instruction->Source1 : instruction->Source2;
    if (uniform.GetIndex() < 0 || uniform.GetIndex() >= 96 ||
        temporary.GetIndex() < 0 || temporary.GetIndex() >= 16) {
        return std::nullopt;
    }
    return MatrixFeed{
        static_cast<uint8_t>(uniform.GetIndex()),
        static_cast<uint8_t>(temporary.GetIndex()), true};
}

std::optional<MatrixFeed> ResolveMatrixFeed(
    const Oot3dPicaDrawPacket& packet,
    const CoordinateFeed& coordinate) {
    MatrixFeed result{};
    for (size_t offset = 0U;
         offset < packet.VertexShader.ProgramWordCount; ++offset) {
        const auto candidate =
            DecodeMatrixFeed(packet, offset, coordinate);
        if (!candidate.has_value()) {
            continue;
        }
        if (result.Present &&
            (result.Uniform != candidate->Uniform ||
             result.WorkingTemporary !=
                 candidate->WorkingTemporary)) {
            return std::nullopt;
        }
        result = *candidate;
    }
    return result.Present ? std::optional<MatrixFeed>{result}
                          : std::nullopt;
}

struct UniformMove {
    uint8_t DestinationTemporary = 0xffU;
    uint8_t Uniform = 0xffU;
    uint8_t Component = 0xffU;
};

std::optional<UniformMove> DecodeUniformBroadcastMove(
    const Oot3dPicaDrawPacket& packet, size_t offset,
    uint8_t firstDestinationComponent,
    uint8_t secondDestinationComponent) {
    const auto instruction = DecodeArithmeticInstruction(packet, offset);
    if (!instruction.has_value() ||
        instruction->Opcode != nihstro::OpCode::Id::MOV ||
        instruction->AddressRegister != 0U ||
        SourceNegated(*instruction, 1U) ||
        instruction->Destination.GetRegisterType() !=
            nihstro::RegisterType::Temporary ||
        instruction->Source1.GetRegisterType() !=
            nihstro::RegisterType::FloatUniform ||
        !instruction->Swizzle.DestComponentEnabled(
            firstDestinationComponent) ||
        !instruction->Swizzle.DestComponentEnabled(
            secondDestinationComponent)) {
        return std::nullopt;
    }
    const uint8_t first = SourceComponent(
        *instruction, 1U, firstDestinationComponent);
    const uint8_t second = SourceComponent(
        *instruction, 1U, secondDestinationComponent);
    if (first != second || instruction->Source1.GetIndex() < 0 ||
        instruction->Source1.GetIndex() >= 96 ||
        instruction->Destination.GetIndex() < 0 ||
        instruction->Destination.GetIndex() >= 16) {
        return std::nullopt;
    }
    return UniformMove{
        static_cast<uint8_t>(instruction->Destination.GetIndex()),
        static_cast<uint8_t>(instruction->Source1.GetIndex()), first};
}

struct UniformComparison {
    uint8_t Uniform = 0xffU;
    std::array<uint8_t, 2U> Components{0xffU, 0xffU};
};

std::optional<UniformComparison> DecodeEqualUniformComparison(
    const Oot3dPicaDrawPacket& packet, size_t offset,
    uint8_t temporary) {
    const auto instruction = DecodeArithmeticInstruction(packet, offset);
    if (!instruction.has_value() ||
        instruction->Opcode != nihstro::OpCode::Id::CMP ||
        instruction->AddressRegister != 0U ||
        SourceNegated(*instruction, 1U) ||
        SourceNegated(*instruction, 2U) ||
        instruction->Instruction.common.compare_op.x.Value() !=
            nihstro::Instruction::Common::CompareOpType::Equal ||
        instruction->Instruction.common.compare_op.y.Value() !=
            nihstro::Instruction::Common::CompareOpType::Equal) {
        return std::nullopt;
    }
    const auto source1Type = instruction->Source1.GetRegisterType();
    const auto source2Type = instruction->Source2.GetRegisterType();
    const bool source1Uniform =
        source1Type == nihstro::RegisterType::FloatUniform;
    const bool source2Uniform =
        source2Type == nihstro::RegisterType::FloatUniform;
    const bool source1Temporary =
        source1Type == nihstro::RegisterType::Temporary &&
        instruction->Source1.GetIndex() == temporary;
    const bool source2Temporary =
        source2Type == nihstro::RegisterType::Temporary &&
        instruction->Source2.GetIndex() == temporary;
    if (!((source1Uniform && source2Temporary) ||
          (source2Uniform && source1Temporary))) {
        return std::nullopt;
    }
    const uint8_t uniformSource = source1Uniform ? 1U : 2U;
    const auto& uniform =
        source1Uniform ? instruction->Source1 : instruction->Source2;
    if (uniform.GetIndex() < 0 || uniform.GetIndex() >= 96) {
        return std::nullopt;
    }
    return UniformComparison{
        static_cast<uint8_t>(uniform.GetIndex()),
        {SourceComponent(*instruction, uniformSource, 0U),
         SourceComponent(*instruction, uniformSource, 1U)}};
}

bool IsConditionalBranch(const Oot3dPicaDrawPacket& packet,
                         size_t offset, bool referenceX,
                         bool referenceY) {
    if (offset >= packet.VertexShader.ProgramWordCount) {
        return false;
    }
    const nihstro::Instruction instruction{
        packet.VertexShader.Program[offset]};
    return instruction.opcode.Value().EffectiveOpCode() ==
               nihstro::OpCode::Id::IFC &&
           instruction.flow_control.op.Value() ==
               nihstro::Instruction::FlowControlType::And &&
           instruction.flow_control.refx.Value() == referenceX &&
           instruction.flow_control.refy.Value() == referenceY;
}

std::optional<uint8_t> FindEnclosingBooleanUniform(
    const Oot3dPicaDrawPacket& packet, size_t enclosedOffset,
    size_t beginOffset = 0U) {
    std::optional<uint8_t> result;
    for (size_t offset = beginOffset; offset < enclosedOffset; ++offset) {
        const nihstro::Instruction instruction{
            packet.VertexShader.Program[offset]};
        if (instruction.opcode.Value().EffectiveOpCode() !=
                nihstro::OpCode::Id::IFU ||
            instruction.flow_control.dest_offset.Value() <=
                enclosedOffset) {
            continue;
        }
        const uint32_t booleanUniform =
            instruction.flow_control.bool_uniform_id.Value();
        if (booleanUniform < 16U) {
            result = static_cast<uint8_t>(booleanUniform);
        }
    }
    return result;
}

struct CoordinateSourceRoute {
    Oot3d::Renderer::PicaVertexTextureCoordinateSourceLayout Layout;
    uint8_t HomogeneousUniform = 0xffU;
    uint8_t HomogeneousComponent = 0xffU;
};

std::optional<CoordinateSourceRoute> DecodeCoordinateSourceRoute(
    const Oot3dPicaDrawPacket& packet, size_t beginOffset,
    size_t endOffset, uint8_t workingTemporary) {
    for (size_t offset = beginOffset; offset < endOffset; ++offset) {
        const auto instruction =
            DecodeArithmeticInstruction(packet, offset);
        if (!instruction.has_value() ||
            instruction->Opcode != nihstro::OpCode::Id::MUL ||
            instruction->AddressRegister != 0U ||
            !IsRegister(instruction->Destination,
                        nihstro::RegisterType::Temporary,
                        workingTemporary) ||
            !instruction->Swizzle.DestComponentEnabled(0U) ||
            !instruction->Swizzle.DestComponentEnabled(1U) ||
            SourceNegated(*instruction, 1U) ||
            SourceNegated(*instruction, 2U)) {
            continue;
        }
        const auto source1Type = instruction->Source1.GetRegisterType();
        const auto source2Type = instruction->Source2.GetRegisterType();
        const bool source1Uniform =
            source1Type == nihstro::RegisterType::FloatUniform;
        const bool source2Uniform =
            source2Type == nihstro::RegisterType::FloatUniform;
        const bool source1Input =
            source1Type == nihstro::RegisterType::Input;
        const bool source2Input =
            source2Type == nihstro::RegisterType::Input;
        if (!((source1Uniform && source2Input) ||
              (source2Uniform && source1Input))) {
            continue;
        }
        const uint8_t uniformSource = source1Uniform ? 1U : 2U;
        const uint8_t inputSource = source1Input ? 1U : 2U;
        const auto& uniform =
            source1Uniform ? instruction->Source1 : instruction->Source2;
        const auto& input =
            source1Input ? instruction->Source1 : instruction->Source2;
        const uint8_t scaleComponent0 =
            SourceComponent(*instruction, uniformSource, 0U);
        const uint8_t scaleComponent1 =
            SourceComponent(*instruction, uniformSource, 1U);
        if (scaleComponent0 != scaleComponent1 ||
            SourceComponent(*instruction, inputSource, 0U) != 0U ||
            SourceComponent(*instruction, inputSource, 1U) != 1U ||
            uniform.GetIndex() < 0 || uniform.GetIndex() >= 96 ||
            input.GetIndex() < 0 || input.GetIndex() >= 16) {
            continue;
        }
        const auto enabled = FindEnclosingBooleanUniform(
            packet, offset, beginOffset);
        const auto homogeneous = DecodeUniformBroadcastMove(
            packet, offset + 1U, 2U, 3U);
        if (!enabled.has_value() || !homogeneous.has_value() ||
            homogeneous->DestinationTemporary != workingTemporary) {
            continue;
        }
        return CoordinateSourceRoute{
            {static_cast<uint8_t>(input.GetIndex()),
             static_cast<uint8_t>(uniform.GetIndex()),
             scaleComponent0, *enabled},
            homogeneous->Uniform, homogeneous->Component};
    }
    return std::nullopt;
}

std::optional<Oot3d::Renderer::PicaVertexTextureCoordinateLayout>
DecodeCmbTextureCoordinate0Layout(
    const Oot3dPicaDrawPacket& packet,
    const std::array<SemanticSource, 24U>& semantics) {
    const auto outputFeed =
        ResolveCoordinateOutputFeed(packet, semantics);
    if (!outputFeed.has_value()) {
        return std::nullopt;
    }
    const auto matrixU = ResolveMatrixFeed(packet, (*outputFeed)[0]);
    const auto matrixV = ResolveMatrixFeed(packet, (*outputFeed)[1]);
    if (!matrixU.has_value() || !matrixV.has_value() ||
        matrixU->WorkingTemporary != matrixV->WorkingTemporary) {
        return std::nullopt;
    }

    for (size_t matrixOffset = 5U;
         matrixOffset + 1U < packet.VertexShader.ProgramWordCount;
         ++matrixOffset) {
        const auto directU = DecodeMatrixFeed(
            packet, matrixOffset, (*outputFeed)[0]);
        const auto directV = DecodeMatrixFeed(
            packet, matrixOffset + 1U, (*outputFeed)[1]);
        if (!directU.has_value() || !directV.has_value() ||
            directU->Uniform != matrixU->Uniform ||
            directV->Uniform != matrixV->Uniform ||
            directU->WorkingTemporary !=
                matrixU->WorkingTemporary ||
            directV->WorkingTemporary !=
                matrixV->WorkingTemporary) {
            continue;
        }

        const auto selector = DecodeUniformBroadcastMove(
            packet, matrixOffset - 2U, 0U, 1U);
        const nihstro::Instruction resolverCall{
            packet.VertexShader.Program[matrixOffset - 1U]};
        const auto mode = DecodeUniformBroadcastMove(
            packet, matrixOffset - 5U, 0U, 1U);
        const auto modeComparison = DecodeEqualUniformComparison(
            packet, matrixOffset - 4U,
            mode.has_value() ? mode->DestinationTemporary : 0xffU);
        if (!selector.has_value() || !mode.has_value() ||
            !modeComparison.has_value() ||
            resolverCall.opcode.Value().EffectiveOpCode() !=
                nihstro::OpCode::Id::CALL ||
            !IsConditionalBranch(packet, matrixOffset - 3U, false,
                                 false)) {
            continue;
        }
        const auto coordinateEnable = FindEnclosingBooleanUniform(
            packet, matrixOffset - 5U);
        if (!coordinateEnable.has_value()) {
            continue;
        }

        const size_t resolverBegin =
            resolverCall.flow_control.dest_offset.Value();
        const size_t resolverEnd =
            resolverBegin +
            resolverCall.flow_control.num_instructions.Value();
        if (resolverEnd > packet.VertexShader.ProgramWordCount ||
            resolverBegin + 3U >= resolverEnd) {
            continue;
        }
        const auto sourceComparison = DecodeEqualUniformComparison(
            packet, resolverBegin,
            selector->DestinationTemporary);
        if (!sourceComparison.has_value() ||
            !IsConditionalBranch(packet, resolverBegin + 2U, true,
                                 false)) {
            continue;
        }
        const nihstro::Instruction firstBranch{
            packet.VertexShader.Program[resolverBegin + 2U]};
        const size_t secondBranchOffset =
            firstBranch.flow_control.dest_offset.Value();
        if (secondBranchOffset >= resolverEnd ||
            !IsConditionalBranch(packet, secondBranchOffset, false,
                                 true)) {
            continue;
        }
        const nihstro::Instruction secondBranch{
            packet.VertexShader.Program[secondBranchOffset]};
        const size_t thirdRouteOffset =
            secondBranch.flow_control.dest_offset.Value();
        const size_t thirdRouteEnd =
            thirdRouteOffset +
            secondBranch.flow_control.num_instructions.Value();
        if (thirdRouteOffset >= resolverEnd ||
            thirdRouteEnd > resolverEnd) {
            continue;
        }

        const std::array<std::optional<CoordinateSourceRoute>, 3U>
            routes{
                DecodeCoordinateSourceRoute(
                    packet, resolverBegin + 3U,
                    secondBranchOffset,
                    matrixU->WorkingTemporary),
                DecodeCoordinateSourceRoute(
                    packet, secondBranchOffset + 1U,
                    thirdRouteOffset,
                    matrixU->WorkingTemporary),
                DecodeCoordinateSourceRoute(
                    packet, thirdRouteOffset, thirdRouteEnd,
                    matrixU->WorkingTemporary)};
        if (!routes[0].has_value() || !routes[1].has_value() ||
            !routes[2].has_value() ||
            routes[0]->HomogeneousUniform !=
                routes[1]->HomogeneousUniform ||
            routes[0]->HomogeneousUniform !=
                routes[2]->HomogeneousUniform ||
            routes[0]->HomogeneousComponent !=
                routes[1]->HomogeneousComponent ||
            routes[0]->HomogeneousComponent !=
                routes[2]->HomogeneousComponent) {
            continue;
        }

        Oot3d::Renderer::PicaVertexTextureCoordinateLayout layout;
        layout.Operation = Oot3d::Renderer::
            PicaVertexTextureCoordinateOperation::CmbAffine;
        layout.EnableBooleanUniform = *coordinateEnable;
        layout.SourceSelectorUniform = selector->Uniform;
        layout.SourceSelectorComponent = selector->Component;
        layout.SourceSelectorConstantUniform =
            sourceComparison->Uniform;
        layout.SourceSelectorConstantComponents =
            sourceComparison->Components;
        layout.CoordinateModeUniform = mode->Uniform;
        layout.CoordinateModeComponent = mode->Component;
        layout.CoordinateModeConstantUniform =
            modeComparison->Uniform;
        layout.CoordinateModeConstantComponents =
            modeComparison->Components;
        layout.MatrixRowUUniform = matrixU->Uniform;
        layout.MatrixRowVUniform = matrixV->Uniform;
        layout.HomogeneousUniform =
            routes[0]->HomogeneousUniform;
        layout.HomogeneousComponent =
            routes[0]->HomogeneousComponent;
        layout.SourceCount = 3U;
        for (size_t route = 0U; route < routes.size(); ++route) {
            layout.Sources[route] = routes[route]->Layout;
        }
        if (layout.Available()) {
            return layout;
        }
    }
    return std::nullopt;
}

bool SameRegister(const nihstro::SourceRegister& left, const nihstro::SourceRegister& right) {
    return left.GetRegisterType() == right.GetRegisterType() && left.GetIndex() == right.GetIndex();
}

bool SameRegister(const nihstro::DestRegister& left, const nihstro::DestRegister& right) {
    return left.GetRegisterType() == right.GetRegisterType() && left.GetIndex() == right.GetIndex();
}

struct DotMatrixChain {
    size_t Offset = 0U;
    uint8_t FirstUniform = 0xffU;
    uint8_t AddressRegister = 0U;
    nihstro::DestRegister Destination{ 0U };
    nihstro::SourceRegister Value{ 0U };
};

std::optional<uint8_t> SingleDestinationComponent(const ArithmeticInstructionView& instruction) {
    std::optional<uint8_t> result;
    for (uint8_t component = 0U; component < 4U; ++component) {
        if (!instruction.Swizzle.DestComponentEnabled(component)) {
            continue;
        }
        if (result.has_value()) {
            return std::nullopt;
        }
        result = component;
    }
    return result;
}

std::optional<DotMatrixChain> DecodeDotMatrixChain(const Oot3dPicaDrawPacket& packet, size_t offset,
                                                   nihstro::OpCode::Id opcode, uint8_t rowCount) {
    DotMatrixChain result;
    result.Offset = offset;
    for (uint8_t row = 0U; row < rowCount; ++row) {
        const auto instruction = DecodeArithmeticInstruction(packet, offset + row);
        if (!instruction.has_value() || instruction->Opcode != opcode || SourceNegated(*instruction, 1U) ||
            SourceNegated(*instruction, 2U) || !SourceIdentity(*instruction, 1U) || !SourceIdentity(*instruction, 2U)) {
            return std::nullopt;
        }
        const auto destinationComponent = SingleDestinationComponent(*instruction);
        if (!destinationComponent.has_value() || *destinationComponent != row) {
            return std::nullopt;
        }
        const bool source1Uniform = instruction->Source1.GetRegisterType() == nihstro::RegisterType::FloatUniform;
        const bool source2Uniform = instruction->Source2.GetRegisterType() == nihstro::RegisterType::FloatUniform;
        if (source1Uniform == source2Uniform) {
            return std::nullopt;
        }
        const auto& uniform = source1Uniform ? instruction->Source1 : instruction->Source2;
        const auto& value = source1Uniform ? instruction->Source2 : instruction->Source1;
        if (uniform.GetIndex() < 0 || uniform.GetIndex() >= 96) {
            return std::nullopt;
        }
        if (row == 0U) {
            result.FirstUniform = static_cast<uint8_t>(uniform.GetIndex());
            result.AddressRegister = instruction->AddressRegister;
            result.Destination = instruction->Destination;
            result.Value = value;
        } else if (uniform.GetIndex() != static_cast<int>(result.FirstUniform + row) ||
                   instruction->AddressRegister != result.AddressRegister ||
                   !SameRegister(instruction->Destination, result.Destination) || !SameRegister(value, result.Value)) {
            return std::nullopt;
        }
    }
    return result;
}

template <typename Predicate>
std::optional<DotMatrixChain> FindConsistentDotMatrixChain(const Oot3dPicaDrawPacket& packet,
                                                           nihstro::OpCode::Id opcode, uint8_t rowCount,
                                                           Predicate&& predicate) {
    std::optional<DotMatrixChain> result;
    for (size_t offset = 0U; offset + rowCount <= packet.VertexShader.ProgramWordCount; ++offset) {
        const auto candidate = DecodeDotMatrixChain(packet, offset, opcode, rowCount);
        if (!candidate.has_value() || !predicate(*candidate)) {
            continue;
        }
        if (result.has_value() &&
            (result->FirstUniform != candidate->FirstUniform || result->AddressRegister != candidate->AddressRegister ||
             !SameRegister(result->Destination, candidate->Destination) ||
             !SameRegister(result->Value, candidate->Value))) {
            return std::nullopt;
        }
        if (!result.has_value()) {
            result = candidate;
        }
    }
    return result;
}

std::optional<uint8_t> DecodeMovaTemporary(const Oot3dPicaDrawPacket& packet, size_t offset) {
    const auto instruction = DecodeArithmeticInstruction(packet, offset);
    if (!instruction.has_value() || instruction->Opcode != nihstro::OpCode::Id::MOVA ||
        instruction->Source1.GetRegisterType() != nihstro::RegisterType::Temporary || SourceNegated(*instruction, 1U) ||
        instruction->Source1.GetIndex() < 0 || instruction->Source1.GetIndex() >= 16) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(instruction->Source1.GetIndex());
}

struct InputMultiplyFeed {
    uint8_t InputRegister = 0xffU;
    uint8_t InputComponent = 0xffU;
    uint8_t DestinationMask = 0U;
};

std::optional<InputMultiplyFeed> DecodeInputMultiplyFeed(const Oot3dPicaDrawPacket& packet, size_t offset,
                                                         uint8_t destinationTemporary) {
    const auto instruction = DecodeArithmeticInstruction(packet, offset);
    if (!instruction.has_value() || instruction->Opcode != nihstro::OpCode::Id::MUL ||
        instruction->AddressRegister != 0U ||
        !IsRegister(instruction->Destination, nihstro::RegisterType::Temporary, destinationTemporary) ||
        SourceNegated(*instruction, 1U) || SourceNegated(*instruction, 2U)) {
        return std::nullopt;
    }
    const bool source1Input = instruction->Source1.GetRegisterType() == nihstro::RegisterType::Input;
    const bool source2Input = instruction->Source2.GetRegisterType() == nihstro::RegisterType::Input;
    const bool source1Uniform = instruction->Source1.GetRegisterType() == nihstro::RegisterType::FloatUniform;
    const bool source2Uniform = instruction->Source2.GetRegisterType() == nihstro::RegisterType::FloatUniform;
    if (!((source1Input && source2Uniform) || (source2Input && source1Uniform))) {
        return std::nullopt;
    }
    const uint8_t inputSource = source1Input ? 1U : 2U;
    const auto& input = source1Input ? instruction->Source1 : instruction->Source2;
    uint8_t destinationMask = 0U;
    std::optional<uint8_t> inputComponent;
    for (uint8_t component = 0U; component < 4U; ++component) {
        if (!instruction->Swizzle.DestComponentEnabled(component)) {
            continue;
        }
        destinationMask |= static_cast<uint8_t>(1U << component);
        const uint8_t sourceComponent = SourceComponent(*instruction, inputSource, component);
        if (inputComponent.has_value() && *inputComponent != sourceComponent) {
            return std::nullopt;
        }
        inputComponent = sourceComponent;
    }
    if (!inputComponent.has_value() || input.GetIndex() < 0 || input.GetIndex() >= 16) {
        return std::nullopt;
    }
    return InputMultiplyFeed{ static_cast<uint8_t>(input.GetIndex()), *inputComponent, destinationMask };
}

struct UniformBranch {
    size_t Offset = 0U;
    size_t ThenBegin = 0U;
    size_t ElseBegin = 0U;
    size_t End = 0U;
    uint8_t BooleanUniform = 0xffU;
};

std::optional<UniformBranch> DecodeUniformBranch(const Oot3dPicaDrawPacket& packet, size_t offset) {
    if (offset >= packet.VertexShader.ProgramWordCount) {
        return std::nullopt;
    }
    const nihstro::Instruction instruction{ packet.VertexShader.Program[offset] };
    if (instruction.opcode.Value().EffectiveOpCode() != nihstro::OpCode::Id::IFU) {
        return std::nullopt;
    }
    const size_t elseBegin = instruction.flow_control.dest_offset.Value();
    const size_t end = elseBegin + instruction.flow_control.num_instructions.Value();
    const uint32_t booleanUniform = instruction.flow_control.bool_uniform_id.Value();
    if (offset + 1U > elseBegin || elseBegin > end || end > packet.VertexShader.ProgramWordCount ||
        booleanUniform >= 16U) {
        return std::nullopt;
    }
    return UniformBranch{ offset, offset + 1U, elseBegin, end, static_cast<uint8_t>(booleanUniform) };
}

bool Contains(size_t begin, size_t end, size_t offset) {
    return begin <= offset && offset < end;
}

struct CmbTransformPrograms {
    std::optional<Oot3d::Renderer::PicaVertexTransformLayout> Transform;
    std::optional<Oot3d::Renderer::PicaVertexSkeletonLayout> Skeleton;
};

CmbTransformPrograms DecodeCmbTransformPrograms(const Oot3dPicaDrawPacket& packet,
                                                const std::array<SemanticSource, 24U>& semantics) {
    CmbTransformPrograms result;
    if (!semantics[0U].Present || !semantics[1U].Present || !semantics[2U].Present || !semantics[3U].Present) {
        return result;
    }
    const auto projection =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP4, 4U, [&](const DotMatrixChain& chain) {
            if (chain.AddressRegister != 0U || chain.Destination.GetRegisterType() != nihstro::RegisterType::Output) {
                return false;
            }
            for (uint8_t component = 0U; component < 4U; ++component) {
                if (semantics[component].Register != chain.Destination.GetIndex() ||
                    semantics[component].Component != component) {
                    return false;
                }
            }
            return true;
        });
    if (!projection.has_value() || projection->Value.GetRegisterType() != nihstro::RegisterType::Temporary) {
        return result;
    }
    const auto view =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP4, 3U, [&](const DotMatrixChain& chain) {
            return chain.AddressRegister == 0U &&
                   chain.Destination.GetRegisterType() == nihstro::RegisterType::Temporary &&
                   chain.Destination.GetIndex() == projection->Value.GetIndex() &&
                   chain.Value.GetRegisterType() == nihstro::RegisterType::Temporary;
        });
    if (!view.has_value()) {
        return result;
    }
    const auto model =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP4, 3U, [&](const DotMatrixChain& chain) {
            return chain.AddressRegister == 0U &&
                   chain.Destination.GetRegisterType() == nihstro::RegisterType::Temporary &&
                   chain.Destination.GetIndex() == view->Value.GetIndex() &&
                   chain.Value.GetRegisterType() == nihstro::RegisterType::Input;
        });
    if (!model.has_value()) {
        return result;
    }
    const auto normal =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP3, 3U, [&](const DotMatrixChain& chain) {
            return chain.AddressRegister == 0U && chain.FirstUniform == model->FirstUniform &&
                   chain.Value.GetRegisterType() == nihstro::RegisterType::Input;
        });
    if (!normal.has_value()) {
        return result;
    }

    Oot3d::Renderer::PicaVertexTransformLayout transform;
    transform.Operation = Oot3d::Renderer::PicaVertexTransformOperation::ModelViewProjection3x4;
    transform.ProjectionFirstUniform = projection->FirstUniform;
    transform.ProjectionRowCount = 4U;
    transform.ViewFirstUniform = view->FirstUniform;
    transform.ViewRowCount = 3U;
    transform.ModelFirstUniform = model->FirstUniform;
    transform.ModelRowCount = 3U;
    transform.PositionInputRegister = static_cast<uint8_t>(model->Value.GetIndex());
    transform.NormalInputRegister = static_cast<uint8_t>(normal->Value.GetIndex());
    if (!transform.Available()) {
        return result;
    }
    result.Transform = transform;

    const auto indexedPosition =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP4, 3U, [&](const DotMatrixChain& chain) {
            return chain.AddressRegister != 0U && chain.FirstUniform == model->FirstUniform &&
                   chain.Value.GetRegisterType() == nihstro::RegisterType::Temporary;
        });
    if (!indexedPosition.has_value() || indexedPosition->Offset == 0U) {
        return result;
    }
    const auto indexedNormal =
        FindConsistentDotMatrixChain(packet, nihstro::OpCode::Id::DP3, 3U, [&](const DotMatrixChain& chain) {
            return chain.AddressRegister != 0U && chain.FirstUniform == model->FirstUniform &&
                   chain.Value.GetRegisterType() == nihstro::RegisterType::Temporary;
        });
    const auto addressTemporary = DecodeMovaTemporary(packet, indexedPosition->Offset - 1U);
    if (!indexedNormal.has_value() || !addressTemporary.has_value()) {
        return result;
    }
    const size_t paletteRoutine = indexedPosition->Offset - 1U;
    std::vector<size_t> paletteCalls;
    std::array<bool, 4U> influenceComponents{};
    std::optional<uint8_t> boneIndexInput;
    std::optional<uint8_t> boneWeightInput;
    for (size_t offset = 0U; offset < packet.VertexShader.ProgramWordCount; ++offset) {
        const nihstro::Instruction instruction{ packet.VertexShader.Program[offset] };
        if (instruction.opcode.Value().EffectiveOpCode() != nihstro::OpCode::Id::CALL ||
            instruction.flow_control.dest_offset.Value() != paletteRoutine) {
            continue;
        }
        paletteCalls.push_back(offset);
        const size_t begin = offset > 3U ? offset - 3U : 0U;
        for (size_t feedOffset = begin; feedOffset < offset; ++feedOffset) {
            const auto feed = DecodeInputMultiplyFeed(packet, feedOffset, *addressTemporary);
            if (!feed.has_value()) {
                continue;
            }
            if ((feed->DestinationMask & 0x3U) != 0U) {
                if (boneIndexInput.has_value() && *boneIndexInput != feed->InputRegister) {
                    return result;
                }
                boneIndexInput = feed->InputRegister;
                if (feed->InputComponent < influenceComponents.size()) {
                    influenceComponents[feed->InputComponent] = true;
                }
            }
            if ((feed->DestinationMask & (1U << 3U)) != 0U) {
                if (boneWeightInput.has_value() && *boneWeightInput != feed->InputRegister) {
                    return result;
                }
                boneWeightInput = feed->InputRegister;
            }
        }
    }
    if (paletteCalls.empty() || !boneIndexInput.has_value() || !boneWeightInput.has_value()) {
        return result;
    }

    std::optional<uint8_t> skeletonEnable;
    std::optional<uint8_t> multipleInfluenceEnable;
    uint8_t maximumInfluences =
        static_cast<uint8_t>(std::count(influenceComponents.begin(), influenceComponents.end(), true));
    for (size_t offset = 0U; offset < packet.VertexShader.ProgramWordCount; ++offset) {
        const auto branch = DecodeUniformBranch(packet, offset);
        if (!branch.has_value()) {
            continue;
        }
        const auto countCalls = [&](size_t begin, size_t end) {
            return static_cast<uint8_t>(std::count_if(paletteCalls.begin(), paletteCalls.end(),
                                                      [&](size_t call) { return Contains(begin, end, call); }));
        };
        const uint8_t thenCalls = countCalls(branch->ThenBegin, branch->ElseBegin);
        const uint8_t elseCalls = countCalls(branch->ElseBegin, branch->End);
        const bool modelInThen = Contains(branch->ThenBegin, branch->ElseBegin, model->Offset);
        const bool modelInElse = Contains(branch->ElseBegin, branch->End, model->Offset);
        if ((modelInThen && elseCalls != 0U) || (modelInElse && thenCalls != 0U)) {
            skeletonEnable = branch->BooleanUniform;
        }
        if ((thenCalls > 1U && elseCalls == 1U) || (elseCalls > 1U && thenCalls == 1U)) {
            multipleInfluenceEnable = branch->BooleanUniform;
            maximumInfluences = std::max(maximumInfluences, std::max(thenCalls, elseCalls));
        }
    }
    if (!skeletonEnable.has_value() || maximumInfluences == 0U || maximumInfluences > 4U) {
        return result;
    }

    Oot3d::Renderer::PicaVertexSkeletonLayout skeleton;
    skeleton.Operation = Oot3d::Renderer::PicaVertexSkeletonOperation::MatrixPalette3x4;
    skeleton.EnableBooleanUniform = *skeletonEnable;
    skeleton.MultipleInfluenceBooleanUniform =
        multipleInfluenceEnable.value_or(Oot3d::Renderer::PicaVertexSkeletonLayout::Unavailable);
    skeleton.PaletteFirstUniform = model->FirstUniform;
    skeleton.RowsPerMatrix = 3U;
    skeleton.BoneIndexInputRegister = *boneIndexInput;
    skeleton.BoneWeightInputRegister = *boneWeightInput;
    skeleton.MaximumInfluences = maximumInfluences;
    if (skeleton.Available()) {
        result.Skeleton = skeleton;
    }
    return result;
}

} // namespace

uint64_t ComputeOot3dPicaVertexShaderStateKey(
    const Oot3dPicaDrawPacket& packet) {
    constexpr uint64_t kFnvOffset = 1469598103934665603ULL;
    uint64_t key = HashWords(kFnvOffset, packet.VertexShader.Program.data(),
                             packet.VertexShader.ProgramWordCount);
    key = HashWords(key, packet.VertexShader.Swizzles.data(),
                    packet.VertexShader.SwizzleWordCount);
    const std::array<uint32_t, 11> interfaceWords{
        packet.Registers[0x4FU], packet.Registers[0x50U],
        packet.Registers[0x51U], packet.Registers[0x52U],
        packet.Registers[0x53U], packet.Registers[0x54U],
        packet.Registers[0x55U], packet.Registers[0x56U],
        packet.Registers[0x2BAU], packet.Registers[0x2BDU],
        packet.Registers[0x229U]};
    key = HashWords(key, interfaceWords.data(), interfaceWords.size());
    const uint32_t arithmeticSemantics =
        kAccuratePicaMultiplication ? 1U : 0U;
    return HashWords(key, &arithmeticSemantics, 1U);
}

Oot3dPicaVertexUniformState BuildOot3dPicaVertexUniformState(
    const Oot3dPicaDrawPacket& packet) {
    Oot3dPicaVertexUniformState uniforms;
    for (size_t index = 0;
         index < packet.VertexShader.BooleanUniforms.size(); ++index) {
        if (packet.VertexShader.BooleanUniforms[index]) {
            uniforms.BooleanMask |= 1U << index;
        }
    }
    for (size_t row = 0; row < uniforms.Integers.size(); ++row) {
        for (size_t component = 0;
             component < uniforms.Integers[row].size(); ++component) {
            uniforms.Integers[row][component] =
                packet.VertexShader.IntegerUniforms[row][component];
        }
    }
    uniforms.Floats = packet.VertexShader.FloatUniforms;
    return uniforms;
}

bool GenerateOot3dPicaVertexShader(
    const Oot3dPicaDrawPacket& packet,
    const Oot3dPicaDecodedDrawState& state,
    Oot3dPicaGeneratedVertexShader& shader, std::string* error) {
    shader = {};
    if (packet.VertexShader.ProgramWordCount == 0U ||
        state.ShaderInterface.VertexMainOffset >=
            packet.VertexShader.Program.size()) {
        SetError(error, "PICA vertex program or entrypoint is invalid");
        return false;
    }
    const std::string body =
        Pica::Shader::Generator::GLSL::DecompileProgram(
            packet.VertexShader.Program.Values(),
            packet.VertexShader.Swizzles.Values(),
            state.ShaderInterface.VertexMainOffset, InputRegisterName,
            OutputRegisterName, kAccuratePicaMultiplication);
    if (body.empty()) {
        SetError(error, "PICA vertex program could not be decompiled");
        return false;
    }

    using Oot3d::Renderer::PicaVertexShaderHook;
    using Oot3d::Renderer::PicaVertexShaderSemantic;
    auto temporalProgram =
        std::make_shared<Oot3dPicaTemporalVertexProgram>();
    auto& hooks = temporalProgram->Hooks;
    hooks.SchemaVersion =
        Oot3d::Renderer::kPicaShaderHookSchemaVersion;
    hooks.Semantics =
        PicaVertexShaderSemantic::PicaRegisterState |
        PicaVertexShaderSemantic::VertexUniformState |
        PicaVertexShaderSemantic::ClipPositionOutput;
    const auto setHook = [&](PicaVertexShaderHook hook, size_t offset) {
        hooks.Offsets[static_cast<size_t>(hook)] = offset;
    };

    std::array<SemanticSource, 24> semantics{};
    size_t packedOutput = 0;
    for (size_t outputRegister = 0; outputRegister < 16U;
         ++outputRegister) {
        if ((state.ShaderInterface.OutputMask & (1U << outputRegister)) == 0U) {
            continue;
        }
        if (packedOutput < 7U) {
            const uint32_t map = packet.Registers[0x50U + packedOutput];
            for (size_t component = 0; component < 4U; ++component) {
                const uint8_t semantic =
                    static_cast<uint8_t>((map >> (component * 8U)) & 0x1FU);
                if (semantic < semantics.size()) {
                    semantics[semantic] =
                        {true, static_cast<uint8_t>(outputRegister),
                         static_cast<uint8_t>(component)};
                }
            }
        }
        ++packedOutput;
    }
    if (semantics[18U].Present && semantics[19U].Present &&
        semantics[20U].Present) {
        hooks.Semantics |=
            PicaVertexShaderSemantic::ViewPositionOutput;
    }
    const auto textureCoordinate0 =
        DecodeCmbTextureCoordinate0Layout(packet, semantics);
    if (textureCoordinate0.has_value()) {
        hooks.TextureCoordinates[0U] = *textureCoordinate0;
        hooks.Semantics |=
            PicaVertexShaderSemantic::TextureCoordinateProgram;
    }
    const auto transformPrograms = DecodeCmbTransformPrograms(packet, semantics);
    if (transformPrograms.Transform.has_value()) {
        hooks.Transform = *transformPrograms.Transform;
        hooks.Semantics |= PicaVertexShaderSemantic::TransformProgram;
    }
    if (transformPrograms.Skeleton.has_value()) {
        hooks.Skeleton = *transformPrograms.Skeleton;
        hooks.Semantics |= PicaVertexShaderSemantic::SkeletonProgram;
    }

    std::ostringstream source;
    source << "#version 450\n";
    for (size_t index = 0; index < 16U; ++index) {
        source << "layout(location=" << index << ") in vec4 "
               << InputRegisterName(static_cast<uint32_t>(index)) << ";\n";
    }
    source << "layout(location=0) out vec4 pica_primary_color;\n"
              "layout(location=1) out vec2 pica_texcoord0;\n"
              "layout(location=2) out vec2 pica_texcoord1;\n"
              "layout(location=3) out vec2 pica_texcoord2;\n"
              "layout(location=4) out float pica_texcoord0_w;\n"
              "layout(location=5) out vec4 pica_normquat;\n"
              "layout(location=6) out vec3 pica_view;\n"
              "layout(set=0,binding=0,std140) uniform PicaVertexUniforms {\n"
              "    uint b;\n"
              "    int flip_viewport;\n"
              "    uvec4 i[4];\n"
              "    vec4 f[96];\n"
              "} uniforms;\n";
    const size_t registerStateBegin = StreamOffset(source);
    setHook(PicaVertexShaderHook::GlobalDeclarations,
            registerStateBegin);
    setHook(PicaVertexShaderHook::RegisterStateBegin,
            registerStateBegin);
    for (size_t index = 0; index < 16U; ++index) {
        source << "vec4 " << OutputRegisterName(static_cast<uint32_t>(index))
               << " = vec4(0.0);\n";
    }
    source << body << '\n';
    setHook(PicaVertexShaderHook::RegisterStateEnd,
            StreamOffset(source));

    const auto component = [&](size_t semantic, const char* fallback) {
        if (!semantics[semantic].Present) {
            return std::string(fallback);
        }
        return OutputRegisterName(semantics[semantic].Register) + "." +
               "xyzw"[semantics[semantic].Component];
    };
    source << "void main() {";
    setHook(PicaVertexShaderHook::MainBodyBegin,
            StreamOffset(source));
    source << "\n"
              "    exec_shader();\n"
           << "    vec4 pica_position = vec4(" << component(0, "1.0") << ", "
           << component(1, "1.0") << ", " << component(2, "1.0") << ", "
           << component(3, "1.0") << ");\n"
              "    float pica_ndc_z = pica_position.z / pica_position.w;\n"
              "    if (pica_ndc_z > 0.0 && pica_ndc_z < 0.000001) "
              "pica_position.z = 0.0;\n"
              "    if (pica_ndc_z < -1.0 && pica_ndc_z > -1.00001) "
              "pica_position.z = -pica_position.w;\n"
              "    if (uniforms.flip_viewport != 0) pica_position.y = "
              "-pica_position.y;\n"
              "    gl_Position = vec4(pica_position.x, pica_position.y, "
              "-pica_position.z, pica_position.w);\n"
           << "    pica_normquat = vec4(" << component(4, "1.0") << ", "
           << component(5, "1.0") << ", " << component(6, "1.0") << ", "
           << component(7, "1.0") << ");\n"
           << "    vec4 pica_raw_primary_color = vec4(" << component(8, "1.0") << ", "
           << component(9, "1.0") << ", " << component(10, "1.0") << ", "
           << component(11, "1.0") << ");\n"
              "    pica_primary_color = min(abs(pica_raw_primary_color), "
              "vec4(1.0));\n"
           << "    pica_texcoord0 = vec2(" << component(12, "1.0") << ", "
           << component(13, "1.0") << ");\n"
           << "    pica_texcoord1 = vec2(" << component(14, "1.0") << ", "
           << component(15, "1.0") << ");\n"
           << "    pica_texcoord0_w = " << component(16, "1.0") << ";\n"
           << "    pica_view = vec3(" << component(18, "1.0") << ", "
           << component(19, "1.0") << ", " << component(20, "1.0") << ");\n"
           << "    pica_texcoord2 = vec2(" << component(22, "1.0") << ", "
           << component(23, "1.0") << ");\n";
    setHook(PicaVertexShaderHook::MainBodyEnd,
            StreamOffset(source));
    source << "}\n";

    shader.StateKey = ComputeOot3dPicaVertexShaderStateKey(packet);
    shader.Source = source.str();
    shader.SourceIdentity =
        Oot3d::Renderer::IdentifyPicaShaderSource(shader.Source);
    hooks.SourceSize = shader.Source.size();
    const std::string_view sourceView = shader.Source;
    if (!hooks.ValidFor(sourceView)) {
        SetError(error, "generated PICA vertex hook layout is invalid");
        return false;
    }
    const size_t registerStateEnd = hooks.Offset(
        PicaVertexShaderHook::RegisterStateEnd);
    const size_t mainBodyBegin = hooks.Offset(
        PicaVertexShaderHook::MainBodyBegin);
    const size_t mainBodyEnd = hooks.Offset(
        PicaVertexShaderHook::MainBodyEnd);
    temporalProgram->PreviousRegisterState = PreviousScopedSource(
        sourceView.substr(registerStateBegin,
                          registerStateEnd - registerStateBegin));
    temporalProgram->PreviousMainBody = PreviousScopedSource(
        sourceView.substr(mainBodyBegin,
                          mainBodyEnd - mainBodyBegin));
    shader.TemporalProgram = std::move(temporalProgram);
    shader.Uniforms = BuildOot3dPicaVertexUniformState(packet);
    return true;
}

} // namespace Oot3dNativeGame
