#include "nihstro/shader_bytecode.h"
#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_fragment_shader_gen.h"
#include "oot3d_native_pica_frontend.h"
#include "oot3d_native_pica_program_descriptor.h"
#include "oot3d_native_pica_shader_gen.h"

#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_frontend_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

uint32_t ArithmeticInstruction(nihstro::OpCode::Id opcode, nihstro::DestRegister destination,
                               nihstro::SourceRegister source1, nihstro::SourceRegister source2, uint8_t swizzle,
                               uint8_t addressRegister = 0U) {
    nihstro::Instruction instruction{ 0U };
    instruction.opcode = opcode;
    instruction.common.operand_desc_id = swizzle;
    instruction.common.src1 = source1;
    instruction.common.src2 = source2;
    instruction.common.address_register_index = addressRegister;
    instruction.common.dest = destination;
    return instruction.hex;
}

uint32_t MovaInstruction(nihstro::SourceRegister source, uint8_t swizzle) {
    nihstro::Instruction instruction{ 0U };
    instruction.opcode = nihstro::OpCode::Id::MOVA;
    instruction.common.operand_desc_id = swizzle;
    instruction.common.src1 = source;
    return instruction.hex;
}

uint32_t UniformBranchInstruction(uint8_t booleanUniform, uint16_t elseOffset, uint8_t elseInstructionCount) {
    nihstro::Instruction instruction{ 0U };
    instruction.opcode = nihstro::OpCode::Id::IFU;
    instruction.flow_control.bool_uniform_id = booleanUniform;
    instruction.flow_control.dest_offset = elseOffset;
    instruction.flow_control.num_instructions = elseInstructionCount;
    return instruction.hex;
}

uint32_t CallInstruction(uint16_t target, uint8_t instructionCount) {
    nihstro::Instruction instruction{ 0U };
    instruction.opcode = nihstro::OpCode::Id::CALL;
    instruction.flow_control.dest_offset = target;
    instruction.flow_control.num_instructions = instructionCount;
    return instruction.hex;
}

uint32_t TrivialInstruction(nihstro::OpCode::Id opcode) {
    nihstro::Instruction instruction{ 0U };
    instruction.opcode = opcode;
    return instruction.hex;
}

uint32_t Swizzle(uint8_t destinationMask, std::optional<uint8_t> source1Broadcast = std::nullopt,
                 std::optional<uint8_t> source2Broadcast = std::nullopt) {
    nihstro::SwizzlePattern pattern{ 0U };
    pattern.dest_mask = destinationMask;
    for (uint8_t component = 0U; component < 4U; ++component) {
        pattern.SetSelectorSrc1(component,
                                static_cast<nihstro::SwizzlePattern::Selector>(source1Broadcast.value_or(component)));
        pattern.SetSelectorSrc2(component,
                                static_cast<nihstro::SwizzlePattern::Selector>(source2Broadcast.value_or(component)));
    }
    return pattern.hex;
}

Oot3dNativeGame::Oot3dPicaDrawPacket BuildCmbTransformPacket() {
    using nihstro::DestRegister;
    using nihstro::OpCode;
    using nihstro::SourceRegister;
    Oot3dNativeGame::Oot3dPicaDrawPacket packet{};
    packet.Registers[0x4FU] = 2U;
    packet.Registers[0x50U] = 0x03020100U;
    packet.Registers[0x51U] = 0x1F141312U;
    packet.Registers[0x2B9U] = 7U;
    packet.Registers[0x2BDU] = 3U;

    std::array<uint8_t, 4U> matrixSwizzles{};
    for (uint8_t component = 0U; component < 4U; ++component) {
        matrixSwizzles[component] = component;
        packet.VertexShader.Swizzles[component] = Swizzle(static_cast<uint8_t>(0x8U >> component));
    }
    for (uint8_t component = 0U; component < 4U; ++component) {
        packet.VertexShader.Swizzles[4U + component] = Swizzle(0xCU, 3U, component);
        packet.VertexShader.Swizzles[8U + component] = Swizzle(0x1U, 3U, component);
    }
    packet.VertexShader.Swizzles[12U] = Swizzle(0xCU);
    packet.VertexShader.SwizzleWordCount = 13U;

    auto& program = packet.VertexShader.Program;
    program[0] = UniformBranchInstruction(2U, 17U, 6U);
    program[1] = UniformBranchInstruction(3U, 14U, 3U);
    const std::array<uint8_t, 5U> influenceComponents{ 0U, 1U, 2U, 3U, 0U };
    size_t offset = 2U;
    for (uint8_t component : influenceComponents) {
        program[offset++] =
            ArithmeticInstruction(OpCode::Id::MUL, DestRegister::MakeTemporary(1), SourceRegister::MakeFloat(93),
                                  SourceRegister::MakeInput(6), static_cast<uint8_t>(4U + component));
        program[offset++] =
            ArithmeticInstruction(OpCode::Id::MUL, DestRegister::MakeTemporary(1), SourceRegister::MakeFloat(94),
                                  SourceRegister::MakeInput(7), static_cast<uint8_t>(8U + component));
        program[offset++] = CallInstruction(31U, 7U);
    }
    for (uint8_t row = 0U; row < 3U; ++row) {
        program[17U + row] =
            ArithmeticInstruction(OpCode::Id::DP3, DestRegister::MakeTemporary(11), SourceRegister::MakeFloat(20 + row),
                                  SourceRegister::MakeInput(1), matrixSwizzles[row]);
        program[20U + row] =
            ArithmeticInstruction(OpCode::Id::DP4, DestRegister::MakeTemporary(8), SourceRegister::MakeFloat(20 + row),
                                  SourceRegister::MakeInput(0), matrixSwizzles[row]);
        program[23U + row] =
            ArithmeticInstruction(OpCode::Id::DP4, DestRegister::MakeTemporary(15), SourceRegister::MakeFloat(4 + row),
                                  SourceRegister::MakeTemporary(8), matrixSwizzles[row]);
    }
    for (uint8_t row = 0U; row < 4U; ++row) {
        program[26U + row] =
            ArithmeticInstruction(OpCode::Id::DP4, DestRegister::MakeOutput(0), SourceRegister::MakeFloat(row),
                                  SourceRegister::MakeTemporary(15), matrixSwizzles[row]);
    }
    program[30U] = TrivialInstruction(OpCode::Id::END);
    program[31U] = MovaInstruction(SourceRegister::MakeTemporary(1), 12U);
    for (uint8_t row = 0U; row < 3U; ++row) {
        program[32U + row] =
            ArithmeticInstruction(OpCode::Id::DP4, DestRegister::MakeTemporary(3), SourceRegister::MakeFloat(20 + row),
                                  SourceRegister::MakeTemporary(15), matrixSwizzles[row], 1U);
        program[35U + row] =
            ArithmeticInstruction(OpCode::Id::DP3, DestRegister::MakeTemporary(4), SourceRegister::MakeFloat(20 + row),
                                  SourceRegister::MakeTemporary(14), matrixSwizzles[row], 2U);
    }
    packet.VertexShader.ProgramWordCount = 38U;
    return packet;
}

