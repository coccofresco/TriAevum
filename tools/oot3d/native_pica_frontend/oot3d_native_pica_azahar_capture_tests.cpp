#include "oot3d_native_pica_azahar_capture.h"

#include "oot3d_native_pica_draw_state.h"
#include "oot3d_native_pica_fragment_shader_gen.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

nlohmann::json MakeDrawEvent() {
  nlohmann::json registers = nlohmann::json::array();
  for (size_t index = 0; index < 0x300U; ++index) {
    registers.push_back(index == 0x200U ? "0x0000002A" : "0x00000000");
  }
  return {
      {"event", "draw_begin"},
      {"capture_index", 4U},
      {"frame_index", 5U},
      {"draw_index", 6U},
      {"mode", "arrays"},
      {"is_indexed", false},
      {"cmd_list_addr", "0x12345678"},
      {"cmd_list_offset_words", 9U},
      {"triangle_topology", 0U},
      {"register_snapshot",
       {{"complete",
         {{"first", "0x000"},
          {"last", "0x2FF"},
          {"values", std::move(registers)}}}}},
      {"vs_uniforms",
       {{"f", nlohmann::json::array(
                  {nlohmann::json::array({1.0F, 2.0F, 3.0F, 4.0F})})},
        {"b", {true, false}},
        {"i",
         nlohmann::json::array({nlohmann::json::array({1U, 2U, 3U, 4U})})}}},
      {"gs_uniforms", {{"f", nlohmann::json::array()}}},
      {"shader_identity",
       {{"format", "oot3d.azahar.pica_shader_identity.v1"},
        {"vertex",
         {{"program_hash", "0x1111"},
          {"swizzle_hash", "0x2222"},
          {"entry_point", 3U},
          {"program_words", 4U},
          {"swizzle_words", 5U}}},
        {"geometry",
         {{"enabled", false},
          {"program_hash", "0x0000"},
          {"swizzle_hash", "0x0000"},
          {"entry_point", 0U},
          {"program_words", 0U},
          {"swizzle_words", 0U}}},
        {"fragment_config_hash", "0x3333"}}},
  };
}

void TestValidCaptureDecodesThroughProductionFrontend() {
  auto event = MakeDrawEvent();
  Oot3dNativeGame::Oot3dPicaDrawPacket packet;
  Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata metadata;
  std::string error;
  Require(Oot3dNativeGame::DecodeOot3dAzaharPicaDraw(event, packet, metadata,
                                                     &error),
          error);
  Require(packet.CommandListAddress == 0x12345678U,
          "command-list address was not decoded");
  Require(packet.Registers[0x200U] == 42U, "register snapshot was not decoded");
  Require(packet.VertexShader.FloatUniforms[0][2] == 3.0F,
          "float uniforms were not decoded");
  Require(packet.VertexShader.BooleanUniforms[0],
          "boolean uniforms were not decoded");
  Require(packet.VertexShader.IntegerUniforms[0][3] == 4U,
          "integer uniforms were not decoded");
  Require(metadata.FragmentConfigHash == "0x3333",
          "fragment identity was not decoded");

  Oot3dNativeGame::Oot3dPicaDecodedDrawState state;
  Require(Oot3dNativeGame::DecodeOot3dPicaDrawState(packet, state, &error),
          error);
  Oot3dNativeGame::Oot3dPicaGeneratedFragmentShader shader;
  Require(Oot3dNativeGame::GenerateOot3dPicaFragmentShader(packet, state,
                                                           shader, &error),
          error);
  Require(!shader.Source.empty(), "production fragment source is empty");
}

void TestMalformedRegisterRangeIsRejected() {
  auto event = MakeDrawEvent();
  event["register_snapshot"]["complete"]["last"] = "0x2FE";
  Oot3dNativeGame::Oot3dPicaDrawPacket packet;
  Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata metadata;
  std::string error;
  Require(!Oot3dNativeGame::DecodeOot3dAzaharPicaDraw(event, packet, metadata,
                                                      &error),
          "malformed register range was accepted");
  Require(error.find("invalid range") != std::string::npos,
          "malformed range diagnostic is not actionable");
}

void TestPipelineKeyMatchesCoverageTuple() {
  auto event = MakeDrawEvent();
  Oot3dNativeGame::Oot3dPicaDrawPacket packet;
  Oot3dNativeGame::Oot3dAzaharPicaDrawMetadata metadata;
  std::string error;
  Require(Oot3dNativeGame::DecodeOot3dAzaharPicaDraw(event, packet, metadata,
                                                     &error),
          error);
  const auto expected = nlohmann::json::array(
      {nlohmann::json::array({"0x1111", "0x2222", 3U, 4U, 5U}), nullptr,
       "0x3333", 0U, "arrays"});
  Require(Oot3dNativeGame::BuildOot3dAzaharPipelineKey(metadata) ==
              expected.dump(),
          "pipeline tuple does not match the coverage schema");
}

} // namespace

int main() {
  try {
    TestValidCaptureDecodesThroughProductionFrontend();
    TestMalformedRegisterRangeIsRejected();
    TestPipelineKeyMatchesCoverageTuple();
    std::cout << "OOT3D Azahar capture adapter tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &exception) {
    std::cerr << "OOT3D Azahar capture adapter tests failed: "
              << exception.what() << '\n';
    return EXIT_FAILURE;
  }
}
