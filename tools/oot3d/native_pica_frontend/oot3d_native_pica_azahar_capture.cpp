#include "oot3d_native_pica_azahar_capture.h"

#include <array>
#include <charconv>
#include <limits>
#include <string_view>

#include <nlohmann/json.hpp>

namespace Oot3dNativeGame {
namespace {

void SetError(std::string *error, std::string message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

bool ReadUnsigned(const nlohmann::json &value, uint64_t &result) {
  if (value.is_number_unsigned()) {
    result = value.get<uint64_t>();
    return true;
  }
  if (value.is_number_integer()) {
    const int64_t signedValue = value.get<int64_t>();
    if (signedValue < 0) {
      return false;
    }
    result = static_cast<uint64_t>(signedValue);
    return true;
  }
  if (!value.is_string()) {
    return false;
  }
  std::string_view text = value.get_ref<const std::string &>();
  int base = 10;
  if (text.size() > 2U && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2U);
    base = 16;
  }
  if (text.empty()) {
    return false;
  }
  uint64_t parsed = 0;
  const auto [end, ec] =
      std::from_chars(text.data(), text.data() + text.size(), parsed, base);
  if (ec != std::errc{} || end != text.data() + text.size()) {
    return false;
  }
  result = parsed;
  return true;
}

bool ReadU32(const nlohmann::json &value, uint32_t &result) {
  uint64_t parsed = 0;
  if (!ReadUnsigned(value, parsed) ||
      parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  result = static_cast<uint32_t>(parsed);
  return true;
}

bool ReadOptionalU64(const nlohmann::json &object, std::string_view key,
                     uint64_t &result, std::string *error) {
  const auto found = object.find(key);
  if (found == object.end()) {
    result = 0;
    return true;
  }
  if (!ReadUnsigned(*found, result)) {
    SetError(error, "Azahar draw field '" + std::string(key) +
                        "' is not an unsigned integer");
    return false;
  }
  return true;
}

bool ReadOptionalU32(const nlohmann::json &object, std::string_view key,
                     uint32_t &result, std::string *error) {
  const auto found = object.find(key);
  if (found == object.end()) {
    result = 0;
    return true;
  }
  if (!ReadU32(*found, result)) {
    SetError(error, "Azahar draw field '" + std::string(key) +
                        "' is not a 32-bit unsigned integer");
    return false;
  }
  return true;
}

template <size_t Count>
bool ReadFloatUniforms(const nlohmann::json &uniforms,
                       std::array<std::array<float, 4>, Count> &output,
                       std::string *error) {
  const auto found = uniforms.find("f");
  if (found == uniforms.end()) {
    return true;
  }
  if (!found->is_array() || found->size() > output.size()) {
    SetError(error, "Azahar float uniform array exceeds the PICA limit");
    return false;
  }
  for (size_t index = 0; index < found->size(); ++index) {
    const auto &vector = found->at(index);
    if (!vector.is_array() || vector.size() != 4U) {
      SetError(error, "Azahar float uniform is not a vec4");
      return false;
    }
    for (size_t component = 0; component < 4U; ++component) {
      if (!vector.at(component).is_number()) {
        SetError(error, "Azahar float uniform contains a non-number");
        return false;
      }
      output[index][component] = vector.at(component).get<float>();
    }
  }
  return true;
}

bool ReadShaderUniforms(const nlohmann::json &event, std::string_view key,
                        Oot3dPicaShaderState &shader, std::string *error) {
  const auto found = event.find(key);
  if (found == event.end()) {
    return true;
  }
  if (!found->is_object() ||
      !ReadFloatUniforms(*found, shader.FloatUniforms, error)) {
    if (error != nullptr && error->empty()) {
      SetError(error, "Azahar shader uniforms are not an object");
    }
    return false;
  }
  const auto booleans = found->find("b");
  if (booleans != found->end()) {
    if (!booleans->is_array() ||
        booleans->size() > shader.BooleanUniforms.size()) {
      SetError(error, "Azahar boolean uniform array exceeds the PICA limit");
      return false;
    }
    for (size_t index = 0; index < booleans->size(); ++index) {
      if (!booleans->at(index).is_boolean()) {
        SetError(error, "Azahar boolean uniform contains a non-boolean");
        return false;
      }
      shader.BooleanUniforms[index] = booleans->at(index).get<bool>();
    }
  }
  const auto integers = found->find("i");
  if (integers != found->end()) {
    if (!integers->is_array() ||
        integers->size() > shader.IntegerUniforms.size()) {
      SetError(error, "Azahar integer uniform array exceeds the PICA limit");
      return false;
    }
    for (size_t index = 0; index < integers->size(); ++index) {
      const auto &vector = integers->at(index);
      if (!vector.is_array() || vector.size() != 4U) {
        SetError(error, "Azahar integer uniform is not a four-byte vector");
        return false;
      }
      for (size_t component = 0; component < 4U; ++component) {
        uint32_t value = 0;
        if (!ReadU32(vector.at(component), value) || value > 0xFFU) {
          SetError(error, "Azahar integer uniform component is not a byte");
          return false;
        }
        shader.IntegerUniforms[index][component] = static_cast<uint8_t>(value);
      }
    }
  }
  return true;
}

bool ReadShaderIdentityMember(const nlohmann::json &identity,
                              std::string_view name, bool enabled,
                              std::string &programHash,
                              std::string &swizzleHash, uint32_t &entryPoint,
                              uint32_t &programWords, uint32_t &swizzleWords,
                              std::string *error) {
  const auto found = identity.find(name);
  if (found == identity.end() || !found->is_object()) {
    SetError(error, "Azahar shader identity lacks '" + std::string(name) + "'");
    return false;
  }
  if (!enabled) {
    return true;
  }
  try {
    programHash = found->at("program_hash").get<std::string>();
    swizzleHash = found->at("swizzle_hash").get<std::string>();
  } catch (const std::exception &) {
    SetError(error, "Azahar shader identity hashes are invalid");
    return false;
  }
  return ReadOptionalU32(*found, "entry_point", entryPoint, error) &&
         ReadOptionalU32(*found, "program_words", programWords, error) &&
         ReadOptionalU32(*found, "swizzle_words", swizzleWords, error);
}

} // namespace

bool DecodeOot3dAzaharPicaDraw(const nlohmann::json &event,
                               Oot3dPicaDrawPacket &packet,
                               Oot3dAzaharPicaDrawMetadata &metadata,
                               std::string *error) {
  if (error != nullptr) {
    error->clear();
  }
  packet = {};
  metadata = {};
  if (!event.is_object() ||
      event.value("event", std::string{}) != "draw_begin") {
    SetError(error, "Azahar event is not a draw_begin record");
    return false;
  }
  uint64_t commandAddress = 0;
  uint64_t commandOffset = 0;
  if (!ReadOptionalU64(event, "capture_index", metadata.CaptureIndex, error) ||
      !ReadOptionalU64(event, "frame_index", metadata.FrameIndex, error) ||
      !ReadOptionalU64(event, "draw_index", metadata.DrawIndex, error) ||
      !ReadOptionalU64(event, "cmd_list_addr", commandAddress, error) ||
      !ReadOptionalU64(event, "cmd_list_offset_words", commandOffset, error) ||
      commandAddress > std::numeric_limits<uint32_t>::max() ||
      commandOffset > std::numeric_limits<uint32_t>::max()) {
    if (error != nullptr && error->empty()) {
      SetError(error, "Azahar command-list location exceeds 32-bit PICA state");
    }
    return false;
  }
  packet.CommandListAddress = static_cast<uint32_t>(commandAddress);
  packet.CommandListOffsetWords = static_cast<uint32_t>(commandOffset);
  packet.Indexed = event.value("is_indexed", false);
  metadata.DrawMode = event.value("mode", std::string{});
  if (!ReadOptionalU32(event, "triangle_topology", metadata.TriangleTopology,
                       error)) {
    return false;
  }

  const auto snapshot = event.find("register_snapshot");
  if (snapshot == event.end() || !snapshot->is_object() || snapshot->empty()) {
    SetError(error, "Azahar draw lacks a native register snapshot");
    return false;
  }
  std::array<bool, 0x300> assigned{};
  for (const auto &[groupName, group] : snapshot->items()) {
    if (!group.is_object() || !group.contains("first") ||
        !group.contains("last") || !group.contains("values")) {
      SetError(error,
               "Azahar register group '" + groupName + "' is incomplete");
      return false;
    }
    uint32_t first = 0;
    uint32_t last = 0;
    if (!ReadU32(group.at("first"), first) ||
        !ReadU32(group.at("last"), last) || first > last ||
        last >= packet.Registers.size() || !group.at("values").is_array() ||
        group.at("values").size() != last - first + 1U) {
      SetError(error, "Azahar register group '" + groupName +
                          "' has an invalid range");
      return false;
    }
    const auto &values = group.at("values");
    for (uint32_t index = first; index <= last; ++index) {
      if (assigned[index] ||
          !ReadU32(values.at(index - first), packet.Registers[index])) {
        SetError(error, "Azahar register group '" + groupName +
                            "' overlaps or contains an invalid value");
        return false;
      }
      assigned[index] = true;
    }
  }

  if (const auto lightingLuts = event.find("lighting_luts");
      lightingLuts != event.end()) {
    if (!lightingLuts->is_array()) {
      SetError(error, "Azahar lighting LUT state is not an array");
      return false;
    }
    auto state = std::make_shared<Oot3dPicaLightingLutState>();
    std::array<bool, Oot3dPicaLightingLutState::TableCount> assignedTables{};
    for (const auto &table : *lightingLuts) {
      if (!table.is_object() || !table.contains("table") ||
          !table.contains("values")) {
        SetError(error, "Azahar lighting LUT table is incomplete");
        return false;
      }
      uint32_t tableIndex = 0;
      if (!ReadU32(table.at("table"), tableIndex) ||
          tableIndex >= Oot3dPicaLightingLutState::TableCount ||
          assignedTables[tableIndex] || !table.at("values").is_array() ||
          table.at("values").size() !=
              Oot3dPicaLightingLutState::EntryCount) {
        SetError(error, "Azahar lighting LUT table is invalid");
        return false;
      }
      for (size_t entry = 0;
           entry < Oot3dPicaLightingLutState::EntryCount; ++entry) {
        if (!ReadU32(table.at("values").at(entry),
                     state->Entry(tableIndex, entry))) {
          SetError(error, "Azahar lighting LUT entry is invalid");
          return false;
        }
      }
      assignedTables[tableIndex] = true;
    }
    state->ContentHash = ComputeOot3dPicaLightingLutContentHash(*state);
    state->ContentHashAvailable = true;
    packet.LightingLuts = std::move(state);
  }

  if (!ReadShaderUniforms(event, "vs_uniforms", packet.VertexShader, error) ||
      !ReadShaderUniforms(event, "gs_uniforms", packet.GeometryShader, error)) {
    return false;
  }
  const auto identity = event.find("shader_identity");
  if (identity == event.end() || !identity->is_object() ||
      identity->value("format", std::string{}) !=
          "oot3d.azahar.pica_shader_identity.v1") {
    SetError(error, "Azahar draw lacks a supported native shader identity");
    return false;
  }
  if (!ReadShaderIdentityMember(
          *identity, "vertex", true, metadata.VertexProgramHash,
          metadata.VertexSwizzleHash, metadata.VertexEntryPoint,
          metadata.VertexProgramWords, metadata.VertexSwizzleWords, error)) {
    return false;
  }
  const auto geometry = identity->find("geometry");
  metadata.GeometryShaderEnabled = geometry != identity->end() &&
                                   geometry->is_object() &&
                                   geometry->value("enabled", false);
  if (!ReadShaderIdentityMember(
          *identity, "geometry", metadata.GeometryShaderEnabled,
          metadata.GeometryProgramHash, metadata.GeometrySwizzleHash,
          metadata.GeometryEntryPoint, metadata.GeometryProgramWords,
          metadata.GeometrySwizzleWords, error)) {
    return false;
  }
  try {
    metadata.FragmentConfigHash =
        identity->at("fragment_config_hash").get<std::string>();
  } catch (const std::exception &) {
    SetError(error, "Azahar fragment configuration hash is invalid");
    return false;
  }
  return true;
}

std::string
BuildOot3dAzaharPipelineKey(const Oot3dAzaharPicaDrawMetadata &metadata) {
  const nlohmann::json vertex = {
      metadata.VertexProgramHash, metadata.VertexSwizzleHash,
      metadata.VertexEntryPoint, metadata.VertexProgramWords,
      metadata.VertexSwizzleWords};
  nlohmann::json geometry = nullptr;
  if (metadata.GeometryShaderEnabled) {
    geometry = {metadata.GeometryProgramHash, metadata.GeometrySwizzleHash,
                metadata.GeometryEntryPoint, metadata.GeometryProgramWords,
                metadata.GeometrySwizzleWords};
  }
  return nlohmann::json::array({vertex, geometry, metadata.FragmentConfigHash,
                                metadata.TriangleTopology, metadata.DrawMode})
      .dump();
}

} // namespace Oot3dNativeGame
