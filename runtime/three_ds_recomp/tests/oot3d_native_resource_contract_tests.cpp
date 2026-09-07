#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "three_ds_recomp/oot3d/Oot3dNativeFormat.h"
#include "three_ds_recomp/oot3d/Oot3dNativeActorRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"
#include "three_ds_recomp/oot3d/Oot3dNativeFast3dRenderer.h"
#include "three_ds_recomp/oot3d/Oot3dNativeEffectSs.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeResourceContract.h"

namespace {

nlohmann::json ValidContractJson() {
    return {
        { "format", "oot3d_native_resource_contract_v1" },
        { "manifest", "I:/oot3dre_work/standalone_demo/link_house/demo_manifest.json" },
        { "target_engine", "three_ds_recomp_runtime" },
        { "runtime_asset_policy", "oot3d_native_or_directly_offline_derived_only" },
        { "n64_usage_policy", "behavior_baseline_and_delta_reference_only" },
        { "runtime_n64_asset_substitution_allowed", false },
        { "shipwright_runtime_replacement_allowed", false },
        { "resource_count", 4 },
        { "resources",
          nlohmann::json::array({
              {
                  { "id", "link_house_room_visual" },
                  { "role", "room_visual_mesh" },
                  { "source_format", "oot3d_zsi_embedded_cmb" },
                  { "source_path", "E:/oot3d/scene/link_0_info.zsi" },
                  { "runtime_artifact", "room/link_house_room.glb" },
                  { "runtime_format", "offline_glb_cache_from_oot3d_cmb" },
                  { "future_loader_contract", "oot3d.scene.zsi_room_embedded_cmb_to_static_mesh" },
                  { "native_or_directly_derived", true },
                  { "runtime_n64_asset_path", "" },
              },
              {
                  { "id", "link_house_collision" },
                  { "role", "scene_collision" },
                  { "source_format", "oot3d_zsi_native_collision" },
                  { "source_path", "E:/oot3d/scene/link_info.zsi" },
                  { "runtime_artifact", "collision/collision" },
                  { "runtime_format", "offline_xml_cache_from_oot3d_zsi_collision" },
                  { "future_loader_contract", "oot3d.scene.zsi_collision_to_collision_world" },
                  { "native_or_directly_derived", true },
                  { "runtime_n64_asset_path", "" },
              },
              {
                  { "id", "link_child_model_animation" },
                  { "role", "player_model_and_animation" },
                  { "source_format", "oot3d_cmb_plus_csab" },
                  { "source_path", "I:/oot3dre_work/character_conversion/link_child_character_conversion_manifest.json" },
                  { "runtime_artifact", "character/link_child_run.glb" },
                  { "runtime_format", "offline_glb_cache_from_oot3d_cmb_csab" },
                  { "future_loader_contract", "oot3d.character.cmb_csab_to_skinned_player" },
                  { "native_or_directly_derived", true },
                  { "runtime_n64_asset_path", "" },
              },
              {
                  { "id", "link_child_validation_manifest" },
                  { "role", "player_asset_validation" },
                  { "source_format", "oot3d_character_conversion_manifest" },
                  { "source_path", "I:/oot3dre_work/character_conversion/link_child_character_conversion_manifest.json" },
                  { "runtime_artifact", "character/link_child_run.validated.manifest.json" },
                  { "runtime_format", "offline_validation_manifest" },
                  { "future_loader_contract", "oot3d.character.validation_metadata" },
                  { "native_or_directly_derived", true },
                  { "runtime_n64_asset_path", "" },
              },
          }) },
    };
}

void WriteBinary(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    file << text;
}

void PutLe32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void PutLe64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value) {
    PutLe32(bytes, offset, static_cast<uint32_t>(value & 0xFFFFFFFFu));
    PutLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xFFFFFFFFu));
}

void PutLe16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void PutF32(std::vector<uint8_t>& bytes, size_t offset, float value) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(raw));
    PutLe32(bytes, offset, raw);
}

void PutAscii(std::vector<uint8_t>& bytes, size_t offset, std::string_view value) {
    for (size_t i = 0; i < value.size(); ++i) {
        bytes[offset + i] = static_cast<uint8_t>(value[i]);
    }
}

std::vector<uint8_t> MinimalGlbHeader() {
    return {
        'g', 'l', 'T', 'F',
        0x02, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> MinimalZsiHeader() {
    return { 'Z', 'S', 'I', 0x01, 0x00, 0x00, 0x00, 0x00 };
}

std::vector<uint8_t> MinimalCmbHeader(uint32_t version = 6) {
    std::vector<uint8_t> bytes(0x50, 0);
    bytes[0] = 'c';
    bytes[1] = 'm';
    bytes[2] = 'b';
    bytes[3] = ' ';
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, version);
    return bytes;
}

std::vector<uint8_t> MinimalZarHeader() {
    std::vector<uint8_t> bytes(0x18, 0);
    bytes[0] = 'Z';
    bytes[1] = 'A';
    bytes[2] = 'R';
    bytes[3] = 0x01;
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    return bytes;
}

std::vector<uint8_t> MinimalCsabHeader() {
    std::vector<uint8_t> bytes(0x20, 0);
    bytes[0] = 'c';
    bytes[1] = 's';
    bytes[2] = 'a';
    bytes[3] = 'b';
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 5);
    return bytes;
}

std::vector<uint8_t> MinimalFacebTrack() {
    std::vector<uint8_t> bytes(0x14, 0);
    PutAscii(bytes, 0x00, "fkb");
    bytes[0x03] = 0x01;
    PutLe32(bytes, 0x04, 3);
    PutLe16(bytes, 0x08, 0);
    bytes[0x0A] = 0xFF;
    bytes[0x0B] = 0xFF;
    PutLe16(bytes, 0x0C, 15);
    bytes[0x0E] = 1;
    bytes[0x0F] = 0;
    PutLe16(bytes, 0x10, 16);
    bytes[0x12] = 2;
    bytes[0x13] = 1;
    return bytes;
}

std::vector<uint8_t> MinimalCtxbHeader() {
    std::vector<uint8_t> bytes(0x48, 0);
    bytes[0] = 'c';
    bytes[1] = 't';
    bytes[2] = 'x';
    bytes[3] = 'b';
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 1);
    PutLe32(bytes, 0x10, 0x18);
    PutLe32(bytes, 0x14, 0x48);
    bytes[0x18] = 't';
    bytes[0x19] = 'e';
    bytes[0x1A] = 'x';
    bytes[0x1B] = ' ';
    return bytes;
}

std::vector<uint8_t> MinimalCmabScalarTrack() {
    std::vector<uint8_t> bytes(0x80, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0x74);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 900);
    PutLe32(bytes, 0x28, 1);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 12);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 1);
    PutLe32(bytes, 0x50, 20);
    PutLe32(bytes, 0x54, 1);
    PutLe32(bytes, 0x58, 2);
    PutLe32(bytes, 0x60, 900);
    PutLe32(bytes, 0x64, 0);
    PutF32(bytes, 0x68, 0.0f);
    PutLe32(bytes, 0x6C, 900);
    PutF32(bytes, 0x70, -1.0f);

    PutAscii(bytes, 0x74, "strt");
    PutLe32(bytes, 0x78, 0);
    return bytes;
}

std::vector<uint8_t> MinimalCmabComponentScalarTracks() {
    std::vector<uint8_t> bytes(0x9C, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0x94);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 900);
    PutLe32(bytes, 0x28, 1);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 12);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 1);
    PutLe32(bytes, 0x48, 10);
    PutLe32(bytes, 0x4C, 0);
    PutLe32(bytes, 0x50, 0x00340014);
    PutLe32(bytes, 0x54, 1);
    PutLe32(bytes, 0x58, 2);
    PutLe32(bytes, 0x5C, 0);
    PutLe32(bytes, 0x60, 900);
    PutLe32(bytes, 0x64, 0);
    PutF32(bytes, 0x68, 0.0f);
    PutLe32(bytes, 0x6C, 900);
    PutF32(bytes, 0x70, 3.0f);

    PutLe32(bytes, 0x74, 1);
    PutLe32(bytes, 0x78, 2);
    PutLe32(bytes, 0x7C, 0);
    PutLe32(bytes, 0x80, 900);
    PutLe32(bytes, 0x84, 0);
    PutF32(bytes, 0x88, 0.0f);
    PutLe32(bytes, 0x8C, 900);
    PutF32(bytes, 0x90, 2.0f);

    PutAscii(bytes, 0x94, "strt");
    PutLe32(bytes, 0x98, 0);
    return bytes;
}

std::vector<uint8_t> MinimalCmabTransformScalarTrack() {
    std::vector<uint8_t> bytes(0x80, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0x74);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 900);
    PutLe32(bytes, 0x28, 0);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 12);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 5);
    PutLe32(bytes, 0x48, 10);
    PutLe32(bytes, 0x4C, 0);
    PutLe16(bytes, 0x50, 0x14);

    PutLe32(bytes, 0x54, 1);
    PutLe32(bytes, 0x58, 2);
    PutLe32(bytes, 0x5C, 0);
    PutLe32(bytes, 0x60, 900);
    PutLe32(bytes, 0x64, 0);
    PutF32(bytes, 0x68, 0.0f);
    PutLe32(bytes, 0x6C, 900);
    PutF32(bytes, 0x70, 0.5f);

    PutAscii(bytes, 0x74, "strt");
    PutLe32(bytes, 0x78, 0);
    return bytes;
}

std::vector<uint8_t> MinimalCmabMadsRecordOffsetTable() {
    std::vector<uint8_t> bytes(0xD0, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0xCC);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 900);
    PutLe32(bytes, 0x28, 1);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 2);
    PutLe32(bytes, 0x3C, 0x10);
    PutLe32(bytes, 0x40, 0x64);

    PutAscii(bytes, 0x44, "mmad");
    PutLe32(bytes, 0x48, 1);
    PutLe32(bytes, 0x4C, 10);
    PutLe32(bytes, 0x50, 0);
    PutLe32(bytes, 0x54, 0x00340014);
    PutLe32(bytes, 0x58, 1);
    PutLe32(bytes, 0x5C, 2);
    PutLe32(bytes, 0x60, 0);
    PutLe32(bytes, 0x64, 900);
    PutLe32(bytes, 0x68, 0);
    PutF32(bytes, 0x6C, 0.0f);
    PutLe32(bytes, 0x70, 900);
    PutF32(bytes, 0x74, 3.0f);
    PutLe32(bytes, 0x78, 1);
    PutLe32(bytes, 0x7C, 2);
    PutLe32(bytes, 0x80, 0);
    PutLe32(bytes, 0x84, 900);
    PutLe32(bytes, 0x88, 0);
    PutF32(bytes, 0x8C, 0.0f);
    PutLe32(bytes, 0x90, 900);
    PutF32(bytes, 0x94, 2.0f);

    PutAscii(bytes, 0x98, "mmad");
    PutLe32(bytes, 0x9C, 3);
    PutLe32(bytes, 0xA0, 10);
    PutLe32(bytes, 0xA4, 0);
    PutLe32(bytes, 0xA8, 0x14);
    PutLe32(bytes, 0xAC, 1);
    PutLe32(bytes, 0xB0, 2);
    PutLe32(bytes, 0xB4, 0);
    PutLe32(bytes, 0xB8, 900);
    PutLe32(bytes, 0xBC, 0);
    PutF32(bytes, 0xC0, 0.0f);
    PutLe32(bytes, 0xC4, 900);
    PutF32(bytes, 0xC8, 1.0f);

    PutAscii(bytes, 0xCC, "strt");
    return bytes;
}

std::vector<uint8_t> MinimalCmabConstantColor() {
    std::vector<uint8_t> bytes(0x80, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0x78);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 900);
    PutLe32(bytes, 0x28, 0);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 0x0C);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 4);
    PutLe32(bytes, 0x48, 10);
    PutLe16(bytes, 0x4C, 2);
    PutLe16(bytes, 0x50, 0x18);

    PutLe32(bytes, 0x58, 1);
    PutLe32(bytes, 0x5C, 2);
    PutLe32(bytes, 0x60, 0);
    PutLe32(bytes, 0x64, 900);
    PutLe32(bytes, 0x68, 0);
    PutF32(bytes, 0x6C, 0.0f);
    PutLe32(bytes, 0x70, 900);
    PutF32(bytes, 0x74, 0.5f);

    PutAscii(bytes, 0x78, "strt");
    return bytes;
}

std::vector<uint8_t> MinimalCmabTextureSwap() {
    std::vector<uint8_t> bytes(0xD2, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0xB0);
    PutLe32(bytes, 0x1C, 0xD0);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 2);
    PutLe32(bytes, 0x2C, 20);
    PutLe32(bytes, 0x30, 0x64);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 12);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 2);
    PutLe32(bytes, 0x48, 14);
    PutLe32(bytes, 0x50, 20);
    PutLe32(bytes, 0x54, 3);
    PutLe32(bytes, 0x58, 2);
    PutLe32(bytes, 0x60, 1);
    PutLe32(bytes, 0x64, 0);
    PutF32(bytes, 0x68, 0.0f);
    PutLe32(bytes, 0x6C, 1);
    PutF32(bytes, 0x70, 1.0f);

    PutAscii(bytes, 0x74, "txpt");
    PutLe32(bytes, 0x78, 2);
    PutLe32(bytes, 0x7C, 1);
    PutLe16(bytes, 0x80, 1);
    PutLe16(bytes, 0x82, 0xCAFE);
    PutLe16(bytes, 0x84, 1);
    PutLe16(bytes, 0x86, 1);
    PutLe16(bytes, 0x88, 0x6757);
    PutLe16(bytes, 0x8A, 0x1401);
    PutLe32(bytes, 0x8C, 0);
    PutLe32(bytes, 0x90, 0);
    PutLe32(bytes, 0x94, 1);
    PutLe16(bytes, 0x98, 1);
    PutLe16(bytes, 0x9A, 0xCAFE);
    PutLe16(bytes, 0x9C, 1);
    PutLe16(bytes, 0x9E, 1);
    PutLe16(bytes, 0xA0, 0x6757);
    PutLe16(bytes, 0xA2, 0x1401);
    PutLe32(bytes, 0xA4, 1);
    PutLe32(bytes, 0xA8, 0);

    PutAscii(bytes, 0xB0, "strt");
    PutLe32(bytes, 0xB4, 2);
    PutLe32(bytes, 0xB8, 0);
    PutLe32(bytes, 0xBC, 8);
    PutAscii(bytes, 0xC0, "c_eye01");
    PutAscii(bytes, 0xC8, "c_eye02");
    bytes[0xD0] = 0x7F;
    bytes[0xD1] = 0x80;
    return bytes;
}

std::vector<uint8_t> MinimalCmabWrappedHermiteSourceCurve() {
    std::vector<uint8_t> bytes(0x8C, 0);
    PutAscii(bytes, 0x00, "cmab");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x10, 1);
    PutLe32(bytes, 0x14, 0x20);
    PutLe32(bytes, 0x18, 0x84);
    PutLe32(bytes, 0x20, 0xFFFFFFFF);
    PutLe32(bytes, 0x24, 10);
    PutLe32(bytes, 0x28, 1);
    PutLe32(bytes, 0x2C, 20);

    PutAscii(bytes, 0x34, "mads");
    PutLe32(bytes, 0x38, 1);
    PutLe32(bytes, 0x3C, 12);

    PutAscii(bytes, 0x40, "mmad");
    PutLe32(bytes, 0x44, 3);
    PutLe32(bytes, 0x48, 10);
    PutLe32(bytes, 0x4C, 0x00140000);
    PutLe32(bytes, 0x50, 0);

    PutLe32(bytes, 0x54, 2);
    PutLe32(bytes, 0x58, 2);
    PutLe32(bytes, 0x60, 10);
    PutLe32(bytes, 0x64, 0);
    PutF32(bytes, 0x68, 0.0f);
    PutF32(bytes, 0x6C, 0.0f);
    PutF32(bytes, 0x70, 0.0f);
    PutLe32(bytes, 0x74, 10);
    PutF32(bytes, 0x78, 10.0f);
    PutF32(bytes, 0x7C, 0.0f);
    PutF32(bytes, 0x80, 0.0f);

    PutAscii(bytes, 0x84, "strt");
    PutLe32(bytes, 0x88, 0);
    return bytes;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticCmabTargetModel() {
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Source = "actor/zelda_link_child_new.zar!child/model/childlink_v2.cmb";
    model.Name = "childlink_v2";

    ThreeDsRecomp::Oot3d::CmbTexture eyeTexture;
    eyeTexture.Index = 0;
    eyeTexture.Name = "c_eye01";
    model.Textures.push_back(eyeTexture);

    ThreeDsRecomp::Oot3d::CmbTexture bodyTexture;
    bodyTexture.Index = 1;
    bodyTexture.Name = "c_body";
    model.Textures.push_back(bodyTexture);

    model.Materials.resize(16);
    for (uint32_t materialIndex = 0; materialIndex < model.Materials.size(); ++materialIndex) {
        model.Materials[materialIndex].Index = materialIndex;
    }
    model.Materials[14].TextureMappersUsed = 1;
    model.Materials[14].TextureMappers[0].TextureIndex = 0;
    return model;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture SyntheticRenderTexture(std::string name, uint8_t luminance) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
    texture.Name = std::move(name);
    texture.Width = 1;
    texture.Height = 1;
    texture.TextureFormat = 0x6757;
    texture.DataType = 0x1401;
    texture.Rgba8Decoded = true;
    texture.Rgba8 = { luminance, luminance, luminance, luminance };
    return texture;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel SyntheticCmabRenderModel() {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
    model.Source = "actor/zelda_link_child_new.zar!child/model/childlink_v2.cmb";
    model.Name = "childlink_v2";
    model.Textures.push_back(SyntheticRenderTexture("c_eye01", 0x7F));
    model.Textures.push_back(SyntheticRenderTexture("c_body", 0x40));

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
    batch.MaterialIndex = 14;
    batch.Material.Textured = true;
    batch.Material.TextureIndex = 0;
    batch.Material.TextureMapperSlot = 0;
    batch.Material.TextureMapperTextureIndices[0] = 0;
    batch.Material.NativeRuntimeMaterialLaneDecoded = true;
    batch.Material.NativeRuntimeMaterialLaneIndex = 14;
    model.Batches.push_back(std::move(batch));
    return model;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel SyntheticCmabColorRenderModel() {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
    model.Source = "actor/zelda_link_child_new.zar!child/model/childlink_v2.cmb";
    model.Name = "childlink_v2";

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
    batch.MaterialIndex = 10;
    batch.Material.NativeRuntimeMaterialLaneDecoded = true;
    batch.Material.NativeRuntimeMaterialLaneIndex = 10;
    batch.Material.MaterialColorsDecoded = true;
    batch.Material.DiffuseColor = { 10, 20, 30, 40 };
    batch.Material.ConstantColors[2] = { 5, 6, 7, 8 };
    model.Batches.push_back(std::move(batch));
    return model;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel SyntheticCmabTransformRenderModel() {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
    model.Source = "actor/zelda_link_child_new.zar!child/model/childlink_v2.cmb";
    model.Name = "childlink_v2";
    model.Textures.push_back(SyntheticRenderTexture("c_body", 0x40));

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderTextureCoordState coord;
    coord.Slot = 0;
    coord.Active = true;
    coord.SelectedPrimary = true;
    coord.Decoded = true;
    coord.TransformAppliesToUv0 = true;
    coord.Source = "oot3d_cmb_material_texture_coord";

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderVertex vertex;
    vertex.NativeSourceUv0 = { 1.0f, 0.0f };
    vertex.NativeSourceUv0Available = true;
    vertex.Uv0 = vertex.NativeSourceUv0;

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
    batch.MaterialIndex = 10;
    batch.Material.Textured = true;
    batch.Material.TextureIndex = 0;
    batch.Material.TextureMapperSlot = 0;
    batch.Material.TextureMapperTextureIndices[0] = 0;
    batch.Material.NativeRuntimeMaterialLaneDecoded = true;
    batch.Material.NativeRuntimeMaterialLaneIndex = 10;
    batch.Material.TextureCoords.push_back(coord);
    batch.Material.SelectedTextureCoord = coord;
    batch.Material.SelectedTextureCoordDecoded = true;
    batch.Vertices.push_back(vertex);
    model.Batches.push_back(std::move(batch));
    return model;
}

std::vector<uint8_t> MinimalShbinHeader() {
    std::vector<uint8_t> bytes(0xE0, 0);
    constexpr size_t dvlpOffset = 0x0C;
    constexpr size_t dvleOffset = 0x50;

    PutAscii(bytes, 0x00, "DVLB");
    PutLe32(bytes, 0x04, 1);
    PutLe32(bytes, 0x08, static_cast<uint32_t>(dvleOffset));

    PutAscii(bytes, dvlpOffset, "DVLP");
    PutLe32(bytes, dvlpOffset + 0x04, 0x08020000);
    PutLe32(bytes, dvlpOffset + 0x08, 0x1C);
    PutLe32(bytes, dvlpOffset + 0x0C, 2);
    PutLe32(bytes, dvlpOffset + 0x10, 0x24);
    PutLe32(bytes, dvlpOffset + 0x14, 2);
    PutLe32(bytes, dvlpOffset + 0x18, 0x34);
    PutLe32(bytes, dvlpOffset + 0x1C, 0x11111111);
    PutLe32(bytes, dvlpOffset + 0x20, 0x22222222);
    PutLe32(bytes, dvlpOffset + 0x24, 0xAAAA0001);
    PutLe32(bytes, dvlpOffset + 0x28, 0);
    PutLe32(bytes, dvlpOffset + 0x2C, 0xBBBB0002);
    PutLe32(bytes, dvlpOffset + 0x30, 1);
    PutAscii(bytes, dvlpOffset + 0x34, "test.vsh");

    PutAscii(bytes, dvleOffset, "DVLE");
    bytes[dvleOffset + 0x06] = 0;
    PutLe32(bytes, dvleOffset + 0x08, 1);
    PutLe32(bytes, dvleOffset + 0x0C, 2);
    PutLe32(bytes, dvleOffset + 0x18, 0x50);
    PutLe32(bytes, dvleOffset + 0x1C, 1);
    PutLe32(bytes, dvleOffset + 0x28, 0x40);
    PutLe32(bytes, dvleOffset + 0x2C, 2);
    PutLe32(bytes, dvleOffset + 0x30, 0x64);
    PutLe32(bytes, dvleOffset + 0x34, 1);
    PutLe32(bytes, dvleOffset + 0x38, 0x6C);
    PutLe32(bytes, dvleOffset + 0x3C, 0x20);

    PutLe64(bytes, dvleOffset + 0x40, 3ull | (2ull << 16) | (7ull << 32));
    PutLe64(bytes, dvleOffset + 0x48, 4ull | (2ull << 16) | (4ull << 32));
    PutLe32(bytes, dvleOffset + 0x50, 2 | (5u << 16));
    PutLe32(bytes, dvleOffset + 0x54, 0x01020304);
    PutLe32(bytes, dvleOffset + 0x58, 0x11121314);
    PutLe32(bytes, dvleOffset + 0x5C, 0x21222324);
    PutLe32(bytes, dvleOffset + 0x60, 0x31323334);
    PutLe32(bytes, dvleOffset + 0x64, 0);
    PutLe32(bytes, dvleOffset + 0x68, 0x00120010);
    PutAscii(bytes, dvleOffset + 0x6C, "u_modelview");

    return bytes;
}

std::vector<uint8_t> MinimalLuminanceCtxbTexture() {
    constexpr uint16_t width = 8;
    constexpr uint16_t height = 8;
    constexpr uint32_t payloadSize = width * height;
    std::vector<uint8_t> bytes(0x48 + payloadSize, 0);
    PutAscii(bytes, 0x00, "ctxb");
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 1);
    PutLe32(bytes, 0x10, 0x18);
    PutLe32(bytes, 0x14, 0x48);
    PutAscii(bytes, 0x18, "tex ");
    PutLe32(bytes, 0x1C, 0x30);
    PutLe32(bytes, 0x20, 1);
    PutLe32(bytes, 0x24, payloadSize);
    PutLe32(bytes, 0x28, 0x00001234);
    PutLe16(bytes, 0x2C, width);
    PutLe16(bytes, 0x2E, height);
    PutLe16(bytes, 0x30, 0x6757);
    PutLe16(bytes, 0x32, 0x1401);
    for (uint32_t i = 0; i < payloadSize; ++i) {
        bytes[0x48 + i] = static_cast<uint8_t>(i * 3);
    }
    return bytes;
}

std::vector<uint8_t> MinimalTriangleCmb() {
    constexpr size_t sklOff = 0x50;
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t texOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t sklmOff = texOff + 0x10;
    constexpr size_t mshsOff = sklmOff + 0x10;
    constexpr size_t shpOff = sklmOff + 0x30;
    constexpr size_t sepdOff = shpOff + 0x20;
    constexpr size_t prmsOff = sepdOff + 0x120;
    constexpr size_t prmOff = prmsOff + 0x24;
    constexpr size_t lutsOff = prmOff + 0x40;
    constexpr size_t vatrOff = lutsOff + 0x44;
    constexpr size_t positionDataOff = vatrOff + 0x40;
    constexpr size_t indicesOff = positionDataOff + 0x30;
    constexpr size_t fileSize = indicesOff + 0x10;
    std::vector<uint8_t> bytes(fileSize, 0);

    PutAscii(bytes, 0x00, "cmb ");
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 6);
    PutAscii(bytes, 0x10, "tri");
    PutLe32(bytes, 0x20, 3);
    PutLe32(bytes, 0x24, sklOff);
    PutLe32(bytes, 0x28, matsOff);
    PutLe32(bytes, 0x2C, texOff);
    PutLe32(bytes, 0x30, sklmOff);
    PutLe32(bytes, 0x34, lutsOff);
    PutLe32(bytes, 0x38, vatrOff);
    PutLe32(bytes, 0x3C, indicesOff);
    PutLe32(bytes, 0x40, fileSize);

    PutAscii(bytes, sklOff, "skl ");
    PutLe32(bytes, sklOff + 0x04, 0x38);
    PutLe32(bytes, sklOff + 0x08, 1);
    PutLe16(bytes, sklOff + 0x10, 0);
    PutLe16(bytes, sklOff + 0x12, 0xFFFF);
    PutF32(bytes, sklOff + 0x14, 1.0f);
    PutF32(bytes, sklOff + 0x18, 1.0f);
    PutF32(bytes, sklOff + 0x1C, 1.0f);

    PutAscii(bytes, matsOff, "mats");
    PutLe32(bytes, matsOff + 0x08, 1);
    bytes[materialOff + 0x04] = 3;
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x10, 0x84C2);
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x12, 0x62C9);
    bytes[materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x14] = 1;
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x18, 0x62B7);
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x1C, 0x62C3);
    bytes[materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x1E] = 1;
    bytes[materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x20] = 1;
    bytes[materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x23] = 1;
    bytes[materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x24] = 1;
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x26, 0x62A5);
    PutF32(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset + 0x28, 0.25f);
    PutAscii(bytes, texOff, "tex ");
    PutLe32(bytes, texOff + 0x08, 0);

    PutAscii(bytes, sklmOff, "sklm");
    PutLe32(bytes, sklmOff + 0x08, static_cast<uint32_t>(mshsOff - sklmOff));
    PutLe32(bytes, sklmOff + 0x0C, static_cast<uint32_t>(shpOff - sklmOff));
    PutAscii(bytes, mshsOff, "mshs");
    PutLe32(bytes, mshsOff + 0x08, 1);
    PutLe16(bytes, mshsOff + 0x0C, 1);
    PutLe16(bytes, mshsOff + 0x10, 0);

    PutAscii(bytes, shpOff, "shp ");
    PutLe32(bytes, shpOff + 0x08, 1);
    PutLe16(bytes, shpOff + 0x10, static_cast<uint16_t>(sepdOff - shpOff));

    PutAscii(bytes, sepdOff, "sepd");
    PutLe16(bytes, sepdOff + 0x08, 1);
    PutLe16(bytes, sepdOff + 0x0A, 0x01);
    PutLe32(bytes, sepdOff + 0x24, 0);
    PutF32(bytes, sepdOff + 0x28, 1.0f);
    PutLe16(bytes, sepdOff + 0x2C, 0x1406);
    PutLe16(bytes, sepdOff + 0x2E, 0);
    PutLe16(bytes, sepdOff + 0x108, static_cast<uint16_t>(prmsOff - sepdOff));

    PutAscii(bytes, prmsOff, "prms");
    PutLe32(bytes, prmsOff + 0x08, 1);
    PutLe16(bytes, prmsOff + 0x0C, 0);
    PutLe16(bytes, prmsOff + 0x0E, 1);
    PutLe32(bytes, prmsOff + 0x10, 0x20);
    PutLe32(bytes, prmsOff + 0x14, 0x24);
    PutLe16(bytes, prmsOff + 0x20, 0);
    PutAscii(bytes, prmOff, "prm ");
    PutLe32(bytes, prmOff + 0x08, 1);
    PutLe32(bytes, prmOff + 0x0C, 0);
    PutLe16(bytes, prmOff + 0x10, 0x1403);
    PutLe16(bytes, prmOff + 0x14, 3);
    PutLe16(bytes, prmOff + 0x16, 0);

    PutAscii(bytes, lutsOff, "luts");
    PutLe32(bytes, lutsOff + 0x04, 0x44);
    PutLe32(bytes, lutsOff + 0x08, 1);
    PutLe32(bytes, lutsOff + 0x0C, 0);
    PutLe32(bytes, lutsOff + 0x10, 0x14);
    bytes[lutsOff + 0x14] = 2;
    bytes[lutsOff + 0x15] = 1;
    bytes[lutsOff + 0x16] = 1;
    PutLe32(bytes, lutsOff + 0x18, 2);
    PutLe32(bytes, lutsOff + 0x1C, 0);
    PutLe32(bytes, lutsOff + 0x20, 0xFF);
    PutLe32(bytes, lutsOff + 0x24, 0);
    PutF32(bytes, lutsOff + 0x28, 0.0f);
    PutF32(bytes, lutsOff + 0x2C, 0.0f);
    PutF32(bytes, lutsOff + 0x30, 0.0f);
    PutLe32(bytes, lutsOff + 0x34, 255);
    PutF32(bytes, lutsOff + 0x38, 1.0f);
    PutF32(bytes, lutsOff + 0x3C, 0.0f);
    PutF32(bytes, lutsOff + 0x40, 0.0f);

    PutAscii(bytes, vatrOff, "vatr");
    PutLe32(bytes, vatrOff + 0x0C, 36);
    PutLe32(bytes, vatrOff + 0x10, static_cast<uint32_t>(positionDataOff - vatrOff));
    PutF32(bytes, positionDataOff + 0x00, 0.0f);
    PutF32(bytes, positionDataOff + 0x04, 0.0f);
    PutF32(bytes, positionDataOff + 0x08, 0.0f);
    PutF32(bytes, positionDataOff + 0x0C, 1.0f);
    PutF32(bytes, positionDataOff + 0x10, 0.0f);
    PutF32(bytes, positionDataOff + 0x14, 0.0f);
    PutF32(bytes, positionDataOff + 0x18, 0.0f);
    PutF32(bytes, positionDataOff + 0x1C, 1.0f);
    PutF32(bytes, positionDataOff + 0x20, 0.0f);

    PutLe16(bytes, indicesOff + 0, 0);
    PutLe16(bytes, indicesOff + 2, 1);
    PutLe16(bytes, indicesOff + 4, 2);
    return bytes;
}

std::vector<uint8_t> MinimalTriangleCmbWithMipmappedTexture() {
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t texOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t oldSklmOff = texOff + 0x10;
    constexpr size_t texEntryGrowth = 0x20;
    constexpr uint32_t baseEncodedSize = 16 * 16;
    constexpr uint32_t mip1EncodedSize = 8 * 8;
    constexpr uint32_t textureDataSize = baseEncodedSize + mip1EncodedSize;

    auto bytes = MinimalTriangleCmb();
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(oldSklmOff), texEntryGrowth, 0);
    const uint32_t textureDataOffset = static_cast<uint32_t>(bytes.size());
    bytes.resize(bytes.size() + textureDataSize, 0);

    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x30, static_cast<uint32_t>(oldSklmOff + texEntryGrowth));
    PutLe32(bytes, 0x34, 0x3D4 + texEntryGrowth);
    PutLe32(bytes, 0x38, 0x418 + texEntryGrowth);
    PutLe32(bytes, 0x3C, 0x488 + texEntryGrowth);
    PutLe32(bytes, 0x40, textureDataOffset);

    PutLe32(bytes, materialOff + 0x08, 1);
    PutLe32(bytes, materialOff + 0x0C, 1);
    PutLe16(bytes, materialOff + 0x10, 0);
    PutLe16(bytes, materialOff + 0x14, 0x2703);
    PutLe16(bytes, materialOff + 0x16, 0x2601);
    PutLe16(bytes, materialOff + 0x18, 0x2901);
    PutLe16(bytes, materialOff + 0x1A, 0x2901);
    PutF32(bytes, materialOff + 0x20, -1.4f);
    PutF32(bytes, materialOff + 0x5C, 1.0f);
    PutF32(bytes, materialOff + 0x60, 1.0f);

    PutLe32(bytes, texOff + 0x08, 1);
    const size_t entry = texOff + 0x0C;
    PutLe32(bytes, entry + 0x00, textureDataSize);
    PutLe16(bytes, entry + 0x04, 2);
    PutLe16(bytes, entry + 0x06, 0xCAFE);
    PutLe16(bytes, entry + 0x08, 16);
    PutLe16(bytes, entry + 0x0A, 16);
    PutLe16(bytes, entry + 0x0C, 0x6757);
    PutLe16(bytes, entry + 0x0E, 0x1401);
    PutLe32(bytes, entry + 0x10, 0);
    PutAscii(bytes, entry + 0x14, "mip_l8");

    std::fill_n(bytes.begin() + textureDataOffset, baseEncodedSize, 0x44);
    std::fill_n(bytes.begin() + textureDataOffset + baseEncodedSize, mip1EncodedSize, 0x88);
    return bytes;
}

std::vector<uint8_t> MinimalTriangleCmbWithTextureEnvTail() {
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t oldTexOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t tailSize = ThreeDsRecomp::Oot3d::kOot3dCmbMaterialTextureEnvSize;
    auto bytes = MinimalTriangleCmb();
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(oldTexOff), tailSize, 0);

    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x2C, static_cast<uint32_t>(oldTexOff + tailSize));
    PutLe32(bytes, 0x30, static_cast<uint32_t>(oldTexOff + 0x10 + tailSize));
    PutLe32(bytes, 0x34, static_cast<uint32_t>(0x3D4 + tailSize));
    PutLe32(bytes, 0x38, static_cast<uint32_t>(0x418 + tailSize));
    PutLe32(bytes, 0x3C, static_cast<uint32_t>(0x488 + tailSize));
    PutLe32(bytes, 0x40, static_cast<uint32_t>(bytes.size()));

    PutLe32(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialRawTextureStageCountOffset, 1);
    PutLe16(bytes, materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialRawTextureStageIndexOffset, 0);
    PutLe16(bytes, oldTexOff + 0x00, 0x2100);
    PutLe16(bytes, oldTexOff + 0x04, 1);
    PutLe16(bytes, oldTexOff + 0x06, 1);
    PutLe16(bytes, oldTexOff + 0x0C, 0x8577);
    PutLe16(bytes, oldTexOff + 0x0E, 0x84C0);
    PutLe16(bytes, oldTexOff + 0x12, 0x0300);
    PutLe16(bytes, oldTexOff + 0x14, 0x0300);
    return bytes;
}

std::vector<uint8_t> MinimalTriangleCmbWithConstantVertexColor() {
    auto bytes = MinimalTriangleCmb();
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t texOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t sklmOff = texOff + 0x10;
    constexpr size_t shpOff = sklmOff + 0x30;
    constexpr size_t sepdOff = shpOff + 0x20;
    constexpr size_t colorListOff = sepdOff + 0x5C;

    PutLe16(bytes, sepdOff + 0x0A, 0x01 | ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor);
    PutLe16(bytes, sepdOff + 0x106, ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor);
    PutF32(bytes, colorListOff + 0x04, 1.0f);
    PutLe16(bytes, colorListOff + 0x08, 0x1406);
    PutLe16(bytes, colorListOff + 0x0A, 1);
    PutF32(bytes, colorListOff + 0x0C, 0.25f);
    PutF32(bytes, colorListOff + 0x10, 0.50f);
    PutF32(bytes, colorListOff + 0x14, 0.75f);
    PutF32(bytes, colorListOff + 0x18, 1.0f);
    return bytes;
}

TEST(Oot3dNativeAssets, AdvancesNativeEffectSsDustLcgSpawnAndAtlasState) {
    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsDustProfile profile;
    profile.Decoded = true;
    profile.TextureLoaded = true;
    profile.RandomInitialState = 1;
    profile.RandomMultiplier = 1664525;
    profile.RandomIncrement = 1013904223;
    profile.PrimaryColor = { 170, 130, 90, 255 };
    profile.EnvironmentColor = { 100, 60, 20, 255 };
    profile.ColorRandomRange = 20.0f;
    profile.ColorRandomOffset = 10.0f;
    profile.AccelerationRandomSpan = 0.4f;
    profile.AccelerationRandomCenter = 0.5f;
    profile.UpdateRateScale = 1.0f / 3.0f;
    profile.GlobalUpdateRate = 2;
    profile.TextureFrameCount = 16;

    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsDustSpawnProfile spawn;
    spawn.Decoded = true;
    spawn.Flags = 5;
    spawn.ScaleBase = 200;
    spawn.ScaleRandomRange = 100.0f;
    spawn.ScaleStepBase = 30;
    spawn.ScaleStepRandomRange = 10.0f;
    spawn.DurationBase = 30;
    spawn.DurationRandomRange = 20.0f;
    spawn.DurationToLifeScale = 3.0f;
    spawn.DurationToLifeBias = 0.5f;

    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsRuntime runtime;
    ThreeDsRecomp::Oot3d::ResetOot3dNativeEffectSsRuntime(runtime, profile);
    ThreeDsRecomp::Oot3d::SpawnOot3dNativeEffectSsDust(runtime, spawn, { 1.0f, 2.0f, 3.0f });

    ASSERT_EQ(runtime.Particles.size(), 1u);
    EXPECT_EQ(runtime.Particles[0].Scale, 223);
    EXPECT_EQ(runtime.Particles[0].ScaleStep, 33);
    EXPECT_EQ(runtime.Particles[0].InitialLife, 60);
    EXPECT_EQ(runtime.Particles[0].Flags, 5u);
    EXPECT_EQ(runtime.Particles[0].PrimaryColor.R, 174u);
    EXPECT_EQ(runtime.Particles[0].PrimaryColor.G, 134u);
    EXPECT_EQ(runtime.Particles[0].PrimaryColor.B, 94u);
    ThreeDsRecomp::Oot3d::UpdateOot3dNativeEffectSs(runtime);
    ASSERT_EQ(runtime.Particles.size(), 1u);
    EXPECT_FLOAT_EQ(runtime.Particles[0].Position.X, 1.0f);
    EXPECT_FLOAT_EQ(runtime.Particles[0].Position.Y, 3.0f);
    EXPECT_FLOAT_EQ(runtime.Particles[0].Position.Z, 3.0f);
    EXPECT_EQ(runtime.Particles[0].RemainingLife, 59);
    EXPECT_EQ(runtime.Particles[0].TextureFrame, 0u);
    EXPECT_EQ(runtime.Particles[0].Scale, 245);
    EXPECT_GE(runtime.Particles[0].Acceleration.X, -0.134f);
    EXPECT_LE(runtime.Particles[0].Acceleration.X, 0.134f);
    EXPECT_GE(runtime.Particles[0].Acceleration.Z, -0.134f);
    EXPECT_LE(runtime.Particles[0].Acceleration.Z, 0.134f);

    for (int tick = 1; tick < 60; ++tick) {
        ThreeDsRecomp::Oot3d::UpdateOot3dNativeEffectSs(runtime);
    }
    EXPECT_TRUE(runtime.Particles.empty());
}

TEST(Oot3dNativeAssets, MapsNativeHorseDustStackArgumentsByArmAbi) {
    const auto layout = ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsEuCodeLayout();

    EXPECT_EQ(layout.SpriteTemplatePositionPointerLiteralAddress, 0x00348F18u);
    EXPECT_EQ(layout.GlobalUpdateRateInstructionAddress, 0x00416FF0u);
    EXPECT_EQ(layout.HorseDustScaleRandomRangeAddress, 0x0014B304u);
    EXPECT_EQ(layout.HorseDustScaleBaseInstructionAddress, 0x0014BC6Cu);
    EXPECT_EQ(layout.HorseDustScaleStepRandomRangeAddress, 0x0014BCCCu);
    EXPECT_EQ(layout.HorseDustScaleStepBaseInstructionAddress, 0x0014BC4Cu);
    EXPECT_EQ(layout.HorseDustDurationRandomRangeAddress, 0x0014B788u);
    EXPECT_EQ(layout.HorseDustDurationBaseInstructionAddress, 0x0014BC34u);
}

TEST(Oot3dNativeAssets, MaterializesNativeEffectSsDustPicaMaterialAndAtlasCell) {
    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsDustProfile profile;
    profile.Decoded = true;
    profile.TextureLoaded = true;
    profile.ObjectArchivePath = "actor/zelda_keep.zar";
    profile.TextureName = "soft/tex/soft_smoke.ctxb";
    profile.PrimaryColor = { 170, 130, 90, 255 };
    profile.EnvironmentColor = { 100, 60, 20, 255 };
    profile.GlobalEnvironmentColorBias = 30;
    profile.GlobalEnvironmentColorNormalizeScale = 1.0f / 255.0f;
    profile.GlobalEnvironmentColorClamp = 255.0f;
    profile.SceneSkyboxDefaultColorScale = 1.0f;
    profile.SceneSkyboxByteColorScale = 1.0f / 255.0f;
    profile.RenderScale = 0.1f;
    profile.SpriteTemplateHalfExtent = 0.5f;
    profile.AtlasColumns = 4;
    profile.AtlasRows = 4;
    profile.TextureFrameCount = 16;
    profile.FadeTickCount = 4;
    profile.SamplerMinFilter = 0x2601;
    profile.SamplerMagFilter = 0x2601;
    profile.SamplerWrapS = 0x812F;
    profile.SamplerWrapT = 0x812F;
    profile.Texture.Name = profile.TextureName;
    profile.Texture.Width = 64;
    profile.Texture.Height = 64;
    profile.Texture.Rgba8Decoded = true;
    profile.Texture.HasNativeAlpha = true;
    profile.Texture.Rgba8 = { 255, 255, 255, 255 };

    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsRuntime runtime;
    runtime.DustProfile = profile;
    ThreeDsRecomp::Oot3d::Oot3dNativeEffectSsParticle particle;
    particle.Position = { 10.0f, 20.0f, 30.0f };
    particle.Scale = 200;
    particle.InitialLife = 30;
    particle.RemainingLife = 30;
    particle.TextureFrame = 5;
    particle.Flags = 1;
    particle.PrimaryColor = profile.PrimaryColor;
    particle.EnvironmentColor = profile.EnvironmentColor;
    runtime.Particles.push_back(particle);

    const auto environment = ThreeDsRecomp::Oot3d::ResolveOot3dNativeEffectSsRenderEnvironment(
        profile, { 72, 83, 90, 255 }, 0u);
    const auto model = ThreeDsRecomp::Oot3d::MaterializeOot3dNativeEffectSsDust(
        runtime, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, &environment);
    ASSERT_EQ(model.Textures.size(), 1u);
    ASSERT_EQ(model.Batches.size(), 1u);
    const auto& batch = model.Batches[0];
    ASSERT_EQ(batch.Vertices.size(), 6u);
    EXPECT_TRUE(batch.Material.Textured);
    EXPECT_TRUE(batch.Material.NativeSamplerStateDecoded);
    EXPECT_EQ(batch.Material.NativeSamplerMinFilter, 0x2601u);
    EXPECT_TRUE(batch.Material.NativeBlendStateSupported);
    EXPECT_EQ(batch.Material.BlendSrc, 0x0302u);
    EXPECT_EQ(batch.Material.BlendDst, 0x0303u);
    EXPECT_TRUE(batch.Material.DepthTest);
    EXPECT_FALSE(batch.Material.DepthWrite);
    EXPECT_TRUE(batch.Material.NativePicaFogOverrideDecoded);
    EXPECT_FALSE(batch.Material.NativePicaFogEnabled);
    EXPECT_EQ(batch.Material.NativePicaFogOverrideSource,
              "oot3d_EffectSs_type0_sprite_draw_path_without_scene_fog");
    EXPECT_TRUE(batch.Material.TextureEnvProgram.TextureColorAddendResolved);
    EXPECT_EQ(batch.Material.TextureEnvProgram.TextureColorAddend.R, 40u);
    EXPECT_EQ(batch.Material.TextureEnvProgram.TextureColorAddend.G, 26u);
    EXPECT_EQ(batch.Material.TextureEnvProgram.TextureColorAddend.B, 9u);
    EXPECT_TRUE(batch.Material.TextureEnvProgram.TextureColorAddUsesTexture0Alpha);
    EXPECT_TRUE(batch.Material.TextureEnvProgram.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_EQ(batch.Vertices[0].Color.R, 28u);
    EXPECT_EQ(batch.Vertices[0].Color.G, 31u);
    EXPECT_EQ(batch.Vertices[0].Color.B, 32u);
    EXPECT_FLOAT_EQ(batch.Vertices[0].Uv0.X, 0.25f);
    EXPECT_FLOAT_EQ(batch.Vertices[0].Uv0.Y, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices[1].Uv0.Y, 0.75f);
    EXPECT_FLOAT_EQ(batch.Vertices[1].NativeSourceUv0.Y, 0.25f);
    EXPECT_FLOAT_EQ(batch.Vertices[2].Uv0.X, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices[2].Uv0.Y, 0.5f);
    EXPECT_FLOAT_EQ(batch.Vertices[0].Position.X, 0.0f);
    EXPECT_FLOAT_EQ(batch.Vertices[0].Position.Y, 10.0f);
    EXPECT_FLOAT_EQ(batch.Vertices[2].Position.X, 20.0f);
    EXPECT_FLOAT_EQ(batch.Vertices[2].Position.Y, 10.0f);

    particle.Flags = 0;
    runtime.Particles.push_back(particle);
    const auto mixedFlagModel = ThreeDsRecomp::Oot3d::MaterializeOot3dNativeEffectSsDust(
        runtime, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, &environment);
    ASSERT_EQ(mixedFlagModel.Batches.size(), 2u);
    EXPECT_EQ(mixedFlagModel.Batches[0].Material.TextureEnvProgram.TextureColorAddend.R, 100u);
    EXPECT_EQ(mixedFlagModel.Batches[0].Vertices[0].Color.R, 70u);
    EXPECT_EQ(mixedFlagModel.Batches[1].Material.TextureEnvProgram.TextureColorAddend.R, 40u);
    EXPECT_EQ(mixedFlagModel.Batches[1].Vertices[0].Color.R, 28u);
}

TEST(Oot3dNativeAssets, SelectsDecodedHorseDustContactWindowByAnimation) {
    ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustEmitterProfile profile;
    profile.Decoded = true;
    profile.GallopAnimationIndices = { 7, 9 };
    profile.Contacts[0] = { 1, 13, 14.0f, 16.0f, 5.0f };
    profile.Contacts[1] = { 2, 6, 8.0f, 10.0f, 10.0f };
    profile.Contacts[2] = { 4, 22, 1.0f, 3.0f, 10.0f };
    profile.Contacts[3] = { 8, 18, 26.0f, 28.0f, 10.0f };

    EXPECT_EQ(ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustContactAtFrame(profile, 7, 9.0f),
              &profile.Contacts[1]);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustContactAtFrame(profile, 9, 27.0f),
              &profile.Contacts[3]);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustContactAtFrame(profile, 5, 9.0f),
              nullptr);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustContactAtFrame(profile, 7, 8.0f),
              nullptr);
}

std::vector<uint8_t> MinimalTriangleCmbWithConstantVertexNormal() {
    auto bytes = MinimalTriangleCmb();
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t texOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t sklmOff = texOff + 0x10;
    constexpr size_t shpOff = sklmOff + 0x30;
    constexpr size_t sepdOff = shpOff + 0x20;
    constexpr size_t normalListOff = sepdOff + 0x40;

    PutLe16(bytes, sepdOff + 0x0A, 0x01 | ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal);
    PutLe16(bytes, sepdOff + 0x106, ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal);
    PutF32(bytes, normalListOff + 0x04, 1.0f);
    PutLe16(bytes, normalListOff + 0x08, 0x1406);
    PutLe16(bytes, normalListOff + 0x0A, 1);
    PutF32(bytes, normalListOff + 0x0C, 0.0f);
    PutF32(bytes, normalListOff + 0x10, 1.0f);
    PutF32(bytes, normalListOff + 0x14, 0.0f);
    return bytes;
}

std::vector<uint8_t> MinimalTriangleCmbWithFlag3AndNoBump() {
    auto bytes = MinimalTriangleCmb();
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t blockOff =
        materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset;
    PutLe16(bytes, blockOff + 0x12, 0x62C8);
    bytes[blockOff + 0x1F] = 1;
    return bytes;
}

ThreeDsRecomp::Oot3d::CmbMaterialTextureEnvSetting TextureEnvStage(
    uint32_t index, uint16_t combineRgb, uint16_t colorScale,
    std::array<uint16_t, 3> sourceRgb, std::array<uint16_t, 3> operandRgb) {
    ThreeDsRecomp::Oot3d::CmbMaterialTextureEnvSetting stage;
    stage.Index = index;
    stage.Decoded = true;
    stage.RawTextureEnv.resize(ThreeDsRecomp::Oot3d::kOot3dCmbMaterialTextureEnvSize);
    stage.CombineRgb = combineRgb;
    stage.CombineAlpha = 0x2100;
    stage.ColorScale = colorScale;
    stage.AlphaScale = 1;
    stage.SourceRgb = sourceRgb;
    stage.OperandRgb = operandRgb;
    stage.SourceAlpha = { 0x8577, 0x84C0, 0x8576 };
    stage.OperandAlpha = { 0x0302, 0x0302, 0x0302 };
    return stage;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTextureEnvReplacePreviousModel() {
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Source = "synthetic_texenv_replace_previous.cmb";
    model.Name = "synthetic_texenv_replace_previous";

    ThreeDsRecomp::Oot3d::CmbTexture texture;
    texture.Index = 0;
    texture.Name = "white";
    texture.Width = 1;
    texture.Height = 1;
    texture.TextureFormat = 0x6752;
    texture.DataType = 0x1401;
    texture.Rgba8Decoded = true;
    texture.Rgba8 = { 255, 255, 255, 255 };
    model.Textures.push_back(texture);

    auto modulateStage = TextureEnvStage(
        0, 0x2100, 2, { 0x8577, 0x84C0, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    auto replacePreviousStage = TextureEnvStage(
        1, 0x1E01, 1, { 0x8578, 0x6210, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    model.TextureEnvSettings = { modulateStage, replacePreviousStage };

    ThreeDsRecomp::Oot3d::CmbMaterial material;
    material.Index = 0;
    material.TextureMappersUsed = 1;
    material.TextureCoordsUsed = 1;
    material.RawTextureStageSelectorDecoded = true;
    material.RawTextureStageCount = 2;
    material.RawTextureStageSlots = { 0, 1, -1, -1, -1, -1 };
    material.TextureMappers[0].TextureIndex = 0;
    material.TextureCoords[0].Scale = { 1.0f, 1.0f };
    material.TextureEnvStages = { modulateStage, replacePreviousStage };
    model.Materials.push_back(material);

    ThreeDsRecomp::Oot3d::CmbMesh mesh;
    mesh.Index = 0;
    mesh.ShapeIndex = 0;
    mesh.MaterialIndex = 0;
    model.Meshes.push_back(mesh);

    ThreeDsRecomp::Oot3d::CmbShape shape;
    shape.Index = 0;
    shape.Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition | ThreeDsRecomp::Oot3d::kOot3dCmbAttributeUv0;
    shape.Positions = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
    shape.Uv0 = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    ThreeDsRecomp::Oot3d::CmbPrimitive primitive;
    primitive.Indices = { 0, 1, 2 };
    shape.Primitives.push_back(primitive);
    model.Shapes.push_back(shape);

    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticUnlitTextureEnvVertexColorModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_unlit_texenv_vertex_color.cmb";
    model.Name = "synthetic_unlit_texenv_vertex_color";
    model.Shapes[0].Flags |= ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor;
    model.Shapes[0].Colors = {
        { 64, 128, 192, 255 },
        { 96, 144, 208, 255 },
        { 128, 160, 224, 255 },
    };
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticNormalMappedTextureCoordModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_normal_mapped_texture_coord.cmb";
    model.Name = "synthetic_normal_mapped_texture_coord";
    model.Materials[0].TextureCoords[0].MappingMethod = 3;
    model.Materials[0].TextureCoords[0].Scale = { 2.0f, 3.0f };
    model.Materials[0].TextureCoords[0].Translation = { 0.25f, -0.5f };
    model.Shapes[0].Flags |= ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal;
    model.Shapes[0].Normals = {
        { -0.6f, 0.8f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.6f, -0.8f, 0.0f },
    };
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexture0Texture1AddMultiplyConstantModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture0_texture1_add_multiply_constant.cmb";
    model.Name = "synthetic_texture0_texture1_add_multiply_constant";

    auto texture1 = model.Textures[0];
    texture1.Index = 1;
    texture1.Name = "secondary";
    model.Textures.push_back(texture1);

    auto stage0 = TextureEnvStage(
        0, 0x6402, 1, { 0x84C0, 0x84C1, 0x84C0 },
        { 0x0300, 0x0300, 0x0300 });
    stage0.CombineAlpha = 0x2100;
    stage0.SourceAlpha = { 0x8577, 0x84C0, 0x8576 };
    stage0.UnknownUshort2[0] = 0;

    auto stage1 = TextureEnvStage(
        1, 0x2100, 2, { 0x8578, 0x8576, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    stage1.CombineAlpha = 0x1E01;
    stage1.SourceAlpha = { 0x8578, 0x8576, 0x8576 };
    stage1.UnknownUshort2[0] = 0;

    auto stage2 = TextureEnvStage(
        2, 0x1E01, 1, { 0x8578, 0x8576, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    stage2.CombineAlpha = 0x2100;
    stage2.SourceAlpha = { 0x8578, 0x8576, 0x8576 };
    stage2.UnknownUshort2[0] = 5;

    model.TextureEnvSettings = { stage0, stage1, stage2 };
    auto& material = model.Materials[0];
    material.TextureMappersUsed = 2;
    material.TextureCoordsUsed = 2;
    material.TextureMappers[1].TextureIndex = 1;
    material.TextureCoords[1].Scale = { 3.0f, 2.0f };
    material.ConstantColors[0] = { 204, 110, 0, 255 };
    material.ConstantColors[5] = { 255, 255, 255, 179 };
    material.TextureEnvStages = { stage0, stage1, stage2 };
    material.TextureEnv = stage0;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexture0Texture1AddThenPrimaryColorModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture0_texture1_add_then_primary_color.cmb";
    model.Name = "synthetic_texture0_texture1_add_then_primary_color";

    auto texture1 = model.Textures[0];
    texture1.Index = 1;
    texture1.Name = "secondary";
    model.Textures.push_back(texture1);

    auto stage0 = TextureEnvStage(
        0, 0x0104, 1, { 0x84C0, 0x84C1, 0x84C1 },
        { 0x0300, 0x0300, 0x0300 });
    stage0.CombineAlpha = 0x2100;
    stage0.SourceAlpha = { 0x8577, 0x84C0, 0x8576 };

    auto stage1 = TextureEnvStage(
        1, 0x2100, 1, { 0x8578, 0x8577, 0x84C0 },
        { 0x0300, 0x0300, 0x0300 });
    stage1.CombineAlpha = 0x1E01;
    stage1.SourceAlpha = { 0x8578, 0x84C0, 0x84C0 };

    auto stage2 = TextureEnvStage(
        2, 0x1E01, 1, { 0x8578, 0x8576, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    stage2.CombineAlpha = 0x2100;
    stage2.SourceAlpha = { 0x8578, 0x8576, 0x8576 };
    stage2.UnknownUshort2[0] = 5;

    model.TextureEnvSettings = { stage0, stage1, stage2 };
    auto& material = model.Materials[0];
    material.TextureMappersUsed = 2;
    material.TextureCoordsUsed = 2;
    material.RawTextureStageCount = 3;
    material.RawTextureStageSlots = { 0, 1, 2, -1, -1, -1 };
    material.TextureMappers[1].TextureIndex = 1;
    material.TextureCoords[1].Scale = { 1.0f, 1.0f };
    material.ConstantColors[5] = { 255, 255, 255, 179 };
    material.TextureEnvStages = { stage0, stage1, stage2 };
    material.TextureEnv = stage0;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexture0Texture1AddWithPreviousConstantAlphaModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture0_texture1_add_previous_constant_alpha.cmb";
    model.Name = "synthetic_texture0_texture1_add_previous_constant_alpha";

    auto texture1 = model.Textures[0];
    texture1.Index = 1;
    texture1.Name = "secondary";
    model.Textures.push_back(texture1);

    auto stage0 = TextureEnvStage(
        0, 0x2100, 2, { 0x8577, 0x84C0, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    stage0.CombineAlpha = 0x2100;
    stage0.SourceAlpha = { 0x8577, 0x84C0, 0x8576 };

    auto stage1 = TextureEnvStage(
        1, 0x6401, 1, { 0x8577, 0x84C1, 0x8578 },
        { 0x0300, 0x0300, 0x0300 });
    stage1.CombineAlpha = 0x2100;
    stage1.SourceAlpha = { 0x8578, 0x8576, 0x8576 };
    stage1.UnknownUshort2[0] = 0;

    model.TextureEnvSettings = { stage0, stage1 };
    auto& material = model.Materials[0];
    material.TextureMappersUsed = 2;
    material.TextureCoordsUsed = 2;
    material.RawTextureStageCount = 2;
    material.RawTextureStageSlots = { 0, 1, -1, -1, -1, -1 };
    material.TextureMappers[1].TextureIndex = 1;
    material.TextureCoords[1].Scale = { 1.0f, 1.0f };
    material.ConstantColors[0] = { 0, 0, 0, 204 };
    material.TextureEnvStages = { stage0, stage1 };
    material.TextureEnv = stage0;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexturePrimaryScaleThenConstantAlphaModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture_primary_scale_then_constant_alpha.cmb";
    model.Name = "synthetic_texture_primary_scale_then_constant_alpha";

    auto stage0 = TextureEnvStage(
        0, 0x2100, 2, { 0x8577, 0x84C0, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    auto stage1 = TextureEnvStage(
        1, 0x2100, 1, { 0x8578, 0x8576, 0x8576 },
        { 0x0300, 0x0302, 0x0300 });
    stage1.CombineAlpha = 0x1E01;
    stage1.SourceAlpha = { 0x8578, 0x6210, 0x8576 };

    model.TextureEnvSettings = { stage0, stage1 };
    auto& material = model.Materials[0];
    material.ConstantColors.fill({ 0, 0, 0, 128 });
    material.TextureEnvStages = { stage0, stage1 };
    material.TextureEnv = stage0;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexture1Texture2MultiplyAddPreviousModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture1_texture2_multiply_add_previous.cmb";
    model.Name = "synthetic_texture1_texture2_multiply_add_previous";

    auto texture1 = model.Textures[0];
    texture1.Index = 1;
    texture1.Name = "secondary";
    model.Textures.push_back(texture1);
    auto texture2 = model.Textures[0];
    texture2.Index = 2;
    texture2.Name = "tertiary";
    model.Textures.push_back(texture2);

    auto stage0 = TextureEnvStage(
        0, 0x2100, 2, { 0x8577, 0x84C0, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    auto stage1 = TextureEnvStage(
        1, 0x6401, 1, { 0x84C2, 0x84C1, 0x8578 },
        { 0x0300, 0x0300, 0x0300 });
    stage1.CombineAlpha = 0x1E01;
    stage1.SourceAlpha = { 0x8578, 0x6210, 0x8576 };

    model.TextureEnvSettings = { stage0, stage1 };
    auto& material = model.Materials[0];
    material.TextureMappersUsed = 3;
    material.TextureCoordsUsed = 3;
    material.TextureMappers[1].TextureIndex = 1;
    material.TextureMappers[1].MinFilter = 0x2600;
    material.TextureMappers[1].MagFilter = 0x2601;
    material.TextureMappers[1].WrapS = 0x2901;
    material.TextureMappers[1].WrapT = 0x812F;
    material.TextureMappers[2].TextureIndex = 2;
    material.TextureMappers[2].MinFilter = 0x2601;
    material.TextureMappers[2].MagFilter = 0x2600;
    material.TextureMappers[2].WrapS = 0x812F;
    material.TextureMappers[2].WrapT = 0x2901;
    material.TextureCoords[1].MappingMethod = 3;
    material.TextureCoords[2].MappingMethod = 1;
    material.TextureEnvStages = { stage0, stage1 };
    material.TextureEnv = stage0;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTexture0ConstantAlphaModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texture0_constant_alpha.cmb";
    model.Name = "synthetic_texture0_constant_alpha";

    auto stage = TextureEnvStage(
        0, 0x1E01, 1, { 0x84C0, 0x84C0, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    stage.CombineAlpha = 0x2100;
    stage.SourceAlpha = { 0x8576, 0x84C0, 0x8576 };
    stage.UnknownUshort2[0] = 5;

    model.TextureEnvSettings = { stage };
    auto& material = model.Materials[0];
    material.RawTextureStageCount = 1;
    material.RawTextureStageSlots = { 0, -1, -1, -1, -1, -1 };
    material.ConstantColors[5] = { 255, 255, 255, 128 };
    material.TextureEnvStages = { stage };
    material.TextureEnv = stage;
    material.FragmentLightingEnabled = true;
    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticTextureEnvAlphaConstantModel() {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.Source = "synthetic_texenv_alpha_constant.cmb";
    model.Name = "synthetic_texenv_alpha_constant";

    auto& preserveStage = model.TextureEnvSettings[1];
    preserveStage.CombineAlpha = 0x1E01;
    preserveStage.SourceAlpha = { 0x8578, 0x8576, 0x8576 };

    auto alphaConstantStage = TextureEnvStage(
        2, 0x1E01, 1, { 0x8578, 0x8576, 0x8576 },
        { 0x0300, 0x0300, 0x0300 });
    alphaConstantStage.CombineAlpha = 0x2100;
    alphaConstantStage.SourceAlpha = { 0x8578, 0x8576, 0x8576 };
    alphaConstantStage.UnknownUshort2[0] = 5;
    model.TextureEnvSettings.push_back(alphaConstantStage);

    auto& material = model.Materials[0];
    material.ConstantColors[5] = { 255, 255, 255, 179 };
    material.TextureEnvStages = model.TextureEnvSettings;
    material.RawTextureStageCount = 3;
    material.RawTextureStageSlots = { 0, 1, 2, -1, -1, -1 };
    return model;
}

ThreeDsRecomp::Oot3d::CmbMaterialLightingBlock CompleteMaterialLightingBlockWithPicaNormalMap(
    uint32_t textureUnit) {
    ThreeDsRecomp::Oot3d::CmbMaterialLightingBlock block;
    block.Decoded = true;
    block.Size = ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockMinSize;
    block.RawBlock.resize(ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockMinSize);
    block.PicaBumpTextureUnit = textureUnit;
    block.PicaBumpTextureUnitRecognized = true;
    block.PicaBumpMode = 1;
    block.PicaBumpModeRecognized = true;
    block.PicaLightingConfig = 0;
    block.PicaLightingConfigRecognized = true;
    block.PicaLutInputAbsD0DisableBit = 1;
    block.PicaLutInputAbsD0DisableBitResolved = true;
    block.PicaLutInputAbsSpDisableBit = 1;
    block.PicaLutInputAbsSpDisableBitResolved = true;
    block.PicaLutScaleSp = 0;
    block.PicaLutScaleSpResolved = true;
    block.PicaLutInputFr = 0;
    block.PicaLutInputFrResolved = true;
    block.PicaLutInputAbsFrDisableBit = 1;
    block.PicaLutInputAbsFrDisableBitResolved = true;
    block.PicaLutInputAbsRbDisableBit = 1;
    block.PicaLutInputAbsRbDisableBitResolved = true;
    block.PicaLutInputRb = 0;
    block.PicaLutInputRbRecognized = true;
    block.PicaLutScaleRb = 0;
    block.PicaLutScaleRbRecognized = true;
    return block;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticActorMaterialLightingNormalMapModel() {
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Source = "synthetic_actor_material_lighting_normal_map.cmb";
    model.Name = "synthetic_actor_material_lighting_normal_map";

    ThreeDsRecomp::Oot3d::CmbTexture texture;
    texture.Index = 0;
    texture.Name = "native_pica_normal_map";
    texture.Width = 1;
    texture.Height = 1;
    texture.TextureFormat = 0x6752;
    texture.DataType = 0x1401;
    texture.Rgba8Decoded = true;
    texture.Rgba8 = { 128, 128, 255, 255 };
    model.Textures.push_back(texture);

    ThreeDsRecomp::Oot3d::CmbMaterial material;
    material.Index = 0;
    material.TextureMappersUsed = 1;
    material.TextureCoordsUsed = 1;
    material.RawMaterial.resize(ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize);
    material.FragmentLightingEnabled = true;
    material.MaterialColorsDecoded = true;
    material.EmissionColor = { 0, 0, 0, 255 };
    material.AmbientColor = { 255, 255, 255, 255 };
    material.DiffuseColor = { 255, 255, 255, 255 };
    material.TextureMappers[0].TextureIndex = 0;
    material.TextureCoords[0].Scale = { 1.0f, 1.0f };
    material.LightingBlock = CompleteMaterialLightingBlockWithPicaNormalMap(0);
    model.Materials.push_back(material);

    ThreeDsRecomp::Oot3d::CmbMesh mesh;
    mesh.Index = 0;
    mesh.ShapeIndex = 0;
    mesh.MaterialIndex = 0;
    model.Meshes.push_back(mesh);

    ThreeDsRecomp::Oot3d::CmbShape shape;
    shape.Index = 0;
    shape.Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition |
                  ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal |
                  ThreeDsRecomp::Oot3d::kOot3dCmbAttributeUv0;
    shape.Positions = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
    shape.Normals = { { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f } };
    shape.Uv0 = { { 0.0f, 0.0f }, { 0.5f, 0.0f }, { 0.0f, 0.5f } };
    ThreeDsRecomp::Oot3d::CmbPrimitive primitive;
    primitive.Indices = { 0, 1, 2 };
    shape.Primitives.push_back(primitive);
    model.Shapes.push_back(shape);

    return model;
}

ThreeDsRecomp::Oot3d::CmbModel SyntheticStaticBakedVertexHemisphereLightingModel() {
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Source = "synthetic_static_baked_vertex_hemisphere.cmb";
    model.Name = "synthetic_static_baked_vertex_hemisphere";

    ThreeDsRecomp::Oot3d::CmbSkeletonBone bone;
    bone.Index = 0;
    model.Skeleton.Bones.push_back(bone);

    ThreeDsRecomp::Oot3d::CmbMaterial material;
    material.Index = 0;
    material.FragmentLightingEnabled = false;
    material.VertexLightingEnabled = true;
    material.HemisphereLightingEnabled = true;
    material.MaterialColorsDecoded = true;
    material.EmissionColor = { 0, 0, 0, 255 };
    material.AmbientColor = { 255, 255, 255, 255 };
    material.DiffuseColor = { 255, 255, 255, 255 };
    model.Materials.push_back(material);

    ThreeDsRecomp::Oot3d::CmbMesh mesh;
    mesh.Index = 0;
    mesh.ShapeIndex = 0;
    mesh.MaterialIndex = 0;
    model.Meshes.push_back(mesh);

    ThreeDsRecomp::Oot3d::CmbShape shape;
    shape.Index = 0;
    shape.Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition |
                  ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal;
    shape.Positions = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
    shape.Normals = { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
    ThreeDsRecomp::Oot3d::CmbPrimitive primitive;
    primitive.Indices = { 0, 1, 2 };
    shape.Primitives.push_back(primitive);
    model.Shapes.push_back(shape);

    return model;
}

nlohmann::json SyntheticPicaLightingSemanticsForVertexHemisphereScope(std::string_view scope,
                                                                      std::string_view formula,
                                                                      std::string_view lightColorMode =
                                                                          "neutral_max_rgb_intensity_for_neutral_cmb_material_colors") {
    return {
        { "format", "oot3d_pica_lighting_semantics_v1" },
        { "source_kind", "oot3d_pica_light_settings_record_layout_semantics" },
        { "layout", "oot3d_pica_light_settings_record_0x1c" },
        { "record_size", 28 },
        { "engine_modes",
          {
              { "native_pica_lighting_vertex_color",
                {
                    { "record_selector", "active_setup_record_from_player_floor_light_setting_index" },
                    { "record_selector_source",
                      "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting" },
                    { "ambient_color_source", "oot3d_runtime_light_settings_rgb_u8" },
                    { "ambient_color_offset", 10 },
                    { "diffuse_color_source", "oot3d_runtime_light_settings_rgb_u8" },
                    { "diffuse_color_offset", 16 },
                    { "light1_color_source", "oot3d_runtime_light_settings_rgb_u8" },
                    { "light1_color_offset", 22 },
                    { "vertex_color_formula",
                      "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb / 255, 0, 255) / 255)" },
                    { "directional_formula",
                      "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * (light0.rgb * max(dot(normal, light0), 0) + light1.rgb * max(dot(normal, light1), 0)) / 255, 0, 255) / 255)" },
                    { "light0_vector_source", "oot3d_runtime_light_settings_signed_vec3_normalized" },
                    { "light0_vector_offset", 13 },
                    { "light1_vector_source", "oot3d_runtime_light_settings_signed_vec3_normalized" },
                    { "light1_vector_offset", 19 },
                    { "texture_combiner", "texel0_times_vertex_color" },
                    { "textured_base_color_source", "constant_white_until_material_combiner_route_decoded" },
                    { "material_lighting_enable_source", "oot3d_cmb_material_fragment_lighting_flag" },
                    { "material_lighting_deferred_source", "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" },
                    { "material_vertex_hemisphere_model_scope", std::string(scope) },
                    { "material_vertex_hemisphere_lighting_source",
                      "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags" },
                    { "vertex_hemisphere_lighting_formula", std::string(formula) },
                    { "vertex_hemisphere_lighting_reference",
                      "azahar_pica_frame_vs_uniform_f82_f84_diffuse_accumulator_for_native_cmb_skeleton_vertex_output" },
                    { "vertex_hemisphere_light_color_mode", std::string(lightColorMode) },
                    { "material_emission_source", "oot3d_cmb_material_emission_rgb" },
                    { "material_ambient_source", "oot3d_cmb_material_ambient_rgb" },
                    { "material_diffuse_source", "oot3d_cmb_material_diffuse_rgb" },
                    { "modulate_textured_batches", true },
                    { "preserve_vertex_alpha", true },
                } },
          } },
    };
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene SyntheticPicaLightingSceneForStaticBakedScope(
    bool actorVsPacketAvailable = false) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
    scene.NativePicaLightingSemanticsAvailable = true;
    scene.NativePicaLightingSemanticsSourceKind =
        "oot3d_pica_light_settings_record_layout_semantics";
    scene.NativePicaLightingSemantics = SyntheticPicaLightingSemanticsForVertexHemisphereScope(
        "native_cmb_transform_instance_or_static_baked_models_with_native_normals",
        "pica_diffuse_accumulator_evaluated_per_vertex_normal_for_native_cmb_transform_instance_or_static_baked_models_with_native_normals",
        actorVsPacketAvailable
            ? "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors"
            : "neutral_max_rgb_intensity_for_neutral_cmb_material_colors");
    scene.NativePicaLighting.Available = true;
    scene.NativePicaLighting.SourceKind = "oot3d_zsi_light_settings_list_native_pica_state";
    scene.NativePicaLighting.SelectedLightSettingsLayout = "oot3d_pica_light_settings_record_0x1c";
    scene.NativePicaLighting.ActiveSetupIndex = 0;
    scene.PlayerStart.Valid = true;
    scene.PlayerStart.Rotation.Y = -32768.0;
    scene.PlayerStart.FloorLightSettingIndex = 0;

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoPicaLightSettingsRecord record;
    record.SetupIndex = 0;
    record.Index = 0;
    record.EntrySize = 28;
    record.Layout = "oot3d_pica_light_settings_record_0x1c";
    record.NativeRuntimeEnvironmentLightSettingsAvailable = true;
    record.RawBytes.resize(28, 0);
    record.RawBytes[10] = 181;
    record.RawBytes[11] = 181;
    record.RawBytes[12] = 160;
    record.RawBytes[13] = 4;
    record.RawBytes[14] = 0;
    record.RawBytes[15] = 0;
    record.RawBytes[16] = 0;
    record.RawBytes[17] = 128;
    record.RawBytes[18] = 59;
    record.RawBytes[19] = 70;
    record.RawBytes[20] = 0;
    record.RawBytes[21] = 0;
    record.RawBytes[22] = 22;
    record.RawBytes[23] = 69;
    record.RawBytes[24] = 248;
    scene.NativePicaLighting.LightSettings.push_back(record);
    if (actorVsPacketAvailable) {
        auto bridgeRecord = record;
        bridgeRecord.Index = 1;
        bridgeRecord.NativeRuntimeEnvironmentLightSettingsAvailable = false;
        bridgeRecord.NativeActorVsLightPacketColorCandidateAvailable = true;
        bridgeRecord.NativeActorVsAmbientColorCandidateAvailable = true;
        bridgeRecord.NativeActorVsAmbientColor = { 1, 2, 3, 255 };
        bridgeRecord.NativeActorVsDiffuse0Color = { 4, 5, 6, 255 };
        bridgeRecord.NativeActorVsDiffuse1Color = { 7, 8, 9, 255 };
        scene.NativePicaLighting.LightSettings.push_back(bridgeRecord);

        auto actorVsRecord = record;
        actorVsRecord.Index = 2;
        actorVsRecord.NativeRuntimeEnvironmentLightSettingsAvailable = false;
        actorVsRecord.NativeActorVsLightPacketColorCandidateAvailable = true;
        actorVsRecord.NativeActorVsAmbientColorCandidateAvailable = true;
        actorVsRecord.NativeActorVsAmbientColor = { 181, 181, 160, 255 };
        actorVsRecord.NativeActorVsDiffuse0Color = { 255, 255, 219, 255 };
        actorVsRecord.NativeActorVsDiffuse1Color = { 109, 99, 79, 255 };
        scene.NativePicaLighting.LightSettings.push_back(actorVsRecord);
    }
    return scene;
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoPicaLightSettingsRecord SyntheticRuntimeEnvironmentLightRecord(
    int index,
    ThreeDsRecomp::Oot3d::ColorRgba8 ambient,
    ThreeDsRecomp::Oot3d::ColorRgba8 light0,
    ThreeDsRecomp::Oot3d::ColorRgba8 light1,
    ThreeDsRecomp::Oot3d::ColorRgba8 fog,
    double cameraFar = 32000.0,
    double fogFar = 40000.0,
    uint16_t fogNear = 40) {
    const auto contract = ThreeDsRecomp::Oot3d::BuildNativeKankyoRuntimeBridgeContract();
    const auto& layout = contract.ZsiLightSettingsRecord;

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoPicaLightSettingsRecord record;
    record.SetupIndex = 0;
    record.Index = index;
    record.EntrySize = static_cast<int>(layout.NativeRecordSizeBytes);
    record.Layout = layout.NativeRecordLayoutName;
    record.NativeRuntimeEnvironmentLightSettingsAvailable = true;
    record.RawBytes.resize(layout.NativeRecordSizeBytes, 0);

    const auto putColor = [&](uint32_t offset, ThreeDsRecomp::Oot3d::ColorRgba8 color) {
        record.RawBytes[offset + 0] = color.R;
        record.RawBytes[offset + 1] = color.G;
        record.RawBytes[offset + 2] = color.B;
    };
    const auto putDirection = [&](uint32_t offset, int8_t x, int8_t y, int8_t z) {
        record.RawBytes[offset + 0] = static_cast<uint8_t>(x);
        record.RawBytes[offset + 1] = static_cast<uint8_t>(y);
        record.RawBytes[offset + 2] = static_cast<uint8_t>(z);
    };

    putColor(layout.RuntimeAmbientColorOffset, ambient);
    putDirection(layout.RuntimeLight0DirectionOffset, 0, 127, 0);
    putColor(layout.RuntimeLight0ColorOffset, light0);
    putDirection(layout.RuntimeLight1DirectionOffset, 0, -127, 0);
    putColor(layout.RuntimeLight1ColorOffset, light1);
    putColor(layout.RuntimeFogColorOffset, fog);

    record.NativeRuntimeAmbientColor = ambient;
    record.NativeRuntimeLight0Color = light0;
    record.NativeRuntimeLight1Color = light1;
    record.NativeRuntimeFogColor = fog;
    record.FloatParam0 = cameraFar;
    record.FloatParam1 = fogFar;
    record.FloatParamsFinite = true;
    record.NativeRuntimePackedHalfwordRaw = fogNear & layout.RuntimePackedHalfwordMask;
    return record;
}

ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene SyntheticRuntimeTransitionFogScene() {
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(false);
    scene.NativePicaLighting.LightSettings.clear();

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoLightSettingsTransitionMode mode;
    mode.ModeIndex = 0;
    mode.Entries.push_back({
        0,
        0,
        100,
        0,
        1,
    });
    scene.NativePicaLighting.NativeRuntimeTransitionTableAvailable = true;
    scene.NativePicaLighting.NativeRuntimeTransitionModes = { mode };

    scene.NativePicaLighting.LightSettings.push_back(
        SyntheticRuntimeEnvironmentLightRecord(
            0, { 10, 20, 30, 255 }, { 40, 50, 60, 255 }, { 70, 80, 90, 255 },
            { 20, 40, 60, 255 }, 32000.0, 40000.0, 40));
    scene.NativePicaLighting.LightSettings.push_back(
        SyntheticRuntimeEnvironmentLightRecord(
            1, { 110, 120, 130, 255 }, { 140, 150, 160, 255 }, { 170, 180, 190, 255 },
            { 100, 140, 180, 255 }, 32000.0, 48000.0, 200));

    auto actorVsRecord = scene.NativePicaLighting.LightSettings[0];
    actorVsRecord.Index = 2;
    actorVsRecord.NativeRuntimeEnvironmentLightSettingsAvailable = false;
    actorVsRecord.NativeActorVsLightPacketColorCandidateAvailable = true;
    actorVsRecord.NativeActorVsAmbientColorCandidateAvailable = true;
    actorVsRecord.NativeActorVsAmbientColor = { 181, 181, 160, 255 };
    actorVsRecord.NativeActorVsDiffuse0Color = { 255, 255, 219, 255 };
    actorVsRecord.NativeActorVsDiffuse1Color = { 109, 99, 79, 255 };
    const auto contract = ThreeDsRecomp::Oot3d::BuildNativeKankyoRuntimeBridgeContract();
    const auto fogOffset = contract.ZsiLightSettingsRecord.ActorPacketPicaFogColorOffset;
    actorVsRecord.RawBytes[fogOffset + 0] = 244;
    actorVsRecord.RawBytes[fogOffset + 1] = 239;
    actorVsRecord.RawBytes[fogOffset + 2] = 130;
    scene.NativePicaLighting.LightSettings.push_back(actorVsRecord);

    auto actorVsRecordTo = actorVsRecord;
    actorVsRecordTo.Index = 3;
    actorVsRecordTo.NativeActorVsAmbientColor = { 21, 31, 41, 255 };
    actorVsRecordTo.NativeActorVsDiffuse0Color = { 51, 61, 71, 255 };
    actorVsRecordTo.NativeActorVsDiffuse1Color = { 81, 91, 101, 255 };
    actorVsRecordTo.RawBytes[fogOffset + 0] = 120;
    actorVsRecordTo.RawBytes[fogOffset + 1] = 130;
    actorVsRecordTo.RawBytes[fogOffset + 2] = 140;
    scene.NativePicaLighting.LightSettings.push_back(actorVsRecordTo);

    return scene;
}

std::vector<uint8_t> MinimalZsiWithEmbeddedCmb() {
    auto cmb = MinimalTriangleCmb();
    std::vector<uint8_t> zsi(0x20, 0);
    zsi[0] = 'Z';
    zsi[1] = 'S';
    zsi[2] = 'I';
    zsi[3] = 0x01;
    zsi.insert(zsi.end(), cmb.begin(), cmb.end());
    return zsi;
}

std::vector<uint8_t> MinimalCsabMetadata() {
    std::vector<uint8_t> bytes(0x3C, 0);
    PutAscii(bytes, 0x00, "csab");
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 5);
    PutLe32(bytes, 0x28, 72);
    PutLe32(bytes, 0x30, 0);
    PutLe32(bytes, 0x34, 1);
    PutLe16(bytes, 0x38, 0xFFFF);
    return bytes;
}

std::vector<uint8_t> MinimalCsabConstantTranslation(float translationX) {
    constexpr size_t nodeStart = 0x40;
    constexpr size_t channelBlockStart = nodeStart + 0x1C;
    std::vector<uint8_t> bytes(channelBlockStart + 0x18, 0);
    PutAscii(bytes, 0x00, "csab");
    PutLe32(bytes, 0x04, static_cast<uint32_t>(bytes.size()));
    PutLe32(bytes, 0x08, 5);
    PutLe32(bytes, 0x28, 1);
    PutLe32(bytes, 0x30, 1);
    PutLe32(bytes, 0x34, 1);
    PutLe16(bytes, 0x38, 0);
    PutLe32(bytes, 0x3C, static_cast<uint32_t>(nodeStart - 0x18));
    PutLe16(bytes, nodeStart + 0x08, 0x1C);
    PutLe32(bytes, channelBlockStart + 0x00, 1);
    PutLe32(bytes, channelBlockStart + 0x04, 1);
    PutF32(bytes, channelBlockStart + 0x14, translationX);
    return bytes;
}

ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture SyntheticDecodedCtxbRenderTexture(std::string name) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
    texture.SourceIndex = 3;
    texture.Name = std::move(name);
    texture.Width = 2;
    texture.Height = 2;
    texture.TextureFormat = 0x675Au;
    texture.DataType = 0;
    texture.Rgba8Decoded = true;
    texture.HasNativeAlpha = false;
    texture.Rgba8 = {
        255, 255, 255, 255,
        255, 180, 160, 128,
        255, 255, 160, 128,
        255, 255, 255, 0,
    };
    return texture;
}

} // namespace

TEST(Oot3dNativeFormat, DetectsNativeHeaders) {
    auto zsiBytes = MinimalZsiHeader();
    auto cmbBytes = MinimalCmbHeader();
    auto zarBytes = MinimalZarHeader();
    auto csabBytes = MinimalCsabHeader();
    auto ctxbBytes = MinimalCtxbHeader();
    auto shbinBytes = MinimalShbinHeader();
    auto zsi = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(zsiBytes);
    auto cmb = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(cmbBytes);
    auto zar = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(zarBytes);
    auto csab = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(csabBytes);
    auto ctxb = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(ctxbBytes);
    auto shbin = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(shbinBytes);

    EXPECT_EQ(zsi.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Zsi);
    EXPECT_EQ(cmb.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Cmb);
    EXPECT_EQ(zar.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Zar);
    EXPECT_EQ(csab.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Csab);
    EXPECT_EQ(ctxb.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Ctxb);
    EXPECT_EQ(shbin.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Shbin);
    EXPECT_TRUE(zsi.HeaderMatches);
    EXPECT_TRUE(cmb.HeaderMatches);
    EXPECT_TRUE(zar.HeaderMatches);
    EXPECT_TRUE(csab.HeaderMatches);
    EXPECT_TRUE(ctxb.HeaderMatches);
    EXPECT_TRUE(shbin.HeaderMatches);
    EXPECT_EQ(shbin.Version, 0x08020000u);
}

TEST(Oot3dNativeFormat, RejectsGlbAsNativeSourceFormat) {
    auto glbBytes = MinimalGlbHeader();
    auto probe = ThreeDsRecomp::Oot3d::ProbeOot3dNativeFormatBytes(glbBytes);

    EXPECT_FALSE(probe.IsRecognized);
    EXPECT_FALSE(probe.HeaderMatches);
    EXPECT_EQ(probe.Kind, ThreeDsRecomp::Oot3d::NativeFormatKind::Unknown);
    EXPECT_EQ(probe.Issue, "unknown_oot3d_native_magic");
}

TEST(Oot3dNativeAssets, BuildsGanon2DescriptorCmbSelectionFromZarTypeLocalIndices) {
    ThreeDsRecomp::Oot3d::ZarArchive archive;
    archive.Source = "synthetic actor/zelda_ganon2.zar";
    archive.Files = {
        { 5, "Texture/descriptor_5.ctxb", "ctxb", 5, 0x1000, 0x80 },
        { 6, "Anim/descriptor_6.csab", "csab", 6, 0x1080, 0x90 },
        { 40, "Model/gn2_demo_ec_tfc_modelT.cmb", "cmb", 5, 0x2000, 0x500 },
        { 41, "Model/master_gn2_swordB_model.cmb", "cmb", 6, 0x2500, 0x4A80 },
        { 42, "Model/master_sword_shadow_model.cmb", "cmb", 7, 0x6F80, 0x880 },
    };

    const auto contract = ThreeDsRecomp::Oot3d::BuildNativeGanon2DescriptorCmbSelectionContract(archive);

    EXPECT_EQ(contract.SourceKind, "oot3d_codebin_object_descriptor_cmb_selection");
    EXPECT_EQ(contract.SelectorFunctionAddress, 0x0036A924u);
    EXPECT_EQ(contract.ObjectGetIndexFunctionAddress, 0x00363C10u);
    EXPECT_EQ(contract.CmbResolverAddress, 0x00358EF8u);
    EXPECT_EQ(contract.SourceId, 0x0153u);
    EXPECT_EQ(contract.SourceSymbol, "OBJECT_GANON2");
    EXPECT_EQ(contract.ArchivePath, "rom:/actor/zelda_ganon2.zar");
    EXPECT_EQ(contract.DescriptorIds[0], 5u);
    EXPECT_EQ(contract.DescriptorIds[1], 6u);
    EXPECT_EQ(contract.DescriptorIds[2], 7u);
    EXPECT_EQ(contract.ResourceContextObjectTableBaseOffset, 0x3A58u);
    EXPECT_EQ(contract.ResourceContextEntryStrideBytes, 0x80u);
    EXPECT_EQ(contract.ResourceContextAvailabilityPointerOffset, 0x3A64u);
    EXPECT_EQ(contract.ResourceContextPayloadOffset, 0x3A6Cu);
    EXPECT_EQ(contract.DescriptorStoreOffset, 0x350u);
    EXPECT_TRUE(contract.DescriptorIdsAreCmbTypeLocalIndices);
    EXPECT_FALSE(contract.DescriptorIdsAreGlobalFileIndices);
    EXPECT_FALSE(contract.DescriptorIdsAreZsiIndices);
    EXPECT_FALSE(contract.ActiveDemoBinding);
    ASSERT_EQ(contract.ResolvedCmbs.size(), 3u);
    EXPECT_EQ(contract.ResolvedCmbs[0].DescriptorId, 5u);
    EXPECT_EQ(contract.ResolvedCmbs[0].File.Index, 40u);
    EXPECT_EQ(contract.ResolvedCmbs[0].File.TypeName, "cmb");
    EXPECT_EQ(contract.ResolvedCmbs[0].File.TypeLocalIndex, 5u);
    EXPECT_EQ(contract.ResolvedCmbs[0].File.Name, "Model/gn2_demo_ec_tfc_modelT.cmb");
    EXPECT_EQ(contract.ResolvedCmbs[1].DescriptorId, 6u);
    EXPECT_EQ(contract.ResolvedCmbs[1].File.Index, 41u);
    EXPECT_EQ(contract.ResolvedCmbs[1].File.Name, "Model/master_gn2_swordB_model.cmb");
    EXPECT_EQ(contract.ResolvedCmbs[2].DescriptorId, 7u);
    EXPECT_EQ(contract.ResolvedCmbs[2].File.Index, 42u);
    EXPECT_EQ(contract.ResolvedCmbs[2].File.Name, "Model/master_sword_shadow_model.cmb");

    const std::array<uint32_t, 1> zsiDescriptorIds = { 5 };
    EXPECT_THROW((void)ThreeDsRecomp::Oot3d::ResolveZarTypeLocalEntries(archive, "zsi", zsiDescriptorIds),
                 std::runtime_error);
}

TEST(Oot3dNativeAssets, ParsesShbinProgramTables) {
    auto bytes = MinimalShbinHeader();
    auto shader = ThreeDsRecomp::Oot3d::ParseShbinShaderBinaryBytes(bytes, "CmbVShader.shbin");

    EXPECT_EQ(shader.Source, "CmbVShader.shbin");
    EXPECT_EQ(shader.ProgramCount, 1u);
    EXPECT_EQ(shader.DvlpOffset, 0x0Cu);
    EXPECT_EQ(shader.DvlpVersion, 0x08020000u);
    EXPECT_EQ(shader.BinaryOffset, 0x1Cu);
    EXPECT_EQ(shader.BinarySizeWords, 2u);
    ASSERT_EQ(shader.ProgramCode.size(), 2u);
    EXPECT_EQ(shader.ProgramCode[0], 0x11111111u);
    EXPECT_EQ(shader.ProgramCode[1], 0x22222222u);
    ASSERT_EQ(shader.Swizzles.size(), 2u);
    EXPECT_EQ(shader.Swizzles[0].Pattern, 0xAAAA0001u);
    EXPECT_EQ(shader.Swizzles[1].Unknown, 1u);
    ASSERT_EQ(shader.Filenames.size(), 1u);
    EXPECT_EQ(shader.Filenames[0], "test.vsh");

    ASSERT_EQ(shader.Programs.size(), 1u);
    const auto& program = shader.Programs[0];
    EXPECT_EQ(program.DvleOffset, 0x50u);
    EXPECT_EQ(program.ShaderType, 0u);
    EXPECT_EQ(program.MainOffsetWords, 1u);
    EXPECT_EQ(program.EndMainOffsetWords, 2u);

    ASSERT_EQ(program.Outputs.size(), 2u);
    EXPECT_EQ(program.Outputs[0].Type, 3u);
    EXPECT_EQ(program.Outputs[0].RegisterId, 2u);
    EXPECT_EQ(program.Outputs[0].ComponentMask, 7u);
    EXPECT_EQ(program.Outputs[0].Descriptor, 7u);
    EXPECT_EQ(program.Outputs[0].SemanticName, "out.tex0");
    EXPECT_EQ(program.Outputs[1].Type, 4u);
    EXPECT_EQ(program.Outputs[1].RegisterId, 2u);
    EXPECT_EQ(program.Outputs[1].ComponentMask, 4u);
    EXPECT_EQ(program.Outputs[1].Descriptor, 4u);
    EXPECT_EQ(program.Outputs[1].SemanticName, "out.tex0w");

    ASSERT_EQ(program.Constants.size(), 1u);
    EXPECT_EQ(program.Constants[0].Type, 2u);
    EXPECT_EQ(program.Constants[0].RegisterId, 5u);
    EXPECT_EQ(program.Constants[0].Values[0], 0x01020304u);
    EXPECT_EQ(program.Constants[0].Values[3], 0x31323334u);

    ASSERT_EQ(program.Uniforms.size(), 1u);
    EXPECT_EQ(program.Uniforms[0].Name, "u_modelview");
    EXPECT_EQ(program.Uniforms[0].RegisterStart, 0x10u);
    EXPECT_EQ(program.Uniforms[0].RegisterEnd, 0x12u);
}

TEST(Oot3dNativeAssets, ParsesCmbGeometrySkeletonAndPrimitiveData) {
    auto bytes = MinimalTriangleCmb();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle.cmb");

    EXPECT_EQ(model.Name, "tri");
    EXPECT_EQ(model.Version, 6u);
    EXPECT_EQ(model.BoneCount(), 1u);
    EXPECT_EQ(model.Meshes.size(), 1u);
    EXPECT_TRUE(model.MeshPassSplitIndexDecoded);
    EXPECT_EQ(model.MeshPassSplitIndex, 1);
    EXPECT_EQ(model.Shapes.size(), 1u);
    EXPECT_EQ(model.PrimitiveCount(), 1u);
    EXPECT_EQ(model.TriangleCount(), 1u);
    EXPECT_EQ(model.VertexCount(), 3u);
    EXPECT_TRUE(model.IsStaticCandidate());
    EXPECT_TRUE(model.Luts.Decoded);
    EXPECT_EQ(model.Luts.SourceOffset, 0x3D4u);
    EXPECT_EQ(model.Luts.ChunkSize, 0x44u);
    EXPECT_EQ(model.Luts.Count, 1u);
    ASSERT_EQ(model.Luts.Records.size(), 1u);
    EXPECT_EQ(model.Luts.Records[0].SourceOffset, 0x14u);
    EXPECT_EQ(model.Luts.Records[0].Size, 0x30u);
    EXPECT_EQ(model.Luts.Records[0].Type, 2u);
    EXPECT_EQ(model.Luts.Records[0].HeaderByte01, 1u);
    EXPECT_EQ(model.Luts.Records[0].HeaderByte02, 1u);
    EXPECT_EQ(model.Luts.Records[0].HeaderByte03, 0u);
    EXPECT_EQ(model.Luts.Records[0].PointCount, 2u);
    EXPECT_EQ(model.Luts.Records[0].HeaderWord08, 0u);
    EXPECT_EQ(model.Luts.Records[0].HeaderWord0C, 0xFFu);
    EXPECT_EQ(model.Luts.Records[0].PointStrideBytes, 0x10u);
    EXPECT_EQ(model.Luts.Records[0].RawRecord.size(), 0x30u);
    ASSERT_EQ(model.Luts.Records[0].Points.size(), 2u);
    EXPECT_EQ(model.Luts.Records[0].Points[0].X, 0);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].Points[0].Value, 0.0f);
    EXPECT_EQ(model.Luts.Records[0].Points[1].X, 255);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].Points[1].Value, 1.0f);
    ASSERT_EQ(model.Luts.Records[0].Samples.size(), 0x101u);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].Samples[0], 0.0f);
    const float lutMidT = 128.0f / 255.0f;
    EXPECT_NEAR(model.Luts.Records[0].Samples[128], (3.0f - 2.0f * lutMidT) * lutMidT * lutMidT,
                1.0e-6f);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].Samples[255], 1.0f);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].Samples[256], 1.0f);
    ASSERT_EQ(model.Luts.Records[0].PackedBaseValues.size(), 0x100u);
    ASSERT_EQ(model.Luts.Records[0].PackedDeltaValues.size(), 0x100u);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].PackedBaseValues[0], 0.0f);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].PackedBaseValues[255], 1.0f);
    EXPECT_FLOAT_EQ(model.Luts.Records[0].PackedDeltaValues[255], 0.0f);
    ASSERT_EQ(model.Shapes[0].Positions.size(), 3u);
    EXPECT_FLOAT_EQ(model.Shapes[0].Positions[1].X, 1.0f);
    EXPECT_FLOAT_EQ(model.Shapes[0].Positions[2].Y, 1.0f);
    ASSERT_EQ(model.Shapes[0].Primitives[0].Indices.size(), 3u);
    EXPECT_EQ(model.Shapes[0].Primitives[0].Indices[2], 2u);
}

TEST(Oot3dNativeAssets, DecodesCmbMipChainAndNativeSamplerLodBias) {
    const auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(
        MinimalTriangleCmbWithMipmappedTexture(), "triangle_mipmapped.cmb");

    ASSERT_EQ(model.Textures.size(), 1u);
    const auto& texture = model.Textures[0];
    EXPECT_EQ(texture.MipmapCount, 2u);
    EXPECT_TRUE(texture.MipmapLayoutDecoded);
    EXPECT_TRUE(texture.Rgba8Decoded);
    ASSERT_EQ(texture.Rgba8.size(), 16u * 16u * 4u);
    EXPECT_EQ(texture.Rgba8[0], 0x44u);
    ASSERT_EQ(texture.AdditionalMipLevels.size(), 1u);
    const auto& mip = texture.AdditionalMipLevels[0];
    EXPECT_EQ(mip.Level, 1u);
    EXPECT_EQ(mip.Width, 8u);
    EXPECT_EQ(mip.Height, 8u);
    EXPECT_EQ(mip.DataOffset, 16u * 16u);
    EXPECT_EQ(mip.DataSize, 8u * 8u);
    EXPECT_TRUE(mip.Rgba8Decoded);
    ASSERT_EQ(mip.Rgba8.size(), 8u * 8u * 4u);
    EXPECT_EQ(mip.Rgba8[0], 0x88u);

    ASSERT_EQ(model.Materials.size(), 1u);
    EXPECT_EQ(model.Materials[0].TextureMappers[0].MinFilter, 0x2703u);
    EXPECT_EQ(model.Materials[0].TextureMappers[0].MagFilter, 0x2601u);
    EXPECT_FLOAT_EQ(model.Materials[0].TextureMappers[0].LodBias, -1.4f);

    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Textures.size(), 1u);
    EXPECT_EQ(renderModel.Textures[0].MipmapCount, 2u);
    EXPECT_TRUE(renderModel.Textures[0].MipmapLayoutDecoded);
    EXPECT_EQ(renderModel.Textures[0].Rgba8ByteCount, 16u * 16u * 4u + 8u * 8u * 4u);
    EXPECT_TRUE(renderModel.Textures[0].Rgba8HashAvailable);
    ASSERT_EQ(renderModel.Textures[0].AdditionalMipLevels.size(), 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    EXPECT_TRUE(renderModel.Batches[0].Material.NativeSamplerStateDecoded);
    EXPECT_EQ(renderModel.Batches[0].Material.NativeSamplerMinFilter, 0x2703u);
    EXPECT_FLOAT_EQ(renderModel.Batches[0].Material.NativeSamplerLodBias, -1.4f);
    EXPECT_TRUE(renderModel.Batches[0].Material.TextureMapperSamplerStates[0].Decoded);
    EXPECT_FLOAT_EQ(renderModel.Batches[0].Material.TextureMapperSamplerStates[0].LodBias, -1.4f);
}

TEST(Oot3dNativeAssets, PreservesDecodedTextureIdentityForIncompleteDeclaredMipChain) {
    auto bytes = MinimalTriangleCmbWithMipmappedTexture();
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr size_t texOff = materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize;
    constexpr size_t entry = texOff + 0x0C;
    PutLe16(bytes, entry + 0x04, 3);

    const auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_incomplete_mips.cmb");
    ASSERT_EQ(model.Textures.size(), 1u);
    EXPECT_EQ(model.Textures[0].MipmapCount, 3u);
    EXPECT_FALSE(model.Textures[0].MipmapLayoutDecoded);
    ASSERT_EQ(model.Textures[0].AdditionalMipLevels.size(), 1u);

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Textures.size(), 1u);
    const uint64_t hashBeforeStrip = renderModel.Textures[0].Rgba8Hash;
    const size_t bytesBeforeStrip = renderModel.Textures[0].Rgba8ByteCount;
    EXPECT_TRUE(renderModel.Textures[0].Rgba8HashAvailable);
    EXPECT_NE(hashBeforeStrip, 0u);

    const size_t stripped = ThreeDsRecomp::Oot3d::StripOot3dNativeRenderModelTexturePayloads(renderModel);
    EXPECT_EQ(stripped, bytesBeforeStrip);
    EXPECT_TRUE(renderModel.Textures[0].Rgba8HashAvailable);
    EXPECT_EQ(renderModel.Textures[0].Rgba8Hash, hashBeforeStrip);
    EXPECT_EQ(renderModel.Textures[0].Rgba8ByteCount, bytesBeforeStrip);
    EXPECT_TRUE(renderModel.Textures[0].Rgba8.empty());
    EXPECT_TRUE(renderModel.Textures[0].AdditionalMipLevels[0].Rgba8.empty());
}

TEST(Oot3dNativeAssets, ParsesCmbMaterialLightingBlock) {
    auto bytes = MinimalTriangleCmb();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle.cmb");

    ASSERT_EQ(model.Materials.size(), 1u);
    const auto& block = model.Materials[0].LightingBlock;
    EXPECT_TRUE(block.Decoded);
    EXPECT_EQ(block.SourceOffset, ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockOffset);
    EXPECT_EQ(block.Size, ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockMinSize);
    EXPECT_EQ(block.RawBlock.size(), ThreeDsRecomp::Oot3d::kOot3dCmbMaterialLightingBlockMinSize);
    EXPECT_EQ(block.PicaBumpTextureUnitRaw, 0x84C2u);
    EXPECT_EQ(block.PicaBumpTextureUnit, 2u);
    EXPECT_TRUE(block.PicaBumpTextureUnitRecognized);
    EXPECT_EQ(block.PicaBumpModeRaw, 0x62C9u);
    EXPECT_EQ(block.PicaBumpMode, 1u);
    EXPECT_TRUE(block.PicaBumpModeRecognized);
    EXPECT_TRUE(block.Flag1);
    EXPECT_EQ(block.PicaLightingConfigRaw, 0x62B7u);
    EXPECT_EQ(block.PicaLightingConfig, 8u);
    EXPECT_TRUE(block.PicaLightingConfigRecognized);
    EXPECT_EQ(block.UnresolvedEnum4Raw, 0x62C3u);
    EXPECT_EQ(block.UnresolvedEnum4Encoded, 3u);
    EXPECT_TRUE(block.UnresolvedEnum4Recognized);
    EXPECT_EQ(block.Pica62C0SelectorRaw, 0x62C3u);
    EXPECT_EQ(block.Pica62C0Selector, 3u);
    EXPECT_TRUE(block.Pica62C0SelectorRecognized);
    EXPECT_EQ(block.PicaLutInputAbsD0Raw, 0x62C3u);
    EXPECT_EQ(block.PicaLutInputAbsD0Selector, 3u);
    EXPECT_TRUE(block.PicaLutInputAbsD0SelectorRecognized);
    EXPECT_EQ(block.PicaLutInputAbsD0DisableBit, 0u);
    EXPECT_TRUE(block.PicaLutInputAbsD0DisableBitResolved);
    EXPECT_TRUE(block.Flag2);
    EXPECT_EQ(block.PicaLutInputAbsSpDisableBit, 0u);
    EXPECT_TRUE(block.PicaLutInputAbsSpDisableBitResolved);
    EXPECT_EQ(block.PicaLutScaleSp, 1u);
    EXPECT_TRUE(block.PicaLutScaleSpResolved);
    EXPECT_FALSE(block.Flag3);
    EXPECT_TRUE(block.Flag4);
    EXPECT_TRUE(block.Flag5);
    EXPECT_EQ(block.PicaLutInputFr, 1u);
    EXPECT_TRUE(block.PicaLutInputFrResolved);
    EXPECT_EQ(block.PicaLutInputAbsFrDisableBit, 0u);
    EXPECT_TRUE(block.PicaLutInputAbsFrDisableBitResolved);
    EXPECT_TRUE(block.Flag0);
    EXPECT_EQ(block.PicaLutInputAbsRbDisableBit, 0u);
    EXPECT_TRUE(block.PicaLutInputAbsRbDisableBitResolved);
    EXPECT_EQ(block.PicaLutInputRaw, 0x62A5u);
    EXPECT_EQ(block.PicaLutInput, 5u);
    EXPECT_TRUE(block.PicaLutInputRecognized);
    EXPECT_EQ(block.PicaLutInputRb, 5u);
    EXPECT_TRUE(block.PicaLutInputRbRecognized);
    EXPECT_EQ(block.PicaLutScaleSourceBits, 0x3E800000u);
    EXPECT_FLOAT_EQ(block.PicaLutScaleSourceValue, 0.25f);
    EXPECT_EQ(block.PicaLutScale, 6u);
    EXPECT_TRUE(block.PicaLutScaleRecognized);
    EXPECT_EQ(block.PicaLutScaleRb, 6u);
    EXPECT_TRUE(block.PicaLutScaleRbRecognized);
}

TEST(Oot3dNativeAssets, DecodesCmbCullFaceThroughNativePicaMapping) {
    using ThreeDsRecomp::Oot3d::DecodeOot3dCmbPicaCullMode;
    using ThreeDsRecomp::Oot3d::Oot3dNativePicaCullMode;

    EXPECT_EQ(DecodeOot3dCmbPicaCullMode(0), Oot3dNativePicaCullMode::KeepClockwise);
    EXPECT_EQ(DecodeOot3dCmbPicaCullMode(1), Oot3dNativePicaCullMode::KeepCounterClockwise);
    EXPECT_EQ(DecodeOot3dCmbPicaCullMode(2), Oot3dNativePicaCullMode::KeepClockwise);
    EXPECT_EQ(DecodeOot3dCmbPicaCullMode(3), Oot3dNativePicaCullMode::KeepAll);
    EXPECT_EQ(DecodeOot3dCmbPicaCullMode(0, ThreeDsRecomp::Oot3d::kOot3dNativeFrontFaceClockwise),
              Oot3dNativePicaCullMode::KeepCounterClockwise);
}

TEST(Oot3dNativeAssets, ParsesCmbPostMaterialTextureEnvTailBinding) {
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr uint32_t textureEnvTailOff =
        static_cast<uint32_t>(materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize);
    auto bytes = MinimalTriangleCmbWithTextureEnvTail();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_texenv_tail.cmb");

    ASSERT_EQ(model.TextureEnvSettings.size(), 1u);
    EXPECT_EQ(model.TextureEnvSettings[0].SourceOffset, textureEnvTailOff);
    ASSERT_EQ(model.Materials.size(), 1u);
    const auto& material = model.Materials[0];
    EXPECT_TRUE(material.PostMaterialTextureEnvTableDecoded);
    EXPECT_TRUE(material.PostMaterialTextureEnvTableDerivedFromLanePointer);
    EXPECT_EQ(material.PostMaterialTextureEnvTableSourceOffset, textureEnvTailOff);
    EXPECT_EQ(material.PostMaterialTextureEnvTableRecordSize,
              ThreeDsRecomp::Oot3d::kOot3dCmbMaterialTextureEnvSize);
    EXPECT_EQ(material.PostMaterialTextureEnvTableRecordCount, 1u);
    EXPECT_EQ(material.RawTextureStageCount, 1u);
    EXPECT_EQ(material.RawTextureStageSlots[0], 0);
    EXPECT_TRUE(material.PostMaterialTextureEnvStageResolved[0]);
    EXPECT_EQ(material.PostMaterialTextureEnvStageSourceOffsets[0], textureEnvTailOff);
    ASSERT_EQ(material.TextureEnvStages.size(), 1u);
    EXPECT_EQ(material.TextureEnvStages[0].SourceOffset, textureEnvTailOff);
}

TEST(Oot3dNativeAssets, ExposesCmbPostMaterialTextureEnvTailBindingInRenderSummary) {
    constexpr size_t matsOff = 0x88;
    constexpr size_t materialOff = matsOff + 0x0C;
    constexpr uint32_t textureEnvTailOff =
        static_cast<uint32_t>(materialOff + ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize);
    auto bytes = MinimalTriangleCmbWithTextureEnvTail();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_texenv_tail.cmb");
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);

    EXPECT_EQ(summary["native_material_post_material_texture_env_table_decoded_batch_count"], 1);
    EXPECT_EQ(summary["native_material_post_material_texture_env_stage_resolved_batch_count"], 1);
    ASSERT_EQ(summary["native_materials"].size(), 1u);
    const auto& material = summary["native_materials"][0];
    const auto& tail = material["post_material_texture_env_table"];
    EXPECT_TRUE(tail["decoded"]);
    EXPECT_TRUE(tail["derived_from_lane_pointer"]);
    EXPECT_EQ(tail["source"].get<std::string>(),
              "oot3d_codebin_004c34ac_lane_0x08_mats_texture_env_table_base");
    EXPECT_EQ(tail["table_source_offset"], textureEnvTailOff);
    EXPECT_EQ(tail["record_size"], ThreeDsRecomp::Oot3d::kOot3dCmbMaterialTextureEnvSize);
    EXPECT_EQ(tail["record_count"], 1);
    EXPECT_TRUE(tail["stage_resolved"][0]);
    EXPECT_EQ(tail["stage_source_offsets"][0], textureEnvTailOff);
    ASSERT_EQ(material["texture_env_stages"].size(), 1u);
    EXPECT_EQ(material["texture_env_stages"][0]["source_offset"], textureEnvTailOff);
}

TEST(Oot3dNativeAssets, AppliesNativeCmbResourceVisibilityDuringRenderModelBuild) {
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(MinimalTriangleCmb(), "triangle_visibility.cmb");
    ASSERT_EQ(model.Meshes.size(), 1u);
    model.Meshes[0].VisibilityId = 3;
    auto secondMesh = model.Meshes[0];
    secondMesh.Index = 1;
    secondMesh.VisibilityId = 4;
    model.Meshes.push_back(secondMesh);

    std::vector<uint8_t> resourceVisibility(5, 0);
    resourceVisibility[4] = 1;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelBuildOptions options;
    options.ResourceVisibility = &resourceVisibility;
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model, options);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    EXPECT_EQ(renderModel.Batches[0].MeshIndex, 1u);
    EXPECT_EQ(renderModel.Batches[0].VisibilityId, 4u);
    EXPECT_TRUE(renderModel.NativeCmbResourceVisibilityApplied);
    EXPECT_EQ(renderModel.NativeCmbResourceVisibility, resourceVisibility);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_TRUE(summary["native_cmb_resource_visibility_applied"]);
    EXPECT_EQ(summary["native_cmb_resource_visibility_count"], 5u);
    ASSERT_EQ(summary["native_cmb_visible_resource_ids"].size(), 1u);
    EXPECT_EQ(summary["native_cmb_visible_resource_ids"][0], 4u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_EQ(summary["native_batches"][0]["visibility_id"], 4u);
}

TEST(Oot3dNativeAssets, AppliesCmbNormalMappingTextureCoordinatesBeforeMaterialTransform) {
    auto model = SyntheticNormalMappedTextureCoordModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& vertices = renderModel.Batches[0].Vertices;
    ASSERT_EQ(vertices.size(), 3u);
    EXPECT_NEAR(vertices[0].Uv0.X, -0.1f, 1.0e-6f);
    EXPECT_NEAR(vertices[0].Uv0.Y, 4.2f, 1.0e-6f);
    EXPECT_NEAR(vertices[1].Uv0.X, 0.5f, 1.0e-6f);
    EXPECT_NEAR(vertices[1].Uv0.Y, 3.0f, 1.0e-6f);
    EXPECT_NEAR(vertices[2].Uv0.X, 1.1f, 1.0e-6f);
    EXPECT_NEAR(vertices[2].Uv0.Y, 1.8f, 1.0e-6f);
}

TEST(Oot3dNativeAssets, ResolvesPicaTexture0Texture1AddMultiplyConstantProgram) {
    auto model = SyntheticTexture0Texture1AddMultiplyConstantModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_texture0_texture1_add_multiply_texture0_then_constant_modulate");
    EXPECT_TRUE(program.Texture0Texture1AddMultiplyTexture0Resolved);
    EXPECT_EQ(program.Texture0Texture1AddMultiplyTexture0MapperSlot, 1u);
    EXPECT_EQ(material.SecondaryTextureIndex, 1);
    EXPECT_TRUE(program.TextureColorMultiplierResolved);
    EXPECT_NEAR(program.TextureColorMultiplier.X, 1.6f, 1.0e-6f);
    EXPECT_NEAR(program.TextureColorMultiplier.Y, 220.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(program.TextureColorMultiplier.Z, 0.0f, 1.0e-6f);
    EXPECT_TRUE(program.AlphaMultiplierResolved);
    EXPECT_NEAR(program.AlphaMultiplier, 179.0f / 255.0f, 1.0e-6f);
    EXPECT_EQ(program.ConstantColorIndex, 0);

    material.TextureEnvProgram.ColorShaderPathApplied = true;
    EXPECT_EQ(ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
                  renderModel, 5, { 255, 255, 255, 128 }, "synthetic_runtime_alpha"),
              1u);
    EXPECT_TRUE(material.TextureEnvProgram.ColorShaderPathApplied);
    EXPECT_NEAR(material.TextureEnvProgram.AlphaMultiplier, 128.0f / 255.0f, 1.0e-6f);
}

TEST(Oot3dNativeAssets, ResolvesPicaTexture0Texture1AddThenPrimaryColorProgram) {
    auto model = SyntheticTexture0Texture1AddThenPrimaryColorModel();
    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_texture0_texture1_add_then_primary_color_modulate");
    EXPECT_TRUE(program.Texture0Texture1AddThenPrimaryColorModulateResolved);
    EXPECT_EQ(program.Texture0Texture1AddThenPrimaryColorModulateStageCount, 2u);
    EXPECT_EQ(program.Texture0Texture1AddThenPrimaryColorModulateMapperSlot, 1u);
    EXPECT_EQ(material.SecondaryTextureIndex, 1);
    EXPECT_TRUE(program.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_TRUE(program.AlphaMultiplierResolved);
    EXPECT_NEAR(program.AlphaMultiplier, 179.0f / 255.0f, 1.0e-6f);
    EXPECT_FALSE(material.NativeMaterialCombinerRequiresDecoder);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_material_combiner_pending_batch_count"], 0u);
    ASSERT_EQ(summary["native_materials"].size(), 1u);
    const auto& jsonProgram = summary["native_materials"][0]["texture_env_program"];
    EXPECT_TRUE(jsonProgram["texture0_texture1_add_then_primary_color_modulate_resolved"]);
}

TEST(Oot3dNativeAssets, ResolvesPicaPreviousConstantAlphaAcrossRgbMultAddStage) {
    const auto model = SyntheticTexture0Texture1AddWithPreviousConstantAlphaModel();
    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_texture0_texture1_vertex_color_add");
    EXPECT_TRUE(program.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_EQ(program.Texture0PrimaryColorAlphaModulateStageCount, 1u);
    EXPECT_TRUE(program.AlphaMultiplierResolved);
    EXPECT_EQ(program.AlphaMultiplierStageCount, 1u);
    EXPECT_NEAR(program.AlphaMultiplier, 204.0f / 255.0f, 1.0e-6f);
    EXPECT_EQ(program.AlphaMultiplierSource,
              "oot3d_pica_texenv_previous_constant_alpha_chain");
}

TEST(Oot3dNativeAssets, PreservesNativeMaterialWhileApplyingDerivedTextureEnvShaderRoute) {
    const auto model = SyntheticTexture0Texture1AddThenPrimaryColorModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    ASSERT_FALSE(renderModel.Batches[0].Material.TextureEnvProgram.ColorShaderPathApplied);

    ThreeDsRecomp::Oot3d::Oot3dNativePicaLightingRenderState lighting;
    lighting.Available = true;
    lighting.ModulateTexturedBatches = true;
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, renderModel, true);

    const auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.TextureEnvProgram.ColorShaderPathApplied);
    EXPECT_TRUE(material.VertexColorModulatesTexture);
    EXPECT_EQ(material.TextureEnvProgram.ColorShaderPath,
              "texenv_program_texture0_texture1_add_then_primary_color_modulate");
    EXPECT_EQ(material.SecondaryTextureIndex, 1);
}

TEST(Oot3dNativeAssets, ResolvesPicaPrimaryTextureScaleThenConstantAlphaProgram) {
    const auto model = SyntheticTexturePrimaryScaleThenConstantAlphaModel();
    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_texture0_vertex_color_constant_factor");
    EXPECT_TRUE(program.PrimaryColorMultiplierResolved);
    EXPECT_NEAR(program.PrimaryColorMultiplier.X, 2.0f, 1.0e-6f);
    EXPECT_NEAR(program.PrimaryColorMultiplier.Y, 2.0f, 1.0e-6f);
    EXPECT_NEAR(program.PrimaryColorMultiplier.Z, 2.0f, 1.0e-6f);
    EXPECT_TRUE(program.TextureColorMultiplierResolved);
    EXPECT_NEAR(program.TextureColorMultiplier.X, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(program.TextureColorMultiplier.Y, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_NEAR(program.TextureColorMultiplier.Z, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_TRUE(program.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_FALSE(material.NativeMaterialCombinerRequiresDecoder);
}

TEST(Oot3dNativeAssets, ResolvesExplicitZeroTextureEnvConstantSelector) {
    auto model = SyntheticTexturePrimaryScaleThenConstantAlphaModel();
    model.Materials[0].ConstantColors.fill({ 0, 0, 0, 255 });
    model.Materials[0].ConstantColors[0] = { 0, 0, 0, 0 };
    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_texture0_vertex_color_constant_factor");
    EXPECT_TRUE(program.ConstantColorResolved);
    EXPECT_EQ(program.ConstantColorIndex, 0);
    EXPECT_EQ(program.ConstantColorSource,
              "cmb_texture_env_stage_constant_selector");
    EXPECT_TRUE(program.TextureColorMultiplierResolved);
    EXPECT_FLOAT_EQ(program.TextureColorMultiplier.X, 0.0f);
    EXPECT_FLOAT_EQ(program.TextureColorMultiplier.Y, 0.0f);
    EXPECT_FLOAT_EQ(program.TextureColorMultiplier.Z, 0.0f);
    EXPECT_FALSE(material.NativeMaterialCombinerRequiresDecoder);
}

TEST(Oot3dNativeAssets, ResolvesPicaTexture1Texture2MultiplyAddPreviousProgram) {
    const auto model = SyntheticTexture1Texture2MultiplyAddPreviousModel();
    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    const auto& program = material.TextureEnvProgram;
    EXPECT_TRUE(program.ColorShaderPathSupported);
    EXPECT_EQ(program.ColorShaderPath,
              "texenv_program_primary_texture0_then_texture1_texture2_multiply_add_previous");
    EXPECT_TRUE(program.Texture1Texture2MultiplyAddPreviousResolved);
    EXPECT_EQ(program.Texture1Texture2MultiplyAddPreviousTexture1MapperSlot, 1u);
    EXPECT_EQ(program.Texture1Texture2MultiplyAddPreviousTexture2MapperSlot, 2u);
    EXPECT_TRUE(program.PrimaryColorMultiplierResolved);
    EXPECT_NEAR(program.PrimaryColorMultiplier.X, 2.0f, 1.0e-6f);
    EXPECT_EQ(material.SecondaryTextureIndex, 1);
    EXPECT_EQ(material.TertiaryTextureIndex, 2);
    EXPECT_TRUE(material.TextureMapperSamplerStates[1].Decoded);
    EXPECT_EQ(material.TextureMapperSamplerStates[1].MagFilter, 0x2601u);
    EXPECT_EQ(material.TextureMapperSamplerStates[1].WrapT, 0x812Fu);
    EXPECT_TRUE(material.TextureMapperSamplerStates[2].Decoded);
    EXPECT_EQ(material.TextureMapperSamplerStates[2].MinFilter, 0x2601u);
    EXPECT_EQ(material.TextureMapperSamplerStates[2].WrapS, 0x812Fu);
    EXPECT_FALSE(material.NativeMaterialCombinerRequiresDecoder);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_material_combiner_pending_batch_count"], 0u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_TRUE(summary["native_batches"][0]["secondary_texture"]["resolved"]);
    EXPECT_TRUE(summary["native_batches"][0]["tertiary_texture"]["resolved"]);
}

TEST(Oot3dNativeAssets, ResolvesTexture0ConstantAlphaAndCullsUnusedFragmentLutEvaluation) {
    const auto model = SyntheticTexture0ConstantAlphaModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.TextureEnvProgram.ColorShaderPathSupported);
    EXPECT_EQ(material.TextureEnvProgram.ColorShaderPath, "texenv_program_texture0");
    EXPECT_FALSE(material.TextureEnvProgram.UsesPrimaryColor);
    EXPECT_TRUE(material.TextureEnvProgram.Texture0ConstantColorAlphaModulateResolved);
    EXPECT_TRUE(material.TextureEnvProgram.AlphaMultiplierResolved);
    EXPECT_NEAR(material.TextureEnvProgram.AlphaMultiplier, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_FALSE(material.NativeMaterialCombinerRequiresDecoder);

    material.MaterialPicaLutInput.Available = true;
    material.MaterialPicaLutInput.Complete = true;
    material.MaterialPicaLutInput.SourceKind = "synthetic_complete_pica_lut_packet";
    ThreeDsRecomp::Oot3d::Oot3dNativePicaLightingRenderState lighting;
    lighting.Available = true;
    lighting.ModulateTexturedBatches = true;
    lighting.MaterialLightingEnableSource = "oot3d_cmb_material_fragment_lighting_flag";
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, renderModel);

    EXPECT_TRUE(material.NativePicaMaterialLutInputPacketUsedForFragmentLighting);
    EXPECT_FALSE(material.NativePicaMaterialLutInputEvaluationRequired);
    EXPECT_TRUE(material.NativePicaMaterialLutInputEvaluationCulledAsUnused);
    EXPECT_FALSE(material.NativePicaMaterialLutInputEvaluationPending);
    EXPECT_FALSE(material.VertexColorModulatesTexture);
    EXPECT_NE(material.NativePicaMaterialLutInputApplicationSource.find(
                  "texture_env_fragment_lighting_output_unused"),
              std::string::npos);
}

TEST(Oot3dNativeAssets, EnablesPicaFogOnlyForRuntimeListBoundRoomMaterials) {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    const auto unboundModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(unboundModel.Batches.size(), 1u);
    EXPECT_TRUE(unboundModel.Batches[0].Material.NativePicaFogOverrideDecoded);
    EXPECT_FALSE(unboundModel.Batches[0].Material.NativePicaFogEnabled);
    EXPECT_NE(unboundModel.Batches[0].Material.NativePicaFogOverrideSource.find(
                  "material_packet_enable_offset_0x0a_default_clear"),
              std::string::npos);

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
    scene.RoomModel = std::move(model);
    ThreeDsRecomp::Oot3d::CsabPose pose;
    std::vector<ThreeDsRecomp::Oot3d::Matrix4f> skinTransforms;
    const auto renderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(
        scene, pose, skinTransforms, 0.0f, nullptr, false);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    EXPECT_TRUE(renderScene.Room.Batches[0].Material.NativePicaFogOverrideDecoded);
    EXPECT_TRUE(renderScene.Room.Batches[0].Material.NativePicaFogEnabled);
    EXPECT_NE(renderScene.Room.Batches[0].Material.NativePicaFogOverrideSource.find(
                  "002d960c_play_0x4c30_0x500c_runtime_material_list_binding"),
              std::string::npos);
}

TEST(Oot3dNativeAssets, FiltersCutsceneActorVisualsByDecodedNativeBehavior) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
    scene.ActorVisualModels.resize(2);
    scene.ActorVisualModels[0].ActorName = "unresolved";
    scene.ActorVisualModels[1].ActorName = "resolved";

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoActorVisualInstance unresolved;
    unresolved.ActorEntryIndex = 0;
    unresolved.ActorName = "unresolved";
    unresolved.ModelIndex = 0;
    scene.ActorVisualInstances.push_back(unresolved);

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoActorVisualInstance resolved;
    resolved.ActorEntryIndex = 1;
    resolved.ActorName = "resolved";
    resolved.ModelIndex = 1;
    resolved.NativeVisualBehaviorResolved = true;
    scene.ActorVisualInstances.push_back(resolved);

    ThreeDsRecomp::Oot3d::CsabPose pose;
    std::vector<ThreeDsRecomp::Oot3d::Matrix4f> skinTransforms;
    const auto renderScene = ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoRenderScene(
        scene, pose, skinTransforms, 0.0f, nullptr, false,
        ThreeDsRecomp::Oot3d::Oot3dNativeActorVisualSelection::NativeBehaviorResolved);

    ASSERT_EQ(renderScene.ActorVisuals.size(), 1u);
    EXPECT_EQ(renderScene.ActorVisuals[0].Name, "resolved#1:");
}

TEST(Oot3dNativeAssets, ResolvesPicaTexture0PrimaryColorAlphaModulateProgram) {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    model.TextureEnvSettings.resize(1);
    model.Materials[0].TextureEnvStages.resize(1);
    model.Materials[0].RawTextureStageCount = 1;
    model.Materials[0].RawTextureStageSlots = { 0, -1, -1, -1, -1, -1 };

    const auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& program = renderModel.Batches[0].Material.TextureEnvProgram;
    EXPECT_TRUE(program.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_EQ(program.Texture0PrimaryColorAlphaModulateStageCount, 1u);
    EXPECT_EQ(program.Texture0PrimaryColorAlphaModulateSource,
              "oot3d_pica_texenv_alpha_modulate_texture0_primary_color");

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_TRUE(summary["native_batches"][0]["texture_env_program"]
                       ["texture0_primary_color_alpha_modulate_resolved"]);
}

TEST(Oot3dNativeAssets, ExposesCmbMaterialLightingBlockInRenderSummary) {
    auto bytes = MinimalTriangleCmb();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle.cmb");
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);

    EXPECT_EQ(summary["native_material_lighting_complete_batch_count"], 0);
    EXPECT_EQ(summary["native_material_lighting_incomplete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_block_decoded_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_lut_input_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_lut_input_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_lut_input_fragment_lighting_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_lut_input_evaluation_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_lut_input_evaluation_applied_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_bump_mode_available_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_bump_mode_recognized_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_bump_mode_active_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_bump_mode_backend_pending_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_flag3_available_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_flag3_active_batch_count"], 0);
    EXPECT_EQ(summary["native_material_lighting_flag3_backend_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_fragment_lighting_config_batch_count"], 1);
    EXPECT_EQ(summary["native_material_fragment_lighting_config_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_fragment_lighting_config_runtime_override_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_fragment_lighting_config_known_payload_word_count"], 4);
    EXPECT_EQ(summary["native_cmb_lut_chunk_decoded"], true);
    EXPECT_EQ(summary["native_cmb_lut_record_count"], 1);
    EXPECT_EQ(summary["native_cmb_lut_declared_record_count"], 1);
    EXPECT_EQ(summary["native_pica_cmb_lut_asset_decode_contract_available"], true);
    EXPECT_EQ(summary["native_pica_cmb_lut_final_shader_semantic_resolved"], false);
    EXPECT_EQ(summary["native_pica_cmb_lut_shader_evaluation_pending"], true);
    ASSERT_TRUE(summary["native_cmb_luts"].is_object());
    ASSERT_TRUE(summary["native_cmb_luts"]["records"].is_array());
    ASSERT_EQ(summary["native_cmb_luts"]["records"].size(), 1u);
    const auto& lutRecord = summary["native_cmb_luts"]["records"][0];
    EXPECT_EQ(lutRecord["type"], 2);
    EXPECT_EQ(lutRecord["sample_count"], 257);
    EXPECT_EQ(lutRecord["packed_base_value_count"], 256);
    EXPECT_EQ(lutRecord["packed_delta_value_count"], 256);
    EXPECT_NEAR(lutRecord["sample_128"].get<double>(),
                (3.0 - 2.0 * (128.0 / 255.0)) * (128.0 / 255.0) * (128.0 / 255.0),
                0.000001);
    ASSERT_TRUE(summary["native_materials"].is_array());
    ASSERT_FALSE(summary["native_materials"].empty());
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_material_lut_input_packet_available"]);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_material_lut_input_packet_complete"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_pica_material_lut_input_packet_used_for_fragment_lighting"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_pica_material_lut_input_evaluation_pending"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_pica_material_lut_input_evaluation_applied"]);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_bump_mode_available"]);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_bump_mode_recognized"]);
    EXPECT_EQ(summary["native_materials"][0]["native_pica_bump_mode"], 1);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_bump_mode_active"]);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_bump_mode_backend_pending"]);
    EXPECT_EQ(
        summary["native_materials"][0]["native_pica_bump_mode_source"],
        "oot3d_cmb_material_lighting_block_pica_bump_mode_normal_map_missing_texture_unit");
    EXPECT_TRUE(summary["native_materials"][0]["native_material_lighting_flag3_available"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_material_lighting_flag3_active"]);
    EXPECT_FALSE(
        summary["native_materials"][0]["native_material_lighting_flag3_backend_pending"]);
    EXPECT_EQ(summary["native_materials"][0]["native_material_lighting_flag3_source"],
              "oot3d_cmb_material_lighting_block_flag3_disabled");
    EXPECT_FALSE(summary["native_materials"][0]["native_material_lighting_complete"]);
    ASSERT_TRUE(
        summary["native_materials"][0]["native_material_lighting_incomplete_reasons"].is_array());
    EXPECT_EQ(
        summary["native_materials"][0]["native_material_lighting_incomplete_reasons"][0],
        "pica_bump_mode_backend_pending");
    EXPECT_EQ(
        summary["native_materials"][0]["native_material_lighting_complete_source"],
        "oot3d_native_cmb_material_lighting_block_lut_fragment_config_vertex_vector_bump_flag3_textureenv_contract");
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_fragment_lighting_config_packet_available"]);
    EXPECT_TRUE(summary["native_materials"][0]["native_pica_fragment_lighting_config_packet_complete"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_pica_fragment_lighting_config_runtime_override_pending"]);
    EXPECT_EQ(summary["native_materials"][0]["native_pica_fragment_lighting_config_known_payload_word_count"],
              4);
    const auto& block = summary["native_materials"][0]["material_lighting_block"];
    EXPECT_EQ(block["source"], "oot3d_cmb_material_lighting_block_offset_0xcc");
    EXPECT_EQ(block["source_offset"], "0x0cc");
    EXPECT_EQ(block["pica_bump_texture_unit"]["native"], "0x84c2");
    EXPECT_EQ(block["pica_bump_texture_unit"]["decoded"], 2);
    EXPECT_EQ(block["pica_lighting_config"]["decoded"], 8);
    EXPECT_EQ(block["unresolved_enum4"]["decoded"], 3);
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["decoded"], 3);
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["register"], "0x1d0");
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["field_name"], "disable_d0");
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["sampler"], "d0");
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["pica_disable_bit"], 0);
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["native_selector_disable_bit_by_decoded_value"][0], 1);
    EXPECT_EQ(block["pica_lut_input_abs_d0"]["native_selector_disable_bit_by_decoded_value"][3], 0);
    EXPECT_EQ(block["pica_lut_input_abs_sp"]["field_name"], "disable_sp");
    EXPECT_EQ(block["pica_lut_input_abs_sp"]["bit_shift"], 9);
    EXPECT_EQ(block["pica_lut_input_abs_sp"]["pica_disable_bit"], 0);
    EXPECT_EQ(block["pica_lut_scale_sp"]["register"], "0x1d2");
    EXPECT_EQ(block["pica_lut_scale_sp"]["field_name"], "sp");
    EXPECT_EQ(block["pica_lut_scale_sp"]["decoded_lighting_scale"], 1);
    EXPECT_EQ(block["pica_lut_input_fr"]["register"], "0x1d1");
    EXPECT_EQ(block["pica_lut_input_fr"]["field_name"], "fr");
    EXPECT_EQ(block["pica_lut_input_fr"]["decoded_lighting_lut_input"], 1);
    EXPECT_EQ(block["pica_lut_input_abs_fr"]["field_name"], "disable_fr");
    EXPECT_EQ(block["pica_lut_input_abs_fr"]["bit_shift"], 13);
    EXPECT_EQ(block["pica_lut_input_abs_fr"]["pica_disable_bit"], 0);
    EXPECT_EQ(block["pica_lut_input_abs_rb"]["field_name"], "disable_rb");
    EXPECT_EQ(block["pica_lut_input_abs_rb"]["bit_shift"], 17);
    EXPECT_EQ(block["pica_lut_input_abs_rb"]["pica_disable_bit"], 0);
    EXPECT_EQ(block["unresolved_enum4_semantic_resolved"], true);
    EXPECT_EQ(block["unresolved_enum4_semantic_alias"], "pica_lut_input_abs_d0");
    EXPECT_EQ(block["pica_lut_input_rb"]["register"], "0x1d1");
    EXPECT_EQ(block["pica_lut_input_rb"]["field_name"], "rb");
    EXPECT_EQ(block["pica_lut_input_rb"]["bit_shift"], 16);
    EXPECT_EQ(block["pica_lut_input_rb"]["decoded_lighting_lut_input"], 5);
    EXPECT_EQ(block["pica_lut_scale"]["native_bits"], "0x3e800000");
    EXPECT_EQ(block["pica_lut_scale"]["decoded"], 6);
    EXPECT_EQ(block["pica_lut_scale_rb"]["register"], "0x1d2");
    EXPECT_EQ(block["pica_lut_scale_rb"]["field_name"], "rb");
    EXPECT_EQ(block["pica_lut_scale_rb"]["bit_shift"], 16);
    EXPECT_EQ(block["pica_lut_scale_rb"]["decoded_lighting_scale"], 6);
    EXPECT_EQ(block["flags"]["flag3"]["enabled"], false);
    EXPECT_EQ(block["flag_semantics_resolved"], false);
    EXPECT_EQ(block["resolved_flag_semantics"]["flag0"], "pica_lut_input_abs_rb_disable_bit");
    ASSERT_TRUE(block["native_pica_config_packet"].is_object());
    EXPECT_EQ(block["native_pica_config_packet"]["emitter_address"], "0x0040d040");
    EXPECT_EQ(block["native_pica_config_packet"]["packet_headers"][0], 0x000F01D0);
    EXPECT_EQ(block["native_pica_config_packet"]["boolean_byte_offsets"][0], 0x195);
    EXPECT_EQ(block["native_pica_config_packet"]["primary_nibble_byte_offsets"][0], 0x194);
    EXPECT_EQ(block["native_pica_config_packet"]["secondary_nibble_byte_offsets"][2], 0x19E);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lighting_config"],
              0x194);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_abs_d0"],
              0x195);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_abs_sp"],
              0x19D);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_scale_sp"],
              0x19E);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_fr"],
              0x1A0);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_abs_fr"],
              0x1A1);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_abs_rb"],
              0x1A5);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_input_rb"],
              0x1A4);
    EXPECT_EQ(block["native_pica_config_packet"]["resolved_material_block_runtime_offsets"]
                   ["pica_lut_scale_rb"],
              0x1A6);
    EXPECT_EQ(block["native_pica_config_packet"]["pica_lut_input_abs_d0"]["field_name"],
              "disable_d0");
    EXPECT_EQ(block["native_pica_config_packet"]["unresolved_material_block_runtime_offsets"]
                   ["flag3"],
              0x19F);
    const auto& picaLutInput = summary["native_materials"][0]["material_pica_lut_input"];
    EXPECT_TRUE(picaLutInput["available"]);
    EXPECT_TRUE(picaLutInput["complete"]);
    EXPECT_EQ(picaLutInput["source_kind"],
              "oot3d_cmb_material_lighting_block_codebin_0040d040");
    EXPECT_EQ(picaLutInput["source_status"],
              "complete_register_bits_from_codebin_lane_reset_and_cmb_material_block");
    EXPECT_EQ(picaLutInput["consumer_address"], "0x0040d040");
    EXPECT_EQ(picaLutInput["packet_word_count"], 6);
    EXPECT_EQ(picaLutInput["resolved_field_count"], 21);
    ASSERT_TRUE(picaLutInput["registers"].is_array());
    ASSERT_EQ(picaLutInput["registers"].size(), 3u);
    EXPECT_EQ(picaLutInput["registers"][0]["register"], "0x1d0");
    EXPECT_EQ(picaLutInput["registers"][0]["register_name"],
              "GPUREG_LIGHTING_LUTINPUT_ABS");
    EXPECT_EQ(picaLutInput["registers"][0]["known_bit_mask"], "0x02222222");
    EXPECT_EQ(picaLutInput["registers"][0]["known_value"], "0x02200020");
    EXPECT_EQ(picaLutInput["registers"][1]["register"], "0x1d1");
    EXPECT_EQ(picaLutInput["registers"][1]["known_bit_mask"], "0x0fffffff");
    EXPECT_EQ(picaLutInput["registers"][1]["known_value"], "0x00051028");
    EXPECT_EQ(picaLutInput["registers"][2]["register"], "0x1d2");
    EXPECT_EQ(picaLutInput["registers"][2]["known_bit_mask"], "0x0fffffff");
    EXPECT_EQ(picaLutInput["registers"][2]["known_value"], "0x00060100");
    ASSERT_TRUE(picaLutInput["samplers"].is_array());
    ASSERT_EQ(picaLutInput["samplers"].size(), 7u);
    EXPECT_EQ(picaLutInput["samplers"][0]["sampler"], "d0");
    EXPECT_EQ(picaLutInput["samplers"][0]["pica_role"], "distribution0");
    EXPECT_TRUE(picaLutInput["samplers"][0]["complete"]);
    EXPECT_TRUE(picaLutInput["samplers"][0]["abs_input_enabled"]);
    EXPECT_EQ(picaLutInput["samplers"][0]["lut_input_raw"], 8);
    EXPECT_TRUE(picaLutInput["samplers"][0]["lut_input_raw_high_bit_set"]);
    EXPECT_EQ(picaLutInput["samplers"][0]["lut_input"], 0);
    EXPECT_EQ(picaLutInput["samplers"][0]["lut_input_name"], "NH");
    EXPECT_FALSE(picaLutInput["samplers"][0]["lut_input_shader_semantic_resolved"]);
    EXPECT_DOUBLE_EQ(picaLutInput["samplers"][0]["scale_value"], 1.0);
    EXPECT_EQ(picaLutInput["samplers"][1]["sampler"], "d1");
    EXPECT_EQ(picaLutInput["samplers"][1]["pica_role"], "distribution1");
    EXPECT_FALSE(picaLutInput["samplers"][1]["abs_input_enabled"]);
    EXPECT_EQ(picaLutInput["samplers"][1]["lut_input_raw"], 2);
    EXPECT_EQ(picaLutInput["samplers"][1]["lut_input_name"], "NV");
    EXPECT_TRUE(picaLutInput["samplers"][1]["lut_input_shader_semantic_resolved"]);
    EXPECT_EQ(picaLutInput["samplers"][2]["sampler"], "sp");
    EXPECT_EQ(picaLutInput["samplers"][2]["pica_role"], "spotlight_attenuation");
    EXPECT_EQ(picaLutInput["samplers"][2]["lut_input_name"], "NH");
    EXPECT_DOUBLE_EQ(picaLutInput["samplers"][2]["scale_value"], 2.0);
    EXPECT_EQ(picaLutInput["samplers"][3]["sampler"], "fr");
    EXPECT_EQ(picaLutInput["samplers"][3]["pica_role"], "fresnel");
    EXPECT_EQ(picaLutInput["samplers"][3]["lut_input_name"], "VH");
    EXPECT_EQ(picaLutInput["samplers"][4]["sampler"], "rb");
    EXPECT_EQ(picaLutInput["samplers"][4]["pica_role"], "reflect_blue");
    EXPECT_EQ(picaLutInput["samplers"][4]["lut_input_name"], "CP");
    EXPECT_DOUBLE_EQ(picaLutInput["samplers"][4]["scale_value"], 0.25);
    EXPECT_EQ(picaLutInput["samplers"][5]["sampler"], "rg");
    EXPECT_FALSE(picaLutInput["samplers"][5]["abs_input_enabled"]);
    EXPECT_EQ(picaLutInput["samplers"][6]["sampler"], "rr");
    EXPECT_FALSE(picaLutInput["samplers"][6]["abs_input_enabled"]);
    EXPECT_EQ(picaLutInput["resolved_fields"][0],
              "GPUREG_LIGHTING_LUTINPUT_ABS.disable_d0");
    EXPECT_EQ(picaLutInput["resolved_fields"][20],
              "GPUREG_LIGHTING_LUTINPUT_SCALE.rr");
    EXPECT_TRUE(picaLutInput["unresolved_fields"].empty());

    const auto& fragmentConfig =
        summary["native_materials"][0]["material_fragment_lighting_config"];
    EXPECT_TRUE(fragmentConfig["available"]);
    EXPECT_TRUE(fragmentConfig["complete"]);
    EXPECT_EQ(fragmentConfig["source_kind"],
              "oot3d_cmb_material_fragment_lighting_config_codebin_003fad68_00313d6c");
    EXPECT_EQ(fragmentConfig["source_status"],
              "complete_fragment_lighting_config_payload_from_codebin_and_cmb_material");
    EXPECT_EQ(
        fragmentConfig["primary_source_status"],
        "codebin_004c34ac_cmb_material_blend_gate_0x138_to_runtime_lane_0x1c0_"
        "default_autoclass1_gate_0x0b_clear");
    EXPECT_EQ(fragmentConfig["emitter_address"], "0x00313d6c");
    EXPECT_EQ(fragmentConfig["material_setup_address"], "0x003fad68");
    EXPECT_EQ(fragmentConfig["prepared_state_initializer_address"], "0x00313cec");
    EXPECT_EQ(fragmentConfig["packet_register"], "0x112");
    EXPECT_EQ(fragmentConfig["packet_header"], "0x803f0112");
    EXPECT_EQ(fragmentConfig["payload_word_count"], 4);
    EXPECT_EQ(fragmentConfig["known_payload_word_count"], 4);
    EXPECT_EQ(fragmentConfig["native_type"], "0x6030");
    EXPECT_EQ(fragmentConfig["flags"], "0x000f");
    EXPECT_TRUE(fragmentConfig["runtime_override_gate_resolved"]);
    EXPECT_TRUE(fragmentConfig["runtime_material_primary_source_resolved"]);
    EXPECT_TRUE(fragmentConfig["runtime_secondary_mode_source_resolved"]);
    EXPECT_TRUE(fragmentConfig["primary_enable_known"]);
    EXPECT_EQ(fragmentConfig["primary_enable"], 0);
    EXPECT_TRUE(fragmentConfig["primary_mode_known"]);
    EXPECT_EQ(fragmentConfig["primary_mode"], 0);
    EXPECT_TRUE(fragmentConfig["primary_runtime_lane_value_known"]);
    EXPECT_EQ(fragmentConfig["primary_runtime_lane_value"], 0);
    EXPECT_TRUE(fragmentConfig["primary_default_gate_payload0_candidate_known"]);
    EXPECT_EQ(fragmentConfig["primary_default_gate_payload0_candidate"], "0x00000000");
    EXPECT_EQ(
        fragmentConfig["runtime_override_gate_resolution_source"],
        "AutoClass1_00347258_default_gate_0x0b_clear_plus_002d5f68_draw_entry_override_only_when_flag_0x80000000");
    EXPECT_EQ(fragmentConfig["runtime_state_initializer_address"], "0x00347258");
    EXPECT_EQ(fragmentConfig["runtime_state_copy_helper_address"], "0x00310f7c");
    EXPECT_TRUE(fragmentConfig["runtime_override_gate_default_known"]);
    EXPECT_EQ(fragmentConfig["runtime_override_gate_default_value"], 0);
    EXPECT_TRUE(fragmentConfig["runtime_override_gate_copy_path_known"]);
    EXPECT_TRUE(fragmentConfig["runtime_secondary_mode_default_known"]);
    EXPECT_EQ(fragmentConfig["runtime_secondary_mode_default_value"], 1);
    EXPECT_TRUE(fragmentConfig["runtime_secondary_mode_copy_path_known"]);
    EXPECT_TRUE(fragmentConfig["runtime_state_writer_scan_resolved"]);
    EXPECT_TRUE(fragmentConfig["runtime_state_writers_classified_as_draw_local"]);
    EXPECT_FALSE(fragmentConfig["runtime_active_override_producer_resolved_from_writer_scan"]);
    ASSERT_TRUE(fragmentConfig["runtime_override_writer_addresses"].is_array());
    EXPECT_EQ(fragmentConfig["runtime_override_writer_addresses"].size(), 4u);
    EXPECT_EQ(fragmentConfig["runtime_override_writer_addresses"][2], "0x002d5f68");
    ASSERT_TRUE(fragmentConfig["runtime_secondary_mode_writer_addresses"].is_array());
    EXPECT_EQ(fragmentConfig["runtime_secondary_mode_writer_addresses"].size(), 4u);
    EXPECT_EQ(fragmentConfig["runtime_secondary_mode_writer_addresses"][0], "0x00228a34");
    EXPECT_TRUE(fragmentConfig["runtime_draw_entry_submit_route_resolved"]);
    EXPECT_TRUE(fragmentConfig["runtime_draw_entry_override_gate_rule_resolved"]);
    EXPECT_FALSE(fragmentConfig["runtime_draw_entry_route_promotes_active_material_override"]);
    EXPECT_EQ(fragmentConfig["runtime_gameplay_draw_address"], "0x002e25f0");
    EXPECT_EQ(fragmentConfig["runtime_gameplay_draw_dispatcher_address"], "0x00461904");
    EXPECT_EQ(fragmentConfig["runtime_gameplay_draw_dispatcher_callsite_address"], "0x002e2cdc");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_submit_address"], "0x002d5f68");
    ASSERT_TRUE(fragmentConfig["runtime_draw_entry_submit_callsite_addresses"].is_array());
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_submit_callsite_addresses"].size(), 2u);
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_submit_callsite_addresses"][0], "0x00461bb4");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_submit_callsite_addresses"][1], "0x00461d28");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_flags_offset"], "0x004");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_render_context_pointer_offset"], "0x178");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_callback_offset"], "0x140");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_visibility_state_offset"], "0x120");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_submitted_byte_offset"], "0x121");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_fade_counter_offset"], "0x19e");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_fade_limit_offset"], "0x19f");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_override_gate_flag_mask"], "0x80000000");
    EXPECT_EQ(fragmentConfig["runtime_draw_entry_override_gate_force_full_flag_mask"],
              "0x00000020");
    EXPECT_TRUE(fragmentConfig["runtime_submit_manager_material_route_resolved"]);
    EXPECT_FALSE(
        fragmentConfig["runtime_submit_manager_material_route_resolves_active_override_gate"]);
    EXPECT_EQ(fragmentConfig["runtime_submit_manager_vtable_address"], "0x004ebd78");
    EXPECT_EQ(fragmentConfig["runtime_submit_manager_material_config_slot_offset"], "0x28");
    EXPECT_EQ(fragmentConfig["runtime_submit_manager_material_config_address"], "0x003fac2c");
    EXPECT_EQ(fragmentConfig["runtime_submit_manager_material_state_slot_offset"], "0x44");
    EXPECT_EQ(fragmentConfig["runtime_submit_manager_material_state_address"], "0x003fad68");
    EXPECT_TRUE(fragmentConfig["runtime_material_lane_dispatch_resolved"]);
    EXPECT_FALSE(fragmentConfig["runtime_material_lane_dispatch_promotes_active_override_gate"]);
    EXPECT_EQ(fragmentConfig["runtime_material_lane_dispatch_address"], "0x00452854");
    EXPECT_EQ(fragmentConfig["runtime_material_lane_dispatch_loop_end_address"], "0x00452920");
    EXPECT_EQ(fragmentConfig["runtime_material_lane_dispatch_cmb_mesh_material_lane_byte_offset"],
              "0x002");
    EXPECT_EQ(fragmentConfig["runtime_material_lane_dispatch_material_lane_stride_bytes"],
              "0x1cc");
    EXPECT_EQ(fragmentConfig["runtime_material_lane_dispatch_source"],
              "oot3d_codebin_00452854_material_draw_dispatch_cmb_mesh_material_index");
    EXPECT_TRUE(fragmentConfig["secondary_enable_known"]);
    EXPECT_EQ(fragmentConfig["secondary_enable"], 0);
    EXPECT_TRUE(fragmentConfig["secondary_mode_known"]);
    EXPECT_EQ(fragmentConfig["secondary_mode"], 0);
    EXPECT_TRUE(fragmentConfig["secondary_type_selector_known"]);
    EXPECT_EQ(fragmentConfig["secondary_type_selector"], 3);
    EXPECT_TRUE(fragmentConfig["secondary_type_register_value_known"]);
    EXPECT_EQ(fragmentConfig["secondary_type_register_value"], "0x0000");
    EXPECT_TRUE(fragmentConfig["secondary_type_disabled"]);
    EXPECT_TRUE(fragmentConfig["primary_cmb_blend_gate_known"]);
    EXPECT_EQ(fragmentConfig["primary_cmb_blend_gate"], 0);
    EXPECT_TRUE(fragmentConfig["aux_byte_known"]);
    EXPECT_TRUE(fragmentConfig["aux_halfword_known"]);
    ASSERT_TRUE(fragmentConfig["payload_words"].is_array());
    ASSERT_EQ(fragmentConfig["payload_words"].size(), 4u);
    EXPECT_EQ(fragmentConfig["payload_words"][0]["register"], "0x112");
    EXPECT_TRUE(fragmentConfig["payload_words"][0]["known"]);
    EXPECT_EQ(fragmentConfig["payload_words"][0]["value"], "0x00000000");
    EXPECT_EQ(
        fragmentConfig["payload_words"][0]["source"],
        "FUN_00313D6C primary branch; primary enable from codebin_004c34ac runtime lane "
        "with AutoClass1 default gate clear");
    EXPECT_EQ(fragmentConfig["payload_words"][1]["register"], "0x113");
    EXPECT_TRUE(fragmentConfig["payload_words"][1]["known"]);
    EXPECT_EQ(fragmentConfig["payload_words"][1]["value"], "0x0000000f");
    EXPECT_EQ(fragmentConfig["payload_words"][2]["register"], "0x114");
    EXPECT_TRUE(fragmentConfig["payload_words"][2]["known"]);
    EXPECT_EQ(fragmentConfig["payload_words"][2]["value"], "0x00000000");
    EXPECT_EQ(fragmentConfig["payload_words"][3]["register"], "0x115");
    EXPECT_TRUE(fragmentConfig["payload_words"][3]["known"]);
    EXPECT_EQ(fragmentConfig["payload_words"][3]["value"], "0x00000000");
    EXPECT_EQ(
        fragmentConfig["payload_words"][3]["source"],
        "FUN_00313D6C secondary enable/mode branch from CMB secondary enable and "
        "AutoClass1 default gate material mode");
    EXPECT_TRUE(fragmentConfig["unresolved_fields"].empty());
}

TEST(Oot3dNativeAssets, ResolvesActorMaterialLightingNormalMapBumpTextureUnit) {
    auto model = SyntheticActorMaterialLightingNormalMapModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);

    EXPECT_EQ(summary["native_material_lighting_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_incomplete_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_lut_input_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_fragment_lighting_config_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_bump_mode_active_batch_count"], 1);
    EXPECT_EQ(summary["native_material_pica_bump_mode_backend_pending_batch_count"], 0);
    ASSERT_TRUE(summary["native_materials"].is_array());
    ASSERT_FALSE(summary["native_materials"].empty());
    const auto& material = summary["native_materials"][0];
    EXPECT_TRUE(material["native_pica_bump_texture_unit_recognized"]);
    EXPECT_EQ(material["native_pica_bump_texture_unit"], 0);
    EXPECT_EQ(material["native_pica_bump_texture_index"], 0);
    EXPECT_TRUE(material["native_pica_bump_normal_map_backend_supported"]);
    EXPECT_FALSE(material["native_pica_bump_normal_map_applied"]);
    EXPECT_EQ(material["native_pica_bump_mode_source"],
              "oot3d_cmb_material_lighting_block_pica_bump_mode_normal_map_vertex_backend");
    EXPECT_EQ(material["native_pica_bump_texture_source"],
              "oot3d_cmb_material_lighting_block_pica_bump_texture_unit");
    EXPECT_TRUE(material["native_material_lighting_complete"]);
    EXPECT_TRUE(material["native_material_lighting_incomplete_reasons"].empty());
}

TEST(Oot3dNativeAssets, TreatsMaterialLightingFlag3AsRawNonBlockingState) {
    auto bytes = MinimalTriangleCmbWithFlag3AndNoBump();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_flag3.cmb");
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);

    EXPECT_EQ(summary["native_material_lighting_complete_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_incomplete_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_bump_mode_active_batch_count"], 0);
    EXPECT_EQ(summary["native_material_pica_bump_mode_backend_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_lighting_flag3_available_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_flag3_active_batch_count"], 1);
    EXPECT_EQ(summary["native_material_lighting_flag3_backend_pending_batch_count"], 0);
    ASSERT_TRUE(summary["native_materials"].is_array());
    ASSERT_FALSE(summary["native_materials"].empty());
    EXPECT_TRUE(summary["native_materials"][0]["native_material_lighting_flag3_active"]);
    EXPECT_FALSE(summary["native_materials"][0]["native_material_lighting_flag3_backend_pending"]);
    EXPECT_EQ(
        summary["native_materials"][0]["native_material_lighting_flag3_source"],
        "oot3d_cmb_material_lighting_block_flag3_copied_no_material_lighting_consumer");
    EXPECT_TRUE(summary["native_materials"][0]["native_material_lighting_complete"]);
    EXPECT_TRUE(
        summary["native_materials"][0]["native_material_lighting_incomplete_reasons"].empty());
}

TEST(Oot3dNativeAssets, ExposesMaterialLutInputCoverageInFast3dStatsJson) {
    ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStats stats;
    stats.NativePicaMaterialLutInputPacketAvailableBatchCount = 4;
    stats.NativePicaMaterialLutInputPacketCompleteBatchCount = 3;
    stats.NativePicaMaterialLutInputFragmentLightingBatchCount = 2;
    stats.NativePicaMaterialLutInputEvaluationPendingBatchCount = 2;
    stats.NativePicaMaterialLutInputEvaluationAppliedBatchCount = 0;
    stats.NativePicaAlphaTestDecodedBatchCount = 3;
    stats.NativePicaAlphaTestAppliedBatchCount = 3;
    stats.NativePicaAlphaTestBackendUnsupportedBatchCount = 0;

    const auto json = ThreeDsRecomp::Oot3d::Oot3dNativeFast3dRenderStatsToJson(stats);
    EXPECT_TRUE(json["backend_draw_model_counts"].is_object());
    EXPECT_TRUE(json["missing_texture_batch_model_counts"].is_object());
    EXPECT_EQ(json["native_pica_material_lut_input_packet_available_batch_count"], 4);
    EXPECT_EQ(json["native_pica_material_lut_input_packet_complete_batch_count"], 3);
    EXPECT_EQ(json["native_pica_material_lut_input_fragment_lighting_batch_count"], 2);
    EXPECT_EQ(json["native_pica_material_lut_input_evaluation_pending_batch_count"], 2);
    EXPECT_EQ(json["native_pica_material_lut_input_evaluation_applied_batch_count"], 0);
    EXPECT_EQ(json["native_pica_alpha_test_decoded_batch_count"], 3);
    EXPECT_EQ(json["native_pica_alpha_test_applied_batch_count"], 3);
    EXPECT_EQ(json["native_pica_alpha_test_backend_unsupported_batch_count"], 0);
}

TEST(Oot3dNativeAssets, PreservesNativeCmbConstantVertexColor) {
    auto bytes = MinimalTriangleCmbWithConstantVertexColor();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_constant_color.cmb");

    ASSERT_EQ(model.Shapes.size(), 1u);
    ASSERT_EQ(model.Shapes[0].Colors.size(), 3u);
    EXPECT_EQ(model.Shapes[0].Colors[0].R, 64u);
    EXPECT_EQ(model.Shapes[0].Colors[0].G, 128u);
    EXPECT_EQ(model.Shapes[0].Colors[0].B, 191u);
    EXPECT_EQ(model.Shapes[0].Colors[0].A, 255u);
    EXPECT_TRUE(model.Shapes[0].HasConstantAttribute(ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor));

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    ASSERT_EQ(renderModel.Batches[0].Vertices.size(), 3u);
    EXPECT_TRUE(renderModel.Batches[0].Vertices[0].NativeColorAvailable);
    EXPECT_EQ(renderModel.Batches[0].Vertices[0].Color.R, 64u);
    EXPECT_EQ(renderModel.Batches[0].Vertices[0].Color.G, 128u);
    EXPECT_EQ(renderModel.Batches[0].Vertices[0].Color.B, 191u);

    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_color_available_batch_count"], 1);
    EXPECT_EQ(summary["native_color_batch_count"], 1);
    EXPECT_EQ(summary["native_color_vertex_count"], 3);
    ASSERT_TRUE(summary["native_batches"].is_array());
    ASSERT_FALSE(summary["native_batches"].empty());
    EXPECT_EQ(summary["native_batches"][0]["native_color_available"], true);
    EXPECT_EQ(summary["native_batches"][0]["native_cmb_constant_color"], true);
}

TEST(Oot3dNativeAssets, PreservesNativeCmbConstantVertexNormal) {
    auto bytes = MinimalTriangleCmbWithConstantVertexNormal();
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(bytes, "triangle_constant_normal.cmb");

    ASSERT_EQ(model.Shapes.size(), 1u);
    ASSERT_EQ(model.Shapes[0].Normals.size(), 3u);
    EXPECT_FLOAT_EQ(model.Shapes[0].Normals[0].X, 0.0f);
    EXPECT_FLOAT_EQ(model.Shapes[0].Normals[0].Y, 1.0f);
    EXPECT_FLOAT_EQ(model.Shapes[0].Normals[0].Z, 0.0f);
    EXPECT_TRUE(model.Shapes[0].HasConstantAttribute(ThreeDsRecomp::Oot3d::kOot3dCmbAttributeNormal));

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    ASSERT_EQ(renderModel.Batches[0].Vertices.size(), 3u);
    EXPECT_TRUE(renderModel.Batches[0].Vertices[0].NativeNormalAvailable);
    EXPECT_FLOAT_EQ(renderModel.Batches[0].Vertices[0].Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(renderModel.Batches[0].Vertices[0].Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(renderModel.Batches[0].Vertices[0].Normal.Z, 0.0f);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    ASSERT_TRUE(summary["native_batches"].is_array());
    ASSERT_FALSE(summary["native_batches"].empty());
    EXPECT_EQ(summary["native_batches"][0]["native_cmb_constant_normal"], true);
}

TEST(Oot3dNativeAssets, AppliesActorMaterialLightingNormalMapDuringPicaLighting) {
    auto model = SyntheticActorMaterialLightingNormalMapModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope();
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_TRUE(material.NativePicaLightingApplied);
    EXPECT_TRUE(material.NativePicaDirectionalLightingApplied);
    EXPECT_TRUE(material.NativePicaBumpNormalMapBackendSupported);
    EXPECT_TRUE(material.NativePicaBumpNormalMapApplied);
    EXPECT_FALSE(material.NativePicaBumpModeBackendPending);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderScene.Room);
    ASSERT_TRUE(summary["native_batches"].is_array());
    ASSERT_FALSE(summary["native_batches"].empty());
    EXPECT_TRUE(summary["native_batches"][0]["native_pica_bump_normal_map_backend_supported"]);
    EXPECT_TRUE(summary["native_batches"][0]["native_pica_bump_normal_map_applied"]);
}

TEST(Oot3dNativeAssets, SupportsTextureEnvReplacePreviousNoOpStage) {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);

    EXPECT_EQ(summary["native_material_combiner_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_texture_env_shader_pending_batch_count"], 0);
    EXPECT_EQ(summary["native_material_texture_env_program_color_shader_supported_batch_count"], 1);
    EXPECT_EQ(summary["native_material_texture_env_program_color_shader_applied_batch_count"], 0);
    EXPECT_EQ(summary["native_material_texture_env_program_requires_multi_stage_batch_count"], 1);
    EXPECT_EQ(summary["native_material_texture_env_program_uses_previous_batch_count"], 1);

    ASSERT_TRUE(summary["native_materials"].is_array());
    ASSERT_FALSE(summary["native_materials"].empty());
    const auto& material = summary["native_materials"][0];
    EXPECT_FALSE(material["native_material_combiner_requires_decoder"]);
    const auto& program = material["texture_env_program"];
    EXPECT_EQ(program["color_shader_path"], "texenv_program_texture0_vertex_color");
    EXPECT_TRUE(program["color_shader_path_supported"]);
    EXPECT_TRUE(program["uses_previous"]);
    EXPECT_FALSE(program["uses_fragment_lighting_color"]);
    EXPECT_TRUE(program["primary_color_multiplier_resolved"]);
    EXPECT_EQ(program["primary_color_multiplier_stage_count"], 1);
    EXPECT_DOUBLE_EQ(program["primary_color_multiplier"]["x"], 2.0);
    EXPECT_DOUBLE_EQ(program["primary_color_multiplier"]["y"], 2.0);
    EXPECT_DOUBLE_EQ(program["primary_color_multiplier"]["z"], 2.0);
}

TEST(Oot3dNativeAssets, ClassifiesUnlitNativeTextureEnvVertexColorRoute) {
    auto model = SyntheticUnlitTextureEnvVertexColorModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope();
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_FALSE(material.FragmentLightingEnabled);
    EXPECT_FALSE(material.VertexLightingEnabled);
    EXPECT_FALSE(material.HemisphereLightingEnabled);
    EXPECT_EQ(material.NativePicaLightingApplication, "oot3d_cmb_no_fragment_lighting");
    EXPECT_TRUE(material.NativePicaUnlitTextureEnvRouteDecoded);
    EXPECT_TRUE(material.NativePicaUnlitTextureEnvRouteRequiresNativeColor);
    EXPECT_TRUE(material.NativePicaUnlitTextureEnvRouteNativeColorAvailable);
    EXPECT_TRUE(material.NativePicaUnlitTextureEnvRouteApplied);
    EXPECT_EQ(material.NativePicaUnlitTextureEnvRouteSource,
              "oot3d_cmb_lighting_flags_disabled_texture_env_primary_color_texture0");
    EXPECT_TRUE(material.TextureEnvProgram.ColorShaderPathApplied);
    EXPECT_TRUE(material.VertexColorModulatesTexture);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderScene.Room);
    EXPECT_EQ(summary["native_material_fragment_lighting_enabled_batch_count"], 0);
    EXPECT_EQ(summary["native_material_cmb_lighting_flag_enabled_batch_count"], 0);
    EXPECT_EQ(summary["native_pica_unlit_texture_env_route_decoded_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_unlit_texture_env_route_applied_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_unlit_texture_env_route_missing_native_color_batch_count"], 0);
    ASSERT_TRUE(summary["native_batches"].is_array());
    const auto& batch = summary["native_batches"][0];
    EXPECT_TRUE(batch["native_pica_unlit_texture_env_route_decoded"]);
    EXPECT_TRUE(batch["native_pica_unlit_texture_env_route_applied"]);
    EXPECT_TRUE(batch["native_pica_unlit_texture_env_route_native_color_available"]);
    EXPECT_EQ(batch["native_pica_unlit_texture_env_route_source"],
              "oot3d_cmb_lighting_flags_disabled_texture_env_primary_color_texture0");
}

TEST(Oot3dNativeAssets, AppliesNativeVertexHemisphereLightingToStaticBakedCmbWithNativeNormals) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope();
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    EXPECT_TRUE(renderScene.PicaLighting.Available);
    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_TRUE(material.NativePicaLightingApplied);
    EXPECT_TRUE(material.NativePicaVertexLightingApplied);
    EXPECT_TRUE(material.NativePicaHemisphereLightingApplied);
    EXPECT_EQ(material.NativePicaLightingApplication,
              "oot3d_cmb_static_baked_vertex_or_hemisphere_lighting");
    EXPECT_EQ(material.NativePicaVertexHemisphereLightingSource,
              "oot3d_cmb_material_vertex_or_hemisphere_lighting_flags");
    EXPECT_TRUE(material.NativePicaVertexHemisphereVectorResolved);
    EXPECT_FALSE(material.NativePicaVertexHemisphereVectorPending);

    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderScene.Room);
    EXPECT_EQ(summary["transform_baked_into_vertices"], true);
    EXPECT_EQ(summary["native_normal_available_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_lighting_applied_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_hemisphere_lighting_applied_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_lighting_deferred_batch_count"], 0);
    EXPECT_EQ(summary["native_pica_hemisphere_lighting_deferred_batch_count"], 0);
}

TEST(Oot3dNativeAssets, ReappliesRuntimePicaLightingFromNativeVertexInputColors) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    model.Materials[0].DiffuseColor = { 0, 0, 0, 255 };
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope();
    auto firstLighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene);
    firstLighting.AmbientColor = { 32, 64, 128, 255 };
    auto secondLighting = firstLighting;
    secondLighting.AmbientColor = { 192, 128, 64, 255 };

    auto reusedModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    const auto nativeInputColor = reusedModel.Batches[0].Vertices[0].Color;
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(firstLighting, reusedModel);
    const auto firstOutputColor = reusedModel.Batches[0].Vertices[0].Color;
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(secondLighting, reusedModel);

    auto freshModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(secondLighting, freshModel);

    ASSERT_FALSE(reusedModel.Batches.empty());
    ASSERT_FALSE(reusedModel.Batches[0].Vertices.empty());
    EXPECT_TRUE(reusedModel.Batches[0].Vertices[0].NativePicaLightingInputColorAvailable);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].NativePicaLightingInputColor.R, nativeInputColor.R);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].NativePicaLightingInputColor.G, nativeInputColor.G);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].NativePicaLightingInputColor.B, nativeInputColor.B);
    EXPECT_NE(reusedModel.Batches[0].Vertices[0].Color.R, firstOutputColor.R);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].Color.R, freshModel.Batches[0].Vertices[0].Color.R);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].Color.G, freshModel.Batches[0].Vertices[0].Color.G);
    EXPECT_EQ(reusedModel.Batches[0].Vertices[0].Color.B, freshModel.Batches[0].Vertices[0].Color.B);
    EXPECT_EQ(reusedModel.Batches[0].Material.NativePicaLightingDiagnostics.AmbientColor.R, 192);
    EXPECT_EQ(reusedModel.Batches[0].Material.NativePicaLightingDiagnostics.AmbientColor.G, 192);
    EXPECT_EQ(reusedModel.Batches[0].Material.NativePicaLightingDiagnostics.AmbientColor.B, 192);
}

TEST(Oot3dNativeAssets, KeepsPicaDirectionalUniformVectorsInModelSpaceWhenDeclared) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope();
    auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene);
    lighting.Light0Vector = { 0.0f, 1.0f, 0.0f };
    lighting.Light1Vector = { 0.0f, -1.0f, 0.0f };
    lighting.DirectionalVectorsUseModelSpace = true;
    lighting.DirectionalVectorSpaceSource = "synthetic_pica_vsh_model_space_uniform";

    auto modelSpaceRenderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    modelSpaceRenderModel.ModelToWorld.M[0][0] = 0.0f;
    modelSpaceRenderModel.ModelToWorld.M[0][1] = -1.0f;
    modelSpaceRenderModel.ModelToWorld.M[1][0] = 1.0f;
    modelSpaceRenderModel.ModelToWorld.M[1][1] = 0.0f;
    auto worldSpaceRenderModel = modelSpaceRenderModel;

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, modelSpaceRenderModel);
    lighting.DirectionalVectorsUseModelSpace = false;
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, worldSpaceRenderModel);

    ASSERT_FALSE(modelSpaceRenderModel.Batches.empty());
    ASSERT_FALSE(modelSpaceRenderModel.Batches[0].Vertices.empty());
    EXPECT_GT(modelSpaceRenderModel.Batches[0].Vertices[0].Color.R,
              worldSpaceRenderModel.Batches[0].Vertices[0].Color.R);

}

TEST(Oot3dNativeAssets, UsesRuntimeEnvironmentColorsForStaticBakedCmb) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_TRUE(material.NativePicaVertexHemisphereVectorResolved);
    EXPECT_FALSE(material.NativePicaVertexHemisphereVectorPending);
    EXPECT_TRUE(material.NativePicaDirectionalLightingApplied);
    EXPECT_EQ(material.NativePicaVertexHemisphereVectorSource,
              "active_0045dd50_runtime_environment_signed_vec3");
    EXPECT_FALSE(material.NativePicaVertexHemisphereActorVsColorPacketApplied);
    EXPECT_EQ(material.NativePicaVertexHemisphereColorSource,
              "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends");
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.R, 181);
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.G, 181);
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.B, 160);

    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderScene.Room);
    EXPECT_EQ(summary["native_pica_directional_lighting_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_hemisphere_vector_resolved_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_hemisphere_vector_pending_batch_count"], 0);
    ASSERT_TRUE(summary["native_batches"].is_array());
    const auto& staticBatch = summary["native_batches"][0];
    EXPECT_FALSE(staticBatch["native_pica_vertex_hemisphere_actor_vs_color_packet_applied"]);
    EXPECT_EQ(staticBatch["native_pica_vertex_hemisphere_color_source"],
              "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends");
}

TEST(Oot3dNativeAssets, KeepsZeroMaterialDiffuseForStaticBakedVertexHemisphereLighting) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    model.Materials[0].AmbientColor = { 255, 255, 255, 255 };
    model.Materials[0].DiffuseColor = { 0, 0, 0, 255 };
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_TRUE(material.NativePicaEffectiveMaterialDiffuseResolved);
    EXPECT_FALSE(material.NativePicaEffectiveMaterialDiffuseUsesAmbient);
    EXPECT_EQ(material.NativePicaEffectiveMaterialDiffuseSource, "oot3d_cmb_material_diffuse_rgb");
    EXPECT_EQ(material.NativePicaEffectiveMaterialDiffuseColor.R, 0);
    EXPECT_EQ(material.NativePicaEffectiveMaterialDiffuseColor.G, 0);
    EXPECT_EQ(material.NativePicaEffectiveMaterialDiffuseColor.B, 0);
}

TEST(Oot3dNativeAssets, AppliesAmbientOnlyStaticVertexLightingWithoutNativeNormals) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    model.Materials[0].AmbientColor = { 255, 255, 255, 255 };
    model.Materials[0].DiffuseColor = { 0, 0, 0, 255 };
    model.Shapes[0].Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition;
    model.Shapes[0].Normals.clear();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& batch = renderScene.Room.Batches[0];
    const auto& material = batch.Material;
    EXPECT_TRUE(material.NativePicaLightingApplied);
    EXPECT_TRUE(material.NativePicaVertexLightingApplied);
    EXPECT_TRUE(material.NativePicaHemisphereLightingApplied);
    EXPECT_FALSE(material.NativePicaDirectionalLightingApplied);
    EXPECT_TRUE(material.NativePicaLightingDiagnostics.Available);
    EXPECT_EQ(material.NativePicaLightingDiagnostics.NativeNormalVertexCount, 0u);
    EXPECT_EQ(material.NativePicaLightingDiagnostics.ModulationColorStats.Average.R, 181);
    EXPECT_EQ(material.NativePicaLightingDiagnostics.ModulationColorStats.Average.G, 181);
    EXPECT_EQ(material.NativePicaLightingDiagnostics.ModulationColorStats.Average.B, 160);
}

TEST(Oot3dNativeAssets, AppliesRecognizedCmbVShaderStaticAmbientToEveryActiveSlot) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    model.Materials[0].AmbientColor = { 255, 255, 255, 255 };
    model.Materials[0].DiffuseColor = { 0, 0, 0, 255 };
    model.Shapes[0].Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition | ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor;
    model.Shapes[0].Normals.clear();
    model.Shapes[0].Colors = {
        { 255, 255, 255, 128 },
        { 255, 255, 255, 128 },
        { 255, 255, 255, 128 },
    };

    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene);
    lighting.AmbientColor = { 104, 104, 61, 255 };
    lighting.DiffuseColor = { 0, 0, 0, 255 };
    lighting.Light1Color = { 0, 0, 0, 255 };
    lighting.DirectionalLightCount = 2;
    lighting.CmbVShaderLightingAccumulatorDecoded = true;
    lighting.CmbVShaderLightingAccumulatorSlotCount = 3;
    lighting.CmbVShaderLightingAccumulatorSource = "synthetic_recognized_cmb_vshader";

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, renderModel);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& batch = renderModel.Batches[0];
    EXPECT_TRUE(batch.Material.NativePicaCmbVShaderLightingAccumulatorApplied);
    EXPECT_EQ(batch.Material.NativePicaCmbVShaderLightingActiveLightCount, 2u);
    EXPECT_EQ(batch.Material.NativePicaCmbVShaderLightingAmbientSlotCount, 2u);
    EXPECT_DOUBLE_EQ(batch.Material.NativePicaCmbVShaderLightingAlphaScale, 2.0);
    EXPECT_EQ(batch.Material.NativePicaCmbVShaderLightingAccumulatorSource, "synthetic_recognized_cmb_vshader");
    ASSERT_FALSE(batch.Vertices.empty());
    for (const auto& vertex : batch.Vertices) {
        EXPECT_EQ(vertex.Color.R, 208);
        EXPECT_EQ(vertex.Color.G, 208);
        EXPECT_EQ(vertex.Color.B, 122);
        EXPECT_EQ(vertex.Color.A, 255);
    }

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    ASSERT_TRUE(summary["native_batches"].is_array());
    const auto& nativeBatch = summary["native_batches"][0];
    EXPECT_TRUE(nativeBatch["native_pica_cmb_vshader_lighting_accumulator_applied"]);
    EXPECT_EQ(nativeBatch["native_pica_cmb_vshader_lighting_active_light_count"], 2);
    EXPECT_EQ(nativeBatch["native_pica_cmb_vshader_lighting_ambient_slot_count"], 2);
    EXPECT_DOUBLE_EQ(nativeBatch["native_pica_cmb_vshader_lighting_alpha_scale"], 2.0);
}

TEST(Oot3dNativeAssets, AppliesRecognizedCmbVShaderActorAmbientOnlyToFirstActiveSlot) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    model.Materials[0].AmbientColor = { 255, 255, 255, 255 };
    model.Materials[0].DiffuseColor = { 0, 0, 0, 255 };
    model.Shapes[0].Flags = ThreeDsRecomp::Oot3d::kOot3dCmbAttributePosition | ThreeDsRecomp::Oot3d::kOot3dCmbAttributeColor;
    model.Shapes[0].Normals.clear();
    model.Shapes[0].Colors = {
        { 255, 255, 255, 128 },
        { 255, 255, 255, 128 },
        { 255, 255, 255, 128 },
    };

    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene);
    lighting.AmbientColor = { 104, 104, 61, 255 };
    lighting.DiffuseColor = { 0, 0, 0, 255 };
    lighting.Light1Color = { 0, 0, 0, 255 };
    lighting.DirectionalLightCount = 2;
    lighting.CmbVShaderLightingAccumulatorDecoded = true;
    lighting.CmbVShaderLightingAccumulatorSlotCount = 3;
    lighting.CmbVShaderLightingAccumulatorSource = "synthetic_recognized_cmb_vshader";
    lighting.ActorVsLightPacket.Available = true;
    lighting.ActorVsLightPacket.CompactPayloadSourceResolved = true;
    lighting.ActorVsLightPacket.VectorOriginResolved = true;
    lighting.ActorVsLightPacket.CompactPayloadSlot0Direction = { 0.0f, 1.0f, 0.0f };
    lighting.ActorVsLightPacket.CompactPayloadSlot1Direction = { 0.0f, -1.0f, 0.0f };
    lighting.VertexHemisphereLightColorMode =
        "oot3d_runtime_environment_light_colors_with_actor_vs_compact_payload_vectors";

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model, {
                                                                           {},
                                                                           1.0,
                                                                           false,
                                                                       });
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, renderModel);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& batch = renderModel.Batches[0];
    EXPECT_TRUE(batch.Material.NativePicaCmbVShaderLightingAccumulatorApplied);
    EXPECT_EQ(batch.Material.NativePicaCmbVShaderLightingActiveLightCount, 2u);
    EXPECT_EQ(batch.Material.NativePicaCmbVShaderLightingAmbientSlotCount, 1u);
    ASSERT_FALSE(batch.Vertices.empty());
    for (const auto& vertex : batch.Vertices) {
        EXPECT_EQ(vertex.Color.R, 104);
        EXPECT_EQ(vertex.Color.G, 104);
        EXPECT_EQ(vertex.Color.B, 61);
        EXPECT_EQ(vertex.Color.A, 255);
    }
}

TEST(Oot3dNativeAssets, PreservesNeutralPrimaryColorWhenVertexLightingIsDeferredWithoutNativeNormals) {
    auto model = SyntheticTextureEnvReplacePreviousModel();
    auto& material = model.Materials[0];
    material.FragmentLightingEnabled = false;
    material.VertexLightingEnabled = true;
    material.HemisphereLightingEnabled = true;
    material.MaterialColorsDecoded = true;
    material.AmbientColor = { 185, 191, 128, 255 };
    material.DiffuseColor = { 255, 255, 255, 255 };
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& batch = renderScene.Room.Batches[0];
    EXPECT_EQ(batch.Material.NativePicaLightingApplication,
              "oot3d_cmb_vertex_or_hemisphere_lighting_deferred");
    EXPECT_TRUE(batch.Material.NativePicaVertexLightingDeferred);
    EXPECT_TRUE(batch.Material.NativePicaHemisphereLightingDeferred);
    ASSERT_FALSE(batch.Vertices.empty());
    for (const auto& vertex : batch.Vertices) {
        EXPECT_EQ(vertex.Color.R, 255);
        EXPECT_EQ(vertex.Color.G, 255);
        EXPECT_EQ(vertex.Color.B, 255);
    }
}

TEST(Oot3dNativeAssets, UsesRuntimeEnvironmentColorsWithActorVsVectorPacket) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    renderScene.Room = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(
        model,
        {
            {},
            1.0,
            false,
        });

    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLighting(scene, renderScene);

    ASSERT_EQ(renderScene.Room.Batches.size(), 1u);
    const auto& material = renderScene.Room.Batches[0].Material;
    EXPECT_TRUE(material.NativePicaVertexHemisphereVectorResolved);
    EXPECT_FALSE(material.NativePicaVertexHemisphereVectorPending);
    EXPECT_TRUE(material.NativePicaDirectionalLightingApplied);
    EXPECT_EQ(material.NativePicaVertexHemisphereVectorSource,
              "selected_0045dd50_runtime_actor_vs_compact_payload_dir_s8");
    EXPECT_FALSE(material.NativePicaVertexHemisphereActorVsColorPacketApplied);
    EXPECT_EQ(material.NativePicaVertexHemisphereColorSource,
              "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends");
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.R, 181);
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.G, 181);
    EXPECT_EQ(material.NativePicaVertexHemisphereAmbientColor.B, 160);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse0Color.R, 0);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse0Color.G, 128);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse0Color.B, 59);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse1Color.R, 22);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse1Color.G, 69);
    EXPECT_EQ(material.NativePicaVertexHemisphereDiffuse1Color.B, 248);
    EXPECT_TRUE(renderScene.PicaLighting.ActorVsLightPacket.VectorOriginResolved);
    EXPECT_TRUE(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSourceResolved);
    EXPECT_FALSE(renderScene.PicaLighting.ActorVsLightPacket.FinalPacketWriterResolved);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.RecordSelectionIndexDelta, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.RecordIndex, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadActiveAngle, 32768);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.AmbientColor.R, 181);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.AmbientColor.G, 181);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.AmbientColor.B, 160);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse0Color.R, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse0Color.G, 128);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse0Color.B, 59);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse1Color.R, 22);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse1Color.G, 69);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.Diffuse1Color.B, 248);
    EXPECT_TRUE(renderScene.PicaLighting.ActorVsLightPacket.PicaFogColorAvailable);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.PicaFogColorOffset, 0x19u);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.PicaFogColor.R, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.PicaFogColor.G, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.PicaFogColor.B, 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0DirectionS8[0], 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0DirectionS8[1], 120);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0DirectionS8[2], 20);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1DirectionS8[0], 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1DirectionS8[1], -120);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1DirectionS8[2], -20);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[0], 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[1], 120);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[2], 20);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[3], 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[4], 128);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Bytes[5], 59);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[0], 0);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[1], 0x88);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[2], 0xEC);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[3], 22);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[4], 69);
    EXPECT_EQ(renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Bytes[5], 248);
    EXPECT_TRUE(renderScene.PicaLighting.ActorVsLightPacket.RuntimeLightPacketPackNegatesPreparedVector);
    EXPECT_TRUE(material.NativePicaLightingDiagnostics.Available);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light0Vector.X,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Direction.X, 1e-5f);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light0Vector.Y,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Direction.Y, 1e-5f);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light0Vector.Z,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot0Direction.Z, 1e-5f);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light1Vector.X,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Direction.X, 1e-5f);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light1Vector.Y,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Direction.Y, 1e-5f);
    EXPECT_NEAR(material.NativePicaLightingDiagnostics.Light1Vector.Z,
                renderScene.PicaLighting.ActorVsLightPacket.CompactPayloadSlot1Direction.Z, 1e-5f);

    auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderScene.Room);
    EXPECT_EQ(summary["transform_baked_into_vertices"], false);
    EXPECT_EQ(summary["native_pica_directional_lighting_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_hemisphere_vector_resolved_batch_count"], 1);
    EXPECT_EQ(summary["native_pica_vertex_hemisphere_vector_pending_batch_count"], 0);
    ASSERT_TRUE(summary["native_batches"].is_array());
    const auto& actorBatch = summary["native_batches"][0];
    EXPECT_FALSE(actorBatch["native_pica_vertex_hemisphere_actor_vs_color_packet_applied"]);
    EXPECT_EQ(actorBatch["native_pica_vertex_hemisphere_color_source"],
              "active_0045dd50_preaddend_runtime_environment_rgb_pending_state_0x6c_0x72_addends");
}

TEST(Oot3dNativeAssets, UsesDrawLocalLightPacketColorsWithExplicitVectors) {
    auto model = SyntheticStaticBakedVertexHemisphereLightingModel();
    auto scene = SyntheticPicaLightingSceneForStaticBakedScope(true);
    auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene);
    lighting.VertexHemisphereLightColorMode =
        "oot3d_draw_local_light_packet_colors_with_explicit_vectors";
    lighting.Light0Vector = { 0.0f, 1.0f, 0.0f };
    lighting.Light1Vector = { 0.0f, -1.0f, 0.0f };
    lighting.ActorVsLightPacket.Available = true;
    lighting.ActorVsLightPacket.ColorPacketAvailable = true;
    lighting.ActorVsLightPacket.VectorOriginResolved = true;
    lighting.ActorVsLightPacket.CompactPayloadSourceResolved = false;
    lighting.ActorVsLightPacket.AmbientColor = { 32, 32, 32, 255 };
    lighting.ActorVsLightPacket.Diffuse0Color = { 128, 128, 128, 255 };
    lighting.ActorVsLightPacket.Diffuse1Color = { 0, 0, 0, 255 };
    lighting.ActorVsLightPacket.ColorSource = "synthetic_draw_local_light_packet";
    lighting.VertexHemisphereRuntimeColorSource =
        lighting.ActorVsLightPacket.ColorSource;

    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(
        model,
        {
            {},
            1.0,
            false,
        });
    ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(lighting, renderModel);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& batch = renderModel.Batches[0];
    EXPECT_TRUE(batch.Material.NativePicaVertexHemisphereActorVsColorPacketApplied);
    EXPECT_EQ(batch.Material.NativePicaVertexHemisphereColorSource,
              "synthetic_draw_local_light_packet");
    ASSERT_FALSE(batch.Vertices.empty());
    for (const auto& vertex : batch.Vertices) {
        EXPECT_EQ(vertex.Color.R, 160);
        EXPECT_EQ(vertex.Color.G, 160);
        EXPECT_EQ(vertex.Color.B, 160);
    }
}

TEST(Oot3dNativeAssets, UsesTransitionedRuntimeEnvironmentFogForPicaAndEnvironmentBackground) {
    auto scene = SyntheticRuntimeTransitionFogScene();
    ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput runtime;
    runtime.TimeResolved = true;
    runtime.DayTime = 50;
    runtime.SkyboxTime = 50;
    runtime.LightModeResolved = true;
    runtime.LightModeCurrent = 0;
    runtime.LightModeTarget = 0;

    const auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene, &runtime);

    ASSERT_TRUE(lighting.ResolvedRuntimeLightSetting.Available);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.UsedForRender);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.TransitionTableBranchApplied);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.FogColor.R, 60);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.FogColor.G, 90);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.FogColor.B, 120);
    EXPECT_FALSE(lighting.ResolvedRuntimeLightSetting.RuntimeFinalFogColorUsedForRender);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.RuntimeFogDistanceContractResolved);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.RuntimeCameraFarUsedForRender);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.RuntimeFogDistancesUsedForRender);
    EXPECT_DOUBLE_EQ(lighting.ResolvedRuntimeLightSetting.CameraFar, 32000.0);
    EXPECT_DOUBLE_EQ(lighting.ResolvedRuntimeLightSetting.FogFar, 44000.0);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.FogNear, 120);
    ASSERT_TRUE(lighting.ActorVsLightPacket.PicaFogColorAvailable);
    EXPECT_TRUE(lighting.ActorVsLightPacket.RuntimeTransitionColorBlendApplied);
    EXPECT_EQ(lighting.ActorVsLightPacket.RuntimeTransitionColorFromRecordIndex, 0);
    EXPECT_EQ(lighting.ActorVsLightPacket.RuntimeTransitionColorToRecordIndex, 1);
    EXPECT_EQ(lighting.ActorVsLightPacket.AmbientColor.R, 60);
    EXPECT_EQ(lighting.ActorVsLightPacket.AmbientColor.G, 70);
    EXPECT_EQ(lighting.ActorVsLightPacket.AmbientColor.B, 80);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse0Color.R, 90);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse0Color.G, 100);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse0Color.B, 110);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse1Color.R, 120);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse1Color.G, 130);
    EXPECT_EQ(lighting.ActorVsLightPacket.Diffuse1Color.B, 140);
    EXPECT_EQ(lighting.ActorVsLightPacket.PicaFogColor.R, 60);
    EXPECT_EQ(lighting.ActorVsLightPacket.PicaFogColor.G, 90);
    EXPECT_EQ(lighting.ActorVsLightPacket.PicaFogColor.B, 120);

    const auto fog = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaFogState(scene, lighting);
    EXPECT_TRUE(fog.FogEnabled);
    EXPECT_EQ(fog.Color.R, 60);
    EXPECT_EQ(fog.Color.G, 90);
    EXPECT_EQ(fog.Color.B, 120);
    EXPECT_NE(fog.ColorSource.find("0045dd50_preaddend_environment_fog_rgb"),
              std::string::npos);

    const auto background =
        ThreeDsRecomp::Oot3d::BuildOot3dNativeEnvironmentBackgroundState(scene, lighting, &runtime);
    ASSERT_TRUE(background.Available);
    EXPECT_TRUE(background.UsedForRender);
    EXPECT_EQ(background.ClearColor.R, 60);
    EXPECT_EQ(background.ClearColor.G, 90);
    EXPECT_EQ(background.ClearColor.B, 120);
}

TEST(Oot3dNativeAssets, AppliesNativeCmbResourceVisibilityToCachedRenderModel) {
    auto model = ThreeDsRecomp::Oot3d::ParseCmbModelBytes(MinimalTriangleCmb(), "cached_visibility.cmb");
    auto secondMesh = model.Meshes[0];
    secondMesh.Index = 1;
    secondMesh.VisibilityId = 4;
    model.Meshes[0].VisibilityId = 3;
    model.Meshes.push_back(secondMesh);
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);
    ASSERT_EQ(renderModel.Batches.size(), 2u);

    std::vector<uint8_t> resourceVisibility(5, 0);
    resourceVisibility[4] = 1;
    EXPECT_EQ(ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelResourceVisibility(
                  renderModel, resourceVisibility),
              1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    EXPECT_EQ(renderModel.Batches[0].VisibilityId, 4u);
    EXPECT_TRUE(renderModel.NativeCmbResourceVisibilityApplied);
    EXPECT_EQ(renderModel.NativeCmbResourceVisibility, resourceVisibility);
}

TEST(Oot3dNativeAssets, ResolvesRuntimeEnvironmentWithoutPlayerFloorLightSetting) {
    auto scene = SyntheticRuntimeTransitionFogScene();
    scene.PlayerStart.FloorPolygonIndex = -1;
    scene.PlayerStart.FloorSurfaceType = -1;
    scene.PlayerStart.FloorLightSettingRawIndex = -1;
    scene.PlayerStart.FloorLightSettingIndex = -1;

    ThreeDsRecomp::Oot3d::Oot3dNativeRuntimeEnvironmentInput runtime;
    runtime.TimeResolved = true;
    runtime.DayTime = 50;
    runtime.SkyboxTime = 50;
    runtime.LightModeResolved = true;
    runtime.LightModeCurrent = 0;
    runtime.LightModeTarget = 0;

    const auto lighting = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaLightingRenderState(scene, &runtime);

    ASSERT_TRUE(lighting.Available);
    EXPECT_EQ(lighting.RecordSelector,
              "active_setup_runtime_environment_record_when_player_floor_unavailable");
    EXPECT_EQ(lighting.RecordSelectorSource,
              "code_bin_0045dd50_runtime_mode_angle_or_direct_light_setting_scene_state");
    ASSERT_TRUE(lighting.ResolvedRuntimeLightSetting.Available);
    EXPECT_TRUE(lighting.ResolvedRuntimeLightSetting.TransitionTableBranchApplied);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.CurrentFromLightSettingIndex, 0);
    EXPECT_EQ(lighting.ResolvedRuntimeLightSetting.CurrentToLightSettingIndex, 1);
    EXPECT_EQ(lighting.AmbientColor.R, 60);
    EXPECT_EQ(lighting.DiffuseColor.R, 90);
    EXPECT_EQ(lighting.Light1Color.R, 120);
}

TEST(Oot3dNativeAssets, ParsesCtxbTextureAndNativeDescriptorSlot) {
    auto bytes = MinimalLuminanceCtxbTexture();
    auto texture = ThreeDsRecomp::Oot3d::ParseCtxbTextureBytes(bytes, "actor/zelda_keep.zar!kankyo/tex/test_l8.ctxb");

    EXPECT_EQ(texture.Name, "test_l8");
    EXPECT_EQ(texture.DeclaredFileSize, bytes.size());
    EXPECT_EQ(texture.HeaderVersion, 1u);
    EXPECT_EQ(texture.TexChunkOffset, 0x18u);
    EXPECT_EQ(texture.PayloadOffset, 0x48u);
    EXPECT_EQ(texture.PayloadSize, 64u);
    EXPECT_EQ(texture.Flags, 0x00001234u);
    EXPECT_EQ(texture.Width, 8u);
    EXPECT_EQ(texture.Height, 8u);
    EXPECT_EQ(texture.TextureFormat, 0x6757u);
    EXPECT_EQ(texture.DataType, 0x1401u);
    EXPECT_EQ(texture.SamplerWord, 0x14016757u);
    EXPECT_EQ(texture.Data.size(), 64u);
    EXPECT_TRUE(texture.Rgba8Decoded);
    EXPECT_EQ(texture.Rgba8.size(), 8u * 8u * 4u);
    for (size_t pixel = 0; pixel < texture.Width * texture.Height; ++pixel) {
        EXPECT_EQ(texture.Rgba8[pixel * 4 + 3], 255u);
    }

    auto slot = ThreeDsRecomp::Oot3d::BuildNativeCtxbDescriptorSlot(texture, 2);
    EXPECT_EQ(slot.SlotIndex, 2u);
    EXPECT_EQ(slot.SlotStrideBytes, 0x30u);
    EXPECT_EQ(slot.SlotRecordBaseOffset, 0x13Cu + 2u * 0x30u);
    EXPECT_EQ(slot.SourceTextureHeaderBaseOffset, 0x24u);
    EXPECT_EQ(slot.SourcePayloadPointerFieldOffset, 0x4Cu);
    EXPECT_EQ(slot.TextureHeaderParameter, 0x1234);
    EXPECT_EQ(slot.Width, texture.Width);
    EXPECT_EQ(slot.Height, texture.Height);
    EXPECT_EQ(slot.TextureFormat, texture.TextureFormat);
    EXPECT_EQ(slot.DataType, texture.DataType);
    EXPECT_EQ(slot.PackedFormatDataType, texture.SamplerWord);
    EXPECT_EQ(slot.PayloadSize, texture.PayloadSize);
}

TEST(Oot3dNativeAssets, ParsesCmabScalarMaterialAnimationTrack) {
    auto bytes = MinimalCmabScalarTrack();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "kankyo/BlueOnly.zar!misc/fine_kumo_a.cmab");

    EXPECT_EQ(animation.Source, "kankyo/BlueOnly.zar!misc/fine_kumo_a.cmab");
    EXPECT_EQ(animation.Version, 1u);
    EXPECT_EQ(animation.DeclaredFileSize, bytes.size());
    EXPECT_EQ(animation.StringTableOffset, 0x74u);
    EXPECT_EQ(animation.TextureDataOffset, 0u);
    EXPECT_EQ(animation.FrameCountCandidate, 900u);
    EXPECT_EQ(animation.LoopModeCandidate, 1u);
    EXPECT_EQ(animation.MadsOffset, ThreeDsRecomp::Oot3d::kOot3dCmabHeaderMadsOffset);
    EXPECT_EQ(animation.MadsRecordCount, 1u);
    EXPECT_EQ(animation.MadsStride, 12u);
    ASSERT_EQ(animation.MadsRecordOffsets.size(), 1u);
    EXPECT_EQ(animation.MadsRecordOffsets[0], 0x40u);
    EXPECT_TRUE(animation.StringTableNames.empty());
    EXPECT_TRUE(animation.EmbeddedTextures.empty());
    EXPECT_FALSE(animation.TexturePayloadDecoded);

    ASSERT_EQ(animation.MmadRecords.size(), 1u);
    const auto& record = animation.MmadRecords[0];
    EXPECT_EQ(record.Offset, 0x40u);
    EXPECT_EQ(record.MadsRecordOffset, 0x40u);
    EXPECT_TRUE(record.MadsRecordOffsetMatched);
    EXPECT_EQ(record.Size, 0x34u);
    EXPECT_EQ(record.HeaderWords[0], 1u);
    EXPECT_EQ(record.NativeType, 1u);
    EXPECT_TRUE(record.NativeTypeFactorySupported);
    EXPECT_EQ(record.TargetMaterialIndex, 0u);
    EXPECT_EQ(record.TargetComponentOrStageIndex, 0u);
    EXPECT_TRUE(record.TargetSelectorPresent);
    EXPECT_EQ(record.TargetSelector, 0);
    EXPECT_EQ(record.NativeValueKind, "transform_vec2_gate_1_plus_selector");
    EXPECT_EQ(record.NativeChannelOffsetTableOffset, 0x10u);
    EXPECT_EQ(record.NativeChannelOffsetCount, 2u);
    ASSERT_EQ(record.NativeChannelOffsets.size(), 2u);
    EXPECT_EQ(record.NativeChannelOffsets[0].ComponentIndex, 0u);
    EXPECT_EQ(record.NativeChannelOffsets[0].TableOffset, 0x10u);
    EXPECT_EQ(record.NativeChannelOffsets[0].RelativeOffset, 20);
    EXPECT_TRUE(record.NativeChannelOffsets[0].Present);
    EXPECT_EQ(record.NativeChannelOffsets[1].ComponentIndex, 1u);
    EXPECT_EQ(record.NativeChannelOffsets[1].TableOffset, 0x12u);
    EXPECT_EQ(record.NativeChannelOffsets[1].RelativeOffset, 0);
    EXPECT_FALSE(record.NativeChannelOffsets[1].Present);
    EXPECT_EQ(record.HeaderWords[3], 20u);
    EXPECT_EQ(record.KeyframeCountCandidate, 2u);
    EXPECT_EQ(record.LastFrameCandidate, 900u);
    EXPECT_TRUE(record.ScalarKeyframesDecoded);
    ASSERT_EQ(record.ScalarKeyframes.size(), 2u);
    EXPECT_EQ(record.ScalarKeyframes[0].Frame, 0u);
    EXPECT_FLOAT_EQ(record.ScalarKeyframes[0].Value, 0.0f);
    EXPECT_EQ(record.ScalarKeyframes[1].Frame, 900u);
    EXPECT_FLOAT_EQ(record.ScalarKeyframes[1].Value, -1.0f);
}

TEST(Oot3dNativeAssets, ParsesCmabComponentScalarMaterialAnimationTracks) {
    auto bytes = MinimalCmabComponentScalarTracks();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_blkobj.zar!misc/m_WhontR_0d_model.cmab");

    ASSERT_EQ(animation.MmadRecords.size(), 1u);
    ASSERT_EQ(animation.MadsRecordOffsets.size(), 1u);
    EXPECT_EQ(animation.MadsRecordOffsets[0], 0x40u);
    const auto& record = animation.MmadRecords[0];
    EXPECT_EQ(record.Offset, 0x40u);
    EXPECT_EQ(record.MadsRecordOffset, 0x40u);
    EXPECT_TRUE(record.MadsRecordOffsetMatched);
    EXPECT_EQ(record.Size, 0x54u);
    EXPECT_EQ(record.HeaderWords[0], 1u);
    EXPECT_EQ(record.HeaderWords[1], 10u);
    EXPECT_EQ(record.HeaderWords[2], 0u);
    EXPECT_EQ(record.NativeType, 1u);
    EXPECT_TRUE(record.NativeTypeFactorySupported);
    EXPECT_EQ(record.TargetMaterialIndex, 10u);
    EXPECT_EQ(record.TargetComponentOrStageIndex, 0u);
    EXPECT_TRUE(record.TargetSelectorPresent);
    EXPECT_EQ(record.TargetSelector, 0);
    EXPECT_EQ(record.NativeValueKind, "transform_vec2_gate_1_plus_selector");
    EXPECT_EQ(record.NativeChannelOffsetTableOffset, 0x10u);
    EXPECT_EQ(record.NativeChannelOffsetCount, 2u);
    ASSERT_EQ(record.NativeChannelOffsets.size(), 2u);
    EXPECT_EQ(record.NativeChannelOffsets[0].RelativeOffset, 0x14);
    EXPECT_TRUE(record.NativeChannelOffsets[0].Present);
    EXPECT_EQ(record.NativeChannelOffsets[1].RelativeOffset, 0x34);
    EXPECT_TRUE(record.NativeChannelOffsets[1].Present);
    ASSERT_EQ(record.NativeSourceCurves.size(), 2u);
    EXPECT_EQ(record.NativeSourceCurves[0].ComponentIndex, 0u);
    EXPECT_EQ(record.NativeSourceCurves[0].Offset, 0x14u);
    EXPECT_EQ(record.NativeSourceCurves[0].Size, 0x20u);
    EXPECT_TRUE(record.NativeSourceCurves[0].Decoded);
    EXPECT_EQ(record.NativeSourceCurves[0].Status, "decoded");
    EXPECT_EQ(record.NativeSourceCurves[0].Type, 1u);
    EXPECT_EQ(record.NativeSourceCurves[0].PointStrideBytes, 0x08u);
    EXPECT_FALSE(record.NativeSourceCurves[0].WrapEnabled);
    ASSERT_EQ(record.NativeSourceCurves[0].Points.size(), 2u);
    EXPECT_EQ(record.NativeSourceCurves[0].Points[1].Frame, 900);
    EXPECT_FLOAT_EQ(record.NativeSourceCurves[0].Points[1].Value, 3.0f);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(record.NativeSourceCurves[0], 450.0f), 1.5f);
    EXPECT_EQ(record.NativeSourceCurves[1].ComponentIndex, 1u);
    EXPECT_EQ(record.NativeSourceCurves[1].Offset, 0x34u);
    EXPECT_TRUE(record.NativeSourceCurves[1].Decoded);
    EXPECT_TRUE(record.NativeSourceCurves[1].WrapEnabled);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(record.NativeSourceCurves[1], 450.0f), 1.0f);
    EXPECT_EQ(record.HeaderWords[3], 0x00340014u);
    EXPECT_EQ(record.ScalarTrackOffsets[0], 0x14u);
    EXPECT_EQ(record.ScalarTrackOffsets[1], 0x34u);
    EXPECT_FALSE(record.ScalarKeyframesDecoded);

    ASSERT_EQ(record.ScalarTracks.size(), 2u);
    const auto& component0 = record.ScalarTracks[0];
    EXPECT_EQ(component0.ComponentIndex, 0u);
    EXPECT_EQ(component0.Offset, 0x14u);
    EXPECT_EQ(component0.Size, 0x20u);
    EXPECT_TRUE(component0.KeyframesDecoded);
    EXPECT_EQ(component0.KeyframeCountCandidate, 2u);
    EXPECT_EQ(component0.LastFrameCandidate, 900u);
    ASSERT_EQ(component0.Keyframes.size(), 2u);
    EXPECT_EQ(component0.Keyframes[1].Frame, 900u);
    EXPECT_FLOAT_EQ(component0.Keyframes[1].Value, 3.0f);

    const auto& component1 = record.ScalarTracks[1];
    EXPECT_EQ(component1.ComponentIndex, 1u);
    EXPECT_EQ(component1.Offset, 0x34u);
    EXPECT_EQ(component1.Size, 0x20u);
    EXPECT_TRUE(component1.KeyframesDecoded);
    ASSERT_EQ(component1.Keyframes.size(), 2u);
    EXPECT_EQ(component1.Keyframes[1].Frame, 900u);
    EXPECT_FLOAT_EQ(component1.Keyframes[1].Value, 2.0f);
}

TEST(Oot3dNativeAssets, ParsesCmabMadsRecordOffsetTable) {
    auto bytes = MinimalCmabMadsRecordOffsetTable();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_blkobj.zar!misc/m_WhontR_0d_model.cmab");

    EXPECT_EQ(animation.MadsRecordCount, 2u);
    EXPECT_EQ(animation.MadsStride, 0x10u);
    ASSERT_EQ(animation.MadsRecordOffsets.size(), 2u);
    EXPECT_EQ(animation.MadsRecordOffsets[0], 0x44u);
    EXPECT_EQ(animation.MadsRecordOffsets[1], 0x98u);

    ASSERT_EQ(animation.MmadRecords.size(), 2u);
    const auto& first = animation.MmadRecords[0];
    EXPECT_EQ(first.Offset, 0x44u);
    EXPECT_EQ(first.MadsRecordOffset, 0x44u);
    EXPECT_TRUE(first.MadsRecordOffsetMatched);
    EXPECT_EQ(first.Size, 0x54u);
    EXPECT_EQ(first.NativeType, 1u);
    EXPECT_TRUE(first.NativeTypeFactorySupported);
    EXPECT_EQ(first.TargetMaterialIndex, 10u);
    EXPECT_EQ(first.TargetComponentOrStageIndex, 0u);
    EXPECT_TRUE(first.TargetSelectorPresent);
    EXPECT_EQ(first.NativeValueKind, "transform_vec2_gate_1_plus_selector");
    EXPECT_EQ(first.NativeChannelOffsetTableOffset, 0x10u);
    ASSERT_EQ(first.NativeChannelOffsets.size(), 2u);
    ASSERT_EQ(first.ScalarTracks.size(), 2u);

    const auto& second = animation.MmadRecords[1];
    EXPECT_EQ(second.Offset, 0x98u);
    EXPECT_EQ(second.MadsRecordOffset, 0x98u);
    EXPECT_TRUE(second.MadsRecordOffsetMatched);
    EXPECT_EQ(second.Size, 0x34u);
    EXPECT_EQ(second.NativeType, 3u);
    EXPECT_TRUE(second.NativeTypeFactorySupported);
    EXPECT_EQ(second.TargetMaterialIndex, 10u);
    EXPECT_EQ(second.TargetComponentOrStageIndex, 0u);
    EXPECT_FALSE(second.TargetSelectorPresent);
    EXPECT_EQ(second.NativeValueKind, "material_color_vec4_gate_0");
    EXPECT_EQ(second.NativeChannelOffsetTableOffset, 0x0Cu);
    EXPECT_EQ(second.NativeChannelOffsetCount, 4u);
    ASSERT_EQ(second.NativeChannelOffsets.size(), 4u);
    EXPECT_EQ(second.NativeChannelOffsets[0].TableOffset, 0x0Cu);
    EXPECT_EQ(second.NativeChannelOffsets[0].RelativeOffset, 0);
    EXPECT_FALSE(second.NativeChannelOffsets[0].Present);
    EXPECT_EQ(second.NativeChannelOffsets[1].TableOffset, 0x0Eu);
    EXPECT_EQ(second.NativeChannelOffsets[1].RelativeOffset, 0);
    EXPECT_FALSE(second.NativeChannelOffsets[1].Present);
    EXPECT_EQ(second.NativeChannelOffsets[2].TableOffset, 0x10u);
    EXPECT_EQ(second.NativeChannelOffsets[2].RelativeOffset, 0x14);
    EXPECT_TRUE(second.NativeChannelOffsets[2].Present);
    EXPECT_EQ(second.NativeChannelOffsets[3].TableOffset, 0x12u);
    EXPECT_EQ(second.NativeChannelOffsets[3].RelativeOffset, 0);
    EXPECT_FALSE(second.NativeChannelOffsets[3].Present);
    ASSERT_EQ(second.NativeSourceCurves.size(), 1u);
    EXPECT_EQ(second.NativeSourceCurves[0].ComponentIndex, 2u);
    EXPECT_EQ(second.NativeSourceCurves[0].Offset, 0x14u);
    EXPECT_TRUE(second.NativeSourceCurves[0].Decoded);
    EXPECT_EQ(second.NativeSourceCurves[0].Type, 1u);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(second.NativeSourceCurves[0], 900.0f), 1.0f);
    EXPECT_TRUE(second.ScalarKeyframesDecoded);
}

TEST(Oot3dNativeAssets, ParsesCmabTextureSwapPayload) {
    auto bytes = MinimalCmabTextureSwap();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_link_child_new.zar!child/misc/childlink_eye.cmab");

    EXPECT_EQ(animation.Version, 1u);
    EXPECT_EQ(animation.DeclaredFileSize, bytes.size());
    EXPECT_EQ(animation.StringTableOffset, 0xB0u);
    EXPECT_EQ(animation.TextureDataOffset, 0xD0u);
    EXPECT_EQ(animation.TexturePayloadOffsetCandidate, 0x64u);
    EXPECT_EQ(animation.TexturePayloadOffset, 0x74u);
    EXPECT_EQ(animation.FrameCountCandidate, 2u);
    ASSERT_EQ(animation.StringTableNames.size(), 2u);
    EXPECT_EQ(animation.StringTableNames[0], "c_eye01");
    EXPECT_EQ(animation.StringTableNames[1], "c_eye02");

    ASSERT_EQ(animation.MmadRecords.size(), 1u);
    const auto& record = animation.MmadRecords[0];
    EXPECT_EQ(record.HeaderWords[0], 2u);
    EXPECT_EQ(record.HeaderWords[1], 14u);
    EXPECT_EQ(record.HeaderWords[4], 3u);
    EXPECT_EQ(record.NativeType, 2u);
    EXPECT_TRUE(record.TargetSelectorPresent);
    EXPECT_EQ(record.TargetSelector, 0);
    EXPECT_EQ(record.NativeValueKind, "texture_frame_int_gate_7_plus_stage");
    EXPECT_EQ(record.NativeChannelOffsetTableOffset, 0x10u);
    EXPECT_EQ(record.NativeChannelOffsetCount, 1u);
    ASSERT_EQ(record.NativeChannelOffsets.size(), 1u);
    EXPECT_EQ(record.NativeChannelOffsets[0].RelativeOffset, 0x14);
    EXPECT_TRUE(record.NativeChannelOffsets[0].Present);
    ASSERT_EQ(record.NativeSourceCurves.size(), 1u);
    EXPECT_EQ(record.NativeSourceCurves[0].ComponentIndex, 0u);
    EXPECT_EQ(record.NativeSourceCurves[0].Offset, 0x14u);
    EXPECT_EQ(record.NativeSourceCurves[0].Size, 0x20u);
    EXPECT_TRUE(record.NativeSourceCurves[0].Decoded);
    EXPECT_EQ(record.NativeSourceCurves[0].Status, "decoded");
    EXPECT_EQ(record.NativeSourceCurves[0].Type, 3u);
    EXPECT_EQ(record.NativeSourceCurves[0].PointStrideBytes, 0x08u);
    EXPECT_FALSE(record.NativeSourceCurves[0].WrapEnabled);
    ASSERT_EQ(record.NativeSourceCurves[0].Points.size(), 2u);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(record.NativeSourceCurves[0], 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(record.NativeSourceCurves[0], 1.0f), 1.0f);
    EXPECT_TRUE(record.ScalarKeyframesDecoded);
    ASSERT_EQ(record.ScalarKeyframes.size(), 2u);
    EXPECT_EQ(record.ScalarKeyframes[1].Frame, 1u);
    EXPECT_FLOAT_EQ(record.ScalarKeyframes[1].Value, 1.0f);

    EXPECT_TRUE(animation.TexturePayloadDecoded);
    ASSERT_EQ(animation.EmbeddedTextures.size(), 2u);
    const auto& texture0 = animation.EmbeddedTextures[0];
    EXPECT_EQ(texture0.Name, "c_eye01");
    EXPECT_EQ(texture0.MipmapCount, 1u);
    EXPECT_EQ(texture0.Width, 1u);
    EXPECT_EQ(texture0.Height, 1u);
    EXPECT_EQ(texture0.TextureFormat, 0x6757u);
    EXPECT_EQ(texture0.DataType, 0x1401u);
    EXPECT_EQ(texture0.DataOffset, 0u);
    EXPECT_EQ(texture0.DataSize, 1u);
    EXPECT_EQ(texture0.Data[0], 0x7Fu);
    EXPECT_TRUE(texture0.Rgba8Decoded);
    ASSERT_EQ(texture0.Rgba8.size(), 4u);
    EXPECT_EQ(texture0.Rgba8[0], 0x7Fu);
    EXPECT_EQ(texture0.Rgba8[1], 0x7Fu);
    EXPECT_EQ(texture0.Rgba8[2], 0x7Fu);
    EXPECT_EQ(texture0.Rgba8[3], 0xFFu);

    const auto& texture1 = animation.EmbeddedTextures[1];
    EXPECT_EQ(texture1.Name, "c_eye02");
    EXPECT_EQ(texture1.MipmapCount, 1u);
    EXPECT_EQ(texture1.DataOffset, 1u);
    EXPECT_EQ(texture1.DataSize, 1u);
    EXPECT_EQ(texture1.Data[0], 0x80u);
    EXPECT_TRUE(texture1.Rgba8Decoded);
    ASSERT_EQ(texture1.Rgba8.size(), 4u);
    EXPECT_EQ(texture1.Rgba8[0], 0x80u);
    EXPECT_EQ(texture1.Rgba8[1], 0x80u);
    EXPECT_EQ(texture1.Rgba8[2], 0x80u);
    EXPECT_EQ(texture1.Rgba8[3], 0xFFu);
}

TEST(Oot3dNativeAssets, SamplesCmabWrappedHermiteSourceCurve) {
    auto bytes = MinimalCmabWrappedHermiteSourceCurve();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_blkobj.zar!misc/hermite_wrap_test.cmab");

    ASSERT_EQ(animation.MmadRecords.size(), 1u);
    const auto& record = animation.MmadRecords[0];
    EXPECT_EQ(record.NativeType, 3u);
    ASSERT_EQ(record.NativeChannelOffsets.size(), 4u);
    EXPECT_FALSE(record.NativeChannelOffsets[0].Present);
    EXPECT_TRUE(record.NativeChannelOffsets[1].Present);
    EXPECT_EQ(record.NativeChannelOffsets[1].RelativeOffset, 0x14);

    ASSERT_EQ(record.NativeSourceCurves.size(), 1u);
    const auto& curve = record.NativeSourceCurves[0];
    EXPECT_EQ(curve.ComponentIndex, 1u);
    EXPECT_EQ(curve.Offset, 0x14u);
    EXPECT_EQ(curve.Size, 0x30u);
    EXPECT_TRUE(curve.Decoded);
    EXPECT_EQ(curve.Status, "decoded");
    EXPECT_EQ(curve.Type, 2u);
    EXPECT_EQ(curve.HeaderWord0C, 10u);
    EXPECT_EQ(curve.PointStrideBytes, 0x10u);
    EXPECT_TRUE(curve.WrapEnabled);
    EXPECT_TRUE(curve.SampleFrame0Valid);
    EXPECT_FLOAT_EQ(curve.SampleFrame0, 0.0f);
    ASSERT_EQ(curve.Points.size(), 2u);
    EXPECT_EQ(curve.Points[1].Frame, 10);
    EXPECT_FLOAT_EQ(curve.Points[1].Value, 10.0f);

    EXPECT_NEAR(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(curve, 5.0f), 5.0f, 1.0e-6f);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(curve, -1.0f), 10.0f);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::SampleCmabSourceCurve(curve, 11.0f), 0.0f);
}

TEST(Oot3dNativeAssets, ParsesFacebMaterialFrameTrack) {
    auto bytes = MinimalFacebTrack();
    auto track = ThreeDsRecomp::Oot3d::ParseFacebMaterialFrameTrackBytes(
        bytes, "actor/zelda_link_child_new.zar!boy/anim/o_get_mae.faceb");

    EXPECT_EQ(track.Source, "actor/zelda_link_child_new.zar!boy/anim/o_get_mae.faceb");
    EXPECT_EQ(track.EntryCount, 3u);
    EXPECT_EQ(track.ExpectedSize, bytes.size());
    EXPECT_TRUE(track.SizeMatchesEntryCount);
    ASSERT_EQ(track.Events.size(), 3u);

    EXPECT_EQ(track.Events[0].Index, 0u);
    EXPECT_EQ(track.Events[0].Frame, 0u);
    EXPECT_EQ(track.Events[0].EyeIndex, 0xFFu);
    EXPECT_EQ(track.Events[0].MouthIndex, 0xFFu);
    EXPECT_EQ(track.Events[1].Frame, 15u);
    EXPECT_EQ(track.Events[1].EyeIndex, 1u);
    EXPECT_EQ(track.Events[1].MouthIndex, 0u);
    EXPECT_EQ(track.Events[2].Frame, 16u);
    EXPECT_EQ(track.Events[2].EyeIndex, 2u);
    EXPECT_EQ(track.Events[2].MouthIndex, 1u);
}

TEST(Oot3dNativeAssets, SamplesFacebHoldAsIndependentPersistentLanes) {
    auto track = ThreeDsRecomp::Oot3d::ParseFacebMaterialFrameTrackBytes(MinimalFacebTrack());

    const auto held = ThreeDsRecomp::Oot3d::SampleFacebMaterialFrameTrack(track, 0.0f);
    EXPECT_FALSE(held.EyeSelected);
    EXPECT_FALSE(held.MouthSelected);
    EXPECT_EQ(held.HoldValue, 0xFFu);

    const auto firstUpdate = ThreeDsRecomp::Oot3d::SampleFacebMaterialFrameTrack(track, 15.0f);
    EXPECT_TRUE(firstUpdate.EyeSelected);
    EXPECT_TRUE(firstUpdate.MouthSelected);
    EXPECT_EQ(firstUpdate.EyeIndex, 1u);
    EXPECT_EQ(firstUpdate.MouthIndex, 0u);

    ThreeDsRecomp::Oot3d::FacebMaterialFrameTrack independentTrack;
    independentTrack.Events = {
        { 0, 0, 2, 0xFF },
        { 1, 5, 0xFF, 3 },
    };
    const auto independent =
        ThreeDsRecomp::Oot3d::SampleFacebMaterialFrameTrack(independentTrack, 5.0f);
    EXPECT_TRUE(independent.EyeSelected);
    EXPECT_TRUE(independent.MouthSelected);
    EXPECT_EQ(independent.EyeIndex, 2u);
    EXPECT_EQ(independent.MouthIndex, 3u);
}

TEST(Oot3dNativeActorRenderProvider, ResolvesFaceHoldAcrossAnimationTracks) {
    ThreeDsRecomp::Oot3d::NativeActorFaceRuntimeState state;
    ThreeDsRecomp::Oot3d::NativeActorFaceSample hold;
    hold.Available = true;
    hold.HoldValue = 0xFF;

    auto resolved = ThreeDsRecomp::Oot3d::ResolveNativeActorFaceState(hold, state);
    EXPECT_TRUE(resolved.Available);
    EXPECT_EQ(resolved.EyeIndex, 0u);
    EXPECT_EQ(resolved.MouthIndex, 0u);
    EXPECT_EQ(resolved.Status, "actor_face_state_held");

    ThreeDsRecomp::Oot3d::NativeActorFaceSample eyeUpdate = hold;
    eyeUpdate.EyeSelected = true;
    eyeUpdate.EyeIndex = 4;
    resolved = ThreeDsRecomp::Oot3d::ResolveNativeActorFaceState(eyeUpdate, state);
    EXPECT_EQ(resolved.EyeIndex, 4u);
    EXPECT_EQ(resolved.MouthIndex, 0u);

    resolved = ThreeDsRecomp::Oot3d::ResolveNativeActorFaceState(hold, state);
    EXPECT_EQ(resolved.EyeIndex, 4u);
    EXPECT_EQ(resolved.MouthIndex, 0u);
}

TEST(Oot3dNativeActorRenderProvider, ResolvesProfileSegmentContinuityByNativeCsab) {
    const nlohmann::json profile = {
        { "segment_continuity_contract",
          {
              { "format", "oot3d_character_segment_continuity_contract_v1" },
              { "status", "ready" },
              { "normalization_policy", "cumulative_segment_root_offset" },
              { "root_motion_bone", 1 },
              { "segments",
                nlohmann::json::array({
                    {
                        { "csab_name", "child/anim/cl_nml_climb_upL.csab" },
                        { "normalization_offset", { 0.0f, 0.0f, -636.555938f } },
                    },
                }) },
          } },
    };

    const auto matched = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationSpatialBinding(
        profile, "child/anim/cl_nml_climb_upL.csab");
    EXPECT_TRUE(matched.Available);
    EXPECT_TRUE(matched.SegmentContractPresent);
    EXPECT_TRUE(matched.SegmentMatched);
    EXPECT_EQ(matched.RootMotionBone, 1);
    EXPECT_FLOAT_EQ(matched.SegmentNormalizationOffset[0], 0.0f);
    EXPECT_FLOAT_EQ(matched.SegmentNormalizationOffset[1], 0.0f);
    EXPECT_FLOAT_EQ(matched.SegmentNormalizationOffset[2], -636.555938f);

    const auto unsegmented = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationSpatialBinding(
        profile, "child/anim/nml_run_free.csab");
    EXPECT_TRUE(unsegmented.Available);
    EXPECT_FALSE(unsegmented.SegmentMatched);
    EXPECT_EQ(unsegmented.RootMotionBone, 1);
    EXPECT_EQ(unsegmented.Status, "actor_spatial_animation_not_segmented");
}

TEST(Oot3dNativeActorRenderProvider, ResolvesProfileAnimationTimeSourceByNativeCsab) {
    const nlohmann::json profile = {
        { "animation_time_source_contract",
          {
              { "format", "oot3d_character_animation_time_source_contract_v1" },
              { "status", "ready" },
              { "default_source", "skel_animation_clock" },
              { "bindings",
                nlohmann::json::array({
                    {
                        { "csab_name", "child/anim/nml_run_free.csab" },
                        { "source", "player_locomotion_cycle" },
                        { "sample_mode", "direct_scaled_frame" },
                        { "source_frame_span", 29.0f },
                        { "sample_scale", 20.0f / 29.0f },
                        { "sample_offset", 0.0f },
                    },
                }) },
          } },
    };

    const auto matched = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationTimeSourceBinding(
        profile, "child/anim/nml_run_free.csab");
    EXPECT_TRUE(matched.Available);
    EXPECT_TRUE(matched.ContractPresent);
    EXPECT_TRUE(matched.Matched);
    EXPECT_TRUE(matched.DirectSample);
    EXPECT_EQ(matched.Source, "player_locomotion_cycle");
    EXPECT_FLOAT_EQ(matched.SourceFrameSpan, 29.0f);
    EXPECT_NEAR(matched.SampleScale, 20.0f / 29.0f, 0.000001f);
    EXPECT_NEAR(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(matched, 28.0f),
        28.0f * (20.0f / 29.0f), 0.000001f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(matched, 29.0f),
        0.0f);

    const auto defaultClock = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationTimeSourceBinding(
        profile, "child/anim/nml_wait_free.csab");
    EXPECT_TRUE(defaultClock.Available);
    EXPECT_FALSE(defaultClock.Matched);
    EXPECT_FALSE(defaultClock.DirectSample);
    EXPECT_EQ(defaultClock.Source, "skel_animation_clock");
}

TEST(Oot3dNativeActorRenderProvider, ResolvesFullSpanPlayerAnimationTimeline) {
    const nlohmann::json profile = {
        { "animation_time_source_contract",
          {
              { "format", "oot3d_character_animation_time_source_contract_v1" },
              { "status", "ready" },
              { "default_source", "skel_animation_clock" },
              { "bindings",
                nlohmann::json::array({
                    {
                        { "csab_name", "boy/anim/nml_100step_up.csab" },
                        { "source", "player_skel_animation_timeline" },
                        { "sample_mode", "direct_clamped_normalized_frame" },
                        { "source_frame_span", 18.0f },
                        { "sample_scale", 27.0f / 17.0f },
                        { "sample_offset", 0.0f },
                        { "scaffold_playback_scale", 18.0f / 28.0f },
                        { "native_frame_span", 28.0f },
                        { "playback_mode", "once_full_span" },
                    },
                }) },
          } },
    };

    const auto binding = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationTimeSourceBinding(
        profile, "boy/anim/nml_100step_up.csab");
    EXPECT_TRUE(binding.Available);
    EXPECT_TRUE(binding.DirectSample);
    EXPECT_TRUE(binding.ClampSample);
    EXPECT_TRUE(binding.AdjustScaffoldPlayback);
    EXPECT_EQ(binding.Source, "player_skel_animation_timeline");
    EXPECT_FLOAT_EQ(binding.NativeFrameSpan, 28.0f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(binding, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(binding, 17.0f), 27.0f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(binding, 99.0f), 27.0f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationDirectSampleFrame(
            binding, 22.0f, 4.0f, 40.0f),
        13.5f);
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationScaffoldPlaybackSpeed(
            binding, 1.3f, 0.0f, 17.0f, true),
        1.3f * (18.0f / 28.0f));
    EXPECT_FLOAT_EQ(
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationScaffoldPlaybackSpeed(
            binding, 1.3f, 0.0f, 8.0f, true),
        1.3f);
}

TEST(Oot3dNativeActorRenderProvider, ResolvesNativePlayerLocomotionControllerVariant) {
    const nlohmann::json profile = {
        { "format", "oot3d_character_runtime_profile_v1" },
        { "animation_controller_contract",
          {
              { "format", "oot3d_character_animation_controller_contract_v1" },
              { "status", "ready" },
              { "controllers",
                nlohmann::json::array({
                    {
                        { "id", "player_forward_locomotion" },
                        { "notification_semantic", "player_forward_locomotion" },
                        { "phase_source", "player_locomotion_cycle" },
                        { "source_frame_span", 29.0f },
                        { "blend_source", "player_linear_velocity" },
                        { "blend_threshold", 3.7f },
                        { "blend_scale", 0.8f },
                        { "warmup_source", "player_locomotion_blend_weight" },
                        { "variants",
                          nlohmann::json::array({
                              {
                                  { "animation_type_index", 0 },
                                  { "walk_csab_name", "child/anim/nml_walk_free.csab" },
                                  { "run_csab_name", "child/anim/nml_run_free.csab" },
                              },
                              {
                                  { "animation_type_index", 2 },
                                  { "walk_csab_name", "child/anim/nml_walk.csab" },
                                  { "run_csab_name", "child/anim/nml_run.csab" },
                              },
                          }) },
                    },
                }) },
          } },
    };

    const auto binding = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationControllerBinding(
        profile, "player_forward_locomotion", 2);
    EXPECT_TRUE(binding.Available);
    EXPECT_EQ(binding.ControllerId, "player_forward_locomotion");
    EXPECT_EQ(binding.PhaseSource, "player_locomotion_cycle");
    EXPECT_FLOAT_EQ(binding.SourceFrameSpan, 29.0f);
    EXPECT_FLOAT_EQ(binding.BlendThreshold, 3.7f);
    EXPECT_FLOAT_EQ(binding.BlendScale, 0.8f);
    EXPECT_EQ(binding.WalkCsabName, "child/anim/nml_walk.csab");
    EXPECT_EQ(binding.RunCsabName, "child/anim/nml_run.csab");

    const auto missingVariant = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationControllerBinding(
        profile, "player_forward_locomotion", 5);
    EXPECT_FALSE(missingVariant.Available);
    EXPECT_EQ(missingVariant.Status, "actor_animation_controller_variant_unavailable");
}

TEST(Oot3dNativeActorRenderProvider, ResolvesNativePlayerSkelAnimeSamplingPolicy) {
    const nlohmann::json profile = {
        { "skel_anime_sampling_contract",
          {
              { "format", "oot3d_skel_anime_sampling_contract_v1" },
              { "frame_data_path", 1 },
              { "special_bone", 1 },
              { "default_channel_mask", 2 },
              { "special_bone_channel_mask", 3 },
              { "base_translation", { 0.0f, 3538.080078125f, 0.0f } },
          } },
    };

    const auto binding =
        ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationSamplingBinding(profile);

    EXPECT_TRUE(binding.Available);
    EXPECT_TRUE(binding.ContractPresent);
    EXPECT_EQ(binding.Status, "ready");
    EXPECT_EQ(binding.Policy.DefaultChannelMask, 2u);
    EXPECT_EQ(binding.Policy.SpecialBone, 1);
    EXPECT_EQ(binding.Policy.SpecialBoneChannelMask, 3u);
    EXPECT_EQ(binding.BaseTranslation,
              (std::array<float, 3>{ 0.0f, 3538.080078125f, 0.0f }));
}

TEST(Oot3dNativeActorRenderProvider, ResolvesProfileRootMotionOwnership) {
    const nlohmann::json profile = {
        { "root_motion_ownership_contract",
          {
              { "format", "oot3d_character_root_motion_ownership_contract_v1" },
              { "status", "ready" },
              { "movement_enabled_flag", 1 },
              { "update_y_flag", 2 },
              { "translation_policy", "align_only_controller_consumed_axes" },
              { "xz_ownership", "controller_when_movement_enabled" },
              { "y_ownership", "controller_when_movement_enabled_and_update_y" },
              { "root_rotation_policy", "preserve_authored_oot3d" },
          } },
    };

    const auto ownership =
        ThreeDsRecomp::Oot3d::ResolveNativeActorRootMotionOwnership(profile);

    EXPECT_TRUE(ownership.Available);
    EXPECT_TRUE(ownership.ContractPresent);
    EXPECT_EQ(ownership.MovementEnabledFlag, 1u);
    EXPECT_EQ(ownership.UpdateYFlag, 2u);
    EXPECT_TRUE(ownership.AlignXzWhenMovementEnabled);
    EXPECT_TRUE(ownership.AlignYWhenMovementEnabledAndUpdateY);
    EXPECT_TRUE(ownership.PreserveAuthoredRootRotation);

    const auto preserved = ThreeDsRecomp::Oot3d::ResolveNativeActorRootMotionAlignment(
        ownership, 0, { 100.0f, 200.0f, 300.0f },
        { 10.0f, 20.0f, 30.0f });
    EXPECT_TRUE(preserved.Available);
    EXPECT_FALSE(preserved.ControllerOwnsXz);
    EXPECT_FALSE(preserved.ControllerOwnsY);
    EXPECT_EQ(preserved.Translation, (std::array<float, 3>{ 0.0f, 0.0f, 0.0f }));

    const auto xzOwned = ThreeDsRecomp::Oot3d::ResolveNativeActorRootMotionAlignment(
        ownership, 1, { 100.0f, 200.0f, 300.0f },
        { 10.0f, 20.0f, 30.0f });
    EXPECT_TRUE(xzOwned.ControllerOwnsXz);
    EXPECT_FALSE(xzOwned.ControllerOwnsY);
    EXPECT_EQ(xzOwned.Translation, (std::array<float, 3>{ 90.0f, 0.0f, 270.0f }));

    const auto xyzOwned = ThreeDsRecomp::Oot3d::ResolveNativeActorRootMotionAlignment(
        ownership, 1 | 2, { 0.0f, 3538.080078125f, 0.0f },
        { -100.0f, -2815.0f, -600.0f });
    EXPECT_TRUE(xyzOwned.ControllerOwnsXz);
    EXPECT_TRUE(xyzOwned.ControllerOwnsY);
    EXPECT_EQ(xyzOwned.Translation,
              (std::array<float, 3>{ 100.0f, 6353.080078125f, 600.0f }));
}

TEST(Oot3dNativeAssets, BindsCmabTextureSwapToNativeMaterialLane) {
    auto bytes = MinimalCmabTextureSwap();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_link_child_new.zar!child/misc/childlink_eye.cmab");
    auto targetModel = SyntheticCmabTargetModel();

    auto binding = ThreeDsRecomp::Oot3d::BuildCmabMaterialAnimationBinding(targetModel, animation);

    EXPECT_EQ(binding.Source, animation.Source);
    EXPECT_EQ(binding.TargetModelSource, targetModel.Source);
    EXPECT_EQ(binding.Role, "eye");
    EXPECT_EQ(binding.Status, "ready");
    EXPECT_TRUE(binding.TargetResolved);
    EXPECT_TRUE(binding.RuntimeLaneBindingDecoded);
    EXPECT_TRUE(binding.TextureSwapTrackDecoded);
    EXPECT_TRUE(binding.TextureSwapFramesResolved);
    EXPECT_EQ(binding.FrameCountCandidate, 2u);
    EXPECT_EQ(binding.MmadRecordIndex, 0);
    ASSERT_EQ(binding.TargetTextureIndices.size(), 2u);
    EXPECT_EQ(binding.TargetTextureIndices[0], 0);
    EXPECT_EQ(binding.TargetTextureIndices[1], -1);
    ASSERT_EQ(binding.EmbeddedTextureIndices.size(), 2u);
    EXPECT_EQ(binding.EmbeddedTextureIndices[0], 0);
    EXPECT_EQ(binding.EmbeddedTextureIndices[1], 1);
    ASSERT_EQ(binding.TargetMaterialIndices.size(), 1u);
    EXPECT_EQ(binding.TargetMaterialIndices[0], 14);
    ASSERT_EQ(binding.NativeRuntimeMaterialLaneIndices.size(), 1u);
    EXPECT_EQ(binding.NativeRuntimeMaterialLaneIndices[0], 14);

    ASSERT_EQ(binding.TextureSwapFrames.size(), 2u);
    EXPECT_EQ(binding.TextureSwapFrames[0].KeyframeIndex, 0u);
    EXPECT_EQ(binding.TextureSwapFrames[0].Frame, 0u);
    EXPECT_FLOAT_EQ(binding.TextureSwapFrames[0].Value, 0.0f);
    EXPECT_EQ(binding.TextureSwapFrames[0].TextureFrameIndex, 0);
    EXPECT_EQ(binding.TextureSwapFrames[0].TextureName, "c_eye01");
    EXPECT_EQ(binding.TextureSwapFrames[0].EmbeddedTextureIndex, 0);
    EXPECT_EQ(binding.TextureSwapFrames[0].TargetTextureIndex, 0);
    EXPECT_EQ(binding.TextureSwapFrames[1].KeyframeIndex, 1u);
    EXPECT_EQ(binding.TextureSwapFrames[1].Frame, 1u);
    EXPECT_FLOAT_EQ(binding.TextureSwapFrames[1].Value, 1.0f);
    EXPECT_EQ(binding.TextureSwapFrames[1].TextureFrameIndex, 1);
    EXPECT_EQ(binding.TextureSwapFrames[1].TextureName, "c_eye02");
    EXPECT_EQ(binding.TextureSwapFrames[1].EmbeddedTextureIndex, 1);
    EXPECT_EQ(binding.TextureSwapFrames[1].TargetTextureIndex, -1);
}

TEST(Oot3dNativeAssets, AppliesCmabTextureSwapFrameToRenderMaterialLane) {
    auto bytes = MinimalCmabTextureSwap();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/zelda_link_child_new.zar!child/misc/childlink_eye.cmab");
    auto targetModel = SyntheticCmabTargetModel();

    auto baseFrameRenderModel = SyntheticCmabRenderModel();
    const auto baseAppliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        baseFrameRenderModel, targetModel, { animation }, 0.0f);
    ASSERT_EQ(baseAppliedCount, 1u);
    ASSERT_EQ(baseFrameRenderModel.Textures.size(), 2u);
    ASSERT_EQ(baseFrameRenderModel.Batches.size(), 1u);
    EXPECT_EQ(baseFrameRenderModel.Batches[0].Material.TextureIndex, 0);
    EXPECT_TRUE(baseFrameRenderModel.Batches[0].Material.NativeMaterialAnimationApplied);
    EXPECT_EQ(baseFrameRenderModel.Batches[0].Material.NativeMaterialAnimationRole, "eye");
    EXPECT_EQ(baseFrameRenderModel.Batches[0].Material.NativeMaterialAnimationTextureName, "c_eye01");
    EXPECT_EQ(baseFrameRenderModel.Batches[0].Material.NativeMaterialAnimationTextureFrameIndex, 0);
    EXPECT_EQ(baseFrameRenderModel.Batches[0].Material.NativeMaterialAnimationFrame, 0u);

    auto swapFrameRenderModel = SyntheticCmabRenderModel();
    const auto swapAppliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        swapFrameRenderModel, targetModel, { animation }, 1.0f);
    ASSERT_EQ(swapAppliedCount, 1u);
    ASSERT_EQ(swapFrameRenderModel.Textures.size(), 3u);
    EXPECT_EQ(swapFrameRenderModel.Textures[2].Name, "c_eye02");
    ASSERT_EQ(swapFrameRenderModel.Textures[2].Rgba8.size(), 4u);
    EXPECT_EQ(swapFrameRenderModel.Textures[2].Rgba8[0], 0x80u);
    ASSERT_EQ(swapFrameRenderModel.Batches.size(), 1u);
    const auto& material = swapFrameRenderModel.Batches[0].Material;
    EXPECT_TRUE(material.Textured);
    EXPECT_EQ(material.TextureIndex, 2);
    EXPECT_EQ(material.TextureMapperSlot, 0u);
    EXPECT_EQ(material.TextureMapperTextureIndices[0], 2);
    EXPECT_EQ(material.TextureBindingSource, "oot3d_cmab_material_animation_texture_swap");
    EXPECT_TRUE(material.NativeMaterialAnimationApplied);
    EXPECT_EQ(material.NativeMaterialAnimationSource, animation.Source);
    EXPECT_EQ(material.NativeMaterialAnimationRole, "eye");
    EXPECT_EQ(material.NativeMaterialAnimationTextureName, "c_eye02");
    EXPECT_EQ(material.NativeMaterialAnimationTextureFrameIndex, 1);
    EXPECT_EQ(material.NativeMaterialAnimationFrame, 1u);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(swapFrameRenderModel);
    EXPECT_EQ(summary["texture_count"].get<size_t>(), 3u);
    EXPECT_EQ(summary["native_material_animation_applied_batch_count"].get<size_t>(), 1u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_EQ(summary["native_batches"][0]["native_material_animation_texture_name"].get<std::string>(),
              "c_eye02");

    auto roleFrameRenderModel = SyntheticCmabRenderModel();
    const std::array roleAnimations = { animation };
    const auto roleAppliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrames(
        roleFrameRenderModel, targetModel, roleAnimations, { { "eye", 1.0f } }, 0.0f);
    ASSERT_EQ(roleAppliedCount, 1u);
    ASSERT_EQ(roleFrameRenderModel.Textures.size(), 3u);
    EXPECT_EQ(roleFrameRenderModel.Batches[0].Material.TextureIndex, 2);
    EXPECT_EQ(roleFrameRenderModel.Batches[0].Material.NativeMaterialAnimationTextureName, "c_eye02");
    EXPECT_EQ(roleFrameRenderModel.Batches[0].Material.NativeMaterialAnimationTextureFrameIndex, 1);
}

TEST(Oot3dNativeAssets, AppliesCmabMaterialColorCurveToRenderMaterialLane) {
    auto bytes = MinimalCmabMadsRecordOffsetTable();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/test.zar!misc/material_color.cmab");
    auto targetModel = SyntheticCmabTargetModel();
    auto renderModel = SyntheticCmabColorRenderModel();

    const auto appliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        renderModel, targetModel, { animation }, 450.0f);

    ASSERT_EQ(appliedCount, 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.NativeMaterialAnimationApplied);
    EXPECT_FALSE(material.NativeMaterialAnimationTextureApplied);
    EXPECT_TRUE(material.NativeMaterialAnimationColorApplied);
    EXPECT_EQ(material.NativeMaterialAnimationSource, animation.Source);
    EXPECT_EQ(material.NativeMaterialAnimationColorKind, "material_color_vec4_gate_0");
    EXPECT_EQ(material.NativeMaterialAnimationColorSelector, -1);
    EXPECT_EQ(material.NativeMaterialAnimationColorComponentMask, 0x04u);
    EXPECT_FLOAT_EQ(material.NativeMaterialAnimationColorSampleFrame, 450.0f);
    EXPECT_EQ(material.DiffuseColor.R, 10u);
    EXPECT_EQ(material.DiffuseColor.G, 20u);
    EXPECT_EQ(material.DiffuseColor.B, 128u);
    EXPECT_EQ(material.DiffuseColor.A, 40u);
    EXPECT_EQ(material.NativeMaterialAnimationColorValue.B, 128u);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_material_animation_applied_batch_count"].get<size_t>(), 1u);
    EXPECT_EQ(summary["native_material_animation_texture_applied_batch_count"].get<size_t>(), 0u);
    EXPECT_EQ(summary["native_material_animation_color_applied_batch_count"].get<size_t>(), 1u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_TRUE(summary["native_batches"][0]["native_material_animation_color_applied"].get<bool>());
    EXPECT_EQ(summary["native_batches"][0]["native_material_animation_color_component_mask"].get<uint32_t>(),
              0x04u);
}

TEST(Oot3dNativeAssets, AppliesCmabConstantColorCurveToRenderMaterialLane) {
    auto bytes = MinimalCmabConstantColor();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/test.zar!misc/constant_color.cmab");
    auto targetModel = SyntheticCmabTargetModel();
    auto renderModel = SyntheticCmabColorRenderModel();

    const auto appliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        renderModel, targetModel, { animation }, 900.0f);

    ASSERT_EQ(appliedCount, 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.NativeMaterialAnimationApplied);
    EXPECT_FALSE(material.NativeMaterialAnimationTextureApplied);
    EXPECT_TRUE(material.NativeMaterialAnimationColorApplied);
    EXPECT_EQ(material.NativeMaterialAnimationColorKind, "constant_color_vec4_gate_0a_plus_selector");
    EXPECT_EQ(material.NativeMaterialAnimationColorSelector, 2);
    EXPECT_EQ(material.NativeMaterialAnimationColorComponentMask, 0x01u);
    EXPECT_FLOAT_EQ(material.NativeMaterialAnimationColorSampleFrame, 900.0f);
    EXPECT_EQ(material.ConstantColors[2].R, 128u);
    EXPECT_EQ(material.ConstantColors[2].G, 6u);
    EXPECT_EQ(material.ConstantColors[2].B, 7u);
    EXPECT_EQ(material.ConstantColors[2].A, 8u);
    EXPECT_EQ(material.NativeMaterialAnimationColorValue.R, 128u);
}

TEST(Oot3dNativeAssets, AppliesRuntimeMaterialConstantColorOverrideToRenderModel) {
    auto renderModel = SyntheticCmabColorRenderModel();

    const auto appliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
        renderModel,
        5,
        { 255, 128, 64, 192 },
        "oot3d_enmag_draw_material_slot_5_color_apply_function_0x00358964");

    ASSERT_EQ(appliedCount, 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.NativeRuntimeMaterialColorOverrideApplied);
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideSlot, 5);
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideSource,
              "oot3d_enmag_draw_material_slot_5_color_apply_function_0x00358964");
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideValue.R, 255u);
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideValue.G, 128u);
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideValue.B, 64u);
    EXPECT_EQ(material.NativeRuntimeMaterialColorOverrideValue.A, 192u);
    EXPECT_EQ(material.ConstantColors[5].R, 255u);
    EXPECT_EQ(material.ConstantColors[5].G, 128u);
    EXPECT_EQ(material.ConstantColors[5].B, 64u);
    EXPECT_EQ(material.ConstantColors[5].A, 192u);

    const auto invalidAppliedCount =
        ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
            renderModel, ThreeDsRecomp::Oot3d::kOot3dCmbMaterialConstantColorCount,
            { 1, 2, 3, 4 }, "invalid_slot_probe");
    EXPECT_EQ(invalidAppliedCount, 0u);
    EXPECT_EQ(material.ConstantColors[5].R, 255u);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_runtime_material_color_override_batch_count"].get<size_t>(), 1u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_TRUE(summary["native_batches"][0]["native_runtime_material_color_override_applied"].get<bool>());
    EXPECT_EQ(summary["native_batches"][0]["native_runtime_material_color_override_slot"].get<int32_t>(), 5);
}

TEST(Oot3dNativeAssets, AppliesCmabTransformVec2CurveToRenderTextureCoordinateLane) {
    auto bytes = MinimalCmabComponentScalarTracks();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/test.zar!misc/texture_transform_vec2.cmab");
    auto targetModel = SyntheticCmabTargetModel();
    auto renderModel = SyntheticCmabTransformRenderModel();

    const auto appliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        renderModel, targetModel, { animation }, 450.0f);

    ASSERT_EQ(appliedCount, 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    auto& batch = renderModel.Batches[0];
    auto& material = batch.Material;
    ASSERT_EQ(material.TextureCoords.size(), 1u);
    const auto& coord = material.TextureCoords[0];
    EXPECT_TRUE(material.NativeMaterialAnimationApplied);
    EXPECT_FALSE(material.NativeMaterialAnimationTextureApplied);
    EXPECT_FALSE(material.NativeMaterialAnimationColorApplied);
    EXPECT_TRUE(material.NativeMaterialAnimationTextureTransformApplied);
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformKind,
              "transform_vec2_gate_1_plus_selector");
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformSelector, 0);
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformComponentMask, 0x03u);
    EXPECT_FLOAT_EQ(material.NativeMaterialAnimationTextureTransformSampleFrame, 450.0f);
    EXPECT_TRUE(coord.NativeMaterialAnimationApplied);
    EXPECT_EQ(coord.NativeMaterialAnimationKind, "transform_vec2_gate_1_plus_selector");
    EXPECT_EQ(coord.NativeMaterialAnimationComponentMask, 0x03u);
    EXPECT_FLOAT_EQ(coord.Rotation, 0.0f);
    EXPECT_FLOAT_EQ(coord.Translation.X, 1.5f);
    EXPECT_FLOAT_EQ(coord.Translation.Y, 1.0f);
    ASSERT_EQ(batch.Vertices.size(), 1u);
    EXPECT_TRUE(batch.Vertices[0].NativeSourceUv0Available);
    EXPECT_NEAR(batch.Vertices[0].Uv0.X, -0.5f, 1.0e-5f);
    EXPECT_NEAR(batch.Vertices[0].Uv0.Y, -1.0f, 1.0e-5f);

    const auto summary = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelSummaryToJson(renderModel);
    EXPECT_EQ(summary["native_material_animation_applied_batch_count"].get<size_t>(), 1u);
    EXPECT_EQ(summary["native_material_animation_texture_transform_applied_batch_count"].get<size_t>(),
              1u);
    ASSERT_EQ(summary["native_batches"].size(), 1u);
    EXPECT_TRUE(summary["native_batches"][0]["native_material_animation_texture_transform_applied"]
                    .get<bool>());
}

TEST(Oot3dNativeAssets, AppliesCmabTransformScalarCurveToRenderTextureCoordinateLane) {
    auto bytes = MinimalCmabTransformScalarTrack();
    auto animation = ThreeDsRecomp::Oot3d::ParseCmabMaterialAnimationBytes(
        bytes, "actor/test.zar!misc/texture_transform_scalar.cmab");
    auto targetModel = SyntheticCmabTargetModel();
    auto renderModel = SyntheticCmabTransformRenderModel();

    const auto appliedCount = ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
        renderModel, targetModel, { animation }, 1200.0f);

    ASSERT_EQ(appliedCount, 1u);
    ASSERT_EQ(renderModel.Batches.size(), 1u);
    const auto& batch = renderModel.Batches[0];
    const auto& material = batch.Material;
    ASSERT_EQ(material.TextureCoords.size(), 1u);
    const auto& coord = material.TextureCoords[0];
    EXPECT_TRUE(material.NativeMaterialAnimationTextureTransformApplied);
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformKind,
              "transform_scalar_gate_4_plus_selector");
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformSelector, 0);
    EXPECT_EQ(material.NativeMaterialAnimationTextureTransformComponentMask, 0x01u);
    EXPECT_FLOAT_EQ(material.NativeMaterialAnimationTextureTransformSampleFrame, 1200.0f);
    EXPECT_FLOAT_EQ(coord.Rotation, 0.5f);
    EXPECT_FLOAT_EQ(coord.Translation.X, 0.0f);
    EXPECT_FLOAT_EQ(coord.Translation.Y, 0.0f);
    ASSERT_EQ(batch.Vertices.size(), 1u);
    EXPECT_NEAR(batch.Vertices[0].Uv0.X, 0.5f + 0.5f * std::cos(0.5f) + 0.5f * std::sin(0.5f),
                1.0e-5f);
    EXPECT_NEAR(batch.Vertices[0].Uv0.Y, 0.5f + 0.5f * std::sin(0.5f) - 0.5f * std::cos(0.5f),
                1.0e-5f);
}

TEST(Oot3dNativeAssets, ParsesNativeLightSettingsTransitionTableFromCodeBinBytes) {
    std::vector<uint8_t> codeBin(0x40, 0);
    const uint32_t codeBase = 0x00100000;
    const uint32_t tableAddress = codeBase + 0x10;
    const std::vector<uint8_t> tableBytes = {
        0x00, 0x00, 0xAC, 0x2A, 0x03, 0x03,
        0xAC, 0x2A, 0x00, 0x40, 0x03, 0x00,
    };
    std::copy(tableBytes.begin(), tableBytes.end(), codeBin.begin() + 0x10);

    const auto modes = ThreeDsRecomp::Oot3d::ParseNativeLightSettingsTransitionTableFromCodeBinBytes(
        codeBin, codeBase, tableAddress, 1, 2, 0x36, 6);
    ASSERT_EQ(modes.size(), 1u);
    EXPECT_EQ(modes[0].ModeIndex, 0u);
    ASSERT_EQ(modes[0].Entries.size(), 2u);
    EXPECT_EQ(modes[0].Entries[0].StartAngle, 0x0000u);
    EXPECT_EQ(modes[0].Entries[0].EndAngle, 0x2AACu);
    EXPECT_EQ(modes[0].Entries[0].FromLightSettingIndex, 3u);
    EXPECT_EQ(modes[0].Entries[0].ToLightSettingIndex, 3u);
    EXPECT_EQ(modes[0].Entries[1].StartAngle, 0x2AACu);
    EXPECT_EQ(modes[0].Entries[1].EndAngle, 0x4000u);
    EXPECT_EQ(modes[0].Entries[1].FromLightSettingIndex, 3u);
    EXPECT_EQ(modes[0].Entries[1].ToLightSettingIndex, 0u);
}

TEST(Oot3dNativeAssets, ParsesNativeActorProfileFromCodeBinBytes) {
    constexpr uint32_t codeBase = 0x00100000;
    constexpr uint32_t tableLiteralAddress = 0x00373B64;
    constexpr uint16_t actorId = 0x00E5;
    constexpr uint32_t tableAddress = codeBase + 0x1000;
    constexpr uint32_t profileAddress = codeBase + 0x4000;
    std::vector<uint8_t> codeBin(static_cast<size_t>(0x0038BBB8 - codeBase) + 0x24, 0);
    const auto writeU16 = [&](size_t offset, uint16_t value) {
        codeBin[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        codeBin[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    };
    const auto writeU32 = [&](size_t offset, uint32_t value) {
        codeBin[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        codeBin[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        codeBin[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        codeBin[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    };

    writeU32(tableLiteralAddress - codeBase, tableAddress);
    const size_t overlayEntryOffset =
        static_cast<size_t>(tableAddress - codeBase) + static_cast<size_t>(actorId) * 0x20;
    writeU32(overlayEntryOffset + 0x14, profileAddress);
    const size_t profileOffset = profileAddress - codeBase;
    writeU16(profileOffset + 0x00, actorId);
    codeBin[profileOffset + 0x02] = 6;
    writeU32(profileOffset + 0x04, 0x40400000);
    writeU16(profileOffset + 0x08, 0x017A);
    writeU32(profileOffset + 0x0C, 0x01B4);
    writeU32(profileOffset + 0x10, 0x0037B828);
    writeU32(profileOffset + 0x14, 0x0037BA18);
    writeU32(profileOffset + 0x18, 0x0038BBB8);
    writeU32(profileOffset + 0x1C, 0x0038BA90);

    const size_t initOffset = 0x0037B828 - codeBase;
    writeU32(initOffset + 0x1C, 0xE3A03000);
    writeU32(initOffset + 0x20, 0xE3A02001);
    writeU32(initOffset + 0x5C, 0xEDDF8A58);
    const float defaultScale = 0.1f;
    uint32_t defaultScaleRaw = 0;
    std::memcpy(&defaultScaleRaw, &defaultScale, sizeof(defaultScaleRaw));
    writeU32(initOffset + 0x1C4, defaultScaleRaw);
    const uint32_t paramScaleTableAddress = codeBase + 0x5000;
    writeU32(initOffset + 0x1C8, paramScaleTableAddress);
    const float paramScaleMultiplier = 0.0001f;
    uint32_t paramScaleMultiplierRaw = 0;
    std::memcpy(&paramScaleMultiplierRaw, &paramScaleMultiplier, sizeof(paramScaleMultiplierRaw));
    writeU32(initOffset + 0x1CC, paramScaleMultiplierRaw);
    for (size_t index = 0; index < 5; ++index) {
        constexpr std::array<uint16_t, 5> paramScales = { 0, 0, 70, 210, 300 };
        writeU16(static_cast<size_t>(paramScaleTableAddress - codeBase) + index * 2, paramScales[index]);
    }
    writeU32(static_cast<size_t>(0x0038BBB8 - codeBase) + 0x20, 0xE2411020);

    const auto profile = ThreeDsRecomp::Oot3d::ParseNativeActorProfileFromCodeBinBytes(codeBin, actorId);
    EXPECT_TRUE(profile.Valid);
    EXPECT_EQ(profile.ActorId, actorId);
    EXPECT_EQ(profile.Category, 6u);
    EXPECT_EQ(profile.Flags, 0x40400000u);
    EXPECT_EQ(profile.ObjectId, 0x017Au);
    EXPECT_EQ(profile.InstanceSize, 0x01B4u);
    EXPECT_EQ(profile.InitFunctionAddress, 0x0037B828u);
    EXPECT_EQ(profile.DestroyFunctionAddress, 0x0037BA18u);
    EXPECT_EQ(profile.UpdateFunctionAddress, 0x0038BBB8u);
    EXPECT_EQ(profile.DrawFunctionAddress, 0x0038BA90u);
    EXPECT_EQ(profile.OverlayTableAddress, tableAddress);
    EXPECT_EQ(profile.ProfileAddress, profileAddress);

    const auto behavior = ThreeDsRecomp::Oot3d::ParseNativeActorVisualBehaviorFromCodeBinBytes(codeBin, profile);
    EXPECT_TRUE(behavior.Valid);
    EXPECT_EQ(behavior.FieryCmbTypeLocalIndex, 0u);
    EXPECT_EQ(behavior.NormalCmbTypeLocalIndex, 1u);
    EXPECT_FLOAT_EQ(behavior.DefaultScale, 0.1f);
    EXPECT_FLOAT_EQ(behavior.ParamScales[2], 0.007f);
    EXPECT_FLOAT_EQ(behavior.ParamScales[3], 0.021f);
    EXPECT_FLOAT_EQ(behavior.ParamScales[4], 0.03f);
    EXPECT_EQ(behavior.RotationYStepS16PerTick, -0x20);
}

TEST(Oot3dNativeAssets, SubmitsNativeKankyoPrimitiveBackendInputWithDecodedCtxbTexture) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene renderScene;
    auto& input = renderScene.EnvironmentBackground.NativeKankyoLensEffect.PrimitiveBackendInput;
    input.Available = true;
    input.PrimitivePacketBridgeResolved = true;
    input.AttributeMaskPacketResolved = true;
    input.QuadBatchLanePlanResolved = true;
    input.TextureInputResolved = true;
    input.DecodedTextureInputResolved = true;
    input.TerminalTextureInputResolved = true;
    input.TerminalDecodedTextureInputResolved = true;
    input.GeometryInputResolved = true;
    input.ColorInputResolved = true;
    input.Runtime28CParticleBatchContractResolved = true;
    input.Runtime28CEnqueueDispatchResolved = true;
    input.Runtime28CPositionProducerResolved = true;
    input.Runtime28CPositionRuntimeInputResolved = false;
    input.Runtime28CPositionRuntimeInputBlockedReason =
        "native lens position producer requires play+0x5bb4 view/projection matrix fields "
        "consumed by FUN_00368cc0 before exact runtime centers can be materialized";
    input.Runtime28CVisibleBatchInputsResolved = true;
    input.Runtime28CQuadLaneMaterialized = true;
    input.ReadyForBackendInput = true;
    input.SourceKind = "oot3d_kankyo_effect_primitive_backend_input_plan";
    input.TextureName = "tex/cloud_lensflare.ctxb";
    input.TerminalTextureName = "tex/cloud_sun.ctxb";
    input.ColorPassPrimitiveValue = 3;
    input.AlphaPassPrimitiveValue = 2;
    input.QuadBatchVisibleElementCount = 12;
    input.QuadBatchNativeVerticesPerQuad = 6;
    input.QuadBatchExpandedVertexCount = 72;
    input.Runtime28CQuadVertexCount = 4;
    input.Runtime28CDrawCountPerVisibleBatch = 6;
    input.Runtime28CPositionRecordStrideBytes = 0x0C;
    input.Runtime28CMatrixRecordStrideBytes = 0x30;
    input.Runtime28CLocalVectorRecordStrideBytes = 0x0C;
    input.Runtime28CColorRecordStrideBytes = 0x10;
    input.Runtime28CTexcoordRecordStrideBytes = 0x08;
    input.Runtime28CBatchCapacityOffset = 0x1F8;
    input.Runtime28CLocalVectorArrayPointerOffset = 0x1EC;
    input.Runtime28CQuadLaneVertexInputCount = 48;
    input.Runtime28CDrawCount = 72;
    input.Runtime28CQueuedElementCount = 12;
    input.Runtime28CSpecialSubmitElementCount = 1;
    input.Runtime28CSpecialSubmitStartElementIndex = 0x0B;
    input.Runtime28CSpecialSubmitEndElementIndex = 0x0C;
    input.Runtime28CPrimarySubmitQueueIndexBase = 0x12;
    input.Runtime28CTerminalSubmitQueueIndexBase = 0x14;
    input.PrimaryBatchLastElementIndex = 0x0B;
    input.TerminalElementIndex = 0x0C;
    input.TerminalNativeVertexCount = 4;
    input.TerminalSubmitElementCount = 1;
    input.TerminalElementInputResolved = true;
    input.TerminalElementInput = {
        12,
        12,
        112.0f,
        122.0f,
        0.854592f,
        -0.05f,
        { 255, 255, 160, 128 },
    };
    input.OverlayPrimitiveVertexCount = 9;
    input.Texture = SyntheticDecodedCtxbRenderTexture(input.TextureName);
    input.TerminalTexture = SyntheticDecodedCtxbRenderTexture(input.TerminalTextureName);
    for (uint32_t batch = 0; batch < input.QuadBatchVisibleElementCount; ++batch) {
        input.Runtime28CVisibleBatches.push_back({
            batch,
            batch,
            10.0f + static_cast<float>(batch),
            20.0f + static_cast<float>(batch),
            0.1f,
            -0.1f,
        });
        for (uint32_t corner = 0; corner < input.Runtime28CQuadVertexCount; ++corner) {
            input.Runtime28CQuadLaneVertices.push_back({
                batch,
                batch,
                corner,
                batch * input.Runtime28CPositionRecordStrideBytes,
                batch * input.Runtime28CMatrixRecordStrideBytes,
                (batch * input.Runtime28CQuadVertexCount + corner) *
                    input.Runtime28CColorRecordStrideBytes,
                batch * input.Runtime28CTexcoordRecordStrideBytes,
                0x260 + corner * input.Runtime28CTexcoordRecordStrideBytes,
                0x264 + corner * input.Runtime28CTexcoordRecordStrideBytes,
            });
        }
    }
    for (uint32_t i = 0; i < input.OverlayPrimitiveVertexCount; ++i) {
        input.OverlayVertices.push_back({
            i,
            (i % 3) == 0 ? 2.3333333f : 0.0f,
            (i / 3) == 0 ? 1.0f : 0.0f,
            { 255, 255, 160, 128 },
        });
    }

    ThreeDsRecomp::Oot3d::Oot3dNativeRecordingRenderBackend backend;
    const auto result = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(renderScene, backend);

    EXPECT_TRUE(result.IsValid);
    EXPECT_EQ(result.EffectPrimitiveSubmitCount, 1u);
    EXPECT_EQ(result.EffectPrimitiveTextureUploadCount, 1u);
    EXPECT_EQ(result.EffectPrimitiveMissingTextureCount, 0u);
    EXPECT_EQ(result.EffectPrimitiveOverlayVertexInputCount, 9u);
    EXPECT_EQ(result.EffectPrimitiveExpectedExpandedVertexCount, 72u);
    EXPECT_EQ(result.EffectPrimitiveRuntime28CQuadLaneVertexInputCount, 48u);
    EXPECT_EQ(result.EffectPrimitiveRuntime28CDrawCount, 72u);
    ASSERT_EQ(backend.UploadedTextures().size(), 1u);
    EXPECT_EQ(backend.UploadedTextures()[0].ModelName, "oot3d_kankyo_primitive");
    EXPECT_EQ(backend.UploadedTextures()[0].Width, 2u);
    EXPECT_EQ(backend.UploadedTextures()[0].Height, 2u);
    ASSERT_EQ(backend.SubmittedKankyoPrimitives().size(), 1u);
    const auto& primitive = backend.SubmittedKankyoPrimitives()[0];
    EXPECT_EQ(primitive.TextureName, "tex/cloud_lensflare.ctxb");
    EXPECT_NE(primitive.TextureHandle, ThreeDsRecomp::Oot3d::kInvalidOot3dNativeTextureHandle);
    EXPECT_EQ(primitive.TerminalTextureName, "tex/cloud_sun.ctxb");
    EXPECT_EQ(primitive.TerminalNativeVertexCount, 4u);
    EXPECT_EQ(primitive.TerminalSubmitElementCount, 1u);
    EXPECT_TRUE(primitive.TerminalElementInputResolved);
    EXPECT_EQ(primitive.ColorPassPrimitiveValue, 3u);
    EXPECT_EQ(primitive.AlphaPassPrimitiveValue, 2u);
    EXPECT_EQ(primitive.QuadBatchVisibleElementCount, 12u);
    EXPECT_EQ(primitive.QuadBatchExpandedVertexCount, 72u);
    EXPECT_EQ(primitive.Runtime28CQuadLaneVertexInputCount, 48u);
    EXPECT_EQ(primitive.Runtime28CDrawCount, 72u);
    EXPECT_TRUE(primitive.Runtime28CQuadLaneMaterialized);
    EXPECT_EQ(primitive.OverlayPrimitiveVertexCount, 9u);
    EXPECT_EQ(primitive.OverlayVertexInputCount, 9u);
    EXPECT_TRUE(primitive.DecodedTextureInputResolved);
}

TEST(Oot3dNativeAssets, SubmitsCmbModelsUsingNativeGlobalMeshPassOrder) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    const auto makeBatch = [](uint32_t meshIndex) {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
        batch.MeshIndex = meshIndex;
        batch.Vertices.resize(3);
        return batch;
    };
    const auto makeModel = [&](std::string name, uint32_t splitIndex,
                               std::initializer_list<uint32_t> meshIndices) {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
        model.Name = std::move(name);
        model.SubmitQueue = ThreeDsRecomp::Oot3d::Oot3dNativeSubmitQueue::Small;
        model.NativeCmbMeshPassSplitIndexDecoded = true;
        model.NativeCmbMeshPassSplitIndex = splitIndex;
        for (const auto meshIndex : meshIndices) {
            model.Batches.push_back(makeBatch(meshIndex));
        }
        return model;
    };

    scene.ActorVisuals.push_back(makeModel("flame", 0, { 0 }));
    scene.ActorVisuals.push_back(makeModel("main", 17, { 0, 17 }));
    scene.ActorVisuals.push_back(makeModel("copyright", 0, { 0 }));

    ThreeDsRecomp::Oot3d::Oot3dNativeRecordingRenderBackend backend;
    const auto result = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(scene, backend);

    EXPECT_TRUE(result.IsValid);
    EXPECT_EQ(result.DrawCallCount, 4u);
    ASSERT_EQ(backend.SubmittedBatches().size(), 4u);
    EXPECT_EQ(backend.SubmittedBatches()[0].ModelName, "main");
    EXPECT_EQ(backend.SubmittedBatches()[0].MeshIndex, 0u);
    EXPECT_EQ(backend.SubmittedBatches()[1].ModelName, "flame");
    EXPECT_EQ(backend.SubmittedBatches()[2].ModelName, "main");
    EXPECT_EQ(backend.SubmittedBatches()[2].MeshIndex, 17u);
    EXPECT_EQ(backend.SubmittedBatches()[3].ModelName, "copyright");
}

TEST(Oot3dNativeAssets, SubmitsEveryEmbeddedRoomCmbAsRoomGeometry) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    const auto makeRoomModel = [](std::string name) {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
        model.Name = std::move(name);
        model.Batches.emplace_back().Vertices.resize(3);
        return model;
    };
    scene.Room = makeRoomModel("room_cmb_0");
    scene.AdditionalRoomModels.push_back(makeRoomModel("room_cmb_1"));
    scene.AdditionalRoomModels.push_back(makeRoomModel("room_cmb_2"));

    ThreeDsRecomp::Oot3d::Oot3dNativeRecordingRenderBackend backend;
    const auto result = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(scene, backend);

    EXPECT_TRUE(result.IsValid);
    EXPECT_EQ(result.DrawCallCount, 3u);
    ASSERT_EQ(backend.SubmittedBatches().size(), 3u);
    EXPECT_EQ(backend.SubmittedBatches()[0].ModelName, "room_cmb_0");
    EXPECT_EQ(backend.SubmittedBatches()[1].ModelName, "room_cmb_1");
    EXPECT_EQ(backend.SubmittedBatches()[2].ModelName, "room_cmb_2");
}

TEST(Oot3dNativeAssets, KeepsProceduralKankyoModelsInNativeRoleOrderOutsideCmbPasses) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    const auto makeModel = [](std::string name,
                              ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole role,
                              bool nativeCmb) {
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
        model.Name = std::move(name);
        model.NativeKankyoRole = role;
        model.NativeCmbMeshPassSplitIndexDecoded = nativeCmb;
        model.NativeCmbMeshPassSplitIndex = 0;
        model.Batches.emplace_back().Vertices.resize(3);
        return model;
    };

    scene.EnvironmentModels.push_back(
        makeModel("sky", ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::SkyBackground, true));
    scene.MoonModels.push_back(
        makeModel("moon", ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::None, false));
    scene.EnvironmentModels.push_back(
        makeModel("sun", ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::Sun, true));

    ThreeDsRecomp::Oot3d::Oot3dNativeRecordingRenderBackend backend;
    const auto result = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(scene, backend);

    EXPECT_TRUE(result.IsValid);
    ASSERT_EQ(backend.SubmittedBatches().size(), 3u);
    EXPECT_EQ(backend.SubmittedBatches()[0].ModelName, "sky");
    EXPECT_EQ(backend.SubmittedBatches()[1].ModelName, "moon");
    EXPECT_EQ(backend.SubmittedBatches()[2].ModelName, "sun");
}

TEST(Oot3dNativeAssets, BuildsNativeKankyoRuntimeBridgeContract) {
    auto contract = ThreeDsRecomp::Oot3d::BuildNativeKankyoRuntimeBridgeContract();

    EXPECT_EQ(contract.SourceKind, "oot3d_code_bin_z_kankyo_ctxb_runtime_bridge");
    EXPECT_EQ(contract.KankyoObjectInitializerAddress, 0x0044FF38u);
    EXPECT_EQ(contract.KankyoObjectInitializerEndAddress, 0x00450B5Fu);
    ASSERT_EQ(contract.RuntimeHelpers.size(), 2u);
    EXPECT_EQ(contract.RuntimeHelpers[0].FunctionAddress, 0x00340D00u);
    EXPECT_EQ(contract.RuntimeHelpers[0].InstanceStorageSize, 0x28Cu);
    EXPECT_EQ(contract.RuntimeHelpers[1].FunctionAddress, 0x0034897Cu);
    EXPECT_EQ(contract.RuntimeHelpers[1].InstanceStorageSize, 0x1E4u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.FunctionAddress, 0x0034897Cu);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ManagerArgumentOrdinal, 1u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.DescriptorObjectArgumentOrdinal, 2u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.OptionalBackingStorageArgumentOrdinal, 3u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.BackingCallsiteScanOutputPath,
              "analysis/runtime_effect_wrapper_backings.csv");
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerStorageSize, 0x35Cu);
    EXPECT_EQ(contract.RuntimeEffectWrapper.InstanceStorageSize, 0x1E4u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.OptionalBackingStorageSize, 0x234u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerClassId, 0x2Eu);
    EXPECT_EQ(contract.RuntimeEffectWrapper.InstanceClassId, 0x2Fu);
    EXPECT_EQ(contract.RuntimeEffectWrapper.OptionalBackingStorageClassId, 0x39u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.OptionalBackingStorageDefaultInitializerAddress, 0x00347258u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerInitializerAddress, 0x002C50D4u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.InstanceInitializerAddress, 0x002C4F00u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerVtableAddress, 0x004EBE00u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerSubmitVtableSlotOffset, 0x08u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerSubmitVtableEntryAddress, 0x004EBE08u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerSubmitFunctionAddress,
              contract.PacketPrep.RenderContextConsumerAddress);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerDescriptorPointerOffset,
              contract.EffectDrawConsumer.DescriptorObjectPointerOffset);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerInstancePointerOffset, 0x354u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ContainerBackingStoragePointerOffset, 0x358u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.InstanceBackingStoragePointerOffset, 0x1DCu);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ManagerWordCopiedToBackingStorageOffset, 0u);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.ReturnValueIsInstancePointer);
    EXPECT_FALSE(contract.RuntimeEffectWrapper.ReturnValueIsContainerPointer);
    EXPECT_FALSE(contract.RuntimeEffectWrapper.ReturnValueIsPacketPrepInput);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.ContainerBackingStoragePointerIsPacketPrepInput);
    EXPECT_FALSE(contract.RuntimeEffectWrapper.WritesDrawHandlePacketBuffer);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.NullOptionalBackingAllocatesDefaultStorage);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.ExternalOptionalBackingBypassesDefaultStorageAllocation);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.OptionalBackingArgumentStoredAtContainerBackingStoragePointer);
    EXPECT_TRUE(contract.RuntimeEffectWrapper.DefaultAllocatedBackingStoredAtInstanceBackingStoragePointer);
    EXPECT_EQ(contract.RuntimeEffectWrapper.WrapperCallsiteScanTotalCount, 72u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.WrapperCallsiteScanNullBackingCount, 60u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.WrapperCallsiteScanExternalActorEffectBackingCount, 12u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingFieldOffset, 0x178u);
    ASSERT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingFunctionAddresses.size(), 10u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingFunctionAddresses.front(),
              0x0018A8E0u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingFunctionAddresses.back(),
              0x002925ACu);
    ASSERT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingCallsiteAddresses.size(), 12u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingCallsiteAddresses.front(),
              0x0018AAA0u);
    EXPECT_EQ(contract.RuntimeEffectWrapper.ExternalActorEffectBackingCallsiteAddresses.back(),
              0x00292670u);

    ASSERT_EQ(contract.Bindings.size(), 4u);
    EXPECT_EQ(contract.Bindings[0].SubresourceIdStart, 0x44u);
    EXPECT_EQ(contract.Bindings[0].DescriptorObjectOffset, 0x1E4u);
    EXPECT_EQ(contract.Bindings[0].DescriptorSlotCount, 1u);
    EXPECT_EQ(contract.Bindings[0].RuntimeInstanceOffset, 0x1E8u);
    EXPECT_EQ(contract.Bindings[2].SubresourceIdStart, 0x46u);
    EXPECT_EQ(contract.Bindings[2].SubresourceIdEnd, 0x49u);
    EXPECT_EQ(contract.Bindings[2].DescriptorObjectOffset, 0x1F4u);
    EXPECT_EQ(contract.Bindings[2].DescriptorObjectCount, 12u);
    EXPECT_EQ(contract.Bindings[2].RuntimeInstanceOffset, 0x224u);
    EXPECT_EQ(contract.Bindings[2].RuntimeInstanceStrideBytes, 4u);
    EXPECT_EQ(contract.Bindings[2].MaterializationHelperAddress, 0x00348BE4u);
    EXPECT_EQ(contract.Bindings[3].SubresourceIdStart, 0x4Au);
    EXPECT_EQ(contract.Bindings[3].SubresourceIdEnd, 0x4Bu);
    EXPECT_EQ(contract.Bindings[3].DescriptorObjectOffset, 0x270u);
    EXPECT_EQ(contract.Bindings[3].DescriptorObjectCount, 1u);
    EXPECT_EQ(contract.Bindings[3].DescriptorSlot, 0u);
    EXPECT_EQ(contract.Bindings[3].DescriptorSlotCount, 2u);
    EXPECT_EQ(contract.Bindings[3].RuntimeInstanceOffset, 0x274u);
    EXPECT_EQ(contract.Bindings[3].RuntimeHelperAddress, 0x0034897Cu);
    EXPECT_EQ(contract.Bindings[3].RuntimeCallsiteAddress, 0x00450AF4u);
    EXPECT_EQ(contract.Bindings[3].RuntimeFlagFieldOffset, 0x178u);
    EXPECT_EQ(contract.Bindings[3].RuntimeFlagOrMask, 0x10u);
    ASSERT_EQ(contract.RuntimeBindingSlots.size(), 16u);
    EXPECT_EQ(contract.RuntimeBindingSlots[0].BindingIndex, 0u);
    EXPECT_EQ(contract.RuntimeBindingSlots[0].SubresourceIdStart, 0x44u);
    EXPECT_EQ(contract.RuntimeBindingSlots[0].DescriptorObjectOffset, 0x1E4u);
    EXPECT_EQ(contract.RuntimeBindingSlots[0].RuntimeInstanceOffset, 0x1E8u);
    EXPECT_TRUE(contract.RuntimeBindingSlots[0].UsesSubmitManager);
    EXPECT_FALSE(contract.RuntimeBindingSlots[0].UsesRenderRecordScheduler);
    EXPECT_EQ(contract.RuntimeBindingSlots[1].BindingIndex, 1u);
    EXPECT_EQ(contract.RuntimeBindingSlots[1].SubresourceIdStart, 0x45u);
    EXPECT_EQ(contract.RuntimeBindingSlots[1].RuntimeInstanceOffset, 0x1F0u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].BindingIndex, 2u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].SubresourceIdStart, 0x46u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].SubresourceIdEnd, 0x49u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].InitialSubresourceId, 0x46u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].DescriptorObjectOffset, 0x1F4u);
    EXPECT_EQ(contract.RuntimeBindingSlots[2].RuntimeInstanceOffset, 0x224u);
    EXPECT_TRUE(contract.RuntimeBindingSlots[2].UsesDynamicSubresourceSelector);
    EXPECT_TRUE(contract.RuntimeBindingSlots[2].UsesSubmitManager);
    EXPECT_EQ(contract.RuntimeBindingSlots[13].SlotIndex, 11u);
    EXPECT_EQ(contract.RuntimeBindingSlots[13].DescriptorObjectOffset, 0x220u);
    EXPECT_EQ(contract.RuntimeBindingSlots[13].RuntimeInstanceOffset, 0x250u);
    EXPECT_EQ(contract.RuntimeBindingSlots[14].BindingIndex, 3u);
    EXPECT_EQ(contract.RuntimeBindingSlots[14].SubresourceIdStart, 0x4Au);
    EXPECT_EQ(contract.RuntimeBindingSlots[14].DescriptorSlot, 0u);
    EXPECT_EQ(contract.RuntimeBindingSlots[14].RuntimeInstanceOffset, 0x274u);
    EXPECT_TRUE(contract.RuntimeBindingSlots[14].SharesRuntimeInstanceAcrossDescriptorSlots);
    EXPECT_FALSE(contract.RuntimeBindingSlots[14].UsesSubmitManager);
    EXPECT_TRUE(contract.RuntimeBindingSlots[14].UsesRenderRecordScheduler);
    EXPECT_EQ(contract.RuntimeBindingSlots[15].SubresourceIdStart, 0x4Bu);
    EXPECT_EQ(contract.RuntimeBindingSlots[15].DescriptorSlot, 1u);
    EXPECT_EQ(contract.RuntimeBindingSlots[15].RuntimeInstanceOffset, 0x274u);

    EXPECT_EQ(contract.ThunderUpdate.FunctionAddress, 0x0045FEB0u);
    EXPECT_EQ(contract.ThunderUpdate.SelectorModulo, 4u);
    EXPECT_EQ(contract.ThunderUpdate.PayloadObjectOffsets[0], 0x254u);
    EXPECT_EQ(contract.ThunderUpdate.PayloadObjectOffsets[3], 0x260u);
    EXPECT_EQ(contract.ThunderUpdate.InitialPayloadObjectOffset, 0x254u);
    EXPECT_EQ(contract.ThunderUpdate.DescriptorRebindCallsiteAddress, 0x00460100u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSubmitHelperAddress, 0x00371EACu);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeTranslationOffsets[2], 0x44u);
    ASSERT_EQ(contract.ThunderUpdate.RuntimeSlots.size(), 12u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.front().SlotIndex, 0u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.front().DescriptorObjectOffset, 0x1F4u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.front().RuntimeInstanceOffset, 0x224u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.front().InitialPayloadObjectOffset, 0x254u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.back().SlotIndex, 11u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.back().DescriptorObjectOffset, 0x220u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.back().RuntimeInstanceOffset, 0x250u);
    EXPECT_EQ(contract.ThunderUpdate.RuntimeSlots.back().DescriptorSlot, 0u);
    EXPECT_TRUE(contract.ThunderUpdate.InitBindsInitialPayloadToAllSlots);
    EXPECT_TRUE(contract.ThunderUpdate.UpdateRebindsSelectedPayloadByModulo);
    EXPECT_TRUE(contract.ThunderUpdate.RuntimeSlotsSubmittedByThunderUpdate);

    EXPECT_EQ(contract.DescriptorMaterialization.FunctionAddress, 0x00348BE4u);
    EXPECT_EQ(contract.DescriptorMaterialization.DescriptorRecordCountOffset, 0x0Cu);
    EXPECT_EQ(contract.DescriptorMaterialization.DescriptorFlagsOffset, 0x1Cu);
    EXPECT_EQ(contract.DescriptorMaterialization.DefaultRecordCount, 4u);
    EXPECT_EQ(contract.DescriptorMaterialization.BufferSetCountFieldOffset, 0x128u);
    EXPECT_EQ(contract.DescriptorMaterialization.Flag0x40PayloadPointerOffset, 0x118u);

    EXPECT_EQ(contract.CtxbDescriptorBinding.FunctionAddress, 0x00348A64u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.DescriptorSlotBaseOffset, 0x13Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.DescriptorSlotStrideBytes, 0x30u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.GeneratedDescriptorWordOffsets[0], 0x13Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.GeneratedDescriptorWordOffsets[3], 0x148u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceTextureHeaderBaseOffset, 0x24u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceTextureParameterHalfwordOffset, 0x28u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourcePayloadPointerFieldOffset, 0x4Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceWidthHalfwordOffset, 0x2Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceHeightHalfwordOffset, 0x2Eu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceFormatHalfwordOffset, 0x30u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SourceDataTypeHalfwordOffset, 0x32u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.PayloadPointerDestinationOffset, 0x158u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.WidthDestinationHalfwordOffset, 0x15Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.HeightDestinationHalfwordOffset, 0x15Eu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.SlotOrdinalDestinationOffset, 0x160u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.PackedFormatDataTypeDestinationOffset, 0x164u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.FormatDataTypePackHelperAddress, 0x0030807Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerSelectHelperAddress, 0x0030835Cu);
    EXPECT_EQ(contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerDestinationOffset, 0x10u);
    EXPECT_EQ(contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerFallbackLiteralAddress, 0x00348B84u);
    EXPECT_TRUE(contract.CtxbDescriptorBinding.WritesDescriptorTextureMetadata);
    EXPECT_FALSE(contract.CtxbDescriptorBinding.WritesPacketPrepSourceSlots);
    EXPECT_FALSE(contract.CtxbDescriptorBinding.DescriptorSlotSourcePointerWritesDrawHandlePacketPrepSource);

    EXPECT_EQ(contract.DrawCommand.FunctionAddress, 0x003FD1B8u);
    EXPECT_EQ(contract.DrawCommand.AllocateCommandHelperAddress, 0x00324154u);
    EXPECT_EQ(contract.DrawCommand.AllocateCommandHelperEndAddress, 0x0032418Fu);
    EXPECT_EQ(contract.DrawCommand.DrawCommandListOffset, 0x3410u);
    EXPECT_EQ(contract.DrawCommand.CommandListOwnerFieldOffset, 0x00u);
    EXPECT_EQ(contract.DrawCommand.CommandListCountFieldOffset, 0x04u);
    EXPECT_EQ(contract.DrawCommand.CommandRecordBaseOffset, 0x08u);
    EXPECT_EQ(contract.DrawCommand.CommandRecordStrideBytes, 0x20u);
    EXPECT_EQ(contract.DrawCommand.CommandListMaxRecordCount, 0x31u);
    EXPECT_EQ(contract.DrawCommand.CommandListGuardExclusiveCount, 0x32u);
    EXPECT_EQ(contract.DrawCommand.CommandRecordListOwnerFieldOffset, 0x00u);
    EXPECT_EQ(contract.DrawCommand.CommandRecordAllocatorArgumentFieldOffset, 0x04u);
    EXPECT_EQ(contract.DrawCommand.CommandType, 6u);
    EXPECT_EQ(contract.DrawCommand.CommandTypeFieldOffset, 0x1Cu);
    EXPECT_EQ(contract.DrawCommand.OwnerContextFieldOffset, 0x08u);
    EXPECT_EQ(contract.DrawCommand.RuntimeBlockFieldOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawCommand.SchedulerContextFieldOffset, 0x10u);
    EXPECT_TRUE(contract.DrawCommand.AllocationFailureReturnsNull);

    EXPECT_EQ(contract.EffectDrawConsumer.DrawFunctionAddress, 0x003FB5ECu);
    EXPECT_EQ(contract.EffectDrawConsumer.SetupFunctionAddress, 0x003FB9ACu);
    EXPECT_EQ(contract.EffectDrawConsumer.DescriptorObjectPointerOffset, 0x350u);
    EXPECT_EQ(contract.EffectDrawConsumer.RuntimeInstancePointerOffset, 0x354u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListOffset, 0x50u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer0PointerOffset, 0x00u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer1PointerOffset, 0x04u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListInitialCapacity, 8u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListCapacityOffset, 0x08u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListPrimaryCountOffset, 0x0Cu);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListDefaultCountOffset, 0x10u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListPrimaryRecordBaseOffset, 0x14u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListDefaultRecordBaseOffset, 0xD4u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListRecordStrideBytes, 0x10u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListDefaultRecordIntensityWord, 0x3F000000u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer0ResolverAddress, 0x00313AD8u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer1ResolverAddress, 0x00313AC8u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListActiveIndexOffset, 0x124u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer0TableOffset, 0x1A0u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListBuffer1TableOffset, 0x1A8u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListAppendDefaultAddress, 0x00313650u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListAppendRecordAddress, 0x00313698u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListCommandClassifyAddress, 0x003136E4u);
    EXPECT_EQ(contract.EffectDrawConsumer.LightListFinalizeAddress, 0x00313864u);
    EXPECT_EQ(contract.EffectDrawConsumer.RuntimeUvTransformBaseOffset, 0x110u);
    EXPECT_EQ(contract.EffectDrawConsumer.RuntimeUvTransformStrideBytes, 0x30u);
    EXPECT_EQ(contract.EffectDrawConsumer.NativeLightSlotCount, 6u);
    EXPECT_EQ(contract.EffectDrawConsumer.DescriptorFlag0x20ClearDrawMode, 5u);
    EXPECT_EQ(contract.EffectDrawConsumer.DescriptorFlag0x20SetDrawMode, 4u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketFunctionAddress, 0x00313444u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskFunctionAddress, 0x003135ACu);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawModeResolverFunctionAddress, 0x0047FF34u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawCommandCommitAddress, 0x003084DCu);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketWordCount, 0x24u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawRenderStateIndexBaseOffset, 0x24u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawEffectIndexBaseStackValue, 0u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawIndexElementTypeLiteral, 0x1403u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawUnsignedShortIndexBaseHighBitMask, 0x80000000u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawMode5ResolvedPrimitiveValue, 1u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawMode6ResolvedPrimitiveValue, 2u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawMode4ResolvedPrimitiveValue, 3u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawMode6010ResolvedPrimitiveValue, 3u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskHeaderWord, 0x000F02B0u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskPayloadOrMask, 0x7FFF0000u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketLiteralWords[0], 0x00020229u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketLiteralWords[1], 0x00020253u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketLiteralWords[10], 0x000F0111u);
    EXPECT_EQ(contract.EffectDrawConsumer.PrimitiveDrawPacketLiteralWords[12], 0x000C02BAu);
    EXPECT_TRUE(contract.EffectDrawConsumer.PrimitiveDrawCountComesFromRuntimePayload);
    EXPECT_TRUE(contract.EffectDrawConsumer.PrimitiveDrawEffectStackIndexBaseIsZero);
    EXPECT_TRUE(contract.EffectDrawConsumer.PrimitiveDrawPacketBridgeResolved);
    EXPECT_TRUE(contract.EffectDrawConsumer.PrimitiveDrawAttributeMaskPacketResolved);

    EXPECT_EQ(contract.GeneralLightListEmitter.FunctionAddress, 0x00409194u);
    EXPECT_EQ(contract.GeneralLightListEmitter.FunctionEndAddress, 0x0040937Cu);
    EXPECT_EQ(contract.GeneralLightListEmitter.InitFunctionAddress, 0x004094F4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.AppendDefaultAddress,
              contract.EffectDrawConsumer.LightListAppendDefaultAddress);
    EXPECT_EQ(contract.GeneralLightListEmitter.AppendRecordAddress,
              contract.EffectDrawConsumer.LightListAppendRecordAddress);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicFloatVectorAppendAddress, 0x0040950Cu);
    EXPECT_EQ(contract.GeneralLightListEmitter.CommandClassifyAddress,
              contract.EffectDrawConsumer.LightListCommandClassifyAddress);
    EXPECT_EQ(contract.GeneralLightListEmitter.CommandParamTranslateAddress, 0x004094B4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.FinalizeAddress,
              contract.EffectDrawConsumer.LightListFinalizeAddress);
    EXPECT_EQ(contract.GeneralLightListEmitter.GenericCommandWriterAddress, 0x00307BD8u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotCount, 8u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SourceRecordStrideBytes,
              contract.ZsiLightSettingsRecord.NativeRecordSizeBytes);
    EXPECT_EQ(contract.GeneralLightListEmitter.SourcePointerFieldOffset, 0x24u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SourceCommandHalfwordOffset, 0x2Cu);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicFloatVectorOffset, 0x30u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicFloatVectorComponentCount, 4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicFloatVectorComponentStrideBytes, 4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SourceOffsetTableByteOffset, 0x28u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SourceOffsetTableWordIndexBase, 10u);
    EXPECT_EQ(contract.GeneralLightListEmitter.EnableMaskHalfwordOffset, 0x08u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicMaskHalfwordSourceOffset, 0x106u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassTableLiteralAddress, 0x00409380u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassTableAddress, 0x004E2F24u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassValues[0], 3u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassValues[2], 4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassValues[5], 2u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SlotClassValues[6], 0u);
    EXPECT_EQ(contract.GeneralLightListEmitter.PrimaryUploadRegister, 0x200u);
    EXPECT_EQ(contract.GeneralLightListEmitter.PrimaryUploadWordCount, 0x27u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SecondaryUploadRegister, 0x2BBu);
    EXPECT_EQ(contract.GeneralLightListEmitter.SecondaryUploadWordCount, 2u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicUploadRegister, 0x232u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicUploadWordCount, 4u);
    EXPECT_EQ(contract.GeneralLightListEmitter.UploadSequentialFlag, 1u);
    EXPECT_EQ(contract.GeneralLightListEmitter.UploadMask, 0xFu);
    EXPECT_EQ(contract.GeneralLightListEmitter.PrimaryUploadPayloadOffset, 0x194u);
    EXPECT_EQ(contract.GeneralLightListEmitter.SecondaryUploadPayloadOffset, 0x230u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicUploadPayloadBaseOffset, 0x238u);
    EXPECT_EQ(contract.GeneralLightListEmitter.DynamicUploadPayloadStrideBytes,
              contract.EffectDrawConsumer.LightListRecordStrideBytes);
    EXPECT_EQ(contract.GeneralLightListEmitter.FinalStateWord0Offset, 0x2F8u);
    EXPECT_EQ(contract.GeneralLightListEmitter.FinalStateWord1Offset, 0x2FCu);
    EXPECT_EQ(contract.GeneralLightListEmitter.OutputStateWord0Offset, 0x20u);
    EXPECT_EQ(contract.GeneralLightListEmitter.OutputStateWord1Offset, 0x24u);
    EXPECT_TRUE(contract.GeneralLightListEmitter.ConsumesZsiLightSettingsRecordStride);
    EXPECT_FALSE(contract.GeneralLightListEmitter.DirectlyWritesRuntimePacketPrepSource);

    EXPECT_EQ(contract.GenericCommandWriterOwnership.ScalarWriterAddress, 0x00307BD8u);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.VectorUniformWriterAddress, 0x00307C94u);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.ScalarWriterIsPacketFormatHelper);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.VectorUniformWriterIsPacketFormatHelper);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.SemanticsAttachedToCallers);
    EXPECT_NE(contract.GenericCommandWriterOwnership.OwnershipRule.find("packet/VSH-uniform"),
              std::string::npos);
    ASSERT_EQ(contract.GenericCommandWriterOwnership.Callers.size(), 4u);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[0].CallerAddress, 0x0031317Cu);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[0].OwnerFunctionAddress,
              contract.PacketPrep.FunctionAddress);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[0].OwnerContract,
              "NativeKankyoPacketPrepContract");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[0].SemanticOwner,
              "runtime_vs_light_packet_slot_upload");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[0].RegisterOrUniformIds,
              (std::vector<uint32_t>{ 0x050, 0x051, 0x052, 0x053, 0x054, 0x055, 0x056, 0x057,
                                       0x058 }));
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[0].ReachesGenericPicaWriterPath);
    EXPECT_FALSE(contract.GenericCommandWriterOwnership.Callers[0].DirectPicaWriterCall);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[0].SemanticsResolvedFromCaller);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[0].PacketFormatHelperOnly);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[1].CallerAddress, 0x00452894u);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[1].OwnerFunctionAddress,
              contract.DrawHandleSubmit.MaterialDrawDispatchAddress);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[1].OwnerContract,
              "NativeKankyoDrawHandleSubmitContract");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[1].SemanticOwner,
              "cmb_mesh_material_lane_selects_material_state_setup");
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[1].RegisterOrUniformIds.empty());
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[1].ReachesGenericPicaWriterPath);
    EXPECT_FALSE(contract.GenericCommandWriterOwnership.Callers[1].DirectPicaWriterCall);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[1].SemanticsResolvedFromCaller);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[1].PacketFormatHelperOnly);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[2].CallerAddress,
              contract.MaterialScalarEmit.DispatchAddress);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[2].OwnerContract,
              "NativePicaMaterialScalarEmitContract");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[2].SemanticOwner,
              "material_scalar_payload_registers_0x0e6_0x0e8");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[2].RegisterOrUniformIds,
              (std::vector<uint32_t>{ contract.MaterialScalarEmit.ScalarRegister0,
                                       contract.MaterialScalarEmit.ScalarRegister1 }));
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[2].DirectPicaWriterCall);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[2].SemanticsResolvedFromCaller);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[3].CallerAddress, 0x00438550u);
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[3].OwnerContract,
              "NativeFramebufferFlushRegisterEmit");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[3].SemanticOwner,
              "framebuffer_flush_register_defaults");
    EXPECT_EQ(contract.GenericCommandWriterOwnership.Callers[3].RegisterOrUniformIds,
              (std::vector<uint32_t>{ 0x111, 0x110, 0x010 }));
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[3].DirectPicaWriterCall);
    EXPECT_TRUE(contract.GenericCommandWriterOwnership.Callers[3].SemanticsResolvedFromCaller);

    EXPECT_EQ(contract.LightingRegisterEmitter.RenderContextSubmitFunctionAddress, 0x003FBBA8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RenderContextSubmitFunctionEndAddress, 0x003FBC6Bu);
    EXPECT_EQ(contract.LightingRegisterEmitter.RenderContextSubmitMinimumDrawPayloadCount, 3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeDrawPayloadCountOffset,
              contract.EffectDrawConsumer.RuntimeDrawPayloadOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter.EffectDrawSetupFunctionAddress,
              contract.EffectDrawConsumer.SetupFunctionAddress);
    EXPECT_EQ(contract.LightingRegisterEmitter.EffectDrawBuildFunctionAddress,
              contract.EffectDrawConsumer.DrawFunctionAddress);
    EXPECT_EQ(contract.LightingRegisterEmitter.GenericDirectWriterAddress, 0x00307C94u);
    EXPECT_EQ(contract.LightingRegisterEmitter.GenericSequentialWriterAddress, 0x00307BD8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.PrimarySecondaryColorEmitterAddress, 0x0031485Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.PrimarySecondaryColorRegister, 0x008u);
    EXPECT_EQ(contract.LightingRegisterEmitter.PrimarySecondaryColorWordCount, 2u);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformEmitterAddress, 0x0031432Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformPrimaryRegister, 0x00Au);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformSecondaryBaseRegister, 0x00Bu);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformPrimaryWordCount, 4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformSecondaryWordCount, 3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.UvTransformSlotStrideRegisters, 3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitFunctionAddress, 0x003F9B5Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitDescriptorPointerWordIndex, 0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitTextureObjectPointerWordIndex, 1u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightRecordTablePointerWordIndex, 2u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitColor0ByteOffset, 0xA8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitColor1ByteOffset, 0xA4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitColorComponentCount, 4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotCountOffset, 0x120u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotIndexTableOffset, 0x124u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitActiveLightSlotIndexStrideBytes, 2u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightRecordStrideBytes, 0x28u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightRecordColorOp0HalfwordOffset, 0x08u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightRecordColorOp1HalfwordOffset, 0x0Au);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightRecordDisabledColorOpValue, 0x8579u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeSubmitLightSlotCount, 6u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightFunctionAddress,
              0x003FA198u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightCountOffset, 0x08u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightUvSourceCountOffset,
              contract.LightingRegisterEmitter.RuntimeUvTransformSourceCountOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightTextureRefBaseOffset,
              0x10u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightSourceRecordBaseOffset,
              contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordBaseOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightRecordStrideBytes,
              contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordStrideBytes);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputTextureIdHalfwordOffset,
              0x450u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputSamplerWordBaseOffset,
              0x458u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputModeWordBaseOffset,
              0x468u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightOutputWordStrideBytes,
              4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideOwnerOffset,
              contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideOwnerOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideGateByteOffset,
              contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideGateByteOffset);
    EXPECT_EQ(
        contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideSourcePointerOffset,
        contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideSourcePointerOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeRecordTextureLightFirstSlotOverrideModeValue,
              4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformBuildFunctionAddress,
              0x003F9F68u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSourceBuilderAddress,
              0x003143A8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformCopyHelperAddress,
              0x00372224u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadHelperAddress,
              0x00409040u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadHelperAddress,
              contract.LightingRegisterEmitter.UvTransformEmitterAddress);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSourceCountOffset, 0x0Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordBaseOffset,
              0x58u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSourceRecordStrideBytes,
              0x18u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformOutputSlotCount, 3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformOutputSlotStrideBytes, 0x30u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformOutputWordCount, 12u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadRegister,
              contract.LightingRegisterEmitter.UvTransformPrimaryRegister);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformPrimaryUploadWordCount,
              contract.LightingRegisterEmitter.UvTransformPrimaryWordCount);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadRegisterBase,
              contract.LightingRegisterEmitter.UvTransformSecondaryBaseRegister);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformSecondaryUploadWordCount,
              contract.LightingRegisterEmitter.UvTransformSecondaryWordCount);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideOwnerOffset,
              0x10u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideGateByteOffset,
              0x1B5u);
    EXPECT_EQ(
        contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverrideSourcePointerOffset,
        0x1A8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.RuntimeUvTransformFirstSlotOverridePayloadOffset,
              0x30u);
    EXPECT_TRUE(contract.LightingRegisterEmitter.RuntimeUvTransformUploadsNativePicaVshUniforms);
    EXPECT_FALSE(contract.LightingRegisterEmitter.RuntimeUvTransformDirectlyWritesPacketPrepSource);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotEmitterAddress, 0x003146E4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotDefaultEmitterAddress, 0x0031466Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotRegisterBaseTableAddress, 0x004E2EBCu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotRegisterBases[0], 0x0C0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotRegisterBases[3], 0x0D8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotRegisterBases[5], 0x0F8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotActiveMainWordCount, 3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotActiveTailRegisterOffset, 4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotActiveTailWordCount, 1u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotDefaultPayloadTableAddress, 0x004E2ED4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotDefaultRegisterBaseTableAddress, 0x004E2EE8u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotDefaultWordCount, 5u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorEmitterAddress, 0x0031448Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorRegisterTableAddress, 0x004E2F00u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorRegisters[0], 0x0C3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorRegisters[2], 0x0D3u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorRegisters[5], 0x0FBu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSlotColorFloatToByteScaleWord, 0x437F0000u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsEmitterAddress, 0x00307E34u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsRegisterBaseTableAddress, 0x004E2F18u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsUploadRegisters[0], 0x081u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsUploadRegisters[1], 0x091u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsUploadRegisters[2], 0x099u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsPrimaryHeader, 0x809F0081u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightSettingsTerminatorHeader, 0x000F008Eu);
    EXPECT_EQ(contract.LightingRegisterEmitter.ColorOperationEmitterAddress, 0x0031429Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.ColorOperationFirstHeader, 0x000F0111u);
    EXPECT_EQ(contract.LightingRegisterEmitter.ColorOperationSecondHeader, 0x000F0110u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutInputEmitterAddress, 0x00314538u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutInputRegister, 0x059u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutInputWordCount, 1u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutConfigEmitterAddress, 0x003142DCu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutConfigRegister, 0x05Au);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutConfigWordCount, 2u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutConfigDefaultTableAddress, 0x004EA0B0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingLutConfigDefaultWord, 0x3F800000u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingEnableEmitterAddress, 0x003142F0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingEnableRegister, 0x05Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.LightingEnableWordCount, 1u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigEmitterAddress, 0x00313D6Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRegister, 0x112u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigHeader, 0x803F0112u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayloadWordCount, 4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayloadRegisters[0], 0x112u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayloadRegisters[1], 0x113u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayloadRegisters[2], 0x114u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayloadRegisters[3], 0x115u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRenderSetupCallsiteAddress,
              0x003FB8F4u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupCallsiteAddress,
              0x003FAF30u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigStateTypeOffset, 0x0Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigFlagsOffset, 0x0Eu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryEnableOffset, 0x10u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryModeOffset, 0x11u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryEnableOffset, 0x12u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryModeOffset, 0x13u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigNativeDefaultType, 0x6030u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigNativeAlternateType, 0x6051u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayload0FallbackMask, 0x0Fu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayload1FallbackMask, 0x0Fu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayload2EnableBit, 0x02u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPayload3EnableBit, 0x02u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigTypeSetterAddress,
              0x00314034u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigPrimaryStateSetterAddress,
              0x003141CCu);
    EXPECT_EQ(
        contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryDisabledStateSetterAddress,
        0x00314098u);
    EXPECT_EQ(
        contract.LightingRegisterEmitter.FragmentLightingConfigSecondaryEnabledStateSetterAddress,
        0x00314108u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigAuxScalarStateSetterAddress,
              0x00314028u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialScalarUploaderAddress,
              0x004090CCu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupAddress,
              0x003FAD68u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialPreparedStateOffset,
              0x24u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialPrimarySourceRuntimeLaneOffset,
              0x1C0u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialPrimaryDefaultTableAddress,
              0x004E056Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialPrimaryOverrideGateOffset,
              0x0Bu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialPrimaryCmbBlendGateOffset,
              0x138u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryTypeSelectorCmbOffset,
              0x04u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryTypeDisabledValue,
              0x03u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryTypeRegisterValues[0],
              0x0404u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryTypeRegisterValues[1],
              0x0405u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryTypeRegisterValues[2],
              0x0408u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryEnableCmbOffset,
              0x134u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryModeCmbOffset,
              0x135u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryParamCmbOffset,
              0x136u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryOverrideModeOffset,
              0x1BAu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialAuxByteCmbOffset,
              0x05u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigMaterialAuxHalfwordCmbOffset,
              0x06u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateInitializerAddress,
              0x00347258u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateCopyHelperAddress,
              0x00310F7Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideGateOffset,
              contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialPrimaryOverrideGateOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideGateDefaultValue,
              0u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeOffset,
              contract.LightingRegisterEmitter
                  .FragmentLightingConfigMaterialSecondaryOverrideModeOffset);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeDefaultValue,
              1u);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateOverrideGateCopiedByCopyHelper);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateSecondaryModeCopiedByCopyHelper);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateDefaultsResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateCopyHelperResolvedFromCodebin);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideWriterAddressCount,
              4u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideWriterAddresses[0],
              0x001C0310u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideWriterAddresses[1],
              0x0028D620u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideWriterAddresses[2],
              0x002D5F68u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateOverrideWriterAddresses[3],
              0x003B4308u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddressCount,
              4u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses[0],
              0x00228A34u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses[1],
              0x002D5F68u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses[2],
              0x00377D90u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses[3],
              0x003B4308u);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateWriterScanResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRuntimeStateWritersClassifiedAsDrawLocal);
    EXPECT_FALSE(contract.LightingRegisterEmitter
                     .FragmentLightingConfigRuntimeStateActiveProducerResolvedFromWriterScan);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigGameplayDrawAddress,
              0x002E25F0u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigGameplayDrawDispatcherAddress,
              0x00461904u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigGameplayDrawDispatcherCallsiteAddress,
              0x002E2CDCu);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntrySubmitAddress,
              0x002D5F68u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntrySubmitCallsiteCount,
              2u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntrySubmitCallsiteAddresses[0],
              0x00461BB4u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntrySubmitCallsiteAddresses[1],
              0x00461D28u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryFlagsOffset,
              0x04u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryRenderContextPointerOffset,
              0x178u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigDrawEntryCallbackOffset,
              0x140u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryVisibilityStateOffset,
              0x120u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntrySubmittedByteOffset,
              0x121u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryFadeCounterOffset,
              0x19Eu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryFadeLimitOffset,
              0x19Fu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryOverrideGateFlagMask,
              0x80000000u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigDrawEntryOverrideGateForceFullFlagMask,
              0x00000020u);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigDrawEntrySubmitRouteResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigDrawEntryOverrideGateRuleResolvedFromCodebin);
    EXPECT_FALSE(contract.LightingRegisterEmitter
                     .FragmentLightingConfigDrawEntryRoutePromotesActiveMaterialOverride);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigSubmitManagerVtableAddress,
              0x004EBD78u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigSubmitManagerMaterialConfigSlotOffset,
              0x28u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigSubmitManagerMaterialConfigAddress,
              0x003FAC2Cu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigSubmitManagerMaterialStateSlotOffset,
              0x44u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigSubmitManagerMaterialStateAddress,
              contract.LightingRegisterEmitter.FragmentLightingConfigMaterialSetupAddress);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigSubmitManagerMaterialRouteResolvedFromCodebin);
    EXPECT_FALSE(contract.LightingRegisterEmitter
                     .FragmentLightingConfigSubmitManagerMaterialRouteResolvesActiveOverrideGate);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextSubmitAddress,
              0x003FBBA8u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigRenderContextPreparedStateOffset,
              0x18u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextAuxFloatOffset,
              0x1E0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextAuxZeroWord,
              0u);
    EXPECT_EQ(contract.LightingRegisterEmitter.FragmentLightingConfigRenderContextNativeType,
              0x6030u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerAddress,
              0x00313CECu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateBaseResetAddress,
              0x00313D58u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerNativeTypeLiteralAddress,
              0x00313D54u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerNativeType,
              0x6030u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultFlags,
              0x000Fu);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultPrimaryEnable,
              0u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultPrimaryMode,
              0u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultSecondaryEnable,
              1u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultSecondaryMode,
              1u);
    EXPECT_EQ(contract.LightingRegisterEmitter
                  .FragmentLightingConfigPreparedStateInitializerDefaultAuxByte,
              0u);
    EXPECT_TRUE(
        contract.LightingRegisterEmitter.FragmentLightingConfigPayloadLogicResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigPreparedStateSettersResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigMaterialSetupSourceResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigRenderContextSourceResolvedFromCodebin);
    EXPECT_TRUE(contract.LightingRegisterEmitter
                    .FragmentLightingConfigPreparedStateInitializerResolvedFromCodebin);
    EXPECT_TRUE(
        contract.LightingRegisterEmitter.FragmentLightingConfigPreparedFlagsOriginResolved);
    EXPECT_FALSE(
        contract.LightingRegisterEmitter.FragmentLightingConfigPreparedStateOriginResolved);
    EXPECT_EQ(contract.LightingRegisterEmitter.AlphaTestEmitterAddress, 0x00313EBCu);
    EXPECT_EQ(contract.LightingRegisterEmitter.AlphaTestRegister, 0x104u);
    EXPECT_EQ(contract.LightingRegisterEmitter.AlphaTestHeader, 0x000F0104u);
    EXPECT_TRUE(contract.LightingRegisterEmitter.EmitsRuntimePicaLightingRegisters);
    EXPECT_TRUE(contract.LightingRegisterEmitter.UsesNativeZsiLightSettingsRecords);
    EXPECT_FALSE(contract.LightingRegisterEmitter.FragopShadowRegisterResolvedInLightingEmitterPath);

    EXPECT_EQ(contract.PacketPrep.FunctionAddress, 0x003130A4u);
    EXPECT_EQ(contract.PacketPrep.RenderContextConsumerAddress, 0x003FBBA8u);
    EXPECT_EQ(contract.PacketPrep.RenderContextConsumerCallsiteAddress, 0x003FBBD8u);
    EXPECT_EQ(contract.PacketPrep.RenderContextPacketBufferOffset, 0x358u);
    EXPECT_EQ(contract.PacketPrep.RenderContextTransformOffset, 0x18u);
    EXPECT_EQ(contract.PacketPrep.DrawHandleConsumerAddress, 0x0030F4D0u);
    EXPECT_EQ(contract.PacketPrep.DrawHandleConsumerCallsiteAddress, 0x0030F524u);
    EXPECT_EQ(contract.PacketPrep.DrawHandlePacketBufferOffset, 0x10u);
    EXPECT_FALSE(contract.PacketPrep.RuntimeSourceVectorCommittedBlockIsDirectInput);
    EXPECT_EQ(contract.PacketPrep.SlotCount, 3u);
    EXPECT_EQ(contract.PacketPrep.SlotStrideBytes, 0x60u);
    EXPECT_EQ(contract.PacketPrep.SourcePayload0Offset, 0x88u);
    EXPECT_EQ(contract.PacketPrep.SourceVectorOffsets[0], 0xC8u);
    EXPECT_EQ(contract.PacketPrep.SourceVectorOffsets[2], 0xD0u);
    EXPECT_EQ(contract.PacketPrep.PreparedVectorOffsets[2], 0xE0u);
    EXPECT_EQ(contract.PacketPrep.PreparedIntensityOffset, 0xE4u);
    EXPECT_EQ(contract.PacketPrep.EnabledFloatWord, 0x3F800000u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadHelperAddress, 0x00466EA0u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadRegisterBaseTableAddress, 0x004E2EA4u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadRegisters[0], 0x051u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadRegisters[1], 0x054u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadRegisters[2], 0x057u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadPairedRegisterOffset, 1u);
    EXPECT_EQ(contract.PacketPrep.EnabledUploadWordCount, 1u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadHelperAddress, 0x00466F00u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadRegisterTableAddress, 0x004E2EB0u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadRegisters[0], 0x050u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadRegisters[1], 0x053u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadRegisters[2], 0x056u);
    EXPECT_EQ(contract.PacketPrep.VectorUploadWordCount, 1u);
    EXPECT_EQ(contract.PacketPrep.PrepViewMatrixAccessorAddress, 0x00313644u);
    EXPECT_EQ(contract.PacketPrep.PrepViewMatrixPointerLiteralAddress, 0x0031364Cu);
    EXPECT_EQ(contract.PacketPrep.PrepViewMatrixRuntimeAddress, 0x005B5018u);
    EXPECT_EQ(contract.PacketPrep.PrepStaticMatrixAccessorAddress, 0x00466F3Cu);
    EXPECT_EQ(contract.PacketPrep.PrepStaticMatrixPointerLiteralAddress, 0x00466F44u);
    EXPECT_EQ(contract.PacketPrep.PrepStaticMatrixRuntimeAddress, 0x005B5058u);
    EXPECT_TRUE(contract.PacketPrep.UploadHelpersUseNativePicaRegisterMaps);

    EXPECT_EQ(contract.RuntimeSourceVector.StaticUpdateAddress, 0x003F95C8u);
    EXPECT_EQ(contract.RuntimeSourceVector.DynamicSubmitCallbackAddress,
              contract.SubmitManager.Runtime1E4SubmitCallbackAddress);
    EXPECT_EQ(contract.RuntimeSourceVector.RuntimeFlagsOffset, contract.EffectDrawConsumer.RuntimeFlagsOffset);
    EXPECT_EQ(contract.RuntimeSourceVector.DynamicTransformUpdateSkipFlagMask, 0x01u);
    EXPECT_EQ(contract.RuntimeSourceVector.StaticUpdateSkipFlagMask, 0x02u);
    EXPECT_EQ(contract.RuntimeSourceVector.CommittedCopySkipFlagMask, 0x08u);
    EXPECT_EQ(contract.RuntimeSourceVector.DescriptorPointerOffset, 0x04u);
    EXPECT_EQ(contract.RuntimeSourceVector.DescriptorTypeOffset, 0x18u);
    EXPECT_EQ(contract.RuntimeSourceVector.DescriptorTypeDirectMatrixValue, 0u);
    EXPECT_EQ(contract.RuntimeSourceVector.DescriptorTypeDerivedMatrixValue, 1u);
    EXPECT_EQ(contract.RuntimeSourceVector.BaseVectorXOffset, 0x48u);
    EXPECT_EQ(contract.RuntimeSourceVector.BaseVectorYOffset, 0x4Cu);
    EXPECT_EQ(contract.RuntimeSourceVector.BaseVectorZOffset, 0x50u);
    EXPECT_EQ(contract.RuntimeSourceVector.StaticTransformBlockOffset, 0x54u);
    EXPECT_EQ(contract.RuntimeSourceVector.DynamicTransformBlockOffset, 0x84u);
    EXPECT_EQ(contract.RuntimeSourceVector.ParentTransformBlockOffset, 0x3Cu);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingBlockOffset, 0xB4u);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingVectorSeedOffsets[0], 0xB4u);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingVectorSeedOffsets[1], 0xC8u);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingVectorSeedOffsets[2], 0xDCu);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingPacketSourceVectorOffsets, contract.PacketPrep.SourceVectorOffsets);
    EXPECT_EQ(contract.RuntimeSourceVector.WorkingBlockWordCount, 12u);
    EXPECT_EQ(contract.RuntimeSourceVector.CommittedBlockOffset, contract.EffectDrawConsumer.RuntimeMatrixOffset);
    EXPECT_EQ(contract.RuntimeSourceVector.CommittedBlockWordCount, 12u);
    EXPECT_EQ(contract.RuntimeSourceVector.MatrixComposeHelperAddress, 0x0036C174u);
    EXPECT_EQ(contract.RuntimeSourceVector.MatrixApplyHelperAddress, 0x0032C78Cu);
    EXPECT_EQ(contract.RuntimeSourceVector.VectorPrepHelperAddress, 0x00372224u);
    EXPECT_EQ(contract.RuntimeSourceVector.PreparedIntensitySourceOffset, 0x2Cu);
    EXPECT_EQ(contract.RuntimeSourceVector.PreparedIntensityDestinationOffset, contract.PacketPrep.PreparedIntensityOffset);

    EXPECT_EQ(contract.RuntimeLightPacketPack.FunctionAddress, 0x003FA5D0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FallbackFunctionAddress, 0x003FA34Cu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.PacketBufferPointerOffset, 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeDescriptorPointerOffset, 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorEnabledByteOffset, 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SlotCount, contract.PacketPrep.SlotCount);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceSlotStrideBytes, contract.PacketPrep.SlotStrideBytes);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourcePreparedVectorBaseOffset,
              contract.PacketPrep.PreparedVectorOffsets[0]);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourcePreparedIntensityOffset,
              contract.PacketPrep.PreparedIntensityOffset);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RequiredPreparedIntensityWord, contract.PacketPrep.EnabledFloatWord);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadGroupCount, 4u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadComponentCount, 3u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadStrideBytes, 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadOffsets[0], contract.PacketPrep.SourcePayload0Offset);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadOffsets[1], contract.PacketPrep.SourcePayload1Offset);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadOffsets[2], 0xA8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.SourceColorPayloadOffsets[3], 0xB8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPrimaryColorScaleOffsets[0], 0xA8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPrimaryColorScaleOffsets[3], 0xABu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload1ScaleOffsets[0], 0xA4u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload1ScaleOffsets[2], 0xA6u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload2ScaleOffsets[0], 0xACu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload2ScaleOffsets[2], 0xAEu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload3ScaleOffsets[0], 0xB0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorPayload3ScaleOffsets[2], 0xB2u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DescriptorByteToFloatScaleWord, 0x3B808081u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.ColorFloatToByteScaleWord, 0x437F0000u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.ColorRoundBiasWord, 0x3F000000u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.ClampMinWord, 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.ClampMaxWord, 0x3F800000u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputBaseOffset, 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputRecordStrideBytes, 0x2Cu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputColorByteOffset, 0x08u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputColorByteCount, 12u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputDirectionPackedWord0Offset, 0x14u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputDirectionPackedWord1Offset, 0x18u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputEnabledByteOffset, 0x1Cu);
    EXPECT_TRUE(contract.RuntimeLightPacketPack.RuntimeOutputNegatesPreparedVectorBeforePack);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordColorByteOffsets[0], 0x04u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordColorByteOffsets[5], 0x09u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordColorByteOffsets[11], 0x0Fu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputSourcePayloadFloatOffsets[0], 0x88u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputSourcePayloadFloatOffsets[3], 0x98u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputSourcePayloadFloatOffsets[8], 0xB0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputSourcePayloadFloatOffsets[11], 0xC0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordDirectionPackedWordOffsets[0], 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordDirectionPackedWordOffsets[1], 0x14u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeOutputFinalRecordFlagByteOffset, 0x18u);
    EXPECT_TRUE(contract.RuntimeLightPacketPack.RuntimeOutputRecordLayoutResolved);
    EXPECT_TRUE(contract.RuntimeLightPacketPack.RuntimeOutputFeedsFinalUploadEmitter);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimeLightEnableFlagsOffset,
              contract.EffectDrawConsumer.RuntimeDrawPayloadOffset);
    EXPECT_EQ(contract.RuntimeLightPacketPack.RuntimePayload3ScaleGateByteOffset, 0x1A1u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DynamicColorGateContextOffset, 0x0Cu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DynamicColorGateTableOffset, 0x04u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DynamicColorGateStrideBytes, 0x124u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.DynamicColorOverrideHelperAddress, 0x00333ABCu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadHelperAddress, 0x004093F8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FallbackFinalUploadHelperAddress, 0x00308498u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSlotLoopAddress, 0x0040D15Cu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSlotPacketEmitterAddress, 0x0040D1A8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSlotLoopCount, 8u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSlotEnableByteBaseOffset, 0x164u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSourceRecordBaseOffset, 0x04u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadSourceRecordStrideBytes, 0x2Cu);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketWordCount, 14u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketHeaderWordIndex, 1u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketPayloadFirstWordIndex, 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketRegisterBase, 0x140u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketRegisterStride, 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPacketHeaderMask, 0x80BF0000u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadRecordSlotIndexByteOffset, 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbPacketWordIndices[0], 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbPacketWordIndices[3], 4u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbByteOffsets[0], 0x0Au);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbByteOffsets[8], 0x06u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbByteOffsets[11], 0x09u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbBitShifts[0], 20u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadPackedRgbBitShifts[2], 0u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadCopiedWordSourceOffsets[0], 0x10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadCopiedWordSourceOffsets[5], 0x20u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadCopiedWordPacketWordIndices[0], 5u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadCopiedWordPacketWordIndices[5], 12u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadFlagPacketWordIndex, 10u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadFlagBaseByteOffset, 0x18u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadFlagBooleanByteOffsets[0], 0x01u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadFlagBooleanByteOffsets[2], 0x03u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadFlagBooleanBitShifts[2], 3u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadZeroPacketWordIndices[0], 9u);
    EXPECT_EQ(contract.RuntimeLightPacketPack.FinalUploadZeroPacketWordIndices[1], 13u);
    EXPECT_TRUE(contract.RuntimeLightPacketPack.FinalUploadRecordPacketEmitterResolved);

    EXPECT_EQ(contract.PacketDefaultRecord.OwnerFunctionAddress, 0x00471F74u);
    EXPECT_EQ(contract.PacketDefaultRecord.AssemblerAddress, 0x00472F8Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.AssemblerEndAddress, 0x004730DCu);
    EXPECT_EQ(contract.PacketDefaultRecord.StaticTemplateAddress, 0x004DBA1Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.StaticTemplateCopySizeBytes, 0x118u);
    EXPECT_EQ(contract.PacketDefaultRecord.TemplateBlockCopyHelperAddress, 0x00371738u);
    EXPECT_EQ(contract.PacketDefaultRecord.RootRecordStackOffset, 0x18u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeCopyHelperAddress, 0x00348B90u);
    EXPECT_EQ(contract.PacketDefaultRecord.FirstRuntimeCopyCallsiteAddress, 0x00473038u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondRuntimeCopyCallsiteAddress, 0x004730BCu);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeCopyDestinationOffset, 0x04u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeCopySizeBytes, 0x120u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimePostCopyZeroOffset, 0x11Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeFlagsWordOffset, 0x20u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeFlagsOrMask, 0xC0u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeSelfPointerOffset, 0u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeSelfPointerValueOffset,
              contract.PacketDefaultRecord.RuntimeCopyDestinationOffset);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeReleasePointerOffset, 0x19Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeReleaseHelperAddress, 0x003445D4u);
    EXPECT_EQ(contract.PacketDefaultRecord.RuntimeBufferSizeBytes, 0x1B8u);
    EXPECT_EQ(contract.PacketDefaultRecord.FirstAllocatorCallsiteAddress, 0x00473020u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondAllocatorCallsiteAddress, 0x004730A8u);
    EXPECT_EQ(contract.PacketDefaultRecord.FirstRuntimeContextBufferOffset, 0x354u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondRuntimeContextBufferOffset, 0x358u);
    EXPECT_EQ(contract.PacketDefaultRecord.ProviderLookupCallsiteAddress, 0x00471FE4u);
    EXPECT_EQ(contract.PacketDefaultRecord.ProviderLookupFunctionAddress, 0x002E11D0u);
    EXPECT_EQ(contract.PacketDefaultRecord.ProviderLookupSlot, 13u);
    EXPECT_EQ(contract.PacketDefaultRecord.ProviderStackPointerOffset, 0x5D0u);
    EXPECT_EQ(contract.PacketDefaultRecord.ResourceContextTableOffset, 0x1A4u);
    EXPECT_EQ(contract.PacketDefaultRecord.ResourceContextEntryCount, 0x33u);
    EXPECT_EQ(contract.PacketDefaultRecord.ResourceContextEntryZeroPathTableAddress, 0x004DB1B8u);
    EXPECT_EQ(contract.PacketDefaultRecord.ResourceContextEntryZeroResolvedPathPattern,
              "rom:/menu/<language>/menu_hint_movie_parts00.ctxb");
    EXPECT_FALSE(contract.PacketDefaultRecord.ProviderSlotIsRoomLightSource);
    EXPECT_FALSE(contract.PacketDefaultRecord.ResourceContextEntryZeroIsRoomLightSource);
    EXPECT_EQ(contract.PacketDefaultRecord.BindingHelperAddress, 0x00348A64u);
    EXPECT_EQ(contract.PacketDefaultRecord.FirstBindingCallsiteAddress, 0x00473058u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBindingCallsiteAddress, 0x004730DCu);
    EXPECT_EQ(contract.PacketDefaultRecord.BindingSlotIndex, 0u);
    EXPECT_EQ(contract.PacketDefaultRecord.BindingModeWord, 0x2600u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferTablePatchAddress, 0x0047305Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchTableStackOffset, 0x160u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchEntryCount, 8u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchStrideBytes, 0x10u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchFloatOffsets[0], 0u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchFloatOffsets[1], 0x08u);
    EXPECT_EQ(contract.PacketDefaultRecord.SecondBufferPatchAddWord, 0x3F800000u);
    ASSERT_EQ(contract.PacketDefaultRecord.Tables.size(), 4u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].Role, "vec4_default_table_stack_0x2e0");
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].SourceAddress, 0x004DB7ACu);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].SourceSizeBytes, 0xC0u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].RecordPointerPatchOffset, 0u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].EntryCount, 12u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[0].EntryStrideBytes, 0x10u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[1].SourceAddress, 0x004DB96Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[1].StackOffset, 0x160u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[1].RecordPointerPatchOffset, 0x04u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[1].EntryCount, 8u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[2].SourceAddress, 0x004DB86Cu);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[2].RecordPointerPatchOffset, 0x08u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[2].EntryCount, 64u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[3].SourceAddress, 0x004DB9ECu);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[3].RecordPointerPatchOffset, 0x10u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[3].EntryCount, 20u);
    EXPECT_EQ(contract.PacketDefaultRecord.Tables[3].EntryStrideBytes, 0x02u);

    EXPECT_EQ(contract.PacketCopyDataflowAudit.RuntimeCopyHelperAddress,
              contract.PacketDefaultRecord.RuntimeCopyHelperAddress);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingHelperAddress,
              contract.PacketDefaultRecord.BindingHelperAddress);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.TotalCallsiteCount, 135u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.RuntimeCopyCallsiteCount, 33u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCount, 102u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingClassifiedNotPacketValueWriterCount,
              contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCount);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingMentionsRuntimePacketPrepContextCount, 49u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingMentionsDrawHandlePacketPointerCount, 3u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingMentionsPacketPrepFunctionCount, 0u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingMentionsRuntimeLightPacketPackFunctionCount, 0u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DescriptorBindingMode2600MentionCount, 62u);
    EXPECT_NE(contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCoverageStatus.find("0x00348A64"),
              std::string::npos);
    EXPECT_NE(contract.PacketCopyDataflowAudit.DescriptorBindingCallsiteCoverageStatus.find("102"),
              std::string::npos);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.DescriptorBindingWritesPacketPrepSourceSlots);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultOwnerFunctionAddress,
              contract.PacketDefaultRecord.OwnerFunctionAddress);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteCount, 28u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteAddresses.size(), 28u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteAddresses[0], 0x004722F0u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultOwnerRuntimeCopyCallsiteAddresses[27], 0x0047323Cu);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.DefaultPacketBufferCopyCallsiteAddresses.size(), 2u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultPacketBufferCopyCallsiteAddresses[0],
              contract.PacketDefaultRecord.FirstRuntimeCopyCallsiteAddress);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.DefaultPacketBufferCopyCallsiteAddresses[1],
              contract.PacketDefaultRecord.SecondRuntimeCopyCallsiteAddress);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCount, 5u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateFunctionAddresses.size(), 4u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateFunctionAddresses[0], 0x002D2754u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateFunctionAddresses[3], 0x004A0928u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCallsiteAddresses.size(), 5u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCallsiteAddresses[0], 0x002D2868u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateCallsiteAddresses[4], 0x004A09B0u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateClassifications.size(), 5u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateClassifications[0],
              "ctxb_dual_record_bounds_and_binding_builder");
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateClassifications[2],
              "global_texture_descriptor_packet_initializer");
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyCandidateClassifications[4],
              "double_buffer_descriptor_rebuild_and_binding");
    EXPECT_EQ(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyExcludedAsDirectLightPacketSourceCount, 5u);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyFeedsSceneZsiLightRecords);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyDirectlyFeedsPacketPrep);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.NonDefaultRuntimeCopyDirectlyFeedsRuntimeLightPacketPack);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.DrawHandleOrPacketPrepMentionedDescriptorBindingCallsiteAddresses.size(),
              3u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedFunctionAddresses.size(), 1u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedFunctionAddresses[0], 0x002F36F4u);
    ASSERT_EQ(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedStoreAddresses.size(), 4u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedStoreAddresses[0], 0x002F3E68u);
    EXPECT_EQ(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExcludedStoreAddresses[3], 0x002F3E74u);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterWritesPacketPrepSource);
    EXPECT_NE(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExclusionStatus.find("0x002F36F4"),
              std::string::npos);
    EXPECT_NE(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExclusionStatus.find("0x0034897C"),
              std::string::npos);
    EXPECT_NE(contract.PacketCopyDataflowAudit.OffsetSimilarVectorWriterExclusionStatus.find("0x003130A4"),
              std::string::npos);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.DecompileScanFoundDirectPacketPrepConsumer);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.DecompileScanFoundRuntimePacketPackConsumer);
    EXPECT_FALSE(contract.PacketCopyDataflowAudit.ActiveRoomPacketSourceResolved);

    EXPECT_EQ(contract.ProviderTable.ProviderLookupFunctionAddress, 0x002E11D0u);
    EXPECT_EQ(contract.ProviderTable.RuntimeProviderTableAddress, 0x0055B490u);
    EXPECT_EQ(contract.ProviderTable.ProviderTableEntryCount, 16u);
    EXPECT_EQ(contract.ProviderTable.ProviderTableEntryStrideBytes, 4u);
    EXPECT_EQ(contract.ProviderTable.ProviderPopulationAddress, 0x004811B4u);
    EXPECT_EQ(contract.ProviderTable.ProviderPopulationEndAddress, 0x00481318u);
    EXPECT_EQ(contract.ProviderTable.MenuCtxbProviderSlot, 13u);
    EXPECT_EQ(contract.ProviderTable.MenuCtxbLanguagePrefixTableAddress, 0x004D3FE8u);
    EXPECT_EQ(contract.ProviderTable.MenuCtxbSuffixTableAddress, 0x004D4010u);
    EXPECT_EQ(contract.ProviderTable.MenuCtxbPathFormat, "%s%s");
    EXPECT_EQ(contract.ProviderTable.MenuCtxbResolvedPathPattern, "rom:/menu/<language>/menu_cursor00.ctxb");
    EXPECT_FALSE(contract.ProviderTable.MenuCtxbProviderIsRoomLightSource);
    EXPECT_EQ(contract.ProviderTable.KankyoCommonOpenHelperAddress, 0x002DE6B8u);
    EXPECT_EQ(contract.ProviderTable.KankyoCommonPath, "rom:/kankyo/kankyo_common.zar");
    EXPECT_EQ(contract.ProviderTable.ZarSetupFunctionAddress, 0x0031B124u);
    EXPECT_EQ(contract.ProviderTable.ZarHeaderTypeSectionOffset, 0x0Cu);
    EXPECT_EQ(contract.ProviderTable.ZarHeaderMetadataSectionOffset, 0x10u);
    EXPECT_EQ(contract.ProviderTable.ZarHeaderDataSectionOffset, 0x14u);
    EXPECT_EQ(contract.ProviderTable.NativeTypeNameTableAddress, 0x0050BBA0u);
    ASSERT_EQ(contract.ProviderTable.NativeTypeSlotNames.size(), 11u);
    EXPECT_EQ(contract.ProviderTable.NativeTypeSlotNames[0], "cmb");
    EXPECT_EQ(contract.ProviderTable.NativeTypeSlotNames[3], "ctxb");
    EXPECT_EQ(contract.ProviderTable.NativeTypeSlotNames[4], "zsi");
    EXPECT_EQ(contract.ProviderTable.NativeTypeSlotNames[10], "unkown");
    EXPECT_EQ(contract.ProviderTable.ProviderTypeSlotIndexBaseOffset, 0x1Cu);
    EXPECT_EQ(contract.ProviderTable.ProviderTypeSlotIndexStrideBytes, 4u);
    EXPECT_EQ(contract.ProviderTable.CmbTypeSlot, 0u);
    EXPECT_EQ(contract.ProviderTable.CtxbTypeSlot, 3u);
    EXPECT_EQ(contract.ProviderTable.ZsiTypeSlot, 4u);
    EXPECT_EQ(contract.ProviderTable.TbdTypeSlot, 8u);
    EXPECT_EQ(contract.ProviderTable.CmbResolverAddress, 0x00358EF8u);
    EXPECT_EQ(contract.ProviderTable.CtxbResolverAddress, 0x00372C90u);
    EXPECT_EQ(contract.ProviderTable.ResolverSectionTablePointerOffset, 0x0Cu);
    EXPECT_EQ(contract.ProviderTable.ResolverOffsetTablePointerOffset, 0x14u);
    EXPECT_EQ(contract.ProviderTable.CmbActiveSectionIndexOffset, 0x1Cu);
    EXPECT_EQ(contract.ProviderTable.CtxbActiveSectionIndexOffset, 0x28u);
    EXPECT_EQ(contract.ProviderTable.TbdActiveSectionIndexOffset, 0x3Cu);
    EXPECT_EQ(contract.ProviderTable.CmbDecodedCacheOffset, 0x4Cu);
    EXPECT_EQ(contract.ProviderTable.CtxbDecodedCacheOffset, 0x54u);
    EXPECT_EQ(contract.ProviderTable.TbdDecodedCacheOffset, 0x68u);
    EXPECT_EQ(contract.ProviderTable.CmbDecodeHelperAddress, 0x00320458u);
    EXPECT_EQ(contract.ProviderTable.CtxbDecodeHelperAddress, 0x003012B4u);
    EXPECT_EQ(contract.ProviderTable.TbdResolverAddress, 0x00328DDCu);
    EXPECT_EQ(contract.ProviderTable.TbdObjectInitializerAddress, 0x004C0F38u);
    EXPECT_EQ(contract.ProviderTable.TbdObjectFreeHelperAddress, 0x00498CD4u);
    EXPECT_EQ(contract.ProviderTable.ZarTeardownFunctionAddress, 0x002F70C4u);
    EXPECT_EQ(contract.ProviderTable.TbdObjectSizeBytes, 8u);
    EXPECT_EQ(contract.ProviderTable.TbdObjectPayloadPointerOffset, 0x00u);
    EXPECT_EQ(contract.ProviderTable.TbdObjectRecordPointerTableOffset, 0x04u);
    EXPECT_EQ(contract.ProviderTable.TbdPayloadRecordCountOffset, 0x0Cu);
    EXPECT_EQ(contract.ProviderTable.TbdPayloadFirstRecordOffset, 0x10u);
    EXPECT_EQ(contract.ProviderTable.TbdRecordSizeOffset, 0x24u);
    EXPECT_EQ(contract.ProviderTable.TbdRecordPayloadAccessorAddress, 0x003373B8u);
    EXPECT_EQ(contract.ProviderTable.TbdRecordPayloadOffset, 0x30u);
    EXPECT_EQ(contract.ProviderTable.KankyoTbdProviderObjectOffset, 0x268u);
    EXPECT_EQ(contract.ProviderTable.KankyoLensflareTbdObjectOffset, 0x26Cu);
    EXPECT_EQ(contract.ProviderTable.KankyoStormTbdObjectOffset, 0x27Cu);
    EXPECT_EQ(contract.ProviderTable.PlayLensflareTbdObjectOffset, 0x0ED0u);
    EXPECT_EQ(contract.ProviderTable.PlayStormTbdObjectOffset, 0x0EE0u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdProviderEntryIndex, 0u);
    EXPECT_EQ(contract.ProviderTable.StormTbdProviderEntryIndex, 1u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdConsumerAddress, 0x002D97E4u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdDefaultDrawWrapperAddress, 0x0045945Cu);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdConditionalDrawWrapperAddress, 0x0045FE28u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdRecordCount, 5u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdRecordIndices[0], 0u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdRecordIndices[4], 4u);
    ASSERT_EQ(contract.ProviderTable.LensflareTbdRecordNames.size(), 5u);
    EXPECT_EQ(contract.ProviderTable.LensflareTbdRecordNames[0], "LensScaleMin");
    EXPECT_EQ(contract.ProviderTable.LensflareTbdRecordNames[4], "LensHalationColor");
    EXPECT_TRUE(contract.ProviderTable.TbdProviderParserLayoutResolved);
    EXPECT_TRUE(contract.ProviderTable.TbdConsumerResolvedBeyondProviderSetup);
    EXPECT_TRUE(contract.ProviderTable.LensflareTbdConsumerResolved);
    EXPECT_FALSE(contract.ProviderTable.LensflareTbdVisibleBackendSubmitResolved);
    EXPECT_EQ(contract.ProviderTable.KankyoCtxbNativeIdStart, 0x44u);
    EXPECT_EQ(contract.ProviderTable.KankyoCtxbNativeIdEnd, 0x4Bu);
    EXPECT_FALSE(contract.ProviderTable.KankyoCtxbIdsAreDirectLocalArchiveIndices);

    EXPECT_EQ(contract.LensflareRuntimeList.KankyoListOffset, 0x0ECu);
    EXPECT_EQ(contract.LensflareRuntimeList.SceneInitAddress, 0x002E47C8u);
    EXPECT_EQ(contract.LensflareRuntimeList.SceneUpdateAddress, 0x002DE22Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.SceneTeardownAddress, 0x002DECFcu);
    EXPECT_EQ(contract.LensflareRuntimeList.StateSetterAddress, 0x002D50E8u);
    EXPECT_EQ(contract.LensflareRuntimeList.DrawAddress, 0x00484F5Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.DrawHelperAddress, 0x002C5314u);
    EXPECT_EQ(contract.LensflareRuntimeList.PrimaryBuilderAddress, 0x002D5124u);
    EXPECT_EQ(contract.LensflareRuntimeList.TeardownAddress, 0x0048500Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.ObjectTransformAddress, 0x00371F1Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.SubmitQueueAddress, 0x002C517Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.SubmitRecordWriterAddress, 0x002C1AE8u);
    EXPECT_EQ(contract.LensflareRuntimeList.BaseRuntimeFactoryAddress, 0x0034897Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeFactoryAddress, 0x00340D00u);
    EXPECT_EQ(contract.LensflareRuntimeList.SharedOwnerConstructorAddress, 0x002C50D4u);
    EXPECT_EQ(contract.LensflareRuntimeList.BaseRuntimeConstructorAddress, 0x002C4F00u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeConstructorAddress, 0x004970E0u);
    EXPECT_EQ(contract.LensflareRuntimeList.BaseRuntimeVtableAddress, 0x004EBD60u);
    EXPECT_EQ(contract.LensflareRuntimeList.BaseRuntimeDrawMethodAddress, 0x003F96BCu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeVtableAddress, 0x004EBE9Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeDrawMethodAddress, 0x003FC2F8u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeDestructorAddress, 0x003FCB20u);
    EXPECT_EQ(contract.LensflareRuntimeList.SharedOwnerVtableAddress, 0x004EBE00u);
    EXPECT_EQ(contract.LensflareRuntimeList.SharedOwnerDrawMethodAddress, 0x003FBBA8u);

    EXPECT_EQ(contract.MoonRuntime.SceneInitAddress, 0x002E47C8u);
    EXPECT_EQ(contract.MoonRuntime.BuilderAddress, 0x002D4F10u);
    EXPECT_EQ(contract.MoonRuntime.BlueSkyBuilderCallsiteAddress, 0x002E4988u);
    EXPECT_EQ(contract.MoonRuntime.CtxbBaseTypeLocalIndex, 4u);
    EXPECT_EQ(contract.MoonRuntime.LayerCount, 3u);
    EXPECT_EQ(contract.MoonRuntime.GeometryTemplateIndices,
              (std::array<uint32_t, 3>{ 1u, 4u, 4u }));
    EXPECT_EQ(contract.MoonRuntime.GeometryTemplateHalfExtents,
              (std::array<float, 3>{ 0.5f, 0.5f, 0.5f }));
    EXPECT_EQ(contract.MoonRuntime.RuntimeObjectTemplateIndices,
              (std::array<uint32_t, 3>{ 3u, 4u, 5u }));
    EXPECT_EQ(contract.MoonRuntime.MinMagFilter, 0x2601u);
    EXPECT_EQ(contract.MoonRuntime.WrapModes,
              (std::array<uint32_t, 3>{ 0x812Fu, 0x8370u, 0x8370u }));
    EXPECT_TRUE(contract.MoonRuntime.InitResolved);
    EXPECT_TRUE(contract.MoonRuntime.TextureInputsResolved);
    EXPECT_FALSE(contract.MoonRuntime.BackendSubmitResolved);
    EXPECT_EQ(contract.LensflareRuntimeList.SharedOwnerObjectSizeBytes, 0x35Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.BaseRuntimeObjectSizeBytes, 0x1E4u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeObjectSizeBytes, 0x28Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchPrimaryBufferResolverAddress, 0x00333270u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchOptionalBuffer0ResolverAddress, 0x003331ECu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchOptionalBuffer1ResolverAddress, 0x00333070u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchVertexCountSetterAddress, 0x00333294u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeElementCountOffset, 0x1FCu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeElementCapacityOffset, 0x1F8u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeInputPositionsPointerOffset, 0x1E4u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeInputMatricesPointerOffset, 0x1E8u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeInputDepthsPointerOffset, 0x1ECu);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeOptionalMatrixPointerOffset, 0x1F0u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchRuntimeOptionalTexcoordPointerOffset, 0x1F4u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchDrawHandleVertexCountOffset, 0x174u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchDrawHandleCapacityOffset, 0x14u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchFallbackQuadVertexCount, 4u);
    EXPECT_EQ(contract.LensflareRuntimeList.QuadBatchNativeVerticesPerQuad, 6u);
    EXPECT_EQ(contract.LensflareRuntimeList.ActiveGroupWordOffset, 0x04u);
    EXPECT_EQ(contract.LensflareRuntimeList.TargetGroupWordOffset, 0x08u);
    EXPECT_EQ(contract.LensflareRuntimeList.BlendWeightWordOffset, 0x0Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.BlendDefaultLiteralAddress, 0x002D511Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.BlendScaleLiteralAddress, 0x002D5120u);
    EXPECT_FLOAT_EQ(contract.LensflareRuntimeList.BlendDefault, 1.0f);
    EXPECT_FLOAT_EQ(contract.LensflareRuntimeList.BlendScale, 1.0f / 255.0f);
    EXPECT_EQ(contract.LensflareRuntimeList.GroupIndexShiftBits, 2u);
    EXPECT_EQ(contract.LensflareRuntimeList.CmbHandleWordIndexBase, 0x04u);
    EXPECT_EQ(contract.LensflareRuntimeList.CmbInstanceWordIndexBase, 0x08u);
    EXPECT_EQ(contract.LensflareRuntimeList.CtxbDescriptorWordIndexBase, 0x0Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.RenderObjectWordIndexBase, 0x24u);
    EXPECT_EQ(contract.LensflareRuntimeList.PrimaryDrawObjectWordIndexBase, 0x28u);
    EXPECT_EQ(contract.LensflareRuntimeList.TerminalDrawObjectWordIndexBase, 0x2Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.PrimaryDrawObjectSourceRow, 1u);
    EXPECT_EQ(contract.LensflareRuntimeList.TerminalDrawObjectSourceRow, 2u);
    EXPECT_EQ(contract.LensflareRuntimeList.PrimaryElementSubmitIndex, 0x0Bu);
    EXPECT_EQ(contract.LensflareRuntimeList.TerminalElementSubmitIndex, 0x0Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.PrimarySubmitQueueIndexBase, 0x12u);
    EXPECT_EQ(contract.LensflareRuntimeList.TerminalSubmitQueueIndexBase, 0x14u);
    EXPECT_EQ(contract.LensflareRuntimeList.SpecialSubmitElementStartIndex, 0x0Bu);
    EXPECT_EQ(contract.LensflareRuntimeList.SpecialSubmitElementEndIndex, 0x0Cu);
    EXPECT_EQ(contract.LensflareRuntimeList.LensflareRuntimeElementCount, 0x0Du);
    EXPECT_EQ(contract.LensflareRuntimeList.RuntimeObjectFlagOffset, 0x178u);
    EXPECT_EQ(contract.LensflareRuntimeList.RuntimeObjectVisibleFlagMask, 0x10u);
    EXPECT_EQ(contract.LensflareRuntimeList.RuntimeObjectPrimaryBuilderFlagMask, 0x80000u);
    EXPECT_EQ(contract.LensflareRuntimeList.BuilderTableAddress, 0x004D1F24u);
    EXPECT_EQ(contract.LensflareRuntimeList.CtxbDescriptorTemplateRows,
              (std::array<uint32_t, 3>{ 0, 2, 3 }));
    EXPECT_EQ(contract.LensflareRuntimeList.RenderObjectSourceRows,
              (std::array<uint32_t, 3>{ 0, 1, 2 }));
    EXPECT_EQ(contract.LensflareRuntimeList.CtxbIndexBaseByRow,
              (std::array<uint32_t, 3>{ 0, 1, 1 }));
    EXPECT_TRUE(contract.LensflareRuntimeList.InitResolved);
    EXPECT_TRUE(contract.LensflareRuntimeList.DrawSlotsResolved);
    EXPECT_TRUE(contract.LensflareRuntimeList.DrawHelperDispatchResolved);
    EXPECT_TRUE(contract.LensflareRuntimeList.TargetGroupDispatchOnlyWhenDifferent);
    EXPECT_TRUE(contract.LensflareRuntimeList.RuntimeVtableMethodsResolved);
    EXPECT_TRUE(contract.LensflareRuntimeList.RuntimeBackendBufferHelpersResolved);
    EXPECT_FALSE(contract.LensflareRuntimeList.BackendSubmitResolved);

    EXPECT_EQ(contract.ShadowDepthRegisterState.SourceKind,
              "oot3d_codebin_pica_shadow_depth_register_state");
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaStateInitializerAddress, 0x003480A8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaStateInitializerCallsiteAddress, 0x004489E4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPacketInitializerAddress, 0x00303A94u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.TextureDescriptorInitializerAddress, 0x00348F34u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorMaterializationAddress,
              contract.DescriptorMaterialization.FunctionAddress);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorBindingAddress,
              contract.CtxbDescriptorBinding.FunctionAddress);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorPacketResetAddress,
              contract.PacketDefaultRecord.RuntimeReleaseHelperAddress);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorPacketObjectSizeBytes,
              contract.DrawHandleSubmit.LazyDescriptorOwnerAllocationSizeBytes);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorPacketRecordPointerLikeWordOffset, 0x1A8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorPacketRecordPointerLikeNextWordOffset, 0x1ACu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaStateWordStrideBytes, 4u);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.PicaRegisterIndexIsStateWordIndex);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapScaleRegister, 0x04Du);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapOffsetRegister, 0x04Eu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapEnableRegister, 0x06Du);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRegister, 0x08Bu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowRegister, 0x130u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapScaleStateWordIndex, 0x017u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapOffsetStateWordIndex, 0x018u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapEnableStateWordIndex, 0x027u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowStateWordIndex, 0x02Au);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowStateWordIndex, 0x05Bu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterAddress, 0x00307BD8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterScannedCallsiteCount, 17u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterFragopShadowMatchCount, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableCallsiteCount, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableResolvedCount, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTablePointerAddresses,
              (std::vector<uint32_t>{ 0x003146CCu, 0x00314858u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicTableAddresses,
              (std::vector<uint32_t>{ 0x004E2EBCu, 0x004E2EE8u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterDynamicRegisterBases,
              (std::vector<uint32_t>{ 0x0C0u, 0x0C8u, 0x0D0u, 0x0D8u, 0x0F0u, 0x0F8u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaScalarWriterScanSource,
              "FindOot3dPicaRegisterWriters.java plus native data words at 0x003146cc/0x00314858");
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterAddress,
              0x00307C94u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterScannedCallsiteCount,
              14u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterFragopShadowMatchCount,
              0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterIndexRegister,
              0x02C0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDataRegister,
              0x02C1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterIndexCommandHeader,
              0x000F02C0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDataCommandHeader,
              0x000F02C1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .GenericPicaVectorUniformWriterDynamicIndexTablePointerAddresses,
              (std::vector<uint32_t>{ 0x00466EFCu, 0x00466F38u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .GenericPicaVectorUniformWriterDynamicIndexTableAddresses,
              (std::vector<uint32_t>{ 0x004E2EA4u, 0x004E2EB0u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterDynamicUniformIndices,
              (std::vector<uint32_t>{ 0x050u, 0x051u, 0x052u, 0x053u, 0x054u,
                                      0x055u, 0x056u, 0x057u, 0x058u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.GenericPicaVectorUniformWriterScanSource,
              "FindOot3dPicaRegisterWriters.java plus code.bin literals at 0x00307d84/0x00307d88 and "
              "native data words at 0x00466efc/0x00466f38");
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterCommitAddress, 0x003084DCu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterScannedCommitRefCount, 21u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterKnownHeaderStoreCount, 41u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterShadowDepthHeaderStoreCount, 2u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterFragopShadowMatchCount, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DirectPicaCommandWriterScanSource,
              "FindOot3dDirectPicaCommandWriters.java scanning direct refs to command commit helper 0x003084dc");
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyHelperAddress, 0x00307AF4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyScannedCallsiteCount, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyResolvedStaticPacketCount, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyFragopShadowMatchCount, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyStaticPacketAddresses,
              (std::vector<uint32_t>{ 0x004E2E9Cu, 0x004E2F44u, 0x004E2F54u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyStaticPacketRegisters,
              (std::vector<uint32_t>{ 0x08Fu, 0x0E0u, 0x1C6u }));
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaPacketCopyScanSource,
              "FindOot3dPicaPacketCopies.java plus native data words at 0x004e2e9c/0x004e2f44/0x004e2f54");
    EXPECT_EQ(contract.ShadowDepthRegisterState.StaticFragopShadowCommandHeaderLiteralMatchCount, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.CapturedFragopShadowRegisterWriteCount, 64u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.CapturedFragopShadowSnapshotRawValue, 0x00003C00u);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.CapturedFragopShadowSnapshotIsPreexistingState);
    EXPECT_EQ(contract.ShadowDepthRegisterState.PicaStateInitializerHighestVerifiedWordIndex, 0x114u);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.PicaStateInitializerWritesFragopShadow);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanRowCount, 56u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanPicaWriterMatchCount, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowWordOffsetAccessScanSource,
              "FindOot3dOffsetAccesses.java for byte offset 0x4c0, excluding actor/camera/view-projection hits");
    EXPECT_FALSE(contract.ShadowDepthRegisterState.FragopShadowSourceRequiresFirstWriteTrace);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFunctionAddress,
              0x00411334u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFunctionEndAddress,
              0x00411B13u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitSlotCount, 0x0BDu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitPayloadBaseOffset,
              0x100Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitMaskByteBaseOffset,
              0x15F4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitHeaderTableAddress,
              0x005A6BF4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .InitialDefaultCommandListEmitFragopShadowHeaderTableSlotIndex,
              0x05Bu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowPayloadOffset,
              0x1178u);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .InitialDefaultCommandListEmitFragopShadowMaskByteOffset,
              0x164Fu);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .InitialDefaultCommandListEmitFragopShadowCommandHeader,
              0x000F0130u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFragopShadowMask,
              0x0Fu);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .InitialDefaultCommandListEmitFragopShadowPayloadRawValue,
              0x00003C00u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitFormula,
              "for slot in [0, 0xbd): if initial_mask[slot] != 0, emit value=shadow_copy[slot], "
              "header=header_table[slot] | (initial_mask[slot]<<16)");
    EXPECT_EQ(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitTraceSource,
              "Ghidra/code.bin: FUN_00411334 emits the initial/default PICA command list from "
              "shadow_copy[slot] at +0x100c and initial mask bytes at +0x15f4; "
              "slot 0x5b emits GPUREG_FRAGOP_SHADOW value 0x00003c00/header 0x000f0130");
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFunctionAddress, 0x0046C204u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFunctionEndAddress, 0x0046FA3Fu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushDirtyBitBaseOffset, 0x7A8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushStateValueBaseOffset, 0x4B4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushMaskByteBaseOffset, 0x3F6u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushShadowCopyBaseOffset, 0x100Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushCommandCursorGlobalAddress,
              0x0054CC4Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushCommandEndGlobalAddress,
              0x0054CC50u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableAddress, 0x005A6BF4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableSourcePairsAddress,
              0x004DED3Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableSourcePairStrideBytes,
              8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushHeaderTableStrideBytes, 4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushRegisterListSentinel, 0x0BDu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowRegisterListAddress,
              0x0054A58Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .FinalRegisterFlushFragopShadowHeaderTableSlotIndex,
              0x05Bu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowStateValueOffset,
              0x620u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowMaskByteOffset,
              0x451u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowShadowCopyOffset,
              0x1178u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowDirtyWordOffset,
              0x7B0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowGroupFlagMask, 0x20u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowRegisterDirtyBitMask,
              0x08000000u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowCommandHeader,
              0x000F0130u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowMask, 0x0Fu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFragopShadowPayloadRawValue,
              0x00003C00u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.NngxPicaRegisterStateInitializerAddress,
              0x00410C68u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.NngxPicaRegisterStateDefaultValueBaseOffset,
              0x1300u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.NngxPicaRegisterStateDefaultMaskByteBaseOffset,
              0x16B1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowDefaultStateValueOffset, 0x146Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FragopShadowDefaultMaskByteOffset, 0x170Cu);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .FragopShadowHeaderTableMappingResolvedFromCodebin);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.FragopShadowDefaultStateResolvedFromCodebin);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .InitialDefaultCommandListEmitResolvedFromCodebin);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .FinalRegisterFlushFragopShadowHeaderTraceWriteCount,
              1209u);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .FinalRegisterFlushFragopShadowPayloadTraceWriteCount,
              1209u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushTraceMemoryWriteRowCount,
              2418u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushTraceRegisterWriteCount, 64u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushFormula,
              "slot = native_state_slot_for_pica_register; if dirty[slot>>5] & (1<<(slot&31)) and "
              "mask_byte[slot] != 0 and state[slot] != shadow[slot], emit value=state[slot], "
              "header=header_table[slot] | (mask_byte[slot]<<16), update shadow[slot], and clear dirty bit");
    EXPECT_EQ(contract.ShadowDepthRegisterState.FinalRegisterFlushTraceSource,
              "Ghidra/code.bin: FUN_00410C68 builds header_table 0x005a6bf4 from source pairs "
              "0x004ded3c and writes default state[0x5b]=0x00003c00/mask[0x5b]=0x0f; "
              "Azahar trace is diagnostic validation only");
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFinalFlushOwnerAddress, 0x003FAD68u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFinalFlushVtableSlotAddress, 0x004EBDBCu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFinalFlushEmitterAddress, 0x00313F50u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFinalFlushCallsiteAddress, 0x003FAEE4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapScaleOffsetSequentialCommandHeader,
              0x801F004Du);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapEnableCommandHeader, 0x000F006Du);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapScaleOffsetSequentialWordCount, 2u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapEnableWordCount, 1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushZeroLiteralAddress, 0x0031401Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushScaleLiteralAddress, 0x003FAF54u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushRecordScaleModeByteOffset, 0x05u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushRecordScaleSourceHalfwordOffset,
              0x06u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushContextScaleFactorFloatOffset,
              0x14u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushContextScaleModeByteOffset, 0x18u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFlushContextExponentWordOffset, 0x1Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthMapFinalFlushFormula,
              "0x00313F50 emits GPUREG_DEPTHMAP_SCALE/OFFSET with header 0x801F004D "
              "and GPUREG_DEPTHMAP_ENABLE with header 0x000F006D; s0==0 encodes "
              "f32(ctx+0x04)-f32(ctx+0x08), offset from f32(ctx+0x04) or scale-mode "
              "adjusted f32(ctx+0x14), enable=1; s0!=0 encodes -s0, offset=0, enable=0");
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterAddress, 0x00409054u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterOwnerAddress, 0x003FA198u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterCallsiteAddress, 0x003FA284u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableAddress, 0x004EBD78u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableSlotAddress, 0x004EBDA8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableSlotOffset, 0x30u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawMethodAddress, 0x003F9B5Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMatrixMethodAddress, 0x003F9F68u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMatrixUploadAddress, 0x00409040u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMatrixRecordOffset, 0x30u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMatrixWordCount, 16u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupEmitterAddress, 0x00408F48u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupCallsiteAddress, 0x003FA24Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupBaseRegister, 0x081u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupWordCount, 10u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupTexParamRegister, 0x08Eu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupTexPointerOffset, 0x08u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupSizeHalfword0Offset, 0x0Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowTextureSetupSizeHalfword1Offset, 0x0Eu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSpecialTexcoordType, 0x6E01u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConstructorAddress, 0x00347258u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordAllocationSizeBytes, 0x234u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordCloneAddress, 0x00310F7Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordCloneWrapperAddress, 0x004C346Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordArrayInitializerAddress, 0x00350820u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordArrayOffset, 0x88u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordStrideBytes, 0x60u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordSubrecordCount, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialPointerOffset, 0x1A8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialPointerValue, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialGateByteOffset, 0x1B5u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialGateByteValue, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialTexcoordByteOffset, 0x1B6u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordInitialTexcoordByteValue, 1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowGlobalRuntimeRootAddress, 0x005BE5B8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowGlobalFactoryManagerSlotOffset, 0x17Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowGlobalFactoryManagerSlotAddress, 0x005BE734u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerConstructorAddress, 0x00417C80u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerVtableAddress, 0x004EC060u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerFactorySlotOffset, 0x08u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerFactoryMethodAddress, 0x003FF53Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerDefaultContextSlotOffset, 0x04u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowFactoryManagerOverrideContextSlotOffset, 0x08u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryAddress, 0x0034897Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryContextField0Offset, 0x1DCu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryContextField1Offset, 0x358u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectSubobjectContextBindAddress,
              0x003F9F3Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectAggregateContextBindAddress,
              0x003FEB14u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectSubobjectContextOffset, 0x10u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectAggregateContextOffset, 0x1Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowActorSpawnAddress, 0x003738D0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowActorContextPointerOffset, 0x178u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawAddress, 0x004BF618u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawRouteFlagOffset, 0x1714u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawRouteFlagMask, 0x04000000u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawCloneCallsiteAddress, 0x004BF984u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawFactoryCallsiteAddress, 0x004BF9A4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawSubmitCallsiteAddress, 0x004BFC88u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerSourceContextPointerOffset, 0x178u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerShadowResourceCmbOffset, 0x24DCu);
    EXPECT_EQ(contract.ShadowDepthRegisterState
                  .Texunit0ShadowPlayerInitCmbResourceVisibilityLoopCallsiteAddress,
              0x00191EB8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowResourceVisibilityClearAddress,
              0x0036932Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowResourceVisibilitySetAddress,
              0x0037266Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceStatePointerOffset,
              0x14u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceVisibilityCountOffset,
              0x68u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectResourceVisibilityBytesOffset,
              0x6Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerDrawObjectOffset, 0x2918u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCloneDestinationOffset, 0x291Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupSetterAddress, 0x0033B504u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelSetterAddress, 0x0032C2C0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerEquipmentDataAddress, 0x0034913Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupTableAddress, 0x0053A558u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupEntrySizeBytes, 5u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupEntryCount, 16u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupRenderModeByteOffset, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupModel0ByteOffset, 1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupGateByteOffset, 2u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupTexcoordByteOffset, 3u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelGroupModel3ByteOffset, 4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableAddress, 0x0053C698u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableEntryStrideBytes, 4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelPointerTableEntryCount, 21u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaHelperAddress, 0x004095E4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaThresholdLiteralAddress, 0x00409634u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaScaleLiteralAddress, 0x00409638u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaClampLiteralAddress, 0x0040963Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterWordCount, 1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterMask, 0x0Fu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitterSequentialFlag, 0u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSpecialRouteGateByteOffset, 0x1B5u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSpecialRouteRecordPointerOffset, 0x1A8u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowBiasSignedDenominatorOffset, 0x0Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowExponentNumeratorFloatOffset, 0x2Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaNearFloatOffset, 0x20u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaFarFloatOffset, 0x24u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaScaleFloatOffset, 0x28u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowProjectionFlagByteOffset, 0x71u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowProjectionFlagXorMask, 1u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowMantissaMask, 0x0FFFFFFEu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowExponentShift, 24u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPacketFormula,
              "GPUREG_TEXUNIT0_SHADOW = (s8(record+0x71) ^ 1) | "
              "(EncodeU24((f32(record+0x20) * f32(record+0x28)) / "
              "(f32(record+0x24) - f32(record+0x20))) & 0x0FFFFFFE) | "
              "(Exponent(f32(record+0x2C) / s16(record+0x0C)) << 24)");
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowPacketEmitResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowSubmitManagerVtableResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConsumerResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordConstructorResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerModelRoutingResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextAllocationChainResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCmbRouteToSourceContextResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .Texunit0ShadowPlayerCmbResourceVisibilityRouteResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .Texunit0ShadowPlayerCmbResourceVisibilityRouteExcludedAsRecordPointerProducer);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowCentralFactoryPropagatesContextOnly);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowDrawObjectContextBindingResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .Texunit0ShadowPlayerDrawCloneCopiesExistingRecordPointer);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .Texunit0ShadowPlayerDrawCreatesShadowObjectFromPlayerCmb);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextInitChainWritesRecordPointer);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerAuxContextExcludedAsSourceContext);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.Texunit0ShadowDirectGateStoreRouteExhausted);
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextRouteSummary,
              "Player_Init loads player+0x24dc from native ZAR_GetCMBByIndex, Player_InitCommon "
              "passes that CMB and player+0x178 into SkelAnime_InitLink, the manager factory "
              "propagates the override AutoClass1 context to draw-object fields, and Player_Draw "
              "later clones the existing source context before creating the Shadow2D draw object "
              "from the same CMB. The clone copies AutoClass1+0x1a8/+0x1b5/+0x1b6/+0x1b7; it "
              "does not create the projection record pointer.");
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowSourceContextNextProofTarget,
              "Find the native CMB/resource/material binding or indirect bulk-copy route that "
              "populates source AutoClass1+0x1a8 before SkelAnime/Player_Draw clone consumers.");
    EXPECT_EQ(contract.ShadowDepthRegisterState.Texunit0ShadowPlayerCmbResourceVisibilityRouteSummary,
              "Player_Init iterates the resource count from player+0x24dc CMB and calls "
              "FUN_0036932C at 0x00191eb8 on draw object player+0x27c for every non-selected "
              "resource id. FUN_0036932C writes 0 to (*(draw+0x14)+0x6c)[id] when id is below "
              "(*(draw+0x14)+0x68); FUN_0037266C is the matching writer of 1. This route mutates "
              "draw-object resource visibility bytes, not AutoClass1+0x1a8.");
    EXPECT_FALSE(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeRecordOwnerResolved);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.Texunit0ShadowRuntimeValuesDecoded);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionUpdateAddress, 0x00471BA4u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.AlternateViewProjectionUpdateAddress, 0x00463D18u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionMatrixInverseAddress, 0x0034A80Cu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionDepthLikeSourceWord0, 0x4Du);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionDepthLikeSourceWord1, 0x4Eu);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionDepthLikeDestinationWord0, 0x65u);
    EXPECT_EQ(contract.ShadowDepthRegisterState.ViewProjectionDepthLikeDestinationWord1, 0x66u);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.ViewProjectionCopyExcludedAsShadowRegisterFlush);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.PicaStateInitializerWritesDepthMapOffset);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.PicaStateInitializerWritesDepthMapEnable);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.PicaStateInitializerWritesTexunit0Shadow);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.GenericPacketInitializerZerosDepthMapScale);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.GenericPacketInitializerZerosDepthMapOffset);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.TextureDescriptorInitializerZerosDepthMapScale);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.TextureDescriptorInitializerZerosDepthMapOffset);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .GenericPacketInitializerClearsDescriptorRecordPointerWords);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .TextureDescriptorInitializerClearsDescriptorRecordPointerWords);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.DescriptorPacketResetClearsDescriptorRecordPointerWords);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .DescriptorPacketInitializersExcludedAsTexunit0ShadowRecordPointerProducers);
    EXPECT_EQ(contract.ShadowDepthRegisterState.DescriptorPacketInitializerExclusionSummary,
              "FUN_00303A94, FUN_003432D4, FUN_00348F34, and FUN_003445D4 initialize or reset "
              "0x1b8-byte descriptor/packet records and clear descriptor word indices 0x6a/0x6b "
              "(byte offsets 0x1a8/0x1ac). They are not writes into the 0x234-byte AutoClass1 "
              "render context that carries the Texunit0Shadow special-route pointer.");
    EXPECT_EQ(contract.ShadowDepthRegisterState.TextureShadowCompareBiasFormula,
              "GPUREG_TEXUNIT0_SHADOW.bias << 1");
    EXPECT_EQ(contract.ShadowDepthRegisterState.FramebufferShadowBiasFormula,
              "GPUREG_FRAGOP_SHADOW constant/linear f16");
    EXPECT_EQ(contract.ShadowDepthRegisterState.DepthEncodeFormula,
              "depth = z_over_w * GPUREG_DEPTHMAP_SCALE + GPUREG_DEPTHMAP_OFFSET; "
              "GPUREG_DEPTHMAP_ENABLE selects W/Z buffering");
    EXPECT_TRUE(contract.ShadowDepthRegisterState.DepthMapScaleOffsetEnableFlushResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.FragopShadowExcludedFromGenericPicaScalarWriter);
    EXPECT_TRUE(contract.ShadowDepthRegisterState
                    .FragopShadowExcludedFromGenericPicaVectorUniformWriter);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.FragopShadowFinalFlushResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.InitialDefaultCommandListEmitResolved);
    EXPECT_TRUE(contract.ShadowDepthRegisterState.FinalRegisterFlushResolved);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.RuntimeValuesDecoded);
    EXPECT_FALSE(contract.ShadowDepthRegisterState.EmulatorTraceUsedAsRuntimeSource);

    EXPECT_EQ(contract.MaterialScalarEmit.DispatchAddress, 0x0047D6ACu);
    EXPECT_EQ(contract.MaterialScalarEmit.DispatchTailBranchAddress, 0x0047D700u);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketGateByteOffset, 0x0Au);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketVectorBaseOffset, 0x6Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketVectorComponentCount, 3u);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketAuxWordOffset, 0x7Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset, 0x80u);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketFlagWordOffset, 0x84u);
    EXPECT_EQ(contract.MaterialScalarEmit.MaterialPacketBackendEnabledByteOffset, 0x98u);
    EXPECT_EQ(contract.MaterialScalarEmit.FallbackPacketEmitAddress, 0x002DD6D0u);
    EXPECT_EQ(contract.MaterialScalarEmit.BackendPacketBuildAddress, 0x002CE8A0u);
    EXPECT_EQ(contract.MaterialScalarEmit.BackendEnableRegisterAddress, 0x002CE8E8u);
    EXPECT_EQ(contract.MaterialScalarEmit.BackendEnableRegisterMask, 0x0B60u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorCommandBuildAddress, 0x0047FE44u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorScaleWordAddress, 0x0047FECCu);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorScaleWord, 0x437F0000u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorCommandWord0Base, 5u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorCommandFlagShift, 16u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorCommandWord1, 0x000500E0u);
    EXPECT_EQ(contract.MaterialScalarEmit.ColorCommandWord3, 0x000F00E1u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarEmitWrapperAddress, 0x0047FED8u);
    EXPECT_EQ(contract.MaterialScalarEmit.GenericScalarWriterAddress, 0x00307BD8u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0CallAddress, 0x0047FF0Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0, 0x0E6u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0Count, 1u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0Mask, 0x0Fu);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0SequentialFlag, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister0PayloadWord, 0u);
    EXPECT_TRUE(contract.MaterialScalarEmit.ScalarRegister0PayloadIsLiteralZero);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1CallAddress, 0x0047FF28u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1, 0x0E8u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1Count, 0x80u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1Mask, 0x0Fu);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1SequentialFlag, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.ScalarRegister1PayloadPointerOffset,
              contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandDispatcherAddress, 0x0047E7BCu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandOpcode, 0x13u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandTargetPointerOffset, 0x10u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandAuxWordOffset, 0x14u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandPayloadPointerOffset, 0x18u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterAddress, 0x00405084u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterCallerAddress, 0x00403F3Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterQueueAllocAddress, 0x0030C20Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterQueueEnqueueAddress, 0x0030C1E8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterRecordWordCount, 7u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterTargetBaseOffset, 0xF4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset, 0x18u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset, 0x1Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitAddress, 0x00403CC8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitCallerAddress, 0x0030D310u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitDriverAddress, 0x0030CBE4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectCaller0Address, 0x00402570u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectCaller1Address, 0x004025F0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitDirectObjectCallerAddress, 0x00403A5Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitEmbeddedObjectOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitAuxReadOffset,
              contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitPayloadReadOffset,
              contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeResolverAddress, 0x00488378u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordResolverAddress, 0x003042D4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeDecodeAddress, 0x0030429Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeSourceArgumentIndex, 2u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeOwnerPayloadOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeOwnerInnerPayloadOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordTableOffset, 0x3Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordCountOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordOffsetTableOffset, 0x08u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeEncodedHighByte, 0x01u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeRecordTypeOffset, 0x0Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordType, 0x2203u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2RecordType, 0x2201u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3RecordType, 0x2202u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ContextListOffset, 0x28u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2ContextListOffset, 0x58u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3ContextListOffset, 0x40u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextListNodeOffset, 0xD4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextPriorityByteOffset, 0x98u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextPriorityWordOffset, 0x50u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitModeContextSubmitArgumentOffset, 0x9Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ValidatorAddress, 0x0048C0D4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2ValidatorAddress, 0x0040E198u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3ValidatorAddress, 0x0048BEFCu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode2PathAddress, 0x004047D8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode3PathAddress, 0x00403A94u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordPayloadResolverAddress, 0x004958ECu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1RecordPayloadRelativeOffset, 0x10u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockWord0Offset, 0x00u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordsOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordCount, 4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockWord5Offset, 0x14u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockByte24Offset, 0x18u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DecodedBlockByte25Offset, 0x19u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Word0ResolverAddress, 0x00495864u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceWordsResolverAddress, 0x00495808u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Byte24ResolverAddress, 0x00495884u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1Byte25ResolverAddress, 0x004958ACu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableRelativeOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableCountOffset, 0x00u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1ReferenceTableFirstEntryOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultReferenceWord, 0xFFFFFFFFu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultByte24, 0x40u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitMode1DefaultByte25, 0x01u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13Mode, 1u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13ModeCopyCallAddress, 0x0030CF00u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitNonOpcode13Mode2CopyCallAddress, 0x0030D048u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitNonOpcode13Mode3CopyCallAddress, 0x0030D184u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialSubmitOpcode13ModeSubmitCallAddress, 0x0030D310u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextBaseInitAddress, 0x0030B174u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextBaseVtableAddress, 0x004EC6A0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived0InitAddress, 0x004044F0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived0VtableAddress, 0x004EC6ECu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived1InitAddress, 0x00404A90u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived1VtableAddress, 0x004EC738u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived2InitAddress, 0x00408248u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextDerived2VtableAddress, 0x004ECA2Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAuxFieldOffset,
              contract.MaterialScalarEmit.PayloadSetterMaterialSubmitAuxReadOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadFieldOffset,
              contract.MaterialScalarEmit.PayloadSetterMaterialSubmitPayloadReadOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextInitZeroValue, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachCallsiteAddress, 0x0030D4C8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachAddress, 0x00405414u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListInsertAddress, 0x0030CAB0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachOptionalListStackOffset, 0x2Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListCountOffset, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListHeadOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachListNodeOffset, 0xECu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachAuxFieldOffset,
              contract.MaterialScalarEmit.PayloadSetterMaterialContextAuxFieldOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextAttachWritesPayloadField, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyAddress, 0x0030B728u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall0Address, 0x0030CF00u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall1Address, 0x0030D048u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyCall2Address, 0x0030D184u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopySourceArgumentIndex, 3u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyDestinationOffset,
              contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadFieldOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopySourceWordCount, 5u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolverSourceOffset, 0x08u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolverVtableSlotOffset, 0x08u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialContextPayloadCopyResolvedObjectOffset, 0x28u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBuilderAddress, 0x00402A50u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBuilderCallsiteAddress, 0x00402B70u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceTemplatePointerAddress, 0x00402B94u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceTemplateAddress, 0x004E74ECu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceObjectBaseOffset, 0x58u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourcePayloadObjectOffset, 0x5Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourcePayloadPointerRelativeOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseConstructorAddress, 0x00402C60u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseVtableAddress, 0x004EC46Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceBaseVtablePointerLiteralAddress,
              0x00402CC4u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectConstructorAddress,
              0x003FB4D0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectVtableAddress, 0x004EBDC8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectVtablePointerLiteralAddress,
              0x003FB518u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterMaterialPayloadSourceDerivedEffectLightListAliasOffset,
              contract.EffectDrawConsumer.LightListOffset + contract.EffectDrawConsumer.LightListPrimaryCountOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorInitAddress, 0x004644A8u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorSourceFileLiteralAddress, 0x00464714u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorObjectSizeLiteralAddress, 0x00464524u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorObjectSizeBytes, 0xC40u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorAuxSourceOffset,
              contract.MaterialScalarEmit.PayloadSetterCommandEmitterAuxSourceOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorPayloadSourceOffset,
              contract.MaterialScalarEmit.PayloadSetterCommandEmitterPayloadSourceOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorPayloadBaseRelativeOffset, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterDescriptorAuxTableRelativeOffset, 0x140u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderAddress, 0x00368704u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderVectorSyncAddress, 0x002D4554u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderPackedTableBuildAddress, 0x004C062Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderObjectPointerOffset, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderEnabledByteOffset, 0x44u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderDirtyByteOffset, 0x45u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderAuxWordSourceOffset, 0x24u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderVectorSourceOffset, 0x04u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderPackedTableSourceOffset, 0x468u);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderPacketAuxWordDestinationOffset,
              contract.MaterialScalarEmit.MaterialPacketAuxWordOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderPacketPayloadPointerDestinationOffset,
              contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.RuntimePayloadBinderPacketVectorDestinationOffset,
              contract.MaterialScalarEmit.MaterialPacketVectorBaseOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterAddress, 0x00487740u);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterAuxWordDestinationOffset,
              contract.MaterialScalarEmit.MaterialPacketAuxWordOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.PayloadSetterPointerDestinationOffset,
              contract.MaterialScalarEmit.MaterialPacketPayloadPointerOffset);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadRuntimeUpdateCallerAddress, 0x002E2674u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadRuntimeUpdateAddress, 0x00464B2Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadSourceFileLiteralAddress, 0x00464D60u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadDefaultSourceAddress, 0x004FA8B8u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadObjectSizeLiteralAddress, 0x00464DA4u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadObjectSizeBytes, 0x668u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadInitAddress, 0x0047FDF8u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadBuildAddress, 0x0047FD44u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadReleaseAddress, 0x0047FE2Cu);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadSourcePointerOffset, 0u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadSourceFloat0Offset, 0x68u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadSourceFloat1Offset, 0x268u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadPackedTableOffset, 0x468u);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadPackedTableEntryCount,
              contract.MaterialScalarEmit.ScalarRegister1Count);
    EXPECT_EQ(contract.MaterialScalarEmit.FogPayloadPackedTableWordBytes, 4u);

    EXPECT_EQ(contract.CmbLutAssetDecode.DecodeAddress, 0x004C382Cu);
    EXPECT_EQ(contract.CmbLutAssetDecode.DecodeCallsiteAddress, 0x00320440u);
    EXPECT_EQ(contract.CmbLutAssetDecode.DecodeOwnerFunctionAddress, 0x0031FF64u);
    EXPECT_EQ(contract.CmbLutAssetDecode.SourceLutSectionCountOffset, 0x08u);
    EXPECT_EQ(contract.CmbLutAssetDecode.SourceLutRecordOffsetTableOffset, 0x10u);
    EXPECT_EQ(contract.CmbLutAssetDecode.RuntimeObjectSourceSectionPointerOffset, 0u);
    EXPECT_EQ(contract.CmbLutAssetDecode.RuntimeObjectPointerTableOffset, 0x04u);
    EXPECT_EQ(contract.CmbLutAssetDecode.RuntimeObjectPackedTableBaseOffset, 0x08u);
    EXPECT_EQ(contract.CmbLutAssetDecode.RuntimeObjectAllocatorContextOffset, 0x0Cu);
    EXPECT_EQ(contract.CmbLutAssetDecode.AllocatorCursorOffset, 0x08u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PointerTableEntrySizeBytes, 4u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PackedTableBytesPerLut, 0x800u);
    EXPECT_EQ(contract.CmbLutAssetDecode.SourceSampleCount, 0x101u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PackedBaseValueOffset, 0u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PackedDeltaValueOffset, 0x400u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PackedValueCount, 0x100u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PackedIterationCount, 0x80u);
    EXPECT_EQ(contract.CmbLutAssetDecode.PointerTableInitAddress, 0x002DEB7Cu);
    EXPECT_EQ(contract.CmbLutAssetDecode.SourceEvaluatorAddress, 0x003087A4u);
    EXPECT_EQ(contract.CmbLutAssetDecode.ClampMinimumWordAddress, 0x004C39B4u);
    EXPECT_EQ(contract.CmbLutAssetDecode.ClampMinimumWord, 0u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadPointerBindAddress, 0x002FB074u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHelperAddress, 0x004C6964u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadRegisterBase, 0x6614u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHelperRegisterRangeBase, 0x6610u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHelperRegisterRangeCount, 0x20u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadCopyWordCount, 0x200u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadCopyByteCount,
              contract.CmbLutAssetDecode.PackedTableBytesPerLut);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadCopyDestinationOffset, 0x04u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadInvalidationWordOffset, 0x81Cu);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadInvalidationWordValue, 0xFFFFFFFFu);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadDirtyFlagMask, 0x4000u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHeaderWord0, 0u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHeaderWord1Address, 0x004C39B8u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHeaderWord1, 0x6605u);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHeaderWord2Address, 0x004C39BCu);
    EXPECT_EQ(contract.CmbLutAssetDecode.UploadHeaderWord2, 0x1406u);
    EXPECT_FALSE(contract.CmbLutAssetDecode.FinalShaderSemanticResolved);

    EXPECT_EQ(contract.DrawHandleSubmit.FunctionAddress, 0x0030F4D0u);
    EXPECT_EQ(contract.DrawHandleSubmit.PacketPrepAddress, contract.PacketPrep.FunctionAddress);
    EXPECT_EQ(contract.DrawHandleSubmit.MatrixBeginAddress, 0x0032471Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MatrixEndAddress, 0x002F9C74u);
    EXPECT_EQ(contract.DrawHandleSubmit.PrimitivePacketBuildAddress, 0x0045259Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialAnimationBuild0Address, 0x00452934u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialAnimationBuild1Address, 0x00452B40u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialAnimationBuild2Address, 0x00452F04u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialStateSetupAddress, contract.MaterialScalarEmit.DispatchAddress);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialStateFallbackAddress,
              contract.MaterialScalarEmit.FallbackPacketEmitAddress);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshPacketFlushAddress, 0x00466E2Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandWriterInitAddress, 0x002EA028u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandWriterStateOffset, 0x18u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandScratchPointerLiteralAddress, 0x0030F6ACu);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandScratchCapacityBytes, 0x4000u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandPrimitiveEmitterAddress,
              contract.DrawHandleSubmit.PrimitivePacketBuildAddress);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandUsedSizeAddress, 0x00314870u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandElementBuildAddress, 0x00454780u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandListOffset, 0x5Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandListElementStrideBytes, 0x18u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandElementUsedSizeOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandElementAlignedCopyDestinationOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandElementAllocationOverheadBytes, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.PerMeshCommandElementAllocationAlignmentBytes, 0x10u);
    EXPECT_FALSE(contract.DrawHandleSubmit.PerMeshCommandBuilderDirectlyWritesPacketPrepSource);
    EXPECT_NE(contract.DrawHandleSubmit.PerMeshCommandBuilderStatus.find("0030f4d0"), std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.PerMeshCommandBuilderStatus.find("00454780"), std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.PerMeshCommandBuilderStatus.find("draw_handle+0x10"), std::string::npos);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTransformCacheAllocAddress, 0x00454780u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTransformCachePatchAddress, 0x004547DCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialPacketPointerOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialCacheReadyByteOffset, 0x14u);
    EXPECT_EQ(contract.DrawHandleSubmit.SubmittedPassByteOffset, 0x15u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTransformArrayOffset, 0x5Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.VisibilityTableOffset, 0x6Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTransformPatchSourceOffset, 0x80u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialDrawDispatchAddress, 0x00452854u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialDrawDispatchLoopEndAddress, 0x00452920u);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialDrawDispatchResolvedFromCodebin);
    EXPECT_FALSE(contract.DrawHandleSubmit.MaterialDrawDispatchPromotesActiveOverrideGate);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMeshStrideBytes, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMeshMaterialLaneByteOffset, 0x02u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMeshVisibilityByteOffset, 0x03u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMeshCountOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMeshPassSplitIndexOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneStrideBytes, 0x1CCu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialStateGateByteOffset, 0x02u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneAnimationByteBlock0Offset, 0xA4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneAnimationByteBlock1Offset, 0xA8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePopulateCallerAddress, 0x0031FF64u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePopulateAddress, 0x004C34ACu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneResetHelperAddress, 0x004C6264u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPopulateAddress, 0x004C6364u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendSourceTranslateAddress, 0x00307A48u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendOperandTranslateAddress, 0x003079D0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendEquationTranslateAddress, 0x00307964u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendSourceTablePointerAddress, 0x00307ABCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendSourceTableAddress, 0x004EA320u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendOperandTablePointerAddress, 0x00307A44u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendOperandTableAddress, 0x004EA340u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendEquationTablePointerAddress, 0x003079CCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendEquationTableAddress, 0x004EA360u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendFloatScaleWordAddress, 0x004C3664u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendFloatScaleWord, 0x437F0000u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialCountOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialTableOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialRecordSizeBytes, ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneSourceMaterialPointerOffset, 0x00u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRuntimeContextPointerOffset, 0x04u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTableMeshResourceHandleTablePointerOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTableLaneBasePointerOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialTableArenaCursorPointerOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneMeshResourceHandleTablePointerOffset,
              contract.DrawHandleSubmit.MaterialLaneRuntimeContextPointerOffset);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePostMaterialRecordTablePointerOffset,
              contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerOffset);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneHeaderPointersResolvedFromCodeBin);
    EXPECT_FALSE(contract.DrawHandleSubmit.MaterialLaneHeaderPointersAreDirectCmbMaterialData);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLanePostMaterialTableDerivedFromCmbMaterialRecordTail);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLanePostMaterialTablePointerSharedByAllLanes);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLanePostMaterialTableResolvedAsTextureEnvTable);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePostMaterialTableBaseFormulaMaterialCountOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLanePostMaterialTableBaseFormulaMaterialRecordSizeBytes,
              ThreeDsRecomp::Oot3d::kOot3dCmbMaterialSize);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialTextureEnvRecordSizeBytes,
              ThreeDsRecomp::Oot3d::kOot3dCmbMaterialTextureEnvSize);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialTextureEnvStageCountOffset,
              ThreeDsRecomp::Oot3d::kOot3dCmbMaterialRawTextureStageCountOffset);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialTextureEnvStageIndexOffset,
              ThreeDsRecomp::Oot3d::kOot3dCmbMaterialRawTextureStageIndexOffset);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneHeaderPointerStatus.find("0x0031FF64"),
              std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneHeaderPointerStatus.find("0x004C34AC"),
              std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneHeaderPointerStatus.find("TextureEnv"),
              std::string::npos);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockDestinationOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialCopiedBlockSourceOffset, 0xCCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockSourcePointerOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockMinimumSourceBytes, 0x2Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0TranslateAddress, 0x004C7CE8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorTranslateAddress, 0x004C7D60u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1TranslateAddress, 0x004C7EB8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2TranslateAddress, 0x004C7DDCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3TranslateAddress, 0x004C7E18u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4TranslateAddress, 0x004C7F08u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputTranslateAddress, 0x004C7CE8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleTranslateAddress, 0x004C7D60u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitTranslateAddress, 0x004C7EB8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeTranslateAddress, 0x004C7DDCu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigTranslateAddress, 0x004C7E18u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalSourcePointerOffset, 0x00u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalClearedByteOffset, 0x04u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockLocalClearedByteCount, 3u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag0SourceLocalOffset, 0x24u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0SourceLocalOffset, 0x26u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorSourceLocalOffset, 0x28u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1SourceLocalOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2SourceLocalOffset, 0x12u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag1SourceLocalOffset, 0x14u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3SourceLocalOffset, 0x18u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4SourceLocalOffset, 0x1Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag2SourceLocalOffset, 0x1Eu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3SourceLocalOffset, 0x1Fu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag4SourceLocalOffset, 0x20u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5SourceLocalOffset, 0x23u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputSourceLocalOffset, 0x26u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleSourceLocalOffset, 0x28u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitSourceLocalOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeSourceLocalOffset, 0x12u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigSourceLocalOffset, 0x18u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag0ByteOffset, 0x1A5u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum0ByteOffset, 0x1A4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFloatSelectorByteOffset, 0x1A6u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum1ByteOffset, 0x198u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum2ByteOffset, 0x197u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag1ByteOffset, 0x19Du);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum3ByteOffset, 0x194u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockEnum4ByteOffset, 0x195u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag2ByteOffset, 0x19Eu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3ByteOffset, 0x19Fu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag4ByteOffset, 0x1A0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag5ByteOffset, 0x1A1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputByteOffset, 0x1A4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutScaleByteOffset, 0x1A6u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpTextureUnitByteOffset, 0x198u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeByteOffset, 0x197u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigByteOffset, 0x194u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputConstantBase, 0x62A0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputConstantCount, 6u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaTextureUnitConstantBase, 0x84C0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaTextureUnitConstantCount, 4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeConstantBase, 0x62C8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaBumpModeConstantCount, 3u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigConstantBase, 0x62B0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfigLinearCount, 7u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfig7Constant, 0x62B7u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLightingConfig7EncodedValue, 8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0TranslateAddress,
              0x004C7F08u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0SourceLocalOffset,
              0x1Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset, 0x195u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0Register, 0x1D0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift, 1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantBase,
              0x62C0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantCount,
              4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector[0],
              1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector[3],
              0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0RegisterName,
              "GPUREG_LIGHTING_LUTINPUT_ABS");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0FieldName,
              "disable_d0");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0SamplerName, "d0");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputAbsD0PackingRule,
              "selector <= 1 ? 1 - selector : 0");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketSubmitWrapperAddress,
              0x00308498u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress,
              0x0040D040u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketFollowupEmitterAddress,
              0x0040CDD8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketWordCount, 6u);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleansInvertClamp01);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketHeaders[0], 0x000F01D0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketHeaders[1], 0x000F01D1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPacketHeaders[2], 0x000F01D2u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets[0],
              0x195u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets[2],
              0x19Du);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets[3],
              0x1A1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts[3],
              13u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPrimaryNibbleByteOffsets[0],
              0x194u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigPrimaryNibbleByteOffsets[3],
              0x1A0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSecondaryNibbleByteOffsets[2],
              0x19Eu);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts[4],
              16u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisters[0], 0x1D0u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisters[1], 0x1D1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisters[2], 0x1D2u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisterNames[0],
              "GPUREG_LIGHTING_LUTINPUT_ABS");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisterNames[1],
              "GPUREG_LIGHTING_LUTINPUT_SELECT");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegisterNames[2],
              "GPUREG_LIGHTING_LUTINPUT_SCALE");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[0], "d0");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[2], "sp");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigSamplerOrder[6], "rr");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldConsumerOffsetScanOutputPath,
              "analysis/material_lighting_lane_offset_accesses.csv");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldConsumerExportPath,
              "analysis/material_lighting_raw_field_consumer_ghidra_export");
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldOffsetAccessScanRowCount, 3103u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldByteConsumerCandidateCount, 37u);
    ASSERT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldTrueConsumerFunctionAddresses.size(), 1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldTrueConsumerFunctionAddresses[0], 0x0040D040u);
    ASSERT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldProducerFunctionAddresses.size(), 2u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldProducerFunctionAddresses[0], 0x004C6264u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldProducerFunctionAddresses[1], 0x004C6364u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldFalsePositiveFunctionAddresses.size(), 8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneRawFieldFalsePositiveFunctionAddresses[1], 0x002A1A18u);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigRegistersResolvedFromCodeBin);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigEmitterConsumesLaneByteOffsets);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaConfigOffsetArraysAreLaneRelative);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPicaLutInputPackingResolved);
    EXPECT_TRUE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved);
    EXPECT_FALSE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3ConsumedByPicaConfigEmitter);
    EXPECT_FALSE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3FinalSemanticResolved);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3Status.find("0x004C6364"),
              std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3Status.find("0x0040D040"),
              std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3Status.find("0x002D5F68"),
              std::string::npos);
    EXPECT_NE(contract.DrawHandleSubmit.MaterialLaneCopiedBlockFlag3Status.find("0x00461904"),
              std::string::npos);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlendModeByteOffset, 0x138u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlendModeEnabledValue, 1u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendEnabledByteOffset, 0x1C0u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend0SourceOffset, 0x13Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend0OperandOffset, 0x13Eu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend0EquationOffset, 0x140u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend1SourceOffset, 0x144u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend1OperandOffset, 0x146u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlend1EquationOffset, 0x148u);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlendColorFloatBaseOffset, 0x14Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.CmbMaterialBlendColorFloatCount, 4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend0SourceByteOffset, 0x1C2u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend0OperandByteOffset, 0x1C3u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend0EquationByteOffset, 0x1C6u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend1SourceByteOffset, 0x1C4u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend1OperandByteOffset, 0x1C5u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlend1EquationByteOffset, 0x1C7u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendColorByteBaseOffset, 0x1C8u);
    EXPECT_EQ(contract.DrawHandleSubmit.MaterialLaneBlendColorByteCount, 4u);
    EXPECT_EQ(contract.DrawHandleSubmit.VtableLaneSelectSlotOffset, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.VtableMeshPrepareSlotOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.VtablePrimitivePacketSlotOffset, 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.VtableMeshDrawSlotOffset, 0x14u);
    EXPECT_EQ(contract.DrawHandleSubmit.VtableMaterialDrawSlotOffset, 0x20u);
    EXPECT_EQ(contract.DrawHandleSubmit.VtableMaterialPreDrawSlotOffset, 0x24u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListAllocatorAddress, 0x00313CE0u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBuilderAddress, 0x002FC694u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListOwnerCandidateAddress, 0x0044BD54u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListCountOffset, 0u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListViewportWidthFloatOffset, 0x04u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListViewportHeightFloatOffset, 0x08u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferOffsets[0], 0x0Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferOffsets[1], 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferOffsets[4], 0x1Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferStrideBytes[0], 0x30u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferStrideBytes[2], 0x20u);
    EXPECT_EQ(contract.DrawHandleSubmit.ExcludedDrawListBufferStrideBytes[3], 0x40u);
    EXPECT_TRUE(contract.DrawHandleSubmit.ExcludedDrawListStoresCountWhereSubmittedHandleRequiresVtable);
    EXPECT_FALSE(contract.DrawHandleSubmit.ExcludedDrawListDirectlyWritesPacketPrepSource);
    EXPECT_FALSE(contract.DrawHandleSubmit.ExcludedDrawListCandidateIsSubmittedDrawHandle);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAddress, 0x004A3658u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorSingletonPointerAddress, 0x004A3788u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorSingletonAddress, 0x0055A1F8u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerDescriptorTablePointerAddress, 0x004A378Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerDescriptorTableAddress, 0x004FA640u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSlotTablePointerAddress, 0x004A3798u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSlotTableAddress, 0x004FA77Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorTagPointerAddress, 0x004A3790u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAllocatorTagAddress, 0x004CF4CCu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerGenericAllocationTypePointerAddress, 0x004A3794u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerGenericAllocationTypeId, 0x0279u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialAllocationTypeId, 0x027Cu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerAllocationSizeBytes, 0x1B8u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialInitializerAddress, 0x003432D4u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerGenericInitializerAddress, 0x00348F34u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerMaterializerAddress, 0x00348BE4u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerBindingAddress, 0x00348A64u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerProviderResolverAddress, 0x0033AAACu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerProviderContextWordOffset, 0x140u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerPointerStoreIndexBias, 1u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerBindingSlotCount, 2u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerBindingRecordStrideBytes, 0x10u);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerBindingSentinelByteValue, 0xFFu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialDescriptorIds[0], 0x0Fu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialDescriptorIds[1], 0x2Eu);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerSpecialDescriptorIds[2], 0x2Fu);
    EXPECT_TRUE(contract.DrawHandleSubmit.LazyDescriptorOwnerResolved);
    EXPECT_FALSE(contract.DrawHandleSubmit.LazyDescriptorOwnerDirectlyWritesPacketPrepSource);
    EXPECT_EQ(contract.DrawHandleSubmit.LazyDescriptorOwnerStatus,
              "resolved_descriptor_object_owner_and_ctxb_binding_path_not_packet_value_writer");

    EXPECT_EQ(contract.GameplayDrawSequence.FunctionAddress, 0x002E25F0u);
    EXPECT_EQ(contract.GameplayDrawSequence.TailBranchAddress, 0x002E2E50u);
    EXPECT_EQ(contract.GameplayDrawSequence.TailBranchTargetAddress, 0x0046FBACu);
    ASSERT_EQ(contract.GameplayDrawSequence.Callsites.size(), 6u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[0].Role, "environment_vector_prep");
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[0].CallsiteAddress, 0x002E2BC4u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[0].FunctionAddress, 0x004594E0u);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[0].HasGate);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[0].GateByteOffset, 0x31A6u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[1].FunctionAddress, 0x0046423Cu);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[2].FunctionAddress, 0x0045C25Cu);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[3].Role, "z_kankyo_thunder_runtime_submit_update");
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[3].CallsiteAddress, 0x002E2BE4u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[3].FunctionAddress, contract.ThunderUpdate.FunctionAddress);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[3].HasSecondArgument);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[3].SecondArgumentValue, 0u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[4].Role, "z_kankyo_weather_particle_submit_update");
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[4].CallsiteAddress, 0x002E2CCCu);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[4].FunctionAddress, 0x00463544u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[5].Role, "z_kankyo_storm_runtime_schedule");
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[5].CallsiteAddress, 0x002E2DB0u);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[5].FunctionAddress, 0x004599BCu);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[5].HasGate);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[5].GateByteOffset, 0x3266u);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[5].GateRequiresNonZero);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[5].HasSecondArgument);
    EXPECT_TRUE(contract.GameplayDrawSequence.Callsites[5].SecondArgumentIsDynamic);
    EXPECT_EQ(contract.GameplayDrawSequence.Callsites[5].SecondArgumentSourceByteOffset, 0x3266u);
    EXPECT_EQ(contract.RenderRecordScheduler.FunctionAddress, 0x00328350u);
    EXPECT_EQ(contract.RenderRecordScheduler.FunctionEndAddress, 0x0032839Fu);
    EXPECT_EQ(contract.RenderRecordScheduler.CountBaseOffset, 0x14u);
    EXPECT_EQ(contract.RenderRecordScheduler.CountStrideBytes, 4u);
    EXPECT_EQ(contract.RenderRecordScheduler.CategoryRecordStrideBytes, 0x60u);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimePointerArrayOffset, 0x214u);
    EXPECT_EQ(contract.RenderRecordScheduler.AuxValueArrayOffset, 0x4B4u);
    EXPECT_EQ(contract.RenderRecordScheduler.MaxAcceptedRecordCount, 0x18u);
    EXPECT_EQ(contract.RenderRecordScheduler.PostInsertCallbackAddress, 0x0030FDA8u);
    EXPECT_EQ(contract.RenderRecordScheduler.SortFunctionAddress, 0x0030FDA8u);
    EXPECT_EQ(contract.RenderRecordScheduler.SortFunctionEndAddress, 0x0030FE73u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainFunctionAddress, 0x002FEA30u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainFunctionEndAddress, 0x002FEABBu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandleCountBaseOffset, 0x754u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandlePointerArrayOffset, 0x770u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandlePointerDrawHandleOffset, 0x14u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandleSubmitFunctionAddress, 0x0030F4D0u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandleSubmitPassValues[0], 0u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrawHandleSubmitPassValues[1], 1u);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeDispatchVtableSlotOffset, 0x0Cu);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4VtableAddress, 0x004EBD60u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchVtableEntryAddress, 0x004EBD6Cu);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchFunctionAddress, 0x003F9680u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchFunctionEndAddress, 0x003F96B7u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGatePointerLiteralAddress,
              0x003F96B8u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGateAddress, 0x0054C8C8u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchFlagMasks[0], 0x20u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchFlagMasks[1], 0x40u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGateValues[0], 0u);
    EXPECT_EQ(contract.RenderRecordScheduler.Runtime1E4DispatchGlobalGateValues[1], 1u);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerPointerOffset, 0x08u);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerSubmitVtableAddress,
              contract.RuntimeEffectWrapper.ContainerVtableAddress);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerSubmitVtableEntryAddress,
              contract.RuntimeEffectWrapper.ContainerSubmitVtableEntryAddress);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerSubmitVtableSlotOffset,
              contract.RuntimeEffectWrapper.ContainerSubmitVtableSlotOffset);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerSubmitFunctionAddress,
              contract.RuntimeEffectWrapper.ContainerSubmitFunctionAddress);
    EXPECT_EQ(contract.RenderRecordScheduler.RuntimeContainerSubmitEffectDrawFunctionAddress,
              contract.EffectDrawConsumer.DrawFunctionAddress);
    ASSERT_EQ(contract.RenderRecordScheduler.DrainWrappers.size(), 6u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[0].FunctionAddress, 0x004228C4u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[0].FunctionEndAddress, 0x004228CBu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[0].Category, 1u);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainWrappers[0].HasPreDrainSetup);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[1].FunctionAddress, 0x004228CCu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[1].FunctionEndAddress, 0x004228D3u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[1].Category, 2u);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainWrappers[1].HasPreDrainSetup);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[2].FunctionAddress, 0x004228D4u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[2].FunctionEndAddress, 0x004228DBu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[2].Category, 5u);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainWrappers[2].HasPreDrainSetup);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[3].FunctionAddress, 0x004228DCu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[3].FunctionEndAddress, 0x004228E3u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[3].Category, 3u);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainWrappers[3].HasPreDrainSetup);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[4].FunctionAddress, 0x004228E4u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[4].FunctionEndAddress, 0x004228EBu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[4].Category, 4u);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainWrappers[4].HasPreDrainSetup);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[5].FunctionAddress, 0x0041AFACu);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[5].FunctionEndAddress, 0x0041B197u);
    EXPECT_EQ(contract.RenderRecordScheduler.DrainWrappers[5].Category, 6u);
    EXPECT_TRUE(contract.RenderRecordScheduler.DrainWrappers[5].HasPreDrainSetup);
    EXPECT_TRUE(contract.RenderRecordScheduler.WritesRuntimePointerAndAuxValue);
    EXPECT_TRUE(contract.RenderRecordScheduler.ReturnsFalseOnOverflow);
    EXPECT_TRUE(contract.RenderRecordScheduler.SortsAscendingByAuxValue);
    EXPECT_TRUE(contract.RenderRecordScheduler.SortMovesRuntimePointerWithAuxValue);
    EXPECT_TRUE(contract.RenderRecordScheduler.DrainsDrawHandlesBeforeRuntimeVtableDispatch);
    EXPECT_TRUE(contract.RenderRecordScheduler.RuntimeDispatchUsesVtableSlot);
    EXPECT_TRUE(contract.RenderRecordScheduler.Runtime1E4DispatchPassesThroughContainerSubmit);
    EXPECT_TRUE(contract.RenderRecordScheduler.DrainConnectsToEffectDrawConsumer);
    EXPECT_FALSE(contract.RenderRecordScheduler.DrainConnectsToType6DrawCommand);
    EXPECT_EQ(contract.StormRuntimeSchedule.UpdateFunctionAddress, 0x004599BCu);
    EXPECT_EQ(contract.StormRuntimeSchedule.UpdateFunctionEndAddress, 0x00459F67u);
    EXPECT_EQ(contract.StormRuntimeSchedule.GameplayDrawCallsiteAddress, 0x002E2DB0u);
    EXPECT_EQ(contract.StormRuntimeSchedule.GameplayDrawModeGatePlayOffset, 0x3266u);
    EXPECT_TRUE(contract.StormRuntimeSchedule.GameplayDrawRequiresNonZeroModeGate);
    EXPECT_TRUE(contract.StormRuntimeSchedule.GameplayDrawPassesModeGateAsSecondArgument);
    EXPECT_EQ(contract.StormRuntimeSchedule.FadeByte0PlayOffset, 0x3267u);
    EXPECT_EQ(contract.StormRuntimeSchedule.FadeByte1PlayOffset, 0x3268u);
    EXPECT_EQ(contract.StormRuntimeSchedule.RuntimePointerPlayOffset, 0x3404u);
    EXPECT_EQ(contract.StormRuntimeSchedule.RuntimePointerKankyoOffset, 0x274u);
    EXPECT_EQ(contract.StormRuntimeSchedule.PhaseAccumulatorPlayOffset, 0x3408u);
    EXPECT_EQ(contract.StormRuntimeSchedule.PhaseAccumulatorKankyoOffset, 0x278u);
    EXPECT_EQ(contract.StormRuntimeSchedule.EnvironmentVectorResolverAddress, 0x002E4660u);
    EXPECT_EQ(contract.StormRuntimeSchedule.PauseContextGetStateAddress, 0x003695F8u);
    EXPECT_EQ(contract.StormRuntimeSchedule.SlotParameterWriterAddress, 0x003429C8u);
    EXPECT_EQ(contract.StormRuntimeSchedule.RuntimeTransformBlockOffsets[0], 0x110u);
    EXPECT_EQ(contract.StormRuntimeSchedule.RuntimeTransformBlockOffsets[1], 0x140u);
    EXPECT_EQ(contract.StormRuntimeSchedule.DescriptorSlotIndices[0], 0u);
    EXPECT_EQ(contract.StormRuntimeSchedule.DescriptorSlotIndices[1], 1u);
    EXPECT_EQ(contract.StormRuntimeSchedule.SchedulerFunctionAddress,
              contract.RenderRecordScheduler.FunctionAddress);
    EXPECT_EQ(contract.StormRuntimeSchedule.SchedulerBaseLiteralAddress, 0x00459F84u);
    EXPECT_EQ(contract.StormRuntimeSchedule.SchedulerBaseAddress, 0x005C0BA8u);
    EXPECT_EQ(contract.StormRuntimeSchedule.SchedulerCategory, 2u);
    EXPECT_EQ(contract.StormRuntimeSchedule.SchedulerAuxValue, 0u);
    EXPECT_TRUE(contract.StormRuntimeSchedule.UsesRenderRecordSchedulerInsteadOfSubmitWrapper);
    EXPECT_TRUE(contract.StormRuntimeSchedule.ActiveSchedulingResolved);

    EXPECT_EQ(contract.EnvironmentVector.PrepFunctionAddress, 0x004594E0u);
    EXPECT_EQ(contract.EnvironmentVector.PrepFunctionEndAddress, 0x004596AFu);
    EXPECT_EQ(contract.EnvironmentVector.PrepGlobalAngleStateAddress, 0x00587958u);
    EXPECT_EQ(contract.EnvironmentVector.PrepGlobalAngleHalfwordOffset, 0x0Cu);
    EXPECT_EQ(contract.EnvironmentVector.PrepGlobalEnvironmentStateAddress, 0x00531EB4u);
    EXPECT_EQ(contract.EnvironmentVector.PrepGlobalVectorScaleFloatOffset, 0x1Cu);
    EXPECT_EQ(contract.EnvironmentVector.PrepOutputVectorXOffset, 0x3194u);
    EXPECT_EQ(contract.EnvironmentVector.PrepOutputVectorYOffset, 0x3198u);
    EXPECT_EQ(contract.EnvironmentVector.PrepOutputVectorZOffset, 0x319Cu);
    EXPECT_EQ(contract.EnvironmentVector.PrepVectorSubmitTargetOffset, 0x327Cu);
    EXPECT_EQ(contract.EnvironmentVector.PrepVectorSubmitContextOffset, 0x01B8u);
    EXPECT_EQ(contract.EnvironmentVector.PrepSmoothingGateAddress, 0x0037571Cu);
    EXPECT_EQ(contract.EnvironmentVector.PrepSmoothingHelperAddress, 0x0036E168u);
    EXPECT_EQ(contract.EnvironmentVector.PrepSinHelperAddress, 0x002CFCA0u);
    EXPECT_EQ(contract.EnvironmentVector.PrepCosHelperAddress, 0x00338F60u);
    EXPECT_EQ(contract.EnvironmentVector.PrepSubmitPositiveVectorAddress, 0x0047D578u);
    EXPECT_EQ(contract.EnvironmentVector.PrepSubmitNegativeVectorAddress, 0x0047D600u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFunctionAddress, 0x002E4660u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFunctionEndAddress, 0x002E47BFu);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceObjectPointerOffset, 0x340Cu);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceTableHelperAddress, 0x003373B8u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceTableHelperIndex, 0u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceRecordCount, 4u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceRecordStrideBytes, 0x0Cu);
    EXPECT_EQ(contract.EnvironmentVector.ResolverSourceComponentCount, 3u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverOutputRecordStrideBytes, 0x10u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverOutputComponentCount, 4u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverOutputAlphaWord, 0x3F800000u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverStateByteOffset, 0x31B0u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverTargetLightSettingOffset, 0x3237u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverTargetLightSettingInvalidValue, 0xFFu);
    EXPECT_EQ(contract.EnvironmentVector.ResolverActiveOrTargetRecordIndex, 1u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFallbackGlobalStateAddress, 0x00531EB4u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFallbackBlendFromIndexOffset, 0x04u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFallbackBlendToIndexOffset, 0x05u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverFallbackBlendWeightFloatOffset, 0x28u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverColorPackScaleWord, 0x437F0000u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverPackedColorOutputOffset, 0x5B8u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverPackedColorModeByteOffset, 0x5BCu);
    EXPECT_EQ(contract.EnvironmentVector.ResolverPackedColorModeValue, 3u);
    ASSERT_EQ(contract.EnvironmentVector.ResolverPackedColorCallsiteAddresses.size(), 4u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverPackedColorCallsiteAddresses[0], 0x002E3320u);
    EXPECT_EQ(contract.EnvironmentVector.ResolverPackedColorCallsiteAddresses[3], 0x00459C1Cu);

    EXPECT_EQ(contract.EnvironmentLightSettingState.SurfaceTypeGetterAddress, 0x002C1E10u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.SurfaceTypeFieldReaderAddress, 0x00322088u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.SurfaceTypeLightSettingFieldId, 1u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.SurfaceTypeRawIndexLeftShift, 21u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.SurfaceTypeRawIndexRightShift, 27u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.PlayerFloorLightSettingGetterCallsiteAddress, 0x0032F140u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.PlayerFloorChangeHelperCallsiteAddress, 0x0032F14Cu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.ChangeHelperAddress, 0x0032B13Cu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionResetHelperAddress, 0x004B8FC0u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionResetCallerAddress, 0x002D0AFCu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeRequestHelperAddress, 0x0033B880u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.RequestHelperAddress, 0x00316D74u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.CameraWaterRequestCallsiteAddress, 0x002D09D8u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.PlayEnvironmentBaseOffset, 0x3000u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.EnvironmentStateByteOffset, 0x31B0u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionStateByteOffset, 0x3234u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.CurrentLightSettingOffset, 0x3235u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.PreviousLightSettingOffset, 0x3236u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TargetLightSettingOffset, 0x3237u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.BlendWeightFloatOffset, 0x3258u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.NormalizeThreshold, 31u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.NormalizeFallbackValue, 0u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TargetInvalidValue, 0xFFu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.BlendWeightZeroWord, 0u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.BlendWeightOneWord, 0x3F800000u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.FallbackGlobalStateAddress, 0x00531EB4u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.FallbackGlobalPreviousByteOffset, 0x01u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.FallbackMirrorCurrentByteOffset, 0x31B1u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.FallbackMirrorPreviousByteOffset, 0x31B2u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeCurrentOffset, 0x31B1u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeTargetOffset, 0x31B2u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeBlendActiveOffset, 0x31B3u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeBlendRemainingHalfwordOffset, 0x31B4u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.TransitionModeBlendDurationHalfwordOffset, 0x31B6u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.DecompileConsumerScanCandidateCount, 12u);
    EXPECT_TRUE(contract.EnvironmentLightSettingState.TransitionModeRequestHelperWritesModeState);
    EXPECT_FALSE(contract.EnvironmentLightSettingState.DecompileConsumerScanFoundFinalScenePacketConsumer);
    EXPECT_FALSE(contract.EnvironmentLightSettingState.FinalScenePacketConsumerResolved);
    ASSERT_EQ(contract.EnvironmentLightSettingState.RequestHelperCallsiteAddresses.size(), 6u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.RequestHelperCallsiteAddresses[0], 0x002D09D8u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.RequestHelperCallsiteAddresses[5], 0x003B7220u);
    ASSERT_EQ(contract.EnvironmentLightSettingState.StateMachineHelperCandidateAddresses.size(), 2u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.StateMachineHelperCandidateAddresses[0], 0x0032B13Cu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.StateMachineHelperCandidateAddresses[1], 0x004B8FC0u);
    ASSERT_EQ(contract.EnvironmentLightSettingState.ExcludedActorOrCutsceneWriterCandidateAddresses.size(), 10u);
    EXPECT_EQ(contract.EnvironmentLightSettingState.ExcludedActorOrCutsceneWriterCandidateAddresses[0], 0x001317DCu);
    EXPECT_EQ(contract.EnvironmentLightSettingState.ExcludedActorOrCutsceneWriterCandidateAddresses[9], 0x003D374Cu);

    EXPECT_EQ(contract.ZsiLightSettingsRecord.CommandId, 0x0Fu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.SceneCommandHandlerTableAddress, 0x0053CC84u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.SceneCommandHandlerAddress, 0x00379188u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.SceneCommandEntrySizeBytes, 8u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.SceneCommandCountByteOffset, 1u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.SceneCommandSegmentOffsetWordOffset, 4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.PlayStateLightSettingsCountOffset, 0x322Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.PlayStateLightSettingsListPointerOffset, 0x3230u);
    EXPECT_TRUE(contract.ZsiLightSettingsRecord.SceneCommandStoresNativeListPointer);
    ASSERT_EQ(contract.ZsiLightSettingsRecord.CandidateStartDeltas.size(), 4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.CandidateStartDeltas[0], 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.CandidateStartDeltas[3], 0x10u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.NativeRecordSizeBytes, 0x1Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.LegacyRecordSizeBytes, 0x16u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.NativeRecordLayoutName, "oot3d_pica_light_settings_record_0x1c");
    EXPECT_EQ(contract.ZsiLightSettingsRecord.LegacyRecordLayoutName, "legacy_light_settings_record_0x16_raw");
    EXPECT_EQ(contract.ZsiLightSettingsRecord.NativeEnvPrefixSizeBytes, 0x0Fu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ColorComponentOrder, "bgr_u8");
    EXPECT_EQ(contract.ZsiLightSettingsRecord.DirectionComponentEncoding, "signed_vec3_u8");
    EXPECT_EQ(contract.ZsiLightSettingsRecord.AmbientColorOffset, 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.Light0DirectionOffset, 0x03u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.Light0ColorOffset, 0x06u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.Light1DirectionOffset, 0x09u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.Light1ColorOffset, 0x0Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.Native3dsTailByteOffset, 0x0Fu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.FloatParam0Offset, 0x10u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.FloatParam1Offset, 0x14u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.TailWordOffset, 0x18u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.TailWordSizeBytes, 4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketDiffuse0ColorOffset, 0x04u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketDiffuse1ColorOffset, 0x0Au);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketAmbientPreviousTailByte0Offset, 0x1Au);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketAmbientPreviousTailByte1Offset, 0x1Bu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketAmbientCurrentByteOffset, 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketRecordIndexDelta, 2u);
    EXPECT_NE(contract.ZsiLightSettingsRecord.ActorPacketRecordSelectionSource.find("plus_two"),
              std::string::npos);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeConsumerAddress, 0x0045DD50u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeConsumerCallerAddress, 0x002E43CCu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeStateBasePlayOffset, 0x3190u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeOutputBasePlayOffset, 0x0A70u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimePauseFlagPlayOffset, 0x318Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeInitializedFlagPlayOffset, 0x3234u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeCurrentIndexPlayOffset, 0x3235u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimePreviousIndexPlayOffset, 0x3236u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTargetIndexPlayOffset, 0x3237u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeBlendWeightPlayOffset, 0x3258u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionTableAddress, 0x00531EFCu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionTableCodeBase, 0x00100000u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeCount, 5u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeStrideBytes, 0x36u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntryCount, 9u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntrySizeBytes, 6u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntryStartAngleOffset, 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntryEndAngleOffset, 0x02u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntryFromIndexOffset, 0x04u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionEntryToIndexOffset, 0x05u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeStateBasePlayOffset, 0x3190u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeCurrentRelativeOffset, 0x21u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeTargetRelativeOffset, 0x22u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendActiveRelativeOffset, 0x23u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendRemainingHalfwordRelativeOffset, 0x24u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendDurationHalfwordRelativeOffset, 0x26u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeCurrentOffset, 0x31B1u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeTargetOffset, 0x31B2u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendActiveOffset, 0x31B3u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendRemainingHalfwordOffset, 0x31B4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendDurationHalfwordOffset, 0x31B6u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeProjectionMatrixPlayOffset, 0x01DCu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeViewInitAddress, 0x002E5A38u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeViewDefaultNearLiteralAddress, 0x002E5B00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeViewDefaultFarLiteralAddress, 0x002E5B04u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeViewUpdateAddress, 0x002DE690u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeProjectionBuildAddress, 0x00471BA4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeSceneProjectionFarLiteralAddress, 0x00479290u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeSceneProjectionMatrixOffset, 0x94u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleWorkingStateAddress, 0x00587958u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleWorkingHalfwordOffset, 0x0Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleOutputStateAddress, 0x00588E58u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleOutputHalfwordOffset, 0xA8u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackStateAddress, 0x00531EB4u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackModeWeightFloatOffset, 0x28u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackFromIndexOffset, 0x04u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionGlobalFallbackToIndexOffset, 0x05u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeScalar0Offset, 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeScalar1Offset, 0x04u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimePackedHalfwordOffset, 0x08u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimePackedHalfwordMask, 0x03FFu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeTransitionRateShift, 10u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeAmbientColorOffset, 0x0Au);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeLight0DirectionOffset, 0x0Du);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeLight0ColorOffset, 0x10u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeLight1DirectionOffset, 0x13u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeLight1ColorOffset, 0x16u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeFogColorOffset, 0x19u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.ActorPacketPicaFogColorOffset, 0x0Du);
    EXPECT_NE(contract.ZsiLightSettingsRecord.ActorPacketPicaFogColorSource.find("GPUREG_FOG_COLOR"),
              std::string::npos);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeColorComponentCount, 3u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeDirectionComponentCount, 3u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadProducerAddress, 0x0045DD50u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadConsumerHandlerAddress, 0x00253A4Cu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotCount, 2u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotStrideBytes, 0x18u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotOffsets[0], 0x30u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSlotOffsets[1], 0x48u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionOffset, 0x00u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorOffset, 0x03u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSizeBytes, 0x06u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSourceOffsets[0], 0xB5u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSourceOffsets[1], 0xBBu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSourceOffsets[0], 0x33u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSourceOffsets[1], 0x4Bu);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionComponentCount, 3u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorComponentCount, 3u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionScaleDenominator, 127u);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionAngleBias, 0x8000u);
    EXPECT_FLOAT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSinScaleX, -120.0f);
    EXPECT_FLOAT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleY, 120.0f);
    EXPECT_FLOAT_EQ(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionCosScaleZ, 20.0f);
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeColorComponentOrder, "rgb_u8");
    EXPECT_EQ(contract.ZsiLightSettingsRecord.RuntimeDirectionComponentEncoding, "signed_vec3_s8");
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSource.find("0045dd50"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadSource.find("00253a4c"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadDirectionSource.find("sin_cos"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadColorSource.find("plus_two"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeTransitionActiveAngleSource.find("00587958"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeTransitionModeStateSource.find("play_plus_0x3190"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeTransitionBlendFormula.find("entry.end_angle"),
              std::string::npos);
    EXPECT_NE(contract.ZsiLightSettingsRecord.RuntimeTransitionModeBlendFormula.find("duration - remaining"),
              std::string::npos);
    EXPECT_TRUE(contract.ZsiLightSettingsRecord.RuntimeConsumerResolved);
    EXPECT_TRUE(contract.ZsiLightSettingsRecord.RuntimeActorVsCompactPayloadResolved);
    EXPECT_TRUE(contract.ZsiLightSettingsRecord.RuntimeTransitionTableDecodedFromCodeBin);
    EXPECT_FALSE(contract.ZsiLightSettingsRecord.FinalPacketWriterResolved);

    EXPECT_EQ(contract.SubmitManager.SubmitWrapperAddress, contract.ThunderUpdate.RuntimeSubmitHelperAddress);
    EXPECT_EQ(contract.SubmitManager.SubmitCoreAddress, 0x00367788u);
    EXPECT_EQ(contract.SubmitManager.SubmitRecordWriteAddress, 0x002C1AE8u);
    EXPECT_EQ(contract.SubmitManager.ManagerInitializerAddress, 0x0041706Cu);
    EXPECT_EQ(contract.SubmitManager.ManagerVtableAddress, 0x004EBD78u);
    EXPECT_EQ(contract.SubmitManager.ContextSubmitManagerOffset, 0x180u);
    EXPECT_EQ(contract.SubmitManager.ManagerStorageSizeBytes, 0x2170u);
    EXPECT_EQ(contract.SubmitManager.InitFlagAddress, 0x0055A21Cu);
    EXPECT_EQ(contract.SubmitManager.GlobalContextAddress, 0x005BE5B8u);
    EXPECT_EQ(contract.SubmitManager.SubmitManagerAddress, 0x005BE738u);
    EXPECT_EQ(contract.SubmitManager.SubmitManagerAddress,
              contract.SubmitManager.GlobalContextAddress + contract.SubmitManager.ContextSubmitManagerOffset);
    EXPECT_EQ(contract.SubmitManager.PrimaryQueueCountStorageOffset, 0x08u);
    EXPECT_EQ(contract.SubmitManager.PrimaryQueueStorageOffset, 0x2Cu);
    EXPECT_EQ(contract.SubmitManager.SecondaryQueueStorageOffset, 0x102Cu);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryQueueStorageOffset, 0x202Cu);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryQueueCapacity, 0x20u);
    EXPECT_EQ(contract.SubmitManager.SmallQueueCountOffset, 0x212Cu);
    EXPECT_EQ(contract.SubmitManager.SmallQueueStorageOffset, 0x2130u);
    EXPECT_EQ(contract.SubmitManager.SmallQueueCapacity, 8u);
    EXPECT_EQ(contract.SubmitManager.InitialRecordStateValue, 3u);
    EXPECT_EQ(contract.SubmitManager.ManagerModeFlagOffset, 0x04u);
    EXPECT_EQ(contract.SubmitManager.SecondaryQueueCountOffset, 0x0Cu);
    EXPECT_EQ(contract.SubmitManager.QueueCapacityOffset, 0x14u);
    EXPECT_EQ(contract.SubmitManager.PrimaryQueueCountPointerOffset, 0x18u);
    EXPECT_EQ(contract.SubmitManager.PrimaryQueueBaseOffset, 0x1Cu);
    EXPECT_EQ(contract.SubmitManager.SecondaryQueueBaseOffset, 0x24u);
    EXPECT_EQ(contract.SubmitManager.SubmitRecordStrideBytes, 8u);
    EXPECT_EQ(contract.SubmitManager.SubmitRecordRuntimePointerOffset, 0u);
    EXPECT_EQ(contract.SubmitManager.SubmitRecordValidByteOffset, 4u);
    EXPECT_EQ(contract.SubmitManager.SubmitRecordValidValue, 1u);
    EXPECT_EQ(contract.SubmitManager.RuntimeVtableSlotOffset, 0x08u);
    EXPECT_EQ(contract.SubmitManager.RuntimeSubmitCallbackVtableSlotOffset,
              contract.SubmitManager.RuntimeVtableSlotOffset);
    EXPECT_EQ(contract.SubmitManager.RuntimeRenderDrainVtableSlotOffset,
              contract.RenderRecordScheduler.RuntimeDispatchVtableSlotOffset);
    EXPECT_EQ(contract.SubmitManager.CallbackContext0Offset, 0x114u);
    EXPECT_EQ(contract.SubmitManager.CallbackContext1Offset, 0x174u);
    EXPECT_EQ(contract.SubmitManager.CallbackContext2Offset, 0x30u);
    EXPECT_EQ(contract.SubmitManager.Runtime1E4InitializerAddress, 0x002C4F00u);
    EXPECT_EQ(contract.SubmitManager.Runtime1E4VtableAddress, 0x004EBD60u);
    EXPECT_EQ(contract.SubmitManager.Runtime1E4SubmitCallbackAddress, 0x003F96BCu);
    EXPECT_EQ(contract.SubmitManager.Runtime1E4RenderDrainCallbackAddress,
              contract.RenderRecordScheduler.Runtime1E4DispatchFunctionAddress);
    EXPECT_EQ(contract.SubmitManager.Runtime28CInitializerAddress, 0x004970E0u);
    EXPECT_EQ(contract.SubmitManager.Runtime28CVtableAddress, 0x004EBE9Cu);
    EXPECT_EQ(contract.SubmitManager.Runtime28CSubmitCallbackAddress, 0x003FC2F8u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDrawCountSetterAddress, 0x00333294u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDrawPayloadOffset, contract.EffectDrawConsumer.RuntimeDrawPayloadOffset);
    EXPECT_EQ(contract.SubmitManager.RuntimeFlagsOffset, contract.EffectDrawConsumer.RuntimeFlagsOffset);
    EXPECT_EQ(contract.SubmitManager.FrameDrawPassAddress, 0x00300328u);
    EXPECT_EQ(contract.SubmitManager.FrameDrawPassManagerOffset, contract.SubmitManager.ContextSubmitManagerOffset);
    EXPECT_EQ(contract.SubmitManager.PrimarySecondaryPass0Address, 0x004224ACu);
    EXPECT_EQ(contract.SubmitManager.PrimarySecondaryPass1Address, 0x004224DCu);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryPass0Address, 0x002FAE00u);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryPass1Address, 0x002FAD1Cu);
    EXPECT_EQ(contract.SubmitManager.SmallQueueDrainAddress, 0x0042250Cu);
    EXPECT_EQ(contract.SubmitManager.RecordArrayPass0Address, 0x002FAE10u);
    EXPECT_EQ(contract.SubmitManager.RecordArrayPass1Address, 0x002FAD2Cu);
    EXPECT_EQ(contract.SubmitManager.RecordArrayCombinedPassAddress, 0x003FE374u);
    EXPECT_EQ(contract.SubmitManager.RecordArrayDepthSortAddress, 0x00422910u);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryRouteBeginAddress, 0x0032D5DCu);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryRouteEndAddress, 0x0032D5B8u);
    EXPECT_EQ(contract.SubmitManager.QueueResetAddress, 0x00417034u);
    EXPECT_EQ(contract.SubmitManager.AlternateQueueResetAddress, 0x0041ACACu);
    EXPECT_EQ(contract.SubmitManager.PrimaryQueuePointerOffset, 0x20u);
    EXPECT_EQ(contract.SubmitManager.SecondaryQueuePointerOffset, contract.SubmitManager.SecondaryQueueBaseOffset);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryQueueCountOffset, 0x10u);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryQueuePointerOffset, 0x28u);
    EXPECT_EQ(contract.SubmitManager.MainQueueCapacity, 0x200u);
    EXPECT_EQ(contract.SubmitManager.NormalRouteModeValue, 0u);
    EXPECT_EQ(contract.SubmitManager.AuxiliaryRouteModeValue, 2u);
    EXPECT_EQ(contract.SubmitManager.FramePrimarySecondaryGateArgumentValue, 0u);
    EXPECT_EQ(contract.SubmitManager.RecordArrayDefaultModeArgumentValue, 0u);
    EXPECT_EQ(contract.SubmitManager.RuntimeRecordStateDrawHandleValue, 0u);
    EXPECT_EQ(contract.SubmitManager.RuntimeRecordStateCallbackValue, 1u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDrawGateByteOffset, 0xADu);
    EXPECT_EQ(contract.SubmitManager.RuntimePrimaryDrawHandleOffset, 0x14u);
    EXPECT_EQ(contract.SubmitManager.RuntimeSecondaryDrawHandleOffset, 0x18u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateFactoryAddress, 0x002C1AF8u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateBlockSizeBytes, 0x30u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateCopyOffsets[0], 0x54u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateCopyOffsets[1], 0x84u);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateCopyOffsets[2], 0x0Cu);
    EXPECT_EQ(contract.SubmitManager.RuntimeDefaultDrawStateCopyOffsets[5], 0xB4u);
    EXPECT_TRUE(contract.SubmitManager.RuntimeDefaultDrawStateCopyCoversSubmitDrawHandleOffsets);
    EXPECT_FALSE(contract.SubmitManager.Runtime1E4InitializerInstallsActiveSubmitDrawHandles);
    EXPECT_EQ(contract.SubmitManager.RuntimeAnimatedDrawHandleResolverAddress, 0x003687A8u);
    EXPECT_EQ(contract.SubmitManager.RuntimeAnimatedDrawHandleResolverPrimaryHandleOffset,
              contract.SubmitManager.RuntimePrimaryDrawHandleOffset);
    EXPECT_EQ(contract.SubmitManager.RuntimeAnimatedDrawHandleResolverPacketPrepSourceOffset,
              contract.DrawHandleSubmit.MaterialPacketPointerOffset);
    EXPECT_TRUE(contract.SubmitManager.RuntimeAnimatedDrawHandleResolverReturnsPacketPrepSource);
    EXPECT_EQ(contract.SubmitManager.RuntimeAnimatedDrawHandleGateByteOffset, 0x1B4u);
    EXPECT_EQ(contract.SubmitManager.RuntimeState1ResolverAddress, 0x002EA854u);
    EXPECT_EQ(contract.SubmitManager.RuntimeState1GateWordOffset, 0x170u);
    EXPECT_EQ(contract.SubmitManager.RuntimeState1VtableSlotOffset, 0x0Cu);
    EXPECT_EQ(contract.SubmitManager.RuntimeState1FlagsMask, 0x80u);
    EXPECT_EQ(contract.SubmitManager.DrawHandleSubmitAddress, contract.DrawHandleSubmit.FunctionAddress);
    EXPECT_EQ(contract.SubmitManager.DrawHandlePassByteOffset, contract.DrawHandleSubmit.SubmittedPassByteOffset);
    EXPECT_EQ(contract.SubmitManager.DrawHandleMaterialPacketOffset, contract.DrawHandleSubmit.MaterialPacketPointerOffset);
    EXPECT_EQ(contract.SubmitManager.DrawHandleSubmittedPass0Value, 0u);
    EXPECT_EQ(contract.SubmitManager.DrawHandleSubmittedPass1Value, 1u);
    EXPECT_EQ(contract.SubmitManager.DrawHandlePacketPrepAddress, contract.PacketPrep.FunctionAddress);
    EXPECT_EQ(contract.SubmitManager.DirectSubmitCallerCount, 57u);
    EXPECT_TRUE(contract.SubmitManager.SubmitRecordWriteStoresRuntimePointerAndValidByteOnly);
    EXPECT_TRUE(contract.SubmitManager.SubmitCoreInvokesRuntimeVtableSlot);
    EXPECT_TRUE(contract.SubmitManager.SubmitCoreUsesSubmitCallbackVtableSlot);
    EXPECT_FALSE(contract.SubmitManager.SubmitCoreUsesRenderDrainVtableSlot);
    EXPECT_TRUE(contract.SubmitManager.SubmitCorePassesNativeCallbackContexts);
    EXPECT_FALSE(contract.SubmitManager.SubmitCoreDirectlyWritesPacketPrepSource);
    EXPECT_FALSE(contract.SubmitManager.SubmitCoreDirectlyWritesDrawHandlePacketPrepSource);
    EXPECT_FALSE(contract.SubmitManager.SubmitManagerConnectsToEffectDrawConsumer);
    EXPECT_FALSE(contract.SubmitManager.SubmitManagerConnectsToType6DrawCommand);
    EXPECT_FALSE(contract.SubmitManager.PacketPrepBackingWriterResolved);
    EXPECT_NE(contract.SubmitManager.PacketPrepBackingWriterStatus.find("00367788"), std::string::npos);
    EXPECT_NE(contract.SubmitManager.PacketPrepBackingWriterStatus.find("002C4F00"), std::string::npos);
    EXPECT_NE(contract.SubmitManager.PacketPrepBackingWriterStatus.find("vtable+0x0C"), std::string::npos);
    EXPECT_NE(contract.SubmitManager.PacketPrepBackingWriterStatus.find("002FC694"), std::string::npos);
    EXPECT_NE(contract.SubmitManager.PacketPrepBackingWriterStatus.find("003130A4"), std::string::npos);
    EXPECT_EQ(contract.SubmitManager.WeatherParticleSubmitGameplayDrawCallsiteAddress, 0x002E2CCCu);
    EXPECT_EQ(contract.SubmitManager.WeatherParticleSubmitFunctionAddress, 0x00463544u);
    EXPECT_EQ(contract.SubmitManager.WeatherParticleSubmitFunctionEndAddress, 0x00463CE7u);
    EXPECT_EQ(contract.SubmitManager.WeatherParticleSubmitPlayGateByteOffset, 0x326Fu);
    EXPECT_TRUE(contract.SubmitManager.WeatherParticleSubmitPlayGateRequiresNonZero);
    EXPECT_TRUE(contract.SubmitManager.WeatherParticleSubmitGateAlsoParticleLoopCount);
    EXPECT_EQ(contract.SubmitManager.WeatherParticleKankyoObjectBasePlayOffset, 0x3190u);
    ASSERT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes.size(), 2u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[0].Role, "rain");
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[0].SubmitCallsiteAddress, 0x00463A68u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[0].RuntimePointerPlayOffset, 0x3378u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[0].RuntimePointerKankyoOffset, 0x1E8u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[1].Role, "ripple");
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[1].SubmitCallsiteAddress, 0x00463CD8u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[1].RuntimePointerPlayOffset, 0x3380u);
    EXPECT_EQ(contract.SubmitManager.VerifiedWeatherRuntimeSubmitRoutes[1].RuntimePointerKankyoOffset, 0x1F0u);
    ASSERT_EQ(contract.SubmitManager.VerifiedKankyoSubmitCallsiteAddresses.size(), 1u);
    EXPECT_EQ(contract.SubmitManager.VerifiedKankyoSubmitCallsiteAddresses[0],
              contract.ThunderUpdate.RuntimeSubmitCallsiteAddress);
    EXPECT_EQ(contract.SubmitManager.VerifiedKankyoSubmittedRuntimeOffset,
              contract.ThunderUpdate.RuntimeInstanceOffset);
    EXPECT_EQ(contract.SubmitManager.VerifiedKankyoSubmittedRuntimeStrideBytes,
              contract.ThunderUpdate.RuntimeInstanceStrideBytes);
    EXPECT_EQ(contract.SubmitManager.VerifiedKankyoSubmittedRuntimeSlotCount, contract.ThunderUpdate.SlotCount);
    EXPECT_TRUE(contract.SubmitManager.UnresolvedKankyoRuntimeSubmitOffsets.empty());
    EXPECT_TRUE(contract.SubmitManager.NonThunderKankyoRuntimeSubmitsResolved);
    EXPECT_EQ(contract.Runtime28CParticleBatch.CallbackAddress,
              contract.SubmitManager.Runtime28CSubmitCallbackAddress);
    EXPECT_EQ(contract.Runtime28CParticleBatch.CallbackEndAddress, 0x003FCB1Fu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeFlagsOffset, contract.SubmitManager.RuntimeFlagsOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeDescriptorPointerOffset,
              contract.RuntimeSourceVector.DescriptorPointerOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeTransformBlockOffset,
              contract.RuntimeSourceVector.DynamicTransformBlockOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeAverageDepthOffset,
              contract.PacketPrep.PreparedIntensityOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimePositionArrayPointerOffset, 0x1E4u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeMatrixArrayPointerOffset, 0x1E8u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeColorArrayPointerOffset, 0x1F0u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeTexcoordArrayPointerOffset, 0x1F4u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeBatchCountOffset, 0x1FCu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeBatchCapacityOffset, 0x1F8u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeLocalVectorArrayPointerOffset, 0x1ECu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeAveragePositionBaseOffset, 0x280u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.RuntimeAveragePositionComponentCount, 3u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DescriptorFlagsOffset,
              contract.DescriptorMaterialization.DescriptorFlagsOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DescriptorTypeOffset,
              contract.RuntimeSourceVector.DescriptorTypeOffset);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DescriptorTypeDirectMatrixValue,
              contract.RuntimeSourceVector.DescriptorTypeDirectMatrixValue);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DescriptorTypeBillboardMatrixValue,
              contract.RuntimeSourceVector.DescriptorTypeDerivedMatrixValue);
    EXPECT_EQ(contract.Runtime28CParticleBatch.MatrixArrayRecordStrideBytes, 0x30u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.PositionRecordStrideBytes, 0x0Cu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.LocalVectorRecordStrideBytes, 0x0Cu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.ColorRecordStrideBytes, 0x10u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.TexcoordRecordStrideBytes, 0x08u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.QuadVertexCount, 4u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DrawCountPerVisibleBatch, 6u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DrawCountSetterAddress,
              contract.SubmitManager.RuntimeDrawCountSetterAddress);
    EXPECT_EQ(contract.Runtime28CParticleBatch.OutputPositionBufferResolverAddress, 0x00333270u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.OutputColorBufferResolverAddress, 0x003331ECu);
    EXPECT_EQ(contract.Runtime28CParticleBatch.OutputTexcoordBufferResolverAddress, 0x00333070u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.OptionalSecondaryPositionBufferResolverAddress, 0x00408C80u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.SkipSecondaryPositionRuntimeFlagMask, 0x00020000u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.SkipColorRuntimeFlagMask, 0x00040000u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.SkipTexcoordRuntimeFlagMask, 0x00080000u);
    EXPECT_EQ(contract.Runtime28CParticleBatch.DescriptorSkipSecondaryPositionFlagMask, 0x80u);
    EXPECT_TRUE(contract.Runtime28CParticleBatch.ClearsBatchCountAfterEmit);
    EXPECT_FALSE(contract.Runtime28CParticleBatch.ResolvesNonThunderSubmitCallsite);
    EXPECT_TRUE(contract.Runtime28CParticleBatch.EnqueueWriterResolved);
    EXPECT_TRUE(contract.Runtime28CParticleBatch.ObjectTransformReplicatesColorForAllQuadCorners);
    ASSERT_EQ(contract.SubmitManager.VtableSlots.size(), 10u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[0].Role, "record_array_pass_0");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[0].SlotOffset, 0x08u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[0].FunctionAddress, contract.SubmitManager.RecordArrayPass0Address);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[3].Role, "record_array_depth_sort");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[3].FunctionAddress, contract.SubmitManager.RecordArrayDepthSortAddress);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[4].Role, "manager_cleanup_release");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[4].SlotOffset, 0x24u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[4].FunctionAddress, 0x003FB2A8u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[6].Role, "descriptor_light_list_build");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[6].SlotOffset, 0x30u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[6].FunctionAddress, 0x003F9B5Cu);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[7].Role, "runtime_three_slot_light_packet_pack");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[7].FunctionAddress, 0x003FA5D0u);
    EXPECT_EQ(contract.SubmitManager.VtableSlots[9].Role, "render_state_setup");
    EXPECT_EQ(contract.SubmitManager.VtableSlots[9].FunctionAddress, 0x003FAD68u);
}

TEST(Oot3dNativeAssets, MaterializesNativeKankyoMoonDrawRoute) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    auto& background = scene.EnvironmentBackground;
    background.RuntimeEnvironmentTimeInputAvailable = true;
    background.RuntimeEnvironmentSkyboxTime = 0x8000;
    auto& moon = background.NativeKankyoMoon;
    moon.Available = true;
    moon.NativeInitResolved = true;
    moon.TextureInputsResolved = true;
    moon.ReadyForBackendInput = true;
    moon.LayerCount = 3;

    constexpr std::array<uint32_t, 3> wraps = { 0x812F, 0x8370, 0x8370 };
    for (uint32_t index = 0; index < 3; ++index) {
        const std::string name = "fine_moon" + std::to_string(index);
        ThreeDsRecomp::Oot3d::Oot3dNativeKankyoMoonLayerState layer;
        layer.TextureName = name;
        layer.GeometryTemplateHalfExtent = 0.5f;
        layer.MinMagFilter = 0x2601;
        layer.WrapS = wraps[index];
        layer.WrapT = wraps[index];
        moon.Layers.push_back(layer);
        background.NativeKankyoExtraCtxbNames.push_back(name);

        ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
        texture.Name = name;
        texture.Width = 1;
        texture.Height = 1;
        texture.Rgba8Decoded = true;
        texture.HasNativeAlpha = index == 0;
        texture.Rgba8 = { 255, 255, 255, 255 };
        background.NativeKankyoExtraCtxbTextures.push_back(std::move(texture));
    }

    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoMoonRuntime(
        scene, { 0.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, 1.0 });

    ASSERT_TRUE(moon.RuntimeTransformResolved);
    ASSERT_TRUE(moon.NativeVisibleBackendSubmitResolved);
    ASSERT_EQ(scene.MoonModels.size(), 3u);
    EXPECT_NE(scene.MoonModels[0].Name.find("fine_moon1"), std::string::npos);
    EXPECT_NE(scene.MoonModels[1].Name.find("fine_moon0"), std::string::npos);
    EXPECT_NE(scene.MoonModels[2].Name.find("fine_moon2"), std::string::npos);
    EXPECT_DOUBLE_EQ(moon.RuntimeCelestialVector.X, 0.0);
    EXPECT_DOUBLE_EQ(moon.RuntimeCelestialVector.Y, 3000.0);
    EXPECT_DOUBLE_EQ(moon.RuntimeCelestialVector.Z, 500.0);
    EXPECT_FLOAT_EQ(moon.Layers[0].RuntimeScale, 640.0f);
    EXPECT_FLOAT_EQ(moon.Layers[1].RuntimeScale, 1280.0f);
    EXPECT_FLOAT_EQ(moon.Layers[2].RuntimeScale, 1280.0f);
    EXPECT_FLOAT_EQ(moon.Layers[0].GeometryTemplateHalfExtent, 0.5f);
    EXPECT_FLOAT_EQ(moon.Layers[0].RuntimeUvMax, 1.0f);
    EXPECT_FLOAT_EQ(moon.Layers[1].RuntimeUvMax, 2.0f);
    EXPECT_FLOAT_EQ(moon.Layers[2].RuntimeUvMax, 2.0f);
    EXPECT_FALSE(moon.Layers[0].RuntimeAdditiveBlend);
    EXPECT_TRUE(moon.Layers[1].RuntimeAdditiveBlend);
    EXPECT_TRUE(moon.Layers[2].RuntimeAdditiveBlend);

    for (const auto& model : scene.MoonModels) {
        ASSERT_EQ(model.Batches.size(), 1u);
        ASSERT_EQ(model.Batches[0].Vertices.size(), 6u);
        const auto& material = model.Batches[0].Material;
        EXPECT_FALSE(material.DepthTest);
        EXPECT_FALSE(material.DepthWrite);
        EXPECT_TRUE(material.NativeSamplerStateDecoded);
        EXPECT_TRUE(material.NativeBlendStateSupported);
        EXPECT_TRUE(material.NativePicaFogOverrideDecoded);
        EXPECT_FALSE(material.NativePicaFogEnabled);
    }
    EXPECT_EQ(scene.MoonModels[0].Batches[0].Material.BlendDst, 0x0001);
    EXPECT_EQ(scene.MoonModels[1].Batches[0].Material.BlendDst, 0x0303);
    EXPECT_TRUE(scene.MoonModels[1].Batches[0].Material.AlphaTest);
    EXPECT_EQ(scene.MoonModels[1].Batches[0].Material.AlphaFunction, 0x0204);

    const auto& coreVertices = scene.MoonModels[1].Batches[0].Vertices;
    ASSERT_GE(coreVertices.size(), 3u);
    float minX = coreVertices[0].Position.X;
    float maxX = minX;
    for (const auto& vertex : coreVertices) {
        minX = std::min(minX, vertex.Position.X);
        maxX = std::max(maxX, vertex.Position.X);
    }
    EXPECT_FLOAT_EQ(maxX - minX, 640.0f);
}

TEST(Oot3dNativeAssets, BlendsNativeKankyoProfileAttributeBuffers) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel base;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel current;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel next;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch baseBatch;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch currentBatch;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch nextBatch;
    baseBatch.MeshIndex = currentBatch.MeshIndex = nextBatch.MeshIndex = 2;
    baseBatch.ShapeIndex = currentBatch.ShapeIndex = nextBatch.ShapeIndex = 4;
    baseBatch.PrimitiveIndex = currentBatch.PrimitiveIndex = nextBatch.PrimitiveIndex = 6;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderVertex baseVertex;
    baseVertex.Position = { 100.0f, 200.0f, 300.0f };
    baseVertex.Normal = { 0.0f, 0.0f, 1.0f };
    baseVertex.Uv0 = { 0.75f, 0.125f };
    baseVertex.Uv1 = { 0.5f, 0.25f };
    baseVertex.NativeSourceUv0 = { 0.875f, 0.625f };
    baseVertex.Color = { 1, 2, 3, 4 };
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderVertex currentVertex;
    currentVertex.Position = { 0.0f, 4.0f, 8.0f };
    currentVertex.Normal = { 0.0f, 1.0f, 0.0f };
    currentVertex.Uv0 = { 0.0f, 0.5f };
    currentVertex.Uv1 = { 0.25f, 0.75f };
    currentVertex.NativeSourceUv0 = { 0.0f, 1.0f };
    currentVertex.Color = { 10, 20, 30, 40 };
    auto nextVertex = currentVertex;
    nextVertex.Position = { 8.0f, 12.0f, 16.0f };
    nextVertex.Normal = { 1.0f, 0.0f, 0.0f };
    nextVertex.Uv0 = { 1.0f, 1.0f };
    nextVertex.Uv1 = { 0.75f, 0.25f };
    nextVertex.NativeSourceUv0 = { 1.0f, 0.0f };
    nextVertex.Color = { 13, 25, 39, 51 };
    baseBatch.Vertices.push_back(baseVertex);
    currentBatch.Vertices.push_back(currentVertex);
    nextBatch.Vertices.push_back(nextVertex);
    base.Batches.push_back(baseBatch);
    current.Batches.push_back(currentBatch);
    next.Batches.push_back(nextBatch);

    ASSERT_TRUE(ThreeDsRecomp::Oot3d::BlendOot3dNativeKankyoProfileRenderModels(
        base, current, next, 3, 0, 0.25f));
    const auto& blended = base.Batches[0].Vertices[0];
    EXPECT_FLOAT_EQ(blended.Position.X, 100.0f);
    EXPECT_FLOAT_EQ(blended.Position.Y, 200.0f);
    EXPECT_FLOAT_EQ(blended.Position.Z, 300.0f);
    EXPECT_FLOAT_EQ(blended.Normal.Z, 1.0f);
    EXPECT_FLOAT_EQ(blended.Uv0.X, 0.75f);
    EXPECT_FLOAT_EQ(blended.Uv1.X, 0.5f);
    EXPECT_FLOAT_EQ(blended.NativeSourceUv0.X, 0.875f);
    EXPECT_EQ(blended.Color.R, 10u);
    EXPECT_EQ(blended.Color.G, 21u);
    EXPECT_EQ(blended.Color.B, 32u);
    EXPECT_EQ(blended.Color.A, 42u);
    EXPECT_TRUE(base.NativeKankyoProfileAttributeBlendApplied);
    EXPECT_EQ(base.NativeKankyoProfileBlendCurrentIndex, 3);
    EXPECT_EQ(base.NativeKankyoProfileBlendNextIndex, 0);
    EXPECT_FLOAT_EQ(base.NativeKankyoProfileBlendWeight, 0.25f);

    auto incompatible = next;
    incompatible.Batches[0].PrimitiveIndex = 7;
    EXPECT_FALSE(ThreeDsRecomp::Oot3d::BlendOot3dNativeKankyoProfileRenderModels(
        base, current, incompatible, 3, 0, 0.5f));
}

TEST(Oot3dNativeAssets, ResolvesNativeKankyoSpecialChildSelectorAlpha) {
    using ThreeDsRecomp::Oot3d::ResolveOot3dNativeKankyoSpecialChildAlpha;

    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(3, 3, 80), 255u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(3, 0, 60), 195u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(0, 3, 60), 60u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(0, 1, 60), 0u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(3, 4, 60), 0u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(0, 7, 60), 255u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSpecialChildAlpha(-1, 0, 60), 255u);
}

TEST(Oot3dNativeAssets, SelectsNativeKankyoMoonCtxbBaseFromSkyboxInitBranch) {
    ThreeDsRecomp::Oot3d::Oot3dNativeEnvironmentBackgroundState background;
    background.SkyboxCommandArgument = 8;
    for (uint32_t index = 2; index < 5; ++index) {
        background.NativeKankyoExtraDrawCtxbTypeLocalIndices.push_back(index);
        background.NativeKankyoExtraCtxbNames.push_back(
            "fine_moon" + std::to_string(index - 2));
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
        texture.Width = index == 2 ? 128 : 64;
        texture.Height = texture.Width;
        background.NativeKankyoExtraCtxbTextures.push_back(std::move(texture));
    }

    const auto blueOnlyMoon =
        ThreeDsRecomp::Oot3d::NativeRenderResolveKankyoMoonState(background);
    ASSERT_TRUE(blueOnlyMoon.ReadyForBackendInput);
    EXPECT_EQ(blueOnlyMoon.MoonInitCallsiteAddress, 0x002E4B14u);
    EXPECT_EQ(blueOnlyMoon.CtxbBaseTypeLocalIndex, 2u);
    ASSERT_EQ(blueOnlyMoon.Layers.size(), 3u);
    EXPECT_EQ(blueOnlyMoon.Layers.front().TextureName, "fine_moon0");

    background.SkyboxCommandArgument = 4;
    const auto noMoon = ThreeDsRecomp::Oot3d::NativeRenderResolveKankyoMoonState(background);
    EXPECT_FALSE(noMoon.NativeInitResolved);
    EXPECT_FALSE(noMoon.Available);
    EXPECT_TRUE(noMoon.Layers.empty());
}

TEST(Oot3dNativeAssets, ResolvesNativeKankyoSecondaryCloudAlpha) {
    using ThreeDsRecomp::Oot3d::ResolveOot3dNativeKankyoSecondaryCloudAlpha;

    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x23, 3, 0, 84), 0u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x24, 8, 11, 84), 0u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x23, 0, 7, 60), 195u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x24, 0, 11, 60), 60u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x23, 0, 4, 60), 0u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x25, 0, 3, 60), 255u);
    EXPECT_EQ(ResolveOot3dNativeKankyoSecondaryCloudAlpha(0x23, -1, 0, 60), 255u);
}

TEST(Oot3dNativeAssets, MaterializesNativeKankyoSkyAndSunTransforms) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    scene.EnvironmentBackground.RuntimeEnvironmentTimeInputAvailable = true;
    scene.EnvironmentBackground.RuntimeEnvironmentSkyboxTime = 0x8000;

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel sky;
    sky.NativeKankyoRole = ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::SkyBackground;
    sky.ModelToWorld = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderScaleTranslateTransform(320.0, {});
    sky.Batches.emplace_back();
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel cloud = sky;
    cloud.NativeKankyoRole = ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::Cloud;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel star = sky;
    star.NativeKankyoRole = ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::Star;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel sun;
    sun.NativeKankyoRole = ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::Sun;
    sun.Batches.emplace_back();
    scene.EnvironmentModels = { sky, cloud, star, sun };

    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoSkyRuntime(
        scene, { 10.0, 20.0, 30.0 }, { 11.0, 20.0, 30.0 }, { 0.0, 1.0, 0.0 });

    for (size_t index = 0; index < 3; ++index) {
        const auto& model = scene.EnvironmentModels[index];
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[0][0], 320.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[0][1], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[0][2], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[1][0], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[1][1], 320.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[1][2], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[2][0], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[2][1], 0.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[2][2], 320.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[0][3], 10.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[1][3], 20.0f);
        EXPECT_FLOAT_EQ(model.ModelToWorld.M[2][3], 30.0f);
        EXPECT_TRUE(model.NativeKankyoRuntimeTransformMaterialized);
        ASSERT_EQ(model.Batches.size(), 1u);
        EXPECT_TRUE(model.Batches[0].Material.NativePicaFogOverrideDecoded);
        EXPECT_FALSE(model.Batches[0].Material.NativePicaFogEnabled);
    }
    const auto& transformedSun = scene.EnvironmentModels[3];
    EXPECT_FLOAT_EQ(transformedSun.ModelToWorld.M[0][3], 10.0f);
    EXPECT_FLOAT_EQ(transformedSun.ModelToWorld.M[1][3], 3020.0f);
    EXPECT_FLOAT_EQ(transformedSun.ModelToWorld.M[2][3], 530.0f);
    for (size_t column = 0; column < 3; ++column) {
        const double scale = std::sqrt(
            transformedSun.ModelToWorld.M[0][column] * transformedSun.ModelToWorld.M[0][column] +
            transformedSun.ModelToWorld.M[1][column] * transformedSun.ModelToWorld.M[1][column] +
            transformedSun.ModelToWorld.M[2][column] * transformedSun.ModelToWorld.M[2][column]);
        EXPECT_NEAR(scale, 20.0, 0.0001);
    }
    EXPECT_TRUE(transformedSun.NativeKankyoRuntimeTransformMaterialized);
    ASSERT_EQ(transformedSun.Batches.size(), 1u);
    EXPECT_TRUE(transformedSun.Batches[0].Material.NativePicaFogOverrideDecoded);
    EXPECT_FALSE(transformedSun.Batches[0].Material.NativePicaFogEnabled);
}

TEST(Oot3dNativeAssets, ResolvesPicaTextureAlphaConstantChainAndRuntimeBlend) {
    auto model = SyntheticTextureEnvAlphaConstantModel();
    auto renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(model);

    ASSERT_EQ(renderModel.Batches.size(), 1u);
    auto& material = renderModel.Batches[0].Material;
    EXPECT_TRUE(material.TextureEnvProgram.Texture0PrimaryColorAlphaModulateResolved);
    EXPECT_TRUE(material.TextureEnvProgram.AlphaMultiplierResolved);
    EXPECT_EQ(material.TextureEnvProgram.AlphaMultiplierStageCount, 1u);
    EXPECT_NEAR(material.TextureEnvProgram.AlphaMultiplier, 179.0f / 255.0f, 1.0e-6f);
    EXPECT_FALSE(material.NativeRuntimeVertexAlphaBlend);

    EXPECT_EQ(ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
                  renderModel, 5, { 255, 255, 255, 128 }, "synthetic_runtime_alpha"),
              1u);
    EXPECT_NEAR(material.TextureEnvProgram.AlphaMultiplier, 128.0f / 255.0f, 1.0e-6f);
    EXPECT_TRUE(material.NativeRuntimeVertexAlphaBlend);

    EXPECT_EQ(ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
                  renderModel, 5, { 255, 255, 255, 255 }, "synthetic_runtime_opaque"),
              1u);
    EXPECT_FALSE(material.NativeRuntimeVertexAlphaBlend);
}

TEST(Oot3dNativeAssets, ResolvesCmabRuntimeLoopFrame) {
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::ResolveOot3dNativeCmabMaterialAnimationFrame(300, 1, 520.0f),
                    220.0f);
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::ResolveOot3dNativeCmabMaterialAnimationFrame(300, 1, -20.0f),
                    280.0f);
}

TEST(Oot3dNativeAssets, ResolvesNativePicaViewportAspectFromCodeBinProjectionContract) {
    ThreeDsRecomp::Oot3d::Oot3dNativeEnvironmentBackgroundState background;
    auto& lens = background.NativeKankyoLensEffect;
    lens.LensPositionProjectionScaleXAddress = 0x00484E24;
    lens.LensPositionProjectionScaleYAddress = 0x00484E28;
    lens.LensPositionProjectionScaleX = 200.0f;
    lens.LensPositionProjectionScaleY = -120.0f;

    const auto viewport = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaViewportState(background);
    EXPECT_TRUE(viewport.Available);
    EXPECT_TRUE(viewport.CodeBinSourceDecoded);
    EXPECT_FLOAT_EQ(viewport.HalfWidth, 200.0f);
    EXPECT_FLOAT_EQ(viewport.HalfHeight, 120.0f);
    EXPECT_NEAR(viewport.Aspect, 5.0f / 3.0f, 1.0e-6f);

    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    scene.PicaViewport = viewport;
    EXPECT_NEAR(ThreeDsRecomp::Oot3d::ResolveOot3dNativePicaProjectionAspect(
                    scene, 16.0 / 9.0),
                5.0 / 3.0, 1.0e-6);

    lens.LensPositionProjectionScaleY = 0.0f;
    scene.PicaViewport = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaViewportState(background);
    EXPECT_FALSE(scene.PicaViewport.Available);
    EXPECT_DOUBLE_EQ(ThreeDsRecomp::Oot3d::ResolveOot3dNativePicaProjectionAspect(
                         scene, 16.0 / 9.0),
                     16.0 / 9.0);
}

TEST(Oot3dNativeAssets, MaterializesIndependentNativeKankyoSunHaloBillboard) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    auto& background = scene.EnvironmentBackground;
    background.RuntimeEnvironmentTimeInputAvailable = true;
    background.RuntimeEnvironmentSkyboxTime = 0x8000;
    background.NativeKankyoCurrentProfileIndex = 0;
    background.NativeKankyoNextProfileIndex = 4;
    background.NativeKankyoBlendAlpha = 64;

    auto& halo = background.NativeKankyoSunHalo;
    halo.Available = true;
    halo.NativeRouteResolved = true;
    halo.TextureInputResolved = true;
    halo.SamplerStateResolved = true;
    halo.ReadyForBackendInput = true;
    halo.TextureName = "fine_sun.ctxb";
    halo.CelestialScaleX = -120.0f;
    halo.CelestialScaleY = 120.0f;
    halo.CelestialScaleZ = 20.0f;
    halo.CelestialRadius = 25.0f;
    halo.PositionScale = 1.0f;
    halo.BillboardScale = 1280.0f;
    halo.MinMagFilter = 0x2601;
    halo.WrapS = 0x812F;
    halo.WrapT = 0x812F;

    for (uint32_t profileGroup = 0; profileGroup < 2; ++profileGroup) {
        const std::string textureName =
            profileGroup == 0 ? "fine_sun.ctxb" : "cloud_sun.ctxb";
        ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
        texture.Name = textureName;
        texture.Width = 128;
        texture.Height = 128;
        texture.TextureFormat = 12;
        texture.Rgba8Decoded = true;
        texture.Rgba8 = { 255, 255, 255, 255 };
        background.NativeKankyoExtraCtxbNames.push_back(textureName);
        background.NativeKankyoExtraCtxbTextures.push_back(texture);
        ThreeDsRecomp::Oot3d::Oot3dNativeKankyoSunProfileTextureState profileTexture;
        profileTexture.TextureInputResolved = true;
        profileTexture.ProfileGroupIndex = profileGroup;
        profileTexture.CtxbTypeLocalIndex = profileGroup * 2;
        profileTexture.TextureName = textureName;
        profileTexture.Width = 128;
        profileTexture.Height = 128;
        profileTexture.TextureFormat = 12;
        halo.ProfileTextures.push_back(profileTexture);
    }

    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoSkyRuntime(
        scene, { 10.0, 20.0, 30.0 }, { 11.0, 20.0, 30.0 }, { 0.0, 1.0, 0.0 });

    ASSERT_TRUE(halo.RuntimeTransformResolved);
    ASSERT_TRUE(halo.NativeVisibleBackendSubmitResolved);
    EXPECT_DOUBLE_EQ(halo.RuntimeCelestialVector.X, 0.0);
    EXPECT_DOUBLE_EQ(halo.RuntimeCelestialVector.Y, 3000.0);
    EXPECT_DOUBLE_EQ(halo.RuntimeCelestialVector.Z, 500.0);
    EXPECT_DOUBLE_EQ(halo.RuntimeWorldBasePosition.X, 10.0);
    EXPECT_DOUBLE_EQ(halo.RuntimeWorldBasePosition.Y, 3020.0);
    EXPECT_DOUBLE_EQ(halo.RuntimeWorldBasePosition.Z, 530.0);
    ASSERT_EQ(halo.RuntimeLayerAlphas.size(), 2u);
    ASSERT_EQ(halo.RuntimeLayerTextureNames.size(), 2u);
    EXPECT_FLOAT_EQ(halo.RuntimeLayerAlphas[0], 191.0f / 255.0f);
    EXPECT_FLOAT_EQ(halo.RuntimeLayerAlphas[1], 64.0f / 255.0f);
    EXPECT_EQ(halo.RuntimeLayerTextureNames[0], "fine_sun.ctxb");
    EXPECT_EQ(halo.RuntimeLayerTextureNames[1], "cloud_sun.ctxb");

    ASSERT_EQ(scene.EnvironmentModels.size(), 2u);
    for (size_t modelIndex = 0; modelIndex < scene.EnvironmentModels.size(); ++modelIndex) {
        const auto& model = scene.EnvironmentModels[modelIndex];
        EXPECT_EQ(model.NativeKankyoRole,
                  ThreeDsRecomp::Oot3d::Oot3dNativeKankyoModelRole::SunHalo);
        ASSERT_EQ(model.Textures.size(), 1u);
        ASSERT_EQ(model.Batches.size(), 1u);
        const auto& batch = model.Batches[0];
        ASSERT_EQ(batch.Vertices.size(), 6u);
        EXPECT_TRUE(batch.Material.VertexColorModulatesTexture);
        EXPECT_TRUE(batch.Material.NativeSamplerStateDecoded);
        EXPECT_EQ(batch.Material.NativeSamplerMinFilter, 0x2601u);
        EXPECT_EQ(batch.Material.NativeSamplerWrapS, 0x812Fu);
        EXPECT_FALSE(batch.Material.DepthTest);
        EXPECT_FALSE(batch.Material.DepthWrite);
        EXPECT_TRUE(batch.Material.NativePicaFogOverrideDecoded);
        EXPECT_FALSE(batch.Material.NativePicaFogEnabled);
        EXPECT_TRUE(batch.Material.NativeBlendStateSupported);
        EXPECT_EQ(batch.Material.BlendSrc, 0x0302u);
        EXPECT_EQ(batch.Material.BlendDst, 0x0001u);
        EXPECT_EQ(batch.Material.BlendEquation, 0x8006u);
        EXPECT_FLOAT_EQ(batch.Vertices[0].Uv0.X, 0.0f);
        EXPECT_FLOAT_EQ(batch.Vertices[0].Uv0.Y, 1.0f);
        EXPECT_FLOAT_EQ(batch.Vertices[2].Uv0.X, 1.0f);
        EXPECT_FLOAT_EQ(batch.Vertices[2].Uv0.Y, 1.0f);
    }
    EXPECT_EQ(scene.EnvironmentModels[0].Batches[0].Vertices[0].Color.A, 191u);
    EXPECT_EQ(scene.EnvironmentModels[1].Batches[0].Vertices[0].Color.A, 64u);

    const auto& firstLayerVertices = scene.EnvironmentModels[0].Batches[0].Vertices;
    EXPECT_FLOAT_EQ(firstLayerVertices[0].Position.Z - firstLayerVertices[2].Position.Z,
                    1280.0f);
    EXPECT_FLOAT_EQ(firstLayerVertices[1].Position.Y - firstLayerVertices[0].Position.Y,
                    1280.0f);
}

TEST(Oot3dNativeAssets, CollapsesNativeKankyoSunProfilesWithinSameConstructorGroup) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    auto& background = scene.EnvironmentBackground;
    background.RuntimeEnvironmentTimeInputAvailable = true;
    background.RuntimeEnvironmentSkyboxTime = 0x8000;
    background.NativeKankyoCurrentProfileIndex = 3;
    background.NativeKankyoNextProfileIndex = 0;
    background.NativeKankyoBlendAlpha = 64;

    auto& halo = background.NativeKankyoSunHalo;
    halo.Available = true;
    halo.NativeRouteResolved = true;
    halo.TextureInputResolved = true;
    halo.SamplerStateResolved = true;
    halo.ReadyForBackendInput = true;
    halo.CelestialScaleX = -120.0f;
    halo.CelestialScaleY = 120.0f;
    halo.CelestialScaleZ = 20.0f;
    halo.CelestialRadius = 25.0f;
    halo.PositionScale = 1.0f;
    halo.BillboardScale = 1280.0f;
    halo.MinMagFilter = 0x2601;
    halo.WrapS = 0x812F;
    halo.WrapT = 0x812F;

    ThreeDsRecomp::Oot3d::Oot3dNativeRenderTexture texture;
    texture.Name = "fine_sun.ctxb";
    texture.Width = 128;
    texture.Height = 128;
    texture.TextureFormat = 12;
    texture.Rgba8Decoded = true;
    texture.Rgba8 = { 255, 255, 255, 255 };
    background.NativeKankyoExtraCtxbNames.push_back(texture.Name);
    background.NativeKankyoExtraCtxbTextures.push_back(texture);
    ThreeDsRecomp::Oot3d::Oot3dNativeKankyoSunProfileTextureState profileTexture;
    profileTexture.TextureInputResolved = true;
    profileTexture.ProfileGroupIndex = 0;
    profileTexture.TextureName = texture.Name;
    halo.ProfileTextures.push_back(profileTexture);

    ThreeDsRecomp::Oot3d::MaterializeOot3dNativeKankyoSkyRuntime(
        scene, { 10.0, 20.0, 30.0 }, { 11.0, 20.0, 30.0 }, { 0.0, 1.0, 0.0 });

    ASSERT_EQ(scene.EnvironmentModels.size(), 1u);
    ASSERT_EQ(halo.RuntimeLayerAlphas.size(), 1u);
    ASSERT_EQ(halo.RuntimeLayerTextureNames.size(), 1u);
    EXPECT_FLOAT_EQ(halo.RuntimeLayerAlphas[0], 1.0f);
    EXPECT_EQ(halo.RuntimeLayerTextureNames[0], "fine_sun.ctxb");
}

TEST(Oot3dNativeAssets, ExtractsEmbeddedCmbFromZsi) {
    auto bytes = MinimalZsiWithEmbeddedCmb();
    auto cmbs = ThreeDsRecomp::Oot3d::ParseZsiEmbeddedCmbsBytes(bytes, "room.zsi");

    ASSERT_EQ(cmbs.size(), 1u);
    EXPECT_EQ(cmbs[0].Offset, 0x20u);
    EXPECT_EQ(cmbs[0].Model.TriangleCount(), 1u);
    EXPECT_EQ(cmbs[0].Model.VertexCount(), 3u);
}

TEST(Oot3dNativeAssets, ParsesCsabHeaderMetadata) {
    auto bytes = MinimalCsabMetadata();
    auto metadata = ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(bytes);

    EXPECT_EQ(metadata.DeclaredSize, bytes.size());
    EXPECT_EQ(metadata.Version, 5u);
    EXPECT_EQ(metadata.FrameCount, 72u);
    EXPECT_EQ(metadata.FrameSlotCount(), 73u);
    EXPECT_EQ(metadata.AnimatedBoneCount, 0u);
    EXPECT_EQ(metadata.SkeletonBoneCount, 1u);
    ASSERT_EQ(metadata.BoneToNodeIndices.size(), 1u);
    EXPECT_EQ(metadata.BoneToNodeIndices[0], 0xFFFFu);
}

TEST(Oot3dNativeAssets, AppliesAuthoredCsabConstantTranslationInsteadOfBindPose) {
    constexpr float authoredTranslationX = 25.0f;
    auto bytes = MinimalCsabConstantTranslation(authoredTranslationX);
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Skeleton.Bones.resize(1);
    model.Skeleton.Bones[0].ParentIndex = -1;
    model.Skeleton.Bones[0].Translation.X = 1.0f;
    model.Skeleton.Bones[0].Scale = { 1.0f, 1.0f, 1.0f };

    const auto pose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(bytes, model, 0.0f);

    ASSERT_TRUE(pose.Valid);
    ASSERT_EQ(pose.LocalTransforms.size(), 1u);
    EXPECT_FLOAT_EQ(pose.LocalTransforms[0].M[0][3], authoredTranslationX);
}

TEST(Oot3dNativeAssets, AppliesNativeSkelAnimeChannelMasksPerBone) {
    constexpr float authoredTranslationX = 25.0f;
    auto bytes = MinimalCsabConstantTranslation(authoredTranslationX);
    ThreeDsRecomp::Oot3d::CmbModel model;
    model.Skeleton.Bones.resize(1);
    model.Skeleton.Bones[0].ParentIndex = -1;
    model.Skeleton.Bones[0].Translation.X = 1.0f;
    model.Skeleton.Bones[0].Scale = { 1.0f, 1.0f, 1.0f };

    ThreeDsRecomp::Oot3d::CsabPoseSamplingPolicy ordinaryBonePolicy;
    ordinaryBonePolicy.DefaultChannelMask = 2;
    ordinaryBonePolicy.SpecialBone = 1;
    ordinaryBonePolicy.SpecialBoneChannelMask = 3;
    const auto ordinaryPose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(
        bytes, model, ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(bytes), 0.0f,
        ordinaryBonePolicy);
    ASSERT_TRUE(ordinaryPose.Valid);
    EXPECT_FLOAT_EQ(ordinaryPose.LocalTransforms[0].M[0][3], 1.0f);

    ordinaryBonePolicy.SpecialBone = 0;
    const auto specialPose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(
        bytes, model, ThreeDsRecomp::Oot3d::ParseCsabMetadataBytes(bytes), 0.0f,
        ordinaryBonePolicy);
    ASSERT_TRUE(specialPose.Valid);
    EXPECT_FLOAT_EQ(specialPose.LocalTransforms[0].M[0][3], authoredTranslationX);
}

TEST(Oot3dNativeAssets, MorphsCsabPoseInNativeLocalMatrixSpace) {
    const auto transform = [](float radians, float x, float y, float z) {
        ThreeDsRecomp::Oot3d::Matrix4f matrix{};
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        matrix.M[0][0] = c;
        matrix.M[0][1] = -s;
        matrix.M[0][3] = x;
        matrix.M[1][0] = s;
        matrix.M[1][1] = c;
        matrix.M[1][3] = y;
        matrix.M[2][2] = 1.0f;
        matrix.M[2][3] = z;
        matrix.M[3][3] = 1.0f;
        return matrix;
    };

    ThreeDsRecomp::Oot3d::CmbSkeleton skeleton;
    skeleton.Bones.resize(2);
    skeleton.Bones[0].ParentIndex = -1;
    skeleton.Bones[1].ParentIndex = 0;

    ThreeDsRecomp::Oot3d::CsabPose current;
    current.Valid = true;
    current.LocalTransforms = { transform(0.0f, 0.0f, 0.0f, 0.0f),
                                transform(0.0f, 2.0f, 0.0f, 0.0f) };
    ThreeDsRecomp::Oot3d::CsabPose morph;
    morph.Valid = true;
    morph.LocalTransforms = { transform(static_cast<float>(std::acos(-1.0) * 0.5), 10.0f, 0.0f, 0.0f),
                              transform(0.0f, 4.0f, 0.0f, 0.0f) };

    const auto blended = ThreeDsRecomp::Oot3d::MorphCsabPoseNative(skeleton, current, morph, 0.5f);
    ASSERT_TRUE(blended.Valid);
    ASSERT_EQ(blended.LocalTransforms.size(), 2u);
    ASSERT_EQ(blended.WorldTransforms.size(), 2u);
    const float diagonal = std::sqrt(0.5f);
    EXPECT_NEAR(blended.LocalTransforms[0].M[0][0], diagonal, 0.00001f);
    EXPECT_NEAR(blended.LocalTransforms[0].M[0][1], -diagonal, 0.00001f);
    EXPECT_NEAR(blended.LocalTransforms[0].M[1][0], diagonal, 0.00001f);
    EXPECT_NEAR(blended.LocalTransforms[0].M[1][1], diagonal, 0.00001f);
    EXPECT_FLOAT_EQ(blended.LocalTransforms[0].M[0][3], 5.0f);
    EXPECT_FLOAT_EQ(blended.LocalTransforms[1].M[0][3], 3.0f);
    EXPECT_NEAR(blended.WorldTransforms[1].M[0][3], 5.0f + 3.0f * diagonal, 0.00001f);
    EXPECT_NEAR(blended.WorldTransforms[1].M[1][3], 3.0f * diagonal, 0.00001f);

    const auto initialMorph = ThreeDsRecomp::Oot3d::MorphCsabPoseNative(skeleton, current, morph, 1.0f);
    ASSERT_TRUE(initialMorph.Valid);
    EXPECT_FLOAT_EQ(initialMorph.LocalTransforms[0].M[0][3], 10.0f);
    EXPECT_FLOAT_EQ(initialMorph.LocalTransforms[1].M[0][3], 4.0f);
}

TEST(Oot3dNativeResourceContract, AcceptsNativeContract) {
    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(ValidContractJson());
    auto validation = ThreeDsRecomp::Oot3d::ValidateNativeResourceContract(contract);

    EXPECT_TRUE(validation.IsValid);
    EXPECT_TRUE(validation.Issues.empty());
}

TEST(Oot3dNativeResourceContract, BuildsNativeDemoResourceSet) {
    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(ValidContractJson());
    auto resourceSet = ThreeDsRecomp::Oot3d::BuildNativeDemoResourceSet(contract);

    EXPECT_TRUE(resourceSet.IsValid);
    ASSERT_NE(resourceSet.Resources.RoomVisualMesh, nullptr);
    ASSERT_NE(resourceSet.Resources.SceneCollision, nullptr);
    ASSERT_NE(resourceSet.Resources.PlayerModelAndAnimation, nullptr);
    ASSERT_NE(resourceSet.Resources.PlayerAssetValidation, nullptr);
    EXPECT_EQ(resourceSet.Resources.RoomVisualMesh->Id, "link_house_room_visual");
    EXPECT_EQ(resourceSet.Resources.SceneCollision->Id, "link_house_collision");
}

TEST(Oot3dNativeResourceContract, ProbesNativeDemoFiles) {
    auto tempRoot = std::filesystem::temp_directory_path() / "oot3d_native_resource_contract_tests";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot);

    auto data = ValidContractJson();
    data["resources"][0]["source_path"] = (tempRoot / "link_0_info.zsi").string();
    data["resources"][0]["runtime_artifact"] = (tempRoot / "room.glb").string();
    data["resources"][1]["source_path"] = (tempRoot / "link_info.zsi").string();
    data["resources"][1]["runtime_artifact"] = (tempRoot / "collision.xml").string();
    data["resources"][2]["source_path"] = (tempRoot / "character_manifest.json").string();
    data["resources"][2]["runtime_artifact"] = (tempRoot / "link_child.glb").string();
    data["resources"][3]["source_path"] = (tempRoot / "character_manifest.json").string();
    data["resources"][3]["runtime_artifact"] = (tempRoot / "validated.manifest.json").string();

    WriteBinary(tempRoot / "link_0_info.zsi", MinimalZsiHeader());
    WriteBinary(tempRoot / "link_info.zsi", MinimalZsiHeader());
    WriteText(tempRoot / "character_manifest.json", "{}");
    WriteBinary(tempRoot / "room.glb", MinimalGlbHeader());
    WriteText(tempRoot / "collision.xml", "<Root />");
    WriteBinary(tempRoot / "link_child.glb", MinimalGlbHeader());
    WriteText(tempRoot / "validated.manifest.json", "{}");

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto probe = ThreeDsRecomp::Oot3d::ProbeNativeDemoResourceFiles(contract);

    EXPECT_TRUE(probe.IsValid);
    EXPECT_EQ(probe.Entries.size(), 4u);
    EXPECT_TRUE(probe.Issues.empty());
    EXPECT_EQ(probe.Entries[0].ExpectedSourceKind, "zsi");
    EXPECT_EQ(probe.Entries[0].DetectedSourceKind, "zsi");
    EXPECT_TRUE(probe.Entries[0].SourceNativeProbeRequired);
    EXPECT_TRUE(probe.Entries[0].SourceNativeFormatMatches);
    EXPECT_EQ(probe.Entries[2].ExpectedSourceKind, "cmb_plus_csab");
    EXPECT_EQ(probe.Entries[2].DetectedSourceKind, "derived_manifest");
    EXPECT_FALSE(probe.Entries[2].SourceNativeProbeRequired);
    EXPECT_FALSE(probe.Entries[2].SourceNativeFormatMatches);
    EXPECT_EQ(probe.Entries[2].SourceProbeIssue, "native_cmb_csab_container_reference_pending");

    std::filesystem::remove_all(tempRoot);
}

TEST(Oot3dNativeResourceContract, RejectsGlbSourceDeclaredAsNativeZsi) {
    auto tempRoot = std::filesystem::temp_directory_path() / "oot3d_native_resource_contract_bad_source_tests";
    std::filesystem::remove_all(tempRoot);
    std::filesystem::create_directories(tempRoot);

    auto data = ValidContractJson();
    data["resources"][0]["source_path"] = (tempRoot / "room_source.glb").string();
    data["resources"][0]["runtime_artifact"] = (tempRoot / "room.glb").string();
    data["resources"][1]["source_path"] = (tempRoot / "link_info.zsi").string();
    data["resources"][1]["runtime_artifact"] = (tempRoot / "collision.xml").string();
    data["resources"][2]["source_path"] = (tempRoot / "character_manifest.json").string();
    data["resources"][2]["runtime_artifact"] = (tempRoot / "link_child.glb").string();
    data["resources"][3]["source_path"] = (tempRoot / "character_manifest.json").string();
    data["resources"][3]["runtime_artifact"] = (tempRoot / "validated.manifest.json").string();

    WriteBinary(tempRoot / "room_source.glb", MinimalGlbHeader());
    WriteBinary(tempRoot / "link_info.zsi", MinimalZsiHeader());
    WriteText(tempRoot / "character_manifest.json", "{}");
    WriteBinary(tempRoot / "room.glb", MinimalGlbHeader());
    WriteText(tempRoot / "collision.xml", "<Root />");
    WriteBinary(tempRoot / "link_child.glb", MinimalGlbHeader());
    WriteText(tempRoot / "validated.manifest.json", "{}");

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto probe = ThreeDsRecomp::Oot3d::ProbeNativeDemoResourceFiles(contract);

    EXPECT_FALSE(probe.IsValid);
    ASSERT_FALSE(probe.Issues.empty());
    EXPECT_EQ(probe.Issues[0].Code, "resource_source_native_format_mismatch");
    EXPECT_EQ(probe.Issues[0].ResourceId, "link_house_room_visual");
    EXPECT_EQ(probe.Entries[0].DetectedSourceKind, "unknown");
    EXPECT_FALSE(probe.Entries[0].SourceNativeFormatMatches);

    std::filesystem::remove_all(tempRoot);
}

TEST(Oot3dNativeResourceContract, RejectsRuntimeN64Substitution) {
    auto data = ValidContractJson();
    data["runtime_n64_asset_substitution_allowed"] = true;
    data["resources"][0]["runtime_n64_asset_path"] = "__OTR__/objects/gameplay_keep/link_room";

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto validation = ThreeDsRecomp::Oot3d::ValidateNativeResourceContract(contract);

    EXPECT_FALSE(validation.IsValid);
    ASSERT_GE(validation.Issues.size(), 2u);
    EXPECT_EQ(validation.Issues[0].Code, "runtime_n64_asset_substitution_enabled");
    EXPECT_EQ(validation.Issues[1].Code, "resource_has_runtime_n64_asset_path");
}

TEST(Oot3dNativeResourceContract, RejectsNonOot3dLoaderContract) {
    auto data = ValidContractJson();
    data["resources"][0]["future_loader_contract"] = "shipwright.otr.replacement_texture";

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto validation = ThreeDsRecomp::Oot3d::ValidateNativeResourceContract(contract);

    EXPECT_FALSE(validation.IsValid);
    ASSERT_EQ(validation.Issues.size(), 1u);
    EXPECT_EQ(validation.Issues[0].Code, "resource_invalid_loader_contract");
}

TEST(Oot3dNativeResourceContract, RejectsMissingNativeDemoRole) {
    auto data = ValidContractJson();
    data["resources"].erase(1);
    data["resource_count"] = 3;

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto resourceSet = ThreeDsRecomp::Oot3d::BuildNativeDemoResourceSet(contract);

    EXPECT_FALSE(resourceSet.IsValid);
    ASSERT_EQ(resourceSet.Issues.size(), 1u);
    EXPECT_EQ(resourceSet.Issues[0].Code, "native_demo_resource_role_missing");
    EXPECT_EQ(resourceSet.Issues[0].ResourceId, "scene_collision");
}

TEST(Oot3dNativeResourceContract, RejectsDuplicateResourceId) {
    auto data = ValidContractJson();
    data["resources"][1]["id"] = "link_house_room_visual";

    auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(data);
    auto validation = ThreeDsRecomp::Oot3d::ValidateNativeResourceContract(contract);

    EXPECT_FALSE(validation.IsValid);
    ASSERT_EQ(validation.Issues.size(), 1u);
    EXPECT_EQ(validation.Issues[0].Code, "resource_duplicate_id");
}