class MemoryFillSink final : public Oot3dNativeGame::Oot3dPicaPacketSink {
  public:
    bool SubmitHardwareRegisterWrite(
        const Oot3dNativeGame::Oot3dPicaHardwareRegisterWrite&,
        std::string*) override {
        return true;
    }
    bool SubmitDrawPacket(const Oot3dNativeGame::Oot3dPicaDrawPacket&,
                          std::string*) override {
        ++DrawCount;
        return true;
    }
    bool SubmitMemoryFill(
        const Oot3dNativeGame::Oot3dPicaMemoryFillCommand& command,
        bool* deferredToGpu, std::string*) override {
        Fill = command;
        *deferredToGpu = true;
        return true;
    }
    Oot3dNativeGame::Oot3dPicaMemoryFillCommand Fill;
    size_t DrawCount = 0;
};

} // namespace

int main() {
    auto frontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    auto& frontend = *frontendStorage;
    constexpr std::array<uint32_t, 3> values{
        0x11223344U, 0x55667788U, 0xAABBCCDDU};
    std::string error;
    Require(frontend.WriteHardwareRegisters(0x400000U, values, &error),
            error);
    Require(frontend.HardwareWriteCount() == values.size(),
            "hardware write count is wrong");
    Require(frontend.ReadHardwareRegister(0x400004U) == values[1],
            "register state was not retained");
    Require(frontend.PendingWrites().size() == values.size() &&
                frontend.PendingWrites()[0].PhysicalAddress == 0x1EF00000U,
            "backend-neutral write packet is wrong");
    const auto writes = frontend.TakePendingWrites();
    Require(writes.size() == values.size() && frontend.PendingWrites().empty(),
            "pending writes were not drained");
    Require(!frontend.WriteHardwareRegisters(3U, values, &error),
            "misaligned register write was accepted");
    constexpr std::array<uint32_t, 33> oversized{};
    Require(!frontend.WriteHardwareRegisters(0U, oversized, &error),
            "oversized register write was accepted");
    constexpr std::array<uint32_t, 1> maskedValue{0xFFFF0000U};
    constexpr std::array<uint32_t, 1> maskedBits{0x00FF00FFU};
    Require(frontend.WriteHardwareRegistersWithMask(
                0x400004U, maskedValue, maskedBits, &error),
            error);
    Require(frontend.ReadHardwareRegister(0x400004U) == 0x55FF7700U,
            "masked register write did not preserve unmasked bits");

    MemoryFillSink fillSink;
    auto fillFrontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>(&fillSink);
    auto& fillFrontend = *fillFrontendStorage;
    Oot3dNativeGame::Oot3dGspCommandPacket fillPacket{};
    fillPacket.Control = 2U;
    fillPacket.Parameters = {0x14000100U, 0x11223344U, 0x14000200U,
                             0x14000300U, 0x55667788U, 0x14000400U,
                             0x02010101U};
    bool fillDeferred = false;
    Require(fillFrontend.SubmitGspCommand(fillPacket, {}, &error, nullptr,
                                          &fillDeferred) &&
                fillDeferred &&
                fillSink.Fill.Fills[0].StartAddress == 0x14000100U &&
                fillSink.Fill.Fills[0].EndAddress == 0x14000200U &&
                fillSink.Fill.Fills[0].Value == 0x11223344U &&
                fillSink.Fill.Fills[0].Control == 0x0101U &&
                fillSink.Fill.Fills[1].StartAddress == 0x14000300U &&
                fillSink.Fill.Fills[1].EndAddress == 0x14000400U &&
                fillSink.Fill.Fills[1].Value == 0x55667788U &&
                fillSink.Fill.Fills[1].Control == 0x0201U,
            "GSP memory fill packet was not decoded in native field order");

    Oot3dNativeGame::Oot3dGspCommandPacket submit{};
    submit.Control = 1;
    submit.Parameters[0] = 0x14001000U;
    submit.Parameters[1] = 8U;
    constexpr std::array<uint32_t, 2> commandList{
        0x12345678U, 0x000F0042U};
    Require(frontend.SubmitGspCommand(submit, commandList, &error), error);
    Require(frontend.PendingGspCommands().size() == 1 &&
                frontend.PendingRegisterWrites().size() == 1,
            "submit command list did not produce typed packets");
    const auto& registerWrite = frontend.PendingRegisterWrites()[0];
    Require(registerWrite.CommandListAddress == 0x14001000U &&
                registerWrite.RegisterId == 0x42U &&
                registerWrite.ParameterMask == 0xFU &&
                registerWrite.FinalValue == 0x12345678U &&
                frontend.ReadPicaRegister(0x42U) == 0x12345678U,
            "PICA command-list register write was decoded incorrectly");

    Oot3dNativeGame::Oot3dGspCommandPacket drawSubmit{};
    drawSubmit.Control = 1;
    drawSubmit.Parameters[0] = 0x14002000U;
    drawSubmit.Parameters[1] = 16U;
    constexpr std::array<uint32_t, 4> drawCommandList{
        3U, 0x000F0228U, 1U, 0x000F022EU};
    frontend.SetCommandListCompositionDomain(
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui);
    Require(frontend.SubmitGspCommand(drawSubmit, drawCommandList, &error),
            error);
    Require(frontend.PendingDrawPackets().size() == 1 &&
                !frontend.PendingDrawPackets()[0].Indexed &&
                frontend.PendingDrawPackets()[0].Registers[0x228] == 3U &&
                frontend.PendingDrawPackets()[0].CompositionDomain ==
                    Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui &&
                frontend.PendingDrawPackets()[0].Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Ui &&
                frontend.PendingDrawPackets()[0].Composition.Provenance ==
                    Oot3dNativeGame::Oot3dPicaCompositionProvenance::
                        NativeUiLifecycle,
            "non-indexed PICA draw snapshot is wrong");

    auto compositionFrontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    auto& compositionFrontend = *compositionFrontendStorage;
    compositionFrontend.SetCommandListCompositionDomain(
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene);
    Oot3dNativeGame::Oot3dGspCommandPacket compositionSubmit{};
    compositionSubmit.Control = 1U;
    compositionSubmit.Parameters[0] = 0x14002800U;
    constexpr std::array<uint32_t, 6> compositionCommandList{
        1U, 0x000F022EU, 1U, 0x000F022EU, 1U, 0x000F022EU};
    compositionSubmit.Parameters[1] = static_cast<uint32_t>(
        compositionCommandList.size() * sizeof(uint32_t));
    constexpr std::array<Oot3dNativeGame::
                             Oot3dPicaCommandListCompositionSpan,
                         2>
        compositionSpans{{
            {0x14002800U,
             0x14002808U,
             {Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld,
              Oot3dNativeGame::Oot3dPicaCompositionProvenance::
                  NativeCmbDrawPass,
              0x0030F4D0U, 0U}},
            {0x14002810U,
             0x14002818U,
             {Oot3dNativeGame::Oot3dPicaCompositionLayer::TransparentWorld,
              Oot3dNativeGame::Oot3dPicaCompositionProvenance::
                  NativeCmbDrawPass,
              0x0030F4D0U, 1U}},
        }};
    Require(compositionFrontend.SetNextCommandListCompositionSpans(
                compositionSubmit.Parameters[0],
                compositionSubmit.Parameters[1], compositionSpans, &error),
            error);
    Require(compositionFrontend.SubmitGspCommand(
                compositionSubmit, compositionCommandList, &error),
            error);
    const auto& compositionDraws =
        compositionFrontend.PendingDrawPackets();
    Require(compositionDraws.size() == 3U &&
                compositionDraws[0].Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld &&
                compositionDraws[0].Composition.NativeValue == 0U &&
                compositionDraws[1].Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::Unknown &&
                compositionDraws[2].Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::
                        TransparentWorld &&
                compositionDraws[2].Composition.NativeValue == 1U,
            "native command-list spans did not classify individual draws");
    Require(compositionFrontend.SubmitGspCommand(
                compositionSubmit, compositionCommandList, &error),
            error);
    Require(compositionFrontend.PendingDrawPackets().back()
                .Composition.Layer ==
                Oot3dNativeGame::Oot3dPicaCompositionLayer::Unknown,
            "command-list composition spans were not one-shot");

    Oot3dNativeGame::Oot3dGspCommandPacket shaderSubmit{};
    shaderSubmit.Control = 1;
    shaderSubmit.Parameters[0] = 0x14002400U;
    constexpr std::array<uint32_t, 20> shaderCommandList{
        3U, 0x000F02CBU,
        0xAABBCCDDU, 0x000F02CCU,
        0x11223344U, 0x000F02CCU,
        0x00008001U, 0x000F02B0U,
        0x44332211U, 0x000F02B1U,
        0x80000005U, 0x000F02C0U,
        std::bit_cast<uint32_t>(1.0F), 0x000F02C1U,
        std::bit_cast<uint32_t>(2.0F), 0x000F02C1U,
        std::bit_cast<uint32_t>(3.0F), 0x000F02C1U,
        std::bit_cast<uint32_t>(4.0F), 0x000F02C1U,
    };
    shaderSubmit.Parameters[1] =
        static_cast<uint32_t>(shaderCommandList.size() * sizeof(uint32_t));
    Require(frontend.SubmitGspCommand(shaderSubmit, shaderCommandList, &error),
            error);

    Oot3dNativeGame::Oot3dGspCommandPacket shaderDraw{};
    shaderDraw.Control = 1;
    shaderDraw.Parameters[0] = 0x14002600U;
    shaderDraw.Parameters[1] = 8U;
    constexpr std::array<uint32_t, 2> shaderDrawCommandList{
        1U, 0x000F022EU};
    Require(frontend.SubmitGspCommand(shaderDraw, shaderDrawCommandList,
                                      &error),
            error);
    const auto& shaderDrawPacket = frontend.PendingDrawPackets().back();
    Require(shaderDrawPacket.VertexShader.ProgramWordCount == 5U &&
                shaderDrawPacket.VertexShader.Program[3] == 0xAABBCCDDU &&
                shaderDrawPacket.VertexShader.Program[4] == 0x11223344U &&
                shaderDrawPacket.VertexShader.BooleanUniforms[0] &&
                shaderDrawPacket.VertexShader.BooleanUniforms[15] &&
                shaderDrawPacket.VertexShader.IntegerUniforms[0][0] == 0x11U &&
                shaderDrawPacket.VertexShader.IntegerUniforms[0][3] == 0x44U &&
                shaderDrawPacket.VertexShader.FloatUniforms[5] ==
                    std::array<float, 4>{4.0F, 3.0F, 2.0F, 1.0F},
            "PICA vertex shader upload state was not retained by the draw");
    Require(shaderDrawPacket.GeometryShader.Program[3] == 0xAABBCCDDU &&
                shaderDrawPacket.GeometryShader.FloatUniforms[5] ==
                    shaderDrawPacket.VertexShader.FloatUniforms[5],
            "shared shader unit did not mirror native vertex state");

    Oot3dNativeGame::Oot3dGspCommandPacket defaultAttributeSubmit{};
    defaultAttributeSubmit.Control = 1;
    defaultAttributeSubmit.Parameters[0] = 0x14002500U;
    constexpr std::array<uint32_t, 8> defaultAttributeCommandList{
        2U, 0x000F0232U,
        0x3F00003FU, 0x000F0233U,
        0x00003F00U, 0x000F0234U,
        0x003F0000U, 0x000F0235U,
    };
    defaultAttributeSubmit.Parameters[1] = static_cast<uint32_t>(
        defaultAttributeCommandList.size() * sizeof(uint32_t));
    Require(frontend.SubmitGspCommand(defaultAttributeSubmit,
                                      defaultAttributeCommandList, &error),
            error);
    Require(frontend.ReadPicaRegister(0x232U) == 3U,
            "PICA default attribute index did not autoincrement");

    Oot3dNativeGame::Oot3dGspCommandPacket defaultAttributeDraw{};
    defaultAttributeDraw.Control = 1;
    defaultAttributeDraw.Parameters[0] = 0x14002580U;
    defaultAttributeDraw.Parameters[1] = 8U;
    constexpr std::array<uint32_t, 2> defaultAttributeDrawCommandList{
        1U, 0x000F022EU};
    Require(frontend.SubmitGspCommand(defaultAttributeDraw,
                                      defaultAttributeDrawCommandList, &error),
            error);
    Require(frontend.PendingDrawPackets().back().DefaultAttributes[2] ==
                std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F},
            "PICA default vertex attribute was not retained by the draw");

    Oot3dNativeGame::Oot3dPicaDrawPacket capturedDraw{};
    capturedDraw.Indexed = true;
    capturedDraw.Registers[0x040] = 0x00000002U;
    capturedDraw.Registers[0x041] = 0x0045E000U;
    capturedDraw.Registers[0x043] = 0x00469000U;
    capturedDraw.Registers[0x080] = 1U;
    capturedDraw.Registers[0x082] = (64U << 16U) | 64U;
    capturedDraw.Registers[0x083] =
        (1U << 1U) | (1U << 2U) | (1U << 24U);
    capturedDraw.Registers[0x084] =
        0x1F00U | (3U << 16U) | (1U << 24U);
    capturedDraw.Registers[0x085] = 0x04000140U;
    capturedDraw.Registers[0x08E] = 12U;
    capturedDraw.Registers[0x100] = 0x00E40100U;
    capturedDraw.Registers[0x101] = 0x01010000U;
    capturedDraw.Registers[0x102] = 0x00000003U;
    capturedDraw.Registers[0x104] = 0x00000060U;
    capturedDraw.Registers[0x105] = 0xFF00FF10U;
    capturedDraw.Registers[0x107] = 0x00001F11U;
    capturedDraw.Registers[0x113] = 0x0000000FU;
    capturedDraw.Registers[0x115] = 0x00000002U;
    capturedDraw.Registers[0x200] = 0x03000000U;
    capturedDraw.Registers[0x201] = 0x000000FBU;
    capturedDraw.Registers[0x202] = 0x70FC0000U;
    capturedDraw.Registers[0x203] = 0x08670210U;
    capturedDraw.Registers[0x205] = 0x100C0000U;
    capturedDraw.Registers[0x206] = 0x08670240U;
    capturedDraw.Registers[0x207] = 1U;
    capturedDraw.Registers[0x208] = 0x10100000U;
    capturedDraw.Registers[0x227] = 0x886702C0U;
    capturedDraw.Registers[0x228] = 4U;
    capturedDraw.Registers[0x2B9] = 0xA0000007U;
    capturedDraw.Registers[0x2BB] = 0x13456720U;
    Oot3dNativeGame::Oot3dPicaDecodedDrawState decodedDraw;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                capturedDraw, decodedDraw, &error),
            error);
    const Oot3dNativeGame::Oot3dPicaTextureState mipSizeTexture{
        .Enabled = true,
        .Width = 16U,
        .Height = 16U,
        .Format = 0U,
        .MaxMipLevel = 1U,
    };
    Require(Oot3dNativeGame::Oot3dPicaTextureMipLevelByteSize(
                mipSizeTexture, 0U) == 1024U &&
                Oot3dNativeGame::Oot3dPicaTextureMipLevelByteSize(
                    mipSizeTexture, 1U) == 256U &&
                Oot3dNativeGame::Oot3dPicaTextureMipChainByteSize(
                    mipSizeTexture) == 1280U,
            "PICA native mip byte layout was not preserved");
    Require(decodedDraw.VertexInput.PhysicalBaseAddress == 0x18000000U &&
                decodedDraw.VertexInput.AttributeCount == 8U &&
                decodedDraw.VertexInput.Attributes[0].Format ==
                    Oot3dNativeGame::Oot3dPicaVertexFormat::Float &&
                decodedDraw.VertexInput.Attributes[0].ComponentCount == 3U &&
                decodedDraw.VertexInput.Attributes[2].Default &&
                decodedDraw.VertexInput.Loaders[0].PhysicalAddress ==
                    0x20670210U &&
                decodedDraw.VertexInput.Loaders[0].ByteStride == 12U &&
                decodedDraw.VertexInput.Loaders[1].Components[0] == 1U &&
                decodedDraw.VertexInput.IndexPhysicalAddress == 0x206702C0U &&
                decodedDraw.VertexInput.IndicesAre16Bit &&
                decodedDraw.VertexInput.VertexCount == 4U &&
                decodedDraw.ShaderInterface.MaximumInputAttribute == 7U &&
                decodedDraw.ShaderInterface.InputRegisterByAttribute[0] == 0U &&
                decodedDraw.ShaderInterface.InputRegisterByAttribute[1] == 2U &&
                decodedDraw.CullMode ==
                    Oot3dNativeGame::Oot3dPicaCullMode::KeepCounterClockwise &&
                decodedDraw.Viewport.HalfWidth == 120.0F &&
                decodedDraw.Viewport.HalfHeight == 200.0F &&
                decodedDraw.Scissor.Mode ==
                    Oot3dNativeGame::Oot3dPicaScissorMode::Disabled &&
                decodedDraw.Textures[0].Enabled &&
                decodedDraw.Textures[0].MipLinear &&
                decodedDraw.Textures[0].LodBiasRaw == -256 &&
                decodedDraw.Textures[0].MinMipLevel == 1U &&
                decodedDraw.Textures[0].MaxMipLevel == 3U &&
                Oot3dNativeGame::Oot3dPicaTextureMipLevelCount(
                    decodedDraw.Textures[0]) == 4U &&
                decodedDraw.OutputMerger.ColorWriteMask == 0xFU &&
                decodedDraw.OutputMerger.Blend.Enabled &&
                decodedDraw.OutputMerger.Blend.SourceColor ==
                    Oot3dNativeGame::Oot3dPicaBlendFactor::One &&
                decodedDraw.OutputMerger.LogicOperation ==
                    Oot3dNativeGame::Oot3dPicaLogicOperation::Copy &&
                decodedDraw.OutputMerger.Depth.TestEnabled &&
                decodedDraw.OutputMerger.Depth.WriteEnabled &&
                decodedDraw.OutputMerger.Depth.Compare ==
                    Oot3dNativeGame::Oot3dPicaCompareFunction::Always &&
                !decodedDraw.OutputMerger.Stencil.Enabled,
            "captured native OOT3D draw state decoded incorrectly");

    Oot3dNativeGame::Oot3dPicaDrawPacket endShaderPacket{};
    endShaderPacket.VertexShader.Program[0] = 0x88000000U;
    endShaderPacket.VertexShader.ProgramWordCount = 1U;
    endShaderPacket.VertexShader.SwizzleWordCount = 1U;
    endShaderPacket.Registers[0x4F] = 1U;
    endShaderPacket.Registers[0x50] = 0x03020100U;
    endShaderPacket.Registers[0x2BD] = 1U;
    endShaderPacket.VertexShader.BooleanUniforms[3] = true;
    endShaderPacket.VertexShader.IntegerUniforms[1] = {5U, 6U, 7U, 8U};
    endShaderPacket.VertexShader.FloatUniforms[2] =
        {1.0F, 2.0F, 3.0F, 4.0F};
    Oot3dNativeGame::Oot3dPicaDecodedDrawState endShaderState;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                endShaderPacket, endShaderState, &error),
            error);
    Oot3dNativeGame::Oot3dPicaGeneratedVertexShader generatedShader;
    Require(Oot3dNativeGame::GenerateOot3dPicaVertexShader(
                endShaderPacket, endShaderState, generatedShader, &error),
            error);
    Require(generatedShader.StateKey != 0U &&
                generatedShader.Source.find("bool exec_shader()") !=
                    std::string::npos &&
                generatedShader.Source.find("vec4 sanitize_mul(") !=
                    std::string::npos &&
                generatedShader.Source.find("gl_Position = vec4(") !=
                    std::string::npos &&
                generatedShader.Source.find("pica_primary_color = min(abs(pica_raw_primary_color), "
                                            "vec4(1.0))") !=
                    std::string::npos &&
                generatedShader.Source.find(
                    "pica_ndc_z = pica_position.z / pica_position.w") !=
                    std::string::npos &&
                generatedShader.TemporalProgram != nullptr &&
                generatedShader.TemporalProgram->Hooks.ValidFor(
                    generatedShader.Source) &&
                generatedShader.TemporalProgram->PreviousRegisterState.find(
                    "sanitize_mul_previous") != std::string::npos &&
                generatedShader.TemporalProgram->PreviousMainBody.find(
                    "pica_ndc_z_previous") != std::string::npos &&
                generatedShader.TemporalProgram->PreviousMainBody.find(
                    "pica_raw_primary_color_previous") !=
                    std::string::npos &&
                generatedShader.Uniforms.BooleanMask == (1U << 3U) &&
                generatedShader.Uniforms.Integers[1] ==
                    std::array<uint32_t, 4>{5U, 6U, 7U, 8U} &&
                generatedShader.Uniforms.Floats[2] ==
                    std::array<float, 4>{1.0F, 2.0F, 3.0F, 4.0F},
            "native PICA vertex GLSL was not generated");

    auto transformPacket = BuildCmbTransformPacket();
    Oot3dNativeGame::Oot3dPicaDecodedDrawState transformState;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(transformPacket, transformState, &error), error);
    Oot3dNativeGame::Oot3dPicaGeneratedVertexShader transformShader;
    Require(Oot3dNativeGame::GenerateOot3dPicaVertexShader(transformPacket, transformState, transformShader, &error),
            error);
    const auto& transformHooks = transformShader.TemporalProgram->Hooks;
    Require(transformHooks.Has(
                Oot3d::Renderer::PicaVertexShaderSemantic::
                    ViewPositionOutput),
            "CMB view-position output was not structurally published");
    Require(transformHooks.TransformProgram() != nullptr && transformHooks.Transform.ProjectionFirstUniform == 0U &&
                transformHooks.Transform.ViewFirstUniform == 4U && transformHooks.Transform.ModelFirstUniform == 20U &&
                transformHooks.Transform.PositionInputRegister == 0U &&
                transformHooks.Transform.NormalInputRegister == 1U,
            "CMB transform program was not structurally decoded");
    Require(transformHooks.SkeletonProgram() != nullptr && transformHooks.Skeleton.EnableBooleanUniform == 2U &&
                transformHooks.Skeleton.MultipleInfluenceBooleanUniform == 3U &&
                transformHooks.Skeleton.PaletteFirstUniform == 20U &&
                transformHooks.Skeleton.BoneIndexInputRegister == 6U &&
                transformHooks.Skeleton.BoneWeightInputRegister == 7U &&
                transformHooks.Skeleton.MaximumInfluences == 4U,
            "CMB matrix-palette program was not structurally decoded");
    Oot3dNativeGame::Oot3dPicaDrawPacket fragmentPacket{};
    fragmentPacket.Registers[0x0C0] = 0x0E300E30U;
    fragmentPacket.Registers[0x0C2] = 0x00010001U;
    for (uint16_t base :
         std::array<uint16_t, 5>{0x0C8U, 0x0D0U, 0x0D8U, 0x0F0U,
                                 0x0F8U}) {
        fragmentPacket.Registers[base] = 0x0FFF0FFFU;
        fragmentPacket.Registers[base + 3U] = 0xFFFFFFFFU;
    }
    fragmentPacket.Registers[0x104] = 0x60U;
    Oot3dNativeGame::Oot3dPicaDecodedDrawState fragmentState;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                fragmentPacket, fragmentState, &error),
            error);
    Oot3dNativeGame::Oot3dPicaGeneratedFragmentShader generatedFragment;
    Require(Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                fragmentPacket, fragmentState, generatedFragment, &error),
            error);
    Require(generatedFragment.StateKey != 0U &&
                generatedFragment.Hooks.ValidFor(
                    generatedFragment.Source) &&
                generatedFragment.Hooks.Outputs.CanonicalNative() &&
                generatedFragment.Hooks.Outputs.ColorLocationMask == 1U &&
                generatedFragment.Hooks.Outputs.NativeColorLocation == 0U &&
                generatedFragment.Hooks.Outputs.Depth ==
                    Oot3d::Renderer::
                        PicaFragmentDepthOutput::ExplicitNative &&
                generatedFragment.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::
                        NativeColorOutput) &&
                generatedFragment.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::
                        NormalQuaternion) &&
                generatedFragment.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::
                        PrimaryColorInput) &&
                generatedFragment.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::ViewVector) &&
                generatedFragment.Hooks.Has(
                    Oot3d::Renderer::PicaShaderSemantic::CombinerOutput) &&
                generatedFragment.Source.substr(
                    generatedFragment.Hooks.Offset(
                        Oot3d::Renderer::PicaShaderHook::
                            GlobalDeclarations),
                    std::string_view("void main()").size()) ==
                    "void main()" &&
                generatedFragment.Source.substr(
                    generatedFragment.Hooks.Offset(
                        Oot3d::Renderer::PicaShaderHook::BeforeDepth),
                    std::string_view(
                        "    float pica_z_over_w").size()) ==
                    "    float pica_z_over_w" &&
                generatedFragment.Source.find("combiner_output = vec4") !=
                    std::string::npos &&
                generatedFragment.Source.find("pica_color = vec4") !=
                    std::string::npos,
            "native PICA TEV fragment GLSL was not generated");
    auto secondaryColorPacket = fragmentPacket;
    secondaryColorPacket.Registers[0x0C0U] = 0x00020002U;
    Require(Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                secondaryColorPacket, fragmentState,
                generatedFragment, &error),
            error);
    Require(generatedFragment.Hooks.Has(
                Oot3d::Renderer::PicaShaderSemantic::
                    SecondaryFragmentColorConsumed),
            "PICA TEV secondary-color consumption was not published");
    fragmentPacket.Registers[0x080] = (1U << 2U) | (1U << 13U);
    fragmentPacket.Registers[0x09C] = 0x1F00U;
    fragmentPacket.Registers[0x0C0] = 0x00050005U;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                fragmentPacket, fragmentState, &error),
            error);
    Require(fragmentState.Texture2UsesCoordinate1,
            "PICA texture-coordinate routing was not decoded");
    Require(Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                fragmentPacket, fragmentState, generatedFragment, &error),
            error);
    Require(generatedFragment.Source.find(
                "pica_sample_texture2(vec2(pica_texcoord1.x, 1.0 - "
                "pica_texcoord1.y))") !=
                std::string::npos &&
                generatedFragment.Hooks.SamplesTexture(2U) &&
                !generatedFragment.Hooks.SamplesTexture(0U) &&
                generatedFragment.Hooks.TextureSample(2U) != nullptr &&
                generatedFragment.Hooks.TextureSample(2U)->Coordinate == 1U &&
                generatedFragment.Hooks.TextureSample(2U)->Operation ==
                    Oot3d::Renderer::
                        PicaTextureCoordinateOperation::NativeVFlip &&
                generatedFragment.Uniforms.TextureLodBias[2] == -1.0F,
            "PICA texture 2 ignored native coordinate 1 routing");

    auto fogFrontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    auto& fogFrontend = *fogFrontendStorage;
    Oot3dNativeGame::Oot3dGspCommandPacket fogUpload{};
    fogUpload.Control = 1;
    fogUpload.Parameters[0] = 0x14004000U;
    constexpr uint32_t fogValue0 = (1024U << 13U) | 0x1E00U;
    constexpr uint32_t fogValue1 = (1536U << 13U) | 0x0100U;
    constexpr std::array<uint32_t, 5> fogUploadCommands{
        126U, 0x000F00E6U, fogValue0, 0x801F00E8U, fogValue1};
    fogUpload.Parameters[1] = static_cast<uint32_t>(
        fogUploadCommands.size() * sizeof(uint32_t));
    Require(fogFrontend.SubmitGspCommand(
                fogUpload, fogUploadCommands, &error),
            error);
    Oot3dNativeGame::Oot3dGspCommandPacket fogDraw{};
    fogDraw.Control = 1;
    fogDraw.Parameters[0] = 0x14004100U;
    fogDraw.Parameters[1] = 8U;
    constexpr std::array<uint32_t, 2> fogDrawCommands{
        1U, 0x000F022EU};
    Require(fogFrontend.SubmitGspCommand(fogDraw, fogDrawCommands, &error),
            error);
    const auto& fogPacket = fogFrontend.PendingDrawPackets().back();
    Require(fogFrontend.ReadPicaRegister(0x0E6U) == 128U &&
                fogPacket.FogLut[126] == fogValue0 &&
                fogPacket.FogLut[127] == fogValue1,
            "PICA fog LUT upload did not preserve native autoincrement state");
    auto fogShaderPacket = fogPacket;
    fogShaderPacket.Registers[0x0E0U] = 5U | (1U << 16U);
    fogShaderPacket.Registers[0x0E1U] = 0x00332211U;
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                fogShaderPacket, fragmentState, &error) &&
                Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                    fogShaderPacket, fragmentState, generatedFragment, &error),
            error);
    Require(generatedFragment.Source.find(
                "fragment_uniforms.fog_lut[fog_entry_index >> 1]") !=
                    std::string::npos &&
                generatedFragment.Source.find("(1.0 - pica_depth)") !=
                    std::string::npos &&
                generatedFragment.Uniforms.FogLut[126][0] ==
                    1024.0F / 2047.0F &&
                generatedFragment.Uniforms.FogLut[126][1] ==
                    -512.0F / 2047.0F,
            "native PICA fog shader or signed LUT decoding is wrong");

    auto lightingLutFrontend =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    Oot3dNativeGame::Oot3dGspCommandPacket lightingLutUpload{};
    lightingLutUpload.Control = 1;
    lightingLutUpload.Parameters[0] = 0x14004180U;
    constexpr uint32_t lightingLutValue255 = 0x00123456U;
    constexpr uint32_t lightingLutValue0 = 0x00876543U;
    constexpr std::array<uint32_t, 5> lightingLutUploadCommands{
        255U, 0x000F01C5U, lightingLutValue255,
        0x801F01C8U, lightingLutValue0};
    lightingLutUpload.Parameters[1] = static_cast<uint32_t>(
        lightingLutUploadCommands.size() * sizeof(uint32_t));
    Require(lightingLutFrontend->SubmitGspCommand(
                lightingLutUpload, lightingLutUploadCommands, &error) &&
                lightingLutFrontend->SubmitGspCommand(
                    fogDraw, fogDrawCommands, &error),
            error);
    const auto firstLightingLuts =
        lightingLutFrontend->PendingDrawPackets().back().LightingLuts;
    Require(firstLightingLuts != nullptr &&
                firstLightingLuts->Entry(0U, 255U) ==
                    lightingLutValue255 &&
                firstLightingLuts->Entry(0U, 0U) == lightingLutValue0 &&
                firstLightingLuts->ContentHashAvailable &&
                lightingLutFrontend->ReadPicaRegister(0x1C5U) == 1U,
            "PICA lighting LUT upload did not preserve table/index state");
    constexpr uint32_t changedLightingLutValue0 = 0x00000FFFU;
    constexpr std::array<uint32_t, 4> changedLightingLutCommands{
        0U, 0x000F01C5U, changedLightingLutValue0, 0x000F01C8U};
    lightingLutUpload.Parameters[1] = static_cast<uint32_t>(
        changedLightingLutCommands.size() * sizeof(uint32_t));
    Require(lightingLutFrontend->SubmitGspCommand(
                lightingLutUpload, changedLightingLutCommands, &error) &&
                lightingLutFrontend->SubmitGspCommand(
                    fogDraw, fogDrawCommands, &error),
            error);
    const auto secondLightingLuts =
        lightingLutFrontend->PendingDrawPackets().back().LightingLuts;
    Require(secondLightingLuts != nullptr &&
                secondLightingLuts != firstLightingLuts &&
                firstLightingLuts->Entry(0U, 0U) == lightingLutValue0 &&
                secondLightingLuts->Entry(0U, 0U) ==
                    changedLightingLutValue0 &&
                secondLightingLuts->ContentHash !=
                    firstLightingLuts->ContentHash,
            "PICA lighting LUT draw snapshots are not immutable");

    auto procTexFrontend =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>();
    Oot3dNativeGame::Oot3dGspCommandPacket procTexUpload{};
    procTexUpload.Control = 1;
    procTexUpload.Parameters[0] = 0x14004200U;
    constexpr uint32_t procTexValue127 = 0x00123045U;
    constexpr uint32_t procTexValue0 = 0x00ABC678U;
    constexpr std::array<uint32_t, 5> procTexUploadCommands{
        (2U << 8U) | 127U, 0x000F00AFU, procTexValue127,
        0x801F00B0U, procTexValue0};
    procTexUpload.Parameters[1] = static_cast<uint32_t>(
        procTexUploadCommands.size() * sizeof(uint32_t));
    Require(procTexFrontend->SubmitGspCommand(
                procTexUpload, procTexUploadCommands, &error),
            error);
    Oot3dNativeGame::Oot3dGspCommandPacket procTexDraw{};
    procTexDraw.Control = 1;
    procTexDraw.Parameters[0] = 0x14004300U;
    procTexDraw.Parameters[1] = 8U;
    Require(procTexFrontend->SubmitGspCommand(
                procTexDraw, fogDrawCommands, &error),
            error);
    const auto& procTexUploadedPacket =
        procTexFrontend->PendingDrawPackets().back();
    Require((procTexFrontend->ReadPicaRegister(0x0AFU).value_or(0U) & 0xFFU) ==
                129U &&
                procTexUploadedPacket.ProcTexLuts.ColorMap[127] ==
                    procTexValue127 &&
                procTexUploadedPacket.ProcTexLuts.ColorMap[0] == procTexValue0,
            "PICA procedural texture LUT upload did not preserve native "
            "index and table modulo semantics");

    auto procTexPacket = fragmentPacket;
    procTexPacket.Registers[0x080U] = (1U << 10U) | (1U << 8U);
    procTexPacket.Registers[0x0A8U] = 1U | (1U << 3U);
    procTexPacket.Registers[0x0ACU] = 128U << 11U;
    procTexPacket.Registers[0x0ADU] = 0U;
    procTexPacket.Registers[0x0C0U] = 0x00060006U;
    for (size_t index = 0; index < procTexPacket.ProcTexLuts.ColorMap.size();
         ++index) {
        const uint32_t value = static_cast<uint32_t>(index * 4095U / 127U);
        procTexPacket.ProcTexLuts.ColorMap[index] = value;
        procTexPacket.ProcTexLuts.AlphaMap[index] = value;
    }
    for (size_t index = 0; index < procTexPacket.ProcTexLuts.Color.size();
         ++index) {
        const uint32_t value = static_cast<uint32_t>(index);
        procTexPacket.ProcTexLuts.Color[index] =
            value | (value << 8U) | (value << 16U) | 0xFF000000U;
    }
    Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(
                procTexPacket, fragmentState, &error) &&
                Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                    procTexPacket, fragmentState, generatedFragment, &error),
            error);
    const auto procTexFeatures =
        Oot3dNativeGame::AnalyzeOot3dPicaFragmentFeatures(
            procTexPacket, fragmentState);
    const auto procTexIdentity =
        Oot3dNativeGame::BuildOot3dPicaCanonicalDrawIdentity(
            procTexPacket, fragmentState);
    const uint64_t procTexStateKey = generatedFragment.StateKey;
    Require(procTexFeatures.ProceduralTextureEnabled &&
                procTexFeatures.ProceduralTextureReferenced &&
                procTexFeatures.FullySupported() &&
                generatedFragment.Source.find("pica_sample_proctex()") !=
                    std::string::npos &&
                generatedFragment.Source.find(
                    "pica_proctex_color_lut[256]") != std::string::npos,
            "native PICA procedural texture shader was not generated");
    procTexPacket.ProcTexLuts.ColorMap[0] ^= 1U;
    Require(Oot3dNativeGame::GenerateOot3dPicaFragmentShader(
                procTexPacket, fragmentState, generatedFragment, &error),
            error);
    const auto changedProcTexIdentity =
        Oot3dNativeGame::BuildOot3dPicaCanonicalDrawIdentity(
            procTexPacket, fragmentState);
    Require(generatedFragment.StateKey != procTexStateKey &&
                changedProcTexIdentity.FragmentProgramId !=
                    procTexIdentity.FragmentProgramId,
            "procedural texture LUT data was omitted from shader identity");

    Oot3dNativeGame::Oot3dGspCommandPacket irqSubmit{};
    irqSubmit.Control = 1;
    irqSubmit.Parameters[0] = 0x14003000U;
    irqSubmit.Parameters[1] = 8U;
    constexpr std::array<uint32_t, 2> irqCommandList{
        0x12345678U, 0x000F0010U};
    Require(frontend.SubmitGspCommand(irqSubmit, irqCommandList, &error),
            error);
    const auto interrupts = frontend.TakePendingInterrupts();
    Require(interrupts.size() == 1 &&
                interrupts[0] ==
                    Oot3dNativeGame::Oot3dPicaInterruptId::P3d,
            "PICA IRQ request did not produce the native P3D interrupt");

    MemoryFillSink runtimeSink;
    auto runtimeFrontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>(
            &runtimeSink);
    auto& runtimeFrontend = *runtimeFrontendStorage;
    runtimeFrontend.SetDiagnosticHistoryEnabled(false);
    Require(runtimeFrontend.SubmitGspCommand(
                drawSubmit, drawCommandList, &error) &&
                runtimeSink.DrawCount == 1U &&
                runtimeFrontend.PendingGspCommands().empty() &&
                runtimeFrontend.PendingRegisterWrites().empty() &&
                runtimeFrontend.PendingDrawPackets().empty() &&
                runtimeFrontend.ReadPicaRegister(0x228U) == 3U,
            "history-free PICA runtime changed draw submission state");

    const auto runtimeStateBytes = nlohmann::json::to_msgpack(
        runtimeFrontend.CaptureState());
    auto restoredFrontendStorage =
        std::make_unique<Oot3dNativeGame::Oot3dNativePicaFrontend>(
            &runtimeSink);
    auto& restoredFrontend = *restoredFrontendStorage;
    Require(restoredFrontend.RestoreState(
                nlohmann::json::from_msgpack(runtimeStateBytes), &error) &&
                restoredFrontend.ReadPicaRegister(0x228U) == 3U &&
                restoredFrontend.PendingGspCommands().empty() &&
                restoredFrontend.PendingDrawPackets().empty(),
            "native PICA frontend state did not survive binary round-trip");

    std::cout << "oot3d_native_pica_frontend_tests: ok\n";
    return 0;
}
