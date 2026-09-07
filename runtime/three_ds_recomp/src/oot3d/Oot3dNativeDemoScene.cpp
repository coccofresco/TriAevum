#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderer.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace ThreeDsRecomp::Oot3d {
namespace {

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open JSON file: " + path.string());
    }
    return nlohmann::json::parse(file);
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open text file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open binary file: " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

const NativeZsiLightSettingsRecordContract& NativeZsiLightSettingsRecordLayout() {
    static const auto layout = BuildNativeKankyoRuntimeBridgeContract().ZsiLightSettingsRecord;
    return layout;
}

std::filesystem::path ResolveRelativePath(const std::filesystem::path& basePath,
                                          const std::filesystem::path& value) {
    if (value.empty() || value.is_absolute()) {
        return value;
    }
    return basePath.parent_path() / value;
}

std::string JsonStringAt(const nlohmann::json& object, const std::string& key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return "";
    }
    return object.at(key).get<std::string>();
}

double JsonNumberAt(const nlohmann::json& object, const std::string& key, double fallback) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number()) {
        return fallback;
    }
    return object.at(key).get<double>();
}

int JsonIntAt(const nlohmann::json& object, const std::string& key, int fallback) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_number_integer()) {
        return fallback;
    }
    return object.at(key).get<int>();
}

std::string XmlAttr(const std::string& text, const std::string& name) {
    const std::regex pattern(name + "=\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        throw std::runtime_error("missing XML attribute: " + name);
    }
    return match[1].str();
}

int XmlAttrInt(const std::string& text, const std::string& name) {
    return std::stoi(XmlAttr(text, name));
}

double XmlAttrDouble(const std::string& text, const std::string& name) {
    return std::stod(XmlAttr(text, name));
}

std::string JsonNestedString(const nlohmann::json& object, std::initializer_list<const char*> keys) {
    const nlohmann::json* cursor = &object;
    for (const auto* key : keys) {
        if (!cursor->is_object() || !cursor->contains(key)) {
            return "";
        }
        cursor = &cursor->at(key);
    }
    return cursor->is_string() ? cursor->get<std::string>() : "";
}

std::filesystem::path ResolveNativeCameraTablePath(const std::filesystem::path& manifestPath,
                                                   const nlohmann::json& manifest,
                                                   bool& repoRootDefaultUsed,
                                                   bool& explicitPathUsed) {
    repoRootDefaultUsed = false;
    explicitPathUsed = false;

    auto source = JsonNestedString(manifest, { "sources", "native_camera_table", "path" });
    if (source.empty()) {
        source = JsonNestedString(manifest, { "sources", "camera_table", "path" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "native_camera_table" });
    }
    if (!source.empty()) {
        explicitPathUsed = true;
        return ResolveRelativePath(manifestPath, source);
    }

    const auto repoRoot = JsonStringAt(manifest, "repo_root");
    if (repoRoot.empty()) {
        return {};
    }

    repoRootDefaultUsed = true;
    return std::filesystem::path(repoRoot) / "tools" / "oot3d" / "native_demo_host" / "camera_tables" /
           "oot3d_native_camera_table.json";
}

void LoadNativeCameraTable(Oot3dNativeDemoScene& scene, const std::filesystem::path& manifestPath,
                           const nlohmann::json& manifest) {
    bool repoRootDefaultUsed = false;
    bool explicitPathUsed = false;
    scene.NativeCameraTablePath =
        ResolveNativeCameraTablePath(manifestPath, manifest, repoRootDefaultUsed, explicitPathUsed);
    scene.NativeCameraTableRepoRootDefaultUsed = repoRootDefaultUsed;

    if (scene.NativeCameraTablePath.empty()) {
        return;
    }
    if (!std::filesystem::is_regular_file(scene.NativeCameraTablePath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D native camera table was not found: " +
                                     scene.NativeCameraTablePath.string());
        }
        return;
    }

    scene.NativeCameraTable = ReadJsonFile(scene.NativeCameraTablePath);
    scene.NativeCameraTableAvailable = scene.NativeCameraTable.is_object();
    scene.NativeCameraTableSourceKind = JsonStringAt(scene.NativeCameraTable, "source_kind");
    scene.NativeCameraTableFormat = JsonStringAt(scene.NativeCameraTable, "format");
}

std::filesystem::path ResolveNativePicaLightingSemanticsPath(const std::filesystem::path& manifestPath,
                                                             const nlohmann::json& manifest,
                                                             bool& repoRootDefaultUsed,
                                                             bool& explicitPathUsed) {
    repoRootDefaultUsed = false;
    explicitPathUsed = false;

    auto source = JsonNestedString(manifest, { "sources", "native_pica_lighting_semantics", "path" });
    if (source.empty()) {
        source = JsonNestedString(manifest, { "sources", "pica_lighting_semantics", "path" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "native_pica_lighting_semantics" });
    }
    if (!source.empty()) {
        explicitPathUsed = true;
        return ResolveRelativePath(manifestPath, source);
    }

    const auto repoRoot = JsonStringAt(manifest, "repo_root");
    if (repoRoot.empty()) {
        return {};
    }

    repoRootDefaultUsed = true;
    return std::filesystem::path(repoRoot) / "tools" / "oot3d" / "native_demo_host" / "lighting_tables" /
           "oot3d_pica_lighting_semantics.json";
}

void LoadNativePicaLightingSemantics(Oot3dNativeDemoScene& scene, const std::filesystem::path& manifestPath,
                                     const nlohmann::json& manifest) {
    bool repoRootDefaultUsed = false;
    bool explicitPathUsed = false;
    scene.NativePicaLightingSemanticsPath =
        ResolveNativePicaLightingSemanticsPath(manifestPath, manifest, repoRootDefaultUsed, explicitPathUsed);
    scene.NativePicaLightingSemanticsRepoRootDefaultUsed = repoRootDefaultUsed;

    if (scene.NativePicaLightingSemanticsPath.empty()) {
        return;
    }
    if (!std::filesystem::is_regular_file(scene.NativePicaLightingSemanticsPath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D native PICA lighting semantics table was not found: " +
                                     scene.NativePicaLightingSemanticsPath.string());
        }
        return;
    }

    scene.NativePicaLightingSemantics = ReadJsonFile(scene.NativePicaLightingSemanticsPath);
    scene.NativePicaLightingSemanticsAvailable = scene.NativePicaLightingSemantics.is_object();
    scene.NativePicaLightingSemanticsSourceKind = JsonStringAt(scene.NativePicaLightingSemantics, "source_kind");
    scene.NativePicaLightingSemanticsFormat = JsonStringAt(scene.NativePicaLightingSemantics, "format");
}

std::filesystem::path ResolveNativePicaRegisterTracePath(const std::filesystem::path& manifestPath,
                                                         const nlohmann::json& manifest,
                                                         bool& repoRootDefaultUsed,
                                                         bool& explicitPathUsed) {
    repoRootDefaultUsed = false;
    explicitPathUsed = false;

    auto source = JsonNestedString(manifest, { "sources", "native_pica_register_trace", "path" });
    if (source.empty()) {
        source = JsonNestedString(manifest, { "sources", "pica_register_trace", "path" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "native_pica_register_trace" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "pica_register_trace" });
    }
    if (!source.empty()) {
        explicitPathUsed = true;
        return ResolveRelativePath(manifestPath, source);
    }

    return {};
}

void LoadNativePicaRegisterTrace(Oot3dNativeDemoScene& scene, const std::filesystem::path& manifestPath,
                                 const nlohmann::json& manifest) {
    bool repoRootDefaultUsed = false;
    bool explicitPathUsed = false;
    scene.NativePicaRegisterTracePath =
        ResolveNativePicaRegisterTracePath(manifestPath, manifest, repoRootDefaultUsed, explicitPathUsed);
    scene.NativePicaRegisterTraceRepoRootDefaultUsed = repoRootDefaultUsed;

    if (scene.NativePicaRegisterTracePath.empty()) {
        return;
    }
    if (!std::filesystem::is_regular_file(scene.NativePicaRegisterTracePath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D native PICA register trace was not found: " +
                                     scene.NativePicaRegisterTracePath.string());
        }
        return;
    }

    scene.NativePicaRegisterTrace = ReadJsonFile(scene.NativePicaRegisterTracePath);
    scene.NativePicaRegisterTraceAvailable = scene.NativePicaRegisterTrace.is_object();
    scene.NativePicaRegisterTraceSourceKind = JsonStringAt(scene.NativePicaRegisterTrace, "source_kind");
    scene.NativePicaRegisterTraceFormat = JsonStringAt(scene.NativePicaRegisterTrace, "format");
}

std::filesystem::path ResolveNativeCmbVShaderShbinPath(const std::filesystem::path& manifestPath,
                                                       const nlohmann::json& manifest,
                                                       bool& derivedFromRoomZsiPath,
                                                       bool& repoRootDefaultUsed,
                                                       bool& explicitPathUsed) {
    derivedFromRoomZsiPath = false;
    repoRootDefaultUsed = false;
    explicitPathUsed = false;

    auto source = JsonNestedString(manifest, { "sources", "native_cmb_vshader_shbin", "path" });
    if (source.empty()) {
        source = JsonNestedString(manifest, { "sources", "cmb_vshader_shbin", "path" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "native_cmb_vshader_shbin" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "cmb_vshader_shbin" });
    }
    if (!source.empty()) {
        explicitPathUsed = true;
        return ResolveRelativePath(manifestPath, source);
    }

    const auto roomVisualSource = JsonNestedString(manifest, { "sources", "room_visual", "path" });
    if (!roomVisualSource.empty()) {
        const auto roomZsiPath = ResolveRelativePath(manifestPath, roomVisualSource);
        const auto sceneDir = roomZsiPath.parent_path();
        if (!sceneDir.empty()) {
            derivedFromRoomZsiPath = true;
            if (sceneDir.filename() == "scene") {
                return sceneDir.parent_path() / "CmbVShader.shbin";
            }
            return sceneDir / "CmbVShader.shbin";
        }
    }

    const auto repoRoot = JsonStringAt(manifest, "repo_root");
    if (!repoRoot.empty()) {
        repoRootDefaultUsed = true;
        return std::filesystem::path(repoRoot) / "CmbVShader.shbin";
    }

    return {};
}

void LoadNativeCmbVShaderShbin(Oot3dNativeDemoScene& scene, const std::filesystem::path& manifestPath,
                               const nlohmann::json& manifest) {
    bool derivedFromRoomZsiPath = false;
    bool repoRootDefaultUsed = false;
    bool explicitPathUsed = false;
    scene.NativeCmbVShaderShbinPath = ResolveNativeCmbVShaderShbinPath(
        manifestPath, manifest, derivedFromRoomZsiPath, repoRootDefaultUsed, explicitPathUsed);
    scene.NativeCmbVShaderShbinDerivedFromRoomZsiPath = derivedFromRoomZsiPath;
    scene.NativeCmbVShaderShbinRepoRootDefaultUsed = repoRootDefaultUsed;

    if (scene.NativeCmbVShaderShbinPath.empty()) {
        return;
    }
    if (!std::filesystem::is_regular_file(scene.NativeCmbVShaderShbinPath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D native CMB vertex shader SHBIN was not found: " +
                                     scene.NativeCmbVShaderShbinPath.string());
        }
        return;
    }

    scene.NativeCmbVShader = ParseShbinShaderBinaryFile(scene.NativeCmbVShaderShbinPath);
    scene.NativeCmbVShaderShbinAvailable = true;
    scene.NativeCmbVShaderShbinSourceKind = "oot3d_romfs_cmb_vshader_shbin";
    scene.NativeCmbVShaderShbinFormat = "oot3d_shbin_dvlb_dvlp_dvle_pica_vertex_shader";
}

std::filesystem::path ResolveNativeCodeBinPath(const std::filesystem::path& manifestPath,
                                               const nlohmann::json& manifest,
                                               const std::filesystem::path& roomZsiPath,
                                               bool& explicitPathUsed,
                                               bool& derivedFromRoomZsiPath) {
    explicitPathUsed = false;
    derivedFromRoomZsiPath = false;

    auto source = JsonNestedString(manifest, { "sources", "native_code_bin", "path" });
    if (source.empty()) {
        source = JsonNestedString(manifest, { "sources", "code_bin", "path" });
    }
    if (source.empty()) {
        source = JsonNestedString(manifest, { "assets", "native_code_bin" });
    }
    if (!source.empty()) {
        explicitPathUsed = true;
        return ResolveRelativePath(manifestPath, source);
    }

    const auto sceneDir = roomZsiPath.parent_path();
    if (sceneDir.filename() == "scene") {
        const auto romfsDir = sceneDir.parent_path();
        if (romfsDir.filename() == "romfs") {
            derivedFromRoomZsiPath = true;
            return romfsDir.parent_path() / "exefs" / "code.bin";
        }
    }
    return {};
}

void LoadNativeRuntimeLightTransitionTable(Oot3dNativeDemoPicaLightingState& lighting,
                                           const std::filesystem::path& codeBinPath,
                                           bool explicitPathUsed) {
    lighting.CodeBinPath = codeBinPath;
    if (codeBinPath.empty()) {
        return;
    }
    if (!std::filesystem::is_regular_file(codeBinPath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D code.bin was not found: " + codeBinPath.string());
        }
        return;
    }

    const auto& layout = NativeZsiLightSettingsRecordLayout();
    const auto modes = ParseNativeLightSettingsTransitionTableFromCodeBinFile(
        codeBinPath, layout.RuntimeTransitionTableCodeBase, layout.RuntimeTransitionTableAddress,
        layout.RuntimeTransitionModeCount, layout.RuntimeTransitionEntryCount,
        layout.RuntimeTransitionModeStrideBytes, layout.RuntimeTransitionEntrySizeBytes);

    lighting.NativeRuntimeTransitionModes.clear();
    lighting.NativeRuntimeTransitionModes.reserve(modes.size());
    for (const auto& sourceMode : modes) {
        Oot3dNativeDemoLightSettingsTransitionMode mode;
        mode.ModeIndex = static_cast<int>(sourceMode.ModeIndex);
        mode.Entries.reserve(sourceMode.Entries.size());
        int entryIndex = 0;
        for (const auto& sourceEntry : sourceMode.Entries) {
            mode.Entries.push_back({
                entryIndex,
                sourceEntry.StartAngle,
                sourceEntry.EndAngle,
                sourceEntry.FromLightSettingIndex,
                sourceEntry.ToLightSettingIndex,
            });
            ++entryIndex;
        }
        lighting.NativeRuntimeTransitionModes.push_back(std::move(mode));
    }
    lighting.NativeRuntimeTransitionTableAvailable = !lighting.NativeRuntimeTransitionModes.empty();
    lighting.NativeRuntimeTransitionTableDecodedFromCodeBin = lighting.NativeRuntimeTransitionTableAvailable;
    lighting.NativeRuntimeTransitionTableSourceKind = "oot3d_code_bin_0045dd50_transition_table_00531efc";

    const auto codeBin = ReadBinaryFile(codeBinPath);
    const auto runtimeToFileOffset = [&](uint32_t runtimeAddress) -> std::optional<size_t> {
        if (runtimeAddress < layout.RuntimeTransitionTableCodeBase) {
            return std::nullopt;
        }
        return static_cast<size_t>(runtimeAddress - layout.RuntimeTransitionTableCodeBase);
    };
    const auto canReadCodeBin = [&](size_t offset, size_t size) {
        return offset <= codeBin.size() && size <= codeBin.size() - offset;
    };
    const auto readCodeBinF32 = [&](size_t offset) {
        uint32_t raw = static_cast<uint32_t>(codeBin[offset]) |
                       (static_cast<uint32_t>(codeBin[offset + 1]) << 8) |
                       (static_cast<uint32_t>(codeBin[offset + 2]) << 16) |
                       (static_cast<uint32_t>(codeBin[offset + 3]) << 24);
        float value = 0.0f;
        static_assert(sizeof(value) == sizeof(raw));
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    };

    const auto fallbackOffset =
        runtimeToFileOffset(layout.RuntimeTransitionGlobalFallbackStateAddress);
    if (fallbackOffset.has_value()) {
        const size_t base = *fallbackOffset;
        const size_t modeOffset = base + layout.RuntimeTransitionGlobalFallbackModeOffset;
        if (canReadCodeBin(modeOffset, 1)) {
            lighting.NativeRuntimeTransitionGlobalFallbackStateAvailable = true;
            lighting.NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin = true;
            lighting.NativeRuntimeTransitionGlobalFallbackStateSourceKind =
                "oot3d_code_bin_004b8fc0_global_environment_fallback_state_00531eb4";
            lighting.NativeRuntimeTransitionGlobalFallbackStateAddress =
                layout.RuntimeTransitionGlobalFallbackStateAddress;
            lighting.NativeRuntimeTransitionGlobalFallbackModeOffset =
                layout.RuntimeTransitionGlobalFallbackModeOffset;
            lighting.NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset =
                layout.RuntimeTransitionGlobalFallbackModeWeightFloatOffset;
            lighting.NativeRuntimeTransitionGlobalFallbackFromIndexOffset =
                layout.RuntimeTransitionGlobalFallbackFromIndexOffset;
            lighting.NativeRuntimeTransitionGlobalFallbackToIndexOffset =
                layout.RuntimeTransitionGlobalFallbackToIndexOffset;
            lighting.NativeRuntimeTransitionGlobalFallbackMode =
                static_cast<int>(codeBin[modeOffset]);

            const size_t fromOffset = base + layout.RuntimeTransitionGlobalFallbackFromIndexOffset;
            if (canReadCodeBin(fromOffset, 1)) {
                lighting.NativeRuntimeTransitionGlobalFallbackFromIndex =
                    static_cast<int>(codeBin[fromOffset]);
            }
            const size_t toOffset = base + layout.RuntimeTransitionGlobalFallbackToIndexOffset;
            if (canReadCodeBin(toOffset, 1)) {
                lighting.NativeRuntimeTransitionGlobalFallbackToIndex =
                    static_cast<int>(codeBin[toOffset]);
            }
            const size_t weightOffset =
                base + layout.RuntimeTransitionGlobalFallbackModeWeightFloatOffset;
            if (canReadCodeBin(weightOffset, 4)) {
                lighting.NativeRuntimeTransitionGlobalFallbackModeWeight =
                    static_cast<double>(readCodeBinF32(weightOffset));
            }
        }
    }
}

struct Oot3dNativeDemoGlobalEntranceEntry {
    int GlobalEntranceIndex = -1;
    int TableRuntimeAddress = 0x00543BB8;
    int TableFileOffset = 0x00443BB8;
    int EntryFileOffset = -1;
    int SceneId = -1;
    int LocalEntranceIndex = -1;
    int Field = -1;
    std::string SourceKind = "oot3d_code_bin_global_entrance_table_00543bb8";
    std::filesystem::path CodeBinPath;
};

bool CanRead(const std::vector<uint8_t>& data, size_t offset, size_t size) {
    return offset <= data.size() && size <= data.size() - offset;
}

uint16_t ReadLeU16(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 2)) {
        throw std::runtime_error("read past end of ZSI buffer");
    }
    return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
}

int16_t ReadLeS16(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<int16_t>(ReadLeU16(data, offset));
}

uint32_t ReadLeU32(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 4)) {
        throw std::runtime_error("read past end of ZSI buffer");
    }
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::optional<Oot3dNativeDemoGlobalEntranceEntry> DecodeOot3dNativeGlobalEntranceEntry(
    const std::filesystem::path& codeBinPath,
    bool explicitPathUsed,
    int globalEntranceIndex) {
    if (globalEntranceIndex < 0) {
        return std::nullopt;
    }
    if (codeBinPath.empty()) {
        return std::nullopt;
    }
    if (!std::filesystem::is_regular_file(codeBinPath)) {
        if (explicitPathUsed) {
            throw std::runtime_error("declared OOT3D code.bin was not found: " + codeBinPath.string());
        }
        return std::nullopt;
    }

    constexpr int kGlobalEntranceTableRuntimeAddress = 0x00543BB8;
    constexpr int kGlobalEntranceTableFileOffset = 0x00443BB8;
    constexpr int kGlobalEntranceEntrySize = 4;

    const auto data = ReadBinaryFile(codeBinPath);
    const size_t entryOffset =
        static_cast<size_t>(kGlobalEntranceTableFileOffset) +
        static_cast<size_t>(globalEntranceIndex) * kGlobalEntranceEntrySize;
    if (!CanRead(data, entryOffset, kGlobalEntranceEntrySize)) {
        return std::nullopt;
    }

    Oot3dNativeDemoGlobalEntranceEntry entry;
    entry.GlobalEntranceIndex = globalEntranceIndex;
    entry.TableRuntimeAddress = kGlobalEntranceTableRuntimeAddress;
    entry.TableFileOffset = kGlobalEntranceTableFileOffset;
    entry.EntryFileOffset = static_cast<int>(entryOffset);
    entry.SceneId = static_cast<int>(data[entryOffset]);
    entry.LocalEntranceIndex = static_cast<int>(data[entryOffset + 1]);
    entry.Field = static_cast<int>(ReadLeU16(data, entryOffset + 2));
    entry.CodeBinPath = codeBinPath;
    return entry;
}

float ReadLeF32(const std::vector<uint8_t>& data, size_t offset) {
    const uint32_t raw = ReadLeU32(data, offset);
    float value = 0.0f;
    static_assert(sizeof(value) == sizeof(raw));
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

int RoundS16(double value) {
    const auto rounded = static_cast<int>(std::round(value));
    return std::clamp(rounded, -32768, 32767);
}

bool BoundsArePlausible(const Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return false;
    }
    if (bounds.Min.X > bounds.Max.X || bounds.Min.Y > bounds.Max.Y || bounds.Min.Z > bounds.Max.Z) {
        return false;
    }
    return bounds.Min.X < bounds.Max.X || bounds.Min.Y < bounds.Max.Y || bounds.Min.Z < bounds.Max.Z;
}

bool IsFileOffset(uint32_t offset, size_t size) {
    return static_cast<size_t>(offset) < size;
}

struct ZsiSceneCommand {
    int SetupIndex = -1;
    size_t Offset = 0;
    uint32_t CommandWord = 0;
    uint32_t Argument = 0;
};

struct ZsiSceneSetup {
    int Index = -1;
    size_t Offset = 0;
    size_t EndOffset = 0;
    std::vector<ZsiSceneCommand> Commands;
};

uint8_t ZsiCommandId(const ZsiSceneCommand& command) {
    return static_cast<uint8_t>(command.CommandWord & 0xFF);
}

uint8_t ZsiCommandParameter(const ZsiSceneCommand& command) {
    return static_cast<uint8_t>((command.CommandWord >> 8) & 0xFF);
}

struct ZsiRawCollisionPolygon {
    uint16_t Type = 0;
    uint16_t RawVertexA = 0;
    uint16_t RawVertexB = 0;
    uint16_t RawVertexC = 0;
    uint16_t Marker = 0;
    int16_t NormalX = 0;
    int16_t NormalY = 0;
    int16_t NormalZ = 0;
    float Dist = 0.0f;
};

struct ZsiRawSurfaceType {
    uint32_t Data1 = 0;
    uint32_t Data2 = 0;
};

struct ZsiRawBgCamera {
    uint16_t Setting = 0;
    uint16_t Count = 0;
    uint32_t DataOffset = 0;
};

struct ZsiBgCameraLayout {
    size_t EffectiveBgCamOffset = 0;
    size_t CameraPositionOffset = 0;
    int CameraPointerAdjustment = 0;
    std::vector<ZsiRawBgCamera> Cameras;
    std::vector<int> CameraPositionVectorIndices;
    std::vector<Oot3dDemoVec3> CameraPositionVectors;
};

constexpr int kZsiPolygonVertexIndexMask = 0x1FFF;
constexpr int kZsiPolygonFlagMask = 0xE000;
constexpr int kZsiPolygonIgnoreCameraFlag = 0x2000;
constexpr int kZsiPolygonIgnoreEntitiesFlag = 0x4000;
constexpr int kZsiPolygonIgnoreProjectilesFlag = 0x8000;
constexpr int kZsiPolygonConveyorFlag = 0x2000;

int ZsiPolygonVertexA(const ZsiRawCollisionPolygon& polygon) {
    return polygon.RawVertexA & kZsiPolygonVertexIndexMask;
}

int ZsiPolygonVertexB(const ZsiRawCollisionPolygon& polygon) {
    return polygon.RawVertexB & kZsiPolygonVertexIndexMask;
}

int ZsiPolygonVertexC(const ZsiRawCollisionPolygon& polygon) {
    return polygon.RawVertexC & kZsiPolygonVertexIndexMask;
}

Oot3dNativeDemoCollisionPolygon BuildCollisionPolygonFromRawVertices(int type, int rawVertexA, int rawVertexB,
                                                                     int rawVertexC, int normalX, int normalY,
                                                                     int normalZ, int dist) {
    return {
        type,
        rawVertexA & kZsiPolygonVertexIndexMask,
        rawVertexB & kZsiPolygonVertexIndexMask,
        rawVertexC & kZsiPolygonVertexIndexMask,
        rawVertexA,
        rawVertexB,
        rawVertexC,
        rawVertexA & kZsiPolygonFlagMask,
        rawVertexB & kZsiPolygonFlagMask,
        (rawVertexA & kZsiPolygonIgnoreCameraFlag) != 0,
        (rawVertexA & kZsiPolygonIgnoreEntitiesFlag) != 0,
        (rawVertexA & kZsiPolygonIgnoreProjectilesFlag) != 0,
        (rawVertexB & kZsiPolygonConveyorFlag) != 0,
        normalX,
        normalY,
        normalZ,
        dist,
    };
}

int SurfaceTypeCameraDataIndex(const ZsiRawSurfaceType& surfaceType) {
    return static_cast<int>(surfaceType.Data1 & 0xFF);
}

ZsiRawCollisionPolygon ReadZsiCollisionPolygon(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 0x14)) {
        throw std::runtime_error("read past end of ZSI collision polygon table");
    }
    return {
        ReadLeU16(data, offset + 0x00),
        ReadLeU16(data, offset + 0x02),
        ReadLeU16(data, offset + 0x04),
        ReadLeU16(data, offset + 0x06),
        ReadLeU16(data, offset + 0x08),
        ReadLeS16(data, offset + 0x0A),
        ReadLeS16(data, offset + 0x0C),
        ReadLeS16(data, offset + 0x0E),
        ReadLeF32(data, offset + 0x10),
    };
}

ZsiRawSurfaceType ReadZsiSurfaceType(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 8)) {
        throw std::runtime_error("read past end of ZSI surface type table");
    }
    return {
        ReadLeU32(data, offset + 0x00),
        ReadLeU32(data, offset + 0x04),
    };
}

ZsiRawBgCamera ReadZsiBgCamera(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 8)) {
        throw std::runtime_error("read past end of ZSI bg camera table");
    }
    return {
        ReadLeU16(data, offset + 0x00),
        ReadLeU16(data, offset + 0x02),
        ReadLeU32(data, offset + 0x04),
    };
}

Oot3dDemoVec3 ReadZsiCollisionVertex(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 6)) {
        throw std::runtime_error("read past end of ZSI collision vertex table");
    }
    return {
        static_cast<double>(ReadLeS16(data, offset + 0)),
        static_cast<double>(ReadLeS16(data, offset + 2)),
        static_cast<double>(ReadLeS16(data, offset + 4)),
    };
}

Oot3dDemoVec3 ReadZsiCameraPositionVector(const std::vector<uint8_t>& data, size_t offset) {
    if (!CanRead(data, offset, 6)) {
        throw std::runtime_error("read past end of ZSI camera position vector table");
    }
    return {
        static_cast<double>(ReadLeS16(data, offset + 0)),
        static_cast<double>(ReadLeS16(data, offset + 2)),
        static_cast<double>(ReadLeS16(data, offset + 4)),
    };
}

bool ZsiCollisionPolygonIsValid(const ZsiRawCollisionPolygon& polygon, uint16_t vertexCount,
                                uint16_t surfaceTypeCount) {
    return polygon.Type < surfaceTypeCount && ZsiPolygonVertexA(polygon) < vertexCount &&
           ZsiPolygonVertexB(polygon) < vertexCount && ZsiPolygonVertexC(polygon) < vertexCount;
}

std::optional<std::pair<size_t, size_t>> FindZsiEffectivePolygonLayout(const std::vector<uint8_t>& data,
                                                                       uint32_t polygonOffset,
                                                                       uint32_t surfaceTypeOffset,
                                                                       uint16_t rawPolygonCount,
                                                                       uint16_t vertexCount,
                                                                       uint16_t surfaceTypeCount) {
    std::vector<std::pair<size_t, size_t>> fullCountCandidates;
    std::optional<std::pair<size_t, size_t>> best;
    size_t bestValid = 0;

    for (size_t prefixSize = 0; prefixSize < 0x12; prefixSize += 2) {
        const size_t candidateOffset = static_cast<size_t>(polygonOffset) + prefixSize;
        const size_t candidateEnd = candidateOffset + static_cast<size_t>(rawPolygonCount) * 0x14;
        if (candidateEnd > data.size() || candidateEnd > static_cast<size_t>(surfaceTypeOffset) + 0x10) {
            continue;
        }

        size_t validCount = 0;
        for (size_t index = 0; index < rawPolygonCount; ++index) {
            const auto polygon = ReadZsiCollisionPolygon(data, candidateOffset + index * 0x14);
            if (!ZsiCollisionPolygonIsValid(polygon, vertexCount, surfaceTypeCount)) {
                break;
            }
            ++validCount;
        }

        if (validCount == rawPolygonCount) {
            fullCountCandidates.emplace_back(candidateOffset, rawPolygonCount);
        }
        if (!best.has_value() || validCount > bestValid) {
            bestValid = validCount;
            best = std::make_pair(candidateOffset, validCount);
        }
    }

    if (!fullCountCandidates.empty()) {
        return *std::min_element(fullCountCandidates.begin(), fullCountCandidates.end(),
                                 [](const auto& left, const auto& right) { return left.first < right.first; });
    }
    if (best.has_value() && bestValid > 0) {
        return best;
    }
    return std::nullopt;
}

bool SurfaceTypeHasSectionPrefix(const std::vector<ZsiRawSurfaceType>& surfaceTypes) {
    if (surfaceTypes.size() < 3) {
        return false;
    }
    const auto& first = surfaceTypes[0];
    const auto& second = surfaceTypes[1];
    const auto& third = surfaceTypes[2];
    return (first.Data2 & 0xFFFF) == 0x55DA && first.Data1 != 0 && second.Data1 != 0 &&
           (third.Data1 & 0xFF) < 0x40;
}

int SurfaceTypeLayoutScore(const std::vector<ZsiRawSurfaceType>& surfaceTypes, uint16_t bgCamCount) {
    if (surfaceTypes.empty()) {
        return -0x10000;
    }
    int score = 0;
    for (size_t index = 0; index < surfaceTypes.size(); ++index) {
        const int cameraDataIndex = SurfaceTypeCameraDataIndex(surfaceTypes[index]);
        const bool cameraValid = bgCamCount == 0 || cameraDataIndex < bgCamCount;
        score += cameraValid ? 4 : -4;
        if (index < 2 && !cameraValid) {
            score -= 8;
        }
        if ((surfaceTypes[index].Data2 & 0xFFFF) == 0x55DA) {
            score -= 8;
        }
    }
    return score;
}

std::vector<ZsiRawSurfaceType> ReadZsiSurfaceTypes(const std::vector<uint8_t>& data, size_t offset,
                                                   uint16_t surfaceTypeCount) {
    std::vector<ZsiRawSurfaceType> surfaceTypes;
    surfaceTypes.reserve(surfaceTypeCount);
    for (size_t index = 0; index < surfaceTypeCount; ++index) {
        surfaceTypes.push_back(ReadZsiSurfaceType(data, offset + index * 8));
    }
    return surfaceTypes;
}

std::pair<size_t, std::vector<ZsiRawSurfaceType>> FindZsiEffectiveSurfaceTypeLayout(
    const std::vector<uint8_t>& data, uint32_t surfaceTypeOffset, uint16_t surfaceTypeCount,
    size_t sectionEndOffset, uint16_t bgCamCount) {
    const auto raw = ReadZsiSurfaceTypes(data, surfaceTypeOffset, surfaceTypeCount);
    const size_t prefixedOffset = static_cast<size_t>(surfaceTypeOffset) + 0x10;
    const size_t prefixedEnd = prefixedOffset + static_cast<size_t>(surfaceTypeCount) * 8;
    if (prefixedEnd <= sectionEndOffset && SurfaceTypeHasSectionPrefix(raw)) {
        return { prefixedOffset, ReadZsiSurfaceTypes(data, prefixedOffset, surfaceTypeCount) };
    }
    if (prefixedEnd == sectionEndOffset && prefixedEnd <= data.size()) {
        const auto prefixed = ReadZsiSurfaceTypes(data, prefixedOffset, surfaceTypeCount);
        if (SurfaceTypeLayoutScore(prefixed, bgCamCount) > SurfaceTypeLayoutScore(raw, bgCamCount)) {
            return { prefixedOffset, prefixed };
        }
    }
    return { surfaceTypeOffset, raw };
}

std::optional<ZsiBgCameraLayout> FindZsiEffectiveBgCameraLayout(const std::vector<uint8_t>& data,
                                                                uint32_t bgCamOffset, uint16_t bgCamCount) {
    for (const auto effectiveOffset : { static_cast<size_t>(bgCamOffset), static_cast<size_t>(bgCamOffset) + 0x10 }) {
        if (effectiveOffset + static_cast<size_t>(bgCamCount) * 8 > data.size()) {
            continue;
        }

        ZsiBgCameraLayout layout;
        layout.EffectiveBgCamOffset = effectiveOffset;
        layout.CameraPositionOffset = effectiveOffset + static_cast<size_t>(bgCamCount) * 8;
        layout.CameraPointerAdjustment = static_cast<int>(effectiveOffset) - static_cast<int>(bgCamOffset);
        layout.Cameras.reserve(bgCamCount);
        layout.CameraPositionVectorIndices.reserve(bgCamCount);

        bool valid = true;
        size_t maxPositionVectorIndex = 0;
        for (size_t index = 0; index < bgCamCount; ++index) {
            const auto camera = ReadZsiBgCamera(data, effectiveOffset + index * 8);
            layout.Cameras.push_back(camera);
            if (camera.Setting > 0xFF || camera.Count > 0x40) {
                valid = false;
                break;
            }
            if (camera.Count == 0) {
                if (camera.DataOffset != 0) {
                    valid = false;
                    break;
                }
                layout.CameraPositionVectorIndices.push_back(0);
                continue;
            }

            const int adjustedOffset = static_cast<int>(camera.DataOffset) + layout.CameraPointerAdjustment;
            if (adjustedOffset < static_cast<int>(layout.CameraPositionOffset) ||
                static_cast<size_t>(adjustedOffset) + static_cast<size_t>(camera.Count) * 6 > data.size()) {
                valid = false;
                break;
            }
            if ((static_cast<size_t>(adjustedOffset) - layout.CameraPositionOffset) % 6 != 0) {
                valid = false;
                break;
            }
            const size_t vectorIndex = (static_cast<size_t>(adjustedOffset) - layout.CameraPositionOffset) / 6;
            layout.CameraPositionVectorIndices.push_back(static_cast<int>(vectorIndex));
            maxPositionVectorIndex = std::max(maxPositionVectorIndex, vectorIndex + camera.Count);
        }
        if (!valid) {
            continue;
        }

        layout.CameraPositionVectors.reserve(maxPositionVectorIndex);
        for (size_t index = 0; index < maxPositionVectorIndex; ++index) {
            layout.CameraPositionVectors.push_back(
                ReadZsiCameraPositionVector(data, layout.CameraPositionOffset + index * 6));
        }
        return layout;
    }
    return std::nullopt;
}

Oot3dDemoBounds BoundsFromVertices(const std::vector<Oot3dDemoVec3>& vertices) {
    Oot3dDemoBounds bounds;
    for (const auto& vertex : vertices) {
        if (!bounds.Valid) {
            bounds.Min = vertex;
            bounds.Max = vertex;
            bounds.Valid = true;
            continue;
        }
        bounds.Min.X = std::min(bounds.Min.X, vertex.X);
        bounds.Min.Y = std::min(bounds.Min.Y, vertex.Y);
        bounds.Min.Z = std::min(bounds.Min.Z, vertex.Z);
        bounds.Max.X = std::max(bounds.Max.X, vertex.X);
        bounds.Max.Y = std::max(bounds.Max.Y, vertex.Y);
        bounds.Max.Z = std::max(bounds.Max.Z, vertex.Z);
    }
    return bounds;
}

double BoundsDeltaScore(const Oot3dDemoBounds& expected, const Oot3dDemoBounds& actual, bool maxOnly) {
    if (!expected.Valid || !actual.Valid) {
        return std::numeric_limits<double>::infinity();
    }
    const double deltas[] = {
        std::abs(expected.Min.X - actual.Min.X),
        std::abs(expected.Min.Y - actual.Min.Y),
        std::abs(expected.Min.Z - actual.Min.Z),
        std::abs(expected.Max.X - actual.Max.X),
        std::abs(expected.Max.Y - actual.Max.Y),
        std::abs(expected.Max.Z - actual.Max.Z),
    };
    if (maxOnly) {
        return *std::max_element(std::begin(deltas), std::end(deltas));
    }
    double total = 0.0;
    for (const auto delta : deltas) {
        total += delta;
    }
    return total;
}

std::optional<size_t> FindZsiEffectiveVertexLayout(const std::vector<uint8_t>& data, uint32_t vertexOffset,
                                                   size_t sectionEndOffset, uint16_t vertexCount,
                                                   const Oot3dDemoBounds& headerBounds) {
    struct Candidate {
        size_t Offset = 0;
        size_t PrefixSize = 0;
        double MaxDelta = 0.0;
        double TotalDelta = 0.0;
        size_t SectionEndGap = 0;
    };
    std::vector<Candidate> candidates;

    for (size_t prefixSize = 0; prefixSize < 0x20; prefixSize += 2) {
        const size_t candidateOffset = static_cast<size_t>(vertexOffset) + prefixSize;
        const size_t candidateEnd = candidateOffset + static_cast<size_t>(vertexCount) * 6;
        if (candidateEnd > data.size() || (sectionEndOffset > 0 && candidateEnd > sectionEndOffset)) {
            continue;
        }

        std::vector<Oot3dDemoVec3> vertices;
        vertices.reserve(vertexCount);
        for (size_t index = 0; index < vertexCount; ++index) {
            vertices.push_back(ReadZsiCollisionVertex(data, candidateOffset + index * 6));
        }
        const auto bounds = BoundsFromVertices(vertices);
        const size_t sectionEndGap = sectionEndOffset > candidateEnd ? sectionEndOffset - candidateEnd
                                                                     : candidateEnd - sectionEndOffset;
        candidates.push_back({
            candidateOffset,
            prefixSize,
            BoundsDeltaScore(headerBounds, bounds, true),
            BoundsDeltaScore(headerBounds, bounds, false),
            sectionEndGap,
        });
    }

    if (candidates.empty()) {
        return std::nullopt;
    }
    const auto best = std::min_element(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return std::tie(left.MaxDelta, left.TotalDelta, left.SectionEndGap, left.PrefixSize, left.Offset) <
               std::tie(right.MaxDelta, right.TotalDelta, right.SectionEndGap, right.PrefixSize, right.Offset);
    });
    return best->Offset;
}

constexpr uint8_t kZsiSceneSpawnListCommandId = 0x00;
constexpr uint8_t kZsiSceneCollisionCommandId = 0x03;
constexpr uint8_t kZsiSceneRoomListCommandId = 0x04;
constexpr uint8_t kZsiSceneEntranceListCommandId = 0x06;
constexpr uint8_t kZsiSceneEndCommandId = 0x14;
constexpr uint8_t kZsiSceneFirstCommandId = 0x15;
constexpr uint8_t kZsiSceneCutsceneCommandId = 0x17;
constexpr uint8_t kZsiRoomActorListCommandId = 0x01;
constexpr uint8_t kZsiRoomMeshCommandId = 0x0A;
constexpr uint8_t kZsiRoomObjectListCommandId = 0x0B;
constexpr uint8_t kZsiLightListCommandId = 0x0C;
constexpr uint8_t kZsiLightSettingsListCommandId = 0x0F;
constexpr uint8_t kZsiRoomAlternateHeaderListCommandId = 0x18;
constexpr size_t kZsiActorEntrySize = 0x10;
constexpr size_t kZsiObjectEntrySize = 0x02;
constexpr size_t kZsiEntranceEntrySize = 2;
constexpr size_t kZsiRoomCommandTableDefaultOffset = 0x10;
constexpr size_t kZsiFilePointerBaseOffset = 0x10;
constexpr int kZsiNativeIdMax = 0x03FF;
constexpr int kZsiPlayerActorId = 0;

std::vector<ZsiSceneSetup> ZsiSceneSetups(const std::vector<uint8_t>& data) {
    std::vector<ZsiSceneSetup> setups;
    size_t cursor = 0x18;
    int setupIndex = 0;

    while (CanRead(data, cursor, 8)) {
        const uint32_t firstWord = ReadLeU32(data, cursor);
        if ((firstWord & 0xFF) != kZsiSceneFirstCommandId) {
            break;
        }

        ZsiSceneSetup setup;
        setup.Index = setupIndex;
        setup.Offset = cursor;
        size_t commandOffset = cursor;
        while (CanRead(data, commandOffset, 8)) {
            ZsiSceneCommand command;
            command.SetupIndex = setupIndex;
            command.Offset = commandOffset;
            command.CommandWord = ReadLeU32(data, commandOffset);
            command.Argument = ReadLeU32(data, commandOffset + 4);
            setup.Commands.push_back(command);
            commandOffset += 8;
            if (ZsiCommandId(command) == kZsiSceneEndCommandId) {
                break;
            }
        }

        if (!CanRead(data, commandOffset - 8, 8) ||
            (ReadLeU32(data, commandOffset - 8) & 0xFF) != kZsiSceneEndCommandId) {
            break;
        }
        setup.EndOffset = commandOffset;
        setups.push_back(std::move(setup));
        cursor = commandOffset;
        ++setupIndex;
    }
    return setups;
}

const ZsiSceneCommand* FirstZsiSceneCommand(const ZsiSceneSetup& setup, uint8_t commandId) {
    const auto it = std::find_if(setup.Commands.begin(), setup.Commands.end(), [commandId](const auto& command) {
        return ZsiCommandId(command) == commandId;
    });
    return it == setup.Commands.end() ? nullptr : &*it;
}

std::vector<ZsiSceneCommand> ZsiCollisionCommands(const std::vector<uint8_t>& data) {
    std::vector<ZsiSceneCommand> collisionCommands;
    for (const auto& setup : ZsiSceneSetups(data)) {
        const auto* collisionCommand = FirstZsiSceneCommand(setup, kZsiSceneCollisionCommandId);
        if (collisionCommand != nullptr) {
            collisionCommands.push_back(*collisionCommand);
        }
    }
    return collisionCommands;
}

struct ZsiActorEntry {
    int Index = -1;
    size_t Offset = 0;
    int ActorId = -1;
    Oot3dDemoVec3 Position;
    Oot3dDemoVec3 Rotation;
    int Params = 0;
};

struct ZsiEntranceEntry {
    int Index = -1;
    size_t Offset = 0;
    int Spawn = -1;
    int Room = -1;
};

struct ZsiPlayerSpawnCandidate {
    size_t StartOffset = 0;
    int StartDelta = 0;
    int EntryCount = 0;
    int PlayerEntryCount = 0;
    int PlausiblePlayerEntryCount = 0;
    std::vector<ZsiActorEntry> Entries;
};

struct ZsiEntranceCandidate {
    size_t StartOffset = 0;
    int StartDelta = 0;
    int EntryCount = 0;
    int ValidEntryCount = 0;
    int SequentialEntryCount = 0;
    std::vector<ZsiEntranceEntry> Entries;
};

bool ZsiActorPositionIsPlausible(const ZsiActorEntry& entry) {
    return entry.Position.X >= -20000.0 && entry.Position.X <= 20000.0 &&
           entry.Position.Y >= -20000.0 && entry.Position.Y <= 20000.0 &&
           entry.Position.Z >= -20000.0 && entry.Position.Z <= 20000.0;
}

ZsiActorEntry ReadZsiActorEntry(const std::vector<uint8_t>& data, size_t offset, int index) {
    return {
        index,
        offset,
        static_cast<int>(ReadLeS16(data, offset + 0x00)),
        { static_cast<double>(ReadLeS16(data, offset + 0x02)),
          static_cast<double>(ReadLeS16(data, offset + 0x04)),
          static_cast<double>(ReadLeS16(data, offset + 0x06)) },
        { static_cast<double>(ReadLeS16(data, offset + 0x08)),
          static_cast<double>(ReadLeS16(data, offset + 0x0A)),
          static_cast<double>(ReadLeS16(data, offset + 0x0C)) },
        static_cast<int>(ReadLeS16(data, offset + 0x0E)),
    };
}

ZsiEntranceEntry ReadZsiEntranceEntry(const std::vector<uint8_t>& data, size_t offset, int index) {
    return {
        index,
        offset,
        static_cast<int>(data[offset]),
        static_cast<int>(static_cast<int8_t>(data[offset + 1])),
    };
}

std::optional<ZsiPlayerSpawnCandidate> SelectZsiPlayerSpawnCandidate(const std::vector<uint8_t>& data, size_t offset,
                                                                     int count) {
    if (count <= 0) {
        return std::nullopt;
    }

    std::optional<ZsiPlayerSpawnCandidate> bestCandidate;
    std::tuple<int, int, int, int> bestScore{ -1, -1, -1, -1 };
    for (const size_t startDelta : { size_t{ 0x10 }, size_t{ 0x00 }, size_t{ 0x04 }, size_t{ 0x08 }, size_t{ 0x0C } }) {
        const size_t start = offset + startDelta;
        const size_t byteCount = static_cast<size_t>(count) * kZsiActorEntrySize;
        if (!CanRead(data, start, byteCount)) {
            continue;
        }

        ZsiPlayerSpawnCandidate candidate;
        candidate.StartOffset = start;
        candidate.StartDelta = static_cast<int>(startDelta);
        candidate.EntryCount = count;
        candidate.Entries.reserve(count);
        for (int index = 0; index < count; ++index) {
            auto entry = ReadZsiActorEntry(data, start + static_cast<size_t>(index) * kZsiActorEntrySize, index);
            if (entry.ActorId == kZsiPlayerActorId) {
                ++candidate.PlayerEntryCount;
                if (ZsiActorPositionIsPlausible(entry)) {
                    ++candidate.PlausiblePlayerEntryCount;
                }
            }
            candidate.Entries.push_back(entry);
        }

        const std::tuple<int, int, int, int> score{
            candidate.PlausiblePlayerEntryCount,
            candidate.PlayerEntryCount,
            candidate.StartDelta == 0x10 ? 1 : 0,
            -candidate.StartDelta,
        };
        if (score > bestScore) {
            bestScore = score;
            bestCandidate = std::move(candidate);
        }
    }

    if (!bestCandidate.has_value() || bestCandidate->PlausiblePlayerEntryCount == 0) {
        return std::nullopt;
    }
    return bestCandidate;
}

std::optional<ZsiEntranceCandidate> SelectZsiEntranceCandidate(const std::vector<uint8_t>& data, size_t offset,
                                                               int count, int spawnCount) {
    if (count <= 0 || spawnCount <= 0) {
        return std::nullopt;
    }

    std::optional<ZsiEntranceCandidate> bestCandidate;
    std::tuple<int, int, int, int> bestScore{ -1, -1, -1, -1 };
    for (const size_t startDelta : { size_t{ 0x10 }, size_t{ 0x00 }, size_t{ 0x04 }, size_t{ 0x08 }, size_t{ 0x0C } }) {
        const size_t start = offset + startDelta;
        const size_t byteCount = static_cast<size_t>(count) * kZsiEntranceEntrySize;
        if (!CanRead(data, start, byteCount)) {
            continue;
        }

        ZsiEntranceCandidate candidate;
        candidate.StartOffset = start;
        candidate.StartDelta = static_cast<int>(startDelta);
        candidate.EntryCount = count;
        candidate.Entries.reserve(count);
        int previousSpawn = -1;
        for (int index = 0; index < count; ++index) {
            auto entry = ReadZsiEntranceEntry(data, start + static_cast<size_t>(index) * kZsiEntranceEntrySize, index);
            if (entry.Spawn >= 0 && entry.Spawn < spawnCount && entry.Room >= -1 && entry.Room <= 63) {
                ++candidate.ValidEntryCount;
            }
            if (previousSpawn >= 0 && entry.Spawn == previousSpawn + 1) {
                ++candidate.SequentialEntryCount;
            }
            previousSpawn = entry.Spawn;
            candidate.Entries.push_back(entry);
        }

        const std::tuple<int, int, int, int> score{
            candidate.ValidEntryCount,
            candidate.SequentialEntryCount,
            candidate.StartDelta == 0x10 ? 1 : 0,
            -candidate.StartDelta,
        };
        if (score > bestScore) {
            bestScore = score;
            bestCandidate = std::move(candidate);
        }
    }

    if (!bestCandidate.has_value() || bestCandidate->ValidEntryCount != count) {
        return std::nullopt;
    }
    return bestCandidate;
}

std::optional<Oot3dNativeDemoPlayerStart> TryBuildPlayerStartFromSetup(const std::vector<uint8_t>& data,
                                                                       const ZsiSceneSetup& setup,
                                                                       int preferredEntranceIndex,
                                                                       std::string_view preferredEntranceSource) {
    const auto* spawnCommand = FirstZsiSceneCommand(setup, kZsiSceneSpawnListCommandId);
    if (spawnCommand == nullptr) {
        return std::nullopt;
    }

    auto spawnCandidate =
        SelectZsiPlayerSpawnCandidate(data, static_cast<size_t>(spawnCommand->Argument), ZsiCommandParameter(*spawnCommand));
    if (!spawnCandidate.has_value()) {
        return std::nullopt;
    }

    const auto* entranceCommand = FirstZsiSceneCommand(setup, kZsiSceneEntranceListCommandId);
    std::optional<ZsiEntranceCandidate> entranceCandidate;
    if (entranceCommand != nullptr) {
        entranceCandidate = SelectZsiEntranceCandidate(data, static_cast<size_t>(entranceCommand->Argument),
                                                       ZsiCommandParameter(*entranceCommand),
                                                       spawnCandidate->EntryCount);
    }

    std::vector<int> spawnIndices;
    if (preferredEntranceIndex >= 0) {
        if (!entranceCandidate.has_value()) {
            return std::nullopt;
        }
        const auto entranceIt = std::find_if(
            entranceCandidate->Entries.begin(), entranceCandidate->Entries.end(),
            [preferredEntranceIndex](const auto& entrance) { return entrance.Index == preferredEntranceIndex; });
        if (entranceIt == entranceCandidate->Entries.end()) {
            return std::nullopt;
        }
        spawnIndices.push_back(entranceIt->Spawn);
    } else if (entranceCandidate.has_value()) {
        for (const auto& entry : entranceCandidate->Entries) {
            if (std::find(spawnIndices.begin(), spawnIndices.end(), entry.Spawn) == spawnIndices.end()) {
                spawnIndices.push_back(entry.Spawn);
            }
        }
    } else {
        for (int index = 0; index < spawnCandidate->EntryCount; ++index) {
            spawnIndices.push_back(index);
        }
    }

    for (const int spawnIndex : spawnIndices) {
        if (spawnIndex < 0 || spawnIndex >= static_cast<int>(spawnCandidate->Entries.size())) {
            continue;
        }
        const auto& entry = spawnCandidate->Entries[static_cast<size_t>(spawnIndex)];
        if (entry.ActorId != kZsiPlayerActorId || !ZsiActorPositionIsPlausible(entry)) {
            continue;
        }

        Oot3dNativeDemoPlayerStart playerStart;
        playerStart.Valid = true;
        playerStart.SetupIndex = setup.Index;
        playerStart.SetupOffset = static_cast<int>(setup.Offset);
        playerStart.SpawnCommandOffset = static_cast<int>(spawnCommand->Offset);
        playerStart.SpawnListOffset = static_cast<int>(spawnCandidate->StartOffset);
        playerStart.SpawnListStartDelta = spawnCandidate->StartDelta;
        playerStart.SpawnIndex = entry.Index;
        playerStart.RequestedEntranceIndex = preferredEntranceIndex;
        playerStart.ActorId = entry.ActorId;
        playerStart.Position = entry.Position;
        playerStart.Rotation = entry.Rotation;
        playerStart.Params = entry.Params;
        playerStart.CameraDataIndex = static_cast<int>(static_cast<uint16_t>(entry.Params) & 0x00FF);
        playerStart.SelectionSource =
            preferredEntranceIndex >= 0 ? std::string(preferredEntranceSource) : "first_valid_native_entrance";

        if (entranceCommand != nullptr) {
            playerStart.EntranceCommandOffset = static_cast<int>(entranceCommand->Offset);
        }
        if (entranceCandidate.has_value()) {
            playerStart.EntranceListOffset = static_cast<int>(entranceCandidate->StartOffset);
            playerStart.EntranceListStartDelta = entranceCandidate->StartDelta;
            const auto entranceIt = std::find_if(
                entranceCandidate->Entries.begin(), entranceCandidate->Entries.end(),
                [spawnIndex](const auto& entrance) { return entrance.Spawn == spawnIndex; });
            if (entranceIt != entranceCandidate->Entries.end()) {
                playerStart.EntranceIndex = entranceIt->Index;
                playerStart.Room = entranceIt->Room;
            }
        }
        return playerStart;
    }
    return std::nullopt;
}

Oot3dNativeDemoPlayerStart DecodeOot3dNativeDemoPlayerStart(const std::filesystem::path& scenePath,
                                                            int preferredEntranceIndex,
                                                            std::string_view preferredEntranceSource) {
    Oot3dNativeDemoPlayerStart fallback;
    if (scenePath.empty() || !std::filesystem::is_regular_file(scenePath)) {
        return fallback;
    }

    const auto data = ReadBinaryFile(scenePath);
    if (data.size() < 4 || data[0] != 'Z' || data[1] != 'S' || data[2] != 'I' || data[3] != 0x01) {
        return fallback;
    }

    std::optional<Oot3dNativeDemoPlayerStart> cutsceneFallback;
    for (const auto& setup : ZsiSceneSetups(data)) {
        auto playerStart =
            TryBuildPlayerStartFromSetup(data, setup, preferredEntranceIndex, preferredEntranceSource);
        if (!playerStart.has_value()) {
            continue;
        }
        if (FirstZsiSceneCommand(setup, kZsiSceneCutsceneCommandId) == nullptr) {
            return *playerStart;
        }
        if (!cutsceneFallback.has_value()) {
            cutsceneFallback = playerStart;
        }
    }
    return cutsceneFallback.value_or(fallback);
}

bool ZsiCommandIdLooksPlausible(uint8_t commandId) {
    return commandId <= 0x19;
}

bool ZsiCommandUsesFileOffset(uint8_t commandId) {
    switch (commandId) {
        case kZsiSceneSpawnListCommandId:
        case kZsiRoomActorListCommandId:
        case kZsiSceneCollisionCommandId:
        case kZsiSceneRoomListCommandId:
        case kZsiSceneEntranceListCommandId:
        case kZsiRoomMeshCommandId:
        case kZsiRoomObjectListCommandId:
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F:
        case 0x13:
        case kZsiSceneCutsceneCommandId:
        case kZsiRoomAlternateHeaderListCommandId:
            return true;
        default:
            return false;
    }
}

std::string ZsiCommandName(uint8_t commandId) {
    switch (commandId) {
        case 0x00:
            return "spawn_list";
        case 0x01:
            return "actor_list";
        case 0x03:
            return "collision_header";
        case 0x04:
            return "room_list";
        case 0x05:
            return "wind_settings";
        case 0x06:
            return "entrance_list";
        case 0x07:
            return "special_files";
        case 0x08:
            return "room_behavior";
        case 0x0A:
            return "mesh_header";
        case 0x0B:
            return "object_list";
        case 0x0C:
            return "light_list";
        case 0x0D:
            return "path_list";
        case 0x0E:
            return "transition_actor_list";
        case 0x0F:
            return "light_settings_list";
        case 0x10:
            return "time_settings";
        case 0x11:
            return "skybox_settings";
        case 0x12:
            return "skybox_disables";
        case 0x13:
            return "exit_list";
        case 0x14:
            return "end";
        case 0x15:
            return "sound_settings";
        case 0x16:
            return "echo_settings";
        case 0x17:
            return "cutscene_data";
        case 0x18:
            return "alternate_header_list";
        case 0x19:
            return "misc_settings";
        default:
            return "unknown";
    }
}

Oot3dNativeDemoZsiCommandRecord ZsiCommandRecordFromCommand(const ZsiSceneCommand& command, size_t fileSize) {
    return {
        command.SetupIndex,
        static_cast<int>(command.Offset),
        static_cast<int>(ZsiCommandId(command)),
        static_cast<int>(ZsiCommandParameter(command)),
        command.CommandWord,
        command.Argument,
        ZsiCommandUsesFileOffset(ZsiCommandId(command)) && IsFileOffset(command.Argument, fileSize),
    };
}

std::vector<Oot3dNativeDemoZsiCommandRecord> ZsiCommandRecordsFromSetups(const std::vector<ZsiSceneSetup>& setups,
                                                                         size_t fileSize) {
    std::vector<Oot3dNativeDemoZsiCommandRecord> records;
    for (const auto& setup : setups) {
        for (const auto& command : setup.Commands) {
            records.push_back(ZsiCommandRecordFromCommand(command, fileSize));
        }
    }
    return records;
}

std::vector<Oot3dNativeDemoZsiCommandRecord> ZsiCommandRecordsFromCommands(
    const std::vector<ZsiSceneCommand>& commands, size_t fileSize) {
    std::vector<Oot3dNativeDemoZsiCommandRecord> records;
    records.reserve(commands.size());
    for (const auto& command : commands) {
        records.push_back(ZsiCommandRecordFromCommand(command, fileSize));
    }
    return records;
}

std::optional<std::vector<ZsiSceneCommand>> TryReadZsiInlineCommandTable(const std::vector<uint8_t>& data,
                                                                         size_t tableOffset) {
    std::vector<ZsiSceneCommand> commands;
    bool foundEnd = false;
    size_t commandOffset = tableOffset;
    for (int commandCount = 0; commandCount < 64 && CanRead(data, commandOffset, 8); ++commandCount) {
        ZsiSceneCommand command;
        command.SetupIndex = -1;
        command.Offset = commandOffset;
        command.CommandWord = ReadLeU32(data, commandOffset);
        command.Argument = ReadLeU32(data, commandOffset + 4);
        if (!ZsiCommandIdLooksPlausible(ZsiCommandId(command))) {
            return std::nullopt;
        }
        commands.push_back(command);
        commandOffset += 8;
        if (ZsiCommandId(command) == kZsiSceneEndCommandId) {
            foundEnd = true;
            break;
        }
    }
    if (!foundEnd || commands.empty()) {
        return std::nullopt;
    }
    return commands;
}

bool ZsiInlineCommandTableLooksLikeRoom(const std::vector<ZsiSceneCommand>& commands) {
    bool hasRoomHeader = false;
    bool hasRoomPayload = false;
    for (const auto& command : commands) {
        const uint8_t commandId = ZsiCommandId(command);
        hasRoomHeader = hasRoomHeader || commandId == kZsiRoomAlternateHeaderListCommandId;
        hasRoomPayload = hasRoomPayload || commandId == kZsiRoomMeshCommandId ||
                         commandId == kZsiRoomObjectListCommandId ||
                         commandId == kZsiRoomActorListCommandId;
    }
    return hasRoomHeader && hasRoomPayload;
}

bool ZsiInlineCommandTableHasRoomPayload(const std::vector<ZsiSceneCommand>& commands) {
    return std::any_of(commands.begin(), commands.end(), [](const auto& command) {
        const uint8_t commandId = ZsiCommandId(command);
        return commandId == kZsiRoomMeshCommandId || commandId == kZsiRoomObjectListCommandId ||
               commandId == kZsiRoomActorListCommandId;
    });
}

std::optional<std::pair<size_t, std::vector<ZsiSceneCommand>>> FindZsiRoomCommandTable(
    const std::vector<uint8_t>& data) {
    if (auto commands = TryReadZsiInlineCommandTable(data, kZsiRoomCommandTableDefaultOffset)) {
        if (ZsiInlineCommandTableLooksLikeRoom(*commands)) {
            return std::make_pair(kZsiRoomCommandTableDefaultOffset, std::move(*commands));
        }
    }

    for (size_t offset = 0; offset < 0x80 && CanRead(data, offset, 8); offset += 8) {
        if (offset == kZsiRoomCommandTableDefaultOffset) {
            continue;
        }
        auto commands = TryReadZsiInlineCommandTable(data, offset);
        if (commands && ZsiInlineCommandTableLooksLikeRoom(*commands)) {
            return std::make_pair(offset, std::move(*commands));
        }
    }
    return std::nullopt;
}

const ZsiSceneCommand* FirstZsiInlineCommand(const std::vector<ZsiSceneCommand>& commands, uint8_t commandId) {
    const auto it = std::find_if(commands.begin(), commands.end(), [commandId](const auto& command) {
        return ZsiCommandId(command) == commandId;
    });
    return it == commands.end() ? nullptr : &*it;
}

struct ZsiRoomCommandTableSelection {
    size_t BaseOffset = 0;
    size_t Offset = 0;
    int RequestedSetupIndex = -1;
    int AlternateHeaderListOffset = -1;
    int AlternateHeaderEntryIndex = -1;
    uint32_t AlternateHeaderRawOffset = 0;
    std::string Status;
    std::vector<ZsiSceneCommand> Commands;
};

std::optional<ZsiRoomCommandTableSelection> SelectZsiRoomCommandTableForSetup(
    const std::vector<uint8_t>& data, int activeSetupIndex) {
    auto base = FindZsiRoomCommandTable(data);
    if (!base.has_value()) {
        return std::nullopt;
    }

    ZsiRoomCommandTableSelection selection;
    selection.BaseOffset = base->first;
    selection.Offset = base->first;
    selection.RequestedSetupIndex = activeSetupIndex;
    selection.Commands = base->second;
    selection.Status = activeSetupIndex > 0 ? "native_room_alternate_header_unavailable_base_fallback"
                                             : "native_room_base_header_selected";
    if (activeSetupIndex <= 0) {
        return selection;
    }

    const auto* alternateList = FirstZsiInlineCommand(base->second, kZsiRoomAlternateHeaderListCommandId);
    if (alternateList == nullptr) {
        return selection;
    }
    selection.AlternateHeaderListOffset =
        static_cast<int>(alternateList->Argument + kZsiFilePointerBaseOffset);
    selection.AlternateHeaderEntryIndex = activeSetupIndex - 1;
    const int alternateCount = static_cast<int>(ZsiCommandParameter(*alternateList));
    if (selection.AlternateHeaderEntryIndex < 0 || selection.AlternateHeaderEntryIndex >= alternateCount) {
        selection.Status = "native_room_alternate_header_setup_out_of_range_base_fallback";
        return selection;
    }

    const size_t pointerOffset = static_cast<size_t>(selection.AlternateHeaderListOffset) +
                                 static_cast<size_t>(selection.AlternateHeaderEntryIndex) * sizeof(uint32_t);
    if (!CanRead(data, pointerOffset, sizeof(uint32_t))) {
        selection.Status = "native_room_alternate_header_pointer_out_of_file_base_fallback";
        return selection;
    }

    selection.AlternateHeaderRawOffset = ReadLeU32(data, pointerOffset);
    if (selection.AlternateHeaderRawOffset == 0) {
        selection.Status = "native_room_alternate_header_null_inherits_base";
        return selection;
    }

    const size_t headerOffset = static_cast<size_t>(selection.AlternateHeaderRawOffset) +
                                kZsiFilePointerBaseOffset;
    auto commands = TryReadZsiInlineCommandTable(data, headerOffset);
    if (!commands.has_value() || !ZsiInlineCommandTableHasRoomPayload(*commands)) {
        selection.Status = "native_room_alternate_header_invalid_base_fallback";
        return selection;
    }

    selection.Offset = headerOffset;
    selection.Commands = std::move(*commands);
    selection.Status = "native_room_alternate_header_selected_from_command_0x18";
    return selection;
}

size_t ZsiPayloadEndHint(const std::vector<uint8_t>& data, const std::vector<ZsiSceneCommand>& commands,
                         const ZsiSceneCommand& command) {
    if (!IsFileOffset(command.Argument, data.size())) {
        return data.size();
    }
    size_t payloadEnd = data.size();
    for (const auto& other : commands) {
        if (other.Offset == command.Offset || !ZsiCommandUsesFileOffset(ZsiCommandId(other)) ||
            !IsFileOffset(other.Argument, data.size())) {
            continue;
        }
        if (other.Argument > command.Argument && static_cast<size_t>(other.Argument) < payloadEnd) {
            payloadEnd = static_cast<size_t>(other.Argument);
        }
    }
    return payloadEnd;
}

size_t ZsiRoomPayloadEndHint(const std::vector<uint8_t>& data, const std::vector<ZsiSceneCommand>& commands,
                            const ZsiSceneCommand& command) {
    size_t payloadEnd = data.size();
    for (const auto& other : commands) {
        if (other.Offset == command.Offset || !ZsiCommandUsesFileOffset(ZsiCommandId(other)) ||
            !IsFileOffset(other.Argument, data.size()) || other.Argument <= command.Argument) {
            continue;
        }
        const size_t relocatedOffset = static_cast<size_t>(other.Argument) + kZsiFilePointerBaseOffset;
        if (relocatedOffset < payloadEnd) {
            payloadEnd = relocatedOffset;
        }
    }
    return payloadEnd;
}

bool BytesStartWith(const std::vector<uint8_t>& data, size_t offset, std::string_view value) {
    if (!CanRead(data, offset, value.size())) {
        return false;
    }
    for (size_t index = 0; index < value.size(); ++index) {
        if (data[offset + index] != static_cast<uint8_t>(value[index])) {
            return false;
        }
    }
    return true;
}

bool IsRomPathByte(uint8_t value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == ':' || value == '/' || value == '_' || value == '-' ||
           value == '.';
}

bool StringEndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size(), suffix.size()) == suffix;
}

std::vector<std::pair<size_t, std::string>> ExtractRomSceneZsiPaths(const std::vector<uint8_t>& data,
                                                                    size_t startOffset, size_t endOffset) {
    constexpr std::string_view kRomScenePrefix = "rom:/scene/";
    std::vector<std::pair<size_t, std::string>> paths;
    const size_t boundedEnd = std::min({ endOffset, data.size(), startOffset + size_t{ 0x1000 } });
    for (size_t cursor = startOffset; cursor + kRomScenePrefix.size() < boundedEnd; ++cursor) {
        if (!BytesStartWith(data, cursor, kRomScenePrefix)) {
            continue;
        }

        size_t end = cursor;
        while (end < boundedEnd && data[end] != 0 && IsRomPathByte(data[end])) {
            ++end;
        }
        if (end == cursor || end >= boundedEnd) {
            continue;
        }
        std::string path(reinterpret_cast<const char*>(data.data() + cursor), end - cursor);
        if (!StringEndsWith(path, ".zsi")) {
            continue;
        }
        const auto duplicate = std::find_if(paths.begin(), paths.end(), [&path](const auto& existing) {
            return existing.second == path;
        });
        if (duplicate == paths.end()) {
            paths.emplace_back(cursor, std::move(path));
        }
    }
    return paths;
}

std::filesystem::path ResolveOot3dRomPath(const std::filesystem::path& sourcePath, const std::string& romPath) {
    constexpr std::string_view kRomPrefix = "rom:/";
    if (romPath.rfind(std::string(kRomPrefix), 0) == 0) {
        return sourcePath.parent_path().parent_path() / romPath.substr(kRomPrefix.size());
    }
    return sourcePath.parent_path() / romPath;
}

std::vector<Oot3dNativeDemoRoomReference> DecodeZsiRoomReferences(const std::filesystem::path& scenePath,
                                                                  const std::vector<uint8_t>& data,
                                                                  const std::vector<ZsiSceneSetup>& setups) {
    std::vector<Oot3dNativeDemoRoomReference> references;
    for (const auto& setup : setups) {
        for (const auto& command : setup.Commands) {
            if (ZsiCommandId(command) != kZsiSceneRoomListCommandId ||
                !IsFileOffset(command.Argument, data.size())) {
                continue;
            }

            const size_t payloadEnd = ZsiPayloadEndHint(data, setup.Commands, command);
            auto paths = ExtractRomSceneZsiPaths(data, static_cast<size_t>(command.Argument), payloadEnd);
            int index = 0;
            for (const auto& [offset, romPath] : paths) {
                const auto resolved = ResolveOot3dRomPath(scenePath, romPath);
                references.push_back({
                    setup.Index,
                    static_cast<int>(command.Offset),
                    index,
                    static_cast<int>(offset),
                    romPath,
                    resolved,
                    std::filesystem::is_regular_file(resolved),
                });
                ++index;
            }
        }
    }
    return references;
}

bool ZsiNativeIdIsPossible(int value) {
    return value >= 0 && value <= kZsiNativeIdMax;
}

struct ZsiRoomObjectListCandidate {
    Oot3dNativeDemoRoomPayloadList List;
    std::vector<Oot3dNativeDemoRoomObjectEntry> Entries;
    int PossibleObjectCount = 0;
};

std::optional<ZsiRoomObjectListCandidate> SelectZsiRoomObjectListCandidate(
    const std::vector<uint8_t>& data, const std::vector<ZsiSceneCommand>& commands, const ZsiSceneCommand& command) {
    const int count = static_cast<int>(ZsiCommandParameter(command));
    if (count <= 0 || !IsFileOffset(command.Argument, data.size())) {
        return std::nullopt;
    }

    const size_t payloadEnd = ZsiRoomPayloadEndHint(data, commands, command);
    const size_t startDelta = kZsiFilePointerBaseOffset;
    const size_t start = static_cast<size_t>(command.Argument) + startDelta;
    const size_t byteCount = static_cast<size_t>(count) * kZsiObjectEntrySize;
    if (!CanRead(data, start, byteCount) || start + byteCount > payloadEnd) {
        return std::nullopt;
    }

    ZsiRoomObjectListCandidate candidate;
    candidate.List.Valid = true;
    candidate.List.CommandOffset = static_cast<int>(command.Offset);
    candidate.List.CommandArgument = static_cast<int>(command.Argument);
    candidate.List.Count = count;
    candidate.List.StartOffset = static_cast<int>(start);
    candidate.List.StartDelta = static_cast<int>(startDelta);
    candidate.List.EndOffset = static_cast<int>(start + byteCount);
    candidate.List.PayloadEndHint = static_cast<int>(payloadEnd);
    candidate.List.PayloadEndGap = static_cast<int>(payloadEnd - (start + byteCount));
    candidate.List.EntrySize = static_cast<int>(kZsiObjectEntrySize);
    candidate.List.Interpretation = "oot3d_room_object_list_command_0x0b_zsi_file_base_relative";
    candidate.Entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        const size_t offset = start + static_cast<size_t>(index) * kZsiObjectEntrySize;
        const int objectId = static_cast<int>(ReadLeS16(data, offset));
        const bool possible = ZsiNativeIdIsPossible(objectId);
        candidate.PossibleObjectCount += possible ? 1 : 0;
        candidate.Entries.push_back({ index, static_cast<int>(offset), objectId, possible });
    }
    if (candidate.PossibleObjectCount == 0) {
        return std::nullopt;
    }
    return candidate;
}

bool ZsiRoomActorEntryIsPlausible(const Oot3dNativeDemoRoomActorEntry& entry) {
    return entry.ActorId > 0 && entry.ActorId <= kZsiNativeIdMax && entry.Position.X >= -20000.0 &&
           entry.Position.X <= 20000.0 && entry.Position.Y >= -20000.0 && entry.Position.Y <= 20000.0 &&
           entry.Position.Z >= -20000.0 && entry.Position.Z <= 20000.0;
}

Oot3dNativeDemoRoomActorEntry ReadZsiRoomActorEntry(const std::vector<uint8_t>& data, size_t offset, int index) {
    Oot3dNativeDemoRoomActorEntry entry;
    const auto raw = ReadZsiActorEntry(data, offset, index);
    entry.Index = raw.Index;
    entry.Offset = static_cast<int>(raw.Offset);
    entry.ActorId = raw.ActorId;
    entry.Position = raw.Position;
    entry.Rotation = raw.Rotation;
    entry.Params = raw.Params;
    entry.Plausible = ZsiRoomActorEntryIsPlausible(entry);
    return entry;
}

struct ZsiRoomActorListCandidate {
    Oot3dNativeDemoRoomPayloadList List;
    std::vector<Oot3dNativeDemoRoomActorEntry> Entries;
    int PlausibleActorCount = 0;
    int NonPlayerActorCount = 0;
};

std::optional<ZsiRoomActorListCandidate> SelectZsiRoomActorListCandidate(
    const std::vector<uint8_t>& data, const std::vector<ZsiSceneCommand>& commands, const ZsiSceneCommand& command) {
    const int count = static_cast<int>(ZsiCommandParameter(command));
    if (count <= 0 || !IsFileOffset(command.Argument, data.size())) {
        return std::nullopt;
    }

    const size_t payloadEnd = ZsiRoomPayloadEndHint(data, commands, command);
    const size_t startDelta = kZsiFilePointerBaseOffset;
    const size_t start = static_cast<size_t>(command.Argument) + startDelta;
    const size_t byteCount = static_cast<size_t>(count) * kZsiActorEntrySize;
    if (!CanRead(data, start, byteCount) || start + byteCount > payloadEnd) {
        return std::nullopt;
    }

    ZsiRoomActorListCandidate candidate;
    candidate.List.Valid = true;
    candidate.List.CommandOffset = static_cast<int>(command.Offset);
    candidate.List.CommandArgument = static_cast<int>(command.Argument);
    candidate.List.Count = count;
    candidate.List.StartOffset = static_cast<int>(start);
    candidate.List.StartDelta = static_cast<int>(startDelta);
    candidate.List.EndOffset = static_cast<int>(start + byteCount);
    candidate.List.PayloadEndHint = static_cast<int>(payloadEnd);
    candidate.List.PayloadEndGap = static_cast<int>(payloadEnd - (start + byteCount));
    candidate.List.EntrySize = static_cast<int>(kZsiActorEntrySize);
    candidate.List.Interpretation = "oot3d_room_actor_list_command_0x01_zsi_file_base_relative";
    candidate.Entries.reserve(count);
    for (int index = 0; index < count; ++index) {
        auto entry = ReadZsiRoomActorEntry(data, start + static_cast<size_t>(index) * kZsiActorEntrySize, index);
        candidate.PlausibleActorCount += entry.Plausible ? 1 : 0;
        candidate.NonPlayerActorCount += entry.ActorId > 0 ? 1 : 0;
        candidate.Entries.push_back(entry);
    }
    if (candidate.PlausibleActorCount == 0) {
        return std::nullopt;
    }
    return candidate;
}

Oot3dNativeDemoPicaByteGroup PicaByteGroupFromBytes(const std::vector<uint8_t>& data, size_t offset) {
    Oot3dNativeDemoPicaByteGroup group;
    group.Raw0 = static_cast<int>(data[offset + 0]);
    group.Raw1 = static_cast<int>(data[offset + 1]);
    group.Raw2 = static_cast<int>(data[offset + 2]);
    group.Raw3 = static_cast<int>(data[offset + 3]);
    group.Signed0 = static_cast<int>(static_cast<int8_t>(data[offset + 0]));
    group.Signed1 = static_cast<int>(static_cast<int8_t>(data[offset + 1]));
    group.Signed2 = static_cast<int>(static_cast<int8_t>(data[offset + 2]));
    group.Signed3 = static_cast<int>(static_cast<int8_t>(data[offset + 3]));
    group.Normalized0 = static_cast<double>(group.Raw0) / 255.0;
    group.Normalized1 = static_cast<double>(group.Raw1) / 255.0;
    group.Normalized2 = static_cast<double>(group.Raw2) / 255.0;
    group.Normalized3 = static_cast<double>(group.Raw3) / 255.0;
    return group;
}

int SignedByteValue(uint8_t value) {
    return static_cast<int>(static_cast<int8_t>(value));
}

ColorRgba8 BgrColorFromBytes(const std::vector<uint8_t>& data, size_t offset) {
    return {
        data[offset + 2],
        data[offset + 1],
        data[offset + 0],
        255,
    };
}

ColorRgba8 RgbColorFromBytes(const std::vector<uint8_t>& data, size_t offset) {
    return {
        data[offset + 0],
        data[offset + 1],
        data[offset + 2],
        255,
    };
}

ColorRgba8 ActorVsAmbientColorCandidateFromRecords(const Oot3dNativeDemoPicaLightSettingsRecord& previous,
                                                   const Oot3dNativeDemoPicaLightSettingsRecord& current) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    if (previous.RawBytes.size() <= layout.ActorPacketAmbientPreviousTailByte1Offset ||
        current.RawBytes.size() <= layout.ActorPacketAmbientCurrentByteOffset) {
        return { 0, 0, 0, 255 };
    }
    return {
        previous.RawBytes[layout.ActorPacketAmbientPreviousTailByte0Offset],
        previous.RawBytes[layout.ActorPacketAmbientPreviousTailByte1Offset],
        current.RawBytes[layout.ActorPacketAmbientCurrentByteOffset],
        255,
    };
}

Vec3f SignedVec3FromBytes(const std::vector<uint8_t>& data, size_t offset) {
    return {
        static_cast<float>(SignedByteValue(data[offset + 0])),
        static_cast<float>(SignedByteValue(data[offset + 1])),
        static_cast<float>(SignedByteValue(data[offset + 2])),
    };
}

struct ZsiLightSettingsListCandidate {
    Oot3dNativeDemoRoomPayloadList List;
    int EntrySize = 0;
    std::string Layout;
    int PlausibleFloatPairCount = 0;
};

bool ZsiOot3dLightSettingsFloatPairLooksPlausible(float left, float right) {
    return std::isfinite(left) && std::isfinite(right) && left >= 0.0f && right >= 0.0f &&
           std::abs(left) <= 200000.0f && std::abs(right) <= 200000.0f;
}

std::optional<ZsiLightSettingsListCandidate> SelectZsiLightSettingsListCandidate(
    const std::vector<uint8_t>& data, const std::vector<ZsiSceneCommand>& commands, const ZsiSceneCommand& command) {
    const int count = static_cast<int>(ZsiCommandParameter(command));
    if (count <= 0 || !IsFileOffset(command.Argument, data.size())) {
        return std::nullopt;
    }

    const auto& layout = NativeZsiLightSettingsRecordLayout();
    const size_t payloadEnd = ZsiPayloadEndHint(data, commands, command);
    std::optional<ZsiLightSettingsListCandidate> bestCandidate;
    std::tuple<int, int, int, int, int> bestScore{ -1, -1, -1, -1, -1 };
    for (const uint32_t startDelta : layout.CandidateStartDeltas) {
        for (const uint32_t entrySize : { layout.NativeRecordSizeBytes, layout.LegacyRecordSizeBytes }) {
            const size_t start = static_cast<size_t>(command.Argument) + startDelta;
            const size_t byteCount = static_cast<size_t>(count) * static_cast<size_t>(entrySize);
            if (!CanRead(data, start, byteCount) || start + byteCount > payloadEnd) {
                continue;
            }

            ZsiLightSettingsListCandidate candidate;
            candidate.EntrySize = static_cast<int>(entrySize);
            candidate.Layout = entrySize == layout.NativeRecordSizeBytes ? layout.NativeRecordLayoutName
                                                                         : layout.LegacyRecordLayoutName;
            candidate.List.Valid = true;
            candidate.List.CommandOffset = static_cast<int>(command.Offset);
            candidate.List.CommandArgument = static_cast<int>(command.Argument);
            candidate.List.Count = count;
            candidate.List.StartOffset = static_cast<int>(start);
            candidate.List.StartDelta = static_cast<int>(startDelta);
            candidate.List.EndOffset = static_cast<int>(start + byteCount);
            candidate.List.PayloadEndHint = static_cast<int>(payloadEnd);
            candidate.List.PayloadEndGap = static_cast<int>(payloadEnd - (start + byteCount));
            candidate.List.EntrySize = static_cast<int>(entrySize);
            candidate.List.Interpretation = candidate.Layout;

            if (entrySize == layout.NativeRecordSizeBytes) {
                for (int index = 0; index < count; ++index) {
                    const size_t recordOffset = start + static_cast<size_t>(index) * entrySize;
                    const float float0 = ReadLeF32(data, recordOffset + layout.FloatParam0Offset);
                    const float float1 = ReadLeF32(data, recordOffset + layout.FloatParam1Offset);
                    candidate.PlausibleFloatPairCount +=
                        ZsiOot3dLightSettingsFloatPairLooksPlausible(float0, float1) ? 1 : 0;
                }
            }

            const std::tuple<int, int, int, int, int> score{
                candidate.PlausibleFloatPairCount,
                entrySize == layout.NativeRecordSizeBytes ? 1 : 0,
                candidate.List.PayloadEndGap == 0 ? 1 : 0,
                -static_cast<int>(startDelta),
                -static_cast<int>(entrySize),
            };
            if (score > bestScore) {
                bestScore = score;
                bestCandidate = std::move(candidate);
            }
        }
    }

    if (!bestCandidate.has_value()) {
        return std::nullopt;
    }
    if (bestCandidate->EntrySize == static_cast<int>(layout.NativeRecordSizeBytes) &&
        bestCandidate->PlausibleFloatPairCount != count) {
        return std::nullopt;
    }
    return bestCandidate;
}

Oot3dNativeDemoPicaLightSettingsRecord ReadZsiPicaLightSettingsRecord(
    const std::vector<uint8_t>& data, const ZsiSceneCommand& command, const ZsiLightSettingsListCandidate& list,
    int index) {
    const size_t offset = static_cast<size_t>(list.List.StartOffset) +
                          static_cast<size_t>(index) * static_cast<size_t>(list.EntrySize);
    Oot3dNativeDemoPicaLightSettingsRecord record;
    record.SetupIndex = command.SetupIndex;
    record.CommandOffset = static_cast<int>(command.Offset);
    record.CommandArgument = static_cast<int>(command.Argument);
    record.Index = index;
    record.Offset = static_cast<int>(offset);
    record.EntrySize = list.EntrySize;
    record.Layout = list.Layout;
    record.RawBytes.reserve(static_cast<size_t>(list.EntrySize));
    for (int byteIndex = 0; byteIndex < list.EntrySize; ++byteIndex) {
        record.RawBytes.push_back(data[offset + static_cast<size_t>(byteIndex)]);
    }

    const int halfwordCount = std::min(list.EntrySize / 2, 8);
    record.RawHalfwords.reserve(static_cast<size_t>(halfwordCount));
    for (int halfwordIndex = 0; halfwordIndex < halfwordCount; ++halfwordIndex) {
        record.RawHalfwords.push_back(static_cast<int>(ReadLeU16(data, offset + static_cast<size_t>(halfwordIndex) * 2)));
    }

    const int byteGroupCount = std::min(list.EntrySize / 4, 4);
    record.ByteGroups.reserve(static_cast<size_t>(byteGroupCount));
    for (int groupIndex = 0; groupIndex < byteGroupCount; ++groupIndex) {
        record.ByteGroups.push_back(PicaByteGroupFromBytes(data, offset + static_cast<size_t>(groupIndex) * 4));
    }

    const auto& layout = NativeZsiLightSettingsRecordLayout();
    if (CanRead(data, offset, layout.NativeEnvPrefixSizeBytes)) {
        record.NativeEnvLightSettingsAvailable = true;
        record.NativeEnvLightSettingsPrefixSize = static_cast<int>(layout.NativeEnvPrefixSizeBytes);
        record.AmbientColor = BgrColorFromBytes(data, offset + layout.AmbientColorOffset);
        record.Light0Direction = SignedVec3FromBytes(data, offset + layout.Light0DirectionOffset);
        record.Light0Color = BgrColorFromBytes(data, offset + layout.Light0ColorOffset);
        record.Light1Direction = SignedVec3FromBytes(data, offset + layout.Light1DirectionOffset);
        record.Light1Color = BgrColorFromBytes(data, offset + layout.Light1ColorOffset);
    }

    if (list.EntrySize == static_cast<int>(layout.NativeRecordSizeBytes)) {
        const size_t runtimeOffset = offset + static_cast<size_t>(layout.RuntimeRecordStartDelta);
        if (layout.RuntimeConsumerResolved &&
            CanRead(data, runtimeOffset + layout.RuntimeAmbientColorOffset, layout.RuntimeColorComponentCount) &&
            CanRead(data, runtimeOffset + layout.RuntimeLight0DirectionOffset, layout.RuntimeDirectionComponentCount) &&
            CanRead(data, runtimeOffset + layout.RuntimeLight0ColorOffset, layout.RuntimeColorComponentCount) &&
            CanRead(data, runtimeOffset + layout.RuntimeLight1DirectionOffset, layout.RuntimeDirectionComponentCount) &&
            CanRead(data, runtimeOffset + layout.RuntimeLight1ColorOffset, layout.RuntimeColorComponentCount) &&
            CanRead(data, runtimeOffset + layout.RuntimeFogColorOffset, layout.RuntimeColorComponentCount)) {
            record.NativeRuntimeEnvironmentLightSettingsAvailable = true;
            record.NativeRuntimeEnvironmentRecordOffset = static_cast<int>(runtimeOffset);
            record.NativeRuntimeEnvironmentRecordStartDelta =
                static_cast<int>(layout.RuntimeRecordStartDelta);
            record.NativeRuntimeAmbientColor = RgbColorFromBytes(data, runtimeOffset + layout.RuntimeAmbientColorOffset);
            record.NativeRuntimeLight0Direction = SignedVec3FromBytes(data, runtimeOffset + layout.RuntimeLight0DirectionOffset);
            record.NativeRuntimeLight0Color = RgbColorFromBytes(data, runtimeOffset + layout.RuntimeLight0ColorOffset);
            record.NativeRuntimeLight1Direction = SignedVec3FromBytes(data, runtimeOffset + layout.RuntimeLight1DirectionOffset);
            record.NativeRuntimeLight1Color = RgbColorFromBytes(data, runtimeOffset + layout.RuntimeLight1ColorOffset);
            record.NativeRuntimeFogColor = RgbColorFromBytes(data, runtimeOffset + layout.RuntimeFogColorOffset);
            if (CanRead(data, runtimeOffset, layout.NativeRecordSizeBytes)) {
                record.NativeRuntimeEnvironmentRawBytes.reserve(layout.NativeRecordSizeBytes);
                for (uint32_t byteIndex = 0; byteIndex < layout.NativeRecordSizeBytes; ++byteIndex) {
                    record.NativeRuntimeEnvironmentRawBytes.push_back(
                        data[runtimeOffset + static_cast<size_t>(byteIndex)]);
                }
            }
        }
        if (CanRead(data, runtimeOffset + layout.RuntimeScalar0Offset, 4)) {
            record.NativeRuntimeScalar0Raw = ReadLeU32(data, runtimeOffset + layout.RuntimeScalar0Offset);
        }
        if (CanRead(data, runtimeOffset + layout.RuntimeScalar1Offset, 4)) {
            record.NativeRuntimeScalar1Raw = ReadLeU32(data, runtimeOffset + layout.RuntimeScalar1Offset);
        }
        if (CanRead(data, runtimeOffset + layout.RuntimePackedHalfwordOffset, 2)) {
            record.NativeRuntimePackedHalfwordRaw = ReadLeU16(data, runtimeOffset + layout.RuntimePackedHalfwordOffset);
        }
        if (CanRead(data, offset, layout.ActorPacketDiffuse1ColorOffset + 3)) {
            record.NativeActorVsLightPacketColorCandidateAvailable = true;
            record.NativeActorVsDiffuse0Color =
                RgbColorFromBytes(data, offset + layout.ActorPacketDiffuse0ColorOffset);
            record.NativeActorVsDiffuse1Color =
                RgbColorFromBytes(data, offset + layout.ActorPacketDiffuse1ColorOffset);
        }
        if (CanRead(data, offset, layout.Native3dsTailByteOffset + 1)) {
            record.Native3dsTailByte0 = static_cast<int>(data[offset + layout.Native3dsTailByteOffset]);
        }
        record.FloatParam0 = static_cast<double>(ReadLeF32(data, offset + layout.FloatParam0Offset));
        record.FloatParam1 = static_cast<double>(ReadLeF32(data, offset + layout.FloatParam1Offset));
        record.FloatParamsFinite =
            ZsiOot3dLightSettingsFloatPairLooksPlausible(static_cast<float>(record.FloatParam0),
                                                         static_cast<float>(record.FloatParam1));
        record.TailRaw = ReadLeU32(data, offset + layout.TailWordOffset);
        record.TailBytes.reserve(layout.TailWordSizeBytes);
        for (uint32_t byteIndex = 0; byteIndex < layout.TailWordSizeBytes; ++byteIndex) {
            record.TailBytes.push_back(data[offset + layout.TailWordOffset + byteIndex]);
        }
    }
    return record;
}

void AppendZsiLightSettingsFromCommands(Oot3dNativeDemoPicaLightingState& lighting,
                                        const std::vector<uint8_t>& data,
                                        const std::vector<ZsiSceneCommand>& commands,
                                        bool sceneCommands) {
    for (const auto& command : commands) {
        const uint8_t commandId = ZsiCommandId(command);
        if (commandId == kZsiLightListCommandId) {
            auto record = ZsiCommandRecordFromCommand(command, data.size());
            if (sceneCommands) {
                lighting.SceneLightListCommands.push_back(record);
                ++lighting.SceneLightListCommandCount;
            } else {
                lighting.RoomLightListCommands.push_back(record);
                ++lighting.RoomLightListCommandCount;
            }
            continue;
        }
        if (commandId != kZsiLightSettingsListCommandId) {
            continue;
        }

        auto candidate = SelectZsiLightSettingsListCandidate(data, commands, command);
        if (!candidate.has_value()) {
            continue;
        }
        if (sceneCommands) {
            lighting.SceneLightSettingsLists.push_back(candidate->List);
            ++lighting.SceneLightSettingsCommandCount;
        } else {
            lighting.RoomLightSettingsLists.push_back(candidate->List);
            ++lighting.RoomLightSettingsCommandCount;
        }
        if (lighting.SelectedLightSettingsLayout.empty()) {
            lighting.SelectedLightSettingsLayout = candidate->Layout;
        }
        std::optional<Oot3dNativeDemoPicaLightSettingsRecord> previousRecord;
        for (int index = 0; index < candidate->List.Count; ++index) {
            auto record = ReadZsiPicaLightSettingsRecord(data, command, *candidate, index);
            if (previousRecord.has_value() && record.EntrySize == 0x1C &&
                previousRecord->EntrySize == 0x1C &&
                record.NativeActorVsLightPacketColorCandidateAvailable &&
                previousRecord->NativeActorVsLightPacketColorCandidateAvailable) {
                record.NativeActorVsAmbientColorCandidateAvailable = true;
                record.NativeActorVsAmbientColor =
                    ActorVsAmbientColorCandidateFromRecords(*previousRecord, record);
            }
            if (record.SetupIndex == lighting.ActiveSetupIndex) {
                ++lighting.ActiveSetupLightSettingsRecordCount;
            }
            previousRecord = record;
            lighting.LightSettings.push_back(std::move(record));
        }
    }
}

Oot3dNativeDemoPicaLightingState DecodeOot3dNativeDemoPicaLightingState(
    const std::filesystem::path& scenePath, const std::filesystem::path& roomPath, int activeSetupIndex) {
    Oot3dNativeDemoPicaLightingState lighting;
    lighting.SceneZsiPath = scenePath;
    lighting.RoomZsiPath = roomPath;
    lighting.ActiveSetupIndex = activeSetupIndex;
    lighting.SourceKind = "oot3d_zsi_light_settings_list_native_pica_state";

    if (!scenePath.empty() && std::filesystem::is_regular_file(scenePath)) {
        const auto sceneData = ReadBinaryFile(scenePath);
        if (sceneData.size() >= 4 && sceneData[0] == 'Z' && sceneData[1] == 'S' && sceneData[2] == 'I' &&
            sceneData[3] == 0x01) {
            const auto setups = ZsiSceneSetups(sceneData);
            lighting.SceneSetupCount = static_cast<int>(setups.size());
            for (const auto& setup : setups) {
                AppendZsiLightSettingsFromCommands(lighting, sceneData, setup.Commands, true);
            }
        }
    }

    if (!roomPath.empty() && std::filesystem::is_regular_file(roomPath)) {
        const auto roomData = ReadBinaryFile(roomPath);
        if (roomData.size() >= 4 && roomData[0] == 'Z' && roomData[1] == 'S' && roomData[2] == 'I' &&
            roomData[3] == 0x01) {
            if (auto roomCommandTable = FindZsiRoomCommandTable(roomData)) {
                AppendZsiLightSettingsFromCommands(lighting, roomData, roomCommandTable->second, false);
            }
        }
    }

    lighting.DecodedLightSettingsRecordCount = static_cast<int>(lighting.LightSettings.size());
    lighting.DecodedFromNativeZsi = lighting.DecodedLightSettingsRecordCount > 0 ||
                                    lighting.SceneLightListCommandCount > 0 || lighting.RoomLightListCommandCount > 0;
    lighting.Available = lighting.DecodedFromNativeZsi;
    return lighting;
}

struct Oot3dNativeDemoSemanticTables {
    std::filesystem::path SourcePath;
    bool Available = false;
    std::map<int, std::string> Actors;
    std::map<int, std::string> Objects;
};

struct Oot3dNativeDemoArchiveCandidates {
    std::vector<std::string> Names;
    std::string EmptyStatus;
};

struct Oot3dNativeDemoArchiveProbe {
    std::filesystem::path Path;
    bool Available = false;
    std::string Status;
    int FileCount = 0;
    int CmbCount = 0;
    int CsabCount = 0;
};

std::optional<int> ParseIntLiteral(std::string_view value) {
    try {
        size_t parsed = 0;
        const auto text = std::string(value);
        const auto number = std::stoll(text, &parsed, 0);
        if (parsed != text.size() || number < std::numeric_limits<int>::min() ||
            number > std::numeric_limits<int>::max()) {
            return std::nullopt;
        }
        return static_cast<int>(number);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::filesystem::path ResolveOot3dSemanticSourcePath(const std::filesystem::path& manifestPath,
                                                     const nlohmann::json& manifest) {
    const auto explicitSource = JsonNestedString(manifest, { "sources", "native_semantics", "path" });
    if (!explicitSource.empty()) {
        return ResolveRelativePath(manifestPath, explicitSource);
    }

    const auto explicitAsset = JsonNestedString(manifest, { "assets", "actor_object_semantics" });
    if (!explicitAsset.empty()) {
        return ResolveRelativePath(manifestPath, explicitAsset);
    }

    const auto repoRoot = JsonStringAt(manifest, "repo_root");
    if (!repoRoot.empty()) {
        return std::filesystem::path(repoRoot) / "tools" / "oot3d" / "decomp_support" / "include" / "oot3d" /
               "actor_object_semantics.h";
    }
    return {};
}

Oot3dNativeDemoSemanticTables LoadOot3dSemanticTables(const std::filesystem::path& sourcePath) {
    Oot3dNativeDemoSemanticTables tables;
    tables.SourcePath = sourcePath;
    if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath)) {
        return tables;
    }

    std::ifstream file(sourcePath);
    if (!file) {
        return tables;
    }

    enum class ActiveEnum {
        None,
        Actor,
        Object,
    };
    ActiveEnum active = ActiveEnum::None;
    const std::regex enumEntryPattern(R"(\s*([A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+))");
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("typedef enum Oot3dActorId") != std::string::npos) {
            active = ActiveEnum::Actor;
            continue;
        }
        if (line.find("typedef enum Oot3dObjectId") != std::string::npos) {
            active = ActiveEnum::Object;
            continue;
        }
        if (active == ActiveEnum::None) {
            continue;
        }
        if (line.find('}') != std::string::npos) {
            active = ActiveEnum::None;
            continue;
        }

        std::smatch match;
        if (!std::regex_search(line, match, enumEntryPattern)) {
            continue;
        }
        const auto id = ParseIntLiteral(match[2].str());
        if (!id.has_value()) {
            continue;
        }
        if (active == ActiveEnum::Actor) {
            tables.Actors[*id] = match[1].str();
        } else {
            tables.Objects[*id] = match[1].str();
        }
    }

    tables.Available = !tables.Actors.empty() || !tables.Objects.empty();
    return tables;
}

std::filesystem::path Oot3dActorArchiveRootFromZsiPath(const std::filesystem::path& zsiPath) {
    if (zsiPath.empty()) {
        return {};
    }
    const auto sceneDir = zsiPath.parent_path();
    if (sceneDir.empty()) {
        return {};
    }
    const auto romfsRoot = sceneDir.parent_path();
    if (romfsRoot.empty()) {
        return sceneDir / "actor";
    }
    return romfsRoot / "actor";
}

std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool EndsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

std::string StripPrefix(std::string_view value, std::string_view prefix) {
    if (!StartsWith(value, prefix)) {
        return std::string(value);
    }
    return std::string(value.substr(prefix.size()));
}

std::optional<std::string> StripFirstSemanticToken(std::string_view value) {
    const auto separator = value.find('_');
    if (separator == std::string_view::npos || separator + 1 >= value.size()) {
        return std::nullopt;
    }
    return std::string(value.substr(separator + 1));
}

void AppendUniqueCandidate(std::vector<std::string>& candidates, std::string candidate) {
    if (candidate.empty()) {
        return;
    }
    if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

std::string SemanticNameFor(const std::map<int, std::string>& names, int id, std::string_view fallbackPrefix) {
    if (const auto it = names.find(id); it != names.end()) {
        return it->second;
    }
    std::ostringstream fallback;
    fallback << fallbackPrefix << "_UNKNOWN_0x" << std::uppercase << std::hex << id;
    return fallback.str();
}

Oot3dNativeDemoArchiveCandidates ObjectArchiveCandidatesFromSemanticName(int objectId,
                                                                         std::string_view objectName) {
    Oot3dNativeDemoArchiveCandidates result;
    if (objectId <= 0 || objectName == "OBJECT_INVALID") {
        result.EmptyStatus = "invalid_object_id";
        return result;
    }
    if (objectName.empty() || !StartsWith(objectName, "OBJECT_")) {
        result.EmptyStatus = "semantic_name_missing";
        return result;
    }

    const auto token = ToLowerAscii(StripPrefix(objectName, "OBJECT_"));
    if (token.empty() || StartsWith(token, "unset_") || token == "invalid") {
        result.EmptyStatus = "invalid_object_id";
        return result;
    }

    AppendUniqueCandidate(result.Names, "zelda_" + token + ".zar");
    if (auto strippedToken = StripFirstSemanticToken(token)) {
        AppendUniqueCandidate(result.Names, "zelda_" + *strippedToken + ".zar");
    }
    result.EmptyStatus = "archive_not_found_by_semantic_name";
    return result;
}

Oot3dNativeDemoArchiveCandidates ActorArchiveCandidatesFromSemanticName(int actorId, std::string_view actorName) {
    Oot3dNativeDemoArchiveCandidates result;
    if (actorId < 0) {
        result.EmptyStatus = "invalid_actor_id";
        return result;
    }
    if (actorName.empty() || !StartsWith(actorName, "ACTOR_")) {
        result.EmptyStatus = "semantic_name_missing";
        return result;
    }

    const auto token = ToLowerAscii(StripPrefix(actorName, "ACTOR_"));
    if (token.empty() || StartsWith(token, "unset_")) {
        result.EmptyStatus = "invalid_actor_id";
        return result;
    }

    AppendUniqueCandidate(result.Names, "zelda_" + token + ".zar");
    if (auto strippedToken = StripFirstSemanticToken(token)) {
        AppendUniqueCandidate(result.Names, "zelda_" + *strippedToken + ".zar");
    }
    result.EmptyStatus = "archive_not_found_by_semantic_name";
    return result;
}

std::optional<std::filesystem::path> FindArchiveCandidatePath(const std::filesystem::path& archiveRoot,
                                                              std::string_view candidateName) {
    const auto exactPath = archiveRoot / std::filesystem::path(std::string(candidateName));
    if (std::filesystem::is_regular_file(exactPath)) {
        return exactPath;
    }

    const auto targetName = ToLowerAscii(std::string(candidateName));
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(archiveRoot, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (ToLowerAscii(entry.path().filename().string()) == targetName) {
            return entry.path();
        }
    }
    return std::nullopt;
}

bool ZarFileEntryHasType(const ZarFileEntry& entry, std::string_view typeName) {
    const auto entryType = ToLowerAscii(entry.TypeName);
    if (entryType == typeName) {
        return true;
    }
    const auto name = ToLowerAscii(entry.Name);
    return EndsWith(name, "." + std::string(typeName));
}

Oot3dNativeDemoArchiveProbe ResolveArchiveProbe(const std::filesystem::path& archiveRoot,
                                                const Oot3dNativeDemoArchiveCandidates& candidates) {
    Oot3dNativeDemoArchiveProbe probe;
    if (candidates.Names.empty()) {
        probe.Status = candidates.EmptyStatus.empty() ? "semantic_name_missing" : candidates.EmptyStatus;
        return probe;
    }
    if (archiveRoot.empty() || !std::filesystem::is_directory(archiveRoot)) {
        probe.Status = "actor_archive_root_missing";
        return probe;
    }

    for (const auto& candidateName : candidates.Names) {
        const auto path = FindArchiveCandidatePath(archiveRoot, candidateName);
        if (!path.has_value()) {
            continue;
        }

        probe.Path = *path;
        try {
            const auto archive = ParseZarArchiveFile(*path);
            probe.Available = true;
            probe.Status = "resolved";
            probe.FileCount = static_cast<int>(archive.Files.size());
            for (const auto& file : archive.Files) {
                probe.CmbCount += ZarFileEntryHasType(file, "cmb") ? 1 : 0;
                probe.CsabCount += ZarFileEntryHasType(file, "csab") ? 1 : 0;
            }
        } catch (const std::exception&) {
            probe.Available = false;
            probe.Status = "archive_parse_failed";
            probe.FileCount = 0;
            probe.CmbCount = 0;
            probe.CsabCount = 0;
        }
        return probe;
    }

    probe.Status = candidates.EmptyStatus.empty() ? "archive_not_found_by_semantic_name" : candidates.EmptyStatus;
    return probe;
}

void ApplyObjectArchiveProbe(Oot3dNativeDemoRoomObjectEntry& entry, const Oot3dNativeDemoArchiveProbe& probe) {
    entry.ArchivePath = probe.Path;
    entry.ArchiveAvailable = probe.Available;
    entry.ArchiveResolutionStatus = probe.Status;
    entry.ArchiveFileCount = probe.FileCount;
    entry.ArchiveCmbCount = probe.CmbCount;
    entry.ArchiveCsabCount = probe.CsabCount;
}

void ApplyActorArchiveProbe(Oot3dNativeDemoRoomActorEntry& entry, const Oot3dNativeDemoArchiveProbe& probe) {
    entry.ArchivePath = probe.Path;
    entry.ArchiveAvailable = probe.Available;
    entry.ArchiveResolutionStatus = probe.Status;
    entry.ArchiveFileCount = probe.FileCount;
    entry.ArchiveCmbCount = probe.CmbCount;
    entry.ArchiveCsabCount = probe.CsabCount;
}

void AnnotateNativeDemoAssetGraphArchives(Oot3dNativeDemoAssetGraph& graph,
                                          const Oot3dNativeDemoSemanticTables& semanticTables,
                                          const std::filesystem::path& codeBinPath) {
    std::vector<uint8_t> codeBin;
    graph.NativeActorProfileCodeBinPath = codeBinPath;
    if (!codeBinPath.empty() && std::filesystem::is_regular_file(codeBinPath)) {
        codeBin = ReadBinaryFile(codeBinPath);
        graph.NativeActorProfileCodeBinAvailable = true;
    }

    for (auto& object : graph.RoomObjects) {
        object.ObjectName = SemanticNameFor(semanticTables.Objects, object.ObjectId, "OBJECT");
        const auto candidates = semanticTables.Available
                                    ? ObjectArchiveCandidatesFromSemanticName(object.ObjectId, object.ObjectName)
                                    : Oot3dNativeDemoArchiveCandidates{ {}, "semantic_source_missing" };
        const auto probe = ResolveArchiveProbe(graph.ActorArchiveRoot, candidates);
        ApplyObjectArchiveProbe(object, probe);
        graph.ResolvedObjectArchiveCount += object.ArchiveAvailable ? 1 : 0;
    }

    for (auto& actor : graph.RoomActors) {
        actor.ActorName = SemanticNameFor(semanticTables.Actors, actor.ActorId, "ACTOR");
        NativeActorProfile profile;
        if (!codeBin.empty() && actor.ActorId >= 0 && actor.ActorId <= std::numeric_limits<uint16_t>::max()) {
            try {
                profile = ParseNativeActorProfileFromCodeBinBytes(codeBin, static_cast<uint16_t>(actor.ActorId));
            } catch (const std::exception&) {
            }
        }

        Oot3dNativeDemoArchiveProbe probe;
        bool resolvedFromNativeProfileObject = false;
        if (profile.Valid) {
            actor.NativeProfileAvailable = true;
            actor.NativeProfileAddress = profile.ProfileAddress;
            actor.NativeProfileObjectId = static_cast<int>(profile.ObjectId);
            actor.NativeProfileObjectName =
                SemanticNameFor(semanticTables.Objects, actor.NativeProfileObjectId, "OBJECT");
            actor.NativeProfileObjectPresentInRoomBank =
                std::any_of(graph.RoomObjects.begin(), graph.RoomObjects.end(), [&](const auto& object) {
                    return object.ObjectId == actor.NativeProfileObjectId;
                });
            actor.NativeProfileInitFunctionAddress = profile.InitFunctionAddress;
            actor.NativeProfileUpdateFunctionAddress = profile.UpdateFunctionAddress;
            actor.NativeProfileDrawFunctionAddress = profile.DrawFunctionAddress;
            ++graph.NativeActorProfileDecodedCount;

            try {
                const auto behavior = ParseNativeActorVisualBehaviorFromCodeBinBytes(codeBin, profile);
                if (behavior.Valid) {
                    actor.NativeVisualBehaviorAvailable = true;
                    actor.NativeVisualNormalCmbTypeLocalIndex =
                        static_cast<int>(behavior.NormalCmbTypeLocalIndex);
                    actor.NativeVisualFieryCmbTypeLocalIndex =
                        static_cast<int>(behavior.FieryCmbTypeLocalIndex);
                    actor.NativeVisualScale =
                        actor.Params >= 1 && actor.Params < static_cast<int>(behavior.ParamScales.size())
                            ? static_cast<double>(behavior.ParamScales[static_cast<size_t>(actor.Params)])
                            : static_cast<double>(behavior.DefaultScale);
                    actor.NativeVisualRotationYStepS16PerTick = behavior.RotationYStepS16PerTick;
                    actor.NativeVisualBehaviorSource = behavior.SourceKind;
                }
            } catch (const std::exception&) {
            }

            if (semanticTables.Available && profile.ObjectId > 0 && profile.ObjectId <= kZsiNativeIdMax) {
                const auto objectCandidates =
                    ObjectArchiveCandidatesFromSemanticName(profile.ObjectId, actor.NativeProfileObjectName);
                probe = ResolveArchiveProbe(graph.ActorArchiveRoot, objectCandidates);
                resolvedFromNativeProfileObject = probe.Available;
            }
        }

        if (!probe.Available) {
            const auto actorCandidates = semanticTables.Available
                                             ? ActorArchiveCandidatesFromSemanticName(actor.ActorId, actor.ActorName)
                                             : Oot3dNativeDemoArchiveCandidates{ {}, "semantic_source_missing" };
            probe = ResolveArchiveProbe(graph.ActorArchiveRoot, actorCandidates);
        }
        ApplyActorArchiveProbe(actor, probe);
        if (resolvedFromNativeProfileObject) {
            actor.ArchiveResolutionStatus = "resolved_from_oot3d_code_bin_actor_profile_object_id";
        }
        graph.ResolvedActorArchiveCount += actor.ArchiveAvailable ? 1 : 0;
    }
}

struct NativeActorVisualArchiveLoadResult {
    std::optional<size_t> ModelIndex;
    std::string Status;
    int CmbEntryCount = 0;
    int ParseableCmbEntryCount = 0;
};

struct NativeActorVisualBehaviorRule {
    bool Resolved = false;
    int CmbTypeLocalIndex = -1;
    double Scale = 1.0;
    int RotationYStepS16PerTick = 0;
    std::string Source;
};

NativeActorVisualBehaviorRule ResolveNativeActorVisualBehaviorRule(
    const Oot3dNativeDemoRoomActorEntry& actor) {
    NativeActorVisualBehaviorRule rule;
    if (!actor.NativeVisualBehaviorAvailable || actor.NativeVisualNormalCmbTypeLocalIndex < 0) {
        return rule;
    }

    rule.Resolved = true;
    rule.CmbTypeLocalIndex = actor.NativeVisualNormalCmbTypeLocalIndex;
    rule.RotationYStepS16PerTick = actor.NativeVisualRotationYStepS16PerTick;
    rule.Scale = actor.NativeVisualScale;
    rule.Source = actor.NativeVisualBehaviorSource + ":normal_model_branch";
    return rule;
}

NativeActorVisualArchiveLoadResult LoadNativeActorVisualModelFromArchive(
    Oot3dNativeDemoScene& scene, const Oot3dNativeDemoRoomActorEntry& actor,
    int selectedCmbTypeLocalIndex,
    std::map<std::pair<std::filesystem::path, int>, NativeActorVisualArchiveLoadResult>& archiveModelCache) {
    NativeActorVisualArchiveLoadResult result;
    if (!actor.ArchiveAvailable || actor.ArchivePath.empty()) {
        result.Status = "native_actor_archive_unavailable";
        return result;
    }

    const auto cacheKey = std::make_pair(actor.ArchivePath, selectedCmbTypeLocalIndex);
    if (const auto cacheIt = archiveModelCache.find(cacheKey); cacheIt != archiveModelCache.end()) {
        return cacheIt->second;
    }

    result.Status = "native_actor_archive_parse_failed";
    try {
        const auto archive = ParseZarArchiveFile(actor.ArchivePath);
        std::vector<std::pair<uint32_t, Oot3dNativeDemoActorVisualModel>> parseableModels;
        for (const auto& file : archive.Files) {
            if (!ZarFileEntryHasType(file, "cmb")) {
                continue;
            }

            ++result.CmbEntryCount;
            try {
                auto bytes = ExtractZarFileBytes(actor.ArchivePath, file.Name);
                Oot3dNativeDemoActorVisualModel visualModel;
                visualModel.ActorId = actor.ActorId;
                visualModel.ActorName = actor.ActorName;
                visualModel.ArchivePath = actor.ArchivePath;
                visualModel.CmbName = file.Name;
                visualModel.Model = ParseCmbModelBytes(bytes, actor.ArchivePath.string() + "!" + file.Name);
                parseableModels.emplace_back(file.TypeLocalIndex, std::move(visualModel));
            } catch (const std::exception&) {
            }
        }

        result.ParseableCmbEntryCount = static_cast<int>(parseableModels.size());
        const auto selectedModel = selectedCmbTypeLocalIndex >= 0
                                       ? std::find_if(parseableModels.begin(), parseableModels.end(),
                                                      [selectedCmbTypeLocalIndex](const auto& model) {
                                                          return model.first ==
                                                                 static_cast<uint32_t>(selectedCmbTypeLocalIndex);
                                                      })
                                       : parseableModels.end();
        if (selectedModel != parseableModels.end()) {
            auto visualModel = std::move(selectedModel->second);
            visualModel.SelectionStatus = "native_actor_runtime_cmb_type_local_index_from_code_bin_behavior";
            const auto modelIndex = scene.ActorVisualModels.size();
            scene.ActorVisualModels.push_back(std::move(visualModel));
            result.ModelIndex = modelIndex;
            result.Status = "native_actor_runtime_cmb_type_local_index_from_code_bin_behavior";
        } else if (parseableModels.size() == 1 && selectedCmbTypeLocalIndex < 0) {
            auto visualModel = std::move(parseableModels.front().second);
            visualModel.SelectionStatus = "single_parseable_cmb_entry_from_native_zar";
            const auto modelIndex = scene.ActorVisualModels.size();
            scene.ActorVisualModels.push_back(std::move(visualModel));
            result.ModelIndex = modelIndex;
            result.Status = "single_parseable_cmb_entry_from_native_zar";
        } else if (parseableModels.empty()) {
            result.Status = result.CmbEntryCount == 0 ? "native_zar_has_no_cmb_entries"
                                                      : "native_zar_has_no_parseable_cmb_entries";
        } else if (selectedCmbTypeLocalIndex >= 0) {
            result.Status = "native_actor_runtime_cmb_type_local_index_not_found";
        } else {
            result.Status = "multiple_parseable_cmb_entries_require_native_actor_runtime_selection";
        }
    } catch (const std::exception&) {
    }

    archiveModelCache[cacheKey] = result;
    return result;
}

void AppendNativeActorVisualSkippedInstance(Oot3dNativeDemoScene& scene,
                                            const Oot3dNativeDemoRoomActorEntry& actor,
                                            const NativeActorVisualArchiveLoadResult& loadResult,
                                            std::string reason,
                                            int modelIndex = -1,
                                            std::string cmbName = {},
                                            int skinnedPrimitiveCount = 0) {
    Oot3dNativeDemoActorVisualSkippedInstance skipped;
    skipped.ActorEntryIndex = actor.Index;
    skipped.ActorId = actor.ActorId;
    skipped.ActorName = actor.ActorName;
    skipped.ModelIndex = modelIndex;
    skipped.ArchivePath = actor.ArchivePath;
    skipped.CmbName = std::move(cmbName);
    skipped.Position = actor.Position;
    skipped.Rotation = actor.Rotation;
    skipped.Params = actor.Params;
    skipped.CmbEntryCount = loadResult.CmbEntryCount;
    skipped.ParseableCmbEntryCount = loadResult.ParseableCmbEntryCount;
    skipped.SkinnedPrimitiveCount = skinnedPrimitiveCount;
    skipped.Reason = std::move(reason);
    skipped.SelectionStatus = loadResult.Status;
    scene.ActorVisualSkippedInstances.push_back(std::move(skipped));
}

void LoadNativeActorVisualInstances(Oot3dNativeDemoScene& scene) {
    std::map<std::pair<std::filesystem::path, int>, NativeActorVisualArchiveLoadResult> archiveModelCache;
    for (const auto& actor : scene.AssetGraph.RoomActors) {
        if (!actor.Plausible || !actor.ArchiveAvailable || actor.ArchivePath.empty()) {
            continue;
        }

        const auto behavior = ResolveNativeActorVisualBehaviorRule(actor);
        const auto loadResult = LoadNativeActorVisualModelFromArchive(
            scene, actor, behavior.Resolved ? behavior.CmbTypeLocalIndex : -1, archiveModelCache);
        if (!loadResult.ModelIndex.has_value()) {
            AppendNativeActorVisualSkippedInstance(
                scene, actor, loadResult,
                loadResult.Status == "multiple_parseable_cmb_entries_require_native_actor_runtime_selection"
                    ? "native_actor_runtime_cmb_selection_not_decoded"
                    : "native_actor_visual_model_not_selected");
            continue;
        }

        const auto& visualModel = scene.ActorVisualModels[*loadResult.ModelIndex];
        const std::vector<uint32_t> allMeshes;
        const auto skinnedPrimitiveCount =
            NativeDemoSelectedPrimitiveCountBySkinningMode(visualModel.Model, allMeshes, 1) +
            NativeDemoSelectedPrimitiveCountBySkinningMode(visualModel.Model, allMeshes, 2);
        if (skinnedPrimitiveCount > 0) {
            AppendNativeActorVisualSkippedInstance(
                scene, actor, loadResult, "native_actor_skeleton_pose_contract_not_decoded",
                static_cast<int>(*loadResult.ModelIndex), visualModel.CmbName,
                static_cast<int>(skinnedPrimitiveCount));
            continue;
        }

        Oot3dNativeDemoActorVisualInstance instance;
        instance.ActorEntryIndex = actor.Index;
        instance.ActorId = actor.ActorId;
        instance.ActorName = actor.ActorName;
        instance.ModelIndex = *loadResult.ModelIndex;
        instance.ArchivePath = actor.ArchivePath;
        instance.CmbName = visualModel.CmbName;
        instance.Position = actor.Position;
        instance.Rotation = actor.Rotation;
        instance.Scale = behavior.Resolved ? behavior.Scale : 1.0;
        instance.RotationYStepS16PerTick =
            behavior.Resolved ? behavior.RotationYStepS16PerTick : 0;
        instance.SelectedCmbTypeLocalIndex = behavior.Resolved ? behavior.CmbTypeLocalIndex : -1;
        instance.NativeVisualBehaviorResolved = behavior.Resolved;
        instance.BehaviorSource = behavior.Source;
        instance.TransformStatus = behavior.Resolved
                                       ? "oot3d_actor_entry_transform_with_code_bin_actor_behavior"
                                       : "actor_entry_position_rotation_source_units_rigid_cmb";
        instance.Params = actor.Params;
        scene.ActorVisualInstances.push_back(std::move(instance));
    }
}

Oot3dNativeDemoAssetGraph DecodeOot3dNativeDemoAssetGraph(const std::filesystem::path& scenePath,
                                                          const std::filesystem::path& roomPath,
                                                          const std::filesystem::path& manifestPath,
                                                          const nlohmann::json& manifest,
                                                          int activeSetupIndex,
                                                          const std::filesystem::path& codeBinPath) {
    Oot3dNativeDemoAssetGraph graph;
    graph.SceneZsiPath = scenePath;
    graph.RoomZsiPath = roomPath;
    const auto semanticTables = LoadOot3dSemanticTables(ResolveOot3dSemanticSourcePath(manifestPath, manifest));
    graph.SemanticSourcePath = semanticTables.SourcePath;
    graph.SemanticSourceAvailable = semanticTables.Available;
    graph.ActorArchiveRoot = Oot3dActorArchiveRootFromZsiPath(!scenePath.empty() ? scenePath : roomPath);
    graph.ActorArchiveRootAvailable =
        !graph.ActorArchiveRoot.empty() && std::filesystem::is_directory(graph.ActorArchiveRoot);

    if (!scenePath.empty() && std::filesystem::is_regular_file(scenePath)) {
        const auto sceneData = ReadBinaryFile(scenePath);
        if (sceneData.size() >= 4 && sceneData[0] == 'Z' && sceneData[1] == 'S' && sceneData[2] == 'I' &&
            sceneData[3] == 0x01) {
            const auto setups = ZsiSceneSetups(sceneData);
            graph.SceneSetupCount = static_cast<int>(setups.size());
            graph.SceneCommands = ZsiCommandRecordsFromSetups(setups, sceneData.size());
            graph.SceneCommandCount = static_cast<int>(graph.SceneCommands.size());
            graph.RoomReferences = DecodeZsiRoomReferences(scenePath, sceneData, setups);
        }
    }

    if (!roomPath.empty() && std::filesystem::is_regular_file(roomPath)) {
        const auto roomData = ReadBinaryFile(roomPath);
        if (roomData.size() >= 4 && roomData[0] == 'Z' && roomData[1] == 'S' && roomData[2] == 'I' &&
            roomData[3] == 0x01) {
            if (auto roomCommandTable = SelectZsiRoomCommandTableForSetup(roomData, activeSetupIndex)) {
                graph.RequestedRoomSetupIndex = roomCommandTable->RequestedSetupIndex;
                graph.RoomBaseCommandTableOffset = static_cast<int>(roomCommandTable->BaseOffset);
                graph.RoomCommandTableOffset = static_cast<int>(roomCommandTable->Offset);
                graph.RoomAlternateHeaderListOffset = roomCommandTable->AlternateHeaderListOffset;
                graph.RoomAlternateHeaderEntryIndex = roomCommandTable->AlternateHeaderEntryIndex;
                graph.RoomAlternateHeaderRawOffset = roomCommandTable->AlternateHeaderRawOffset;
                graph.RoomCommandTableSelectionStatus = roomCommandTable->Status;
                graph.RoomCommands = ZsiCommandRecordsFromCommands(roomCommandTable->Commands, roomData.size());
                graph.RoomCommandCount = static_cast<int>(graph.RoomCommands.size());

                if (const auto* objectListCommand =
                        FirstZsiInlineCommand(roomCommandTable->Commands, kZsiRoomObjectListCommandId)) {
                    if (auto objectList =
                            SelectZsiRoomObjectListCandidate(roomData, roomCommandTable->Commands, *objectListCommand)) {
                        graph.RoomObjectList = objectList->List;
                        graph.RoomObjects = std::move(objectList->Entries);
                    }
                }

                if (const auto* actorListCommand =
                        FirstZsiInlineCommand(roomCommandTable->Commands, kZsiRoomActorListCommandId)) {
                    if (auto actorList =
                            SelectZsiRoomActorListCandidate(roomData, roomCommandTable->Commands, *actorListCommand)) {
                        graph.RoomActorList = actorList->List;
                        graph.RoomActors = std::move(actorList->Entries);
                    }
                }
            }
        }
    }

    if (graph.RoomReferences.empty() && !roomPath.empty()) {
        graph.ManifestRoomPathFallbackUsed = true;
        graph.RoomReferences.push_back({
            -1,
            -1,
            0,
            -1,
            "",
            roomPath,
            std::filesystem::is_regular_file(roomPath),
        });
    }

    AnnotateNativeDemoAssetGraphArchives(graph, semanticTables, codeBinPath);
    graph.Valid = graph.SceneCommandCount > 0 || graph.RoomCommandCount > 0 || !graph.RoomReferences.empty();
    return graph;
}

std::optional<Oot3dNativeDemoCollisionScene> TryParseZsiCollisionCandidate(const std::vector<uint8_t>& data,
                                                                           const std::filesystem::path& path,
                                                                           const ZsiSceneCommand& command,
                                                                           size_t offset) {
    if (!CanRead(data, offset, 0x2C)) {
        return std::nullopt;
    }

    Oot3dDemoBounds bounds;
    bounds.Min = { static_cast<double>(ReadLeS16(data, offset + 0x00)),
                   static_cast<double>(ReadLeS16(data, offset + 0x02)),
                   static_cast<double>(ReadLeS16(data, offset + 0x04)) };
    bounds.Max = { static_cast<double>(ReadLeS16(data, offset + 0x06)),
                   static_cast<double>(ReadLeS16(data, offset + 0x08)),
                   static_cast<double>(ReadLeS16(data, offset + 0x0A)) };
    bounds.Valid = true;
    if (!BoundsArePlausible(bounds)) {
        return std::nullopt;
    }

    const auto vertexCount = ReadLeU16(data, offset + 0x0C);
    const auto rawPolygonCount = ReadLeU16(data, offset + 0x0E);
    const auto surfaceTypeCount = ReadLeU16(data, offset + 0x10);
    const auto bgCamCount = ReadLeU16(data, offset + 0x12);
    const auto waterBoxCount = ReadLeU16(data, offset + 0x14);
    if (vertexCount == 0 || rawPolygonCount == 0 || surfaceTypeCount == 0 || bgCamCount > 0x100 ||
        waterBoxCount > 0x40) {
        return std::nullopt;
    }

    const auto vertexOffset = ReadLeU32(data, offset + 0x18);
    const auto polygonOffset = ReadLeU32(data, offset + 0x1C);
    const auto surfaceTypeOffset = ReadLeU32(data, offset + 0x20);
    const auto bgCamOffset = ReadLeU32(data, offset + 0x24);
    const auto waterBoxesOffset = ReadLeU32(data, offset + 0x28);
    for (const auto pointerOffset : { vertexOffset, polygonOffset, surfaceTypeOffset, bgCamOffset, waterBoxesOffset }) {
        if (!IsFileOffset(pointerOffset, data.size())) {
            return std::nullopt;
        }
    }
    if (static_cast<size_t>(vertexOffset) + static_cast<size_t>(vertexCount) * 6 > data.size() ||
        static_cast<size_t>(polygonOffset) + static_cast<size_t>(rawPolygonCount) * 0x14 != surfaceTypeOffset ||
        static_cast<size_t>(surfaceTypeOffset) + static_cast<size_t>(surfaceTypeCount) * 8 > data.size() ||
        static_cast<size_t>(bgCamOffset) + static_cast<size_t>(bgCamCount) * 8 > data.size() ||
        static_cast<size_t>(waterBoxesOffset) + static_cast<size_t>(waterBoxCount) * 0x10 > data.size()) {
        return std::nullopt;
    }

    const auto polygonLayout = FindZsiEffectivePolygonLayout(data, polygonOffset, surfaceTypeOffset, rawPolygonCount,
                                                            vertexCount, surfaceTypeCount);
    if (!polygonLayout.has_value()) {
        return std::nullopt;
    }
    const auto [effectivePolygonOffset, effectivePolygonCount] = *polygonLayout;

    const auto effectiveVertexOffset =
        FindZsiEffectiveVertexLayout(data, vertexOffset, effectivePolygonOffset, vertexCount, bounds);
    if (!effectiveVertexOffset.has_value()) {
        return std::nullopt;
    }

    const auto bgCameraLayout = FindZsiEffectiveBgCameraLayout(data, bgCamOffset, bgCamCount);
    const size_t effectiveBgCamOffset = bgCameraLayout.has_value() ? bgCameraLayout->EffectiveBgCamOffset
                                                                   : static_cast<size_t>(bgCamOffset);
    const auto surfaceTypeLayout = FindZsiEffectiveSurfaceTypeLayout(data, surfaceTypeOffset, surfaceTypeCount,
                                                                     effectiveBgCamOffset, bgCamCount);

    Oot3dNativeDemoCollisionScene collision;
    collision.SourceZsiPath = path;
    collision.ResourcePath = path;
    collision.Bounds = bounds;
    collision.Valid = true;
    collision.DecodedFromNativeZsi = true;
    collision.XmlFallbackUsed = false;
    collision.SetupIndex = command.SetupIndex;
    collision.CommandOffset = static_cast<int>(command.Offset);
    collision.CommandArgument = static_cast<int>(command.Argument);
    collision.HeaderOffset = static_cast<int>(offset);
    collision.EffectiveVertexOffset = static_cast<int>(*effectiveVertexOffset);
    collision.EffectivePolygonOffset = static_cast<int>(effectivePolygonOffset);
    collision.EffectiveSurfaceTypeOffset = static_cast<int>(surfaceTypeLayout.first);
    collision.EffectiveBgCamOffset = static_cast<int>(effectiveBgCamOffset);
    collision.SurfaceTypeCount = surfaceTypeCount;
    collision.BgCamCount = bgCamCount;
    collision.WaterBoxCount = waterBoxCount;
    if (bgCameraLayout.has_value()) {
        collision.CameraPositionOffset = static_cast<int>(bgCameraLayout->CameraPositionOffset);
        collision.CameraPointerAdjustment = bgCameraLayout->CameraPointerAdjustment;
    }

    collision.Vertices.reserve(vertexCount);
    for (size_t index = 0; index < vertexCount; ++index) {
        collision.Vertices.push_back(ReadZsiCollisionVertex(data, *effectiveVertexOffset + index * 6));
    }

    collision.Polygons.reserve(effectivePolygonCount);
    for (size_t index = 0; index < effectivePolygonCount; ++index) {
        const auto polygon = ReadZsiCollisionPolygon(data, effectivePolygonOffset + index * 0x14);
        collision.Polygons.push_back(BuildCollisionPolygonFromRawVertices(
            static_cast<int>(polygon.Type),
            static_cast<int>(polygon.RawVertexA),
            static_cast<int>(polygon.RawVertexB),
            static_cast<int>(polygon.RawVertexC),
            static_cast<int>(polygon.NormalX),
            static_cast<int>(polygon.NormalY),
            static_cast<int>(polygon.NormalZ),
            RoundS16(polygon.Dist)));
    }

    collision.SurfaceTypes.reserve(surfaceTypeLayout.second.size());
    for (const auto& surfaceType : surfaceTypeLayout.second) {
        collision.SurfaceTypes.push_back({
            surfaceType.Data1,
            surfaceType.Data2,
        });
    }

    if (bgCameraLayout.has_value()) {
        collision.BgCameras.reserve(bgCameraLayout->Cameras.size());
        for (size_t index = 0; index < bgCameraLayout->Cameras.size(); ++index) {
            const auto& camera = bgCameraLayout->Cameras[index];
            const int cameraPositionIndex =
                index < bgCameraLayout->CameraPositionVectorIndices.size()
                    ? bgCameraLayout->CameraPositionVectorIndices[index]
                    : 0;
            collision.BgCameras.push_back({
                camera.Setting,
                camera.Count,
                camera.DataOffset,
                cameraPositionIndex,
            });
        }

        for (size_t index = 0; index + 2 < bgCameraLayout->CameraPositionVectors.size(); index += 3) {
            collision.BgCameraPositions.push_back({
                bgCameraLayout->CameraPositionVectors[index],
                bgCameraLayout->CameraPositionVectors[index + 1],
                bgCameraLayout->CameraPositionVectors[index + 2],
            });
        }
    }
    return collision;
}

Oot3dNativeDemoCollisionScene ParseOot3dNativeDemoCollisionZsi(const std::filesystem::path& path) {
    const auto data = ReadBinaryFile(path);
    if (data.size() < 4 || data[0] != 'Z' || data[1] != 'S' || data[2] != 'I' || data[3] != 0x01) {
        throw std::runtime_error("expected OOT3D ZSI magic in collision source: " + path.string());
    }

    std::vector<size_t> seenOffsets;
    for (const auto& command : ZsiCollisionCommands(data)) {
        for (const auto offset : { static_cast<size_t>(command.Argument), static_cast<size_t>(command.Argument) + 0x10 }) {
            if (std::find(seenOffsets.begin(), seenOffsets.end(), offset) != seenOffsets.end()) {
                continue;
            }
            seenOffsets.push_back(offset);
            if (auto collision = TryParseZsiCollisionCandidate(data, path, command, offset)) {
                return *collision;
            }
        }
    }
    throw std::runtime_error("no decoded OOT3D ZSI collision header candidate found: " + path.string());
}

Oot3dNativeDemoCollisionScene ParseOot3dNativeDemoCollisionXml(const std::filesystem::path& path,
                                                               const std::filesystem::path& sourceZsiPath) {
    const auto text = ReadTextFile(path);
    Oot3dNativeDemoCollisionScene collision;
    collision.SourceZsiPath = sourceZsiPath;
    collision.ResourcePath = path;

    std::smatch headerMatch;
    if (!std::regex_search(text, headerMatch, std::regex(R"(<CollisionHeader[^>]*>)"))) {
        throw std::runtime_error("collision XML missing CollisionHeader: " + path.string());
    }
    const std::string header = headerMatch[0].str();
    collision.Bounds.Min = { XmlAttrDouble(header, "MinBoundsX"), XmlAttrDouble(header, "MinBoundsY"),
                             XmlAttrDouble(header, "MinBoundsZ") };
    collision.Bounds.Max = { XmlAttrDouble(header, "MaxBoundsX"), XmlAttrDouble(header, "MaxBoundsY"),
                             XmlAttrDouble(header, "MaxBoundsZ") };
    collision.Bounds.Valid = true;

    const std::regex vertexPattern(R"(<Vertex\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), vertexPattern), end; it != end; ++it) {
        const std::string node = it->str();
        collision.Vertices.push_back({ XmlAttrDouble(node, "X"), XmlAttrDouble(node, "Y"),
                                       XmlAttrDouble(node, "Z") });
    }

    const std::regex polyPattern(R"(<Polygon\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), polyPattern), end; it != end; ++it) {
        const std::string node = it->str();
        collision.Polygons.push_back(BuildCollisionPolygonFromRawVertices(
            XmlAttrInt(node, "Type"),
            XmlAttrInt(node, "VertexA"),
            XmlAttrInt(node, "VertexB"),
            XmlAttrInt(node, "VertexC"),
            XmlAttrInt(node, "NormalX"),
            XmlAttrInt(node, "NormalY"),
            XmlAttrInt(node, "NormalZ"),
            XmlAttrInt(node, "Dist")));
    }

    const std::regex surfacePattern(R"(<PolygonType\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), surfacePattern), end; it != end; ++it) {
        const std::string node = it->str();
        collision.SurfaceTypes.push_back({
            static_cast<uint32_t>(std::stoul(XmlAttr(node, "Data1"))),
            static_cast<uint32_t>(std::stoul(XmlAttr(node, "Data2"))),
        });
    }

    const std::regex cameraPattern(R"(<CameraData\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), cameraPattern), end; it != end; ++it) {
        const std::string node = it->str();
        collision.BgCameras.push_back({
            static_cast<uint16_t>(XmlAttrInt(node, "SType")),
            static_cast<uint16_t>(XmlAttrInt(node, "NumData")),
            0,
            XmlAttrInt(node, "CameraPosDataSeg"),
        });
    }

    const std::regex cameraPositionPattern(R"(<CameraPositionData\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), cameraPositionPattern), end; it != end; ++it) {
        const std::string node = it->str();
        collision.BgCameraPositions.push_back({
            { static_cast<double>(XmlAttrInt(node, "PosX")),
              static_cast<double>(XmlAttrInt(node, "PosY")),
              static_cast<double>(XmlAttrInt(node, "PosZ")) },
            { static_cast<double>(XmlAttrInt(node, "RotX")),
              static_cast<double>(XmlAttrInt(node, "RotY")),
              static_cast<double>(XmlAttrInt(node, "RotZ")) },
            { static_cast<double>(XmlAttrInt(node, "FOV")),
              static_cast<double>(XmlAttrInt(node, "JfifID")),
              static_cast<double>(XmlAttrInt(node, "Unknown")) },
        });
    }

    if (collision.Vertices.empty() || collision.Polygons.empty()) {
        throw std::runtime_error("collision XML contains no vertices or polygons: " + path.string());
    }
    collision.Valid = true;
    collision.DecodedFromNativeZsi = false;
    collision.XmlFallbackUsed = true;
    collision.SurfaceTypeCount = static_cast<int>(collision.SurfaceTypes.size());
    collision.BgCamCount = static_cast<int>(collision.BgCameras.size());
    return collision;
}

bool PointInTriXZ(double x, double z, const Oot3dDemoVec3& a, const Oot3dDemoVec3& b,
                  const Oot3dDemoVec3& c) {
    const double v0x = c.X - a.X;
    const double v0z = c.Z - a.Z;
    const double v1x = b.X - a.X;
    const double v1z = b.Z - a.Z;
    const double v2x = x - a.X;
    const double v2z = z - a.Z;
    const double dot00 = v0x * v0x + v0z * v0z;
    const double dot01 = v0x * v1x + v0z * v1z;
    const double dot02 = v0x * v2x + v0z * v2z;
    const double dot11 = v1x * v1x + v1z * v1z;
    const double dot12 = v1x * v2x + v1z * v2z;
    const double denom = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denom) < 1e-8) {
        return false;
    }
    const double inv = 1.0 / denom;
    const double u = (dot11 * dot02 - dot01 * dot12) * inv;
    const double v = (dot00 * dot12 - dot01 * dot02) * inv;
    return u >= -1e-5 && v >= -1e-5 && (u + v) <= 1.00001;
}

std::optional<std::filesystem::path> ArchivePathFromBangSource(std::string_view source) {
    const auto bang = source.find('!');
    if (bang == std::string_view::npos || bang == 0) {
        return std::nullopt;
    }
    return std::filesystem::path(std::string(source.substr(0, bang)));
}

std::string NormalizeNativeResourceName(std::string_view value) {
    std::string normalized(value);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized;
}

std::string NativeResourceStem(std::string_view value) {
    std::string normalized = NormalizeNativeResourceName(value);
    const auto slash = normalized.find_last_of('/');
    if (slash != std::string::npos) {
        normalized = normalized.substr(slash + 1);
    }
    const auto dot = normalized.find_last_of('.');
    if (dot != std::string::npos) {
        normalized = normalized.substr(0, dot);
    }
    return normalized;
}

std::string NormalizeNativeFilesystemPath(const std::filesystem::path& path) {
    return NormalizeNativeResourceName(path.lexically_normal().generic_string());
}

int ResolveRoomMaterialAnimationRoomIndex(const Oot3dNativeDemoScene& scene) {
    const std::string roomPath = NormalizeNativeFilesystemPath(scene.RoomZsiPath);
    if (roomPath.empty()) {
        return -1;
    }

    for (const bool requireActiveSetup : { true, false }) {
        for (const auto& reference : scene.AssetGraph.RoomReferences) {
            if (!reference.Available || reference.Index < 0 ||
                NormalizeNativeFilesystemPath(reference.ResolvedPath) != roomPath) {
                continue;
            }
            if (requireActiveSetup && reference.SetupIndex != scene.ActiveSceneSetupIndex) {
                continue;
            }
            return reference.Index;
        }
    }
    return -1;
}

std::filesystem::path ResolveRoomMaterialAnimationArchivePath(const Oot3dNativeDemoScene& scene) {
    const auto& sceneZsiPath = scene.AssetGraph.SceneZsiPath;
    if (sceneZsiPath.empty()) {
        return {};
    }

    std::string stem = sceneZsiPath.stem().string();
    const std::string normalizedStem = NormalizeNativeResourceName(stem);
    constexpr std::string_view infoSuffix = "_info";
    if (normalizedStem.size() >= infoSuffix.size() &&
        normalizedStem.substr(normalizedStem.size() - infoSuffix.size()) == infoSuffix) {
        stem.resize(stem.size() - infoSuffix.size());
    }
    return sceneZsiPath.parent_path() / (stem + ".zar");
}

void LoadRoomMaterialAnimations(Oot3dNativeDemoScene& scene) {
    scene.RoomMaterialAnimationNames.clear();
    scene.RoomMaterialAnimations.clear();
    scene.RoomMaterialAnimationRoomIndex = ResolveRoomMaterialAnimationRoomIndex(scene);
    scene.RoomMaterialAnimationArchivePath = ResolveRoomMaterialAnimationArchivePath(scene);

    if (scene.RoomMaterialAnimationRoomIndex < 0) {
        scene.RoomMaterialAnimationStatus = "active_room_index_unresolved_from_zsi_room_references";
        return;
    }
    if (!std::filesystem::is_regular_file(scene.RoomMaterialAnimationArchivePath)) {
        scene.RoomMaterialAnimationStatus = "scene_zar_not_available";
        return;
    }

    const std::string roomPrefix =
        "room" + std::to_string(scene.RoomMaterialAnimationRoomIndex) + "/";
    const auto archive = ParseZarArchiveFile(scene.RoomMaterialAnimationArchivePath);
    for (const auto& file : archive.Files) {
        const std::string normalizedName = NormalizeNativeResourceName(file.Name);
        const bool isCmab = file.TypeName == "cmab" ||
                            (normalizedName.size() >= 5 &&
                             normalizedName.substr(normalizedName.size() - 5) == ".cmab");
        if (!isCmab || normalizedName.rfind(roomPrefix, 0) != 0) {
            continue;
        }

        const auto bytes = ExtractZarFileBytes(scene.RoomMaterialAnimationArchivePath, file.Name);
        scene.RoomMaterialAnimationNames.push_back(file.Name);
        scene.RoomMaterialAnimations.push_back(ParseCmabMaterialAnimationBytes(
            bytes, scene.RoomMaterialAnimationArchivePath.string() + "!" + file.Name));
    }

    scene.RoomMaterialAnimationStatus =
        scene.RoomMaterialAnimations.empty()
            ? "active_room_namespace_contains_no_cmab"
            : "loaded_from_native_scene_zar_active_room_namespace";
}

void AppendUniqueArchive(std::vector<std::filesystem::path>& archives, const std::filesystem::path& archivePath) {
    if (std::find(archives.begin(), archives.end(), archivePath) == archives.end()) {
        archives.push_back(archivePath);
    }
}

std::vector<std::filesystem::path> CharacterArchiveCandidates(const std::filesystem::path& manifestPath,
                                                              const nlohmann::json& manifest) {
    std::vector<std::filesystem::path> archives;
    const auto nativeBindPoseSource = JsonNestedString(manifest, { "target", "native_bind_pose", "source" });
    if (auto archive = ArchivePathFromBangSource(nativeBindPoseSource)) {
        AppendUniqueArchive(archives, ResolveRelativePath(manifestPath, *archive));
    }

    std::filesystem::path archiveRoot;
    if (!archives.empty()) {
        archiveRoot = archives.front().parent_path();
    }

    const auto appendArchive = [&](const nlohmann::json& value) {
        if (!value.is_string()) {
            return;
        }
        std::filesystem::path archivePath(value.get<std::string>());
        if (archivePath.is_relative() && !archiveRoot.empty()) {
            archivePath = archiveRoot / archivePath;
        } else {
            archivePath = ResolveRelativePath(manifestPath, archivePath);
        }
        AppendUniqueArchive(archives, archivePath);
    };

    if (manifest.contains("source_archives") && manifest.at("source_archives").is_object()) {
        const auto& sourceArchives = manifest.at("source_archives");
        if (sourceArchives.contains("model_archive")) {
            appendArchive(sourceArchives.at("model_archive"));
        }
        if (sourceArchives.contains("auxiliary_archives") && sourceArchives.at("auxiliary_archives").is_array()) {
            for (const auto& archive : sourceArchives.at("auxiliary_archives")) {
                appendArchive(archive);
            }
        }
    }
    return archives;
}

std::vector<uint32_t> MeshIndicesFromNativeBindPoseProfile(const nlohmann::json& manifest,
                                                           std::string_view profileName) {
    const auto* cursor = &manifest;
    for (const auto* key : { "target", "native_bind_pose", "display_profiles" }) {
        if (!cursor->is_object() || !cursor->contains(key)) {
            return {};
        }
        cursor = &cursor->at(key);
    }
    if (!cursor->is_array()) {
        return {};
    }
    for (const auto& profile : *cursor) {
        if (!profile.is_object() || JsonStringAt(profile, "name") != profileName) {
            continue;
        }
        if (!profile.contains("mesh_indices") || !profile.at("mesh_indices").is_array()) {
            return {};
        }
        std::vector<uint32_t> indices;
        for (const auto& value : profile.at("mesh_indices")) {
            if (value.is_number_unsigned() || value.is_number_integer()) {
                indices.push_back(value.get<uint32_t>());
            }
        }
        return indices;
    }
    return {};
}

std::vector<uint8_t> ExtractFirstAvailableZarFile(const std::vector<std::filesystem::path>& archives,
                                                  std::string_view fileName) {
    std::string lastError;
    for (const auto& archive : archives) {
        if (!std::filesystem::is_regular_file(archive)) {
            continue;
        }
        try {
            return ExtractZarFileBytes(archive, fileName);
        } catch (const std::exception& ex) {
            lastError = ex.what();
        }
    }
    throw std::runtime_error(lastError.empty() ? "no candidate ZAR archive contained " + std::string(fileName)
                                               : lastError);
}

std::optional<std::vector<uint8_t>> TryExtractFirstAvailableZarFile(const std::vector<std::filesystem::path>& archives,
                                                                    std::string_view fileName) {
    for (const auto& archive : archives) {
        if (!std::filesystem::is_regular_file(archive)) {
            continue;
        }
        try {
            return ExtractZarFileBytes(archive, fileName);
        } catch (const std::exception&) {
        }
    }
    return std::nullopt;
}

size_t AppendLinkCsabClip(Oot3dNativeDemoScene& scene, const std::vector<std::filesystem::path>& archives,
                          std::string id, std::string csabName) {
    if (csabName.empty()) {
        csabName = scene.LinkStandingCsabName;
    }

    Oot3dNativeDemoLinkCsabClip clip;
    clip.Id = std::move(id);
    clip.CsabName = std::move(csabName);
    clip.Bytes = ExtractFirstAvailableZarFile(archives, clip.CsabName);
    clip.Metadata = ParseCsabMetadataBytes(clip.Bytes);

    scene.LinkCsabClips.push_back(std::move(clip));
    return scene.LinkCsabClips.size() - 1;
}

std::optional<size_t> AppendOptionalLinkCsabClip(Oot3dNativeDemoScene& scene,
                                                 const std::vector<std::filesystem::path>& archives, std::string id,
                                                 std::string csabName) {
    auto bytes = TryExtractFirstAvailableZarFile(archives, csabName);
    if (!bytes) {
        return std::nullopt;
    }

    Oot3dNativeDemoLinkCsabClip clip;
    clip.Id = std::move(id);
    clip.CsabName = std::move(csabName);
    clip.Bytes = std::move(*bytes);
    clip.Metadata = ParseCsabMetadataBytes(clip.Bytes);

    scene.LinkCsabClips.push_back(std::move(clip));
    return scene.LinkCsabClips.size() - 1;
}

void LoadLinkMaterialAnimations(Oot3dNativeDemoScene& scene, const std::vector<std::filesystem::path>& archives,
                                const nlohmann::json& characterManifest) {
    scene.LinkMaterialAnimations.clear();
    if (!characterManifest.is_object() || !characterManifest.contains("material_animation_payloads") ||
        !characterManifest.at("material_animation_payloads").is_object()) {
        return;
    }

    const auto& payloads = characterManifest.at("material_animation_payloads");
    if (!payloads.contains("bindings") || !payloads.at("bindings").is_array()) {
        return;
    }

    const std::string targetModelName = NormalizeNativeResourceName(scene.LinkCmbName);
    std::set<std::string> loadedAnimationNames;
    for (const auto& binding : payloads.at("bindings")) {
        if (!binding.is_object()) {
            continue;
        }

        const std::string cmabName = JsonStringAt(binding, "cmab_name");
        if (cmabName.empty()) {
            continue;
        }

        const std::string targetCmbName = JsonStringAt(binding, "target_cmb");
        if (!targetCmbName.empty() && NormalizeNativeResourceName(targetCmbName) != targetModelName) {
            continue;
        }

        const std::string normalizedCmabName = NormalizeNativeResourceName(cmabName);
        if (!loadedAnimationNames.insert(normalizedCmabName).second) {
            continue;
        }

        auto cmabBytes = ExtractFirstAvailableZarFile(archives, cmabName);
        scene.LinkMaterialAnimations.push_back(ParseCmabMaterialAnimationBytes(cmabBytes, cmabName));
    }
}

std::map<std::string, std::string> LinkFacebNameByStem(const nlohmann::json& characterManifest) {
    std::map<std::string, std::string> facebNameByStem;
    if (!characterManifest.is_object() || !characterManifest.contains("auxiliary_animation_payloads") ||
        !characterManifest.at("auxiliary_animation_payloads").is_object()) {
        return facebNameByStem;
    }

    const auto& auxiliaryPayloads = characterManifest.at("auxiliary_animation_payloads");
    if (!auxiliaryPayloads.contains("faceb_tracks") || !auxiliaryPayloads.at("faceb_tracks").is_array()) {
        return facebNameByStem;
    }

    for (const auto& track : auxiliaryPayloads.at("faceb_tracks")) {
        if (!track.is_object()) {
            continue;
        }
        const std::string facebName = JsonStringAt(track, "faceb_name");
        if (facebName.empty()) {
            continue;
        }
        std::string stem = JsonStringAt(track, "stem");
        if (stem.empty()) {
            stem = NativeResourceStem(facebName);
        } else {
            stem = NativeResourceStem(stem);
        }
        if (!stem.empty()) {
            facebNameByStem.emplace(stem, facebName);
        }
    }
    return facebNameByStem;
}

void AttachLinkFacebTracks(Oot3dNativeDemoScene& scene, const std::vector<std::filesystem::path>& archives,
                           const nlohmann::json& characterManifest) {
    const auto facebNameByStem = LinkFacebNameByStem(characterManifest);
    if (facebNameByStem.empty()) {
        return;
    }

    for (auto& clip : scene.LinkCsabClips) {
        const auto stem = NativeResourceStem(clip.CsabName);
        const auto found = facebNameByStem.find(stem);
        if (found == facebNameByStem.end()) {
            continue;
        }
        clip.FacebName = found->second;
        auto facebBytes = ExtractFirstAvailableZarFile(archives, clip.FacebName);
        clip.FacebTrack = ParseFacebMaterialFrameTrackBytes(facebBytes, clip.FacebName);
        clip.FacebTrackAvailable = true;
    }
}

void ExpandBoundsPoint(Oot3dDemoBounds& bounds, const Oot3dDemoVec3& point) {
    if (!bounds.Valid) {
        bounds.Min = point;
        bounds.Max = point;
        bounds.Valid = true;
        return;
    }
    bounds.Min.X = std::min(bounds.Min.X, point.X);
    bounds.Min.Y = std::min(bounds.Min.Y, point.Y);
    bounds.Min.Z = std::min(bounds.Min.Z, point.Z);
    bounds.Max.X = std::max(bounds.Max.X, point.X);
    bounds.Max.Y = std::max(bounds.Max.Y, point.Y);
    bounds.Max.Z = std::max(bounds.Max.Z, point.Z);
}

void ExpandBounds(Oot3dDemoBounds& bounds, const Vec3f& value, const Oot3dDemoVec3& offset = {}) {
    ExpandBoundsPoint(bounds, { value.X + offset.X, value.Y + offset.Y, value.Z + offset.Z });
}

bool MeshIndexSelected(const std::vector<uint32_t>* selectedMeshIndices, uint32_t meshIndex) {
    if (selectedMeshIndices == nullptr || selectedMeshIndices->empty()) {
        return true;
    }
    return std::find(selectedMeshIndices->begin(), selectedMeshIndices->end(), meshIndex) != selectedMeshIndices->end();
}

Matrix4f DemoIdentityMatrix() {
    Matrix4f matrix;
    for (size_t i = 0; i < 4; ++i) {
        matrix.M[i][i] = 1.0f;
    }
    return matrix;
}

Matrix4f DemoMultiplyMatrix(const Matrix4f& left, const Matrix4f& right) {
    Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            for (size_t k = 0; k < 4; ++k) {
                out.M[row][col] += left.M[row][k] * right.M[k][col];
            }
        }
    }
    return out;
}

std::optional<Matrix4f> InvertAffineMatrix(const Matrix4f& matrix) {
    const double a00 = matrix.M[0][0];
    const double a01 = matrix.M[0][1];
    const double a02 = matrix.M[0][2];
    const double a10 = matrix.M[1][0];
    const double a11 = matrix.M[1][1];
    const double a12 = matrix.M[1][2];
    const double a20 = matrix.M[2][0];
    const double a21 = matrix.M[2][1];
    const double a22 = matrix.M[2][2];
    const double det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
                       a02 * (a10 * a21 - a11 * a20);
    if (std::abs(det) <= 0.000000001) {
        return std::nullopt;
    }

    const double invDet = 1.0 / det;
    Matrix4f out{};
    out.M[0][0] = static_cast<float>((a11 * a22 - a12 * a21) * invDet);
    out.M[0][1] = static_cast<float>((a02 * a21 - a01 * a22) * invDet);
    out.M[0][2] = static_cast<float>((a01 * a12 - a02 * a11) * invDet);
    out.M[1][0] = static_cast<float>((a12 * a20 - a10 * a22) * invDet);
    out.M[1][1] = static_cast<float>((a00 * a22 - a02 * a20) * invDet);
    out.M[1][2] = static_cast<float>((a02 * a10 - a00 * a12) * invDet);
    out.M[2][0] = static_cast<float>((a10 * a21 - a11 * a20) * invDet);
    out.M[2][1] = static_cast<float>((a01 * a20 - a00 * a21) * invDet);
    out.M[2][2] = static_cast<float>((a00 * a11 - a01 * a10) * invDet);
    const double tx = matrix.M[0][3];
    const double ty = matrix.M[1][3];
    const double tz = matrix.M[2][3];
    out.M[0][3] = static_cast<float>(-(out.M[0][0] * tx + out.M[0][1] * ty + out.M[0][2] * tz));
    out.M[1][3] = static_cast<float>(-(out.M[1][0] * tx + out.M[1][1] * ty + out.M[1][2] * tz));
    out.M[2][3] = static_cast<float>(-(out.M[2][0] * tx + out.M[2][1] * ty + out.M[2][2] * tz));
    out.M[3][3] = 1.0f;
    return out;
}

Vec3f TransformPosition(const Matrix4f& transform, const Vec3f& position) {
    return {
        transform.M[0][0] * position.X + transform.M[0][1] * position.Y + transform.M[0][2] * position.Z +
            transform.M[0][3],
        transform.M[1][0] * position.X + transform.M[1][1] * position.Y + transform.M[1][2] * position.Z +
            transform.M[1][3],
        transform.M[2][0] * position.X + transform.M[2][1] * position.Y + transform.M[2][2] * position.Z +
            transform.M[2][3],
    };
}

Vec3f TransformDirection(const Matrix4f& transform, const Vec3f& direction) {
    return {
        transform.M[0][0] * direction.X + transform.M[0][1] * direction.Y + transform.M[0][2] * direction.Z,
        transform.M[1][0] * direction.X + transform.M[1][1] * direction.Y + transform.M[1][2] * direction.Z,
        transform.M[2][0] * direction.X + transform.M[2][1] * direction.Y + transform.M[2][2] * direction.Z,
    };
}

Vec3f NormalizeDirection(Vec3f value) {
    const double length = std::sqrt(static_cast<double>(value.X) * value.X +
                                    static_cast<double>(value.Y) * value.Y +
                                    static_cast<double>(value.Z) * value.Z);
    if (length <= 0.000001) {
        return { 0.0f, 1.0f, 0.0f };
    }
    return {
        static_cast<float>(value.X / length),
        static_cast<float>(value.Y / length),
        static_cast<float>(value.Z / length),
    };
}

std::vector<Matrix4f> BuildSkinTransforms(const std::vector<Matrix4f>& bindWorldTransforms, const CsabPose& pose) {
    std::vector<Matrix4f> transforms;
    const size_t count = std::min(bindWorldTransforms.size(), pose.WorldTransforms.size());
    transforms.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (auto inverseBind = InvertAffineMatrix(bindWorldTransforms[i])) {
            transforms.push_back(DemoMultiplyMatrix(pose.WorldTransforms[i], *inverseBind));
        } else {
            transforms.push_back(DemoIdentityMatrix());
        }
    }
    return transforms;
}

nlohmann::json BoundsJson(const Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return nullptr;
    }
    return {
        { "min", { { "x", bounds.Min.X }, { "y", bounds.Min.Y }, { "z", bounds.Min.Z } } },
        { "max", { { "x", bounds.Max.X }, { "y", bounds.Max.Y }, { "z", bounds.Max.Z } } },
        { "max_extent", NativeDemoBoundsMaxExtent(bounds) },
    };
}

} // namespace

Oot3dNativeDemoPicaLightingState DecodeOot3dNativeDemoPicaLightingStateForRuntime(
    const std::filesystem::path& scenePath,
    const std::filesystem::path& roomPath,
    int activeSetupIndex) {
    return DecodeOot3dNativeDemoPicaLightingState(scenePath, roomPath, activeSetupIndex);
}

Oot3dNativeDemoPicaLightSettingsRecord DecodeOot3dNativePicaLightSettingsRecordForRuntime(
    const std::vector<uint8_t>& bytes, int setupIndex, int recordIndex) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    if (bytes.size() != layout.NativeRecordSizeBytes && bytes.size() != layout.LegacyRecordSizeBytes) {
        throw std::invalid_argument("unsupported native PICA light-setting record size");
    }

    ZsiSceneCommand command;
    command.SetupIndex = setupIndex;
    ZsiLightSettingsListCandidate list;
    list.List.Valid = true;
    list.List.StartOffset = 0;
    list.List.Count = 1;
    list.EntrySize = static_cast<int>(bytes.size());
    list.Layout = bytes.size() == layout.NativeRecordSizeBytes ? layout.NativeRecordLayoutName
                                                               : layout.LegacyRecordLayoutName;
    auto record = ReadZsiPicaLightSettingsRecord(bytes, command, list, 0);
    record.Index = recordIndex;
    return record;
}

std::vector<Oot3dNativeDemoPicaLightSettingsRecord>
DecodeOot3dNativePicaLightSettingsTableForRuntime(
    const std::vector<uint8_t>& bytes, int setupIndex) {
    const auto& layout = NativeZsiLightSettingsRecordLayout();
    const size_t entrySize = layout.NativeRecordSizeBytes;
    if (bytes.empty() || entrySize == 0 || bytes.size() % entrySize != 0) {
        throw std::invalid_argument("invalid native PICA light-setting table size");
    }

    ZsiSceneCommand command;
    command.SetupIndex = setupIndex;
    ZsiLightSettingsListCandidate list;
    list.List.Valid = true;
    list.List.StartOffset = 0;
    list.List.Count = static_cast<int>(bytes.size() / entrySize);
    list.EntrySize = static_cast<int>(entrySize);
    list.Layout = layout.NativeRecordLayoutName;

    std::vector<Oot3dNativeDemoPicaLightSettingsRecord> records;
    records.reserve(static_cast<size_t>(list.List.Count));
    std::optional<Oot3dNativeDemoPicaLightSettingsRecord> previousRecord;
    for (int index = 0; index < list.List.Count; ++index) {
        auto record = ReadZsiPicaLightSettingsRecord(bytes, command, list, index);
        if (previousRecord.has_value() &&
            record.NativeActorVsLightPacketColorCandidateAvailable &&
            previousRecord->NativeActorVsLightPacketColorCandidateAvailable) {
            record.NativeActorVsAmbientColorCandidateAvailable = true;
            record.NativeActorVsAmbientColor =
                ActorVsAmbientColorCandidateFromRecords(*previousRecord, record);
        }
        previousRecord = record;
        records.push_back(std::move(record));
    }
    return records;
}

Oot3dNativeDemoAssetGraph DecodeOot3dNativeDemoAssetGraphForRuntime(
    const std::filesystem::path& scenePath,
    const std::filesystem::path& roomPath,
    const std::filesystem::path& manifestPath,
    const nlohmann::json& manifest,
    int activeSetupIndex) {
    bool explicitPathUsed = false;
    bool derivedFromRoomZsiPath = false;
    const auto codeBinPath =
        ResolveNativeCodeBinPath(manifestPath, manifest, roomPath, explicitPathUsed, derivedFromRoomZsiPath);
    return DecodeOot3dNativeDemoAssetGraph(scenePath, roomPath, manifestPath, manifest, activeSetupIndex,
                                           codeBinPath);
}

size_t NativeDemoDecodedTextureCount(const CmbModel& model) {
    return static_cast<size_t>(
        std::count_if(model.Textures.begin(), model.Textures.end(),
                      [](const auto& texture) { return texture.Rgba8Decoded; }));
}

int NativeDemoSurfaceTypeCameraDataIndex(const Oot3dNativeDemoSurfaceType& surfaceType) {
    return static_cast<int>(surfaceType.Data1 & 0xFF);
}

int NativeDemoSurfaceTypeLightSettingRawIndex(const Oot3dNativeDemoSurfaceType& surfaceType) {
    return static_cast<int>((surfaceType.Data2 >> 6) & 0x1F);
}

int NativeDemoNormalizeLightSettingIndex(int lightSettingIndex) {
    return lightSettingIndex > 30 ? 0 : lightSettingIndex;
}

int NativeDemoSurfaceTypeLightSettingIndex(const Oot3dNativeDemoSurfaceType& surfaceType) {
    return NativeDemoNormalizeLightSettingIndex(NativeDemoSurfaceTypeLightSettingRawIndex(surfaceType));
}

size_t NativeDemoSelectedPrimitiveCountBySkinningMode(const CmbModel& model,
                                                      const std::vector<uint32_t>& selectedMeshIndices,
                                                      uint16_t skinningMode) {
    size_t count = 0;
    for (const auto& mesh : model.Meshes) {
        if (!MeshIndexSelected(&selectedMeshIndices, mesh.Index) || mesh.ShapeIndex >= model.Shapes.size()) {
            continue;
        }
        const auto& shape = model.Shapes[mesh.ShapeIndex];
        count += static_cast<size_t>(std::count_if(shape.Primitives.begin(), shape.Primitives.end(),
                                                   [skinningMode](const auto& primitive) {
                                                       return primitive.SkinningMode == skinningMode;
                                                   }));
    }
    return count;
}

CsabPose SampleOot3dNativeDemoLinkPoseFrame(const Oot3dNativeDemoScene& scene, float frame) {
    return SampleOot3dNativeDemoLinkPoseFrame(scene, scene.LinkStandingClipIndex, frame);
}

CsabPose SampleOot3dNativeDemoLinkPoseFrame(const Oot3dNativeDemoScene& scene, size_t clipIndex, float frame) {
    if (clipIndex >= scene.LinkCsabClips.size()) {
        throw std::runtime_error("OOT3D native demo Link CSAB clip index is out of range");
    }
    const auto& clip = scene.LinkCsabClips[clipIndex];
    if (clip.Bytes.empty()) {
        throw std::runtime_error("OOT3D native demo Link CSAB clip has no resident bytes: " + clip.CsabName);
    }
    return scene.LinkPoseSamplingPolicyAvailable
               ? SampleCsabPoseFrameBytes(
                     clip.Bytes, scene.LinkModel, clip.Metadata, frame,
                     scene.LinkPoseSamplingPolicy)
               : SampleCsabPoseFrameBytes(clip.Bytes, scene.LinkModel, clip.Metadata, frame);
}

Oot3dNativeDemoLinkMaterialFrameSelection SampleOot3dNativeDemoLinkMaterialFrameSelection(
    const Oot3dNativeDemoScene& scene, size_t clipIndex, float frame) {
    if (clipIndex >= scene.LinkCsabClips.size()) {
        throw std::runtime_error("OOT3D native demo Link FACEB clip index is out of range");
    }

    Oot3dNativeDemoLinkMaterialFrameSelection selection;
    const auto& clip = scene.LinkCsabClips[clipIndex];
    selection.Source = clip.FacebName;
    selection.FacebTrackAvailable = clip.FacebTrackAvailable;
    if (!clip.FacebTrackAvailable) {
        return selection;
    }

    const auto sampled = SampleFacebMaterialFrameTrack(clip.FacebTrack, frame, selection.HoldValue);
    selection.EyeFrameSelected = sampled.EyeSelected;
    selection.MouthFrameSelected = sampled.MouthSelected;
    selection.EyeFrame = sampled.EyeIndex;
    selection.MouthFrame = sampled.MouthIndex;
    return selection;
}

std::vector<Matrix4f> BuildOot3dNativeDemoSkinTransforms(const std::vector<Matrix4f>& bindWorldTransforms,
                                                         const CsabPose& pose) {
    return BuildSkinTransforms(bindWorldTransforms, pose);
}

Oot3dDemoBounds NativeDemoModelBounds(const CmbModel& model, Oot3dDemoVec3 offset) {
    Oot3dDemoBounds bounds;
    for (const auto& shape : model.Shapes) {
        for (const auto& position : shape.Positions) {
            ExpandBounds(bounds, position, offset);
        }
    }
    return bounds;
}

Oot3dDemoBounds NativeDemoModelBoundsFromMeshes(const CmbModel& model, Oot3dDemoVec3 offset,
                                                const std::vector<uint32_t>* selectedMeshIndices,
                                                const CsabPose* pose,
                                                const std::vector<Matrix4f>* skinTransforms,
                                                double scale) {
    Oot3dDemoBounds bounds;
    for (const auto& mesh : model.Meshes) {
        if (!MeshIndexSelected(selectedMeshIndices, mesh.Index) || mesh.ShapeIndex >= model.Shapes.size()) {
            continue;
        }
        const auto& shape = model.Shapes[mesh.ShapeIndex];
        for (const auto& primitive : shape.Primitives) {
            for (const auto index : primitive.Indices) {
                if (index >= shape.Positions.size()) {
                    continue;
                }
                auto position = NativeDemoPosedVertexPosition(primitive, shape.Positions[index], index, pose,
                                                              skinTransforms);
                position.X *= static_cast<float>(scale);
                position.Y *= static_cast<float>(scale);
                position.Z *= static_cast<float>(scale);
                ExpandBounds(bounds, position, offset);
            }
        }
    }
    if (bounds.Valid) {
        return bounds;
    }
    if (scale == 1.0) {
        return NativeDemoModelBounds(model, offset);
    }

    Oot3dDemoBounds fallback;
    for (const auto& shape : model.Shapes) {
        for (auto position : shape.Positions) {
            position.X *= static_cast<float>(scale);
            position.Y *= static_cast<float>(scale);
            position.Z *= static_cast<float>(scale);
            ExpandBounds(fallback, position, offset);
        }
    }
    return fallback;
}

Oot3dDemoVec3 NativeDemoBoundsCenter(const Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return {};
    }
    return {
        (bounds.Min.X + bounds.Max.X) * 0.5,
        (bounds.Min.Y + bounds.Max.Y) * 0.5,
        (bounds.Min.Z + bounds.Max.Z) * 0.5,
    };
}

double NativeDemoBoundsMaxExtent(const Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return 0.0;
    }
    return std::max({ bounds.Max.X - bounds.Min.X, bounds.Max.Y - bounds.Min.Y, bounds.Max.Z - bounds.Min.Z });
}

void NativeDemoExpandBoundsByBounds(Oot3dDemoBounds& bounds, const Oot3dDemoBounds& value) {
    if (!value.Valid) {
        return;
    }
    ExpandBoundsPoint(bounds, value.Min);
    ExpandBoundsPoint(bounds, value.Max);
}

Vec3f NativeDemoPosedVertexPosition(const CmbPrimitive& primitive, const Vec3f& position, uint32_t vertexIndex,
                                    const CsabPose* pose, const std::vector<Matrix4f>* skinTransforms) {
    if (pose == nullptr || pose->WorldTransforms.empty() || vertexIndex >= primitive.VertexInfluences.size()) {
        return position;
    }

    const auto& influences = primitive.VertexInfluences[vertexIndex];
    if (influences.empty()) {
        return position;
    }

    if (primitive.SkinningMode == 0) {
        const auto boneIndex = influences.front().BoneIndex;
        if (boneIndex < pose->WorldTransforms.size()) {
            return TransformPosition(pose->WorldTransforms[boneIndex], position);
        }
        return position;
    }

    Vec3f out{};
    double totalWeight = 0.0;
    for (const auto& influence : influences) {
        if (influence.Weight <= 0.0f) {
            continue;
        }
        const auto boneIndex = influence.BoneIndex;
        if (skinTransforms == nullptr || boneIndex >= skinTransforms->size()) {
            continue;
        }
        const auto transformed = TransformPosition((*skinTransforms)[boneIndex], position);
        out.X += transformed.X * influence.Weight;
        out.Y += transformed.Y * influence.Weight;
        out.Z += transformed.Z * influence.Weight;
        totalWeight += influence.Weight;
    }
    if (totalWeight > 0.000001) {
        return out;
    }
    return position;
}

Vec3f NativeDemoPosedVertexNormal(const CmbPrimitive& primitive, const Vec3f& normal, uint32_t vertexIndex,
                                  const CsabPose* pose, const std::vector<Matrix4f>* skinTransforms) {
    const Vec3f normalizedNormal = NormalizeDirection(normal);
    if (pose == nullptr || pose->WorldTransforms.empty() || vertexIndex >= primitive.VertexInfluences.size()) {
        return normalizedNormal;
    }

    const auto& influences = primitive.VertexInfluences[vertexIndex];
    if (influences.empty()) {
        return normalizedNormal;
    }

    if (primitive.SkinningMode == 0) {
        const auto boneIndex = influences.front().BoneIndex;
        if (boneIndex < pose->WorldTransforms.size()) {
            return NormalizeDirection(TransformDirection(pose->WorldTransforms[boneIndex], normalizedNormal));
        }
        return normalizedNormal;
    }

    Vec3f out{};
    double totalWeight = 0.0;
    for (const auto& influence : influences) {
        if (influence.Weight <= 0.0f) {
            continue;
        }
        const auto boneIndex = influence.BoneIndex;
        if (skinTransforms == nullptr || boneIndex >= skinTransforms->size()) {
            continue;
        }
        const auto transformed = TransformDirection((*skinTransforms)[boneIndex], normalizedNormal);
        out.X += transformed.X * influence.Weight;
        out.Y += transformed.Y * influence.Weight;
        out.Z += transformed.Z * influence.Weight;
        totalWeight += influence.Weight;
    }
    if (totalWeight > 0.000001) {
        return NormalizeDirection(out);
    }
    return normalizedNormal;
}

std::optional<Oot3dNativeDemoFloorHit> Oot3dNativeDemoFloorHitAt(
    const Oot3dNativeDemoCollisionScene& collision, double x, double z, double queryY) {
    if (!collision.Valid) {
        return std::nullopt;
    }

    std::optional<Oot3dNativeDemoFloorHit> bestHit;
    double bestY = 0.0;
    for (size_t i = 0; i < collision.Polygons.size(); ++i) {
        const auto& poly = collision.Polygons[i];
        if (poly.IgnoreEntities) {
            continue;
        }
        if (poly.NormalY < 12000) {
            continue;
        }
        if (poly.VertexA < 0 || poly.VertexB < 0 || poly.VertexC < 0 ||
            static_cast<size_t>(poly.VertexA) >= collision.Vertices.size() ||
            static_cast<size_t>(poly.VertexB) >= collision.Vertices.size() ||
            static_cast<size_t>(poly.VertexC) >= collision.Vertices.size()) {
            continue;
        }
        const auto& a = collision.Vertices[poly.VertexA];
        const auto& b = collision.Vertices[poly.VertexB];
        const auto& c = collision.Vertices[poly.VertexC];
        if (!PointInTriXZ(x, z, a, b, c)) {
            continue;
        }
        const double nx = static_cast<double>(poly.NormalX) / 32767.0;
        const double ny = static_cast<double>(poly.NormalY) / 32767.0;
        const double nz = static_cast<double>(poly.NormalZ) / 32767.0;
        if (std::abs(ny) < 1e-5) {
            continue;
        }
        const double y = -(nx * x + nz * z + static_cast<double>(poly.Dist)) / ny;
        if (y < queryY && (!bestHit.has_value() || y > bestY)) {
            bestY = y;
            bestHit = Oot3dNativeDemoFloorHit{ y, static_cast<int>(i), poly.Type };
        }
    }
    return bestHit;
}

void AttachOot3dNativePlayerStartFloorLightSetting(Oot3dNativeDemoScene& scene) {
    scene.PlayerStart.FloorPolygonIndex = -1;
    scene.PlayerStart.FloorSurfaceType = -1;
    scene.PlayerStart.FloorLightSettingRawIndex = -1;
    scene.PlayerStart.FloorLightSettingIndex = -1;
    scene.PlayerStart.FloorLightSettingSource.clear();
    if (!scene.PlayerStart.Valid || !scene.Collision.Valid) {
        return;
    }

    const auto hit = Oot3dNativeDemoFloorHitAt(scene.Collision, scene.PlayerStart.Position.X,
                                              scene.PlayerStart.Position.Z,
                                              scene.PlayerStart.Position.Y + 120.0);
    if (!hit.has_value() || hit->SurfaceType < 0 ||
        static_cast<size_t>(hit->SurfaceType) >= scene.Collision.SurfaceTypes.size()) {
        return;
    }

    const auto& surfaceType = scene.Collision.SurfaceTypes[static_cast<size_t>(hit->SurfaceType)];
    const int rawLightSettingIndex = NativeDemoSurfaceTypeLightSettingRawIndex(surfaceType);
    scene.PlayerStart.FloorPolygonIndex = hit->PolygonIndex;
    scene.PlayerStart.FloorSurfaceType = hit->SurfaceType;
    scene.PlayerStart.FloorLightSettingRawIndex = rawLightSettingIndex;
    scene.PlayerStart.FloorLightSettingIndex = NativeDemoNormalizeLightSettingIndex(rawLightSettingIndex);
    scene.PlayerStart.FloorLightSettingSource =
        "oot3d_player_floor_surface_type_data2_bits_6_10_environment_change_light_setting";
}

Oot3dNativeDemoScene LoadOot3dNativeDemoSceneFromManifest(const std::filesystem::path& manifestPath,
                                                          std::string_view standingCsabName,
                                                          int preferredEntranceIndex,
                                                          int activeSetupIndexOverride,
                                                          std::string_view activeSetupSource,
                                                          const Oot3dNativeDemoPlayerClipSelection* playerClips) {
    const auto manifest = ReadJsonFile(manifestPath);
    const auto sources = manifest.at("sources");
    const auto roomVisual = sources.at("room_visual");
    const auto assets = manifest.value("assets", nlohmann::json::object());
    const auto collisionSource =
        sources.contains("collision") && sources.at("collision").is_object() ? sources.at("collision")
                                                                             : nlohmann::json::object();
    const auto linkChild = sources.at("link_child");
    const auto movement = manifest.value("movement", nlohmann::json::object());
    const auto spawn = movement.value("spawn", nlohmann::json::object());
    const auto entrypoint = movement.value("entrypoint", nlohmann::json::object());
    std::string preferredEntranceSource;
    const int manifestGlobalEntranceIndex = JsonIntAt(entrypoint, "global_entrance_index", -1);
    if (preferredEntranceIndex >= 0) {
        preferredEntranceSource = "argument_native_entrance_index";
    }

    Oot3dNativeDemoScene scene;
    scene.ManifestPath = manifestPath;
    scene.RoomZsiPath = ResolveRelativePath(manifestPath, JsonStringAt(roomVisual, "path"));
    scene.CollisionZsiPath = ResolveRelativePath(manifestPath, JsonStringAt(collisionSource, "path"));
    scene.CollisionResourcePath = ResolveRelativePath(manifestPath, JsonStringAt(assets, "collision_xml"));
    scene.LinkManifestPath = ResolveRelativePath(manifestPath, JsonStringAt(linkChild, "path"));
    scene.LinkCmbName = JsonStringAt(linkChild, "cmb_name");
    scene.LinkManifestCsabName = JsonStringAt(linkChild, "csab_name");
    scene.LinkStandingCsabName = std::string(standingCsabName);
    LoadNativeCameraTable(scene, manifestPath, manifest);
    LoadNativePicaLightingSemantics(scene, manifestPath, manifest);
    LoadNativePicaRegisterTrace(scene, manifestPath, manifest);
    LoadNativeCmbVShaderShbin(scene, manifestPath, manifest);
    bool nativeCodeBinExplicitPathUsed = false;
    bool nativeCodeBinDerivedFromRoomZsiPath = false;
    const auto nativeCodeBinPath =
        ResolveNativeCodeBinPath(manifestPath, manifest, scene.RoomZsiPath, nativeCodeBinExplicitPathUsed,
                                 nativeCodeBinDerivedFromRoomZsiPath);
    (void)nativeCodeBinDerivedFromRoomZsiPath;
    std::optional<Oot3dNativeDemoGlobalEntranceEntry> globalEntranceEntry;
    if (preferredEntranceIndex < 0 && manifestGlobalEntranceIndex >= 0) {
        globalEntranceEntry = DecodeOot3dNativeGlobalEntranceEntry(nativeCodeBinPath, nativeCodeBinExplicitPathUsed,
                                                                   manifestGlobalEntranceIndex);
        if (globalEntranceEntry.has_value()) {
            preferredEntranceIndex = globalEntranceEntry->LocalEntranceIndex;
            preferredEntranceSource = "manifest_oot3d_code_bin_global_entrance_table";
        }
    }
    if (preferredEntranceIndex < 0) {
        preferredEntranceIndex = JsonIntAt(entrypoint, "entrance_index", -1);
        if (preferredEntranceIndex >= 0) {
            preferredEntranceSource = "manifest_native_entrypoint";
        } else {
            preferredEntranceIndex = JsonIntAt(movement, "entrance_index", -1);
            if (preferredEntranceIndex >= 0) {
                preferredEntranceSource = "manifest_native_entrance_index";
            }
        }
    }
    scene.Spawn = {
        JsonNumberAt(spawn, "x", 0.0),
        JsonNumberAt(spawn, "y", 0.0),
        JsonNumberAt(spawn, "z", 0.0),
    };
    scene.LinkTargetHeight = JsonNumberAt(movement, "height_units", scene.LinkTargetHeight);
    scene.LinkRadius = JsonNumberAt(movement, "radius_units", scene.LinkRadius);
    scene.PlayerStart =
        DecodeOot3dNativeDemoPlayerStart(scene.CollisionZsiPath, preferredEntranceIndex, preferredEntranceSource);
    if (scene.PlayerStart.Valid && globalEntranceEntry.has_value()) {
        scene.PlayerStart.RequestedGlobalEntranceIndex = globalEntranceEntry->GlobalEntranceIndex;
        scene.PlayerStart.GlobalEntranceTableRuntimeAddress = globalEntranceEntry->TableRuntimeAddress;
        scene.PlayerStart.GlobalEntranceTableFileOffset = globalEntranceEntry->TableFileOffset;
        scene.PlayerStart.GlobalEntranceEntryFileOffset = globalEntranceEntry->EntryFileOffset;
        scene.PlayerStart.GlobalEntranceSceneId = globalEntranceEntry->SceneId;
        scene.PlayerStart.GlobalEntranceLocalEntranceIndex = globalEntranceEntry->LocalEntranceIndex;
        scene.PlayerStart.GlobalEntranceField = globalEntranceEntry->Field;
        scene.PlayerStart.GlobalEntranceSourceKind = globalEntranceEntry->SourceKind;
        scene.PlayerStart.GlobalEntranceCodeBinPath = globalEntranceEntry->CodeBinPath.string();
    }
    if (scene.PlayerStart.Valid) {
        scene.Spawn = scene.PlayerStart.Position;
    }
    scene.ActiveSceneSetupIndex = activeSetupIndexOverride >= 0
                                      ? activeSetupIndexOverride
                                      : (scene.PlayerStart.Valid ? scene.PlayerStart.SetupIndex : 0);
    scene.ActiveSceneSetupSource =
        activeSetupIndexOverride >= 0
            ? (activeSetupSource.empty() ? "argument_native_active_scene_setup_index"
                                         : std::string(activeSetupSource))
            : (scene.PlayerStart.Valid ? "player_start_setup_index" : "default_setup_0");
    scene.NativePicaLighting =
        DecodeOot3dNativeDemoPicaLightingState(scene.CollisionZsiPath, scene.RoomZsiPath,
                                               scene.ActiveSceneSetupIndex);
    LoadNativeRuntimeLightTransitionTable(scene.NativePicaLighting, nativeCodeBinPath, nativeCodeBinExplicitPathUsed);
    scene.AssetGraph = DecodeOot3dNativeDemoAssetGraph(scene.CollisionZsiPath, scene.RoomZsiPath, manifestPath,
                                                       manifest, scene.ActiveSceneSetupIndex, nativeCodeBinPath);
    LoadNativeActorVisualInstances(scene);

    const auto embeddedCmbs = ParseZsiEmbeddedCmbsFile(scene.RoomZsiPath);
    if (embeddedCmbs.empty()) {
        throw std::runtime_error("room ZSI contains no embedded CMB: " + scene.RoomZsiPath.string());
    }
    scene.RoomModel = embeddedCmbs.front().Model;
    scene.RoomBounds = NativeDemoModelBounds(scene.RoomModel);
    LoadRoomMaterialAnimations(scene);
    if (!scene.CollisionZsiPath.empty()) {
        if (!std::filesystem::is_regular_file(scene.CollisionZsiPath)) {
            throw std::runtime_error("declared OOT3D collision ZSI was not found: " +
                                     scene.CollisionZsiPath.string());
        }
        scene.Collision = ParseOot3dNativeDemoCollisionZsi(scene.CollisionZsiPath);
    } else if (!scene.CollisionResourcePath.empty()) {
        if (!std::filesystem::is_regular_file(scene.CollisionResourcePath)) {
            throw std::runtime_error("declared OOT3D collision resource was not found: " +
                                     scene.CollisionResourcePath.string());
        }
        scene.Collision = ParseOot3dNativeDemoCollisionXml(scene.CollisionResourcePath, scene.CollisionZsiPath);
    }
    AttachOot3dNativePlayerStartFloorLightSetting(scene);

    const auto characterManifest = ReadJsonFile(scene.LinkManifestPath);
    const auto archives = CharacterArchiveCandidates(scene.LinkManifestPath, characterManifest);
    const auto linkCmbBytes = ExtractFirstAvailableZarFile(archives, scene.LinkCmbName);
    scene.LinkModel = ParseCmbModelBytes(linkCmbBytes, scene.LinkCmbName);
    LoadLinkMaterialAnimations(scene, archives, characterManifest);

    if (playerClips != nullptr && playerClips->PoseSamplingPolicyAvailable) {
        scene.LinkPoseSamplingPolicyAvailable = true;
        scene.LinkPoseSamplingPolicy = playerClips->PoseSamplingPolicy;
        scene.LinkRootBaseTranslation = playerClips->RootBaseTranslation;
    }

    const std::string idleClip = playerClips != nullptr ? playerClips->Idle : scene.LinkStandingCsabName;
    const std::string walkClip =
        playerClips != nullptr ? playerClips->Walk : std::string(kDefaultLinkWalkCsabName);
    const std::string runClip = playerClips != nullptr ? playerClips->Run : scene.LinkManifestCsabName;
    scene.LinkStandingCsabName = idleClip;
    scene.LinkStandingClipIndex = AppendLinkCsabClip(scene, archives, "idle", idleClip);
    scene.LinkWalkClipIndex = AppendLinkCsabClip(scene, archives, "walk", walkClip);
    scene.LinkMovementClipIndex = runClip.empty()
                                      ? scene.LinkStandingClipIndex
                                      : AppendLinkCsabClip(scene, archives, "move", runClip);
    if (auto clipIndex = AppendOptionalLinkCsabClip(scene, archives, "walk_end_left",
                                                    playerClips != nullptr
                                                        ? playerClips->WalkEndLeft
                                                        : std::string(kDefaultLinkWalkEndLeftCsabName))) {
        scene.LinkWalkEndLeftClipIndex = *clipIndex;
    }
    if (auto clipIndex = AppendOptionalLinkCsabClip(scene, archives, "walk_end_right",
                                                    playerClips != nullptr
                                                        ? playerClips->WalkEndRight
                                                        : std::string(kDefaultLinkWalkEndRightCsabName))) {
        scene.LinkWalkEndRightClipIndex = *clipIndex;
    }
    if (playerClips != nullptr) {
        std::set<std::string> clipIds;
        for (const auto& clip : scene.LinkCsabClips) {
            clipIds.insert(clip.Id);
        }
        for (const auto& binding : playerClips->AdditionalClips) {
            if (binding.Id.empty() || binding.CsabName.empty()) {
                throw std::runtime_error("native player additional clip binding is incomplete");
            }
            if (!clipIds.insert(binding.Id).second) {
                throw std::runtime_error("duplicate native player clip id: " + binding.Id);
            }
            AppendLinkCsabClip(scene, archives, binding.Id, binding.CsabName);
        }
    }
    AttachLinkFacebTracks(scene, archives, characterManifest);
    const auto& standingClip = scene.LinkCsabClips[scene.LinkStandingClipIndex];
    scene.LinkStandingCsabBytes = standingClip.Bytes;
    scene.LinkCsab = standingClip.Metadata;
    scene.LinkStandingPose = SampleOot3dNativeDemoLinkPoseFrame(scene, 0.0f);
    scene.LinkBindWorldTransforms = BuildCmbSkeletonWorldTransforms(scene.LinkModel.Skeleton);
    scene.LinkSkinTransforms = BuildOot3dNativeDemoSkinTransforms(scene.LinkBindWorldTransforms, scene.LinkStandingPose);
    scene.LinkMeshIndices = MeshIndicesFromNativeBindPoseProfile(characterManifest, "all_meshes");
    if (scene.LinkMeshIndices.empty()) {
        scene.LinkMeshIndices.reserve(scene.LinkModel.Meshes.size());
        for (const auto& mesh : scene.LinkModel.Meshes) {
            scene.LinkMeshIndices.push_back(mesh.Index);
        }
    }

    const auto linkNativeBounds = NativeDemoModelBoundsFromMeshes(scene.LinkModel, {}, &scene.LinkMeshIndices,
                                                                  &scene.LinkStandingPose, &scene.LinkSkinTransforms);
    const double linkNativeHeight =
        linkNativeBounds.Valid ? linkNativeBounds.Max.Y - linkNativeBounds.Min.Y : 0.0;
    if (scene.LinkTargetHeight > 0.0 && linkNativeHeight > 0.000001) {
        scene.LinkScale = scene.LinkTargetHeight / linkNativeHeight;
    }
    const auto linkScaledBounds = NativeDemoModelBoundsFromMeshes(scene.LinkModel, {}, &scene.LinkMeshIndices,
                                                                  &scene.LinkStandingPose, &scene.LinkSkinTransforms,
                                                                  scene.LinkScale);
    const auto linkCenter = NativeDemoBoundsCenter(linkScaledBounds);
    scene.LinkOffset = {
        scene.Spawn.X - linkCenter.X,
        scene.Spawn.Y - (linkScaledBounds.Valid ? linkScaledBounds.Min.Y : 0.0),
        scene.Spawn.Z - linkCenter.Z,
    };
    scene.LinkBounds = NativeDemoModelBoundsFromMeshes(scene.LinkModel, scene.LinkOffset, &scene.LinkMeshIndices,
                                                       &scene.LinkStandingPose, &scene.LinkSkinTransforms,
                                                       scene.LinkScale);
    return scene;
}

nlohmann::json Oot3dNativeDemoCollisionSceneSummaryToJson(const Oot3dNativeDemoCollisionScene& collision) {
    if (!collision.Valid) {
        return {
            { "available", false },
        };
    }
    nlohmann::json surfaceTypes = nlohmann::json::array();
    for (size_t index = 0; index < collision.SurfaceTypes.size(); ++index) {
        const auto& surfaceType = collision.SurfaceTypes[index];
        surfaceTypes.push_back({
            { "index", index },
            { "data1", surfaceType.Data1 },
            { "data2", surfaceType.Data2 },
            { "cam_data_index", NativeDemoSurfaceTypeCameraDataIndex(surfaceType) },
            { "light_setting_raw_index", NativeDemoSurfaceTypeLightSettingRawIndex(surfaceType) },
            { "light_setting_index", NativeDemoSurfaceTypeLightSettingIndex(surfaceType) },
            { "light_setting_normalization",
              "Environment_ChangeLightSetting maps raw surface light indices greater than 30 to 0" },
        });
    }
    nlohmann::json bgCameras = nlohmann::json::array();
    for (size_t index = 0; index < collision.BgCameras.size(); ++index) {
        const auto& camera = collision.BgCameras[index];
        bgCameras.push_back({
            { "index", index },
            { "setting", camera.Setting },
            { "count", camera.Count },
            { "data_offset", camera.DataOffset },
            { "camera_position_vector_index", camera.CameraPositionVectorIndex },
        });
    }
    nlohmann::json bgCameraPositions = nlohmann::json::array();
    for (size_t index = 0; index < collision.BgCameraPositions.size(); ++index) {
        const auto& position = collision.BgCameraPositions[index];
        bgCameraPositions.push_back({
            { "index", index },
            { "position", { { "x", position.Position.X }, { "y", position.Position.Y }, { "z", position.Position.Z } } },
            { "rotation", { { "x", position.Rotation.X }, { "y", position.Rotation.Y }, { "z", position.Rotation.Z } } },
            { "other", { { "x", position.Other.X }, { "y", position.Other.Y }, { "z", position.Other.Z } } },
        });
    }
    int ignoreCameraCount = 0;
    int ignoreEntitiesCount = 0;
    int ignoreProjectilesCount = 0;
    int conveyorCount = 0;
    nlohmann::json flagsViaValues = nlohmann::json::object();
    nlohmann::json flagsVibValues = nlohmann::json::object();
    nlohmann::json polygonFlagSample = nlohmann::json::array();
    for (size_t index = 0; index < collision.Polygons.size(); ++index) {
        const auto& polygon = collision.Polygons[index];
        ignoreCameraCount += polygon.IgnoreCamera ? 1 : 0;
        ignoreEntitiesCount += polygon.IgnoreEntities ? 1 : 0;
        ignoreProjectilesCount += polygon.IgnoreProjectiles ? 1 : 0;
        conveyorCount += polygon.Conveyor ? 1 : 0;
        const std::string flagsViaKey = std::to_string(polygon.VertexAFlags);
        const std::string flagsVibKey = std::to_string(polygon.VertexBFlags);
        flagsViaValues[flagsViaKey] = flagsViaValues.value(flagsViaKey, 0) + 1;
        flagsVibValues[flagsVibKey] = flagsVibValues.value(flagsVibKey, 0) + 1;
        if (polygonFlagSample.size() < 8 &&
            (polygon.IgnoreCamera || polygon.IgnoreEntities || polygon.IgnoreProjectiles || polygon.Conveyor)) {
            polygonFlagSample.push_back({
                { "index", index },
                { "raw_vertex_a", polygon.RawVertexA },
                { "raw_vertex_b", polygon.RawVertexB },
                { "raw_vertex_c", polygon.RawVertexC },
                { "vertex_a", polygon.VertexA },
                { "vertex_b", polygon.VertexB },
                { "vertex_c", polygon.VertexC },
                { "flags_via_raw", polygon.VertexAFlags },
                { "flags_vib_raw", polygon.VertexBFlags },
                { "ignore_camera", polygon.IgnoreCamera },
                { "ignore_entities", polygon.IgnoreEntities },
                { "ignore_projectiles", polygon.IgnoreProjectiles },
                { "conveyor", polygon.Conveyor },
            });
        }
    }
    const nlohmann::json polygonFlagCounts = {
        { "ignore_camera", ignoreCameraCount },
        { "ignore_entities", ignoreEntitiesCount },
        { "ignore_projectiles", ignoreProjectilesCount },
        { "conveyor", conveyorCount },
        { "flags_via_values", flagsViaValues },
        { "flags_vib_values", flagsVibValues },
    };
    return {
        { "available", true },
        { "source_kind", "oot3d_scene_zsi_native_collision" },
        { "source", collision.SourceZsiPath.string() },
        { "resource_kind", collision.DecodedFromNativeZsi ? "oot3d_zsi_collision_header" : "CollisionHeader" },
        { "resource_format",
          collision.DecodedFromNativeZsi ? "oot3d_zsi_native_binary" : "shipwright_xml_derived_from_oot3d_zsi" },
        { "resource", collision.ResourcePath.string() },
        { "decoded_from_native_zsi", collision.DecodedFromNativeZsi },
        { "xml_fallback_used", collision.XmlFallbackUsed },
        { "visual_mesh_collision_source_used", false },
        { "native_collision_floor_probe_supported", true },
        { "floor_normal_y_threshold", 12000 },
        { "setup_index", collision.SetupIndex },
        { "command_offset", collision.CommandOffset },
        { "command_argument", collision.CommandArgument },
        { "header_offset", collision.HeaderOffset },
        { "effective_vertex_offset", collision.EffectiveVertexOffset },
        { "effective_polygon_offset", collision.EffectivePolygonOffset },
        { "effective_surface_type_offset", collision.EffectiveSurfaceTypeOffset },
        { "effective_bgcam_offset", collision.EffectiveBgCamOffset },
        { "camera_position_offset", collision.CameraPositionOffset },
        { "camera_pointer_adjustment", collision.CameraPointerAdjustment },
        { "surface_type_count", collision.SurfaceTypeCount },
        { "bgcam_count", collision.BgCamCount },
        { "decoded_surface_type_count", collision.SurfaceTypes.size() },
        { "decoded_bgcam_count", collision.BgCameras.size() },
        { "decoded_bgcam_position_count", collision.BgCameraPositions.size() },
        { "surface_types", surfaceTypes },
        { "bg_cameras", bgCameras },
        { "bg_camera_positions", bgCameraPositions },
        { "water_box_count", collision.WaterBoxCount },
        { "vertex_count", collision.Vertices.size() },
        { "polygon_count", collision.Polygons.size() },
        { "polygon_flag_counts", polygonFlagCounts },
        { "polygon_flag_sample", polygonFlagSample },
        { "bounds", BoundsJson(collision.Bounds) },
    };
}

nlohmann::json PlayerStartJson(const Oot3dNativeDemoPlayerStart& playerStart) {
    if (!playerStart.Valid) {
        return {
            { "available", false },
        };
    }
    return {
        { "available", true },
        { "setup_index", playerStart.SetupIndex },
        { "setup_offset", playerStart.SetupOffset },
        { "spawn_command_offset", playerStart.SpawnCommandOffset },
        { "spawn_list_offset", playerStart.SpawnListOffset },
        { "spawn_list_start_delta", playerStart.SpawnListStartDelta },
        { "spawn_index", playerStart.SpawnIndex },
        { "entrance_command_offset", playerStart.EntranceCommandOffset },
        { "entrance_list_offset", playerStart.EntranceListOffset },
        { "entrance_list_start_delta", playerStart.EntranceListStartDelta },
        { "entrance_index", playerStart.EntranceIndex },
        { "requested_entrance_index", playerStart.RequestedEntranceIndex },
        { "requested_global_entrance_index", playerStart.RequestedGlobalEntranceIndex },
        { "global_entrance_table_runtime_address", playerStart.GlobalEntranceTableRuntimeAddress },
        { "global_entrance_table_file_offset", playerStart.GlobalEntranceTableFileOffset },
        { "global_entrance_entry_file_offset", playerStart.GlobalEntranceEntryFileOffset },
        { "global_entrance_scene_id", playerStart.GlobalEntranceSceneId },
        { "global_entrance_local_entrance_index", playerStart.GlobalEntranceLocalEntranceIndex },
        { "global_entrance_field", playerStart.GlobalEntranceField },
        { "global_entrance_source_kind", playerStart.GlobalEntranceSourceKind },
        { "global_entrance_code_bin_path", playerStart.GlobalEntranceCodeBinPath },
        { "selection_source", playerStart.SelectionSource },
        { "room", playerStart.Room },
        { "actor_id", playerStart.ActorId },
        { "position",
          { { "x", playerStart.Position.X }, { "y", playerStart.Position.Y }, { "z", playerStart.Position.Z } } },
        { "rotation",
          { { "x", playerStart.Rotation.X }, { "y", playerStart.Rotation.Y }, { "z", playerStart.Rotation.Z } } },
        { "params", playerStart.Params },
        { "camera_data_index", playerStart.CameraDataIndex },
        { "floor_polygon_index", playerStart.FloorPolygonIndex },
        { "floor_surface_type", playerStart.FloorSurfaceType },
        { "floor_light_setting_raw_index", playerStart.FloorLightSettingRawIndex },
        { "floor_light_setting_index", playerStart.FloorLightSettingIndex },
        { "floor_light_setting_source", playerStart.FloorLightSettingSource },
        { "basis",
          "z_play reads player->actor.params & 0xff as playerStartBgCamIndex and calls Camera_ChangeDataIdx when it is not 0xff" },
    };
}

nlohmann::json ZsiCommandRecordJson(const Oot3dNativeDemoZsiCommandRecord& command) {
    return {
        { "setup_index", command.SetupIndex },
        { "offset", command.Offset },
        { "command_id", command.CommandId },
        { "command_name", ZsiCommandName(static_cast<uint8_t>(command.CommandId)) },
        { "parameter", command.Parameter },
        { "command_word", command.CommandWord },
        { "argument", command.Argument },
        { "argument_in_file", command.ArgumentInFile },
    };
}

nlohmann::json RoomReferenceJson(const Oot3dNativeDemoRoomReference& reference) {
    return {
        { "setup_index", reference.SetupIndex },
        { "command_offset", reference.CommandOffset },
        { "index", reference.Index },
        { "offset", reference.Offset },
        { "rom_path", reference.RomPath },
        { "resolved_path", reference.ResolvedPath.string() },
        { "available", reference.Available },
    };
}

nlohmann::json RoomPayloadListJson(const Oot3dNativeDemoRoomPayloadList& list) {
    if (!list.Valid) {
        return {
            { "available", false },
        };
    }
    return {
        { "available", true },
        { "command_offset", list.CommandOffset },
        { "command_argument", list.CommandArgument },
        { "count", list.Count },
        { "start_offset", list.StartOffset },
        { "start_delta", list.StartDelta },
        { "end_offset", list.EndOffset },
        { "payload_end_hint", list.PayloadEndHint },
        { "payload_end_gap", list.PayloadEndGap },
        { "entry_size", list.EntrySize },
        { "interpretation", list.Interpretation },
    };
}

std::string BytesToHexString(const std::vector<uint8_t>& bytes) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (size_t index = 0; index < bytes.size(); ++index) {
        if (index > 0) {
            stream << ' ';
        }
        stream << std::setw(2) << static_cast<int>(bytes[index]);
    }
    return stream.str();
}

nlohmann::json ByteVectorJson(const std::vector<uint8_t>& bytes) {
    nlohmann::json values = nlohmann::json::array();
    for (uint8_t value : bytes) {
        values.push_back(static_cast<int>(value));
    }
    return values;
}

nlohmann::json IntVectorJson(const std::vector<int>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (int value : values) {
        result.push_back(value);
    }
    return result;
}

nlohmann::json PicaByteGroupJson(const Oot3dNativeDemoPicaByteGroup& group) {
    return {
        { "raw", { group.Raw0, group.Raw1, group.Raw2, group.Raw3 } },
        { "signed", { group.Signed0, group.Signed1, group.Signed2, group.Signed3 } },
        { "normalized",
          { group.Normalized0, group.Normalized1, group.Normalized2, group.Normalized3 } },
    };
}

nlohmann::json ColorJson(ColorRgba8 color) {
    return {
        { "r", color.R },
        { "g", color.G },
        { "b", color.B },
        { "a", color.A },
    };
}

nlohmann::json Vec3Json(Vec3f value) {
    return {
        { "x", value.X },
        { "y", value.Y },
        { "z", value.Z },
    };
}

struct NativePicaTraceTextureKey {
    int Unit = -1;
    bool Enabled = false;
    int Format = -1;
    int Type = -1;
    int Width = -1;
    int Height = -1;
    std::string Address;
};

struct NativePicaTextureFormatMapping {
    bool Available = false;
    const char* Name = "unknown";
    uint16_t NativeTextureFormat = 0;
    uint16_t NativeDataType = 0;
};

struct NativePicaTraceDrawEvent {
    int DrawIndex = -1;
    int VertexCount = -1;
    bool FragmentLightingEnabled = false;
    bool ShadowPrimaryRoute = false;
    bool Shadow2dTextureBound = false;
    std::vector<NativePicaTraceTextureKey> Textures;
};

struct NativePicaTraceAssetCandidate {
    std::string ModelScope;
    std::string ModelName;
    std::string ModelSource;
    bool NativeCatalogPrimitiveOwner = false;
    bool NativeCatalogLoadedModel = false;
    size_t BatchIndex = 0;
    uint32_t MeshIndex = 0;
    uint32_t ShapeIndex = 0;
    int32_t MaterialIndex = -1;
    uint32_t PrimitiveIndex = 0;
    uint16_t SkinningMode = 0;
    size_t VertexCount = 0;
    int32_t TextureIndex = -1;
    std::string TextureName;
    uint32_t TextureMapperSlot = 0;
    uint16_t TextureWidth = 0;
    uint16_t TextureHeight = 0;
    uint16_t TextureFormat = 0;
    uint16_t TextureDataType = 0;
    int TraceTextureFormat = -1;
    std::string TraceTextureFormatName;
    bool TraceTextureFormatMappingAvailable = false;
    uint16_t TraceMappedNativeTextureFormat = 0;
    uint16_t TraceMappedNativeTextureDataType = 0;
    bool NativeTextureFormatMatchesTrace = false;
    bool NativeMaterialAvailable = false;
    uint64_t NativeMaterialRawFnv1a64 = 0;
    int32_t TextureEnvSelectedStageIndex = -1;
    uint32_t TextureEnvStageRecordCount = 0;
    bool FragmentLightingEnabled = false;
    bool VertexLightingEnabled = false;
    bool HemisphereLightingEnabled = false;
    bool NativePicaSelfShadowCandidate = false;
    std::string DrawSequenceFingerprint;
    size_t NativeDrawSequenceOrdinal = 0;
    int Score = 0;
    std::vector<std::string> Reasons;
};

struct NativePicaCatalogPrimitiveOwner {
    std::string ModelScope;
    std::string ModelName;
    std::string ModelSource;
    bool LoadedModel = false;
    uint32_t MeshIndex = 0;
    uint32_t ShapeIndex = 0;
    int32_t MaterialIndex = -1;
    uint32_t PrimitiveIndex = 0;
    uint16_t SkinningMode = 0;
    size_t VertexCount = 0;
    int32_t TextureIndex = -1;
    std::string TextureName;
    uint32_t TextureMapperSlot = 0;
    uint16_t TextureWidth = 0;
    uint16_t TextureHeight = 0;
    uint16_t TextureFormat = 0;
    uint16_t TextureDataType = 0;
    bool NativeMaterialAvailable = false;
    int32_t TextureEnvSelectedStageIndex = -1;
    uint32_t TextureEnvStageRecordCount = 0;
    bool FragmentLightingEnabled = false;
    bool VertexLightingEnabled = false;
    bool HemisphereLightingEnabled = false;
};

const nlohmann::json* JsonObjectPointer(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_object()) {
        return nullptr;
    }
    return &object.at(key);
}

const nlohmann::json* JsonArrayPointer(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_array()) {
        return nullptr;
    }
    return &object.at(key);
}

int JsonIntLikeAt(const nlohmann::json& object, const char* key, int fallback = -1) {
    if (!object.is_object() || !object.contains(key)) {
        return fallback;
    }
    const auto& value = object.at(key);
    if (value.is_number_integer()) {
        return value.get<int>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    if (value.is_string()) {
        try {
            size_t parsedLength = 0;
            const auto parsed = std::stoll(value.get<std::string>(), &parsedLength, 0);
            if (parsedLength == value.get<std::string>().size() &&
                parsed >= std::numeric_limits<int>::min() &&
                parsed <= std::numeric_limits<int>::max()) {
                return static_cast<int>(parsed);
            }
        } catch (const std::exception&) {
        }
    }
    return fallback;
}

std::string JsonStringLikeAt(const nlohmann::json& object, const char* key) {
    if (!object.is_object() || !object.contains(key) || !object.at(key).is_string()) {
        return "";
    }
    return object.at(key).get<std::string>();
}

constexpr uint16_t kNativePicaUnsignedByte = 0x1401;
constexpr uint16_t kNativePicaUnsignedByte44 = 0x6760;
constexpr uint16_t kNativePicaUnsigned4Bits = 0x6761;
constexpr uint16_t kNativePicaUnsignedShort4444 = 0x8033;
constexpr uint16_t kNativePicaUnsignedShort5551 = 0x8034;
constexpr uint16_t kNativePicaUnsignedShort565 = 0x8363;
constexpr uint16_t kNativePicaTextureRgba = 0x6752;
constexpr uint16_t kNativePicaTextureRgb = 0x6754;
constexpr uint16_t kNativePicaTextureAlpha = 0x6756;
constexpr uint16_t kNativePicaTextureLuminance = 0x6757;
constexpr uint16_t kNativePicaTextureLuminanceAlpha = 0x6758;
constexpr uint16_t kNativePicaTextureEtc1 = 0x675A;
constexpr uint16_t kNativePicaTextureEtc1A4 = 0x675B;

NativePicaTextureFormatMapping NativePicaTextureFormatMappingForTraceEnum(int format) {
    switch (format) {
        case 0:
            return { true, "RGBA8", kNativePicaTextureRgba, kNativePicaUnsignedByte };
        case 1:
            return { true, "RGB8", kNativePicaTextureRgb, kNativePicaUnsignedByte };
        case 2:
            return { true, "RGB5A1", kNativePicaTextureRgba, kNativePicaUnsignedShort5551 };
        case 3:
            return { true, "RGB565", kNativePicaTextureRgb, kNativePicaUnsignedShort565 };
        case 4:
            return { true, "RGBA4", kNativePicaTextureRgba, kNativePicaUnsignedShort4444 };
        case 5:
            return { true, "IA8", kNativePicaTextureLuminanceAlpha, kNativePicaUnsignedByte };
        case 6:
            return { false, "RG8", 0, 0 };
        case 7:
            return { true, "I8", kNativePicaTextureLuminance, kNativePicaUnsignedByte };
        case 8:
            return { true, "A8", kNativePicaTextureAlpha, kNativePicaUnsignedByte };
        case 9:
            return { true, "IA4", kNativePicaTextureLuminanceAlpha, kNativePicaUnsignedByte44 };
        case 10:
            return { true, "I4", kNativePicaTextureLuminance, kNativePicaUnsigned4Bits };
        case 11:
            return { false, "A4", 0, 0 };
        case 12:
            return { true, "ETC1", kNativePicaTextureEtc1, 0 };
        case 13:
            return { true, "ETC1A4", kNativePicaTextureEtc1A4, 0 };
        default:
            return {};
    }
}

std::vector<NativePicaTraceTextureKey> NativePicaTraceTextureKeysFromJson(
    const nlohmann::json& textures) {
    std::vector<NativePicaTraceTextureKey> out;
    if (!textures.is_array()) {
        return out;
    }
    out.reserve(textures.size());
    for (const auto& texture : textures) {
        if (!texture.is_object()) {
            continue;
        }
        NativePicaTraceTextureKey key;
        key.Unit = JsonIntLikeAt(texture, "index");
        key.Enabled = JsonIntLikeAt(texture, "enabled", 0) != 0;
        key.Format = JsonIntLikeAt(texture, "format");
        key.Type = JsonIntLikeAt(texture, "type");
        key.Width = JsonIntLikeAt(texture, "width");
        key.Height = JsonIntLikeAt(texture, "height");
        key.Address = JsonStringLikeAt(texture, "address");
        out.push_back(std::move(key));
    }
    return out;
}

std::vector<NativePicaTraceDrawEvent> NativePicaTraceDrawEventsFromScene(
    const Oot3dNativeDemoScene& scene) {
    std::vector<NativePicaTraceDrawEvent> out;
    if (!scene.NativePicaRegisterTraceAvailable || !scene.NativePicaRegisterTrace.is_object()) {
        return out;
    }
    const auto* frameCapture = JsonObjectPointer(scene.NativePicaRegisterTrace, "frame_capture");
    if (frameCapture == nullptr) {
        return out;
    }
    const auto* drawEvents = JsonArrayPointer(*frameCapture, "draw_events");
    if (drawEvents == nullptr) {
        return out;
    }
    out.reserve(drawEvents->size());
    for (const auto& item : *drawEvents) {
        if (!item.is_object()) {
            continue;
        }
        NativePicaTraceDrawEvent draw;
        draw.DrawIndex = JsonIntLikeAt(item, "draw_index");
        draw.VertexCount = JsonIntLikeAt(item, "num_vertices");
        if (const auto* textures = JsonArrayPointer(item, "textures"); textures != nullptr) {
            draw.Textures = NativePicaTraceTextureKeysFromJson(*textures);
            draw.Shadow2dTextureBound =
                std::any_of(draw.Textures.begin(), draw.Textures.end(),
                            [](const NativePicaTraceTextureKey& texture) {
                                return texture.Enabled && texture.Type == 2;
                            });
        }
        if (const auto* shadow = JsonObjectPointer(item, "shadow_summary"); shadow != nullptr) {
            draw.FragmentLightingEnabled = JsonIntLikeAt(*shadow, "lighting_enable0_raw", 0) != 0;
            const bool enableShadow = JsonIntLikeAt(*shadow, "lighting_enable_shadow", 0) != 0;
            const bool shadowPrimary = JsonIntLikeAt(*shadow, "lighting_shadow_primary", 0) != 0;
            const int disabledMask = JsonIntLikeAt(*shadow, "lighting_disable_shadow_mask", 0xFF);
            draw.ShadowPrimaryRoute = draw.FragmentLightingEnabled && enableShadow &&
                                      shadowPrimary && ((~disabledMask) & 0xFF) != 0;
        }
        out.push_back(std::move(draw));
    }
    return out;
}

std::vector<NativePicaTraceTextureKey> EnabledTraceTextures(
    const NativePicaTraceDrawEvent& draw) {
    std::vector<NativePicaTraceTextureKey> out;
    for (const auto& texture : draw.Textures) {
        if (texture.Enabled) {
            out.push_back(texture);
        }
    }
    return out;
}

std::string NativePicaTraceDrawSequenceFingerprint(const NativePicaTraceDrawEvent& draw) {
    std::vector<NativePicaTraceTextureKey> textures = EnabledTraceTextures(draw);
    std::sort(textures.begin(), textures.end(),
              [](const NativePicaTraceTextureKey& left, const NativePicaTraceTextureKey& right) {
                  return std::tie(left.Unit, left.Width, left.Height, left.Type, left.Format) <
                         std::tie(right.Unit, right.Width, right.Height, right.Type, right.Format);
              });

    std::ostringstream out;
    out << "v=" << draw.VertexCount << ";tex=";
    if (textures.empty()) {
        out << "none";
    } else {
        for (size_t index = 0; index < textures.size(); ++index) {
            if (index != 0) {
                out << ",";
            }
            const auto& texture = textures[index];
            out << texture.Unit << ":" << texture.Width << "x" << texture.Height;
        }
    }
    out << ";fragment_lighting=" << (draw.FragmentLightingEnabled ? 1 : 0);
    return out.str();
}

std::string NativePicaAssetCandidateDrawSequenceFingerprint(
    const NativePicaTraceAssetCandidate& candidate) {
    std::ostringstream out;
    out << "v=" << candidate.VertexCount << ";tex=";
    if (candidate.TextureIndex < 0 || candidate.TextureWidth == 0 || candidate.TextureHeight == 0) {
        out << "none";
    } else {
        out << candidate.TextureMapperSlot << ":" << candidate.TextureWidth << "x" <<
            candidate.TextureHeight;
    }
    out << ";fragment_lighting=" << (candidate.FragmentLightingEnabled ? 1 : 0);
    return out.str();
}

nlohmann::json NativePicaTraceTextureKeyJson(const NativePicaTraceTextureKey& texture) {
    const auto mapping = NativePicaTextureFormatMappingForTraceEnum(texture.Format);
    return {
        { "unit", texture.Unit },
        { "enabled", texture.Enabled },
        { "format", texture.Format },
        { "format_name", mapping.Name },
        { "native_cmb_format_mapping_available", mapping.Available },
        { "native_cmb_texture_format", mapping.NativeTextureFormat },
        { "native_cmb_texture_data_type", mapping.NativeDataType },
        { "type", texture.Type },
        { "width", texture.Width },
        { "height", texture.Height },
        { "address", texture.Address },
    };
}

nlohmann::json NativePicaTraceTextureKeysJson(
    const std::vector<NativePicaTraceTextureKey>& textures) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& texture : textures) {
        out.push_back(NativePicaTraceTextureKeyJson(texture));
    }
    return out;
}

void ScoreCandidateTextureFieldsAgainstTrace(
    const std::vector<NativePicaTraceTextureKey>& enabledTextures,
    NativePicaTraceAssetCandidate& candidate) {
    if (candidate.TextureIndex < 0 || candidate.TextureWidth == 0 ||
        candidate.TextureHeight == 0) {
        if (enabledTextures.empty()) {
            candidate.Score += 20;
            candidate.Reasons.push_back("untextured_trace_matches_untextured_batch");
        }
        return;
    }

    auto mappedTraceTexture = enabledTextures.end();
    auto mappedTraceTextureMapping = NativePicaTextureFormatMapping{};
    for (auto textureIt = enabledTextures.begin(); textureIt != enabledTextures.end();
         ++textureIt) {
        if (textureIt->Unit == static_cast<int>(candidate.TextureMapperSlot)) {
            mappedTraceTexture = textureIt;
            mappedTraceTextureMapping =
                NativePicaTextureFormatMappingForTraceEnum(textureIt->Format);
            break;
        }
    }
    if (mappedTraceTexture == enabledTextures.end() && !enabledTextures.empty()) {
        mappedTraceTexture = enabledTextures.begin();
        mappedTraceTextureMapping =
            NativePicaTextureFormatMappingForTraceEnum(mappedTraceTexture->Format);
    }
    if (mappedTraceTexture != enabledTextures.end()) {
        candidate.TraceTextureFormat = mappedTraceTexture->Format;
        candidate.TraceTextureFormatName = mappedTraceTextureMapping.Name;
        candidate.TraceTextureFormatMappingAvailable = mappedTraceTextureMapping.Available;
        candidate.TraceMappedNativeTextureFormat = mappedTraceTextureMapping.NativeTextureFormat;
        candidate.TraceMappedNativeTextureDataType = mappedTraceTextureMapping.NativeDataType;
        candidate.NativeTextureFormatMatchesTrace =
            mappedTraceTextureMapping.Available &&
            candidate.TextureFormat == mappedTraceTextureMapping.NativeTextureFormat &&
            candidate.TextureDataType == mappedTraceTextureMapping.NativeDataType;
        if (candidate.NativeTextureFormatMatchesTrace) {
            candidate.Score += 10;
            candidate.Reasons.push_back("pica_texture_format_matches_native_cmb_format");
        } else if (mappedTraceTextureMapping.Available) {
            candidate.Reasons.push_back("pica_texture_format_differs_from_native_cmb_format");
        } else {
            candidate.Reasons.push_back("pica_texture_format_mapping_unavailable");
        }
    }

    for (const auto& traceTexture : enabledTextures) {
        if (traceTexture.Width == static_cast<int>(candidate.TextureWidth) &&
            traceTexture.Height == static_cast<int>(candidate.TextureHeight)) {
            candidate.Score += 30;
            candidate.Reasons.push_back("texture_dimension_matches");
            if (traceTexture.Unit == static_cast<int>(candidate.TextureMapperSlot)) {
                candidate.Score += 5;
                candidate.Reasons.push_back("texture_unit_matches_mapper_slot");
            }
            return;
        }
    }
    if (!enabledTextures.empty()) {
        candidate.Reasons.push_back("texture_dimension_mismatch_or_format_mapping_pending");
    }
}

void ScoreTextureBindingAgainstTrace(const Oot3dNativeRenderModel& model,
                                     const Oot3dNativeRenderBatch& batch,
                                     const std::vector<NativePicaTraceTextureKey>& enabledTextures,
                                     NativePicaTraceAssetCandidate& candidate) {
    if (batch.Material.TextureIndex < 0 ||
        static_cast<size_t>(batch.Material.TextureIndex) >= model.Textures.size()) {
        if (enabledTextures.empty() && !batch.Material.Textured) {
            candidate.Score += 20;
            candidate.Reasons.push_back("untextured_trace_matches_untextured_batch");
        }
        return;
    }

    const auto& texture = model.Textures[static_cast<size_t>(batch.Material.TextureIndex)];
    candidate.TextureIndex = batch.Material.TextureIndex;
    candidate.TextureName = texture.Name;
    candidate.TextureWidth = texture.Width;
    candidate.TextureHeight = texture.Height;
    candidate.TextureFormat = texture.TextureFormat;
    candidate.TextureDataType = texture.DataType;

    ScoreCandidateTextureFieldsAgainstTrace(enabledTextures, candidate);
}

void AddNativePicaTraceAssetCandidatesForModel(const NativePicaTraceDrawEvent& draw,
                                               const Oot3dNativeRenderModel& model,
                                               const std::string& modelScope,
                                               std::vector<NativePicaTraceAssetCandidate>& out) {
    if (draw.VertexCount < 0) {
        return;
    }
    const auto enabledTextures = EnabledTraceTextures(draw);
    std::map<std::string, size_t> nativeSequenceOrdinalCounts;
    for (size_t batchIndex = 0; batchIndex < model.Batches.size(); ++batchIndex) {
        const auto& batch = model.Batches[batchIndex];
        if (static_cast<int>(batch.Vertices.size()) != draw.VertexCount) {
            continue;
        }

        NativePicaTraceAssetCandidate candidate;
        candidate.ModelScope = modelScope;
        candidate.ModelName = model.Name;
        candidate.ModelSource = model.Source;
        candidate.BatchIndex = batchIndex;
        candidate.MeshIndex = batch.MeshIndex;
        candidate.ShapeIndex = batch.ShapeIndex;
        candidate.MaterialIndex = batch.MaterialIndex;
        candidate.PrimitiveIndex = batch.PrimitiveIndex;
        candidate.SkinningMode = batch.SkinningMode;
        candidate.VertexCount = batch.Vertices.size();
        candidate.NativeMaterialAvailable = batch.Material.NativeMaterialAvailable;
        candidate.NativeMaterialRawFnv1a64 = batch.Material.NativeMaterialRawFnv1a64;
        candidate.TextureMapperSlot = batch.Material.TextureMapperSlot;
        candidate.TextureEnvSelectedStageIndex = batch.Material.TextureEnvSelectedStageIndex;
        candidate.TextureEnvStageRecordCount = batch.Material.TextureEnvStageRecordCount;
        candidate.FragmentLightingEnabled = batch.Material.FragmentLightingEnabled;
        candidate.VertexLightingEnabled = batch.Material.VertexLightingEnabled;
        candidate.HemisphereLightingEnabled = batch.Material.HemisphereLightingEnabled;
        candidate.NativePicaSelfShadowCandidate = batch.Material.NativePicaSelfShadowCandidate;
        candidate.Score = 100;
        candidate.Reasons.push_back("vertex_count_matches");
        ScoreTextureBindingAgainstTrace(model, batch, enabledTextures, candidate);
        if (batch.Material.NativeMaterialAvailable) {
            candidate.Score += 5;
            candidate.Reasons.push_back("native_cmb_material_hash_available");
        }
        if (batch.Material.TextureEnvStageRecordCount > 0) {
            candidate.Score += 5;
            candidate.Reasons.push_back("native_cmb_texture_env_stage_available");
        }
        if (draw.FragmentLightingEnabled == batch.Material.FragmentLightingEnabled) {
            candidate.Score += 5;
            candidate.Reasons.push_back("fragment_lighting_flag_matches_trace_state");
        }
        if (draw.ShadowPrimaryRoute && batch.Material.NativePicaSelfShadowCandidate) {
            candidate.Score += 5;
            candidate.Reasons.push_back("primary_shadow_route_matches_self_shadow_candidate");
        }
        candidate.DrawSequenceFingerprint =
            NativePicaAssetCandidateDrawSequenceFingerprint(candidate);
        candidate.NativeDrawSequenceOrdinal =
            nativeSequenceOrdinalCounts[candidate.DrawSequenceFingerprint]++;
        out.push_back(std::move(candidate));
    }
}

void AddNativePicaCatalogPrimitiveOwnersForModel(
    const CmbModel& model, const std::string& modelScope, bool loadedModel,
    std::vector<NativePicaCatalogPrimitiveOwner>& out) {
    for (const auto& mesh : model.Meshes) {
        if (mesh.ShapeIndex >= model.Shapes.size() ||
            mesh.MaterialIndex >= model.Materials.size()) {
            continue;
        }
        const auto& shape = model.Shapes[static_cast<size_t>(mesh.ShapeIndex)];
        const auto& material = model.Materials[static_cast<size_t>(mesh.MaterialIndex)];
        const int16_t textureIndex = material.TextureMappers[0].TextureIndex;
        if (textureIndex < 0 ||
            static_cast<size_t>(textureIndex) >= model.Textures.size()) {
            continue;
        }
        const auto& texture = model.Textures[static_cast<size_t>(textureIndex)];
        for (size_t primitiveIndex = 0; primitiveIndex < shape.Primitives.size(); ++primitiveIndex) {
            const auto& primitive = shape.Primitives[primitiveIndex];
            NativePicaCatalogPrimitiveOwner owner;
            owner.ModelScope = modelScope;
            owner.ModelName = model.Name;
            owner.ModelSource = model.Source;
            owner.LoadedModel = loadedModel;
            owner.MeshIndex = mesh.Index;
            owner.ShapeIndex = mesh.ShapeIndex;
            owner.MaterialIndex = mesh.MaterialIndex;
            owner.PrimitiveIndex = static_cast<uint32_t>(primitiveIndex);
            owner.SkinningMode = primitive.SkinningMode;
            owner.VertexCount = primitive.Indices.size();
            owner.TextureIndex = textureIndex;
            owner.TextureName = texture.Name;
            owner.TextureMapperSlot = 0;
            owner.TextureWidth = texture.Width;
            owner.TextureHeight = texture.Height;
            owner.TextureFormat = texture.TextureFormat;
            owner.TextureDataType = texture.DataType;
            owner.NativeMaterialAvailable = true;
            owner.TextureEnvSelectedStageIndex =
                material.TextureEnvStages.empty()
                    ? -1
                    : static_cast<int32_t>(material.TextureEnvStages.front().Index);
            owner.TextureEnvStageRecordCount =
                static_cast<uint32_t>(material.TextureEnvStages.size());
            owner.FragmentLightingEnabled = material.FragmentLightingEnabled;
            owner.VertexLightingEnabled = material.VertexLightingEnabled;
            owner.HemisphereLightingEnabled = material.HemisphereLightingEnabled;
            out.push_back(std::move(owner));
        }
    }
}

void AddNativePicaCatalogPrimitiveOwnersForArchive(
    const std::filesystem::path& archivePath, const std::string& modelScope,
    std::vector<NativePicaCatalogPrimitiveOwner>& out) {
    if (!std::filesystem::is_regular_file(archivePath)) {
        return;
    }

    try {
        const auto archive = ParseZarArchiveFile(archivePath);
        for (const auto& file : archive.Files) {
            if (file.TypeName != "cmb") {
                continue;
            }
            const auto bytes = ExtractZarFileBytes(archivePath, file.Name);
            auto model = ParseCmbModelBytes(bytes, archivePath.string() + "!" + file.Name);
            AddNativePicaCatalogPrimitiveOwnersForModel(model, modelScope, false, out);
        }
    } catch (const std::exception&) {
    }
}

std::vector<NativePicaCatalogPrimitiveOwner> BuildNativePicaCatalogPrimitiveOwners(
    const Oot3dNativeDemoScene& scene) {
    std::vector<NativePicaCatalogPrimitiveOwner> owners;
    if (!scene.RoomModel.Source.empty()) {
        AddNativePicaCatalogPrimitiveOwnersForModel(scene.RoomModel, "room", true, owners);
    }
    if (!scene.LinkModel.Source.empty()) {
        AddNativePicaCatalogPrimitiveOwnersForModel(scene.LinkModel, "link_child", true, owners);
    }
    for (size_t modelIndex = 0; modelIndex < scene.ActorVisualModels.size(); ++modelIndex) {
        const auto& model = scene.ActorVisualModels[modelIndex];
        AddNativePicaCatalogPrimitiveOwnersForModel(
            model.Model, "actor_visual_model#" + std::to_string(modelIndex), true, owners);
    }

    std::set<std::string> archiveKeys;
    for (const auto& object : scene.AssetGraph.RoomObjects) {
        if (!object.ArchiveAvailable) {
            continue;
        }
        const auto key = object.ArchivePath.lexically_normal().string();
        if (archiveKeys.insert(key).second) {
            AddNativePicaCatalogPrimitiveOwnersForArchive(
                object.ArchivePath, "room_object", owners);
        }
    }
    for (const auto& actor : scene.AssetGraph.RoomActors) {
        if (!actor.ArchiveAvailable) {
            continue;
        }
        const auto key = actor.ArchivePath.lexically_normal().string();
        if (archiveKeys.insert(key).second) {
            AddNativePicaCatalogPrimitiveOwnersForArchive(
                actor.ArchivePath, "room_actor", owners);
        }
    }
    return owners;
}

bool NativePicaCatalogCandidateDuplicatesExisting(
    const NativePicaTraceAssetCandidate& candidate,
    const std::vector<NativePicaTraceAssetCandidate>& existing) {
    return std::any_of(
        existing.begin(), existing.end(),
        [&](const NativePicaTraceAssetCandidate& other) {
            return other.ModelScope == candidate.ModelScope &&
                   other.ModelSource == candidate.ModelSource &&
                   other.MeshIndex == candidate.MeshIndex &&
                   other.ShapeIndex == candidate.ShapeIndex &&
                   other.MaterialIndex == candidate.MaterialIndex &&
                   other.PrimitiveIndex == candidate.PrimitiveIndex &&
                   other.TextureIndex == candidate.TextureIndex &&
                   other.VertexCount == candidate.VertexCount;
        });
}

void AddNativePicaTraceAssetCandidatesFromCatalog(
    const NativePicaTraceDrawEvent& draw,
    const std::vector<NativePicaCatalogPrimitiveOwner>& owners,
    std::vector<NativePicaTraceAssetCandidate>& out) {
    if (draw.VertexCount < 0) {
        return;
    }
    const auto enabledTextures = EnabledTraceTextures(draw);
    if (enabledTextures.empty()) {
        return;
    }

    std::map<std::string, size_t> nativeSequenceOrdinalCounts;
    for (const auto& owner : owners) {
        if (static_cast<int>(owner.VertexCount) != draw.VertexCount) {
            continue;
        }

        NativePicaTraceAssetCandidate candidate;
        candidate.ModelScope = owner.ModelScope;
        candidate.ModelName = owner.ModelName;
        candidate.ModelSource = owner.ModelSource;
        candidate.NativeCatalogPrimitiveOwner = true;
        candidate.NativeCatalogLoadedModel = owner.LoadedModel;
        candidate.MeshIndex = owner.MeshIndex;
        candidate.ShapeIndex = owner.ShapeIndex;
        candidate.MaterialIndex = owner.MaterialIndex;
        candidate.PrimitiveIndex = owner.PrimitiveIndex;
        candidate.SkinningMode = owner.SkinningMode;
        candidate.VertexCount = owner.VertexCount;
        candidate.TextureIndex = owner.TextureIndex;
        candidate.TextureName = owner.TextureName;
        candidate.TextureMapperSlot = owner.TextureMapperSlot;
        candidate.TextureWidth = owner.TextureWidth;
        candidate.TextureHeight = owner.TextureHeight;
        candidate.TextureFormat = owner.TextureFormat;
        candidate.TextureDataType = owner.TextureDataType;
        candidate.NativeMaterialAvailable = owner.NativeMaterialAvailable;
        candidate.TextureEnvSelectedStageIndex = owner.TextureEnvSelectedStageIndex;
        candidate.TextureEnvStageRecordCount = owner.TextureEnvStageRecordCount;
        candidate.FragmentLightingEnabled = owner.FragmentLightingEnabled;
        candidate.VertexLightingEnabled = owner.VertexLightingEnabled;
        candidate.HemisphereLightingEnabled = owner.HemisphereLightingEnabled;
        candidate.Score = owner.LoadedModel ? 98 : 90;
        candidate.Reasons.push_back("native_cmb_catalog_primitive_owner");
        ScoreCandidateTextureFieldsAgainstTrace(enabledTextures, candidate);
        if (!candidate.NativeTextureFormatMatchesTrace ||
            std::find(candidate.Reasons.begin(), candidate.Reasons.end(),
                      "texture_dimension_matches") == candidate.Reasons.end()) {
            continue;
        }
        if (candidate.NativeMaterialAvailable) {
            candidate.Score += 5;
            candidate.Reasons.push_back("native_cmb_material_hash_available");
        }
        if (candidate.TextureEnvStageRecordCount > 0) {
            candidate.Score += 5;
            candidate.Reasons.push_back("native_cmb_texture_env_stage_available");
        }
        if (draw.FragmentLightingEnabled == candidate.FragmentLightingEnabled) {
            candidate.Score += 5;
            candidate.Reasons.push_back("fragment_lighting_flag_matches_trace_state");
        }
        candidate.DrawSequenceFingerprint =
            NativePicaAssetCandidateDrawSequenceFingerprint(candidate);
        candidate.NativeDrawSequenceOrdinal =
            nativeSequenceOrdinalCounts[candidate.DrawSequenceFingerprint]++;
        if (!NativePicaCatalogCandidateDuplicatesExisting(candidate, out)) {
            out.push_back(std::move(candidate));
        }
    }
}

nlohmann::json NativePicaTraceAssetCandidateJson(
    const NativePicaTraceAssetCandidate& candidate) {
    return {
        { "model_scope", candidate.ModelScope },
        { "model_name", candidate.ModelName },
        { "model_source", candidate.ModelSource },
        { "native_catalog_primitive_owner", candidate.NativeCatalogPrimitiveOwner },
        { "native_catalog_loaded_model", candidate.NativeCatalogLoadedModel },
        { "batch_index", candidate.BatchIndex },
        { "mesh_index", candidate.MeshIndex },
        { "shape_index", candidate.ShapeIndex },
        { "material_index", candidate.MaterialIndex },
        { "primitive_index", candidate.PrimitiveIndex },
        { "skinning_mode", candidate.SkinningMode },
        { "vertex_count", candidate.VertexCount },
        { "texture_index", candidate.TextureIndex },
        { "texture_name", candidate.TextureName },
        { "texture_mapper_slot", candidate.TextureMapperSlot },
        { "texture_width", candidate.TextureWidth },
        { "texture_height", candidate.TextureHeight },
        { "native_cmb_texture_format", candidate.TextureFormat },
        { "native_cmb_texture_data_type", candidate.TextureDataType },
        { "trace_texture_format", candidate.TraceTextureFormat },
        { "trace_texture_format_name", candidate.TraceTextureFormatName },
        { "trace_texture_format_mapping_available",
          candidate.TraceTextureFormatMappingAvailable },
        { "trace_mapped_native_cmb_texture_format",
          candidate.TraceMappedNativeTextureFormat },
        { "trace_mapped_native_cmb_texture_data_type",
          candidate.TraceMappedNativeTextureDataType },
        { "native_texture_format_matches_trace", candidate.NativeTextureFormatMatchesTrace },
        { "native_material_available", candidate.NativeMaterialAvailable },
        { "native_material_raw_fnv1a64", std::to_string(candidate.NativeMaterialRawFnv1a64) },
        { "texture_env_selected_stage_index", candidate.TextureEnvSelectedStageIndex },
        { "texture_env_stage_record_count", candidate.TextureEnvStageRecordCount },
        { "fragment_lighting_enabled", candidate.FragmentLightingEnabled },
        { "vertex_lighting_enabled", candidate.VertexLightingEnabled },
        { "hemisphere_lighting_enabled", candidate.HemisphereLightingEnabled },
        { "native_pica_self_shadow_candidate", candidate.NativePicaSelfShadowCandidate },
        { "draw_sequence_fingerprint", candidate.DrawSequenceFingerprint },
        { "native_draw_sequence_ordinal", candidate.NativeDrawSequenceOrdinal },
        { "score", candidate.Score },
        { "reasons", candidate.Reasons },
    };
}

nlohmann::json NativePicaTraceAssetBindingsJson(
    const Oot3dNativeDemoScene& scene,
    const Oot3dNativeDemoRenderScene& renderScene) {
    const auto draws = NativePicaTraceDrawEventsFromScene(scene);
    if (draws.empty()) {
        return {
            { "available", false },
            { "format", "oot3d_native_pica_trace_asset_binding_v4" },
            { "source_kind", scene.NativePicaRegisterTraceSourceKind },
            { "trace_format", scene.NativePicaRegisterTraceFormat },
            { "uses_runtime_n64_asset_substitution", false },
        };
    }

    size_t singleCandidateCount = 0;
    size_t sequenceResolvedCandidateCount = 0;
    size_t bestScoreResolvedCandidateCount = 0;
    size_t ambiguousCandidateCount = 0;
    size_t unmatchedCount = 0;
    size_t totalCandidateCount = 0;
    size_t resolvedTextureFormatMatchCount = 0;
    size_t resolvedTextureFormatMismatchCount = 0;
    size_t resolvedTextureFormatUnavailableCount = 0;
    std::map<std::string, size_t> traceSequenceOrdinalCounts;
    nlohmann::json bindings = nlohmann::json::array();
    const auto nativeCatalogPrimitiveOwners = BuildNativePicaCatalogPrimitiveOwners(scene);

    for (const auto& draw : draws) {
        const std::string drawSequenceFingerprint = NativePicaTraceDrawSequenceFingerprint(draw);
        const size_t traceDrawSequenceOrdinal =
            traceSequenceOrdinalCounts[drawSequenceFingerprint]++;

        std::vector<NativePicaTraceAssetCandidate> candidates;
        AddNativePicaTraceAssetCandidatesForModel(draw, renderScene.Room, "room", candidates);
        AddNativePicaTraceAssetCandidatesForModel(draw, renderScene.Link, "link_child", candidates);
        for (size_t actorIndex = 0; actorIndex < renderScene.ActorVisuals.size(); ++actorIndex) {
            AddNativePicaTraceAssetCandidatesForModel(
                draw, renderScene.ActorVisuals[actorIndex],
                "actor_visual#" + std::to_string(actorIndex), candidates);
        }
        AddNativePicaTraceAssetCandidatesFromCatalog(
            draw, nativeCatalogPrimitiveOwners, candidates);

        std::sort(candidates.begin(), candidates.end(),
                  [](const NativePicaTraceAssetCandidate& left,
                     const NativePicaTraceAssetCandidate& right) {
                      if (left.Score != right.Score) {
                          return left.Score > right.Score;
                      }
                      return std::tie(left.ModelScope, left.ModelSource, left.BatchIndex,
                                      left.ShapeIndex, left.MaterialIndex, left.PrimitiveIndex) <
                             std::tie(right.ModelScope, right.ModelSource, right.BatchIndex,
                                      right.ShapeIndex, right.MaterialIndex,
                                      right.PrimitiveIndex);
                  });

        const size_t candidateCount = candidates.size();
        totalCandidateCount += candidateCount;
        std::string status = "unmatched";
        std::optional<size_t> resolvedCandidateIndex;
        if (candidateCount == 1) {
            status = "single_asset_candidate";
            ++singleCandidateCount;
            resolvedCandidateIndex = 0;
        } else if (candidateCount > 1) {
            const int bestScore = candidates.front().Score;
            size_t bestScoreCandidateCount = 0;
            for (size_t index = 0; index < candidates.size(); ++index) {
                const auto& candidate = candidates[index];
                if (candidate.Score != bestScore) {
                    break;
                }
                ++bestScoreCandidateCount;
                if (candidate.DrawSequenceFingerprint == drawSequenceFingerprint &&
                    candidate.NativeDrawSequenceOrdinal == traceDrawSequenceOrdinal) {
                    resolvedCandidateIndex = index;
                    break;
                }
            }
            if (resolvedCandidateIndex.has_value()) {
                status = "sequence_resolved_asset_candidate";
                ++sequenceResolvedCandidateCount;
                candidates[*resolvedCandidateIndex].Reasons.push_back(
                    "draw_sequence_fingerprint_ordinal_matches");
            } else if (bestScoreCandidateCount == 1) {
                status = "best_score_resolved_asset_candidate";
                ++bestScoreResolvedCandidateCount;
                resolvedCandidateIndex = 0;
                candidates[*resolvedCandidateIndex].Reasons.push_back(
                    "unique_best_score_without_sequence_ordinal_match");
            } else {
                status = "ambiguous_asset_candidates";
                ++ambiguousCandidateCount;
            }
        } else {
            ++unmatchedCount;
        }

        nlohmann::json candidateJson = nlohmann::json::array();
        const size_t candidateLimit = std::min<size_t>(8, candidates.size());
        for (size_t index = 0; index < candidateLimit; ++index) {
            candidateJson.push_back(NativePicaTraceAssetCandidateJson(candidates[index]));
        }

        const auto enabledTextures = EnabledTraceTextures(draw);
        nlohmann::json resolvedCandidateJson = nullptr;
        if (resolvedCandidateIndex.has_value()) {
            const auto& resolvedCandidate = candidates[*resolvedCandidateIndex];
            resolvedCandidateJson = NativePicaTraceAssetCandidateJson(resolvedCandidate);
            if (resolvedCandidate.TextureIndex >= 0 && resolvedCandidate.TraceTextureFormat >= 0) {
                if (!resolvedCandidate.TraceTextureFormatMappingAvailable) {
                    ++resolvedTextureFormatUnavailableCount;
                } else if (resolvedCandidate.NativeTextureFormatMatchesTrace) {
                    ++resolvedTextureFormatMatchCount;
                } else {
                    ++resolvedTextureFormatMismatchCount;
                }
            }
        }
        bindings.push_back({
            { "trace_draw_index", draw.DrawIndex },
            { "trace_vertex_count", draw.VertexCount },
            { "trace_draw_sequence_fingerprint", drawSequenceFingerprint },
            { "trace_draw_sequence_ordinal", traceDrawSequenceOrdinal },
            { "trace_fragment_lighting_enabled", draw.FragmentLightingEnabled },
            { "trace_primary_shadow_route", draw.ShadowPrimaryRoute },
            { "trace_shadow2d_texture_bound", draw.Shadow2dTextureBound },
            { "trace_enabled_textures", NativePicaTraceTextureKeysJson(enabledTextures) },
            { "candidate_count", candidateCount },
            { "status", status },
            { "best_score", candidates.empty() ? 0 : candidates.front().Score },
            { "resolved_candidate", resolvedCandidateJson },
            { "candidates", candidateJson },
        });
    }

    return {
        { "available", true },
        { "format", "oot3d_native_pica_trace_asset_binding_v4" },
        { "source_kind", scene.NativePicaRegisterTraceSourceKind },
        { "trace_format", scene.NativePicaRegisterTraceFormat },
        { "trace_path", scene.NativePicaRegisterTracePath.string() },
        { "association_policy",
          "offline_pica_trace_to_native_cmb_batch_fingerprint_no_runtime_asset_substitution" },
        { "runtime_usage_policy",
          "validation_and_semantic_promotion_only_do_not_consume_trace_values_as_runtime_assets" },
        { "uses_runtime_n64_asset_substitution", false },
        { "matching_keys",
          {
              "draw_vertex_count",
              "enabled_texture_unit_dimension",
              "native_cmb_material_raw_fnv1a64",
              "azahar_pica_texture_format_to_native_cmb_texture_format",
              "native_cmb_texture_env_stage_index",
              "native_cmb_lighting_flags",
              "native_cmb_catalog_exact_primitive_owner",
              "draw_sequence_fingerprint_ordinal",
          } },
        { "pica_texture_format_mapping_status",
          "azahar_texturing_regs_texture_format_enum_promoted_to_native_cmb_texture_format_and_data_type_diagnostic_key" },
        { "draw_count", draws.size() },
        { "single_asset_candidate_draw_count", singleCandidateCount },
        { "sequence_resolved_asset_candidate_draw_count", sequenceResolvedCandidateCount },
        { "best_score_resolved_asset_candidate_draw_count", bestScoreResolvedCandidateCount },
        { "resolved_asset_candidate_draw_count",
          singleCandidateCount + sequenceResolvedCandidateCount + bestScoreResolvedCandidateCount },
        { "ambiguous_asset_candidate_draw_count", ambiguousCandidateCount },
        { "unmatched_draw_count", unmatchedCount },
        { "total_candidate_count", totalCandidateCount },
        { "resolved_texture_format_match_draw_count", resolvedTextureFormatMatchCount },
        { "resolved_texture_format_mismatch_draw_count", resolvedTextureFormatMismatchCount },
        { "resolved_texture_format_unavailable_draw_count",
          resolvedTextureFormatUnavailableCount },
        { "bindings", bindings },
        { "basis",
          "PICA draw events from the emulator trace are joined to already extracted native OOT3D CMB batches by stable structural fingerprints. Runtime GPU addresses are preserved only as diagnostics because they are not asset identity." },
    };
}

nlohmann::json PicaLightSettingsRecordJson(const Oot3dNativeDemoPicaLightSettingsRecord& record) {
    nlohmann::json byteGroups = nlohmann::json::array();
    for (const auto& group : record.ByteGroups) {
        byteGroups.push_back(PicaByteGroupJson(group));
    }
    return {
        { "setup_index", record.SetupIndex },
        { "command_offset", record.CommandOffset },
        { "command_argument", record.CommandArgument },
        { "index", record.Index },
        { "offset", record.Offset },
        { "entry_size", record.EntrySize },
        { "layout", record.Layout },
        { "raw_bytes_hex", BytesToHexString(record.RawBytes) },
        { "raw_bytes", ByteVectorJson(record.RawBytes) },
        { "raw_halfwords", IntVectorJson(record.RawHalfwords) },
        { "pica_byte_groups", byteGroups },
        { "native_env_light_settings_available", record.NativeEnvLightSettingsAvailable },
        { "native_env_light_settings_prefix_size", record.NativeEnvLightSettingsPrefixSize },
        { "native_env_light_settings_color_source", "env_light_settings_bgr_u8" },
        { "native_env_light_settings",
          {
              { "ambient_color", ColorJson(record.AmbientColor) },
              { "light0_direction", Vec3Json(record.Light0Direction) },
              { "light0_color", ColorJson(record.Light0Color) },
              { "light1_direction", Vec3Json(record.Light1Direction) },
              { "light1_color", ColorJson(record.Light1Color) },
          } },
        { "native_actor_vs_light_packet_color_candidate_available",
          record.NativeActorVsLightPacketColorCandidateAvailable },
        { "native_actor_vs_light_packet_color_candidate",
          {
              { "source",
                "oot3d_zsi_light_settings_record_0x1c_rgb_fields_observed_in_azahar_vs_uniform_trace" },
              { "ambient_color_available", record.NativeActorVsAmbientColorCandidateAvailable },
              { "ambient_color_source",
                "previous_record_rgb_u8_tail_0x1a_0x1b_plus_current_record_rgb_u8_0x00" },
              { "ambient_color", ColorJson(record.NativeActorVsAmbientColor) },
              { "diffuse0_color_source", "rgb_u8_offset_0x04" },
              { "diffuse0_color", ColorJson(record.NativeActorVsDiffuse0Color) },
              { "diffuse1_color_source", "rgb_u8_offset_0x0a" },
              { "diffuse1_color", ColorJson(record.NativeActorVsDiffuse1Color) },
              { "ambient_color_status",
                "derived from the native 0x1c record stream and still requires actor packet selection validation before runtime promotion" },
          } },
        { "native_runtime_environment_light_settings_available",
          record.NativeRuntimeEnvironmentLightSettingsAvailable },
        { "native_runtime_environment_light_settings",
          {
              { "source", "code_bin_0045dd50_zsi_light_settings_consumer" },
              { "component_order", "rgb_u8" },
              { "record_offset", record.NativeRuntimeEnvironmentRecordOffset },
              { "record_start_delta", record.NativeRuntimeEnvironmentRecordStartDelta },
              { "ambient_color_offset", NativeZsiLightSettingsRecordLayout().RuntimeAmbientColorOffset },
              { "ambient_color", ColorJson(record.NativeRuntimeAmbientColor) },
              { "light0_direction_offset",
                NativeZsiLightSettingsRecordLayout().RuntimeLight0DirectionOffset },
              { "light0_direction", Vec3Json(record.NativeRuntimeLight0Direction) },
              { "light0_color_offset", NativeZsiLightSettingsRecordLayout().RuntimeLight0ColorOffset },
              { "light0_color", ColorJson(record.NativeRuntimeLight0Color) },
              { "light1_direction_offset",
                NativeZsiLightSettingsRecordLayout().RuntimeLight1DirectionOffset },
              { "light1_direction", Vec3Json(record.NativeRuntimeLight1Direction) },
              { "light1_color_offset", NativeZsiLightSettingsRecordLayout().RuntimeLight1ColorOffset },
              { "light1_color", ColorJson(record.NativeRuntimeLight1Color) },
              { "fog_color_offset", NativeZsiLightSettingsRecordLayout().RuntimeFogColorOffset },
              { "fog_color", ColorJson(record.NativeRuntimeFogColor) },
              { "scalar0_raw", record.NativeRuntimeScalar0Raw },
              { "scalar1_raw", record.NativeRuntimeScalar1Raw },
              { "packed_halfword_raw", record.NativeRuntimePackedHalfwordRaw },
          } },
        { "native_3ds_lighting_tail_byte0", record.Native3dsTailByte0 },
        { "native_3ds_lighting_tail_float_param0_offset", record.EntrySize == 0x1C ? 0x10 : -1 },
        { "native_3ds_lighting_tail_float_param1_offset", record.EntrySize == 0x1C ? 0x14 : -1 },
        { "float_param0", record.FloatParam0 },
        { "float_param1", record.FloatParam1 },
        { "float_params_finite", record.FloatParamsFinite },
        { "tail_raw", record.TailRaw },
        { "tail_bytes", ByteVectorJson(record.TailBytes) },
    };
}

nlohmann::json PayloadListArrayJson(const std::vector<Oot3dNativeDemoRoomPayloadList>& lists) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& list : lists) {
        result.push_back(RoomPayloadListJson(list));
    }
    return result;
}

nlohmann::json LightSettingsTransitionEntryJson(const Oot3dNativeDemoLightSettingsTransitionEntry& entry) {
    return {
        { "entry_index", entry.EntryIndex },
        { "start_angle", entry.StartAngle },
        { "end_angle", entry.EndAngle },
        { "from_light_setting_index", entry.FromLightSettingIndex },
        { "to_light_setting_index", entry.ToLightSettingIndex },
    };
}

nlohmann::json LightSettingsTransitionModeJson(const Oot3dNativeDemoLightSettingsTransitionMode& mode) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : mode.Entries) {
        entries.push_back(LightSettingsTransitionEntryJson(entry));
    }
    return {
        { "mode_index", mode.ModeIndex },
        { "entry_count", mode.Entries.size() },
        { "entries", entries },
    };
}

nlohmann::json LightSettingsTransitionModeArrayJson(
    const std::vector<Oot3dNativeDemoLightSettingsTransitionMode>& modes) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& mode : modes) {
        result.push_back(LightSettingsTransitionModeJson(mode));
    }
    return result;
}

nlohmann::json ZsiCommandRecordArrayJson(const std::vector<Oot3dNativeDemoZsiCommandRecord>& commands) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& command : commands) {
        result.push_back(ZsiCommandRecordJson(command));
    }
    return result;
}

nlohmann::json NativePicaLightingStateJson(const Oot3dNativeDemoPicaLightingState& lighting) {
    if (!lighting.Available) {
        return {
            { "available", false },
        };
    }

    nlohmann::json lightSettings = nlohmann::json::array();
    for (const auto& record : lighting.LightSettings) {
        lightSettings.push_back(PicaLightSettingsRecordJson(record));
    }

    return {
        { "available", true },
        { "source_kind", lighting.SourceKind },
        { "scene_zsi", lighting.SceneZsiPath.string() },
        { "room_zsi", lighting.RoomZsiPath.string() },
        { "code_bin", lighting.CodeBinPath.string() },
        { "decoded_from_native_zsi", lighting.DecodedFromNativeZsi },
        { "uses_runtime_n64_asset_substitution", lighting.UsesRuntimeN64AssetSubstitution },
        { "selected_light_settings_layout", lighting.SelectedLightSettingsLayout },
        { "native_runtime_transition_table_available", lighting.NativeRuntimeTransitionTableAvailable },
        { "native_runtime_transition_table_decoded_from_code_bin",
          lighting.NativeRuntimeTransitionTableDecodedFromCodeBin },
        { "native_runtime_transition_table_source_kind", lighting.NativeRuntimeTransitionTableSourceKind },
        { "native_runtime_transition_mode_count", lighting.NativeRuntimeTransitionModes.size() },
        { "native_runtime_transition_modes",
          LightSettingsTransitionModeArrayJson(lighting.NativeRuntimeTransitionModes) },
        { "native_runtime_transition_global_fallback_state_available",
          lighting.NativeRuntimeTransitionGlobalFallbackStateAvailable },
        { "native_runtime_transition_global_fallback_state_decoded_from_code_bin",
          lighting.NativeRuntimeTransitionGlobalFallbackStateDecodedFromCodeBin },
        { "native_runtime_transition_global_fallback_state_source_kind",
          lighting.NativeRuntimeTransitionGlobalFallbackStateSourceKind },
        { "native_runtime_transition_global_fallback_state_address",
          lighting.NativeRuntimeTransitionGlobalFallbackStateAddress },
        { "native_runtime_transition_global_fallback_mode_offset",
          lighting.NativeRuntimeTransitionGlobalFallbackModeOffset },
        { "native_runtime_transition_global_fallback_mode_weight_float_offset",
          lighting.NativeRuntimeTransitionGlobalFallbackModeWeightFloatOffset },
        { "native_runtime_transition_global_fallback_from_index_offset",
          lighting.NativeRuntimeTransitionGlobalFallbackFromIndexOffset },
        { "native_runtime_transition_global_fallback_to_index_offset",
          lighting.NativeRuntimeTransitionGlobalFallbackToIndexOffset },
        { "native_runtime_transition_global_fallback_mode",
          lighting.NativeRuntimeTransitionGlobalFallbackMode },
        { "native_runtime_transition_global_fallback_from_index",
          lighting.NativeRuntimeTransitionGlobalFallbackFromIndex },
        { "native_runtime_transition_global_fallback_to_index",
          lighting.NativeRuntimeTransitionGlobalFallbackToIndex },
        { "native_runtime_transition_global_fallback_mode_weight",
          lighting.NativeRuntimeTransitionGlobalFallbackModeWeight },
        { "active_setup_index", lighting.ActiveSetupIndex },
        { "scene_setup_count", lighting.SceneSetupCount },
        { "scene_light_settings_command_count", lighting.SceneLightSettingsCommandCount },
        { "scene_light_list_command_count", lighting.SceneLightListCommandCount },
        { "room_light_settings_command_count", lighting.RoomLightSettingsCommandCount },
        { "room_light_list_command_count", lighting.RoomLightListCommandCount },
        { "decoded_light_settings_record_count", lighting.DecodedLightSettingsRecordCount },
        { "active_setup_light_settings_record_count", lighting.ActiveSetupLightSettingsRecordCount },
        { "scene_light_settings_lists", PayloadListArrayJson(lighting.SceneLightSettingsLists) },
        { "room_light_settings_lists", PayloadListArrayJson(lighting.RoomLightSettingsLists) },
        { "scene_light_list_commands", ZsiCommandRecordArrayJson(lighting.SceneLightListCommands) },
        { "room_light_list_commands", ZsiCommandRecordArrayJson(lighting.RoomLightListCommands) },
        { "light_settings", lightSettings },
        { "basis",
          "OOT3D ZSI command 0x0F light_settings_list is decoded as native PICA lighting-state records; the current validated layout is selected from record stride and finite float fields, and the render path consumes the structured 0x0045dd50 runtime output map instead of the legacy prefix candidate" },
    };
}

nlohmann::json RoomObjectEntryJson(const Oot3dNativeDemoRoomObjectEntry& entry) {
    return {
        { "index", entry.Index },
        { "offset", entry.Offset },
        { "object_id", entry.ObjectId },
        { "possible_object_id", entry.PossibleObjectId },
        { "object_name", entry.ObjectName },
        { "archive_path", entry.ArchivePath.string() },
        { "archive_available", entry.ArchiveAvailable },
        { "archive_resolution_status", entry.ArchiveResolutionStatus },
        { "archive_file_count", entry.ArchiveFileCount },
        { "archive_cmb_count", entry.ArchiveCmbCount },
        { "archive_csab_count", entry.ArchiveCsabCount },
    };
}

nlohmann::json RoomActorEntryJson(const Oot3dNativeDemoRoomActorEntry& entry) {
    return {
        { "index", entry.Index },
        { "offset", entry.Offset },
        { "actor_id", entry.ActorId },
        { "actor_name", entry.ActorName },
        { "position", { { "x", entry.Position.X }, { "y", entry.Position.Y }, { "z", entry.Position.Z } } },
        { "rotation", { { "x", entry.Rotation.X }, { "y", entry.Rotation.Y }, { "z", entry.Rotation.Z } } },
        { "params", entry.Params },
        { "plausible_actor_entry", entry.Plausible },
        { "native_profile_available", entry.NativeProfileAvailable },
        { "native_profile_address", entry.NativeProfileAddress },
        { "native_profile_object_id", entry.NativeProfileObjectId },
        { "native_profile_object_name", entry.NativeProfileObjectName },
        { "native_profile_object_present_in_room_bank", entry.NativeProfileObjectPresentInRoomBank },
        { "native_profile_init_function_address", entry.NativeProfileInitFunctionAddress },
        { "native_profile_update_function_address", entry.NativeProfileUpdateFunctionAddress },
        { "native_profile_draw_function_address", entry.NativeProfileDrawFunctionAddress },
        { "native_visual_behavior_available", entry.NativeVisualBehaviorAvailable },
        { "native_visual_normal_cmb_type_local_index", entry.NativeVisualNormalCmbTypeLocalIndex },
        { "native_visual_fiery_cmb_type_local_index", entry.NativeVisualFieryCmbTypeLocalIndex },
        { "native_visual_scale", entry.NativeVisualScale },
        { "native_visual_rotation_y_step_s16_per_tick", entry.NativeVisualRotationYStepS16PerTick },
        { "native_visual_behavior_source", entry.NativeVisualBehaviorSource },
        { "archive_path", entry.ArchivePath.string() },
        { "archive_available", entry.ArchiveAvailable },
        { "archive_resolution_status", entry.ArchiveResolutionStatus },
        { "archive_file_count", entry.ArchiveFileCount },
        { "archive_cmb_count", entry.ArchiveCmbCount },
        { "archive_csab_count", entry.ArchiveCsabCount },
    };
}

nlohmann::json AssetGraphJson(const Oot3dNativeDemoAssetGraph& graph) {
    if (!graph.Valid) {
        return {
            { "available", false },
        };
    }

    nlohmann::json sceneCommands = nlohmann::json::array();
    for (const auto& command : graph.SceneCommands) {
        sceneCommands.push_back(ZsiCommandRecordJson(command));
    }
    nlohmann::json roomCommands = nlohmann::json::array();
    for (const auto& command : graph.RoomCommands) {
        roomCommands.push_back(ZsiCommandRecordJson(command));
    }
    nlohmann::json roomReferences = nlohmann::json::array();
    for (const auto& reference : graph.RoomReferences) {
        roomReferences.push_back(RoomReferenceJson(reference));
    }
    nlohmann::json roomObjects = nlohmann::json::array();
    for (const auto& object : graph.RoomObjects) {
        roomObjects.push_back(RoomObjectEntryJson(object));
    }
    nlohmann::json roomActors = nlohmann::json::array();
    for (const auto& actor : graph.RoomActors) {
        roomActors.push_back(RoomActorEntryJson(actor));
    }

    return {
        { "available", true },
        { "source_kind", "oot3d_zsi_scene_room_asset_graph" },
        { "scene_zsi", graph.SceneZsiPath.string() },
        { "room_zsi", graph.RoomZsiPath.string() },
        { "semantic_source", graph.SemanticSourcePath.string() },
        { "semantic_source_available", graph.SemanticSourceAvailable },
        { "actor_archive_root", graph.ActorArchiveRoot.string() },
        { "actor_archive_root_available", graph.ActorArchiveRootAvailable },
        { "native_actor_profile_code_bin", graph.NativeActorProfileCodeBinPath.string() },
        { "native_actor_profile_code_bin_available", graph.NativeActorProfileCodeBinAvailable },
        { "native_actor_profile_decoded_count", graph.NativeActorProfileDecodedCount },
        { "resolved_object_archive_count", graph.ResolvedObjectArchiveCount },
        { "resolved_actor_archive_count", graph.ResolvedActorArchiveCount },
        { "scene_setup_count", graph.SceneSetupCount },
        { "scene_command_count", graph.SceneCommandCount },
        { "requested_room_setup_index", graph.RequestedRoomSetupIndex },
        { "room_base_command_table_offset", graph.RoomBaseCommandTableOffset },
        { "room_command_table_offset", graph.RoomCommandTableOffset },
        { "room_alternate_header_list_offset", graph.RoomAlternateHeaderListOffset },
        { "room_alternate_header_entry_index", graph.RoomAlternateHeaderEntryIndex },
        { "room_alternate_header_raw_offset", graph.RoomAlternateHeaderRawOffset },
        { "room_command_table_selection_status", graph.RoomCommandTableSelectionStatus },
        { "room_command_count", graph.RoomCommandCount },
        { "manifest_room_path_fallback_used", graph.ManifestRoomPathFallbackUsed },
        { "room_reference_count", graph.RoomReferences.size() },
        { "room_object_count", graph.RoomObjects.size() },
        { "room_actor_count", graph.RoomActors.size() },
        { "scene_commands", sceneCommands },
        { "room_commands", roomCommands },
        { "room_references", roomReferences },
        { "room_object_list", RoomPayloadListJson(graph.RoomObjectList) },
        { "room_objects", roomObjects },
        { "room_actor_list", RoomPayloadListJson(graph.RoomActorList) },
        { "room_actors", roomActors },
        { "basis",
          "scene and room dependencies are decoded from native OOT3D ZSI command tables and rom:/scene references" },
    };
}

nlohmann::json CmbTextureCatalogLutsJson(const CmbLutSection& luts);

nlohmann::json ActorVisualModelJson(const Oot3dNativeDemoActorVisualModel& model) {
    const std::vector<uint32_t> allMeshes;
    return {
        { "actor_id", model.ActorId },
        { "actor_name", model.ActorName },
        { "archive_path", model.ArchivePath.string() },
        { "cmb_name", model.CmbName },
        { "selection_status", model.SelectionStatus },
        { "mesh_count", model.Model.Meshes.size() },
        { "shape_count", model.Model.Shapes.size() },
        { "primitive_count", model.Model.PrimitiveCount() },
        { "rigid_primitive_count", NativeDemoSelectedPrimitiveCountBySkinningMode(model.Model, allMeshes, 0) },
        { "mode1_primitive_count", NativeDemoSelectedPrimitiveCountBySkinningMode(model.Model, allMeshes, 1) },
        { "skinned_primitive_count", NativeDemoSelectedPrimitiveCountBySkinningMode(model.Model, allMeshes, 2) },
        { "triangle_count", model.Model.TriangleCount() },
        { "vertex_count", model.Model.VertexCount() },
        { "texture_count", model.Model.Textures.size() },
        { "decoded_texture_count", NativeDemoDecodedTextureCount(model.Model) },
        { "material_count", model.Model.Materials.size() },
        { "lut_chunk_decoded", model.Model.Luts.Decoded },
        { "lut_chunk_size", model.Model.Luts.ChunkSize },
        { "lut_record_count", model.Model.Luts.Records.size() },
        { "luts", CmbTextureCatalogLutsJson(model.Model.Luts) },
        { "bounds", BoundsJson(NativeDemoModelBounds(model.Model)) },
    };
}

nlohmann::json ActorVisualInstanceJson(const Oot3dNativeDemoActorVisualInstance& instance) {
    return {
        { "actor_entry_index", instance.ActorEntryIndex },
        { "actor_id", instance.ActorId },
        { "actor_name", instance.ActorName },
        { "model_index", instance.ModelIndex },
        { "archive_path", instance.ArchivePath.string() },
        { "cmb_name", instance.CmbName },
        { "position", { { "x", instance.Position.X }, { "y", instance.Position.Y }, { "z", instance.Position.Z } } },
        { "rotation_s16", { { "x", instance.Rotation.X }, { "y", instance.Rotation.Y }, { "z", instance.Rotation.Z } } },
        { "scale", instance.Scale },
        { "rotation_y_step_s16_per_tick", instance.RotationYStepS16PerTick },
        { "selected_cmb_type_local_index", instance.SelectedCmbTypeLocalIndex },
        { "native_visual_behavior_resolved", instance.NativeVisualBehaviorResolved },
        { "behavior_source", instance.BehaviorSource },
        { "transform_status", instance.TransformStatus },
        { "params", instance.Params },
    };
}

nlohmann::json ActorVisualSkippedInstanceJson(const Oot3dNativeDemoActorVisualSkippedInstance& instance) {
    return {
        { "actor_entry_index", instance.ActorEntryIndex },
        { "actor_id", instance.ActorId },
        { "actor_name", instance.ActorName },
        { "model_index", instance.ModelIndex },
        { "archive_path", instance.ArchivePath.string() },
        { "cmb_name", instance.CmbName },
        { "position", { { "x", instance.Position.X }, { "y", instance.Position.Y }, { "z", instance.Position.Z } } },
        { "rotation_s16", { { "x", instance.Rotation.X }, { "y", instance.Rotation.Y }, { "z", instance.Rotation.Z } } },
        { "params", instance.Params },
        { "cmb_entry_count", instance.CmbEntryCount },
        { "parseable_cmb_entry_count", instance.ParseableCmbEntryCount },
        { "skinned_primitive_count", instance.SkinnedPrimitiveCount },
        { "reason", instance.Reason },
        { "selection_status", instance.SelectionStatus },
    };
}

nlohmann::json ActorVisualsJson(const Oot3dNativeDemoScene& scene) {
    nlohmann::json models = nlohmann::json::array();
    for (const auto& model : scene.ActorVisualModels) {
        models.push_back(ActorVisualModelJson(model));
    }

    nlohmann::json instances = nlohmann::json::array();
    for (const auto& instance : scene.ActorVisualInstances) {
        instances.push_back(ActorVisualInstanceJson(instance));
    }

    nlohmann::json skippedInstances = nlohmann::json::array();
    for (const auto& instance : scene.ActorVisualSkippedInstances) {
        skippedInstances.push_back(ActorVisualSkippedInstanceJson(instance));
    }

    return {
        { "source_kind", "oot3d_zsi_actor_entries_to_native_zar_cmb" },
        { "selection_basis", "ActorEntry actor id -> semantic enum -> native ZAR archive; render only native actor CMBs whose runtime model and rigid transform are fully determined; skipped instances preserve unresolved native selection contracts for later decoding" },
        { "uses_runtime_n64_asset_substitution", false },
        { "model_count", scene.ActorVisualModels.size() },
        { "instance_count", scene.ActorVisualInstances.size() },
        { "skipped_instance_count", scene.ActorVisualSkippedInstances.size() },
        { "models", models },
        { "instances", instances },
        { "skipped_instances", skippedInstances },
    };
}

std::string NativePicaCmbTextureFormatName(uint16_t textureFormat, uint16_t dataType) {
    if (textureFormat == kNativePicaTextureRgba && dataType == kNativePicaUnsignedByte) {
        return "RGBA8";
    }
    if (textureFormat == kNativePicaTextureRgb && dataType == kNativePicaUnsignedByte) {
        return "RGB8";
    }
    if (textureFormat == kNativePicaTextureRgba && dataType == kNativePicaUnsignedShort5551) {
        return "RGB5A1";
    }
    if (textureFormat == kNativePicaTextureRgb && dataType == kNativePicaUnsignedShort565) {
        return "RGB565";
    }
    if (textureFormat == kNativePicaTextureRgba && dataType == kNativePicaUnsignedShort4444) {
        return "RGBA4";
    }
    if (textureFormat == kNativePicaTextureLuminanceAlpha && dataType == kNativePicaUnsignedByte) {
        return "IA8";
    }
    if (textureFormat == kNativePicaTextureAlpha && dataType == kNativePicaUnsignedByte) {
        return "A8";
    }
    if (textureFormat == kNativePicaTextureLuminance && dataType == kNativePicaUnsignedByte) {
        return "I8";
    }
    if (textureFormat == kNativePicaTextureLuminanceAlpha && dataType == kNativePicaUnsignedByte44) {
        return "IA4";
    }
    if (textureFormat == kNativePicaTextureLuminance && dataType == kNativePicaUnsigned4Bits) {
        return "I4";
    }
    if (textureFormat == kNativePicaTextureEtc1 && dataType == 0) {
        return "ETC1";
    }
    if (textureFormat == kNativePicaTextureEtc1A4 && dataType == 0) {
        return "ETC1A4";
    }
    return "unknown";
}

nlohmann::json CmbTextureCatalogTextureJson(const CmbTexture& texture) {
    return {
        { "texture_index", texture.Index },
        { "texture_name", texture.Name },
        { "width", texture.Width },
        { "height", texture.Height },
        { "texture_format", texture.TextureFormat },
        { "data_type", texture.DataType },
        { "format_name", NativePicaCmbTextureFormatName(texture.TextureFormat, texture.DataType) },
        { "data_offset", texture.DataOffset },
        { "data_size", texture.DataSize },
        { "rgba8_decoded", texture.Rgba8Decoded },
    };
}

nlohmann::json CmbTextureCatalogTextureRefJson(const CmbModel& model, int16_t textureIndex) {
    if (textureIndex < 0 || static_cast<size_t>(textureIndex) >= model.Textures.size()) {
        return {
            { "texture_index", textureIndex },
            { "available", false },
        };
    }

    const auto& texture = model.Textures[static_cast<size_t>(textureIndex)];
    return {
        { "texture_index", textureIndex },
        { "available", true },
        { "resolved_texture_index", texture.Index },
        { "texture_name", texture.Name },
        { "width", texture.Width },
        { "height", texture.Height },
        { "texture_format", texture.TextureFormat },
        { "data_type", texture.DataType },
        { "format_name", NativePicaCmbTextureFormatName(texture.TextureFormat, texture.DataType) },
    };
}

nlohmann::json CmbTextureCatalogMaterialMapperJson(const CmbModel& model,
                                                   const CmbMaterialTextureMapper& mapper,
                                                   size_t mapperSlot) {
    return {
        { "mapper_slot", mapperSlot },
        { "texture_index", mapper.TextureIndex },
        { "min_filter", mapper.MinFilter },
        { "mag_filter", mapper.MagFilter },
        { "wrap_s", mapper.WrapS },
        { "wrap_t", mapper.WrapT },
        { "texture", CmbTextureCatalogTextureRefJson(model, mapper.TextureIndex) },
    };
}

nlohmann::json CmbTextureCatalogMaterialJson(const CmbModel& model, const CmbMaterial& material) {
    nlohmann::json rawTextureStageSlots = nlohmann::json::array();
    for (int16_t slot : material.RawTextureStageSlots) {
        rawTextureStageSlots.push_back(slot);
    }

    nlohmann::json textureEnvStageTableIndices = nlohmann::json::array();
    for (const auto& textureEnv : material.TextureEnvStages) {
        textureEnvStageTableIndices.push_back(textureEnv.Index);
    }

    nlohmann::json textureEnvStageSourceOffsets = nlohmann::json::array();
    nlohmann::json textureEnvStageResolved = nlohmann::json::array();
    for (size_t stage = 0; stage < material.PostMaterialTextureEnvStageResolved.size(); ++stage) {
        const bool resolved = material.PostMaterialTextureEnvStageResolved[stage];
        textureEnvStageResolved.push_back(resolved);
        if (resolved) {
            textureEnvStageSourceOffsets.push_back(material.PostMaterialTextureEnvStageSourceOffsets[stage]);
        } else {
            textureEnvStageSourceOffsets.push_back(nullptr);
        }
    }

    nlohmann::json textureMappers = nlohmann::json::array();
    for (size_t mapperSlot = 0; mapperSlot < std::size(material.TextureMappers); ++mapperSlot) {
        textureMappers.push_back(
            CmbTextureCatalogMaterialMapperJson(model, material.TextureMappers[mapperSlot], mapperSlot));
    }

    return {
        { "material_index", material.Index },
        { "texture_mappers_used", material.TextureMappersUsed },
        { "texture_coords_used", material.TextureCoordsUsed },
        { "raw_texture_stage_selector_decoded", material.RawTextureStageSelectorDecoded },
        { "raw_texture_stage_count", material.RawTextureStageCount },
        { "raw_texture_stage_slots", rawTextureStageSlots },
        { "texture_env_stage_record_count", material.TextureEnvStages.size() },
        { "texture_env_stage_table_indices", textureEnvStageTableIndices },
        { "post_material_texture_env_table_decoded", material.PostMaterialTextureEnvTableDecoded },
        { "post_material_texture_env_table_derived_from_lane_pointer",
          material.PostMaterialTextureEnvTableDerivedFromLanePointer },
        { "post_material_texture_env_table_source_offset",
          material.PostMaterialTextureEnvTableSourceOffset },
        { "post_material_texture_env_table_record_size",
          material.PostMaterialTextureEnvTableRecordSize },
        { "post_material_texture_env_table_record_count",
          material.PostMaterialTextureEnvTableRecordCount },
        { "post_material_texture_env_stage_resolved", textureEnvStageResolved },
        { "post_material_texture_env_stage_source_offsets", textureEnvStageSourceOffsets },
        { "texture_mappers", textureMappers },
    };
}

nlohmann::json CmbTextureCatalogDrawPrimitiveJson(const CmbModel& model,
                                                  const CmbMesh& mesh,
                                                  const CmbPrimitive& primitive,
                                                  size_t primitiveIndex) {
    const bool materialAvailable = mesh.MaterialIndex < model.Materials.size();
    const CmbMaterial* material =
        materialAvailable ? &model.Materials[static_cast<size_t>(mesh.MaterialIndex)] : nullptr;
    const int16_t textureIndex = material != nullptr ? material->TextureMappers[0].TextureIndex : -1;
    return {
        { "mesh_index", mesh.Index },
        { "shape_index", mesh.ShapeIndex },
        { "material_index", mesh.MaterialIndex },
        { "material_available", materialAvailable },
        { "primitive_index", primitiveIndex },
        { "skinning_mode", primitive.SkinningMode },
        { "vertex_count", primitive.Indices.size() },
        { "triangle_count", primitive.Indices.size() / 3 },
        { "bone_count", primitive.BoneIndices.size() },
        { "primary_texture", CmbTextureCatalogTextureRefJson(model, textureIndex) },
    };
}

nlohmann::json CmbTextureCatalogDrawPrimitivesJson(const CmbModel& model) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& mesh : model.Meshes) {
        if (mesh.ShapeIndex >= model.Shapes.size()) {
            out.push_back({
                { "mesh_index", mesh.Index },
                { "shape_index", mesh.ShapeIndex },
                { "material_index", mesh.MaterialIndex },
                { "shape_available", false },
            });
            continue;
        }

        const auto& shape = model.Shapes[static_cast<size_t>(mesh.ShapeIndex)];
        for (size_t primitiveIndex = 0; primitiveIndex < shape.Primitives.size(); ++primitiveIndex) {
            out.push_back(CmbTextureCatalogDrawPrimitiveJson(model, mesh, shape.Primitives[primitiveIndex],
                                                             primitiveIndex));
        }
    }
    return out;
}

nlohmann::json CmbTextureCatalogLutPointJson(const CmbLutRecord::Point& point, size_t pointIndex) {
    return {
        { "point_index", pointIndex },
        { "x", point.X },
        { "value", point.Value },
        { "tangent_in", point.TangentIn },
        { "tangent_out", point.TangentOut },
    };
}

nlohmann::json CmbTextureCatalogLutRecordJson(const CmbLutRecord& record) {
    nlohmann::json points = nlohmann::json::array();
    constexpr size_t kPointPreviewLimit = 16;
    for (size_t pointIndex = 0; pointIndex < record.Points.size() && pointIndex < kPointPreviewLimit; ++pointIndex) {
        points.push_back(CmbTextureCatalogLutPointJson(record.Points[pointIndex], pointIndex));
    }

    nlohmann::json sampleMin = nullptr;
    nlohmann::json sampleMax = nullptr;
    nlohmann::json firstSample = nullptr;
    nlohmann::json lastSample = nullptr;
    if (!record.Samples.empty()) {
        const auto [minIt, maxIt] = std::minmax_element(record.Samples.begin(), record.Samples.end());
        sampleMin = *minIt;
        sampleMax = *maxIt;
        firstSample = record.Samples.front();
        lastSample = record.Samples.back();
    }

    return {
        { "record_index", record.Index },
        { "source_offset", record.SourceOffset },
        { "size", record.Size },
        { "type", record.Type },
        { "header_byte_01", record.HeaderByte01 },
        { "header_byte_02", record.HeaderByte02 },
        { "header_byte_03", record.HeaderByte03 },
        { "point_count", record.PointCount },
        { "point_stride_bytes", record.PointStrideBytes },
        { "header_word_08", record.HeaderWord08 },
        { "header_word_0c", record.HeaderWord0C },
        { "decoded_point_count", record.Points.size() },
        { "sample_count", record.Samples.size() },
        { "packed_base_value_count", record.PackedBaseValues.size() },
        { "packed_delta_value_count", record.PackedDeltaValues.size() },
        { "sample_min", sampleMin },
        { "sample_max", sampleMax },
        { "first_sample", firstSample },
        { "last_sample", lastSample },
        { "point_preview_truncated", record.Points.size() > kPointPreviewLimit },
        { "points", points },
    };
}

nlohmann::json CmbTextureCatalogLutsJson(const CmbLutSection& luts) {
    nlohmann::json records = nlohmann::json::array();
    for (const auto& record : luts.Records) {
        records.push_back(CmbTextureCatalogLutRecordJson(record));
    }

    return {
        { "decoded", luts.Decoded },
        { "source_offset", luts.SourceOffset },
        { "chunk_size", luts.ChunkSize },
        { "record_count", luts.Records.size() },
        { "declared_record_count", luts.Count },
        { "header_word_0c", luts.HeaderWord0C },
        { "records", records },
    };
}

nlohmann::json CmbTextureCatalogCmbJson(const std::filesystem::path& archivePath,
                                        const ZarFileEntry& file) {
    nlohmann::json out = {
        { "entry_index", file.Index },
        { "entry_name", file.Name },
        { "entry_type", file.TypeName },
        { "entry_type_local_index",
          file.TypeLocalIndex == std::numeric_limits<uint32_t>::max() ? nlohmann::json(nullptr)
                                                                      : nlohmann::json(file.TypeLocalIndex) },
        { "entry_offset", file.Offset },
        { "entry_size", file.Size },
        { "parse_status", "not_parsed" },
    };

    try {
        const auto bytes = ExtractZarFileBytes(archivePath, file.Name);
        const auto model = ParseCmbModelBytes(bytes, archivePath.string() + "!" + file.Name);

        nlohmann::json textures = nlohmann::json::array();
        for (const auto& texture : model.Textures) {
            textures.push_back(CmbTextureCatalogTextureJson(texture));
        }

        nlohmann::json materials = nlohmann::json::array();
        for (const auto& material : model.Materials) {
            materials.push_back(CmbTextureCatalogMaterialJson(model, material));
        }

        out["parse_status"] = "parsed";
        out["model_name"] = model.Name;
        out["mesh_count"] = model.Meshes.size();
        out["shape_count"] = model.Shapes.size();
        out["primitive_count"] = model.PrimitiveCount();
        out["triangle_count"] = model.TriangleCount();
        out["vertex_count"] = model.VertexCount();
        out["texture_count"] = model.Textures.size();
        out["decoded_texture_count"] = NativeDemoDecodedTextureCount(model);
        out["material_count"] = model.Materials.size();
        out["texture_env_table_record_count"] = model.TextureEnvSettings.size();
        out["lut_chunk_decoded"] = model.Luts.Decoded;
        out["lut_chunk_size"] = model.Luts.ChunkSize;
        out["lut_record_count"] = model.Luts.Records.size();
        out["luts"] = CmbTextureCatalogLutsJson(model.Luts);
        out["textures"] = textures;
        out["materials"] = materials;
        out["draw_primitives"] = CmbTextureCatalogDrawPrimitivesJson(model);
    } catch (const std::exception& exc) {
        out["parse_status"] = "parse_failed";
        out["parse_error"] = exc.what();
    }

    return out;
}

nlohmann::json NativeCtxbDescriptorSlotJson(const NativeCtxbDescriptorSlot& slot) {
    return {
        { "slot_index", slot.SlotIndex },
        { "slot_stride_bytes", slot.SlotStrideBytes },
        { "slot_record_base_offset", slot.SlotRecordBaseOffset },
        { "source_texture_header_base_offset", slot.SourceTextureHeaderBaseOffset },
        { "source_payload_pointer_field_offset", slot.SourcePayloadPointerFieldOffset },
        { "texture_header_parameter", slot.TextureHeaderParameter },
        { "width", slot.Width },
        { "height", slot.Height },
        { "texture_format", slot.TextureFormat },
        { "data_type", slot.DataType },
        { "packed_format_data_type", slot.PackedFormatDataType },
        { "payload_size", slot.PayloadSize },
    };
}

nlohmann::json NativeKankyoRuntimeHelperJson(const NativeKankyoRuntimeHelperContract& helper) {
    return {
        { "function_address", helper.FunctionAddress },
        { "container_storage_size", helper.ContainerStorageSize },
        { "instance_storage_size", helper.InstanceStorageSize },
        { "container_class_id", helper.ContainerClassId },
        { "instance_class_id", helper.InstanceClassId },
        { "backing_storage_class_id", helper.BackingStorageClassId },
        { "instance_initializer_address", helper.InstanceInitializerAddress },
    };
}

nlohmann::json NativeKankyoRuntimeEffectWrapperJson(
    const NativeKankyoRuntimeEffectWrapperContract& wrapper) {
    return {
        { "function_address", wrapper.FunctionAddress },
        { "manager_argument_ordinal", wrapper.ManagerArgumentOrdinal },
        { "descriptor_object_argument_ordinal", wrapper.DescriptorObjectArgumentOrdinal },
        { "optional_backing_storage_argument_ordinal", wrapper.OptionalBackingStorageArgumentOrdinal },
        { "backing_callsite_scan_output_path", wrapper.BackingCallsiteScanOutputPath },
        { "container_storage_size", wrapper.ContainerStorageSize },
        { "instance_storage_size", wrapper.InstanceStorageSize },
        { "optional_backing_storage_size", wrapper.OptionalBackingStorageSize },
        { "container_class_id", wrapper.ContainerClassId },
        { "instance_class_id", wrapper.InstanceClassId },
        { "optional_backing_storage_class_id", wrapper.OptionalBackingStorageClassId },
        { "optional_backing_storage_default_initializer_address",
          wrapper.OptionalBackingStorageDefaultInitializerAddress },
        { "container_initializer_address", wrapper.ContainerInitializerAddress },
        { "instance_initializer_address", wrapper.InstanceInitializerAddress },
        { "container_vtable_address", wrapper.ContainerVtableAddress },
        { "container_submit_vtable_slot_offset", wrapper.ContainerSubmitVtableSlotOffset },
        { "container_submit_vtable_entry_address", wrapper.ContainerSubmitVtableEntryAddress },
        { "container_submit_function_address", wrapper.ContainerSubmitFunctionAddress },
        { "container_descriptor_pointer_offset", wrapper.ContainerDescriptorPointerOffset },
        { "container_instance_pointer_offset", wrapper.ContainerInstancePointerOffset },
        { "container_backing_storage_pointer_offset", wrapper.ContainerBackingStoragePointerOffset },
        { "instance_backing_storage_pointer_offset", wrapper.InstanceBackingStoragePointerOffset },
        { "manager_word_copied_to_backing_storage_offset",
          wrapper.ManagerWordCopiedToBackingStorageOffset },
        { "return_value_is_instance_pointer", wrapper.ReturnValueIsInstancePointer },
        { "return_value_is_container_pointer", wrapper.ReturnValueIsContainerPointer },
        { "return_value_is_packet_prep_input", wrapper.ReturnValueIsPacketPrepInput },
        { "container_backing_storage_pointer_is_packet_prep_input",
          wrapper.ContainerBackingStoragePointerIsPacketPrepInput },
        { "writes_draw_handle_packet_buffer", wrapper.WritesDrawHandlePacketBuffer },
        { "null_optional_backing_allocates_default_storage",
          wrapper.NullOptionalBackingAllocatesDefaultStorage },
        { "external_optional_backing_bypasses_default_storage_allocation",
          wrapper.ExternalOptionalBackingBypassesDefaultStorageAllocation },
        { "optional_backing_argument_stored_at_container_backing_storage_pointer",
          wrapper.OptionalBackingArgumentStoredAtContainerBackingStoragePointer },
        { "default_allocated_backing_stored_at_instance_backing_storage_pointer",
          wrapper.DefaultAllocatedBackingStoredAtInstanceBackingStoragePointer },
        { "wrapper_callsite_scan_total_count", wrapper.WrapperCallsiteScanTotalCount },
        { "wrapper_callsite_scan_null_backing_count",
          wrapper.WrapperCallsiteScanNullBackingCount },
        { "wrapper_callsite_scan_external_actor_effect_backing_count",
          wrapper.WrapperCallsiteScanExternalActorEffectBackingCount },
        { "external_actor_effect_backing_field_offset",
          wrapper.ExternalActorEffectBackingFieldOffset },
        { "external_actor_effect_backing_function_addresses",
          wrapper.ExternalActorEffectBackingFunctionAddresses },
        { "external_actor_effect_backing_callsite_addresses",
          wrapper.ExternalActorEffectBackingCallsiteAddresses },
    };
}

nlohmann::json NativeKankyoRuntimeBindingJson(const NativeKankyoRuntimeBinding& binding) {
    return {
        { "subresource_id_start", binding.SubresourceIdStart },
        { "subresource_id_end", binding.SubresourceIdEnd },
        { "descriptor_object_offset", binding.DescriptorObjectOffset },
        { "descriptor_object_stride_bytes", binding.DescriptorObjectStrideBytes },
        { "descriptor_object_count", binding.DescriptorObjectCount },
        { "descriptor_slot", binding.DescriptorSlot },
        { "descriptor_slot_count", binding.DescriptorSlotCount },
        { "runtime_instance_offset", binding.RuntimeInstanceOffset },
        { "runtime_instance_stride_bytes", binding.RuntimeInstanceStrideBytes },
        { "runtime_helper_address", binding.RuntimeHelperAddress },
        { "runtime_callsite_address", binding.RuntimeCallsiteAddress },
        { "materialization_helper_address", binding.MaterializationHelperAddress },
        { "materialization_callsite_address", binding.MaterializationCallsiteAddress },
        { "runtime_flag_field_offset", binding.RuntimeFlagFieldOffset },
        { "runtime_flag_or_mask", binding.RuntimeFlagOrMask },
    };
}

nlohmann::json NativeKankyoRuntimeBindingSlotJson(const NativeKankyoRuntimeBindingSlot& slot) {
    return {
        { "binding_index", slot.BindingIndex },
        { "slot_index", slot.SlotIndex },
        { "subresource_id_start", slot.SubresourceIdStart },
        { "subresource_id_end", slot.SubresourceIdEnd },
        { "initial_subresource_id", slot.InitialSubresourceId },
        { "descriptor_object_offset", slot.DescriptorObjectOffset },
        { "descriptor_slot", slot.DescriptorSlot },
        { "runtime_instance_offset", slot.RuntimeInstanceOffset },
        { "runtime_helper_address", slot.RuntimeHelperAddress },
        { "runtime_callsite_address", slot.RuntimeCallsiteAddress },
        { "shares_runtime_instance_across_descriptor_slots",
          slot.SharesRuntimeInstanceAcrossDescriptorSlots },
        { "uses_dynamic_subresource_selector", slot.UsesDynamicSubresourceSelector },
        { "uses_submit_manager", slot.UsesSubmitManager },
        { "uses_render_record_scheduler", slot.UsesRenderRecordScheduler },
    };
}

nlohmann::json NativeKankyoThunderRuntimeSlotJson(const NativeKankyoThunderRuntimeSlot& slot) {
    return {
        { "slot_index", slot.SlotIndex },
        { "descriptor_object_offset", slot.DescriptorObjectOffset },
        { "runtime_instance_offset", slot.RuntimeInstanceOffset },
        { "initial_payload_object_offset", slot.InitialPayloadObjectOffset },
        { "descriptor_slot", slot.DescriptorSlot },
    };
}

nlohmann::json NativeKankyoThunderUpdateJson(const NativeKankyoThunderUpdateContract& update) {
    nlohmann::json runtimeSlots = nlohmann::json::array();
    for (const auto& slot : update.RuntimeSlots) {
        runtimeSlots.push_back(NativeKankyoThunderRuntimeSlotJson(slot));
    }

    return {
        { "function_address", update.FunctionAddress },
        { "function_end_address", update.FunctionEndAddress },
        { "slot_count", update.SlotCount },
        { "selector_modulo", update.SelectorModulo },
        { "selector_sample_callsite_address", update.SelectorSampleCallsiteAddress },
        { "payload_object_offsets", update.PayloadObjectOffsets },
        { "initial_payload_object_offset", update.InitialPayloadObjectOffset },
        { "descriptor_object_offset", update.DescriptorObjectOffset },
        { "descriptor_object_stride_bytes", update.DescriptorObjectStrideBytes },
        { "descriptor_slot", update.DescriptorSlot },
        { "descriptor_rebind_callsite_address", update.DescriptorRebindCallsiteAddress },
        { "runtime_instance_offset", update.RuntimeInstanceOffset },
        { "runtime_instance_stride_bytes", update.RuntimeInstanceStrideBytes },
        { "runtime_submit_helper_address", update.RuntimeSubmitHelperAddress },
        { "runtime_submit_callsite_address", update.RuntimeSubmitCallsiteAddress },
        { "runtime_translation_offsets", update.RuntimeTranslationOffsets },
        { "runtime_transform_matrix_offset", update.RuntimeTransformMatrixOffset },
        { "runtime_uv_transform_offset", update.RuntimeUvTransformOffset },
        { "runtime_slots", runtimeSlots },
        { "init_binds_initial_payload_to_all_slots", update.InitBindsInitialPayloadToAllSlots },
        { "update_rebinds_selected_payload_by_modulo", update.UpdateRebindsSelectedPayloadByModulo },
        { "runtime_slots_submitted_by_thunder_update", update.RuntimeSlotsSubmittedByThunderUpdate },
    };
}

nlohmann::json NativeKankyoDescriptorMaterializationJson(
    const NativeKankyoDescriptorMaterializationContract& materialization) {
    return {
        { "function_address", materialization.FunctionAddress },
        { "function_end_address", materialization.FunctionEndAddress },
        { "descriptor_record_count_offset", materialization.DescriptorRecordCountOffset },
        { "descriptor_flags_offset", materialization.DescriptorFlagsOffset },
        { "default_record_count", materialization.DefaultRecordCount },
        { "materialized_flag_field_offset", materialization.MaterializedFlagFieldOffset },
        { "buffer_set_count_field_offset", materialization.BufferSetCountFieldOffset },
        { "buffer_set_pointer_base_offset", materialization.BufferSetPointerBaseOffset },
        { "secondary_buffer_set_pointer_base_offset", materialization.SecondaryBufferSetPointerBaseOffset },
        { "base_payload_pointer_offset", materialization.BasePayloadPointerOffset },
        { "flag_0x80_payload_pointer_offset", materialization.Flag0x80PayloadPointerOffset },
        { "flag_0x10_payload_pointer_offset", materialization.Flag0x10PayloadPointerOffset },
        { "flag_0x08_payload_pointer_offset", materialization.Flag0x08PayloadPointerOffset },
        { "flag_0x40_payload_pointer_offset", materialization.Flag0x40PayloadPointerOffset },
    };
}

nlohmann::json NativeKankyoCtxbDescriptorBindingJson(const NativeKankyoCtxbDescriptorBindingContract& binding) {
    return {
        { "function_address", binding.FunctionAddress },
        { "function_end_address", binding.FunctionEndAddress },
        { "descriptor_slot_base_offset", binding.DescriptorSlotBaseOffset },
        { "descriptor_slot_stride_bytes", binding.DescriptorSlotStrideBytes },
        { "generated_descriptor_word_offsets", binding.GeneratedDescriptorWordOffsets },
        { "source_texture_header_base_offset", binding.SourceTextureHeaderBaseOffset },
        { "source_texture_parameter_halfword_offset", binding.SourceTextureParameterHalfwordOffset },
        { "source_payload_pointer_field_offset", binding.SourcePayloadPointerFieldOffset },
        { "source_width_halfword_offset", binding.SourceWidthHalfwordOffset },
        { "source_height_halfword_offset", binding.SourceHeightHalfwordOffset },
        { "source_format_halfword_offset", binding.SourceFormatHalfwordOffset },
        { "source_data_type_halfword_offset", binding.SourceDataTypeHalfwordOffset },
        { "payload_pointer_destination_offset", binding.PayloadPointerDestinationOffset },
        { "width_destination_halfword_offset", binding.WidthDestinationHalfwordOffset },
        { "height_destination_halfword_offset", binding.HeightDestinationHalfwordOffset },
        { "slot_ordinal_destination_offset", binding.SlotOrdinalDestinationOffset },
        { "packed_format_data_type_destination_offset", binding.PackedFormatDataTypeDestinationOffset },
        { "format_data_type_pack_helper_address", binding.FormatDataTypePackHelperAddress },
        { "writes_descriptor_texture_metadata", binding.WritesDescriptorTextureMetadata },
        { "writes_packet_prep_source_slots", binding.WritesPacketPrepSourceSlots },
    };
}

nlohmann::json NativeKankyoDrawCommandJson(const NativeKankyoDrawCommandContract& command) {
    return {
        { "function_address", command.FunctionAddress },
        { "function_end_address", command.FunctionEndAddress },
        { "allocate_command_helper_address", command.AllocateCommandHelperAddress },
        { "allocate_command_helper_end_address", command.AllocateCommandHelperEndAddress },
        { "draw_command_list_offset", command.DrawCommandListOffset },
        { "command_list_owner_field_offset", command.CommandListOwnerFieldOffset },
        { "command_list_count_field_offset", command.CommandListCountFieldOffset },
        { "command_record_base_offset", command.CommandRecordBaseOffset },
        { "command_record_stride_bytes", command.CommandRecordStrideBytes },
        { "command_list_max_record_count", command.CommandListMaxRecordCount },
        { "command_list_guard_exclusive_count", command.CommandListGuardExclusiveCount },
        { "command_record_list_owner_field_offset", command.CommandRecordListOwnerFieldOffset },
        { "command_record_allocator_argument_field_offset", command.CommandRecordAllocatorArgumentFieldOffset },
        { "command_type", command.CommandType },
        { "command_type_field_offset", command.CommandTypeFieldOffset },
        { "owner_context_field_offset", command.OwnerContextFieldOffset },
        { "runtime_block_field_offset", command.RuntimeBlockFieldOffset },
        { "scheduler_context_field_offset", command.SchedulerContextFieldOffset },
        { "allocation_failure_returns_null", command.AllocationFailureReturnsNull },
    };
}

nlohmann::json NativeKankyoEffectDrawConsumerJson(const NativeKankyoEffectDrawConsumerContract& consumer) {
    return {
        { "draw_function_address", consumer.DrawFunctionAddress },
        { "draw_function_end_address", consumer.DrawFunctionEndAddress },
        { "setup_function_address", consumer.SetupFunctionAddress },
        { "setup_function_end_address", consumer.SetupFunctionEndAddress },
        { "render_state_offset", consumer.RenderStateOffset },
        { "descriptor_object_pointer_offset", consumer.DescriptorObjectPointerOffset },
        { "runtime_instance_pointer_offset", consumer.RuntimeInstancePointerOffset },
        { "light_list_offset", consumer.LightListOffset },
        { "light_list_cache_valid_byte_offset", consumer.LightListCacheValidByteOffset },
        { "light_list_buffer_0_pointer_offset", consumer.LightListBuffer0PointerOffset },
        { "light_list_buffer_1_pointer_offset", consumer.LightListBuffer1PointerOffset },
        { "light_list_initial_capacity", consumer.LightListInitialCapacity },
        { "light_list_capacity_offset", consumer.LightListCapacityOffset },
        { "light_list_primary_count_offset", consumer.LightListPrimaryCountOffset },
        { "light_list_default_count_offset", consumer.LightListDefaultCountOffset },
        { "light_list_primary_record_base_offset", consumer.LightListPrimaryRecordBaseOffset },
        { "light_list_default_record_base_offset", consumer.LightListDefaultRecordBaseOffset },
        { "light_list_record_stride_bytes", consumer.LightListRecordStrideBytes },
        { "light_list_default_record_intensity_word", consumer.LightListDefaultRecordIntensityWord },
        { "light_list_buffer_0_resolver_address", consumer.LightListBuffer0ResolverAddress },
        { "light_list_buffer_1_resolver_address", consumer.LightListBuffer1ResolverAddress },
        { "light_list_active_index_offset", consumer.LightListActiveIndexOffset },
        { "light_list_buffer_0_table_offset", consumer.LightListBuffer0TableOffset },
        { "light_list_buffer_1_table_offset", consumer.LightListBuffer1TableOffset },
        { "light_list_append_default_address", consumer.LightListAppendDefaultAddress },
        { "light_list_append_record_address", consumer.LightListAppendRecordAddress },
        { "light_list_command_classify_address", consumer.LightListCommandClassifyAddress },
        { "light_list_finalize_address", consumer.LightListFinalizeAddress },
        { "descriptor_flags_offset", consumer.DescriptorFlagsOffset },
        { "descriptor_light_count_offset", consumer.DescriptorLightCountOffset },
        { "descriptor_light_record_base_offset", consumer.DescriptorLightRecordBaseOffset },
        { "descriptor_light_record_stride_bytes", consumer.DescriptorLightRecordStrideBytes },
        { "descriptor_light_texture_id_offset", consumer.DescriptorLightTextureIdOffset },
        { "runtime_matrix_offset", consumer.RuntimeMatrixOffset },
        { "runtime_primary_color_base_offset", consumer.RuntimePrimaryColorBaseOffset },
        { "runtime_secondary_color_base_offset", consumer.RuntimeSecondaryColorBaseOffset },
        { "runtime_uv_transform_base_offset", consumer.RuntimeUvTransformBaseOffset },
        { "runtime_uv_transform_stride_bytes", consumer.RuntimeUvTransformStrideBytes },
        { "runtime_draw_payload_offset", consumer.RuntimeDrawPayloadOffset },
        { "runtime_flags_offset", consumer.RuntimeFlagsOffset },
        { "texture_env_stage_count", consumer.TextureEnvStageCount },
        { "native_light_slot_count", consumer.NativeLightSlotCount },
        { "matrix_upload_mode", consumer.MatrixUploadMode },
        { "descriptor_flag_0x20_clear_draw_mode", consumer.DescriptorFlag0x20ClearDrawMode },
        { "descriptor_flag_0x20_set_draw_mode", consumer.DescriptorFlag0x20SetDrawMode },
        { "primitive_draw_packet_function_address", consumer.PrimitiveDrawPacketFunctionAddress },
        { "primitive_draw_packet_function_end_address", consumer.PrimitiveDrawPacketFunctionEndAddress },
        { "primitive_draw_attribute_mask_function_address", consumer.PrimitiveDrawAttributeMaskFunctionAddress },
        { "primitive_draw_attribute_mask_function_end_address", consumer.PrimitiveDrawAttributeMaskFunctionEndAddress },
        { "primitive_draw_mode_resolver_function_address", consumer.PrimitiveDrawModeResolverFunctionAddress },
        { "primitive_draw_command_commit_address", consumer.PrimitiveDrawCommandCommitAddress },
        { "primitive_draw_packet_word_count", consumer.PrimitiveDrawPacketWordCount },
        { "primitive_draw_render_state_index_base_offset", consumer.PrimitiveDrawRenderStateIndexBaseOffset },
        { "primitive_draw_effect_index_base_stack_value", consumer.PrimitiveDrawEffectIndexBaseStackValue },
        { "primitive_draw_index_element_type_literal", consumer.PrimitiveDrawIndexElementTypeLiteral },
        { "primitive_draw_unsigned_short_index_base_high_bit_mask",
          consumer.PrimitiveDrawUnsignedShortIndexBaseHighBitMask },
        { "primitive_draw_mode_5_resolved_primitive_value", consumer.PrimitiveDrawMode5ResolvedPrimitiveValue },
        { "primitive_draw_mode_6_resolved_primitive_value", consumer.PrimitiveDrawMode6ResolvedPrimitiveValue },
        { "primitive_draw_mode_4_resolved_primitive_value", consumer.PrimitiveDrawMode4ResolvedPrimitiveValue },
        { "primitive_draw_mode_6010_resolved_primitive_value", consumer.PrimitiveDrawMode6010ResolvedPrimitiveValue },
        { "primitive_draw_attribute_mask_header_word", consumer.PrimitiveDrawAttributeMaskHeaderWord },
        { "primitive_draw_attribute_mask_payload_or_mask", consumer.PrimitiveDrawAttributeMaskPayloadOrMask },
        { "primitive_draw_packet_literal_words", consumer.PrimitiveDrawPacketLiteralWords },
        { "primitive_draw_count_comes_from_runtime_payload", consumer.PrimitiveDrawCountComesFromRuntimePayload },
        { "primitive_draw_effect_stack_index_base_is_zero", consumer.PrimitiveDrawEffectStackIndexBaseIsZero },
        { "primitive_draw_packet_bridge_resolved", consumer.PrimitiveDrawPacketBridgeResolved },
        { "primitive_draw_attribute_mask_packet_resolved", consumer.PrimitiveDrawAttributeMaskPacketResolved },
    };
}

nlohmann::json NativePicaGeneralLightListEmitterJson(
    const NativePicaGeneralLightListEmitterContract& emitter) {
    return {
        { "function_address", emitter.FunctionAddress },
        { "function_end_address", emitter.FunctionEndAddress },
        { "init_function_address", emitter.InitFunctionAddress },
        { "append_default_address", emitter.AppendDefaultAddress },
        { "append_record_address", emitter.AppendRecordAddress },
        { "dynamic_float_vector_append_address", emitter.DynamicFloatVectorAppendAddress },
        { "command_classify_address", emitter.CommandClassifyAddress },
        { "command_param_translate_address", emitter.CommandParamTranslateAddress },
        { "finalize_address", emitter.FinalizeAddress },
        { "generic_command_writer_address", emitter.GenericCommandWriterAddress },
        { "slot_count", emitter.SlotCount },
        { "source_record_stride_bytes", emitter.SourceRecordStrideBytes },
        { "source_pointer_field_offset", emitter.SourcePointerFieldOffset },
        { "source_command_halfword_offset", emitter.SourceCommandHalfwordOffset },
        { "dynamic_float_vector_offset", emitter.DynamicFloatVectorOffset },
        { "dynamic_float_vector_component_count", emitter.DynamicFloatVectorComponentCount },
        { "dynamic_float_vector_component_stride_bytes",
          emitter.DynamicFloatVectorComponentStrideBytes },
        { "source_offset_table_byte_offset", emitter.SourceOffsetTableByteOffset },
        { "source_offset_table_word_index_base", emitter.SourceOffsetTableWordIndexBase },
        { "enable_mask_halfword_offset", emitter.EnableMaskHalfwordOffset },
        { "dynamic_mask_halfword_source_offset", emitter.DynamicMaskHalfwordSourceOffset },
        { "slot_class_table_literal_address", emitter.SlotClassTableLiteralAddress },
        { "slot_class_table_address", emitter.SlotClassTableAddress },
        { "slot_class_values", emitter.SlotClassValues },
        { "primary_upload_register", emitter.PrimaryUploadRegister },
        { "primary_upload_word_count", emitter.PrimaryUploadWordCount },
        { "secondary_upload_register", emitter.SecondaryUploadRegister },
        { "secondary_upload_word_count", emitter.SecondaryUploadWordCount },
        { "dynamic_upload_register", emitter.DynamicUploadRegister },
        { "dynamic_upload_word_count", emitter.DynamicUploadWordCount },
        { "upload_sequential_flag", emitter.UploadSequentialFlag },
        { "upload_mask", emitter.UploadMask },
        { "primary_upload_payload_offset", emitter.PrimaryUploadPayloadOffset },
        { "secondary_upload_payload_offset", emitter.SecondaryUploadPayloadOffset },
        { "dynamic_upload_payload_base_offset", emitter.DynamicUploadPayloadBaseOffset },
        { "dynamic_upload_payload_stride_bytes", emitter.DynamicUploadPayloadStrideBytes },
        { "final_state_word_0_offset", emitter.FinalStateWord0Offset },
        { "final_state_word_1_offset", emitter.FinalStateWord1Offset },
        { "output_state_word_0_offset", emitter.OutputStateWord0Offset },
        { "output_state_word_1_offset", emitter.OutputStateWord1Offset },
        { "consumes_zsi_light_settings_record_stride",
          emitter.ConsumesZsiLightSettingsRecordStride },
        { "directly_writes_runtime_packet_prep_source",
          emitter.DirectlyWritesRuntimePacketPrepSource },
    };
}

nlohmann::json NativePicaLightingRegisterEmitterJson(
    const NativePicaLightingRegisterEmitterContract& emitter) {
    return {
        { "render_context_submit_function_address", emitter.RenderContextSubmitFunctionAddress },
        { "render_context_submit_function_end_address", emitter.RenderContextSubmitFunctionEndAddress },
        { "render_context_submit_minimum_draw_payload_count",
          emitter.RenderContextSubmitMinimumDrawPayloadCount },
        { "runtime_draw_payload_count_offset", emitter.RuntimeDrawPayloadCountOffset },
        { "effect_draw_setup_function_address", emitter.EffectDrawSetupFunctionAddress },
        { "effect_draw_build_function_address", emitter.EffectDrawBuildFunctionAddress },
        { "generic_direct_writer_address", emitter.GenericDirectWriterAddress },
        { "generic_direct_writer_index_register", emitter.GenericDirectWriterIndexRegister },
        { "generic_direct_writer_data_register", emitter.GenericDirectWriterDataRegister },
        { "generic_direct_writer_command_mask", emitter.GenericDirectWriterCommandMask },
        { "generic_direct_writer_emits_vsh_float_uniform",
          emitter.GenericDirectWriterEmitsVshFloatUniform },
        { "generic_sequential_writer_address", emitter.GenericSequentialWriterAddress },
        { "primary_secondary_color_emitter_address", emitter.PrimarySecondaryColorEmitterAddress },
        { "primary_secondary_color_register", emitter.PrimarySecondaryColorRegister },
        { "primary_secondary_color_word_count", emitter.PrimarySecondaryColorWordCount },
        { "uv_transform_emitter_address", emitter.UvTransformEmitterAddress },
        { "uv_transform_primary_register", emitter.UvTransformPrimaryRegister },
        { "uv_transform_secondary_base_register", emitter.UvTransformSecondaryBaseRegister },
        { "uv_transform_primary_word_count", emitter.UvTransformPrimaryWordCount },
        { "uv_transform_secondary_word_count", emitter.UvTransformSecondaryWordCount },
        { "uv_transform_slot_stride_registers", emitter.UvTransformSlotStrideRegisters },
        { "runtime_submit_function_address", emitter.RuntimeSubmitFunctionAddress },
        { "runtime_submit_descriptor_pointer_word_index",
          emitter.RuntimeSubmitDescriptorPointerWordIndex },
        { "runtime_submit_texture_object_pointer_word_index",
          emitter.RuntimeSubmitTextureObjectPointerWordIndex },
        { "runtime_submit_light_record_table_pointer_word_index",
          emitter.RuntimeSubmitLightRecordTablePointerWordIndex },
        { "runtime_submit_color_0_byte_offset", emitter.RuntimeSubmitColor0ByteOffset },
        { "runtime_submit_color_1_byte_offset", emitter.RuntimeSubmitColor1ByteOffset },
        { "runtime_submit_color_component_count", emitter.RuntimeSubmitColorComponentCount },
        { "runtime_submit_active_light_slot_count_offset",
          emitter.RuntimeSubmitActiveLightSlotCountOffset },
        { "runtime_submit_active_light_slot_index_table_offset",
          emitter.RuntimeSubmitActiveLightSlotIndexTableOffset },
        { "runtime_submit_active_light_slot_index_stride_bytes",
          emitter.RuntimeSubmitActiveLightSlotIndexStrideBytes },
        { "runtime_submit_light_record_stride_bytes",
          emitter.RuntimeSubmitLightRecordStrideBytes },
        { "runtime_submit_light_record_color_op_0_halfword_offset",
          emitter.RuntimeSubmitLightRecordColorOp0HalfwordOffset },
        { "runtime_submit_light_record_color_op_1_halfword_offset",
          emitter.RuntimeSubmitLightRecordColorOp1HalfwordOffset },
        { "runtime_submit_light_record_disabled_color_op_value",
          emitter.RuntimeSubmitLightRecordDisabledColorOpValue },
        { "runtime_submit_light_slot_count", emitter.RuntimeSubmitLightSlotCount },
        { "runtime_record_texture_light_function_address",
          emitter.RuntimeRecordTextureLightFunctionAddress },
        { "runtime_record_texture_light_count_offset",
          emitter.RuntimeRecordTextureLightCountOffset },
        { "runtime_record_texture_light_uv_source_count_offset",
          emitter.RuntimeRecordTextureLightUvSourceCountOffset },
        { "runtime_record_texture_light_texture_ref_base_offset",
          emitter.RuntimeRecordTextureLightTextureRefBaseOffset },
        { "runtime_record_texture_light_source_record_base_offset",
          emitter.RuntimeRecordTextureLightSourceRecordBaseOffset },
        { "runtime_record_texture_light_record_stride_bytes",
          emitter.RuntimeRecordTextureLightRecordStrideBytes },
        { "runtime_record_texture_light_output_texture_id_halfword_offset",
          emitter.RuntimeRecordTextureLightOutputTextureIdHalfwordOffset },
        { "runtime_record_texture_light_output_sampler_word_base_offset",
          emitter.RuntimeRecordTextureLightOutputSamplerWordBaseOffset },
        { "runtime_record_texture_light_output_mode_word_base_offset",
          emitter.RuntimeRecordTextureLightOutputModeWordBaseOffset },
        { "runtime_record_texture_light_output_word_stride_bytes",
          emitter.RuntimeRecordTextureLightOutputWordStrideBytes },
        { "runtime_record_texture_light_first_slot_override_owner_offset",
          emitter.RuntimeRecordTextureLightFirstSlotOverrideOwnerOffset },
        { "runtime_record_texture_light_first_slot_override_gate_byte_offset",
          emitter.RuntimeRecordTextureLightFirstSlotOverrideGateByteOffset },
        { "runtime_record_texture_light_first_slot_override_source_pointer_offset",
          emitter.RuntimeRecordTextureLightFirstSlotOverrideSourcePointerOffset },
        { "runtime_record_texture_light_first_slot_override_mode_value",
          emitter.RuntimeRecordTextureLightFirstSlotOverrideModeValue },
        { "runtime_uv_transform_build_function_address",
          emitter.RuntimeUvTransformBuildFunctionAddress },
        { "runtime_uv_transform_source_builder_address",
          emitter.RuntimeUvTransformSourceBuilderAddress },
        { "runtime_uv_transform_copy_helper_address",
          emitter.RuntimeUvTransformCopyHelperAddress },
        { "runtime_uv_transform_primary_upload_helper_address",
          emitter.RuntimeUvTransformPrimaryUploadHelperAddress },
        { "runtime_uv_transform_secondary_upload_helper_address",
          emitter.RuntimeUvTransformSecondaryUploadHelperAddress },
        { "runtime_uv_transform_source_count_offset", emitter.RuntimeUvTransformSourceCountOffset },
        { "runtime_uv_transform_source_record_base_offset",
          emitter.RuntimeUvTransformSourceRecordBaseOffset },
        { "runtime_uv_transform_source_record_stride_bytes",
          emitter.RuntimeUvTransformSourceRecordStrideBytes },
        { "runtime_uv_transform_output_slot_count", emitter.RuntimeUvTransformOutputSlotCount },
        { "runtime_uv_transform_output_slot_stride_bytes",
          emitter.RuntimeUvTransformOutputSlotStrideBytes },
        { "runtime_uv_transform_output_word_count", emitter.RuntimeUvTransformOutputWordCount },
        { "runtime_uv_transform_primary_upload_register",
          emitter.RuntimeUvTransformPrimaryUploadRegister },
        { "runtime_uv_transform_primary_upload_word_count",
          emitter.RuntimeUvTransformPrimaryUploadWordCount },
        { "runtime_uv_transform_secondary_upload_register_base",
          emitter.RuntimeUvTransformSecondaryUploadRegisterBase },
        { "runtime_uv_transform_secondary_upload_word_count",
          emitter.RuntimeUvTransformSecondaryUploadWordCount },
        { "runtime_uv_transform_first_slot_override_owner_offset",
          emitter.RuntimeUvTransformFirstSlotOverrideOwnerOffset },
        { "runtime_uv_transform_first_slot_override_gate_byte_offset",
          emitter.RuntimeUvTransformFirstSlotOverrideGateByteOffset },
        { "runtime_uv_transform_first_slot_override_source_pointer_offset",
          emitter.RuntimeUvTransformFirstSlotOverrideSourcePointerOffset },
        { "runtime_uv_transform_first_slot_override_payload_offset",
          emitter.RuntimeUvTransformFirstSlotOverridePayloadOffset },
        { "runtime_uv_transform_uploads_native_pica_vsh_uniforms",
          emitter.RuntimeUvTransformUploadsNativePicaVshUniforms },
        { "runtime_uv_transform_directly_writes_packet_prep_source",
          emitter.RuntimeUvTransformDirectlyWritesPacketPrepSource },
        { "light_slot_emitter_address", emitter.LightSlotEmitterAddress },
        { "light_slot_default_emitter_address", emitter.LightSlotDefaultEmitterAddress },
        { "light_slot_register_base_table_address", emitter.LightSlotRegisterBaseTableAddress },
        { "light_slot_register_bases", emitter.LightSlotRegisterBases },
        { "light_slot_active_main_word_count", emitter.LightSlotActiveMainWordCount },
        { "light_slot_active_tail_register_offset", emitter.LightSlotActiveTailRegisterOffset },
        { "light_slot_active_tail_word_count", emitter.LightSlotActiveTailWordCount },
        { "light_slot_default_payload_table_address", emitter.LightSlotDefaultPayloadTableAddress },
        { "light_slot_default_register_base_table_address",
          emitter.LightSlotDefaultRegisterBaseTableAddress },
        { "light_slot_default_word_count", emitter.LightSlotDefaultWordCount },
        { "light_slot_color_emitter_address", emitter.LightSlotColorEmitterAddress },
        { "light_slot_color_register_table_address", emitter.LightSlotColorRegisterTableAddress },
        { "light_slot_color_registers", emitter.LightSlotColorRegisters },
        { "light_slot_color_float_to_byte_scale_word", emitter.LightSlotColorFloatToByteScaleWord },
        { "light_settings_emitter_address", emitter.LightSettingsEmitterAddress },
        { "light_settings_register_base_table_address", emitter.LightSettingsRegisterBaseTableAddress },
        { "light_settings_upload_registers", emitter.LightSettingsUploadRegisters },
        { "light_settings_primary_header", emitter.LightSettingsPrimaryHeader },
        { "light_settings_terminator_header", emitter.LightSettingsTerminatorHeader },
        { "color_operation_emitter_address", emitter.ColorOperationEmitterAddress },
        { "color_operation_first_header", emitter.ColorOperationFirstHeader },
        { "color_operation_second_header", emitter.ColorOperationSecondHeader },
        { "lighting_lut_input_emitter_address", emitter.LightingLutInputEmitterAddress },
        { "lighting_lut_input_register", emitter.LightingLutInputRegister },
        { "lighting_lut_input_vsh_uniform_index", emitter.LightingLutInputVshUniformIndex },
        { "lighting_lut_input_word_count", emitter.LightingLutInputWordCount },
        { "lighting_lut_config_emitter_address", emitter.LightingLutConfigEmitterAddress },
        { "lighting_lut_config_register", emitter.LightingLutConfigRegister },
        { "lighting_lut_config_vsh_uniform_index", emitter.LightingLutConfigVshUniformIndex },
        { "lighting_lut_config_word_count", emitter.LightingLutConfigWordCount },
        { "lighting_lut_config_default_table_address", emitter.LightingLutConfigDefaultTableAddress },
        { "lighting_lut_config_default_word", emitter.LightingLutConfigDefaultWord },
        { "lighting_enable_emitter_address", emitter.LightingEnableEmitterAddress },
        { "lighting_enable_register", emitter.LightingEnableRegister },
        { "lighting_enable_vsh_uniform_index", emitter.LightingEnableVshUniformIndex },
        { "lighting_enable_word_count", emitter.LightingEnableWordCount },
        { "fragment_lighting_config_emitter_address", emitter.FragmentLightingConfigEmitterAddress },
        { "fragment_lighting_config_register", emitter.FragmentLightingConfigRegister },
        { "fragment_lighting_config_header", emitter.FragmentLightingConfigHeader },
        { "fragment_lighting_config_payload_word_count",
          emitter.FragmentLightingConfigPayloadWordCount },
        { "fragment_lighting_config_payload_registers",
          emitter.FragmentLightingConfigPayloadRegisters },
        { "fragment_lighting_config_render_setup_callsite_address",
          emitter.FragmentLightingConfigRenderSetupCallsiteAddress },
        { "fragment_lighting_config_material_setup_callsite_address",
          emitter.FragmentLightingConfigMaterialSetupCallsiteAddress },
        { "fragment_lighting_config_state_type_offset",
          emitter.FragmentLightingConfigStateTypeOffset },
        { "fragment_lighting_config_flags_offset",
          emitter.FragmentLightingConfigFlagsOffset },
        { "fragment_lighting_config_primary_enable_offset",
          emitter.FragmentLightingConfigPrimaryEnableOffset },
        { "fragment_lighting_config_primary_mode_offset",
          emitter.FragmentLightingConfigPrimaryModeOffset },
        { "fragment_lighting_config_secondary_enable_offset",
          emitter.FragmentLightingConfigSecondaryEnableOffset },
        { "fragment_lighting_config_secondary_mode_offset",
          emitter.FragmentLightingConfigSecondaryModeOffset },
        { "fragment_lighting_config_native_default_type",
          emitter.FragmentLightingConfigNativeDefaultType },
        { "fragment_lighting_config_native_alternate_type",
          emitter.FragmentLightingConfigNativeAlternateType },
        { "fragment_lighting_config_payload0_fallback_mask",
          emitter.FragmentLightingConfigPayload0FallbackMask },
        { "fragment_lighting_config_payload1_fallback_mask",
          emitter.FragmentLightingConfigPayload1FallbackMask },
        { "fragment_lighting_config_payload2_enable_bit",
          emitter.FragmentLightingConfigPayload2EnableBit },
        { "fragment_lighting_config_payload3_enable_bit",
          emitter.FragmentLightingConfigPayload3EnableBit },
        { "fragment_lighting_config_type_setter_address",
          emitter.FragmentLightingConfigTypeSetterAddress },
        { "fragment_lighting_config_primary_state_setter_address",
          emitter.FragmentLightingConfigPrimaryStateSetterAddress },
        { "fragment_lighting_config_secondary_disabled_state_setter_address",
          emitter.FragmentLightingConfigSecondaryDisabledStateSetterAddress },
        { "fragment_lighting_config_secondary_enabled_state_setter_address",
          emitter.FragmentLightingConfigSecondaryEnabledStateSetterAddress },
        { "fragment_lighting_config_aux_scalar_state_setter_address",
          emitter.FragmentLightingConfigAuxScalarStateSetterAddress },
        { "fragment_lighting_config_material_scalar_uploader_address",
          emitter.FragmentLightingConfigMaterialScalarUploaderAddress },
        { "fragment_lighting_config_material_setup_address",
          emitter.FragmentLightingConfigMaterialSetupAddress },
        { "fragment_lighting_config_material_prepared_state_offset",
          emitter.FragmentLightingConfigMaterialPreparedStateOffset },
        { "fragment_lighting_config_material_primary_source_runtime_lane_offset",
          emitter.FragmentLightingConfigMaterialPrimarySourceRuntimeLaneOffset },
        { "fragment_lighting_config_material_primary_default_table_address",
          emitter.FragmentLightingConfigMaterialPrimaryDefaultTableAddress },
        { "fragment_lighting_config_material_primary_override_gate_offset",
          emitter.FragmentLightingConfigMaterialPrimaryOverrideGateOffset },
        { "fragment_lighting_config_material_primary_cmb_blend_gate_offset",
          emitter.FragmentLightingConfigMaterialPrimaryCmbBlendGateOffset },
        { "fragment_lighting_config_material_secondary_type_selector_cmb_offset",
          emitter.FragmentLightingConfigMaterialSecondaryTypeSelectorCmbOffset },
        { "fragment_lighting_config_material_secondary_type_disabled_value",
          emitter.FragmentLightingConfigMaterialSecondaryTypeDisabledValue },
        { "fragment_lighting_config_material_secondary_type_register_values",
          emitter.FragmentLightingConfigMaterialSecondaryTypeRegisterValues },
        { "fragment_lighting_config_material_secondary_enable_cmb_offset",
          emitter.FragmentLightingConfigMaterialSecondaryEnableCmbOffset },
        { "fragment_lighting_config_material_secondary_mode_cmb_offset",
          emitter.FragmentLightingConfigMaterialSecondaryModeCmbOffset },
        { "fragment_lighting_config_material_secondary_param_cmb_offset",
          emitter.FragmentLightingConfigMaterialSecondaryParamCmbOffset },
        { "fragment_lighting_config_material_secondary_override_mode_offset",
          emitter.FragmentLightingConfigMaterialSecondaryOverrideModeOffset },
        { "fragment_lighting_config_material_aux_byte_cmb_offset",
          emitter.FragmentLightingConfigMaterialAuxByteCmbOffset },
        { "fragment_lighting_config_material_aux_halfword_cmb_offset",
          emitter.FragmentLightingConfigMaterialAuxHalfwordCmbOffset },
        { "fragment_lighting_config_runtime_state_initializer_address",
          emitter.FragmentLightingConfigRuntimeStateInitializerAddress },
        { "fragment_lighting_config_runtime_state_copy_helper_address",
          emitter.FragmentLightingConfigRuntimeStateCopyHelperAddress },
        { "fragment_lighting_config_runtime_state_override_gate_offset",
          emitter.FragmentLightingConfigRuntimeStateOverrideGateOffset },
        { "fragment_lighting_config_runtime_state_override_gate_default_value",
          emitter.FragmentLightingConfigRuntimeStateOverrideGateDefaultValue },
        { "fragment_lighting_config_runtime_state_secondary_mode_offset",
          emitter.FragmentLightingConfigRuntimeStateSecondaryModeOffset },
        { "fragment_lighting_config_runtime_state_secondary_mode_default_value",
          emitter.FragmentLightingConfigRuntimeStateSecondaryModeDefaultValue },
        { "fragment_lighting_config_runtime_state_override_gate_copied_by_copy_helper",
          emitter.FragmentLightingConfigRuntimeStateOverrideGateCopiedByCopyHelper },
        { "fragment_lighting_config_runtime_state_secondary_mode_copied_by_copy_helper",
          emitter.FragmentLightingConfigRuntimeStateSecondaryModeCopiedByCopyHelper },
        { "fragment_lighting_config_runtime_state_defaults_resolved_from_codebin",
          emitter.FragmentLightingConfigRuntimeStateDefaultsResolvedFromCodebin },
        { "fragment_lighting_config_runtime_state_copy_helper_resolved_from_codebin",
          emitter.FragmentLightingConfigRuntimeStateCopyHelperResolvedFromCodebin },
        { "fragment_lighting_config_runtime_state_override_writer_address_count",
          emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddressCount },
        { "fragment_lighting_config_runtime_state_override_writer_addresses",
          emitter.FragmentLightingConfigRuntimeStateOverrideWriterAddresses },
        { "fragment_lighting_config_runtime_state_secondary_mode_writer_address_count",
          emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddressCount },
        { "fragment_lighting_config_runtime_state_secondary_mode_writer_addresses",
          emitter.FragmentLightingConfigRuntimeStateSecondaryModeWriterAddresses },
        { "fragment_lighting_config_runtime_state_writer_scan_resolved_from_codebin",
          emitter.FragmentLightingConfigRuntimeStateWriterScanResolvedFromCodebin },
        { "fragment_lighting_config_runtime_state_writers_classified_as_draw_local",
          emitter.FragmentLightingConfigRuntimeStateWritersClassifiedAsDrawLocal },
        { "fragment_lighting_config_runtime_state_active_producer_resolved_from_writer_scan",
          emitter.FragmentLightingConfigRuntimeStateActiveProducerResolvedFromWriterScan },
        { "fragment_lighting_config_gameplay_draw_address",
          emitter.FragmentLightingConfigGameplayDrawAddress },
        { "fragment_lighting_config_gameplay_draw_dispatcher_address",
          emitter.FragmentLightingConfigGameplayDrawDispatcherAddress },
        { "fragment_lighting_config_gameplay_draw_dispatcher_callsite_address",
          emitter.FragmentLightingConfigGameplayDrawDispatcherCallsiteAddress },
        { "fragment_lighting_config_draw_entry_submit_address",
          emitter.FragmentLightingConfigDrawEntrySubmitAddress },
        { "fragment_lighting_config_draw_entry_submit_callsite_count",
          emitter.FragmentLightingConfigDrawEntrySubmitCallsiteCount },
        { "fragment_lighting_config_draw_entry_submit_callsite_addresses",
          emitter.FragmentLightingConfigDrawEntrySubmitCallsiteAddresses },
        { "fragment_lighting_config_draw_entry_flags_offset",
          emitter.FragmentLightingConfigDrawEntryFlagsOffset },
        { "fragment_lighting_config_draw_entry_render_context_pointer_offset",
          emitter.FragmentLightingConfigDrawEntryRenderContextPointerOffset },
        { "fragment_lighting_config_draw_entry_callback_offset",
          emitter.FragmentLightingConfigDrawEntryCallbackOffset },
        { "fragment_lighting_config_draw_entry_visibility_state_offset",
          emitter.FragmentLightingConfigDrawEntryVisibilityStateOffset },
        { "fragment_lighting_config_draw_entry_submitted_byte_offset",
          emitter.FragmentLightingConfigDrawEntrySubmittedByteOffset },
        { "fragment_lighting_config_draw_entry_fade_counter_offset",
          emitter.FragmentLightingConfigDrawEntryFadeCounterOffset },
        { "fragment_lighting_config_draw_entry_fade_limit_offset",
          emitter.FragmentLightingConfigDrawEntryFadeLimitOffset },
        { "fragment_lighting_config_draw_entry_override_gate_flag_mask",
          emitter.FragmentLightingConfigDrawEntryOverrideGateFlagMask },
        { "fragment_lighting_config_draw_entry_override_gate_force_full_flag_mask",
          emitter.FragmentLightingConfigDrawEntryOverrideGateForceFullFlagMask },
        { "fragment_lighting_config_draw_entry_submit_route_resolved_from_codebin",
          emitter.FragmentLightingConfigDrawEntrySubmitRouteResolvedFromCodebin },
        { "fragment_lighting_config_draw_entry_override_gate_rule_resolved_from_codebin",
          emitter.FragmentLightingConfigDrawEntryOverrideGateRuleResolvedFromCodebin },
        { "fragment_lighting_config_draw_entry_route_promotes_active_material_override",
          emitter.FragmentLightingConfigDrawEntryRoutePromotesActiveMaterialOverride },
        { "fragment_lighting_config_submit_manager_vtable_address",
          emitter.FragmentLightingConfigSubmitManagerVtableAddress },
        { "fragment_lighting_config_submit_manager_material_config_slot_offset",
          emitter.FragmentLightingConfigSubmitManagerMaterialConfigSlotOffset },
        { "fragment_lighting_config_submit_manager_material_config_address",
          emitter.FragmentLightingConfigSubmitManagerMaterialConfigAddress },
        { "fragment_lighting_config_submit_manager_material_state_slot_offset",
          emitter.FragmentLightingConfigSubmitManagerMaterialStateSlotOffset },
        { "fragment_lighting_config_submit_manager_material_state_address",
          emitter.FragmentLightingConfigSubmitManagerMaterialStateAddress },
        { "fragment_lighting_config_submit_manager_material_route_resolved_from_codebin",
          emitter.FragmentLightingConfigSubmitManagerMaterialRouteResolvedFromCodebin },
        { "fragment_lighting_config_submit_manager_material_route_resolves_active_override_gate",
          emitter.FragmentLightingConfigSubmitManagerMaterialRouteResolvesActiveOverrideGate },
        { "fragment_lighting_config_render_context_submit_address",
          emitter.FragmentLightingConfigRenderContextSubmitAddress },
        { "fragment_lighting_config_render_context_prepared_state_offset",
          emitter.FragmentLightingConfigRenderContextPreparedStateOffset },
        { "fragment_lighting_config_render_context_aux_float_offset",
          emitter.FragmentLightingConfigRenderContextAuxFloatOffset },
        { "fragment_lighting_config_render_context_aux_zero_word",
          emitter.FragmentLightingConfigRenderContextAuxZeroWord },
        { "fragment_lighting_config_render_context_native_type",
          emitter.FragmentLightingConfigRenderContextNativeType },
        { "fragment_lighting_config_prepared_state_initializer_address",
          emitter.FragmentLightingConfigPreparedStateInitializerAddress },
        { "fragment_lighting_config_prepared_state_base_reset_address",
          emitter.FragmentLightingConfigPreparedStateBaseResetAddress },
        { "fragment_lighting_config_prepared_state_initializer_native_type_literal_address",
          emitter.FragmentLightingConfigPreparedStateInitializerNativeTypeLiteralAddress },
        { "fragment_lighting_config_prepared_state_initializer_native_type",
          emitter.FragmentLightingConfigPreparedStateInitializerNativeType },
        { "fragment_lighting_config_prepared_state_initializer_default_flags",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultFlags },
        { "fragment_lighting_config_prepared_state_initializer_default_primary_enable",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultPrimaryEnable },
        { "fragment_lighting_config_prepared_state_initializer_default_primary_mode",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultPrimaryMode },
        { "fragment_lighting_config_prepared_state_initializer_default_secondary_enable",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultSecondaryEnable },
        { "fragment_lighting_config_prepared_state_initializer_default_secondary_mode",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultSecondaryMode },
        { "fragment_lighting_config_prepared_state_initializer_default_aux_byte",
          emitter.FragmentLightingConfigPreparedStateInitializerDefaultAuxByte },
        { "fragment_lighting_config_payload_logic_resolved_from_codebin",
          emitter.FragmentLightingConfigPayloadLogicResolvedFromCodebin },
        { "fragment_lighting_config_prepared_state_setters_resolved_from_codebin",
          emitter.FragmentLightingConfigPreparedStateSettersResolvedFromCodebin },
        { "fragment_lighting_config_material_setup_source_resolved_from_codebin",
          emitter.FragmentLightingConfigMaterialSetupSourceResolvedFromCodebin },
        { "fragment_lighting_config_render_context_source_resolved_from_codebin",
          emitter.FragmentLightingConfigRenderContextSourceResolvedFromCodebin },
        { "fragment_lighting_config_prepared_state_initializer_resolved_from_codebin",
          emitter.FragmentLightingConfigPreparedStateInitializerResolvedFromCodebin },
        { "fragment_lighting_config_prepared_flags_origin_resolved",
          emitter.FragmentLightingConfigPreparedFlagsOriginResolved },
        { "fragment_lighting_config_prepared_state_origin_resolved",
          emitter.FragmentLightingConfigPreparedStateOriginResolved },
        { "alpha_test_emitter_address", emitter.AlphaTestEmitterAddress },
        { "alpha_test_register", emitter.AlphaTestRegister },
        { "alpha_test_header", emitter.AlphaTestHeader },
        { "emits_runtime_pica_lighting_registers", emitter.EmitsRuntimePicaLightingRegisters },
        { "uses_native_zsi_light_settings_records", emitter.UsesNativeZsiLightSettingsRecords },
        { "fragop_shadow_register_resolved_in_lighting_emitter_path",
          emitter.FragopShadowRegisterResolvedInLightingEmitterPath },
    };
}

nlohmann::json NativeKankyoPacketPrepJson(const NativeKankyoPacketPrepContract& prep) {
    return {
        { "function_address", prep.FunctionAddress },
        { "function_end_address", prep.FunctionEndAddress },
        { "render_context_consumer_address", prep.RenderContextConsumerAddress },
        { "render_context_consumer_callsite_address", prep.RenderContextConsumerCallsiteAddress },
        { "render_context_packet_buffer_offset", prep.RenderContextPacketBufferOffset },
        { "render_context_transform_offset", prep.RenderContextTransformOffset },
        { "draw_handle_consumer_address", prep.DrawHandleConsumerAddress },
        { "draw_handle_consumer_callsite_address", prep.DrawHandleConsumerCallsiteAddress },
        { "draw_handle_packet_buffer_offset", prep.DrawHandlePacketBufferOffset },
        { "enabled_upload_mode_argument", prep.EnabledUploadModeArgument },
        { "enabled_upload_path_bypasses_fragop_shadow_live_setter",
          prep.EnabledUploadPathBypassesFragopShadowLiveSetter },
        { "runtime_source_vector_committed_block_is_direct_input",
          prep.RuntimeSourceVectorCommittedBlockIsDirectInput },
        { "slot_count", prep.SlotCount },
        { "slot_stride_bytes", prep.SlotStrideBytes },
        { "source_payload_0_offset", prep.SourcePayload0Offset },
        { "source_payload_1_offset", prep.SourcePayload1Offset },
        { "source_vector_offsets", prep.SourceVectorOffsets },
        { "enable_intensity_offset", prep.EnableIntensityOffset },
        { "prepared_vector_offsets", prep.PreparedVectorOffsets },
        { "prepared_intensity_offset", prep.PreparedIntensityOffset },
        { "enabled_float_word", prep.EnabledFloatWord },
        { "disabled_fallback_z_word", prep.DisabledFallbackZWord },
        { "enabled_upload_helper_address", prep.EnabledUploadHelperAddress },
        { "enabled_upload_register_base_table_address", prep.EnabledUploadRegisterBaseTableAddress },
        { "enabled_upload_registers", prep.EnabledUploadRegisters },
        { "enabled_upload_paired_register_offset", prep.EnabledUploadPairedRegisterOffset },
        { "enabled_upload_word_count", prep.EnabledUploadWordCount },
        { "vector_upload_helper_address", prep.VectorUploadHelperAddress },
        { "vector_upload_register_table_address", prep.VectorUploadRegisterTableAddress },
        { "vector_upload_registers", prep.VectorUploadRegisters },
        { "vector_upload_word_count", prep.VectorUploadWordCount },
        { "prep_view_matrix_accessor_address", prep.PrepViewMatrixAccessorAddress },
        { "prep_view_matrix_pointer_literal_address", prep.PrepViewMatrixPointerLiteralAddress },
        { "prep_view_matrix_runtime_address", prep.PrepViewMatrixRuntimeAddress },
        { "prep_static_matrix_accessor_address", prep.PrepStaticMatrixAccessorAddress },
        { "prep_static_matrix_pointer_literal_address", prep.PrepStaticMatrixPointerLiteralAddress },
        { "prep_static_matrix_runtime_address", prep.PrepStaticMatrixRuntimeAddress },
        { "upload_helpers_use_native_pica_register_maps", prep.UploadHelpersUseNativePicaRegisterMaps },
    };
}

nlohmann::json NativeKankyoRuntimeSourceVectorJson(const NativeKankyoRuntimeSourceVectorContract& source) {
    return {
        { "static_update_address", source.StaticUpdateAddress },
        { "dynamic_submit_callback_address", source.DynamicSubmitCallbackAddress },
        { "runtime_flags_offset", source.RuntimeFlagsOffset },
        { "dynamic_transform_update_skip_flag_mask", source.DynamicTransformUpdateSkipFlagMask },
        { "static_update_skip_flag_mask", source.StaticUpdateSkipFlagMask },
        { "committed_copy_skip_flag_mask", source.CommittedCopySkipFlagMask },
        { "descriptor_pointer_offset", source.DescriptorPointerOffset },
        { "descriptor_type_offset", source.DescriptorTypeOffset },
        { "descriptor_type_direct_matrix_value", source.DescriptorTypeDirectMatrixValue },
        { "descriptor_type_derived_matrix_value", source.DescriptorTypeDerivedMatrixValue },
        { "base_vector_x_offset", source.BaseVectorXOffset },
        { "base_vector_y_offset", source.BaseVectorYOffset },
        { "base_vector_z_offset", source.BaseVectorZOffset },
        { "static_transform_block_offset", source.StaticTransformBlockOffset },
        { "dynamic_transform_block_offset", source.DynamicTransformBlockOffset },
        { "parent_transform_block_offset", source.ParentTransformBlockOffset },
        { "working_block_offset", source.WorkingBlockOffset },
        { "working_vector_seed_offsets", source.WorkingVectorSeedOffsets },
        { "working_packet_source_vector_offsets", source.WorkingPacketSourceVectorOffsets },
        { "working_block_word_count", source.WorkingBlockWordCount },
        { "committed_block_offset", source.CommittedBlockOffset },
        { "committed_block_word_count", source.CommittedBlockWordCount },
        { "matrix_compose_helper_address", source.MatrixComposeHelperAddress },
        { "matrix_apply_helper_address", source.MatrixApplyHelperAddress },
        { "vector_prep_helper_address", source.VectorPrepHelperAddress },
        { "prepared_intensity_source_offset", source.PreparedIntensitySourceOffset },
        { "prepared_intensity_destination_offset", source.PreparedIntensityDestinationOffset },
    };
}

nlohmann::json NativeKankyoRuntimeLightPacketPackJson(const NativeKankyoRuntimeLightPacketPackContract& pack) {
    return {
        { "function_address", pack.FunctionAddress },
        { "fallback_function_address", pack.FallbackFunctionAddress },
        { "packet_buffer_pointer_offset", pack.PacketBufferPointerOffset },
        { "runtime_descriptor_pointer_offset", pack.RuntimeDescriptorPointerOffset },
        { "descriptor_enabled_byte_offset", pack.DescriptorEnabledByteOffset },
        { "slot_count", pack.SlotCount },
        { "source_slot_stride_bytes", pack.SourceSlotStrideBytes },
        { "source_prepared_vector_base_offset", pack.SourcePreparedVectorBaseOffset },
        { "source_prepared_intensity_offset", pack.SourcePreparedIntensityOffset },
        { "required_prepared_intensity_word", pack.RequiredPreparedIntensityWord },
        { "source_color_payload_group_count", pack.SourceColorPayloadGroupCount },
        { "source_color_payload_component_count", pack.SourceColorPayloadComponentCount },
        { "source_color_payload_stride_bytes", pack.SourceColorPayloadStrideBytes },
        { "source_color_payload_offsets", pack.SourceColorPayloadOffsets },
        { "descriptor_primary_color_scale_offsets", pack.DescriptorPrimaryColorScaleOffsets },
        { "descriptor_payload_1_scale_offsets", pack.DescriptorPayload1ScaleOffsets },
        { "descriptor_payload_2_scale_offsets", pack.DescriptorPayload2ScaleOffsets },
        { "descriptor_payload_3_scale_offsets", pack.DescriptorPayload3ScaleOffsets },
        { "descriptor_byte_to_float_scale_word", pack.DescriptorByteToFloatScaleWord },
        { "color_float_to_byte_scale_word", pack.ColorFloatToByteScaleWord },
        { "color_round_bias_word", pack.ColorRoundBiasWord },
        { "clamp_min_word", pack.ClampMinWord },
        { "clamp_max_word", pack.ClampMaxWord },
        { "runtime_output_base_offset", pack.RuntimeOutputBaseOffset },
        { "runtime_output_record_stride_bytes", pack.RuntimeOutputRecordStrideBytes },
        { "runtime_output_color_byte_offset", pack.RuntimeOutputColorByteOffset },
        { "runtime_output_color_byte_count", pack.RuntimeOutputColorByteCount },
        { "runtime_output_direction_packed_word_0_offset", pack.RuntimeOutputDirectionPackedWord0Offset },
        { "runtime_output_direction_packed_word_1_offset", pack.RuntimeOutputDirectionPackedWord1Offset },
        { "runtime_output_enabled_byte_offset", pack.RuntimeOutputEnabledByteOffset },
        { "runtime_output_final_record_color_byte_offsets",
          pack.RuntimeOutputFinalRecordColorByteOffsets },
        { "runtime_output_source_payload_float_offsets", pack.RuntimeOutputSourcePayloadFloatOffsets },
        { "runtime_output_final_record_direction_packed_word_offsets",
          pack.RuntimeOutputFinalRecordDirectionPackedWordOffsets },
        { "runtime_output_final_record_flag_byte_offset",
          pack.RuntimeOutputFinalRecordFlagByteOffset },
        { "runtime_output_record_layout_resolved", pack.RuntimeOutputRecordLayoutResolved },
        { "runtime_output_feeds_final_upload_emitter",
          pack.RuntimeOutputFeedsFinalUploadEmitter },
        { "runtime_light_enable_flags_offset", pack.RuntimeLightEnableFlagsOffset },
        { "runtime_payload_3_scale_gate_byte_offset", pack.RuntimePayload3ScaleGateByteOffset },
        { "runtime_payload_3_scale_gate_source_local_offset",
          pack.RuntimePayload3ScaleGateSourceLocalOffset },
        { "runtime_payload_3_scale_gate_source_material_offset",
          pack.RuntimePayload3ScaleGateSourceMaterialOffset },
        { "runtime_payload_3_scale_gate_consumer_address",
          pack.RuntimePayload3ScaleGateConsumerAddress },
        { "runtime_payload_3_scale_gate_final_upload_address",
          pack.RuntimePayload3ScaleGateFinalUploadAddress },
        { "runtime_payload_3_scale_gate_semantic_resolved",
          pack.RuntimePayload3ScaleGateSemanticResolved },
        { "runtime_payload_3_scale_gate_semantic", pack.RuntimePayload3ScaleGateSemantic },
        { "dynamic_color_gate_context_offset", pack.DynamicColorGateContextOffset },
        { "dynamic_color_gate_table_offset", pack.DynamicColorGateTableOffset },
        { "dynamic_color_gate_stride_bytes", pack.DynamicColorGateStrideBytes },
        { "dynamic_color_override_helper_address", pack.DynamicColorOverrideHelperAddress },
        { "final_upload_helper_address", pack.FinalUploadHelperAddress },
        { "fallback_final_upload_helper_address", pack.FallbackFinalUploadHelperAddress },
        { "final_upload_slot_loop_address", pack.FinalUploadSlotLoopAddress },
        { "final_upload_slot_packet_emitter_address", pack.FinalUploadSlotPacketEmitterAddress },
        { "final_upload_slot_loop_count", pack.FinalUploadSlotLoopCount },
        { "final_upload_slot_enable_byte_base_offset", pack.FinalUploadSlotEnableByteBaseOffset },
        { "final_upload_source_record_base_offset", pack.FinalUploadSourceRecordBaseOffset },
        { "final_upload_source_record_stride_bytes", pack.FinalUploadSourceRecordStrideBytes },
        { "final_upload_packet_word_count", pack.FinalUploadPacketWordCount },
        { "final_upload_packet_header_word_index", pack.FinalUploadPacketHeaderWordIndex },
        { "final_upload_packet_payload_first_word_index", pack.FinalUploadPacketPayloadFirstWordIndex },
        { "final_upload_packet_register_base", pack.FinalUploadPacketRegisterBase },
        { "final_upload_packet_register_stride", pack.FinalUploadPacketRegisterStride },
        { "final_upload_packet_header_mask", pack.FinalUploadPacketHeaderMask },
        { "final_upload_record_slot_index_byte_offset", pack.FinalUploadRecordSlotIndexByteOffset },
        { "final_upload_packed_rgb_packet_word_indices", pack.FinalUploadPackedRgbPacketWordIndices },
        { "final_upload_packed_rgb_byte_offsets", pack.FinalUploadPackedRgbByteOffsets },
        { "final_upload_packed_rgb_bit_shifts", pack.FinalUploadPackedRgbBitShifts },
        { "final_upload_copied_word_source_offsets", pack.FinalUploadCopiedWordSourceOffsets },
        { "final_upload_copied_word_packet_word_indices", pack.FinalUploadCopiedWordPacketWordIndices },
        { "final_upload_flag_packet_word_index", pack.FinalUploadFlagPacketWordIndex },
        { "final_upload_flag_base_byte_offset", pack.FinalUploadFlagBaseByteOffset },
        { "final_upload_flag_boolean_byte_offsets", pack.FinalUploadFlagBooleanByteOffsets },
        { "final_upload_flag_boolean_bit_shifts", pack.FinalUploadFlagBooleanBitShifts },
        { "final_upload_zero_packet_word_indices", pack.FinalUploadZeroPacketWordIndices },
        { "final_upload_record_packet_emitter_resolved", pack.FinalUploadRecordPacketEmitterResolved },
    };
}

nlohmann::json NativeKankyoDefaultRecordTableJson(const NativeKankyoDefaultRecordTableContract& table) {
    return {
        { "role", table.Role },
        { "source_address", table.SourceAddress },
        { "source_size_bytes", table.SourceSizeBytes },
        { "stack_offset", table.StackOffset },
        { "record_pointer_patch_offset", table.RecordPointerPatchOffset },
        { "entry_count", table.EntryCount },
        { "entry_stride_bytes", table.EntryStrideBytes },
    };
}

nlohmann::json NativeKankyoPacketDefaultRecordJson(const NativeKankyoPacketDefaultRecordContract& record) {
    nlohmann::json tables = nlohmann::json::array();
    for (const auto& table : record.Tables) {
        tables.push_back(NativeKankyoDefaultRecordTableJson(table));
    }

    return {
        { "owner_function_address", record.OwnerFunctionAddress },
        { "assembler_address", record.AssemblerAddress },
        { "assembler_end_address", record.AssemblerEndAddress },
        { "static_template_address", record.StaticTemplateAddress },
        { "static_template_copy_size_bytes", record.StaticTemplateCopySizeBytes },
        { "template_block_copy_helper_address", record.TemplateBlockCopyHelperAddress },
        { "root_record_stack_offset", record.RootRecordStackOffset },
        { "runtime_copy_helper_address", record.RuntimeCopyHelperAddress },
        { "first_runtime_copy_callsite_address", record.FirstRuntimeCopyCallsiteAddress },
        { "second_runtime_copy_callsite_address", record.SecondRuntimeCopyCallsiteAddress },
        { "runtime_copy_destination_offset", record.RuntimeCopyDestinationOffset },
        { "runtime_copy_size_bytes", record.RuntimeCopySizeBytes },
        { "runtime_post_copy_zero_offset", record.RuntimePostCopyZeroOffset },
        { "runtime_flags_word_offset", record.RuntimeFlagsWordOffset },
        { "runtime_flags_or_mask", record.RuntimeFlagsOrMask },
        { "runtime_self_pointer_offset", record.RuntimeSelfPointerOffset },
        { "runtime_self_pointer_value_offset", record.RuntimeSelfPointerValueOffset },
        { "runtime_release_pointer_offset", record.RuntimeReleasePointerOffset },
        { "runtime_release_helper_address", record.RuntimeReleaseHelperAddress },
        { "runtime_buffer_size_bytes", record.RuntimeBufferSizeBytes },
        { "first_allocator_callsite_address", record.FirstAllocatorCallsiteAddress },
        { "second_allocator_callsite_address", record.SecondAllocatorCallsiteAddress },
        { "first_runtime_context_buffer_offset", record.FirstRuntimeContextBufferOffset },
        { "second_runtime_context_buffer_offset", record.SecondRuntimeContextBufferOffset },
        { "provider_lookup_callsite_address", record.ProviderLookupCallsiteAddress },
        { "provider_lookup_function_address", record.ProviderLookupFunctionAddress },
        { "provider_lookup_slot", record.ProviderLookupSlot },
        { "provider_stack_pointer_offset", record.ProviderStackPointerOffset },
        { "resource_context_table_offset", record.ResourceContextTableOffset },
        { "resource_context_entry_count", record.ResourceContextEntryCount },
        { "resource_context_entry_zero_path_table_address", record.ResourceContextEntryZeroPathTableAddress },
        { "resource_context_entry_zero_resolved_path_pattern",
          record.ResourceContextEntryZeroResolvedPathPattern },
        { "provider_slot_is_room_light_source", record.ProviderSlotIsRoomLightSource },
        { "resource_context_entry_zero_is_room_light_source", record.ResourceContextEntryZeroIsRoomLightSource },
        { "binding_helper_address", record.BindingHelperAddress },
        { "first_binding_callsite_address", record.FirstBindingCallsiteAddress },
        { "second_binding_callsite_address", record.SecondBindingCallsiteAddress },
        { "binding_slot_index", record.BindingSlotIndex },
        { "binding_mode_word", record.BindingModeWord },
        { "second_buffer_table_patch_address", record.SecondBufferTablePatchAddress },
        { "second_buffer_patch_table_stack_offset", record.SecondBufferPatchTableStackOffset },
        { "second_buffer_patch_entry_count", record.SecondBufferPatchEntryCount },
        { "second_buffer_patch_stride_bytes", record.SecondBufferPatchStrideBytes },
        { "second_buffer_patch_float_offsets", record.SecondBufferPatchFloatOffsets },
        { "second_buffer_patch_add_word", record.SecondBufferPatchAddWord },
        { "tables", tables },
    };
}

nlohmann::json NativeKankyoMaterialDrawStateJson(const NativeKankyoMaterialDrawStateContract& state) {
    return {
        { "function_address", state.FunctionAddress },
        { "submit_manager_vtable_slot_offset", state.SubmitManagerVtableSlotOffset },
        { "draw_record_index_pointer_slot_offset", state.DrawRecordIndexPointerSlotOffset },
        { "draw_record_table_pointer_slot_offset", state.DrawRecordTablePointerSlotOffset },
        { "draw_record_table_base_pointer_offset", state.DrawRecordTableBasePointerOffset },
        { "draw_record_offset_table_pointer_offset", state.DrawRecordOffsetTablePointerOffset },
        { "draw_record_offset_table_entry_stride_bytes",
          state.DrawRecordOffsetTableEntryStrideBytes },
        { "draw_record_offset_table_entries_are_u16", state.DrawRecordOffsetTableEntriesAreU16 },
        { "runtime_material_table_pointer_slot_offset", state.RuntimeMaterialTablePointerSlotOffset },
        { "runtime_material_table_lane_base_pointer_offset",
          state.RuntimeMaterialTableLaneBasePointerOffset },
        { "runtime_material_lane_index_byte_offset", state.RuntimeMaterialLaneIndexByteOffset },
        { "render_state_bitmask_offset", state.RenderStateBitmaskOffset },
        { "render_state_bitmask_base_value", state.RenderStateBitmaskBaseValue },
        { "source_material_byte_0_offset", state.SourceMaterialByte0Offset },
        { "source_material_byte_0_bit", state.SourceMaterialByte0Bit },
        { "source_material_byte_1_offset", state.SourceMaterialByte1Offset },
        { "source_material_byte_1_bit", state.SourceMaterialByte1Bit },
        { "draw_record_flag_halfword_offset", state.DrawRecordFlagHalfwordOffset },
        { "draw_record_flag_source_bits", state.DrawRecordFlagSourceBits },
        { "draw_record_flag_destination_bits", state.DrawRecordFlagDestinationBits },
        { "source_color_vector_word_count", state.SourceColorVectorWordCount },
        { "source_color_vector_word_offsets", state.SourceColorVectorWordOffsets },
        { "color_vector_upload_helper_address", state.ColorVectorUploadHelperAddress },
        { "color_vector_vsh_uniform_index", state.ColorVectorVshUniformIndex },
        { "color_vector_vsh_uniform_word_count", state.ColorVectorVshUniformWordCount },
        { "lighting_enable_upload_helper_address", state.LightingEnableUploadHelperAddress },
        { "lighting_enable_vsh_uniform_index", state.LightingEnableVshUniformIndex },
        { "lighting_enable_vsh_uniform_word_count", state.LightingEnableVshUniformWordCount },
        { "signed_halfword_float_seed_helper_address", state.SignedHalfwordFloatSeedHelperAddress },
        { "signed_halfword_source_draw_record_offset", state.SignedHalfwordSourceDrawRecordOffset },
        { "signed_halfword_destination_render_state_offset",
          state.SignedHalfwordDestinationRenderStateOffset },
        { "final_pica_register_packing_resolved", state.FinalPicaRegisterPackingResolved },
    };
}

nlohmann::json NativeKankyoPacketCopyDataflowAuditJson(
    const NativeKankyoPacketCopyDataflowAuditContract& audit) {
    return {
        { "scan_output_path", audit.ScanOutputPath },
        { "runtime_copy_helper_address", audit.RuntimeCopyHelperAddress },
        { "descriptor_binding_helper_address", audit.DescriptorBindingHelperAddress },
        { "total_callsite_count", audit.TotalCallsiteCount },
        { "runtime_copy_callsite_count", audit.RuntimeCopyCallsiteCount },
        { "descriptor_binding_callsite_count", audit.DescriptorBindingCallsiteCount },
        { "descriptor_binding_classified_not_packet_value_writer_count",
          audit.DescriptorBindingClassifiedNotPacketValueWriterCount },
        { "descriptor_binding_mentions_runtime_packet_prep_context_count",
          audit.DescriptorBindingMentionsRuntimePacketPrepContextCount },
        { "descriptor_binding_mentions_draw_handle_packet_pointer_count",
          audit.DescriptorBindingMentionsDrawHandlePacketPointerCount },
        { "descriptor_binding_mentions_packet_prep_function_count",
          audit.DescriptorBindingMentionsPacketPrepFunctionCount },
        { "descriptor_binding_mentions_runtime_light_packet_pack_function_count",
          audit.DescriptorBindingMentionsRuntimeLightPacketPackFunctionCount },
        { "descriptor_binding_mode_2600_mention_count",
          audit.DescriptorBindingMode2600MentionCount },
        { "descriptor_binding_callsite_coverage_status",
          audit.DescriptorBindingCallsiteCoverageStatus },
        { "descriptor_binding_writes_packet_prep_source_slots",
          audit.DescriptorBindingWritesPacketPrepSourceSlots },
        { "default_owner_function_address", audit.DefaultOwnerFunctionAddress },
        { "default_owner_runtime_copy_callsite_count", audit.DefaultOwnerRuntimeCopyCallsiteCount },
        { "default_owner_runtime_copy_callsite_addresses",
          audit.DefaultOwnerRuntimeCopyCallsiteAddresses },
        { "default_packet_buffer_copy_callsite_addresses",
          audit.DefaultPacketBufferCopyCallsiteAddresses },
        { "non_default_runtime_copy_candidate_count", audit.NonDefaultRuntimeCopyCandidateCount },
        { "non_default_runtime_copy_candidate_function_addresses",
          audit.NonDefaultRuntimeCopyCandidateFunctionAddresses },
        { "non_default_runtime_copy_candidate_callsite_addresses",
          audit.NonDefaultRuntimeCopyCandidateCallsiteAddresses },
        { "non_default_runtime_copy_candidate_classifications",
          audit.NonDefaultRuntimeCopyCandidateClassifications },
        { "non_default_runtime_copy_excluded_as_direct_light_packet_source_count",
          audit.NonDefaultRuntimeCopyExcludedAsDirectLightPacketSourceCount },
        { "non_default_runtime_copy_feeds_scene_zsi_light_records",
          audit.NonDefaultRuntimeCopyFeedsSceneZsiLightRecords },
        { "non_default_runtime_copy_directly_feeds_packet_prep",
          audit.NonDefaultRuntimeCopyDirectlyFeedsPacketPrep },
        { "non_default_runtime_copy_directly_feeds_runtime_light_packet_pack",
          audit.NonDefaultRuntimeCopyDirectlyFeedsRuntimeLightPacketPack },
        { "draw_handle_or_packet_prep_mentioned_descriptor_binding_callsite_addresses",
          audit.DrawHandleOrPacketPrepMentionedDescriptorBindingCallsiteAddresses },
        { "decompile_scan_found_direct_packet_prep_consumer",
          audit.DecompileScanFoundDirectPacketPrepConsumer },
        { "decompile_scan_found_runtime_packet_pack_consumer",
          audit.DecompileScanFoundRuntimePacketPackConsumer },
        { "active_room_packet_source_resolved", audit.ActiveRoomPacketSourceResolved },
    };
}

nlohmann::json NativeKankyoProviderTableJson(const NativeKankyoProviderTableContract& provider) {
    return {
        { "provider_lookup_function_address", provider.ProviderLookupFunctionAddress },
        { "runtime_provider_table_address", provider.RuntimeProviderTableAddress },
        { "provider_table_entry_count", provider.ProviderTableEntryCount },
        { "provider_table_entry_stride_bytes", provider.ProviderTableEntryStrideBytes },
        { "provider_population_address", provider.ProviderPopulationAddress },
        { "provider_population_end_address", provider.ProviderPopulationEndAddress },
        { "menu_ctxb_provider_slot", provider.MenuCtxbProviderSlot },
        { "menu_ctxb_language_prefix_table_address", provider.MenuCtxbLanguagePrefixTableAddress },
        { "menu_ctxb_suffix_table_address", provider.MenuCtxbSuffixTableAddress },
        { "menu_ctxb_path_format", provider.MenuCtxbPathFormat },
        { "menu_ctxb_resolved_path_pattern", provider.MenuCtxbResolvedPathPattern },
        { "menu_ctxb_provider_is_room_light_source", provider.MenuCtxbProviderIsRoomLightSource },
        { "kankyo_common_open_helper_address", provider.KankyoCommonOpenHelperAddress },
        { "kankyo_common_path", provider.KankyoCommonPath },
        { "zar_setup_function_address", provider.ZarSetupFunctionAddress },
        { "zar_header_type_section_offset", provider.ZarHeaderTypeSectionOffset },
        { "zar_header_metadata_section_offset", provider.ZarHeaderMetadataSectionOffset },
        { "zar_header_data_section_offset", provider.ZarHeaderDataSectionOffset },
        { "native_type_name_table_address", provider.NativeTypeNameTableAddress },
        { "native_type_slot_names", provider.NativeTypeSlotNames },
        { "provider_type_slot_index_base_offset", provider.ProviderTypeSlotIndexBaseOffset },
        { "provider_type_slot_index_stride_bytes", provider.ProviderTypeSlotIndexStrideBytes },
        { "cmb_type_slot", provider.CmbTypeSlot },
        { "ctxb_type_slot", provider.CtxbTypeSlot },
        { "zsi_type_slot", provider.ZsiTypeSlot },
        { "tbd_type_slot", provider.TbdTypeSlot },
        { "cmb_resolver_address", provider.CmbResolverAddress },
        { "ctxb_resolver_address", provider.CtxbResolverAddress },
        { "resolver_section_table_pointer_offset", provider.ResolverSectionTablePointerOffset },
        { "resolver_offset_table_pointer_offset", provider.ResolverOffsetTablePointerOffset },
        { "cmb_active_section_index_offset", provider.CmbActiveSectionIndexOffset },
        { "ctxb_active_section_index_offset", provider.CtxbActiveSectionIndexOffset },
        { "tbd_active_section_index_offset", provider.TbdActiveSectionIndexOffset },
        { "cmb_decoded_cache_offset", provider.CmbDecodedCacheOffset },
        { "ctxb_decoded_cache_offset", provider.CtxbDecodedCacheOffset },
        { "tbd_decoded_cache_offset", provider.TbdDecodedCacheOffset },
        { "cmb_decode_helper_address", provider.CmbDecodeHelperAddress },
        { "ctxb_decode_helper_address", provider.CtxbDecodeHelperAddress },
        { "tbd_resolver_address", provider.TbdResolverAddress },
        { "tbd_object_initializer_address", provider.TbdObjectInitializerAddress },
        { "tbd_object_free_helper_address", provider.TbdObjectFreeHelperAddress },
        { "zar_teardown_function_address", provider.ZarTeardownFunctionAddress },
        { "tbd_object_size_bytes", provider.TbdObjectSizeBytes },
        { "tbd_object_payload_pointer_offset", provider.TbdObjectPayloadPointerOffset },
        { "tbd_object_record_pointer_table_offset", provider.TbdObjectRecordPointerTableOffset },
        { "tbd_payload_record_count_offset", provider.TbdPayloadRecordCountOffset },
        { "tbd_payload_first_record_offset", provider.TbdPayloadFirstRecordOffset },
        { "tbd_record_size_offset", provider.TbdRecordSizeOffset },
        { "tbd_record_payload_accessor_address", provider.TbdRecordPayloadAccessorAddress },
        { "tbd_record_payload_offset", provider.TbdRecordPayloadOffset },
        { "kankyo_tbd_provider_object_offset", provider.KankyoTbdProviderObjectOffset },
        { "kankyo_lensflare_tbd_object_offset", provider.KankyoLensflareTbdObjectOffset },
        { "kankyo_storm_tbd_object_offset", provider.KankyoStormTbdObjectOffset },
        { "play_lensflare_tbd_object_offset", provider.PlayLensflareTbdObjectOffset },
        { "play_storm_tbd_object_offset", provider.PlayStormTbdObjectOffset },
        { "lensflare_tbd_provider_entry_index", provider.LensflareTbdProviderEntryIndex },
        { "storm_tbd_provider_entry_index", provider.StormTbdProviderEntryIndex },
        { "lensflare_tbd_consumer_address", provider.LensflareTbdConsumerAddress },
        { "lensflare_tbd_default_draw_wrapper_address", provider.LensflareTbdDefaultDrawWrapperAddress },
        { "lensflare_tbd_conditional_draw_wrapper_address", provider.LensflareTbdConditionalDrawWrapperAddress },
        { "lensflare_tbd_record_count", provider.LensflareTbdRecordCount },
        { "lensflare_tbd_record_indices", provider.LensflareTbdRecordIndices },
        { "lensflare_tbd_record_names", provider.LensflareTbdRecordNames },
        { "tbd_provider_parser_layout_resolved", provider.TbdProviderParserLayoutResolved },
        { "tbd_consumer_resolved_beyond_provider_setup", provider.TbdConsumerResolvedBeyondProviderSetup },
        { "lensflare_tbd_consumer_resolved", provider.LensflareTbdConsumerResolved },
        { "lensflare_tbd_visible_backend_submit_resolved", provider.LensflareTbdVisibleBackendSubmitResolved },
        { "kankyo_ctxb_native_id_start", provider.KankyoCtxbNativeIdStart },
        { "kankyo_ctxb_native_id_end", provider.KankyoCtxbNativeIdEnd },
        { "kankyo_ctxb_ids_are_direct_local_archive_indices",
          provider.KankyoCtxbIdsAreDirectLocalArchiveIndices },
    };
}

nlohmann::json NativeKankyoLensflareRuntimeListJson(
    const NativeKankyoLensflareRuntimeListContract& list) {
    return {
        { "kankyo_list_offset", list.KankyoListOffset },
        { "scene_init_address", list.SceneInitAddress },
        { "scene_update_address", list.SceneUpdateAddress },
        { "scene_teardown_address", list.SceneTeardownAddress },
        { "state_setter_address", list.StateSetterAddress },
        { "draw_address", list.DrawAddress },
        { "draw_helper_address", list.DrawHelperAddress },
        { "primary_builder_address", list.PrimaryBuilderAddress },
        { "teardown_address", list.TeardownAddress },
        { "object_transform_address", list.ObjectTransformAddress },
        { "submit_queue_address", list.SubmitQueueAddress },
        { "submit_record_writer_address", list.SubmitRecordWriterAddress },
        { "base_runtime_factory_address", list.BaseRuntimeFactoryAddress },
        { "quad_batch_runtime_factory_address", list.QuadBatchRuntimeFactoryAddress },
        { "shared_owner_constructor_address", list.SharedOwnerConstructorAddress },
        { "base_runtime_constructor_address", list.BaseRuntimeConstructorAddress },
        { "quad_batch_runtime_constructor_address", list.QuadBatchRuntimeConstructorAddress },
        { "base_runtime_vtable_address", list.BaseRuntimeVtableAddress },
        { "base_runtime_draw_method_address", list.BaseRuntimeDrawMethodAddress },
        { "quad_batch_runtime_vtable_address", list.QuadBatchRuntimeVtableAddress },
        { "quad_batch_runtime_draw_method_address", list.QuadBatchRuntimeDrawMethodAddress },
        { "quad_batch_runtime_destructor_address", list.QuadBatchRuntimeDestructorAddress },
        { "shared_owner_vtable_address", list.SharedOwnerVtableAddress },
        { "shared_owner_draw_method_address", list.SharedOwnerDrawMethodAddress },
        { "shared_owner_object_size_bytes", list.SharedOwnerObjectSizeBytes },
        { "base_runtime_object_size_bytes", list.BaseRuntimeObjectSizeBytes },
        { "quad_batch_runtime_object_size_bytes", list.QuadBatchRuntimeObjectSizeBytes },
        { "quad_batch_primary_buffer_resolver_address", list.QuadBatchPrimaryBufferResolverAddress },
        { "quad_batch_optional_buffer0_resolver_address", list.QuadBatchOptionalBuffer0ResolverAddress },
        { "quad_batch_optional_buffer1_resolver_address", list.QuadBatchOptionalBuffer1ResolverAddress },
        { "quad_batch_vertex_count_setter_address", list.QuadBatchVertexCountSetterAddress },
        { "quad_batch_runtime_element_count_offset", list.QuadBatchRuntimeElementCountOffset },
        { "quad_batch_runtime_element_capacity_offset", list.QuadBatchRuntimeElementCapacityOffset },
        { "quad_batch_runtime_input_positions_pointer_offset",
          list.QuadBatchRuntimeInputPositionsPointerOffset },
        { "quad_batch_runtime_input_matrices_pointer_offset",
          list.QuadBatchRuntimeInputMatricesPointerOffset },
        { "quad_batch_runtime_input_depths_pointer_offset",
          list.QuadBatchRuntimeInputDepthsPointerOffset },
        { "quad_batch_runtime_optional_matrix_pointer_offset",
          list.QuadBatchRuntimeOptionalMatrixPointerOffset },
        { "quad_batch_runtime_optional_texcoord_pointer_offset",
          list.QuadBatchRuntimeOptionalTexcoordPointerOffset },
        { "quad_batch_draw_handle_vertex_count_offset", list.QuadBatchDrawHandleVertexCountOffset },
        { "quad_batch_draw_handle_capacity_offset", list.QuadBatchDrawHandleCapacityOffset },
        { "quad_batch_fallback_quad_vertex_count", list.QuadBatchFallbackQuadVertexCount },
        { "quad_batch_native_vertices_per_quad", list.QuadBatchNativeVerticesPerQuad },
        { "active_group_word_offset", list.ActiveGroupWordOffset },
        { "target_group_word_offset", list.TargetGroupWordOffset },
        { "blend_weight_word_offset", list.BlendWeightWordOffset },
        { "blend_default_literal_address", list.BlendDefaultLiteralAddress },
        { "blend_scale_literal_address", list.BlendScaleLiteralAddress },
        { "blend_default", list.BlendDefault },
        { "blend_scale", list.BlendScale },
        { "group_index_shift_bits", list.GroupIndexShiftBits },
        { "cmb_handle_word_index_base", list.CmbHandleWordIndexBase },
        { "cmb_instance_word_index_base", list.CmbInstanceWordIndexBase },
        { "ctxb_descriptor_word_index_base", list.CtxbDescriptorWordIndexBase },
        { "render_object_word_index_base", list.RenderObjectWordIndexBase },
        { "primary_draw_object_word_index_base", list.PrimaryDrawObjectWordIndexBase },
        { "terminal_draw_object_word_index_base", list.TerminalDrawObjectWordIndexBase },
        { "primary_draw_object_source_row", list.PrimaryDrawObjectSourceRow },
        { "terminal_draw_object_source_row", list.TerminalDrawObjectSourceRow },
        { "primary_element_submit_index", list.PrimaryElementSubmitIndex },
        { "terminal_element_submit_index", list.TerminalElementSubmitIndex },
        { "primary_submit_queue_index_base", list.PrimarySubmitQueueIndexBase },
        { "terminal_submit_queue_index_base", list.TerminalSubmitQueueIndexBase },
        { "special_submit_element_start_index", list.SpecialSubmitElementStartIndex },
        { "special_submit_element_end_index", list.SpecialSubmitElementEndIndex },
        { "lensflare_runtime_element_count", list.LensflareRuntimeElementCount },
        { "runtime_object_flag_offset", list.RuntimeObjectFlagOffset },
        { "runtime_object_visible_flag_mask", list.RuntimeObjectVisibleFlagMask },
        { "runtime_object_primary_builder_flag_mask", list.RuntimeObjectPrimaryBuilderFlagMask },
        { "builder_table_address", list.BuilderTableAddress },
        { "ctxb_descriptor_template_rows", list.CtxbDescriptorTemplateRows },
        { "render_object_source_rows", list.RenderObjectSourceRows },
        { "ctxb_index_base_by_row", list.CtxbIndexBaseByRow },
        { "init_resolved", list.InitResolved },
        { "draw_slots_resolved", list.DrawSlotsResolved },
        { "draw_helper_dispatch_resolved", list.DrawHelperDispatchResolved },
        { "target_group_dispatch_only_when_different",
          list.TargetGroupDispatchOnlyWhenDifferent },
        { "runtime_vtable_methods_resolved", list.RuntimeVtableMethodsResolved },
        { "runtime_backend_buffer_helpers_resolved", list.RuntimeBackendBufferHelpersResolved },
        { "backend_submit_resolved", list.BackendSubmitResolved },
    };
}

nlohmann::json NativeKankyoMoonRuntimeJson(
    const NativeKankyoMoonRuntimeContract& moon) {
    return {
        { "scene_init_address", moon.SceneInitAddress },
        { "builder_address", moon.BuilderAddress },
        { "blue_sky_builder_callsite_address", moon.BlueSkyBuilderCallsiteAddress },
        { "ctxb_base_type_local_index", moon.CtxbBaseTypeLocalIndex },
        { "layer_count", moon.LayerCount },
        { "geometry_template_indices", moon.GeometryTemplateIndices },
        { "geometry_template_half_extents", moon.GeometryTemplateHalfExtents },
        { "runtime_object_template_indices", moon.RuntimeObjectTemplateIndices },
        { "min_mag_filter", moon.MinMagFilter },
        { "wrap_modes", moon.WrapModes },
        { "init_resolved", moon.InitResolved },
        { "texture_inputs_resolved", moon.TextureInputsResolved },
        { "backend_submit_resolved", moon.BackendSubmitResolved },
    };
}

nlohmann::json NativePicaMaterialScalarEmitJson(const NativePicaMaterialScalarEmitContract& emit) {
    return {
        { "dispatch_address", emit.DispatchAddress },
        { "dispatch_tail_branch_address", emit.DispatchTailBranchAddress },
        { "material_packet_gate_byte_offset", emit.MaterialPacketGateByteOffset },
        { "material_packet_vector_base_offset", emit.MaterialPacketVectorBaseOffset },
        { "material_packet_vector_component_count", emit.MaterialPacketVectorComponentCount },
        { "material_packet_aux_word_offset", emit.MaterialPacketAuxWordOffset },
        { "material_packet_payload_pointer_offset", emit.MaterialPacketPayloadPointerOffset },
        { "material_packet_flag_word_offset", emit.MaterialPacketFlagWordOffset },
        { "material_packet_backend_enabled_byte_offset", emit.MaterialPacketBackendEnabledByteOffset },
        { "fallback_packet_emit_address", emit.FallbackPacketEmitAddress },
        { "backend_packet_build_address", emit.BackendPacketBuildAddress },
        { "backend_enable_register_address", emit.BackendEnableRegisterAddress },
        { "backend_enable_register_mask", emit.BackendEnableRegisterMask },
        { "color_command_build_address", emit.ColorCommandBuildAddress },
        { "color_scale_word_address", emit.ColorScaleWordAddress },
        { "color_scale_word", emit.ColorScaleWord },
        { "color_command_word_0_base", emit.ColorCommandWord0Base },
        { "color_command_flag_shift", emit.ColorCommandFlagShift },
        { "color_command_word_1", emit.ColorCommandWord1 },
        { "color_command_word_3", emit.ColorCommandWord3 },
        { "scalar_emit_wrapper_address", emit.ScalarEmitWrapperAddress },
        { "generic_scalar_writer_address", emit.GenericScalarWriterAddress },
        { "scalar_register_0_call_address", emit.ScalarRegister0CallAddress },
        { "scalar_register_0", emit.ScalarRegister0 },
        { "scalar_register_0_count", emit.ScalarRegister0Count },
        { "scalar_register_0_mask", emit.ScalarRegister0Mask },
        { "scalar_register_0_sequential_flag", emit.ScalarRegister0SequentialFlag },
        { "scalar_register_0_payload_word", emit.ScalarRegister0PayloadWord },
        { "scalar_register_0_payload_is_literal_zero", emit.ScalarRegister0PayloadIsLiteralZero },
        { "scalar_register_1_call_address", emit.ScalarRegister1CallAddress },
        { "scalar_register_1", emit.ScalarRegister1 },
        { "scalar_register_1_count", emit.ScalarRegister1Count },
        { "scalar_register_1_mask", emit.ScalarRegister1Mask },
        { "scalar_register_1_sequential_flag", emit.ScalarRegister1SequentialFlag },
        { "scalar_register_1_payload_pointer_offset", emit.ScalarRegister1PayloadPointerOffset },
        { "payload_setter_command_dispatcher_address", emit.PayloadSetterCommandDispatcherAddress },
        { "payload_setter_command_opcode", emit.PayloadSetterCommandOpcode },
        { "payload_setter_command_target_pointer_offset", emit.PayloadSetterCommandTargetPointerOffset },
        { "payload_setter_command_aux_word_offset", emit.PayloadSetterCommandAuxWordOffset },
        { "payload_setter_command_payload_pointer_offset", emit.PayloadSetterCommandPayloadPointerOffset },
        { "payload_setter_command_emitter_address", emit.PayloadSetterCommandEmitterAddress },
        { "payload_setter_command_emitter_caller_address", emit.PayloadSetterCommandEmitterCallerAddress },
        { "payload_setter_command_emitter_queue_alloc_address", emit.PayloadSetterCommandEmitterQueueAllocAddress },
        { "payload_setter_command_emitter_queue_enqueue_address", emit.PayloadSetterCommandEmitterQueueEnqueueAddress },
        { "payload_setter_command_emitter_record_word_count", emit.PayloadSetterCommandEmitterRecordWordCount },
        { "payload_setter_command_emitter_target_base_offset", emit.PayloadSetterCommandEmitterTargetBaseOffset },
        { "payload_setter_command_emitter_aux_source_offset", emit.PayloadSetterCommandEmitterAuxSourceOffset },
        { "payload_setter_command_emitter_payload_source_offset", emit.PayloadSetterCommandEmitterPayloadSourceOffset },
        { "payload_setter_material_submit_address", emit.PayloadSetterMaterialSubmitAddress },
        { "payload_setter_material_submit_caller_address", emit.PayloadSetterMaterialSubmitCallerAddress },
        { "payload_setter_material_submit_driver_address", emit.PayloadSetterMaterialSubmitDriverAddress },
        { "payload_setter_material_submit_embedded_object_caller0_address",
          emit.PayloadSetterMaterialSubmitEmbeddedObjectCaller0Address },
        { "payload_setter_material_submit_embedded_object_caller1_address",
          emit.PayloadSetterMaterialSubmitEmbeddedObjectCaller1Address },
        { "payload_setter_material_submit_direct_object_caller_address",
          emit.PayloadSetterMaterialSubmitDirectObjectCallerAddress },
        { "payload_setter_material_submit_embedded_object_offset",
          emit.PayloadSetterMaterialSubmitEmbeddedObjectOffset },
        { "payload_setter_material_submit_aux_read_offset", emit.PayloadSetterMaterialSubmitAuxReadOffset },
        { "payload_setter_material_submit_payload_read_offset", emit.PayloadSetterMaterialSubmitPayloadReadOffset },
        { "payload_setter_material_submit_mode_resolver_address",
          emit.PayloadSetterMaterialSubmitModeResolverAddress },
        { "payload_setter_material_submit_mode_record_resolver_address",
          emit.PayloadSetterMaterialSubmitModeRecordResolverAddress },
        { "payload_setter_material_submit_mode_decode_address", emit.PayloadSetterMaterialSubmitModeDecodeAddress },
        { "payload_setter_material_submit_mode_source_argument_index",
          emit.PayloadSetterMaterialSubmitModeSourceArgumentIndex },
        { "payload_setter_material_submit_mode_owner_payload_offset",
          emit.PayloadSetterMaterialSubmitModeOwnerPayloadOffset },
        { "payload_setter_material_submit_mode_owner_inner_payload_offset",
          emit.PayloadSetterMaterialSubmitModeOwnerInnerPayloadOffset },
        { "payload_setter_material_submit_mode_record_table_offset",
          emit.PayloadSetterMaterialSubmitModeRecordTableOffset },
        { "payload_setter_material_submit_mode_record_count_offset",
          emit.PayloadSetterMaterialSubmitModeRecordCountOffset },
        { "payload_setter_material_submit_mode_record_offset_table_offset",
          emit.PayloadSetterMaterialSubmitModeRecordOffsetTableOffset },
        { "payload_setter_material_submit_mode_encoded_high_byte",
          emit.PayloadSetterMaterialSubmitModeEncodedHighByte },
        { "payload_setter_material_submit_mode_record_type_offset",
          emit.PayloadSetterMaterialSubmitModeRecordTypeOffset },
        { "payload_setter_material_submit_mode_1_record_type", emit.PayloadSetterMaterialSubmitMode1RecordType },
        { "payload_setter_material_submit_mode_2_record_type", emit.PayloadSetterMaterialSubmitMode2RecordType },
        { "payload_setter_material_submit_mode_3_record_type", emit.PayloadSetterMaterialSubmitMode3RecordType },
        { "payload_setter_material_submit_mode_1_context_list_offset",
          emit.PayloadSetterMaterialSubmitMode1ContextListOffset },
        { "payload_setter_material_submit_mode_2_context_list_offset",
          emit.PayloadSetterMaterialSubmitMode2ContextListOffset },
        { "payload_setter_material_submit_mode_3_context_list_offset",
          emit.PayloadSetterMaterialSubmitMode3ContextListOffset },
        { "payload_setter_material_submit_mode_context_list_node_offset",
          emit.PayloadSetterMaterialSubmitModeContextListNodeOffset },
        { "payload_setter_material_submit_mode_context_priority_byte_offset",
          emit.PayloadSetterMaterialSubmitModeContextPriorityByteOffset },
        { "payload_setter_material_submit_mode_context_priority_word_offset",
          emit.PayloadSetterMaterialSubmitModeContextPriorityWordOffset },
        { "payload_setter_material_submit_mode_context_submit_argument_offset",
          emit.PayloadSetterMaterialSubmitModeContextSubmitArgumentOffset },
        { "payload_setter_material_submit_mode_1_validator_address",
          emit.PayloadSetterMaterialSubmitMode1ValidatorAddress },
        { "payload_setter_material_submit_mode_2_validator_address",
          emit.PayloadSetterMaterialSubmitMode2ValidatorAddress },
        { "payload_setter_material_submit_mode_3_validator_address",
          emit.PayloadSetterMaterialSubmitMode3ValidatorAddress },
        { "payload_setter_material_submit_mode_2_path_address", emit.PayloadSetterMaterialSubmitMode2PathAddress },
        { "payload_setter_material_submit_mode_3_path_address", emit.PayloadSetterMaterialSubmitMode3PathAddress },
        { "payload_setter_material_submit_mode_1_record_payload_resolver_address",
          emit.PayloadSetterMaterialSubmitMode1RecordPayloadResolverAddress },
        { "payload_setter_material_submit_mode_1_record_payload_relative_offset",
          emit.PayloadSetterMaterialSubmitMode1RecordPayloadRelativeOffset },
        { "payload_setter_material_submit_mode_1_decoded_block_word_0_offset",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockWord0Offset },
        { "payload_setter_material_submit_mode_1_decoded_block_reference_words_offset",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordsOffset },
        { "payload_setter_material_submit_mode_1_decoded_block_reference_word_count",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockReferenceWordCount },
        { "payload_setter_material_submit_mode_1_decoded_block_word_5_offset",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockWord5Offset },
        { "payload_setter_material_submit_mode_1_decoded_block_byte_24_offset",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockByte24Offset },
        { "payload_setter_material_submit_mode_1_decoded_block_byte_25_offset",
          emit.PayloadSetterMaterialSubmitMode1DecodedBlockByte25Offset },
        { "payload_setter_material_submit_mode_1_word_0_resolver_address",
          emit.PayloadSetterMaterialSubmitMode1Word0ResolverAddress },
        { "payload_setter_material_submit_mode_1_reference_words_resolver_address",
          emit.PayloadSetterMaterialSubmitMode1ReferenceWordsResolverAddress },
        { "payload_setter_material_submit_mode_1_byte_24_resolver_address",
          emit.PayloadSetterMaterialSubmitMode1Byte24ResolverAddress },
        { "payload_setter_material_submit_mode_1_byte_25_resolver_address",
          emit.PayloadSetterMaterialSubmitMode1Byte25ResolverAddress },
        { "payload_setter_material_submit_mode_1_reference_table_relative_offset",
          emit.PayloadSetterMaterialSubmitMode1ReferenceTableRelativeOffset },
        { "payload_setter_material_submit_mode_1_reference_table_count_offset",
          emit.PayloadSetterMaterialSubmitMode1ReferenceTableCountOffset },
        { "payload_setter_material_submit_mode_1_reference_table_first_entry_offset",
          emit.PayloadSetterMaterialSubmitMode1ReferenceTableFirstEntryOffset },
        { "payload_setter_material_submit_mode_1_default_reference_word",
          emit.PayloadSetterMaterialSubmitMode1DefaultReferenceWord },
        { "payload_setter_material_submit_mode_1_default_byte_24",
          emit.PayloadSetterMaterialSubmitMode1DefaultByte24 },
        { "payload_setter_material_submit_mode_1_default_byte_25",
          emit.PayloadSetterMaterialSubmitMode1DefaultByte25 },
        { "payload_setter_material_submit_opcode_13_mode", emit.PayloadSetterMaterialSubmitOpcode13Mode },
        { "payload_setter_material_submit_opcode_13_mode_copy_call_address",
          emit.PayloadSetterMaterialSubmitOpcode13ModeCopyCallAddress },
        { "payload_setter_material_submit_non_opcode_13_mode_2_copy_call_address",
          emit.PayloadSetterMaterialSubmitNonOpcode13Mode2CopyCallAddress },
        { "payload_setter_material_submit_non_opcode_13_mode_3_copy_call_address",
          emit.PayloadSetterMaterialSubmitNonOpcode13Mode3CopyCallAddress },
        { "payload_setter_material_submit_opcode_13_mode_submit_call_address",
          emit.PayloadSetterMaterialSubmitOpcode13ModeSubmitCallAddress },
        { "payload_setter_material_context_base_init_address", emit.PayloadSetterMaterialContextBaseInitAddress },
        { "payload_setter_material_context_base_vtable_address", emit.PayloadSetterMaterialContextBaseVtableAddress },
        { "payload_setter_material_context_derived0_init_address",
          emit.PayloadSetterMaterialContextDerived0InitAddress },
        { "payload_setter_material_context_derived0_vtable_address",
          emit.PayloadSetterMaterialContextDerived0VtableAddress },
        { "payload_setter_material_context_derived1_init_address",
          emit.PayloadSetterMaterialContextDerived1InitAddress },
        { "payload_setter_material_context_derived1_vtable_address",
          emit.PayloadSetterMaterialContextDerived1VtableAddress },
        { "payload_setter_material_context_derived2_init_address",
          emit.PayloadSetterMaterialContextDerived2InitAddress },
        { "payload_setter_material_context_derived2_vtable_address",
          emit.PayloadSetterMaterialContextDerived2VtableAddress },
        { "payload_setter_material_context_aux_field_offset", emit.PayloadSetterMaterialContextAuxFieldOffset },
        { "payload_setter_material_context_payload_field_offset",
          emit.PayloadSetterMaterialContextPayloadFieldOffset },
        { "payload_setter_material_context_init_zero_value", emit.PayloadSetterMaterialContextInitZeroValue },
        { "payload_setter_material_context_attach_callsite_address",
          emit.PayloadSetterMaterialContextAttachCallsiteAddress },
        { "payload_setter_material_context_attach_address", emit.PayloadSetterMaterialContextAttachAddress },
        { "payload_setter_material_context_attach_list_insert_address",
          emit.PayloadSetterMaterialContextAttachListInsertAddress },
        { "payload_setter_material_context_attach_optional_list_stack_offset",
          emit.PayloadSetterMaterialContextAttachOptionalListStackOffset },
        { "payload_setter_material_context_attach_list_count_offset",
          emit.PayloadSetterMaterialContextAttachListCountOffset },
        { "payload_setter_material_context_attach_list_head_offset",
          emit.PayloadSetterMaterialContextAttachListHeadOffset },
        { "payload_setter_material_context_attach_list_node_offset",
          emit.PayloadSetterMaterialContextAttachListNodeOffset },
        { "payload_setter_material_context_attach_aux_field_offset",
          emit.PayloadSetterMaterialContextAttachAuxFieldOffset },
        { "payload_setter_material_context_attach_writes_payload_field",
          emit.PayloadSetterMaterialContextAttachWritesPayloadField },
        { "payload_setter_material_context_payload_copy_address",
          emit.PayloadSetterMaterialContextPayloadCopyAddress },
        { "payload_setter_material_context_payload_copy_call0_address",
          emit.PayloadSetterMaterialContextPayloadCopyCall0Address },
        { "payload_setter_material_context_payload_copy_call1_address",
          emit.PayloadSetterMaterialContextPayloadCopyCall1Address },
        { "payload_setter_material_context_payload_copy_call2_address",
          emit.PayloadSetterMaterialContextPayloadCopyCall2Address },
        { "payload_setter_material_context_payload_copy_source_argument_index",
          emit.PayloadSetterMaterialContextPayloadCopySourceArgumentIndex },
        { "payload_setter_material_context_payload_copy_destination_offset",
          emit.PayloadSetterMaterialContextPayloadCopyDestinationOffset },
        { "payload_setter_material_context_payload_copy_source_word_count",
          emit.PayloadSetterMaterialContextPayloadCopySourceWordCount },
        { "payload_setter_material_context_payload_copy_resolver_source_offset",
          emit.PayloadSetterMaterialContextPayloadCopyResolverSourceOffset },
        { "payload_setter_material_context_payload_copy_resolver_vtable_slot_offset",
          emit.PayloadSetterMaterialContextPayloadCopyResolverVtableSlotOffset },
        { "payload_setter_material_context_payload_copy_resolved_object_offset",
          emit.PayloadSetterMaterialContextPayloadCopyResolvedObjectOffset },
        { "payload_setter_material_payload_source_builder_address",
          emit.PayloadSetterMaterialPayloadSourceBuilderAddress },
        { "payload_setter_material_payload_source_builder_callsite_address",
          emit.PayloadSetterMaterialPayloadSourceBuilderCallsiteAddress },
        { "payload_setter_material_payload_source_template_pointer_address",
          emit.PayloadSetterMaterialPayloadSourceTemplatePointerAddress },
        { "payload_setter_material_payload_source_template_address",
          emit.PayloadSetterMaterialPayloadSourceTemplateAddress },
        { "payload_setter_material_payload_source_object_base_offset",
          emit.PayloadSetterMaterialPayloadSourceObjectBaseOffset },
        { "payload_setter_material_payload_source_payload_object_offset",
          emit.PayloadSetterMaterialPayloadSourcePayloadObjectOffset },
        { "payload_setter_material_payload_source_payload_pointer_relative_offset",
          emit.PayloadSetterMaterialPayloadSourcePayloadPointerRelativeOffset },
        { "payload_setter_material_payload_source_base_constructor_address",
          emit.PayloadSetterMaterialPayloadSourceBaseConstructorAddress },
        { "payload_setter_material_payload_source_base_vtable_address",
          emit.PayloadSetterMaterialPayloadSourceBaseVtableAddress },
        { "payload_setter_material_payload_source_base_vtable_pointer_literal_address",
          emit.PayloadSetterMaterialPayloadSourceBaseVtablePointerLiteralAddress },
        { "payload_setter_material_payload_source_derived_effect_constructor_address",
          emit.PayloadSetterMaterialPayloadSourceDerivedEffectConstructorAddress },
        { "payload_setter_material_payload_source_derived_effect_vtable_address",
          emit.PayloadSetterMaterialPayloadSourceDerivedEffectVtableAddress },
        { "payload_setter_material_payload_source_derived_effect_vtable_pointer_literal_address",
          emit.PayloadSetterMaterialPayloadSourceDerivedEffectVtablePointerLiteralAddress },
        { "payload_setter_material_payload_source_derived_effect_light_list_alias_offset",
          emit.PayloadSetterMaterialPayloadSourceDerivedEffectLightListAliasOffset },
        { "payload_setter_descriptor_init_address", emit.PayloadSetterDescriptorInitAddress },
        { "payload_setter_descriptor_source_file_literal_address", emit.PayloadSetterDescriptorSourceFileLiteralAddress },
        { "payload_setter_descriptor_object_size_literal_address", emit.PayloadSetterDescriptorObjectSizeLiteralAddress },
        { "payload_setter_descriptor_object_size_bytes", emit.PayloadSetterDescriptorObjectSizeBytes },
        { "payload_setter_descriptor_aux_source_offset", emit.PayloadSetterDescriptorAuxSourceOffset },
        { "payload_setter_descriptor_payload_source_offset", emit.PayloadSetterDescriptorPayloadSourceOffset },
        { "payload_setter_descriptor_payload_base_relative_offset",
          emit.PayloadSetterDescriptorPayloadBaseRelativeOffset },
        { "payload_setter_descriptor_aux_table_relative_offset", emit.PayloadSetterDescriptorAuxTableRelativeOffset },
        { "runtime_payload_binder_address", emit.RuntimePayloadBinderAddress },
        { "runtime_payload_binder_vector_sync_address", emit.RuntimePayloadBinderVectorSyncAddress },
        { "runtime_payload_binder_packed_table_build_address", emit.RuntimePayloadBinderPackedTableBuildAddress },
        { "runtime_payload_binder_object_pointer_offset", emit.RuntimePayloadBinderObjectPointerOffset },
        { "runtime_payload_binder_enabled_byte_offset", emit.RuntimePayloadBinderEnabledByteOffset },
        { "runtime_payload_binder_dirty_byte_offset", emit.RuntimePayloadBinderDirtyByteOffset },
        { "runtime_payload_binder_aux_word_source_offset", emit.RuntimePayloadBinderAuxWordSourceOffset },
        { "runtime_payload_binder_vector_source_offset", emit.RuntimePayloadBinderVectorSourceOffset },
        { "runtime_payload_binder_packed_table_source_offset", emit.RuntimePayloadBinderPackedTableSourceOffset },
        { "runtime_payload_binder_packet_aux_word_destination_offset",
          emit.RuntimePayloadBinderPacketAuxWordDestinationOffset },
        { "runtime_payload_binder_packet_payload_pointer_destination_offset",
          emit.RuntimePayloadBinderPacketPayloadPointerDestinationOffset },
        { "runtime_payload_binder_packet_vector_destination_offset",
          emit.RuntimePayloadBinderPacketVectorDestinationOffset },
        { "runtime_list_binder_address", emit.RuntimeListBinderAddress },
        { "runtime_list_binder_source_play_offset", emit.RuntimeListBinderSourcePlayOffset },
        { "runtime_list_0_play_offset", emit.RuntimeList0PlayOffset },
        { "runtime_list_1_play_offset", emit.RuntimeList1PlayOffset },
        { "runtime_list_count_byte_offset", emit.RuntimeListCountByteOffset },
        { "runtime_list_entry_stride_bytes", emit.RuntimeListEntryStrideBytes },
        { "runtime_list_entry_runtime_object_pointer_offset",
          emit.RuntimeListEntryRuntimeObjectPointerOffset },
        { "runtime_object_material_packet_resolver_address",
          emit.RuntimeObjectMaterialPacketResolverAddress },
        { "runtime_object_packet_owner_pointer_offset", emit.RuntimeObjectPacketOwnerPointerOffset },
        { "runtime_object_packet_owner_material_packet_offset",
          emit.RuntimeObjectPacketOwnerMaterialPacketOffset },
        { "runtime_list_binder_applies_fog_source_to_material_packets",
          emit.RuntimeListBinderAppliesFogSourceToMaterialPackets },
        { "payload_setter_address", emit.PayloadSetterAddress },
        { "payload_setter_aux_word_destination_offset", emit.PayloadSetterAuxWordDestinationOffset },
        { "payload_setter_pointer_destination_offset", emit.PayloadSetterPointerDestinationOffset },
        { "fog_payload_runtime_update_caller_address", emit.FogPayloadRuntimeUpdateCallerAddress },
        { "fog_payload_runtime_update_address", emit.FogPayloadRuntimeUpdateAddress },
        { "fog_payload_source_file_literal_address", emit.FogPayloadSourceFileLiteralAddress },
        { "fog_payload_default_source_address", emit.FogPayloadDefaultSourceAddress },
        { "fog_payload_object_size_literal_address", emit.FogPayloadObjectSizeLiteralAddress },
        { "fog_payload_object_size_bytes", emit.FogPayloadObjectSizeBytes },
        { "fog_payload_init_address", emit.FogPayloadInitAddress },
        { "fog_payload_build_address", emit.FogPayloadBuildAddress },
        { "fog_payload_release_address", emit.FogPayloadReleaseAddress },
        { "fog_payload_source_pointer_offset", emit.FogPayloadSourcePointerOffset },
        { "fog_payload_source_float_0_offset", emit.FogPayloadSourceFloat0Offset },
        { "fog_payload_source_float_1_offset", emit.FogPayloadSourceFloat1Offset },
        { "fog_payload_packed_table_offset", emit.FogPayloadPackedTableOffset },
        { "fog_payload_packed_table_entry_count", emit.FogPayloadPackedTableEntryCount },
        { "fog_payload_packed_table_word_bytes", emit.FogPayloadPackedTableWordBytes },
    };
}

nlohmann::json NativePicaCmbLutAssetDecodeJson(const NativePicaCmbLutAssetDecodeContract& decode) {
    return {
        { "decode_address", decode.DecodeAddress },
        { "decode_callsite_address", decode.DecodeCallsiteAddress },
        { "decode_owner_function_address", decode.DecodeOwnerFunctionAddress },
        { "source_lut_section_count_offset", decode.SourceLutSectionCountOffset },
        { "source_lut_record_offset_table_offset", decode.SourceLutRecordOffsetTableOffset },
        { "runtime_object_source_section_pointer_offset", decode.RuntimeObjectSourceSectionPointerOffset },
        { "runtime_object_pointer_table_offset", decode.RuntimeObjectPointerTableOffset },
        { "runtime_object_packed_table_base_offset", decode.RuntimeObjectPackedTableBaseOffset },
        { "runtime_object_allocator_context_offset", decode.RuntimeObjectAllocatorContextOffset },
        { "allocator_cursor_offset", decode.AllocatorCursorOffset },
        { "pointer_table_entry_size_bytes", decode.PointerTableEntrySizeBytes },
        { "packed_table_bytes_per_lut", decode.PackedTableBytesPerLut },
        { "source_sample_count", decode.SourceSampleCount },
        { "packed_base_value_offset", decode.PackedBaseValueOffset },
        { "packed_delta_value_offset", decode.PackedDeltaValueOffset },
        { "packed_value_count", decode.PackedValueCount },
        { "packed_iteration_count", decode.PackedIterationCount },
        { "pointer_table_init_address", decode.PointerTableInitAddress },
        { "source_evaluator_address", decode.SourceEvaluatorAddress },
        { "clamp_minimum_word_address", decode.ClampMinimumWordAddress },
        { "clamp_minimum_word", decode.ClampMinimumWord },
        { "upload_pointer_bind_address", decode.UploadPointerBindAddress },
        { "upload_helper_address", decode.UploadHelperAddress },
        { "upload_register_base", decode.UploadRegisterBase },
        { "upload_helper_register_range_base", decode.UploadHelperRegisterRangeBase },
        { "upload_helper_register_range_count", decode.UploadHelperRegisterRangeCount },
        { "upload_copy_word_count", decode.UploadCopyWordCount },
        { "upload_copy_byte_count", decode.UploadCopyByteCount },
        { "upload_copy_destination_offset", decode.UploadCopyDestinationOffset },
        { "upload_invalidation_word_offset", decode.UploadInvalidationWordOffset },
        { "upload_invalidation_word_value", decode.UploadInvalidationWordValue },
        { "upload_dirty_flag_mask", decode.UploadDirtyFlagMask },
        { "upload_header_word_0", decode.UploadHeaderWord0 },
        { "upload_header_word_1_address", decode.UploadHeaderWord1Address },
        { "upload_header_word_1", decode.UploadHeaderWord1 },
        { "upload_header_word_2_address", decode.UploadHeaderWord2Address },
        { "upload_header_word_2", decode.UploadHeaderWord2 },
        { "final_shader_semantic_resolved", decode.FinalShaderSemanticResolved },
    };
}

nlohmann::json NativeKankyoDrawHandleSubmitJson(const NativeKankyoDrawHandleSubmitContract& submit) {
    return {
        { "function_address", submit.FunctionAddress },
        { "packet_prep_address", submit.PacketPrepAddress },
        { "matrix_begin_address", submit.MatrixBeginAddress },
        { "matrix_end_address", submit.MatrixEndAddress },
        { "primitive_packet_build_address", submit.PrimitivePacketBuildAddress },
        { "material_animation_build_0_address", submit.MaterialAnimationBuild0Address },
        { "material_animation_build_1_address", submit.MaterialAnimationBuild1Address },
        { "material_animation_build_2_address", submit.MaterialAnimationBuild2Address },
        { "material_state_setup_address", submit.MaterialStateSetupAddress },
        { "material_state_fallback_address", submit.MaterialStateFallbackAddress },
        { "per_mesh_packet_flush_address", submit.PerMeshPacketFlushAddress },
        { "per_mesh_command_writer_init_address", submit.PerMeshCommandWriterInitAddress },
        { "per_mesh_command_writer_state_offset", submit.PerMeshCommandWriterStateOffset },
        { "per_mesh_command_scratch_pointer_literal_address",
          submit.PerMeshCommandScratchPointerLiteralAddress },
        { "per_mesh_command_scratch_capacity_bytes", submit.PerMeshCommandScratchCapacityBytes },
        { "per_mesh_command_primitive_emitter_address", submit.PerMeshCommandPrimitiveEmitterAddress },
        { "per_mesh_command_used_size_address", submit.PerMeshCommandUsedSizeAddress },
        { "per_mesh_command_element_build_address", submit.PerMeshCommandElementBuildAddress },
        { "per_mesh_command_list_offset", submit.PerMeshCommandListOffset },
        { "per_mesh_command_list_element_stride_bytes", submit.PerMeshCommandListElementStrideBytes },
        { "per_mesh_command_element_used_size_offset", submit.PerMeshCommandElementUsedSizeOffset },
        { "per_mesh_command_element_aligned_copy_destination_offset",
          submit.PerMeshCommandElementAlignedCopyDestinationOffset },
        { "per_mesh_command_element_allocation_overhead_bytes",
          submit.PerMeshCommandElementAllocationOverheadBytes },
        { "per_mesh_command_element_allocation_alignment_bytes",
          submit.PerMeshCommandElementAllocationAlignmentBytes },
        { "per_mesh_command_builder_directly_writes_packet_prep_source",
          submit.PerMeshCommandBuilderDirectlyWritesPacketPrepSource },
        { "per_mesh_command_builder_status", submit.PerMeshCommandBuilderStatus },
        { "material_transform_cache_alloc_address", submit.MaterialTransformCacheAllocAddress },
        { "material_transform_cache_patch_address", submit.MaterialTransformCachePatchAddress },
        { "material_packet_pointer_offset", submit.MaterialPacketPointerOffset },
        { "material_cache_ready_byte_offset", submit.MaterialCacheReadyByteOffset },
        { "submitted_pass_byte_offset", submit.SubmittedPassByteOffset },
        { "material_transform_array_offset", submit.MaterialTransformArrayOffset },
        { "visibility_table_offset", submit.VisibilityTableOffset },
        { "material_transform_patch_source_offset", submit.MaterialTransformPatchSourceOffset },
        { "material_draw_dispatch_address", submit.MaterialDrawDispatchAddress },
        { "material_draw_dispatch_loop_end_address", submit.MaterialDrawDispatchLoopEndAddress },
        { "material_draw_dispatch_resolved_from_codebin", submit.MaterialDrawDispatchResolvedFromCodebin },
        { "material_draw_dispatch_promotes_active_override_gate",
          submit.MaterialDrawDispatchPromotesActiveOverrideGate },
        { "cmb_mesh_stride_bytes", submit.CmbMeshStrideBytes },
        { "cmb_mesh_material_lane_byte_offset", submit.CmbMeshMaterialLaneByteOffset },
        { "cmb_mesh_visibility_byte_offset", submit.CmbMeshVisibilityByteOffset },
        { "cmb_mesh_count_offset", submit.CmbMeshCountOffset },
        { "cmb_mesh_pass_split_index_offset", submit.CmbMeshPassSplitIndexOffset },
        { "material_lane_stride_bytes", submit.MaterialLaneStrideBytes },
        { "cmb_material_state_gate_byte_offset", submit.CmbMaterialStateGateByteOffset },
        { "material_lane_animation_byte_block_0_offset", submit.MaterialLaneAnimationByteBlock0Offset },
        { "material_lane_animation_byte_block_1_offset", submit.MaterialLaneAnimationByteBlock1Offset },
        { "material_lane_populate_caller_address", submit.MaterialLanePopulateCallerAddress },
        { "material_lane_populate_address", submit.MaterialLanePopulateAddress },
        { "material_lane_reset_helper_address", submit.MaterialLaneResetHelperAddress },
        { "material_lane_copied_block_populate_address", submit.MaterialLaneCopiedBlockPopulateAddress },
        { "material_lane_blend_source_translate_address", submit.MaterialLaneBlendSourceTranslateAddress },
        { "material_lane_blend_operand_translate_address", submit.MaterialLaneBlendOperandTranslateAddress },
        { "material_lane_blend_equation_translate_address", submit.MaterialLaneBlendEquationTranslateAddress },
        { "material_lane_blend_source_table_pointer_address", submit.MaterialLaneBlendSourceTablePointerAddress },
        { "material_lane_blend_source_table_address", submit.MaterialLaneBlendSourceTableAddress },
        { "material_lane_blend_operand_table_pointer_address", submit.MaterialLaneBlendOperandTablePointerAddress },
        { "material_lane_blend_operand_table_address", submit.MaterialLaneBlendOperandTableAddress },
        { "material_lane_blend_equation_table_pointer_address", submit.MaterialLaneBlendEquationTablePointerAddress },
        { "material_lane_blend_equation_table_address", submit.MaterialLaneBlendEquationTableAddress },
        { "material_lane_blend_float_scale_word_address", submit.MaterialLaneBlendFloatScaleWordAddress },
        { "material_lane_blend_float_scale_word", submit.MaterialLaneBlendFloatScaleWord },
        { "cmb_material_count_offset", submit.CmbMaterialCountOffset },
        { "cmb_material_table_offset", submit.CmbMaterialTableOffset },
        { "cmb_material_record_size_bytes", submit.CmbMaterialRecordSizeBytes },
        { "material_lane_source_material_pointer_offset", submit.MaterialLaneSourceMaterialPointerOffset },
        { "material_lane_runtime_context_pointer_offset", submit.MaterialLaneRuntimeContextPointerOffset },
        { "material_lane_post_material_table_pointer_offset", submit.MaterialLanePostMaterialTablePointerOffset },
        { "material_table_mesh_resource_handle_table_pointer_offset",
          submit.MaterialTableMeshResourceHandleTablePointerOffset },
        { "material_table_lane_base_pointer_offset", submit.MaterialTableLaneBasePointerOffset },
        { "material_table_arena_cursor_pointer_offset", submit.MaterialTableArenaCursorPointerOffset },
        { "material_lane_mesh_resource_handle_table_pointer_offset",
          submit.MaterialLaneMeshResourceHandleTablePointerOffset },
        { "material_lane_post_material_record_table_pointer_offset",
          submit.MaterialLanePostMaterialRecordTablePointerOffset },
        { "material_lane_header_pointers_resolved_from_codebin",
          submit.MaterialLaneHeaderPointersResolvedFromCodeBin },
        { "material_lane_header_pointers_are_direct_cmb_material_data",
          submit.MaterialLaneHeaderPointersAreDirectCmbMaterialData },
        { "material_lane_post_material_table_derived_from_cmb_material_record_tail",
          submit.MaterialLanePostMaterialTableDerivedFromCmbMaterialRecordTail },
        { "material_lane_post_material_table_pointer_shared_by_all_lanes",
          submit.MaterialLanePostMaterialTablePointerSharedByAllLanes },
        { "material_lane_post_material_table_resolved_as_texture_env_table",
          submit.MaterialLanePostMaterialTableResolvedAsTextureEnvTable },
        { "material_lane_post_material_table_base_formula_material_count_offset",
          submit.MaterialLanePostMaterialTableBaseFormulaMaterialCountOffset },
        { "material_lane_post_material_table_base_formula_material_record_size_bytes",
          submit.MaterialLanePostMaterialTableBaseFormulaMaterialRecordSizeBytes },
        { "cmb_material_texture_env_record_size_bytes", submit.CmbMaterialTextureEnvRecordSizeBytes },
        { "cmb_material_texture_env_stage_count_offset", submit.CmbMaterialTextureEnvStageCountOffset },
        { "cmb_material_texture_env_stage_index_offset", submit.CmbMaterialTextureEnvStageIndexOffset },
        { "material_lane_header_pointer_status", submit.MaterialLaneHeaderPointerStatus },
        { "material_lane_copied_block_destination_offset", submit.MaterialLaneCopiedBlockDestinationOffset },
        { "cmb_material_copied_block_source_offset", submit.CmbMaterialCopiedBlockSourceOffset },
        { "material_lane_copied_block_source_pointer_offset", submit.MaterialLaneCopiedBlockSourcePointerOffset },
        { "material_lane_copied_block_minimum_source_bytes", submit.MaterialLaneCopiedBlockMinimumSourceBytes },
        { "material_lane_copied_block_enum_0_translate_address", submit.MaterialLaneCopiedBlockEnum0TranslateAddress },
        { "material_lane_copied_block_float_selector_translate_address",
          submit.MaterialLaneCopiedBlockFloatSelectorTranslateAddress },
        { "material_lane_copied_block_enum_1_translate_address", submit.MaterialLaneCopiedBlockEnum1TranslateAddress },
        { "material_lane_copied_block_enum_2_translate_address", submit.MaterialLaneCopiedBlockEnum2TranslateAddress },
        { "material_lane_copied_block_enum_3_translate_address", submit.MaterialLaneCopiedBlockEnum3TranslateAddress },
        { "material_lane_copied_block_enum_4_translate_address", submit.MaterialLaneCopiedBlockEnum4TranslateAddress },
        { "material_lane_copied_block_pica_lut_input_translate_address",
          submit.MaterialLaneCopiedBlockPicaLutInputTranslateAddress },
        { "material_lane_copied_block_pica_lut_scale_translate_address",
          submit.MaterialLaneCopiedBlockPicaLutScaleTranslateAddress },
        { "material_lane_copied_block_pica_bump_texture_unit_translate_address",
          submit.MaterialLaneCopiedBlockPicaBumpTextureUnitTranslateAddress },
        { "material_lane_copied_block_pica_bump_mode_translate_address",
          submit.MaterialLaneCopiedBlockPicaBumpModeTranslateAddress },
        { "material_lane_copied_block_pica_lighting_config_translate_address",
          submit.MaterialLaneCopiedBlockPicaLightingConfigTranslateAddress },
        { "material_lane_copied_block_local_source_pointer_offset",
          submit.MaterialLaneCopiedBlockLocalSourcePointerOffset },
        { "material_lane_copied_block_local_cleared_byte_offset",
          submit.MaterialLaneCopiedBlockLocalClearedByteOffset },
        { "material_lane_copied_block_local_cleared_byte_count",
          submit.MaterialLaneCopiedBlockLocalClearedByteCount },
        { "material_lane_copied_block_flag_0_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag0SourceLocalOffset },
        { "material_lane_copied_block_enum_0_source_local_offset",
          submit.MaterialLaneCopiedBlockEnum0SourceLocalOffset },
        { "material_lane_copied_block_float_selector_source_local_offset",
          submit.MaterialLaneCopiedBlockFloatSelectorSourceLocalOffset },
        { "material_lane_copied_block_enum_1_source_local_offset",
          submit.MaterialLaneCopiedBlockEnum1SourceLocalOffset },
        { "material_lane_copied_block_enum_2_source_local_offset",
          submit.MaterialLaneCopiedBlockEnum2SourceLocalOffset },
        { "material_lane_copied_block_flag_1_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag1SourceLocalOffset },
        { "material_lane_copied_block_enum_3_source_local_offset",
          submit.MaterialLaneCopiedBlockEnum3SourceLocalOffset },
        { "material_lane_copied_block_enum_4_source_local_offset",
          submit.MaterialLaneCopiedBlockEnum4SourceLocalOffset },
        { "material_lane_copied_block_flag_2_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag2SourceLocalOffset },
        { "material_lane_copied_block_flag_3_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag3SourceLocalOffset },
        { "material_lane_copied_block_flag_4_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag4SourceLocalOffset },
        { "material_lane_copied_block_flag_5_source_local_offset",
          submit.MaterialLaneCopiedBlockFlag5SourceLocalOffset },
        { "material_lane_copied_block_pica_lut_input_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaLutInputSourceLocalOffset },
        { "material_lane_copied_block_pica_lut_scale_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaLutScaleSourceLocalOffset },
        { "material_lane_copied_block_pica_bump_texture_unit_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaBumpTextureUnitSourceLocalOffset },
        { "material_lane_copied_block_pica_bump_mode_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaBumpModeSourceLocalOffset },
        { "material_lane_copied_block_pica_lighting_config_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaLightingConfigSourceLocalOffset },
        { "material_lane_copied_block_flag_0_byte_offset", submit.MaterialLaneCopiedBlockFlag0ByteOffset },
        { "material_lane_copied_block_enum_0_byte_offset", submit.MaterialLaneCopiedBlockEnum0ByteOffset },
        { "material_lane_copied_block_float_selector_byte_offset",
          submit.MaterialLaneCopiedBlockFloatSelectorByteOffset },
        { "material_lane_copied_block_enum_1_byte_offset", submit.MaterialLaneCopiedBlockEnum1ByteOffset },
        { "material_lane_copied_block_enum_2_byte_offset", submit.MaterialLaneCopiedBlockEnum2ByteOffset },
        { "material_lane_copied_block_flag_1_byte_offset", submit.MaterialLaneCopiedBlockFlag1ByteOffset },
        { "material_lane_copied_block_enum_3_byte_offset", submit.MaterialLaneCopiedBlockEnum3ByteOffset },
        { "material_lane_copied_block_enum_4_byte_offset", submit.MaterialLaneCopiedBlockEnum4ByteOffset },
        { "material_lane_copied_block_flag_2_byte_offset", submit.MaterialLaneCopiedBlockFlag2ByteOffset },
        { "material_lane_copied_block_flag_3_byte_offset", submit.MaterialLaneCopiedBlockFlag3ByteOffset },
        { "material_lane_copied_block_flag_4_byte_offset", submit.MaterialLaneCopiedBlockFlag4ByteOffset },
        { "material_lane_copied_block_flag_5_byte_offset", submit.MaterialLaneCopiedBlockFlag5ByteOffset },
        { "material_lane_copied_block_pica_lut_input_byte_offset",
          submit.MaterialLaneCopiedBlockPicaLutInputByteOffset },
        { "material_lane_copied_block_pica_lut_scale_byte_offset",
          submit.MaterialLaneCopiedBlockPicaLutScaleByteOffset },
        { "material_lane_copied_block_pica_bump_texture_unit_byte_offset",
          submit.MaterialLaneCopiedBlockPicaBumpTextureUnitByteOffset },
        { "material_lane_copied_block_pica_bump_mode_byte_offset",
          submit.MaterialLaneCopiedBlockPicaBumpModeByteOffset },
        { "material_lane_copied_block_pica_lighting_config_byte_offset",
          submit.MaterialLaneCopiedBlockPicaLightingConfigByteOffset },
        { "material_lane_copied_block_pica_lut_input_constant_base",
          submit.MaterialLaneCopiedBlockPicaLutInputConstantBase },
        { "material_lane_copied_block_pica_lut_input_constant_count",
          submit.MaterialLaneCopiedBlockPicaLutInputConstantCount },
        { "material_lane_copied_block_pica_texture_unit_constant_base",
          submit.MaterialLaneCopiedBlockPicaTextureUnitConstantBase },
        { "material_lane_copied_block_pica_texture_unit_constant_count",
          submit.MaterialLaneCopiedBlockPicaTextureUnitConstantCount },
        { "material_lane_copied_block_pica_bump_mode_constant_base",
          submit.MaterialLaneCopiedBlockPicaBumpModeConstantBase },
        { "material_lane_copied_block_pica_bump_mode_constant_count",
          submit.MaterialLaneCopiedBlockPicaBumpModeConstantCount },
        { "material_lane_copied_block_pica_lighting_config_constant_base",
          submit.MaterialLaneCopiedBlockPicaLightingConfigConstantBase },
        { "material_lane_copied_block_pica_lighting_config_linear_count",
          submit.MaterialLaneCopiedBlockPicaLightingConfigLinearCount },
        { "material_lane_copied_block_pica_lighting_config_7_constant",
          submit.MaterialLaneCopiedBlockPicaLightingConfig7Constant },
        { "material_lane_copied_block_pica_lighting_config_7_encoded_value",
          submit.MaterialLaneCopiedBlockPicaLightingConfig7EncodedValue },
        { "material_lane_copied_block_pica_lut_input_abs_d0_translate_address",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0TranslateAddress },
        { "material_lane_copied_block_pica_lut_input_abs_d0_source_local_offset",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SourceLocalOffset },
        { "material_lane_copied_block_pica_lut_input_abs_d0_byte_offset",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0ByteOffset },
        { "material_lane_copied_block_pica_lut_input_abs_d0_register",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0Register },
        { "material_lane_copied_block_pica_lut_input_abs_d0_bit_shift",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0BitShift },
        { "material_lane_copied_block_pica_lut_input_abs_d0_native_constant_base",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantBase },
        { "material_lane_copied_block_pica_lut_input_abs_d0_native_constant_count",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0NativeConstantCount },
        { "material_lane_copied_block_pica_lut_input_abs_d0_disable_bit_by_selector",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0DisableBitBySelector },
        { "material_lane_copied_block_pica_lut_input_abs_d0_register_name",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0RegisterName },
        { "material_lane_copied_block_pica_lut_input_abs_d0_field_name",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0FieldName },
        { "material_lane_copied_block_pica_lut_input_abs_d0_sampler_name",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0SamplerName },
        { "material_lane_copied_block_pica_lut_input_abs_d0_packing_rule",
          submit.MaterialLaneCopiedBlockPicaLutInputAbsD0PackingRule },
        { "material_lane_copied_block_pica_config_packet_submit_wrapper_address",
          submit.MaterialLaneCopiedBlockPicaConfigPacketSubmitWrapperAddress },
        { "material_lane_copied_block_pica_config_packet_emitter_address",
          submit.MaterialLaneCopiedBlockPicaConfigPacketEmitterAddress },
        { "material_lane_copied_block_pica_config_packet_followup_emitter_address",
          submit.MaterialLaneCopiedBlockPicaConfigPacketFollowupEmitterAddress },
        { "material_lane_copied_block_pica_config_packet_word_count",
          submit.MaterialLaneCopiedBlockPicaConfigPacketWordCount },
        { "material_lane_copied_block_pica_config_booleans_invert_clamp01",
          submit.MaterialLaneCopiedBlockPicaConfigBooleansInvertClamp01 },
        { "material_lane_copied_block_pica_config_packet_headers",
          submit.MaterialLaneCopiedBlockPicaConfigPacketHeaders },
        { "material_lane_copied_block_pica_config_boolean_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigBooleanByteOffsets },
        { "material_lane_copied_block_pica_config_boolean_bit_shifts",
          submit.MaterialLaneCopiedBlockPicaConfigBooleanBitShifts },
        { "material_lane_copied_block_pica_config_primary_nibble_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigPrimaryNibbleByteOffsets },
        { "material_lane_copied_block_pica_config_secondary_nibble_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigSecondaryNibbleByteOffsets },
        { "material_lane_copied_block_pica_config_nibble_bit_shifts",
          submit.MaterialLaneCopiedBlockPicaConfigNibbleBitShifts },
        { "material_lane_copied_block_pica_config_registers",
          submit.MaterialLaneCopiedBlockPicaConfigRegisters },
        { "material_lane_copied_block_pica_config_register_names",
          submit.MaterialLaneCopiedBlockPicaConfigRegisterNames },
        { "material_lane_copied_block_pica_config_sampler_order",
          submit.MaterialLaneCopiedBlockPicaConfigSamplerOrder },
        { "material_lane_raw_field_consumer_offset_scan_output_path",
          submit.MaterialLaneRawFieldConsumerOffsetScanOutputPath },
        { "material_lane_raw_field_consumer_export_path",
          submit.MaterialLaneRawFieldConsumerExportPath },
        { "material_lane_raw_field_offset_access_scan_row_count",
          submit.MaterialLaneRawFieldOffsetAccessScanRowCount },
        { "material_lane_raw_field_byte_consumer_candidate_count",
          submit.MaterialLaneRawFieldByteConsumerCandidateCount },
        { "material_lane_raw_field_true_consumer_function_addresses",
          submit.MaterialLaneRawFieldTrueConsumerFunctionAddresses },
        { "material_lane_raw_field_producer_function_addresses",
          submit.MaterialLaneRawFieldProducerFunctionAddresses },
        { "material_lane_raw_field_false_positive_function_addresses",
          submit.MaterialLaneRawFieldFalsePositiveFunctionAddresses },
        { "material_lane_copied_block_pica_config_registers_resolved_from_codebin",
          submit.MaterialLaneCopiedBlockPicaConfigRegistersResolvedFromCodeBin },
        { "material_lane_copied_block_pica_config_emitter_consumes_lane_byte_offsets",
          submit.MaterialLaneCopiedBlockPicaConfigEmitterConsumesLaneByteOffsets },
        { "material_lane_copied_block_pica_config_offset_arrays_are_lane_relative",
          submit.MaterialLaneCopiedBlockPicaConfigOffsetArraysAreLaneRelative },
        { "material_lane_copied_block_pica_lut_input_packing_resolved",
          submit.MaterialLaneCopiedBlockPicaLutInputPackingResolved },
        { "material_lane_copied_block_pica_62c0_selector_final_semantic_resolved",
          submit.MaterialLaneCopiedBlockPica62C0SelectorFinalSemanticResolved },
        { "cmb_material_blend_mode_byte_offset", submit.CmbMaterialBlendModeByteOffset },
        { "cmb_material_blend_mode_enabled_value", submit.CmbMaterialBlendModeEnabledValue },
        { "material_lane_blend_enabled_byte_offset", submit.MaterialLaneBlendEnabledByteOffset },
        { "cmb_material_blend_0_source_offset", submit.CmbMaterialBlend0SourceOffset },
        { "cmb_material_blend_0_operand_offset", submit.CmbMaterialBlend0OperandOffset },
        { "cmb_material_blend_0_equation_offset", submit.CmbMaterialBlend0EquationOffset },
        { "cmb_material_blend_1_source_offset", submit.CmbMaterialBlend1SourceOffset },
        { "cmb_material_blend_1_operand_offset", submit.CmbMaterialBlend1OperandOffset },
        { "cmb_material_blend_1_equation_offset", submit.CmbMaterialBlend1EquationOffset },
        { "cmb_material_blend_color_float_base_offset", submit.CmbMaterialBlendColorFloatBaseOffset },
        { "cmb_material_blend_color_float_count", submit.CmbMaterialBlendColorFloatCount },
        { "material_lane_blend_0_source_byte_offset", submit.MaterialLaneBlend0SourceByteOffset },
        { "material_lane_blend_0_operand_byte_offset", submit.MaterialLaneBlend0OperandByteOffset },
        { "material_lane_blend_0_equation_byte_offset", submit.MaterialLaneBlend0EquationByteOffset },
        { "material_lane_blend_1_source_byte_offset", submit.MaterialLaneBlend1SourceByteOffset },
        { "material_lane_blend_1_operand_byte_offset", submit.MaterialLaneBlend1OperandByteOffset },
        { "material_lane_blend_1_equation_byte_offset", submit.MaterialLaneBlend1EquationByteOffset },
        { "material_lane_blend_color_byte_base_offset", submit.MaterialLaneBlendColorByteBaseOffset },
        { "material_lane_blend_color_byte_count", submit.MaterialLaneBlendColorByteCount },
        { "vtable_lane_select_slot_offset", submit.VtableLaneSelectSlotOffset },
        { "vtable_mesh_prepare_slot_offset", submit.VtableMeshPrepareSlotOffset },
        { "vtable_primitive_packet_slot_offset", submit.VtablePrimitivePacketSlotOffset },
        { "vtable_mesh_draw_slot_offset", submit.VtableMeshDrawSlotOffset },
        { "vtable_material_draw_slot_offset", submit.VtableMaterialDrawSlotOffset },
        { "vtable_material_pre_draw_slot_offset", submit.VtableMaterialPreDrawSlotOffset },
        { "lazy_descriptor_owner_address", submit.LazyDescriptorOwnerAddress },
        { "lazy_descriptor_owner_allocator_singleton_pointer_address",
          submit.LazyDescriptorOwnerAllocatorSingletonPointerAddress },
        { "lazy_descriptor_owner_allocator_singleton_address",
          submit.LazyDescriptorOwnerAllocatorSingletonAddress },
        { "lazy_descriptor_owner_descriptor_table_pointer_address",
          submit.LazyDescriptorOwnerDescriptorTablePointerAddress },
        { "lazy_descriptor_owner_descriptor_table_address", submit.LazyDescriptorOwnerDescriptorTableAddress },
        { "lazy_descriptor_owner_slot_table_pointer_address", submit.LazyDescriptorOwnerSlotTablePointerAddress },
        { "lazy_descriptor_owner_slot_table_address", submit.LazyDescriptorOwnerSlotTableAddress },
        { "lazy_descriptor_owner_allocator_tag_pointer_address",
          submit.LazyDescriptorOwnerAllocatorTagPointerAddress },
        { "lazy_descriptor_owner_allocator_tag_address", submit.LazyDescriptorOwnerAllocatorTagAddress },
        { "lazy_descriptor_owner_generic_allocation_type_pointer_address",
          submit.LazyDescriptorOwnerGenericAllocationTypePointerAddress },
        { "lazy_descriptor_owner_generic_allocation_type_id",
          submit.LazyDescriptorOwnerGenericAllocationTypeId },
        { "lazy_descriptor_owner_special_allocation_type_id",
          submit.LazyDescriptorOwnerSpecialAllocationTypeId },
        { "lazy_descriptor_owner_allocation_size_bytes", submit.LazyDescriptorOwnerAllocationSizeBytes },
        { "lazy_descriptor_owner_special_initializer_address",
          submit.LazyDescriptorOwnerSpecialInitializerAddress },
        { "lazy_descriptor_owner_generic_initializer_address",
          submit.LazyDescriptorOwnerGenericInitializerAddress },
        { "lazy_descriptor_owner_materializer_address", submit.LazyDescriptorOwnerMaterializerAddress },
        { "lazy_descriptor_owner_binding_address", submit.LazyDescriptorOwnerBindingAddress },
        { "lazy_descriptor_owner_provider_resolver_address",
          submit.LazyDescriptorOwnerProviderResolverAddress },
        { "lazy_descriptor_owner_provider_context_word_offset",
          submit.LazyDescriptorOwnerProviderContextWordOffset },
        { "lazy_descriptor_owner_pointer_store_index_bias",
          submit.LazyDescriptorOwnerPointerStoreIndexBias },
        { "lazy_descriptor_owner_binding_slot_count", submit.LazyDescriptorOwnerBindingSlotCount },
        { "lazy_descriptor_owner_binding_record_stride_bytes",
          submit.LazyDescriptorOwnerBindingRecordStrideBytes },
        { "lazy_descriptor_owner_binding_sentinel_byte_value",
          submit.LazyDescriptorOwnerBindingSentinelByteValue },
        { "lazy_descriptor_owner_special_descriptor_ids", submit.LazyDescriptorOwnerSpecialDescriptorIds },
        { "lazy_descriptor_owner_resolved", submit.LazyDescriptorOwnerResolved },
        { "lazy_descriptor_owner_directly_writes_packet_prep_source",
          submit.LazyDescriptorOwnerDirectlyWritesPacketPrepSource },
        { "lazy_descriptor_owner_status", submit.LazyDescriptorOwnerStatus },
    };
}

nlohmann::json NativeKankyoGameplayDrawCallsiteJson(const NativeKankyoGameplayDrawCallsiteContract& callsite) {
    return {
        { "role", callsite.Role },
        { "callsite_address", callsite.CallsiteAddress },
        { "function_address", callsite.FunctionAddress },
        { "has_gate", callsite.HasGate },
        { "gate_byte_offset", callsite.GateByteOffset },
        { "gate_expected_value", callsite.GateExpectedValue },
        { "has_second_argument", callsite.HasSecondArgument },
        { "second_argument_value", callsite.SecondArgumentValue },
    };
}

nlohmann::json NativeKankyoGameplayDrawSequenceJson(const NativeKankyoGameplayDrawSequenceContract& sequence) {
    nlohmann::json callsites = nlohmann::json::array();
    for (const auto& callsite : sequence.Callsites) {
        callsites.push_back(NativeKankyoGameplayDrawCallsiteJson(callsite));
    }

    return {
        { "function_address", sequence.FunctionAddress },
        { "tail_branch_address", sequence.TailBranchAddress },
        { "tail_branch_target_address", sequence.TailBranchTargetAddress },
        { "callsites", callsites },
    };
}

nlohmann::json NativeKankyoRenderRecordDrainWrapperJson(
    const NativeKankyoRenderRecordDrainWrapperContract& wrapper) {
    return {
        { "function_address", wrapper.FunctionAddress },
        { "function_end_address", wrapper.FunctionEndAddress },
        { "category", wrapper.Category },
        { "has_pre_drain_setup", wrapper.HasPreDrainSetup },
    };
}

nlohmann::json NativeKankyoRenderRecordSchedulerJson(
    const NativeKankyoRenderRecordSchedulerContract& scheduler) {
    nlohmann::json wrappers = nlohmann::json::array();
    for (const auto& wrapper : scheduler.DrainWrappers) {
        wrappers.push_back(NativeKankyoRenderRecordDrainWrapperJson(wrapper));
    }

    return {
        { "function_address", scheduler.FunctionAddress },
        { "function_end_address", scheduler.FunctionEndAddress },
        { "count_base_offset", scheduler.CountBaseOffset },
        { "count_stride_bytes", scheduler.CountStrideBytes },
        { "category_record_stride_bytes", scheduler.CategoryRecordStrideBytes },
        { "runtime_pointer_array_offset", scheduler.RuntimePointerArrayOffset },
        { "aux_value_array_offset", scheduler.AuxValueArrayOffset },
        { "max_accepted_record_count", scheduler.MaxAcceptedRecordCount },
        { "post_insert_callback_address", scheduler.PostInsertCallbackAddress },
        { "sort_function_address", scheduler.SortFunctionAddress },
        { "sort_function_end_address", scheduler.SortFunctionEndAddress },
        { "drain_function_address", scheduler.DrainFunctionAddress },
        { "drain_function_end_address", scheduler.DrainFunctionEndAddress },
        { "draw_handle_count_base_offset", scheduler.DrawHandleCountBaseOffset },
        { "draw_handle_pointer_array_offset", scheduler.DrawHandlePointerArrayOffset },
        { "draw_handle_pointer_draw_handle_offset", scheduler.DrawHandlePointerDrawHandleOffset },
        { "draw_handle_submit_function_address", scheduler.DrawHandleSubmitFunctionAddress },
        { "draw_handle_submit_pass_values", scheduler.DrawHandleSubmitPassValues },
        { "runtime_dispatch_vtable_slot_offset", scheduler.RuntimeDispatchVtableSlotOffset },
        { "runtime_1e4_vtable_address", scheduler.Runtime1E4VtableAddress },
        { "runtime_1e4_dispatch_vtable_entry_address",
          scheduler.Runtime1E4DispatchVtableEntryAddress },
        { "runtime_1e4_dispatch_function_address", scheduler.Runtime1E4DispatchFunctionAddress },
        { "runtime_1e4_dispatch_function_end_address",
          scheduler.Runtime1E4DispatchFunctionEndAddress },
        { "runtime_1e4_dispatch_global_gate_pointer_literal_address",
          scheduler.Runtime1E4DispatchGlobalGatePointerLiteralAddress },
        { "runtime_1e4_dispatch_global_gate_address",
          scheduler.Runtime1E4DispatchGlobalGateAddress },
        { "runtime_1e4_dispatch_flag_masks", scheduler.Runtime1E4DispatchFlagMasks },
        { "runtime_1e4_dispatch_global_gate_values",
          scheduler.Runtime1E4DispatchGlobalGateValues },
        { "runtime_container_pointer_offset", scheduler.RuntimeContainerPointerOffset },
        { "runtime_container_submit_vtable_address",
          scheduler.RuntimeContainerSubmitVtableAddress },
        { "runtime_container_submit_vtable_entry_address",
          scheduler.RuntimeContainerSubmitVtableEntryAddress },
        { "runtime_container_submit_vtable_slot_offset",
          scheduler.RuntimeContainerSubmitVtableSlotOffset },
        { "runtime_container_submit_function_address",
          scheduler.RuntimeContainerSubmitFunctionAddress },
        { "runtime_container_submit_effect_draw_function_address",
          scheduler.RuntimeContainerSubmitEffectDrawFunctionAddress },
        { "drain_wrappers", wrappers },
        { "writes_runtime_pointer_and_aux_value", scheduler.WritesRuntimePointerAndAuxValue },
        { "returns_false_on_overflow", scheduler.ReturnsFalseOnOverflow },
        { "sorts_ascending_by_aux_value", scheduler.SortsAscendingByAuxValue },
        { "sort_moves_runtime_pointer_with_aux_value", scheduler.SortMovesRuntimePointerWithAuxValue },
        { "drains_draw_handles_before_runtime_vtable_dispatch",
          scheduler.DrainsDrawHandlesBeforeRuntimeVtableDispatch },
        { "runtime_dispatch_uses_vtable_slot", scheduler.RuntimeDispatchUsesVtableSlot },
        { "runtime_1e4_dispatch_passes_through_container_submit",
          scheduler.Runtime1E4DispatchPassesThroughContainerSubmit },
        { "drain_connects_to_effect_draw_consumer", scheduler.DrainConnectsToEffectDrawConsumer },
        { "drain_connects_to_type6_draw_command", scheduler.DrainConnectsToType6DrawCommand },
    };
}

nlohmann::json NativeKankyoEnvironmentVectorJson(const NativeKankyoEnvironmentVectorContract& envVector) {
    return {
        { "prep_function_address", envVector.PrepFunctionAddress },
        { "prep_function_end_address", envVector.PrepFunctionEndAddress },
        { "prep_global_angle_state_address", envVector.PrepGlobalAngleStateAddress },
        { "prep_global_angle_halfword_offset", envVector.PrepGlobalAngleHalfwordOffset },
        { "prep_global_environment_state_address", envVector.PrepGlobalEnvironmentStateAddress },
        { "prep_global_vector_scale_float_offset", envVector.PrepGlobalVectorScaleFloatOffset },
        { "prep_output_vector_x_offset", envVector.PrepOutputVectorXOffset },
        { "prep_output_vector_y_offset", envVector.PrepOutputVectorYOffset },
        { "prep_output_vector_z_offset", envVector.PrepOutputVectorZOffset },
        { "prep_vector_submit_target_offset", envVector.PrepVectorSubmitTargetOffset },
        { "prep_vector_submit_context_offset", envVector.PrepVectorSubmitContextOffset },
        { "prep_smoothing_gate_address", envVector.PrepSmoothingGateAddress },
        { "prep_smoothing_helper_address", envVector.PrepSmoothingHelperAddress },
        { "prep_sin_helper_address", envVector.PrepSinHelperAddress },
        { "prep_cos_helper_address", envVector.PrepCosHelperAddress },
        { "prep_submit_positive_vector_address", envVector.PrepSubmitPositiveVectorAddress },
        { "prep_submit_negative_vector_address", envVector.PrepSubmitNegativeVectorAddress },
        { "resolver_function_address", envVector.ResolverFunctionAddress },
        { "resolver_function_end_address", envVector.ResolverFunctionEndAddress },
        { "resolver_source_object_pointer_offset", envVector.ResolverSourceObjectPointerOffset },
        { "resolver_source_table_helper_address", envVector.ResolverSourceTableHelperAddress },
        { "resolver_source_table_helper_index", envVector.ResolverSourceTableHelperIndex },
        { "resolver_source_record_count", envVector.ResolverSourceRecordCount },
        { "resolver_source_record_stride_bytes", envVector.ResolverSourceRecordStrideBytes },
        { "resolver_source_component_count", envVector.ResolverSourceComponentCount },
        { "resolver_output_record_stride_bytes", envVector.ResolverOutputRecordStrideBytes },
        { "resolver_output_component_count", envVector.ResolverOutputComponentCount },
        { "resolver_output_alpha_word", envVector.ResolverOutputAlphaWord },
        { "resolver_state_byte_offset", envVector.ResolverStateByteOffset },
        { "resolver_target_light_setting_offset", envVector.ResolverTargetLightSettingOffset },
        { "resolver_target_light_setting_invalid_value", envVector.ResolverTargetLightSettingInvalidValue },
        { "resolver_active_or_target_record_index", envVector.ResolverActiveOrTargetRecordIndex },
        { "resolver_fallback_global_state_address", envVector.ResolverFallbackGlobalStateAddress },
        { "resolver_fallback_blend_from_index_offset", envVector.ResolverFallbackBlendFromIndexOffset },
        { "resolver_fallback_blend_to_index_offset", envVector.ResolverFallbackBlendToIndexOffset },
        { "resolver_fallback_blend_weight_float_offset", envVector.ResolverFallbackBlendWeightFloatOffset },
        { "resolver_color_pack_scale_word", envVector.ResolverColorPackScaleWord },
        { "resolver_packed_color_output_offset", envVector.ResolverPackedColorOutputOffset },
        { "resolver_packed_color_mode_byte_offset", envVector.ResolverPackedColorModeByteOffset },
        { "resolver_packed_color_mode_value", envVector.ResolverPackedColorModeValue },
        { "resolver_packed_color_callsite_addresses", envVector.ResolverPackedColorCallsiteAddresses },
    };
}

nlohmann::json NativeKankyoEnvironmentLightSettingStateJson(
    const NativeKankyoEnvironmentLightSettingStateContract& state) {
    return {
        { "surface_type_getter_address", state.SurfaceTypeGetterAddress },
        { "surface_type_field_reader_address", state.SurfaceTypeFieldReaderAddress },
        { "surface_type_light_setting_field_id", state.SurfaceTypeLightSettingFieldId },
        { "surface_type_raw_index_left_shift", state.SurfaceTypeRawIndexLeftShift },
        { "surface_type_raw_index_right_shift", state.SurfaceTypeRawIndexRightShift },
        { "player_floor_light_setting_getter_callsite_address", state.PlayerFloorLightSettingGetterCallsiteAddress },
        { "player_floor_change_helper_callsite_address", state.PlayerFloorChangeHelperCallsiteAddress },
        { "change_helper_address", state.ChangeHelperAddress },
        { "transition_reset_helper_address", state.TransitionResetHelperAddress },
        { "transition_reset_caller_address", state.TransitionResetCallerAddress },
        { "request_helper_address", state.RequestHelperAddress },
        { "camera_water_request_callsite_address", state.CameraWaterRequestCallsiteAddress },
        { "play_environment_base_offset", state.PlayEnvironmentBaseOffset },
        { "environment_state_byte_offset", state.EnvironmentStateByteOffset },
        { "transition_state_byte_offset", state.TransitionStateByteOffset },
        { "current_light_setting_offset", state.CurrentLightSettingOffset },
        { "previous_light_setting_offset", state.PreviousLightSettingOffset },
        { "target_light_setting_offset", state.TargetLightSettingOffset },
        { "blend_weight_float_offset", state.BlendWeightFloatOffset },
        { "normalize_threshold", state.NormalizeThreshold },
        { "normalize_fallback_value", state.NormalizeFallbackValue },
        { "target_invalid_value", state.TargetInvalidValue },
        { "blend_weight_zero_word", state.BlendWeightZeroWord },
        { "blend_weight_one_word", state.BlendWeightOneWord },
        { "fallback_global_state_address", state.FallbackGlobalStateAddress },
        { "fallback_global_previous_byte_offset", state.FallbackGlobalPreviousByteOffset },
        { "fallback_mirror_current_byte_offset", state.FallbackMirrorCurrentByteOffset },
        { "fallback_mirror_previous_byte_offset", state.FallbackMirrorPreviousByteOffset },
        { "decompile_consumer_scan_candidate_count", state.DecompileConsumerScanCandidateCount },
        { "decompile_consumer_scan_found_final_scene_packet_consumer",
          state.DecompileConsumerScanFoundFinalScenePacketConsumer },
        { "final_scene_packet_consumer_resolved", state.FinalScenePacketConsumerResolved },
        { "request_helper_callsite_addresses", state.RequestHelperCallsiteAddresses },
        { "state_machine_helper_candidate_addresses", state.StateMachineHelperCandidateAddresses },
        { "excluded_actor_or_cutscene_writer_candidate_addresses",
          state.ExcludedActorOrCutsceneWriterCandidateAddresses },
    };
}

nlohmann::json NativePicaShadowDepthRegisterStateJson(
    const NativePicaShadowDepthRegisterStateContract& shadow) {
    return {
        { "source_kind", shadow.SourceKind },
        { "pica_state_initializer_address", shadow.PicaStateInitializerAddress },
        { "pica_state_initializer_callsite_address", shadow.PicaStateInitializerCallsiteAddress },
        { "generic_packet_initializer_address", shadow.GenericPacketInitializerAddress },
        { "texture_descriptor_initializer_address", shadow.TextureDescriptorInitializerAddress },
        { "descriptor_materialization_address", shadow.DescriptorMaterializationAddress },
        { "descriptor_binding_address", shadow.DescriptorBindingAddress },
        { "descriptor_packet_reset_address", shadow.DescriptorPacketResetAddress },
        { "descriptor_packet_object_size_bytes", shadow.DescriptorPacketObjectSizeBytes },
        { "descriptor_packet_record_pointer_like_word_offset",
          shadow.DescriptorPacketRecordPointerLikeWordOffset },
        { "descriptor_packet_record_pointer_like_next_word_offset",
          shadow.DescriptorPacketRecordPointerLikeNextWordOffset },
        { "pica_state_word_stride_bytes", shadow.PicaStateWordStrideBytes },
        { "pica_register_index_is_state_word_index", shadow.PicaRegisterIndexIsStateWordIndex },
        { "depth_map_scale_register", shadow.DepthMapScaleRegister },
        { "depth_map_offset_register", shadow.DepthMapOffsetRegister },
        { "depth_map_enable_register", shadow.DepthMapEnableRegister },
        { "texunit0_shadow_register", shadow.Texunit0ShadowRegister },
        { "fragop_shadow_register", shadow.FragopShadowRegister },
        { "depth_map_scale_state_word_index", shadow.DepthMapScaleStateWordIndex },
        { "depth_map_offset_state_word_index", shadow.DepthMapOffsetStateWordIndex },
        { "depth_map_enable_state_word_index", shadow.DepthMapEnableStateWordIndex },
        { "texunit0_shadow_state_word_index", shadow.Texunit0ShadowStateWordIndex },
        { "fragop_shadow_state_word_index", shadow.FragopShadowStateWordIndex },
        { "generic_pica_scalar_writer_address", shadow.GenericPicaScalarWriterAddress },
        { "generic_pica_scalar_writer_scanned_callsite_count",
          shadow.GenericPicaScalarWriterScannedCallsiteCount },
        { "generic_pica_scalar_writer_fragop_shadow_match_count",
          shadow.GenericPicaScalarWriterFragopShadowMatchCount },
        { "generic_pica_scalar_writer_dynamic_table_callsite_count",
          shadow.GenericPicaScalarWriterDynamicTableCallsiteCount },
        { "generic_pica_scalar_writer_dynamic_table_resolved_count",
          shadow.GenericPicaScalarWriterDynamicTableResolvedCount },
        { "generic_pica_scalar_writer_dynamic_table_pointer_addresses",
          shadow.GenericPicaScalarWriterDynamicTablePointerAddresses },
        { "generic_pica_scalar_writer_dynamic_table_addresses",
          shadow.GenericPicaScalarWriterDynamicTableAddresses },
        { "generic_pica_scalar_writer_dynamic_register_bases",
          shadow.GenericPicaScalarWriterDynamicRegisterBases },
        { "generic_pica_scalar_writer_scan_source", shadow.GenericPicaScalarWriterScanSource },
        { "generic_pica_vector_uniform_writer_address",
          shadow.GenericPicaVectorUniformWriterAddress },
        { "generic_pica_vector_uniform_writer_scanned_callsite_count",
          shadow.GenericPicaVectorUniformWriterScannedCallsiteCount },
        { "generic_pica_vector_uniform_writer_fragop_shadow_match_count",
          shadow.GenericPicaVectorUniformWriterFragopShadowMatchCount },
        { "generic_pica_vector_uniform_writer_index_register",
          shadow.GenericPicaVectorUniformWriterIndexRegister },
        { "generic_pica_vector_uniform_writer_data_register",
          shadow.GenericPicaVectorUniformWriterDataRegister },
        { "generic_pica_vector_uniform_writer_index_command_header",
          shadow.GenericPicaVectorUniformWriterIndexCommandHeader },
        { "generic_pica_vector_uniform_writer_data_command_header",
          shadow.GenericPicaVectorUniformWriterDataCommandHeader },
        { "generic_pica_vector_uniform_writer_dynamic_index_table_pointer_addresses",
          shadow.GenericPicaVectorUniformWriterDynamicIndexTablePointerAddresses },
        { "generic_pica_vector_uniform_writer_dynamic_index_table_addresses",
          shadow.GenericPicaVectorUniformWriterDynamicIndexTableAddresses },
        { "generic_pica_vector_uniform_writer_dynamic_uniform_indices",
          shadow.GenericPicaVectorUniformWriterDynamicUniformIndices },
        { "generic_pica_vector_uniform_writer_scan_source",
          shadow.GenericPicaVectorUniformWriterScanSource },
        { "direct_pica_command_writer_commit_address", shadow.DirectPicaCommandWriterCommitAddress },
        { "direct_pica_command_writer_scanned_commit_ref_count",
          shadow.DirectPicaCommandWriterScannedCommitRefCount },
        { "direct_pica_command_writer_known_header_store_count",
          shadow.DirectPicaCommandWriterKnownHeaderStoreCount },
        { "direct_pica_command_writer_shadow_depth_header_store_count",
          shadow.DirectPicaCommandWriterShadowDepthHeaderStoreCount },
        { "direct_pica_command_writer_fragop_shadow_match_count",
          shadow.DirectPicaCommandWriterFragopShadowMatchCount },
        { "direct_pica_command_writer_scan_source", shadow.DirectPicaCommandWriterScanSource },
        { "pica_packet_copy_helper_address", shadow.PicaPacketCopyHelperAddress },
        { "pica_packet_copy_scanned_callsite_count", shadow.PicaPacketCopyScannedCallsiteCount },
        { "pica_packet_copy_resolved_static_packet_count",
          shadow.PicaPacketCopyResolvedStaticPacketCount },
        { "pica_packet_copy_fragop_shadow_match_count", shadow.PicaPacketCopyFragopShadowMatchCount },
        { "pica_packet_copy_static_packet_addresses", shadow.PicaPacketCopyStaticPacketAddresses },
        { "pica_packet_copy_static_packet_registers", shadow.PicaPacketCopyStaticPacketRegisters },
        { "pica_packet_copy_scan_source", shadow.PicaPacketCopyScanSource },
        { "static_fragop_shadow_command_header_literal_match_count",
          shadow.StaticFragopShadowCommandHeaderLiteralMatchCount },
        { "captured_fragop_shadow_register_write_count",
          shadow.CapturedFragopShadowRegisterWriteCount },
        { "captured_fragop_shadow_snapshot_raw_value", shadow.CapturedFragopShadowSnapshotRawValue },
        { "captured_fragop_shadow_snapshot_is_preexisting_state",
          shadow.CapturedFragopShadowSnapshotIsPreexistingState },
        { "pica_state_initializer_highest_verified_word_index",
          shadow.PicaStateInitializerHighestVerifiedWordIndex },
        { "pica_state_initializer_writes_fragop_shadow",
          shadow.PicaStateInitializerWritesFragopShadow },
        { "fragop_shadow_word_offset_access_scan_row_count",
          shadow.FragopShadowWordOffsetAccessScanRowCount },
        { "fragop_shadow_word_offset_access_scan_pica_writer_match_count",
          shadow.FragopShadowWordOffsetAccessScanPicaWriterMatchCount },
        { "fragop_shadow_word_offset_access_scan_source",
          shadow.FragopShadowWordOffsetAccessScanSource },
        { "fragop_shadow_source_requires_first_write_trace",
          shadow.FragopShadowSourceRequiresFirstWriteTrace },
        { "initial_default_command_list_emit_function_address",
          shadow.InitialDefaultCommandListEmitFunctionAddress },
        { "initial_default_command_list_emit_function_end_address",
          shadow.InitialDefaultCommandListEmitFunctionEndAddress },
        { "initial_default_command_list_emit_slot_count",
          shadow.InitialDefaultCommandListEmitSlotCount },
        { "initial_default_command_list_emit_payload_base_offset",
          shadow.InitialDefaultCommandListEmitPayloadBaseOffset },
        { "initial_default_command_list_emit_mask_byte_base_offset",
          shadow.InitialDefaultCommandListEmitMaskByteBaseOffset },
        { "initial_default_command_list_emit_header_table_address",
          shadow.InitialDefaultCommandListEmitHeaderTableAddress },
        { "initial_default_command_list_emit_fragop_shadow_header_table_slot_index",
          shadow.InitialDefaultCommandListEmitFragopShadowHeaderTableSlotIndex },
        { "initial_default_command_list_emit_fragop_shadow_payload_offset",
          shadow.InitialDefaultCommandListEmitFragopShadowPayloadOffset },
        { "initial_default_command_list_emit_fragop_shadow_mask_byte_offset",
          shadow.InitialDefaultCommandListEmitFragopShadowMaskByteOffset },
        { "initial_default_command_list_emit_fragop_shadow_command_header",
          shadow.InitialDefaultCommandListEmitFragopShadowCommandHeader },
        { "initial_default_command_list_emit_fragop_shadow_mask",
          shadow.InitialDefaultCommandListEmitFragopShadowMask },
        { "initial_default_command_list_emit_fragop_shadow_payload_raw_value",
          shadow.InitialDefaultCommandListEmitFragopShadowPayloadRawValue },
        { "initial_default_command_list_emit_formula",
          shadow.InitialDefaultCommandListEmitFormula },
        { "initial_default_command_list_emit_trace_source",
          shadow.InitialDefaultCommandListEmitTraceSource },
        { "final_register_flush_function_address", shadow.FinalRegisterFlushFunctionAddress },
        { "final_register_flush_function_end_address", shadow.FinalRegisterFlushFunctionEndAddress },
        { "final_register_flush_dirty_bit_base_offset",
          shadow.FinalRegisterFlushDirtyBitBaseOffset },
        { "final_register_flush_state_value_base_offset",
          shadow.FinalRegisterFlushStateValueBaseOffset },
        { "final_register_flush_mask_byte_base_offset",
          shadow.FinalRegisterFlushMaskByteBaseOffset },
        { "final_register_flush_shadow_copy_base_offset",
          shadow.FinalRegisterFlushShadowCopyBaseOffset },
        { "final_register_flush_command_cursor_global_address",
          shadow.FinalRegisterFlushCommandCursorGlobalAddress },
        { "final_register_flush_command_end_global_address",
          shadow.FinalRegisterFlushCommandEndGlobalAddress },
        { "final_register_flush_header_table_address",
          shadow.FinalRegisterFlushHeaderTableAddress },
        { "final_register_flush_header_table_source_pairs_address",
          shadow.FinalRegisterFlushHeaderTableSourcePairsAddress },
        { "final_register_flush_header_table_source_pair_stride_bytes",
          shadow.FinalRegisterFlushHeaderTableSourcePairStrideBytes },
        { "final_register_flush_header_table_stride_bytes",
          shadow.FinalRegisterFlushHeaderTableStrideBytes },
        { "final_register_flush_register_list_sentinel",
          shadow.FinalRegisterFlushRegisterListSentinel },
        { "final_register_flush_fragop_shadow_register_list_address",
          shadow.FinalRegisterFlushFragopShadowRegisterListAddress },
        { "final_register_flush_fragop_shadow_header_table_slot_index",
          shadow.FinalRegisterFlushFragopShadowHeaderTableSlotIndex },
        { "final_register_flush_fragop_shadow_state_value_offset",
          shadow.FinalRegisterFlushFragopShadowStateValueOffset },
        { "final_register_flush_fragop_shadow_mask_byte_offset",
          shadow.FinalRegisterFlushFragopShadowMaskByteOffset },
        { "final_register_flush_fragop_shadow_shadow_copy_offset",
          shadow.FinalRegisterFlushFragopShadowShadowCopyOffset },
        { "final_register_flush_fragop_shadow_dirty_word_offset",
          shadow.FinalRegisterFlushFragopShadowDirtyWordOffset },
        { "final_register_flush_fragop_shadow_group_flag_mask",
          shadow.FinalRegisterFlushFragopShadowGroupFlagMask },
        { "final_register_flush_fragop_shadow_register_dirty_bit_mask",
          shadow.FinalRegisterFlushFragopShadowRegisterDirtyBitMask },
        { "final_register_flush_fragop_shadow_command_header",
          shadow.FinalRegisterFlushFragopShadowCommandHeader },
        { "final_register_flush_fragop_shadow_mask",
          shadow.FinalRegisterFlushFragopShadowMask },
        { "final_register_flush_fragop_shadow_payload_raw_value",
          shadow.FinalRegisterFlushFragopShadowPayloadRawValue },
        { "fragop_shadow_live_state_setter_address",
          shadow.FragopShadowLiveStateSetterAddress },
        { "fragop_shadow_live_state_setter_end_address",
          shadow.FragopShadowLiveStateSetterEndAddress },
        { "fragop_shadow_live_state_setter_encoded_register_flag",
          shadow.FragopShadowLiveStateSetterEncodedRegisterFlag },
        { "fragop_shadow_live_state_setter_selector_shift",
          shadow.FragopShadowLiveStateSetterSelectorShift },
        { "fragop_shadow_live_state_setter_case_0x1f",
          shadow.FragopShadowLiveStateSetterCase1f },
        { "fragop_shadow_live_state_setter_case_0x20",
          shadow.FragopShadowLiveStateSetterCase20 },
        { "fragop_shadow_live_state_setter_case_0x1f_saved_float_offset",
          shadow.FragopShadowLiveStateSetterCase1fSavedFloatOffset },
        { "fragop_shadow_live_state_setter_case_0x20_saved_float_offset",
          shadow.FragopShadowLiveStateSetterCase20SavedFloatOffset },
        { "fragop_shadow_live_state_setter_state_value_offset",
          shadow.FragopShadowLiveStateSetterStateValueOffset },
        { "fragop_shadow_live_state_setter_mask_byte_offset",
          shadow.FragopShadowLiveStateSetterMaskByteOffset },
        { "fragop_shadow_live_state_setter_dirty_word_offset",
          shadow.FragopShadowLiveStateSetterDirtyWordOffset },
        { "fragop_shadow_live_state_setter_dirty_bit_mask",
          shadow.FragopShadowLiveStateSetterDirtyBitMask },
        { "fragop_shadow_live_state_setter_immediate_shadow_copy_offset",
          shadow.FragopShadowLiveStateSetterImmediateShadowCopyOffset },
        { "fragop_shadow_live_state_setter_vec3_wrapper_address",
          shadow.FragopShadowLiveStateSetterVec3WrapperAddress },
        { "fragop_shadow_live_state_setter_vec4_wrapper_address",
          shadow.FragopShadowLiveStateSetterVec4WrapperAddress },
        { "fragop_shadow_live_state_setter_vec3_direct_callsite_address",
          shadow.FragopShadowLiveStateSetterVec3DirectCallsiteAddress },
        { "fragop_shadow_live_state_setter_vec4_direct_callsite_address",
          shadow.FragopShadowLiveStateSetterVec4DirectCallsiteAddress },
        { "fragop_shadow_live_state_setter_wrapper_caller_addresses",
          shadow.FragopShadowLiveStateSetterWrapperCallerAddresses },
        { "fragop_shadow_live_state_setter_alternate_material_state_caller_address",
          shadow.FragopShadowLiveStateSetterAlternateMaterialStateCallerAddress },
        { "fragop_shadow_live_state_setter_alternate_packet_prep_caller_addresses",
          shadow.FragopShadowLiveStateSetterAlternatePacketPrepCallerAddresses },
        { "fragop_shadow_material_submit_wrapper_address",
          shadow.FragopShadowMaterialSubmitWrapperAddress },
        { "fragop_shadow_material_submit_float_setter_branch_mode",
          shadow.FragopShadowMaterialSubmitFloatSetterBranchMode },
        { "fragop_shadow_material_submit_verified_bypass_mode",
          shadow.FragopShadowMaterialSubmitVerifiedBypassMode },
        { "fragop_shadow_material_submit_verified_callsite_addresses",
          shadow.FragopShadowMaterialSubmitVerifiedCallsiteAddresses },
        { "fragop_shadow_material_submit_verified_caller_addresses",
          shadow.FragopShadowMaterialSubmitVerifiedCallerAddresses },
        { "fragop_shadow_material_submit_verified_callsites_bypass_live_setter",
          shadow.FragopShadowMaterialSubmitVerifiedCallsitesBypassLiveSetter },
        { "fragop_shadow_scalar_material_setter_address",
          shadow.FragopShadowScalarMaterialSetterAddress },
        { "fragop_shadow_scalar_material_setter_excludes_penumbra_cases",
          shadow.FragopShadowScalarMaterialSetterExcludesPenumbraCases },
        { "fragop_shadow_submit_route_exclusion_summary",
          shadow.FragopShadowSubmitRouteExclusionSummary },
        { "fragop_shadow_residual_wrapper_reference_target_count",
          shadow.FragopShadowResidualWrapperReferenceTargetCount },
        { "fragop_shadow_residual_wrapper_reference_total_count",
          shadow.FragopShadowResidualWrapperReferenceTotalCount },
        { "fragop_shadow_residual_wrapper_raw_pointer_match_count",
          shadow.FragopShadowResidualWrapperRawPointerMatchCount },
        { "fragop_shadow_residual_wrapper_reference_source",
          shadow.FragopShadowResidualWrapperReferenceSource },
        { "fragop_shadow_residual_wrapper_references_resolved",
          shadow.FragopShadowResidualWrapperReferencesResolved },
        { "fragop_shadow_residual_wrapper_raw_pointers_excluded",
          shadow.FragopShadowResidualWrapperRawPointersExcluded },
        { "fragop_shadow_live_state_setter_callsite_summary",
          shadow.FragopShadowLiveStateSetterCallsiteSummary },
        { "fragop_shadow_live_state_setter_formula",
          shadow.FragopShadowLiveStateSetterFormula },
        { "fragop_shadow_record_set_builder_address", shadow.FragopShadowRecordSetBuilderAddress },
        { "fragop_shadow_record_set_lookup_helper_address",
          shadow.FragopShadowRecordSetLookupHelperAddress },
        { "fragop_shadow_record_set_apply_helper_address",
          shadow.FragopShadowRecordSetApplyHelperAddress },
        { "fragop_shadow_record_set_animation_block_apply_address",
          shadow.FragopShadowRecordSetAnimationBlockApplyAddress },
        { "fragop_shadow_record_set_primary_record_stride_bytes",
          shadow.FragopShadowRecordSetPrimaryRecordStrideBytes },
        { "fragop_shadow_record_set_secondary_record_stride_bytes",
          shadow.FragopShadowRecordSetSecondaryRecordStrideBytes },
        { "fragop_shadow_record_set_lookup_family_stride_bytes",
          shadow.FragopShadowRecordSetLookupFamilyStrideBytes },
        { "fragop_shadow_record_set_lookup_secondary_base_offset",
          shadow.FragopShadowRecordSetLookupSecondaryBaseOffset },
        { "fragop_shadow_record_set_semantic_id_1f", shadow.FragopShadowRecordSetSemanticId1f },
        { "fragop_shadow_record_set_semantic_id_20", shadow.FragopShadowRecordSetSemanticId20 },
        { "fragop_shadow_record_set_exclusion_summary",
          shadow.FragopShadowRecordSetExclusionSummary },
        { "fragop_shadow_record_set_semantic_ids_excluded_as_live_state_source",
          shadow.FragopShadowRecordSetSemanticIdsExcludedAsLiveStateSource },
        { "fragop_shadow_native_handle_initializer_address",
          shadow.FragopShadowNativeHandleInitializerAddress },
        { "fragop_shadow_native_handle_context_offset",
          shadow.FragopShadowNativeHandleContextOffset },
        { "fragop_shadow_native_handle_source_table_offset",
          shadow.FragopShadowNativeHandleSourceTableOffset },
        { "fragop_shadow_native_handle_provider_entry_count",
          shadow.FragopShadowNativeHandleProviderEntryCount },
        { "fragop_shadow_native_handle_payload_export_function_count",
          shadow.FragopShadowNativeHandlePayloadExportFunctionCount },
        { "fragop_shadow_native_handle_texture2d_resource_id",
          shadow.FragopShadowNativeHandleTexture2dResourceId },
        { "fragop_shadow_native_handle_alt_texture_resource_id",
          shadow.FragopShadowNativeHandleAltTextureResourceId },
        { "fragop_shadow_native_handle_material_resource_base_id",
          shadow.FragopShadowNativeHandleMaterialResourceBaseId },
        { "fragop_shadow_native_handle_material_resource_accepted_range_end_id",
          shadow.FragopShadowNativeHandleMaterialResourceAcceptedRangeEndId },
        { "fragop_shadow_native_handle_lut_resource_base_id",
          shadow.FragopShadowNativeHandleLutResourceBaseId },
        { "fragop_shadow_native_handle_payload_common_writer_address",
          shadow.FragopShadowNativeHandlePayloadCommonWriterAddress },
        { "fragop_shadow_native_handle_texture_container_create_address",
          shadow.FragopShadowNativeHandleTextureContainerCreateAddress },
        { "fragop_shadow_native_handle_cube_texture_container_create_address",
          shadow.FragopShadowNativeHandleCubeTextureContainerCreateAddress },
        { "fragop_shadow_native_handle_payload_route_summary",
          shadow.FragopShadowNativeHandlePayloadRouteSummary },
        { "fragop_shadow_native_handle_payload_route_excluded_as_live_state_source",
          shadow.FragopShadowNativeHandlePayloadRouteExcludedAsLiveStateSource },
        { "fragop_shadow_native_handle_next_proof_target",
          shadow.FragopShadowNativeHandleNextProofTarget },
        { "fragop_shadow_material_state_compiler_address",
          shadow.FragopShadowMaterialStateCompilerAddress },
        { "fragop_shadow_material_state_encoded_handle_flag",
          shadow.FragopShadowMaterialStateEncodedHandleFlag },
        { "fragop_shadow_material_state_encoded_handle_table_pointer_literal_address",
          shadow.FragopShadowMaterialStateEncodedHandleTablePointerLiteralAddress },
        { "fragop_shadow_material_state_encoded_handle_runtime_table_address",
          shadow.FragopShadowMaterialStateEncodedHandleRuntimeTableAddress },
        { "fragop_shadow_material_state_encoded_handle_record_stride_bytes",
          shadow.FragopShadowMaterialStateEncodedHandleRecordStrideBytes },
        { "fragop_shadow_material_state_default_encoded_handle_record_count",
          shadow.FragopShadowMaterialStateDefaultEncodedHandleRecordCount },
        { "fragop_shadow_material_state_encoded_handle_static_table_address",
          shadow.FragopShadowMaterialStateEncodedHandleStaticTableAddress },
        { "fragop_shadow_material_state_encoded_handle_static_table_sentinel_address",
          shadow.FragopShadowMaterialStateEncodedHandleStaticTableSentinelAddress },
        { "fragop_shadow_material_state_encoded_handle_static_record_stride_bytes",
          shadow.FragopShadowMaterialStateEncodedHandleStaticRecordStrideBytes },
        { "fragop_shadow_material_state_encoded_handle_static_record_count",
          shadow.FragopShadowMaterialStateEncodedHandleStaticRecordCount },
        { "fragop_shadow_material_state_selector_1f_static_record_address",
          shadow.FragopShadowMaterialStateSelector1fStaticRecordAddress },
        { "fragop_shadow_material_state_selector_1f_resource_type",
          shadow.FragopShadowMaterialStateSelector1fResourceType },
        { "fragop_shadow_material_state_selector_1f_name_pointer_address",
          shadow.FragopShadowMaterialStateSelector1fNamePointerAddress },
        { "fragop_shadow_material_state_selector_1f_name",
          shadow.FragopShadowMaterialStateSelector1fName },
        { "fragop_shadow_material_state_selector_20_static_record_address",
          shadow.FragopShadowMaterialStateSelector20StaticRecordAddress },
        { "fragop_shadow_material_state_selector_20_resource_type",
          shadow.FragopShadowMaterialStateSelector20ResourceType },
        { "fragop_shadow_material_state_selector_20_name_pointer_address",
          shadow.FragopShadowMaterialStateSelector20NamePointerAddress },
        { "fragop_shadow_material_state_selector_20_name",
          shadow.FragopShadowMaterialStateSelector20Name },
        { "fragop_shadow_material_state_compiler_candidate_summary",
          shadow.FragopShadowMaterialStateCompilerCandidateSummary },
        { "fragop_shadow_material_state_encoded_handle_table_readable_from_codebin",
          shadow.FragopShadowMaterialStateEncodedHandleTableReadableFromCodebin },
        { "fragop_shadow_material_state_encoded_handle_table_source_resolved_from_codebin",
          shadow.FragopShadowMaterialStateEncodedHandleTableSourceResolvedFromCodebin },
        { "fragop_shadow_material_state_selector_coverage_resolved_from_codebin",
          shadow.FragopShadowMaterialStateSelectorCoverageResolvedFromCodebin },
        { "fragop_shadow_material_state_compiler_promoted_as_fragop_owner",
          shadow.FragopShadowMaterialStateCompilerPromotedAsFragopOwner },
        { "fragop_shadow_link_house_default_only_trace_payload_write_count",
          shadow.FragopShadowLinkHouseDefaultOnlyTracePayloadWriteCount },
        { "fragop_shadow_link_house_default_only_trace_header_write_count",
          shadow.FragopShadowLinkHouseDefaultOnlyTraceHeaderWriteCount },
        { "fragop_shadow_link_house_non_default_payload_trace_write_count",
          shadow.FragopShadowLinkHouseNonDefaultPayloadTraceWriteCount },
        { "fragop_shadow_link_house_non_default_header_trace_write_count",
          shadow.FragopShadowLinkHouseNonDefaultHeaderTraceWriteCount },
        { "fragop_shadow_link_house_default_only_payload_raw_value",
          shadow.FragopShadowLinkHouseDefaultOnlyPayloadRawValue },
        { "fragop_shadow_link_house_default_only_command_header",
          shadow.FragopShadowLinkHouseDefaultOnlyCommandHeader },
        { "fragop_shadow_link_house_default_only_validated_by_trace",
          shadow.FragopShadowLinkHouseDefaultOnlyValidatedByTrace },
        { "fragop_shadow_link_house_default_only_resolved_from_codebin",
          shadow.FragopShadowLinkHouseDefaultOnlyResolvedFromCodebin },
        { "fragop_shadow_link_house_default_only_validation_source",
          shadow.FragopShadowLinkHouseDefaultOnlyValidationSource },
        { "nngx_pica_register_state_initializer_address",
          shadow.NngxPicaRegisterStateInitializerAddress },
        { "nngx_pica_register_state_default_value_base_offset",
          shadow.NngxPicaRegisterStateDefaultValueBaseOffset },
        { "nngx_pica_register_state_default_mask_byte_base_offset",
          shadow.NngxPicaRegisterStateDefaultMaskByteBaseOffset },
        { "fragop_shadow_default_state_value_offset",
          shadow.FragopShadowDefaultStateValueOffset },
        { "fragop_shadow_default_mask_byte_offset",
          shadow.FragopShadowDefaultMaskByteOffset },
        { "fragop_shadow_header_table_mapping_resolved_from_codebin",
          shadow.FragopShadowHeaderTableMappingResolvedFromCodebin },
        { "fragop_shadow_default_state_resolved_from_codebin",
          shadow.FragopShadowDefaultStateResolvedFromCodebin },
        { "initial_default_command_list_emit_resolved_from_codebin",
          shadow.InitialDefaultCommandListEmitResolvedFromCodebin },
        { "final_register_flush_fragop_shadow_header_trace_write_count",
          shadow.FinalRegisterFlushFragopShadowHeaderTraceWriteCount },
        { "final_register_flush_fragop_shadow_payload_trace_write_count",
          shadow.FinalRegisterFlushFragopShadowPayloadTraceWriteCount },
        { "final_register_flush_trace_memory_write_row_count",
          shadow.FinalRegisterFlushTraceMemoryWriteRowCount },
        { "final_register_flush_trace_register_write_count",
          shadow.FinalRegisterFlushTraceRegisterWriteCount },
        { "final_register_flush_formula", shadow.FinalRegisterFlushFormula },
        { "final_register_flush_trace_source", shadow.FinalRegisterFlushTraceSource },
        { "depth_map_final_flush_owner_address", shadow.DepthMapFinalFlushOwnerAddress },
        { "depth_map_final_flush_vtable_slot_address",
          shadow.DepthMapFinalFlushVtableSlotAddress },
        { "depth_map_final_flush_emitter_address", shadow.DepthMapFinalFlushEmitterAddress },
        { "depth_map_final_flush_callsite_address", shadow.DepthMapFinalFlushCallsiteAddress },
        { "depth_map_scale_offset_sequential_command_header",
          shadow.DepthMapScaleOffsetSequentialCommandHeader },
        { "depth_map_enable_command_header", shadow.DepthMapEnableCommandHeader },
        { "depth_map_scale_offset_sequential_word_count",
          shadow.DepthMapScaleOffsetSequentialWordCount },
        { "depth_map_enable_word_count", shadow.DepthMapEnableWordCount },
        { "depth_map_flush_zero_literal_address", shadow.DepthMapFlushZeroLiteralAddress },
        { "depth_map_flush_scale_literal_address", shadow.DepthMapFlushScaleLiteralAddress },
        { "depth_map_flush_record_scale_mode_byte_offset",
          shadow.DepthMapFlushRecordScaleModeByteOffset },
        { "depth_map_flush_record_scale_source_halfword_offset",
          shadow.DepthMapFlushRecordScaleSourceHalfwordOffset },
        { "depth_map_flush_context_scale_factor_float_offset",
          shadow.DepthMapFlushContextScaleFactorFloatOffset },
        { "depth_map_flush_context_scale_mode_byte_offset",
          shadow.DepthMapFlushContextScaleModeByteOffset },
        { "depth_map_flush_context_exponent_word_offset",
          shadow.DepthMapFlushContextExponentWordOffset },
        { "depth_map_final_flush_formula", shadow.DepthMapFinalFlushFormula },
        { "texunit0_shadow_packet_emitter_address", shadow.Texunit0ShadowPacketEmitterAddress },
        { "texunit0_shadow_packet_emitter_owner_address",
          shadow.Texunit0ShadowPacketEmitterOwnerAddress },
        { "texunit0_shadow_packet_emitter_callsite_address",
          shadow.Texunit0ShadowPacketEmitterCallsiteAddress },
        { "texunit0_shadow_submit_manager_vtable_address",
          shadow.Texunit0ShadowSubmitManagerVtableAddress },
        { "texunit0_shadow_submit_manager_vtable_slot_address",
          shadow.Texunit0ShadowSubmitManagerVtableSlotAddress },
        { "texunit0_shadow_submit_manager_vtable_slot_offset",
          shadow.Texunit0ShadowSubmitManagerVtableSlotOffset },
        { "texunit0_shadow_draw_method_address", shadow.Texunit0ShadowDrawMethodAddress },
        { "texunit0_shadow_matrix_method_address", shadow.Texunit0ShadowMatrixMethodAddress },
        { "texunit0_shadow_matrix_upload_address", shadow.Texunit0ShadowMatrixUploadAddress },
        { "texunit0_shadow_matrix_record_offset", shadow.Texunit0ShadowMatrixRecordOffset },
        { "texunit0_shadow_matrix_word_count", shadow.Texunit0ShadowMatrixWordCount },
        { "texunit0_shadow_texture_setup_emitter_address",
          shadow.Texunit0ShadowTextureSetupEmitterAddress },
        { "texunit0_shadow_texture_setup_callsite_address",
          shadow.Texunit0ShadowTextureSetupCallsiteAddress },
        { "texunit0_shadow_texture_setup_base_register",
          shadow.Texunit0ShadowTextureSetupBaseRegister },
        { "texunit0_shadow_texture_setup_word_count",
          shadow.Texunit0ShadowTextureSetupWordCount },
        { "texunit0_shadow_texture_setup_tex_param_register",
          shadow.Texunit0ShadowTextureSetupTexParamRegister },
        { "texunit0_shadow_texture_setup_tex_pointer_offset",
          shadow.Texunit0ShadowTextureSetupTexPointerOffset },
        { "texunit0_shadow_texture_setup_size_halfword_0_offset",
          shadow.Texunit0ShadowTextureSetupSizeHalfword0Offset },
        { "texunit0_shadow_texture_setup_size_halfword_1_offset",
          shadow.Texunit0ShadowTextureSetupSizeHalfword1Offset },
        { "texunit0_shadow_special_texcoord_type", shadow.Texunit0ShadowSpecialTexcoordType },
        { "texunit0_shadow_runtime_record_constructor_address",
          shadow.Texunit0ShadowRuntimeRecordConstructorAddress },
        { "texunit0_shadow_runtime_record_allocation_size_bytes",
          shadow.Texunit0ShadowRuntimeRecordAllocationSizeBytes },
        { "texunit0_shadow_runtime_record_clone_address", shadow.Texunit0ShadowRuntimeRecordCloneAddress },
        { "texunit0_shadow_runtime_record_clone_wrapper_address",
          shadow.Texunit0ShadowRuntimeRecordCloneWrapperAddress },
        { "texunit0_shadow_runtime_record_array_initializer_address",
          shadow.Texunit0ShadowRuntimeRecordArrayInitializerAddress },
        { "texunit0_shadow_runtime_record_subrecord_array_offset",
          shadow.Texunit0ShadowRuntimeRecordSubrecordArrayOffset },
        { "texunit0_shadow_runtime_record_subrecord_stride_bytes",
          shadow.Texunit0ShadowRuntimeRecordSubrecordStrideBytes },
        { "texunit0_shadow_runtime_record_subrecord_count",
          shadow.Texunit0ShadowRuntimeRecordSubrecordCount },
        { "texunit0_shadow_runtime_record_initial_pointer_offset",
          shadow.Texunit0ShadowRuntimeRecordInitialPointerOffset },
        { "texunit0_shadow_runtime_record_initial_pointer_value",
          shadow.Texunit0ShadowRuntimeRecordInitialPointerValue },
        { "texunit0_shadow_runtime_record_initial_gate_byte_offset",
          shadow.Texunit0ShadowRuntimeRecordInitialGateByteOffset },
        { "texunit0_shadow_runtime_record_initial_gate_byte_value",
          shadow.Texunit0ShadowRuntimeRecordInitialGateByteValue },
        { "texunit0_shadow_runtime_record_initial_texcoord_byte_offset",
          shadow.Texunit0ShadowRuntimeRecordInitialTexcoordByteOffset },
        { "texunit0_shadow_runtime_record_initial_texcoord_byte_value",
          shadow.Texunit0ShadowRuntimeRecordInitialTexcoordByteValue },
        { "texunit0_shadow_global_runtime_root_address",
          shadow.Texunit0ShadowGlobalRuntimeRootAddress },
        { "texunit0_shadow_global_factory_manager_slot_offset",
          shadow.Texunit0ShadowGlobalFactoryManagerSlotOffset },
        { "texunit0_shadow_global_factory_manager_slot_address",
          shadow.Texunit0ShadowGlobalFactoryManagerSlotAddress },
        { "texunit0_shadow_factory_manager_constructor_address",
          shadow.Texunit0ShadowFactoryManagerConstructorAddress },
        { "texunit0_shadow_factory_manager_vtable_address",
          shadow.Texunit0ShadowFactoryManagerVtableAddress },
        { "texunit0_shadow_factory_manager_factory_slot_offset",
          shadow.Texunit0ShadowFactoryManagerFactorySlotOffset },
        { "texunit0_shadow_factory_manager_factory_method_address",
          shadow.Texunit0ShadowFactoryManagerFactoryMethodAddress },
        { "texunit0_shadow_factory_manager_default_context_slot_offset",
          shadow.Texunit0ShadowFactoryManagerDefaultContextSlotOffset },
        { "texunit0_shadow_factory_manager_override_context_slot_offset",
          shadow.Texunit0ShadowFactoryManagerOverrideContextSlotOffset },
        { "texunit0_shadow_central_factory_address",
          shadow.Texunit0ShadowCentralFactoryAddress },
        { "texunit0_shadow_central_factory_context_field_0_offset",
          shadow.Texunit0ShadowCentralFactoryContextField0Offset },
        { "texunit0_shadow_central_factory_context_field_1_offset",
          shadow.Texunit0ShadowCentralFactoryContextField1Offset },
        { "texunit0_shadow_draw_object_subobject_context_bind_address",
          shadow.Texunit0ShadowDrawObjectSubobjectContextBindAddress },
        { "texunit0_shadow_draw_object_aggregate_context_bind_address",
          shadow.Texunit0ShadowDrawObjectAggregateContextBindAddress },
        { "texunit0_shadow_draw_object_subobject_context_offset",
          shadow.Texunit0ShadowDrawObjectSubobjectContextOffset },
        { "texunit0_shadow_draw_object_aggregate_context_offset",
          shadow.Texunit0ShadowDrawObjectAggregateContextOffset },
        { "texunit0_shadow_actor_spawn_address", shadow.Texunit0ShadowActorSpawnAddress },
        { "texunit0_shadow_actor_context_pointer_offset",
          shadow.Texunit0ShadowActorContextPointerOffset },
        { "texunit0_shadow_player_draw_address", shadow.Texunit0ShadowPlayerDrawAddress },
        { "texunit0_shadow_player_draw_route_flag_offset",
          shadow.Texunit0ShadowPlayerDrawRouteFlagOffset },
        { "texunit0_shadow_player_draw_route_flag_mask",
          shadow.Texunit0ShadowPlayerDrawRouteFlagMask },
        { "texunit0_shadow_player_draw_clone_callsite_address",
          shadow.Texunit0ShadowPlayerDrawCloneCallsiteAddress },
        { "texunit0_shadow_player_draw_factory_callsite_address",
          shadow.Texunit0ShadowPlayerDrawFactoryCallsiteAddress },
        { "texunit0_shadow_player_draw_submit_callsite_address",
          shadow.Texunit0ShadowPlayerDrawSubmitCallsiteAddress },
        { "texunit0_shadow_player_source_context_pointer_offset",
          shadow.Texunit0ShadowPlayerSourceContextPointerOffset },
        { "texunit0_shadow_player_shadow_resource_cmb_offset",
          shadow.Texunit0ShadowPlayerShadowResourceCmbOffset },
        { "texunit0_shadow_player_init_address", shadow.Texunit0ShadowPlayerInitAddress },
        { "texunit0_shadow_player_init_common_address",
          shadow.Texunit0ShadowPlayerInitCommonAddress },
        { "texunit0_shadow_skelanime_init_link_address",
          shadow.Texunit0ShadowSkelAnimeInitLinkAddress },
        { "texunit0_shadow_player_init_cmb_resource_visibility_loop_callsite_address",
          shadow.Texunit0ShadowPlayerInitCmbResourceVisibilityLoopCallsiteAddress },
        { "texunit0_shadow_resource_visibility_clear_address",
          shadow.Texunit0ShadowResourceVisibilityClearAddress },
        { "texunit0_shadow_resource_visibility_set_address",
          shadow.Texunit0ShadowResourceVisibilitySetAddress },
        { "texunit0_shadow_draw_object_resource_state_pointer_offset",
          shadow.Texunit0ShadowDrawObjectResourceStatePointerOffset },
        { "texunit0_shadow_draw_object_resource_visibility_count_offset",
          shadow.Texunit0ShadowDrawObjectResourceVisibilityCountOffset },
        { "texunit0_shadow_draw_object_resource_visibility_bytes_offset",
          shadow.Texunit0ShadowDrawObjectResourceVisibilityBytesOffset },
        { "texunit0_shadow_player_aux_context_pointer_offset",
          shadow.Texunit0ShadowPlayerAuxContextPointerOffset },
        { "texunit0_shadow_player_draw_object_offset",
          shadow.Texunit0ShadowPlayerDrawObjectOffset },
        { "texunit0_shadow_player_clone_destination_offset",
          shadow.Texunit0ShadowPlayerCloneDestinationOffset },
        { "texunit0_shadow_runtime_record_copy_return_wrapper_address",
          shadow.Texunit0ShadowRuntimeRecordCopyReturnWrapperAddress },
        { "texunit0_shadow_runtime_record_direct_copy_caller_count",
          shadow.Texunit0ShadowRuntimeRecordDirectCopyCallerCount },
        { "texunit0_shadow_player_model_group_setter_address",
          shadow.Texunit0ShadowPlayerModelGroupSetterAddress },
        { "texunit0_shadow_player_model_setter_address",
          shadow.Texunit0ShadowPlayerModelSetterAddress },
        { "texunit0_shadow_player_equipment_data_address",
          shadow.Texunit0ShadowPlayerEquipmentDataAddress },
        { "texunit0_shadow_player_model_group_table_address",
          shadow.Texunit0ShadowPlayerModelGroupTableAddress },
        { "texunit0_shadow_player_model_group_entry_size_bytes",
          shadow.Texunit0ShadowPlayerModelGroupEntrySizeBytes },
        { "texunit0_shadow_player_model_group_entry_count",
          shadow.Texunit0ShadowPlayerModelGroupEntryCount },
        { "texunit0_shadow_player_model_group_render_mode_byte_offset",
          shadow.Texunit0ShadowPlayerModelGroupRenderModeByteOffset },
        { "texunit0_shadow_player_model_group_model0_byte_offset",
          shadow.Texunit0ShadowPlayerModelGroupModel0ByteOffset },
        { "texunit0_shadow_player_model_group_gate_byte_offset",
          shadow.Texunit0ShadowPlayerModelGroupGateByteOffset },
        { "texunit0_shadow_player_model_group_texcoord_byte_offset",
          shadow.Texunit0ShadowPlayerModelGroupTexcoordByteOffset },
        { "texunit0_shadow_player_model_group_model3_byte_offset",
          shadow.Texunit0ShadowPlayerModelGroupModel3ByteOffset },
        { "texunit0_shadow_player_model_pointer_table_address",
          shadow.Texunit0ShadowPlayerModelPointerTableAddress },
        { "texunit0_shadow_player_model_pointer_table_entry_stride_bytes",
          shadow.Texunit0ShadowPlayerModelPointerTableEntryStrideBytes },
        { "texunit0_shadow_player_model_pointer_table_entry_count",
          shadow.Texunit0ShadowPlayerModelPointerTableEntryCount },
        { "texunit0_shadow_mantissa_helper_address", shadow.Texunit0ShadowMantissaHelperAddress },
        { "texunit0_shadow_mantissa_threshold_literal_address",
          shadow.Texunit0ShadowMantissaThresholdLiteralAddress },
        { "texunit0_shadow_mantissa_scale_literal_address",
          shadow.Texunit0ShadowMantissaScaleLiteralAddress },
        { "texunit0_shadow_mantissa_clamp_literal_address",
          shadow.Texunit0ShadowMantissaClampLiteralAddress },
        { "texunit0_shadow_packet_emitter_word_count",
          shadow.Texunit0ShadowPacketEmitterWordCount },
        { "texunit0_shadow_packet_emitter_mask", shadow.Texunit0ShadowPacketEmitterMask },
        { "texunit0_shadow_packet_emitter_sequential_flag",
          shadow.Texunit0ShadowPacketEmitterSequentialFlag },
        { "texunit0_shadow_special_route_gate_byte_offset",
          shadow.Texunit0ShadowSpecialRouteGateByteOffset },
        { "texunit0_shadow_special_route_record_pointer_offset",
          shadow.Texunit0ShadowSpecialRouteRecordPointerOffset },
        { "texunit0_shadow_bias_signed_denominator_offset",
          shadow.Texunit0ShadowBiasSignedDenominatorOffset },
        { "texunit0_shadow_exponent_numerator_float_offset",
          shadow.Texunit0ShadowExponentNumeratorFloatOffset },
        { "texunit0_shadow_mantissa_near_float_offset",
          shadow.Texunit0ShadowMantissaNearFloatOffset },
        { "texunit0_shadow_mantissa_far_float_offset",
          shadow.Texunit0ShadowMantissaFarFloatOffset },
        { "texunit0_shadow_mantissa_scale_float_offset",
          shadow.Texunit0ShadowMantissaScaleFloatOffset },
        { "texunit0_shadow_projection_flag_byte_offset",
          shadow.Texunit0ShadowProjectionFlagByteOffset },
        { "texunit0_shadow_projection_flag_xor_mask", shadow.Texunit0ShadowProjectionFlagXorMask },
        { "texunit0_shadow_mantissa_mask", shadow.Texunit0ShadowMantissaMask },
        { "texunit0_shadow_exponent_shift", shadow.Texunit0ShadowExponentShift },
        { "texunit0_shadow_packet_formula", shadow.Texunit0ShadowPacketFormula },
        { "texunit0_shadow_packet_emit_resolved", shadow.Texunit0ShadowPacketEmitResolved },
        { "texunit0_shadow_submit_manager_vtable_resolved",
          shadow.Texunit0ShadowSubmitManagerVtableResolved },
        { "texunit0_shadow_runtime_record_consumer_resolved",
          shadow.Texunit0ShadowRuntimeRecordConsumerResolved },
        { "texunit0_shadow_runtime_record_constructor_resolved",
          shadow.Texunit0ShadowRuntimeRecordConstructorResolved },
        { "texunit0_shadow_player_model_routing_resolved",
          shadow.Texunit0ShadowPlayerModelRoutingResolved },
        { "texunit0_shadow_source_context_allocation_chain_resolved",
          shadow.Texunit0ShadowSourceContextAllocationChainResolved },
        { "texunit0_shadow_player_cmb_route_to_source_context_resolved",
          shadow.Texunit0ShadowPlayerCmbRouteToSourceContextResolved },
        { "texunit0_shadow_player_cmb_resource_visibility_route_resolved",
          shadow.Texunit0ShadowPlayerCmbResourceVisibilityRouteResolved },
        { "texunit0_shadow_player_cmb_resource_visibility_route_excluded_as_record_pointer_producer",
          shadow.Texunit0ShadowPlayerCmbResourceVisibilityRouteExcludedAsRecordPointerProducer },
        { "texunit0_shadow_central_factory_propagates_context_only",
          shadow.Texunit0ShadowCentralFactoryPropagatesContextOnly },
        { "texunit0_shadow_draw_object_context_binding_resolved",
          shadow.Texunit0ShadowDrawObjectContextBindingResolved },
        { "texunit0_shadow_player_draw_clone_copies_existing_record_pointer",
          shadow.Texunit0ShadowPlayerDrawCloneCopiesExistingRecordPointer },
        { "texunit0_shadow_player_draw_creates_shadow_object_from_player_cmb",
          shadow.Texunit0ShadowPlayerDrawCreatesShadowObjectFromPlayerCmb },
        { "texunit0_shadow_source_context_init_chain_writes_record_pointer",
          shadow.Texunit0ShadowSourceContextInitChainWritesRecordPointer },
        { "texunit0_shadow_player_aux_context_excluded_as_source_context",
          shadow.Texunit0ShadowPlayerAuxContextExcludedAsSourceContext },
        { "texunit0_shadow_direct_gate_store_route_exhausted",
          shadow.Texunit0ShadowDirectGateStoreRouteExhausted },
        { "texunit0_shadow_source_context_route_summary",
          shadow.Texunit0ShadowSourceContextRouteSummary },
        { "texunit0_shadow_source_context_next_proof_target",
          shadow.Texunit0ShadowSourceContextNextProofTarget },
        { "texunit0_shadow_player_cmb_resource_visibility_route_summary",
          shadow.Texunit0ShadowPlayerCmbResourceVisibilityRouteSummary },
        { "texunit0_shadow_cmb_mesh_matrix_parser_address",
          shadow.Texunit0ShadowCmbMeshMatrixParserAddress },
        { "texunit0_shadow_cmb_mesh_matrix_parser_caller_address",
          shadow.Texunit0ShadowCmbMeshMatrixParserCallerAddress },
        { "texunit0_shadow_cmb_mesh_matrix_parser_callsite_address",
          shadow.Texunit0ShadowCmbMeshMatrixParserCallsiteAddress },
        { "texunit0_shadow_cmb_mesh_matrix_source_record_stride_bytes",
          shadow.Texunit0ShadowCmbMeshMatrixSourceRecordStrideBytes },
        { "texunit0_shadow_cmb_mesh_matrix_output_stride_bytes",
          shadow.Texunit0ShadowCmbMeshMatrixOutputStrideBytes },
        { "texunit0_shadow_cmb_mesh_matrix_parser_summary",
          shadow.Texunit0ShadowCmbMeshMatrixParserSummary },
        { "texunit0_shadow_cmb_mesh_matrix_parser_excluded_as_record_pointer_producer",
          shadow.Texunit0ShadowCmbMeshMatrixParserExcludedAsRecordPointerProducer },
        { "texunit0_shadow_runtime_record_owner_resolved",
          shadow.Texunit0ShadowRuntimeRecordOwnerResolved },
        { "texunit0_shadow_runtime_values_decoded", shadow.Texunit0ShadowRuntimeValuesDecoded },
        { "view_projection_update_address", shadow.ViewProjectionUpdateAddress },
        { "alternate_view_projection_update_address", shadow.AlternateViewProjectionUpdateAddress },
        { "view_projection_matrix_inverse_address", shadow.ViewProjectionMatrixInverseAddress },
        { "view_projection_depth_like_source_word_0", shadow.ViewProjectionDepthLikeSourceWord0 },
        { "view_projection_depth_like_source_word_1", shadow.ViewProjectionDepthLikeSourceWord1 },
        { "view_projection_depth_like_destination_word_0",
          shadow.ViewProjectionDepthLikeDestinationWord0 },
        { "view_projection_depth_like_destination_word_1",
          shadow.ViewProjectionDepthLikeDestinationWord1 },
        { "view_projection_copy_excluded_as_shadow_register_flush",
          shadow.ViewProjectionCopyExcludedAsShadowRegisterFlush },
        { "pica_state_initializer_writes_depth_map_offset",
          shadow.PicaStateInitializerWritesDepthMapOffset },
        { "pica_state_initializer_writes_depth_map_enable",
          shadow.PicaStateInitializerWritesDepthMapEnable },
        { "pica_state_initializer_writes_texunit0_shadow",
          shadow.PicaStateInitializerWritesTexunit0Shadow },
        { "generic_packet_initializer_zeros_depth_map_scale",
          shadow.GenericPacketInitializerZerosDepthMapScale },
        { "generic_packet_initializer_zeros_depth_map_offset",
          shadow.GenericPacketInitializerZerosDepthMapOffset },
        { "texture_descriptor_initializer_zeros_depth_map_scale",
          shadow.TextureDescriptorInitializerZerosDepthMapScale },
        { "texture_descriptor_initializer_zeros_depth_map_offset",
          shadow.TextureDescriptorInitializerZerosDepthMapOffset },
        { "generic_packet_initializer_clears_descriptor_record_pointer_words",
          shadow.GenericPacketInitializerClearsDescriptorRecordPointerWords },
        { "texture_descriptor_initializer_clears_descriptor_record_pointer_words",
          shadow.TextureDescriptorInitializerClearsDescriptorRecordPointerWords },
        { "descriptor_packet_reset_clears_descriptor_record_pointer_words",
          shadow.DescriptorPacketResetClearsDescriptorRecordPointerWords },
        { "descriptor_packet_initializers_excluded_as_texunit0_shadow_record_pointer_producers",
          shadow.DescriptorPacketInitializersExcludedAsTexunit0ShadowRecordPointerProducers },
        { "descriptor_packet_initializer_exclusion_summary",
          shadow.DescriptorPacketInitializerExclusionSummary },
        { "texture_shadow_compare_bias_formula", shadow.TextureShadowCompareBiasFormula },
        { "framebuffer_shadow_bias_formula", shadow.FramebufferShadowBiasFormula },
        { "depth_encode_formula", shadow.DepthEncodeFormula },
        { "depth_map_scale_offset_enable_flush_resolved",
          shadow.DepthMapScaleOffsetEnableFlushResolved },
        { "fragop_shadow_excluded_from_generic_pica_scalar_writer",
          shadow.FragopShadowExcludedFromGenericPicaScalarWriter },
        { "fragop_shadow_excluded_from_generic_pica_vector_uniform_writer",
          shadow.FragopShadowExcludedFromGenericPicaVectorUniformWriter },
        { "fragop_shadow_live_state_setter_resolved",
          shadow.FragopShadowLiveStateSetterResolved },
        { "fragop_shadow_live_state_setter_direct_routes_resolved",
          shadow.FragopShadowLiveStateSetterDirectRoutesResolved },
        { "fragop_shadow_verified_submit_paths_bypass_live_state_setter",
          shadow.FragopShadowVerifiedSubmitPathsBypassLiveStateSetter },
        { "fragop_shadow_live_state_setter_active_case_source_resolved",
          shadow.FragopShadowLiveStateSetterActiveCaseSourceResolved },
        { "fragop_shadow_live_state_setter_callsites_resolved",
          shadow.FragopShadowLiveStateSetterCallsitesResolved },
        { "fragop_shadow_runtime_non_default_value_source_resolved",
          shadow.FragopShadowRuntimeNonDefaultValueSourceResolved },
        { "fragop_shadow_final_flush_resolved", shadow.FragopShadowFinalFlushResolved },
        { "initial_default_command_list_emit_resolved",
          shadow.InitialDefaultCommandListEmitResolved },
        { "final_register_flush_resolved", shadow.FinalRegisterFlushResolved },
        { "runtime_values_decoded", shadow.RuntimeValuesDecoded },
        { "emulator_trace_used_as_runtime_source", shadow.EmulatorTraceUsedAsRuntimeSource },
    };
}

nlohmann::json NativeZsiLightSettingsRecordJson(const NativeZsiLightSettingsRecordContract& record) {
    return {
        { "command_id", record.CommandId },
        { "scene_command_handler_table_address", record.SceneCommandHandlerTableAddress },
        { "scene_command_handler_address", record.SceneCommandHandlerAddress },
        { "scene_command_entry_size_bytes", record.SceneCommandEntrySizeBytes },
        { "scene_command_count_byte_offset", record.SceneCommandCountByteOffset },
        { "scene_command_segment_offset_word_offset", record.SceneCommandSegmentOffsetWordOffset },
        { "play_state_light_settings_count_offset", record.PlayStateLightSettingsCountOffset },
        { "play_state_light_settings_list_pointer_offset", record.PlayStateLightSettingsListPointerOffset },
        { "scene_command_stores_native_list_pointer", record.SceneCommandStoresNativeListPointer },
        { "candidate_start_deltas", record.CandidateStartDeltas },
        { "native_record_size_bytes", record.NativeRecordSizeBytes },
        { "legacy_record_size_bytes", record.LegacyRecordSizeBytes },
        { "native_record_layout_name", record.NativeRecordLayoutName },
        { "legacy_record_layout_name", record.LegacyRecordLayoutName },
        { "native_env_prefix_size_bytes", record.NativeEnvPrefixSizeBytes },
        { "color_component_order", record.ColorComponentOrder },
        { "direction_component_encoding", record.DirectionComponentEncoding },
        { "ambient_color_offset", record.AmbientColorOffset },
        { "light0_direction_offset", record.Light0DirectionOffset },
        { "light0_color_offset", record.Light0ColorOffset },
        { "light1_direction_offset", record.Light1DirectionOffset },
        { "light1_color_offset", record.Light1ColorOffset },
        { "native_3ds_tail_byte_offset", record.Native3dsTailByteOffset },
        { "float_param0_offset", record.FloatParam0Offset },
        { "float_param1_offset", record.FloatParam1Offset },
        { "tail_word_offset", record.TailWordOffset },
        { "tail_word_size_bytes", record.TailWordSizeBytes },
        { "actor_packet_diffuse0_color_offset", record.ActorPacketDiffuse0ColorOffset },
        { "actor_packet_diffuse1_color_offset", record.ActorPacketDiffuse1ColorOffset },
        { "actor_packet_pica_fog_color_offset", record.ActorPacketPicaFogColorOffset },
        { "actor_packet_ambient_previous_tail_byte0_offset",
          record.ActorPacketAmbientPreviousTailByte0Offset },
        { "actor_packet_ambient_previous_tail_byte1_offset",
          record.ActorPacketAmbientPreviousTailByte1Offset },
        { "actor_packet_ambient_current_byte_offset", record.ActorPacketAmbientCurrentByteOffset },
        { "actor_packet_record_index_delta", record.ActorPacketRecordIndexDelta },
        { "actor_packet_record_selection_source", record.ActorPacketRecordSelectionSource },
        { "actor_packet_pica_fog_color_source", record.ActorPacketPicaFogColorSource },
        { "runtime_consumer_address", record.RuntimeConsumerAddress },
        { "runtime_consumer_caller_address", record.RuntimeConsumerCallerAddress },
        { "runtime_state_base_play_offset", record.RuntimeStateBasePlayOffset },
        { "runtime_output_base_play_offset", record.RuntimeOutputBasePlayOffset },
        { "runtime_pause_flag_play_offset", record.RuntimePauseFlagPlayOffset },
        { "runtime_initialized_flag_play_offset", record.RuntimeInitializedFlagPlayOffset },
        { "runtime_current_index_play_offset", record.RuntimeCurrentIndexPlayOffset },
        { "runtime_previous_index_play_offset", record.RuntimePreviousIndexPlayOffset },
        { "runtime_target_index_play_offset", record.RuntimeTargetIndexPlayOffset },
        { "runtime_blend_weight_play_offset", record.RuntimeBlendWeightPlayOffset },
        { "runtime_transition_table_address", record.RuntimeTransitionTableAddress },
        { "runtime_transition_table_code_base", record.RuntimeTransitionTableCodeBase },
        { "runtime_transition_mode_count", record.RuntimeTransitionModeCount },
        { "runtime_transition_mode_stride_bytes", record.RuntimeTransitionModeStrideBytes },
        { "runtime_transition_entry_count", record.RuntimeTransitionEntryCount },
        { "runtime_transition_entry_size_bytes", record.RuntimeTransitionEntrySizeBytes },
        { "runtime_transition_entry_start_angle_offset", record.RuntimeTransitionEntryStartAngleOffset },
        { "runtime_transition_entry_end_angle_offset", record.RuntimeTransitionEntryEndAngleOffset },
        { "runtime_transition_entry_from_index_offset", record.RuntimeTransitionEntryFromIndexOffset },
        { "runtime_transition_entry_to_index_offset", record.RuntimeTransitionEntryToIndexOffset },
        { "runtime_transition_mode_state_base_play_offset", record.RuntimeTransitionModeStateBasePlayOffset },
        { "runtime_transition_mode_current_relative_offset",
          record.RuntimeTransitionModeCurrentRelativeOffset },
        { "runtime_transition_mode_target_relative_offset",
          record.RuntimeTransitionModeTargetRelativeOffset },
        { "runtime_transition_mode_blend_active_relative_offset",
          record.RuntimeTransitionModeBlendActiveRelativeOffset },
        { "runtime_transition_mode_blend_remaining_halfword_relative_offset",
          record.RuntimeTransitionModeBlendRemainingHalfwordRelativeOffset },
        { "runtime_transition_mode_blend_duration_halfword_relative_offset",
          record.RuntimeTransitionModeBlendDurationHalfwordRelativeOffset },
        { "runtime_transition_mode_current_offset", record.RuntimeTransitionModeCurrentOffset },
        { "runtime_transition_mode_target_offset", record.RuntimeTransitionModeTargetOffset },
        { "runtime_transition_mode_blend_active_offset", record.RuntimeTransitionModeBlendActiveOffset },
        { "runtime_transition_mode_blend_remaining_halfword_offset",
          record.RuntimeTransitionModeBlendRemainingHalfwordOffset },
        { "runtime_transition_mode_blend_duration_halfword_offset",
          record.RuntimeTransitionModeBlendDurationHalfwordOffset },
        { "runtime_projection_matrix_play_offset", record.RuntimeProjectionMatrixPlayOffset },
        { "runtime_view_init_address", record.RuntimeViewInitAddress },
        { "runtime_view_default_near_literal_address",
          record.RuntimeViewDefaultNearLiteralAddress },
        { "runtime_view_default_far_literal_address",
          record.RuntimeViewDefaultFarLiteralAddress },
        { "runtime_view_update_address", record.RuntimeViewUpdateAddress },
        { "runtime_projection_build_address", record.RuntimeProjectionBuildAddress },
        { "runtime_scene_projection_far_literal_address",
          record.RuntimeSceneProjectionFarLiteralAddress },
        { "runtime_scene_projection_matrix_offset",
          record.RuntimeSceneProjectionMatrixOffset },
        { "runtime_transition_active_angle_working_state_address",
          record.RuntimeTransitionActiveAngleWorkingStateAddress },
        { "runtime_transition_active_angle_working_halfword_offset",
          record.RuntimeTransitionActiveAngleWorkingHalfwordOffset },
        { "runtime_transition_active_angle_output_state_address",
          record.RuntimeTransitionActiveAngleOutputStateAddress },
        { "runtime_transition_active_angle_output_halfword_offset",
          record.RuntimeTransitionActiveAngleOutputHalfwordOffset },
        { "runtime_transition_global_fallback_state_address",
          record.RuntimeTransitionGlobalFallbackStateAddress },
        { "runtime_transition_global_fallback_mode_offset",
          record.RuntimeTransitionGlobalFallbackModeOffset },
        { "runtime_transition_global_fallback_mode_weight_float_offset",
          record.RuntimeTransitionGlobalFallbackModeWeightFloatOffset },
        { "runtime_transition_global_fallback_from_index_offset",
          record.RuntimeTransitionGlobalFallbackFromIndexOffset },
        { "runtime_transition_global_fallback_to_index_offset",
          record.RuntimeTransitionGlobalFallbackToIndexOffset },
        { "runtime_scalar0_offset", record.RuntimeScalar0Offset },
        { "runtime_scalar1_offset", record.RuntimeScalar1Offset },
        { "runtime_packed_halfword_offset", record.RuntimePackedHalfwordOffset },
        { "runtime_packed_halfword_mask", record.RuntimePackedHalfwordMask },
        { "runtime_transition_rate_shift", record.RuntimeTransitionRateShift },
        { "runtime_record_start_delta", record.RuntimeRecordStartDelta },
        { "runtime_ambient_color_offset", record.RuntimeAmbientColorOffset },
        { "runtime_light0_direction_offset", record.RuntimeLight0DirectionOffset },
        { "runtime_light0_color_offset", record.RuntimeLight0ColorOffset },
        { "runtime_light1_direction_offset", record.RuntimeLight1DirectionOffset },
        { "runtime_light1_color_offset", record.RuntimeLight1ColorOffset },
        { "runtime_fog_color_offset", record.RuntimeFogColorOffset },
        { "runtime_color_component_count", record.RuntimeColorComponentCount },
        { "runtime_direction_component_count", record.RuntimeDirectionComponentCount },
        { "runtime_actor_vs_compact_payload_producer_address",
          record.RuntimeActorVsCompactPayloadProducerAddress },
        { "runtime_actor_vs_compact_payload_consumer_handler_address",
          record.RuntimeActorVsCompactPayloadConsumerHandlerAddress },
        { "runtime_actor_vs_compact_payload_slot_count",
          record.RuntimeActorVsCompactPayloadSlotCount },
        { "runtime_actor_vs_compact_payload_slot_stride_bytes",
          record.RuntimeActorVsCompactPayloadSlotStrideBytes },
        { "runtime_actor_vs_compact_payload_slot_offsets",
          record.RuntimeActorVsCompactPayloadSlotOffsets },
        { "runtime_actor_vs_compact_payload_direction_offset",
          record.RuntimeActorVsCompactPayloadDirectionOffset },
        { "runtime_actor_vs_compact_payload_color_offset",
          record.RuntimeActorVsCompactPayloadColorOffset },
        { "runtime_actor_vs_compact_payload_size_bytes",
          record.RuntimeActorVsCompactPayloadSizeBytes },
        { "runtime_actor_vs_compact_payload_direction_source_offsets",
          record.RuntimeActorVsCompactPayloadDirectionSourceOffsets },
        { "runtime_actor_vs_compact_payload_color_source_offsets",
          record.RuntimeActorVsCompactPayloadColorSourceOffsets },
        { "runtime_actor_vs_compact_payload_direction_component_count",
          record.RuntimeActorVsCompactPayloadDirectionComponentCount },
        { "runtime_actor_vs_compact_payload_color_component_count",
          record.RuntimeActorVsCompactPayloadColorComponentCount },
        { "runtime_actor_vs_compact_payload_direction_scale_denominator",
          record.RuntimeActorVsCompactPayloadDirectionScaleDenominator },
        { "runtime_actor_vs_compact_payload_direction_angle_bias",
          record.RuntimeActorVsCompactPayloadDirectionAngleBias },
        { "runtime_actor_vs_compact_payload_direction_sin_scale_x",
          record.RuntimeActorVsCompactPayloadDirectionSinScaleX },
        { "runtime_actor_vs_compact_payload_direction_cos_scale_y",
          record.RuntimeActorVsCompactPayloadDirectionCosScaleY },
        { "runtime_actor_vs_compact_payload_direction_cos_scale_z",
          record.RuntimeActorVsCompactPayloadDirectionCosScaleZ },
        { "runtime_color_component_order", record.RuntimeColorComponentOrder },
        { "runtime_direction_component_encoding", record.RuntimeDirectionComponentEncoding },
        { "runtime_actor_vs_compact_payload_source", record.RuntimeActorVsCompactPayloadSource },
        { "runtime_actor_vs_compact_payload_direction_source",
          record.RuntimeActorVsCompactPayloadDirectionSource },
        { "runtime_actor_vs_compact_payload_color_source",
          record.RuntimeActorVsCompactPayloadColorSource },
        { "runtime_transition_active_angle_source", record.RuntimeTransitionActiveAngleSource },
        { "runtime_transition_mode_state_source", record.RuntimeTransitionModeStateSource },
        { "runtime_transition_blend_formula", record.RuntimeTransitionBlendFormula },
        { "runtime_transition_mode_blend_formula", record.RuntimeTransitionModeBlendFormula },
        { "runtime_consumer_resolved", record.RuntimeConsumerResolved },
        { "runtime_actor_vs_compact_payload_resolved",
          record.RuntimeActorVsCompactPayloadResolved },
        { "runtime_transition_table_decoded_from_code_bin", record.RuntimeTransitionTableDecodedFromCodeBin },
        { "final_packet_writer_resolved", record.FinalPacketWriterResolved },
    };
}

nlohmann::json NativeKankyoSubmitManagerVtableSlotJson(const NativeKankyoSubmitManagerVtableSlot& slot) {
    return {
        { "role", slot.Role },
        { "slot_offset", slot.SlotOffset },
        { "function_address", slot.FunctionAddress },
    };
}

nlohmann::json NativeKankyoRuntimeSubmitRouteJson(const NativeKankyoRuntimeSubmitRouteContract& route) {
    return {
        { "role", route.Role },
        { "submit_callsite_address", route.SubmitCallsiteAddress },
        { "runtime_pointer_play_offset", route.RuntimePointerPlayOffset },
        { "runtime_pointer_kankyo_offset", route.RuntimePointerKankyoOffset },
    };
}

nlohmann::json NativeKankyoSubmitManagerJson(const NativeKankyoSubmitManagerContract& manager) {
    nlohmann::json vtableSlots = nlohmann::json::array();
    for (const auto& slot : manager.VtableSlots) {
        vtableSlots.push_back(NativeKankyoSubmitManagerVtableSlotJson(slot));
    }
    nlohmann::json weatherRoutes = nlohmann::json::array();
    for (const auto& route : manager.VerifiedWeatherRuntimeSubmitRoutes) {
        weatherRoutes.push_back(NativeKankyoRuntimeSubmitRouteJson(route));
    }

    return {
        { "submit_wrapper_address", manager.SubmitWrapperAddress },
        { "submit_core_address", manager.SubmitCoreAddress },
        { "submit_record_write_address", manager.SubmitRecordWriteAddress },
        { "manager_initializer_address", manager.ManagerInitializerAddress },
        { "manager_vtable_address", manager.ManagerVtableAddress },
        { "context_submit_manager_offset", manager.ContextSubmitManagerOffset },
        { "manager_storage_size_bytes", manager.ManagerStorageSizeBytes },
        { "init_guard_address", manager.InitGuardAddress },
        { "global_init_address", manager.GlobalInitAddress },
        { "init_flag_address", manager.InitFlagAddress },
        { "global_context_address", manager.GlobalContextAddress },
        { "submit_manager_address", manager.SubmitManagerAddress },
        { "primary_queue_count_storage_offset", manager.PrimaryQueueCountStorageOffset },
        { "primary_queue_storage_offset", manager.PrimaryQueueStorageOffset },
        { "secondary_queue_storage_offset", manager.SecondaryQueueStorageOffset },
        { "auxiliary_queue_storage_offset", manager.AuxiliaryQueueStorageOffset },
        { "auxiliary_queue_capacity", manager.AuxiliaryQueueCapacity },
        { "small_queue_count_offset", manager.SmallQueueCountOffset },
        { "small_queue_storage_offset", manager.SmallQueueStorageOffset },
        { "small_queue_capacity", manager.SmallQueueCapacity },
        { "initial_record_state_value", manager.InitialRecordStateValue },
        { "manager_mode_flag_offset", manager.ManagerModeFlagOffset },
        { "secondary_queue_count_offset", manager.SecondaryQueueCountOffset },
        { "queue_capacity_offset", manager.QueueCapacityOffset },
        { "primary_queue_count_pointer_offset", manager.PrimaryQueueCountPointerOffset },
        { "primary_queue_base_offset", manager.PrimaryQueueBaseOffset },
        { "secondary_queue_base_offset", manager.SecondaryQueueBaseOffset },
        { "submit_record_stride_bytes", manager.SubmitRecordStrideBytes },
        { "submit_record_runtime_pointer_offset", manager.SubmitRecordRuntimePointerOffset },
        { "submit_record_valid_byte_offset", manager.SubmitRecordValidByteOffset },
        { "submit_record_valid_value", manager.SubmitRecordValidValue },
        { "runtime_vtable_slot_offset", manager.RuntimeVtableSlotOffset },
        { "runtime_submit_callback_vtable_slot_offset", manager.RuntimeSubmitCallbackVtableSlotOffset },
        { "runtime_render_drain_vtable_slot_offset", manager.RuntimeRenderDrainVtableSlotOffset },
        { "callback_context_0_offset", manager.CallbackContext0Offset },
        { "callback_context_1_offset", manager.CallbackContext1Offset },
        { "callback_context_2_offset", manager.CallbackContext2Offset },
        { "runtime_1e4_initializer_address", manager.Runtime1E4InitializerAddress },
        { "runtime_1e4_vtable_address", manager.Runtime1E4VtableAddress },
        { "runtime_1e4_submit_callback_address", manager.Runtime1E4SubmitCallbackAddress },
        { "runtime_1e4_render_drain_callback_address", manager.Runtime1E4RenderDrainCallbackAddress },
        { "runtime_28c_initializer_address", manager.Runtime28CInitializerAddress },
        { "runtime_28c_vtable_address", manager.Runtime28CVtableAddress },
        { "runtime_28c_submit_callback_address", manager.Runtime28CSubmitCallbackAddress },
        { "runtime_draw_count_setter_address", manager.RuntimeDrawCountSetterAddress },
        { "runtime_draw_payload_offset", manager.RuntimeDrawPayloadOffset },
        { "runtime_flags_offset", manager.RuntimeFlagsOffset },
        { "frame_draw_pass_address", manager.FrameDrawPassAddress },
        { "frame_draw_pass_manager_offset", manager.FrameDrawPassManagerOffset },
        { "primary_secondary_pass_0_address", manager.PrimarySecondaryPass0Address },
        { "primary_secondary_pass_1_address", manager.PrimarySecondaryPass1Address },
        { "auxiliary_pass_0_address", manager.AuxiliaryPass0Address },
        { "auxiliary_pass_1_address", manager.AuxiliaryPass1Address },
        { "small_queue_drain_address", manager.SmallQueueDrainAddress },
        { "record_array_pass_0_address", manager.RecordArrayPass0Address },
        { "record_array_pass_1_address", manager.RecordArrayPass1Address },
        { "record_array_combined_pass_address", manager.RecordArrayCombinedPassAddress },
        { "record_array_depth_sort_address", manager.RecordArrayDepthSortAddress },
        { "auxiliary_route_begin_address", manager.AuxiliaryRouteBeginAddress },
        { "auxiliary_route_end_address", manager.AuxiliaryRouteEndAddress },
        { "queue_reset_address", manager.QueueResetAddress },
        { "alternate_queue_reset_address", manager.AlternateQueueResetAddress },
        { "primary_queue_pointer_offset", manager.PrimaryQueuePointerOffset },
        { "secondary_queue_pointer_offset", manager.SecondaryQueuePointerOffset },
        { "auxiliary_queue_count_offset", manager.AuxiliaryQueueCountOffset },
        { "auxiliary_queue_pointer_offset", manager.AuxiliaryQueuePointerOffset },
        { "main_queue_capacity", manager.MainQueueCapacity },
        { "normal_route_mode_value", manager.NormalRouteModeValue },
        { "auxiliary_route_mode_value", manager.AuxiliaryRouteModeValue },
        { "frame_primary_secondary_gate_argument_value", manager.FramePrimarySecondaryGateArgumentValue },
        { "record_array_default_mode_argument_value", manager.RecordArrayDefaultModeArgumentValue },
        { "runtime_record_state_draw_handle_value", manager.RuntimeRecordStateDrawHandleValue },
        { "runtime_record_state_callback_value", manager.RuntimeRecordStateCallbackValue },
        { "runtime_draw_gate_byte_offset", manager.RuntimeDrawGateByteOffset },
        { "runtime_primary_draw_handle_offset", manager.RuntimePrimaryDrawHandleOffset },
        { "runtime_secondary_draw_handle_offset", manager.RuntimeSecondaryDrawHandleOffset },
        { "runtime_animated_draw_handle_resolver_address", manager.RuntimeAnimatedDrawHandleResolverAddress },
        { "runtime_animated_draw_handle_gate_byte_offset", manager.RuntimeAnimatedDrawHandleGateByteOffset },
        { "runtime_state_1_resolver_address", manager.RuntimeState1ResolverAddress },
        { "runtime_state_1_gate_word_offset", manager.RuntimeState1GateWordOffset },
        { "runtime_state_1_vtable_slot_offset", manager.RuntimeState1VtableSlotOffset },
        { "runtime_state_1_flags_mask", manager.RuntimeState1FlagsMask },
        { "draw_handle_submit_address", manager.DrawHandleSubmitAddress },
        { "draw_handle_pass_byte_offset", manager.DrawHandlePassByteOffset },
        { "draw_handle_material_packet_offset", manager.DrawHandleMaterialPacketOffset },
        { "draw_handle_submitted_pass_0_value", manager.DrawHandleSubmittedPass0Value },
        { "draw_handle_submitted_pass_1_value", manager.DrawHandleSubmittedPass1Value },
        { "draw_handle_packet_prep_address", manager.DrawHandlePacketPrepAddress },
        { "direct_submit_caller_count", manager.DirectSubmitCallerCount },
        { "submit_record_write_stores_runtime_pointer_and_valid_byte_only",
          manager.SubmitRecordWriteStoresRuntimePointerAndValidByteOnly },
        { "submit_core_invokes_runtime_vtable_slot", manager.SubmitCoreInvokesRuntimeVtableSlot },
        { "submit_core_uses_submit_callback_vtable_slot",
          manager.SubmitCoreUsesSubmitCallbackVtableSlot },
        { "submit_core_uses_render_drain_vtable_slot",
          manager.SubmitCoreUsesRenderDrainVtableSlot },
        { "submit_core_passes_native_callback_contexts", manager.SubmitCorePassesNativeCallbackContexts },
        { "submit_core_directly_writes_packet_prep_source",
          manager.SubmitCoreDirectlyWritesPacketPrepSource },
        { "submit_core_directly_writes_draw_handle_packet_prep_source",
          manager.SubmitCoreDirectlyWritesDrawHandlePacketPrepSource },
        { "submit_manager_connects_to_effect_draw_consumer",
          manager.SubmitManagerConnectsToEffectDrawConsumer },
        { "submit_manager_connects_to_type6_draw_command",
          manager.SubmitManagerConnectsToType6DrawCommand },
        { "packet_prep_backing_writer_resolved", manager.PacketPrepBackingWriterResolved },
        { "packet_prep_backing_writer_status", manager.PacketPrepBackingWriterStatus },
        { "weather_particle_submit_gameplay_draw_callsite_address",
          manager.WeatherParticleSubmitGameplayDrawCallsiteAddress },
        { "weather_particle_submit_function_address", manager.WeatherParticleSubmitFunctionAddress },
        { "weather_particle_submit_function_end_address", manager.WeatherParticleSubmitFunctionEndAddress },
        { "weather_particle_submit_play_gate_byte_offset", manager.WeatherParticleSubmitPlayGateByteOffset },
        { "weather_particle_submit_play_gate_requires_non_zero",
          manager.WeatherParticleSubmitPlayGateRequiresNonZero },
        { "weather_particle_submit_gate_also_particle_loop_count",
          manager.WeatherParticleSubmitGateAlsoParticleLoopCount },
        { "weather_particle_kankyo_object_base_play_offset", manager.WeatherParticleKankyoObjectBasePlayOffset },
        { "verified_weather_runtime_submit_routes", weatherRoutes },
        { "verified_kankyo_submit_callsite_addresses", manager.VerifiedKankyoSubmitCallsiteAddresses },
        { "verified_kankyo_submitted_runtime_offset", manager.VerifiedKankyoSubmittedRuntimeOffset },
        { "verified_kankyo_submitted_runtime_stride_bytes", manager.VerifiedKankyoSubmittedRuntimeStrideBytes },
        { "verified_kankyo_submitted_runtime_slot_count", manager.VerifiedKankyoSubmittedRuntimeSlotCount },
        { "unresolved_kankyo_runtime_submit_offsets", manager.UnresolvedKankyoRuntimeSubmitOffsets },
        { "non_thunder_kankyo_runtime_submits_resolved", manager.NonThunderKankyoRuntimeSubmitsResolved },
        { "vtable_slots", vtableSlots },
    };
}

nlohmann::json NativeKankyoRuntime28CParticleBatchJson(const NativeKankyoRuntime28CParticleBatchContract& batch) {
    return {
        { "callback_address", batch.CallbackAddress },
        { "callback_end_address", batch.CallbackEndAddress },
        { "runtime_flags_offset", batch.RuntimeFlagsOffset },
        { "runtime_descriptor_pointer_offset", batch.RuntimeDescriptorPointerOffset },
        { "runtime_transform_block_offset", batch.RuntimeTransformBlockOffset },
        { "runtime_average_depth_offset", batch.RuntimeAverageDepthOffset },
        { "runtime_position_array_pointer_offset", batch.RuntimePositionArrayPointerOffset },
        { "runtime_matrix_array_pointer_offset", batch.RuntimeMatrixArrayPointerOffset },
        { "runtime_color_array_pointer_offset", batch.RuntimeColorArrayPointerOffset },
        { "runtime_texcoord_array_pointer_offset", batch.RuntimeTexcoordArrayPointerOffset },
        { "runtime_batch_count_offset", batch.RuntimeBatchCountOffset },
        { "runtime_batch_capacity_offset", batch.RuntimeBatchCapacityOffset },
        { "runtime_local_vector_array_pointer_offset", batch.RuntimeLocalVectorArrayPointerOffset },
        { "runtime_average_position_base_offset", batch.RuntimeAveragePositionBaseOffset },
        { "runtime_average_position_component_count", batch.RuntimeAveragePositionComponentCount },
        { "descriptor_flags_offset", batch.DescriptorFlagsOffset },
        { "descriptor_type_offset", batch.DescriptorTypeOffset },
        { "descriptor_type_direct_matrix_value", batch.DescriptorTypeDirectMatrixValue },
        { "descriptor_type_billboard_matrix_value", batch.DescriptorTypeBillboardMatrixValue },
        { "matrix_array_record_stride_bytes", batch.MatrixArrayRecordStrideBytes },
        { "position_record_stride_bytes", batch.PositionRecordStrideBytes },
        { "local_vector_record_stride_bytes", batch.LocalVectorRecordStrideBytes },
        { "color_record_stride_bytes", batch.ColorRecordStrideBytes },
        { "texcoord_record_stride_bytes", batch.TexcoordRecordStrideBytes },
        { "quad_vertex_count", batch.QuadVertexCount },
        { "draw_count_per_visible_batch", batch.DrawCountPerVisibleBatch },
        { "draw_count_setter_address", batch.DrawCountSetterAddress },
        { "output_position_buffer_resolver_address", batch.OutputPositionBufferResolverAddress },
        { "output_color_buffer_resolver_address", batch.OutputColorBufferResolverAddress },
        { "output_texcoord_buffer_resolver_address", batch.OutputTexcoordBufferResolverAddress },
        { "optional_secondary_position_buffer_resolver_address",
          batch.OptionalSecondaryPositionBufferResolverAddress },
        { "skip_secondary_position_runtime_flag_mask", batch.SkipSecondaryPositionRuntimeFlagMask },
        { "skip_color_runtime_flag_mask", batch.SkipColorRuntimeFlagMask },
        { "skip_texcoord_runtime_flag_mask", batch.SkipTexcoordRuntimeFlagMask },
        { "descriptor_skip_secondary_position_flag_mask", batch.DescriptorSkipSecondaryPositionFlagMask },
        { "clears_batch_count_after_emit", batch.ClearsBatchCountAfterEmit },
        { "resolves_non_thunder_submit_callsite", batch.ResolvesNonThunderSubmitCallsite },
        { "enqueue_writer_resolved", batch.EnqueueWriterResolved },
        { "object_transform_replicates_color_for_all_quad_corners",
          batch.ObjectTransformReplicatesColorForAllQuadCorners },
    };
}

nlohmann::json NativeKankyoRuntimeBridgeJson(const NativeKankyoRuntimeBridgeContract& contract) {
    nlohmann::json helpers = nlohmann::json::array();
    for (const auto& helper : contract.RuntimeHelpers) {
        helpers.push_back(NativeKankyoRuntimeHelperJson(helper));
    }

    nlohmann::json bindings = nlohmann::json::array();
    for (const auto& binding : contract.Bindings) {
        bindings.push_back(NativeKankyoRuntimeBindingJson(binding));
    }

    nlohmann::json runtimeBindingSlots = nlohmann::json::array();
    for (const auto& slot : contract.RuntimeBindingSlots) {
        runtimeBindingSlots.push_back(NativeKankyoRuntimeBindingSlotJson(slot));
    }

    return {
        { "source_kind", contract.SourceKind },
        { "runtime_helpers", helpers },
        { "runtime_effect_wrapper", NativeKankyoRuntimeEffectWrapperJson(contract.RuntimeEffectWrapper) },
        { "bindings", bindings },
        { "runtime_binding_slots", runtimeBindingSlots },
        { "thunder_update", NativeKankyoThunderUpdateJson(contract.ThunderUpdate) },
        { "descriptor_materialization",
          NativeKankyoDescriptorMaterializationJson(contract.DescriptorMaterialization) },
        { "ctxb_descriptor_binding", NativeKankyoCtxbDescriptorBindingJson(contract.CtxbDescriptorBinding) },
        { "draw_command", NativeKankyoDrawCommandJson(contract.DrawCommand) },
        { "effect_draw_consumer", NativeKankyoEffectDrawConsumerJson(contract.EffectDrawConsumer) },
        { "general_light_list_emitter",
          NativePicaGeneralLightListEmitterJson(contract.GeneralLightListEmitter) },
        { "lighting_register_emitter",
          NativePicaLightingRegisterEmitterJson(contract.LightingRegisterEmitter) },
        { "packet_prep", NativeKankyoPacketPrepJson(contract.PacketPrep) },
        { "runtime_source_vector", NativeKankyoRuntimeSourceVectorJson(contract.RuntimeSourceVector) },
        { "runtime_light_packet_pack", NativeKankyoRuntimeLightPacketPackJson(contract.RuntimeLightPacketPack) },
        { "packet_default_record", NativeKankyoPacketDefaultRecordJson(contract.PacketDefaultRecord) },
        { "material_draw_state", NativeKankyoMaterialDrawStateJson(contract.MaterialDrawState) },
        { "packet_copy_dataflow_audit",
          NativeKankyoPacketCopyDataflowAuditJson(contract.PacketCopyDataflowAudit) },
        { "provider_table", NativeKankyoProviderTableJson(contract.ProviderTable) },
        { "lensflare_runtime_list",
          NativeKankyoLensflareRuntimeListJson(contract.LensflareRuntimeList) },
        { "moon_runtime", NativeKankyoMoonRuntimeJson(contract.MoonRuntime) },
        { "material_scalar_emit", NativePicaMaterialScalarEmitJson(contract.MaterialScalarEmit) },
        { "cmb_lut_asset_decode", NativePicaCmbLutAssetDecodeJson(contract.CmbLutAssetDecode) },
        { "draw_handle_submit", NativeKankyoDrawHandleSubmitJson(contract.DrawHandleSubmit) },
        { "gameplay_draw_sequence", NativeKankyoGameplayDrawSequenceJson(contract.GameplayDrawSequence) },
        { "render_record_scheduler", NativeKankyoRenderRecordSchedulerJson(contract.RenderRecordScheduler) },
        { "submit_manager", NativeKankyoSubmitManagerJson(contract.SubmitManager) },
        { "runtime_28c_particle_batch",
          NativeKankyoRuntime28CParticleBatchJson(contract.Runtime28CParticleBatch) },
        { "environment_vector", NativeKankyoEnvironmentVectorJson(contract.EnvironmentVector) },
        { "environment_light_setting_state",
          NativeKankyoEnvironmentLightSettingStateJson(contract.EnvironmentLightSettingState) },
        { "shadow_depth_register_state",
          NativePicaShadowDepthRegisterStateJson(contract.ShadowDepthRegisterState) },
        { "zsi_light_settings_record",
          NativeZsiLightSettingsRecordJson(contract.ZsiLightSettingsRecord) },
    };
}

nlohmann::json CtxbTextureCatalogCtxbJson(const std::filesystem::path& archivePath,
                                          const ZarFileEntry& file) {
    nlohmann::json out = {
        { "entry_index", file.Index },
        { "entry_name", file.Name },
        { "entry_type", file.TypeName },
        { "entry_type_local_index",
          file.TypeLocalIndex == std::numeric_limits<uint32_t>::max() ? nlohmann::json(nullptr)
                                                                      : nlohmann::json(file.TypeLocalIndex) },
        { "entry_offset", file.Offset },
        { "entry_size", file.Size },
        { "parse_status", "not_parsed" },
    };

    try {
        const auto bytes = ExtractZarFileBytes(archivePath, file.Name);
        const auto texture = ParseCtxbTextureBytes(bytes, archivePath.string() + "!" + file.Name);
        const auto descriptorSlotZero = BuildNativeCtxbDescriptorSlot(texture, 0);

        out["parse_status"] = "parsed";
        out["source"] = texture.Source;
        out["name"] = texture.Name;
        out["declared_file_size"] = texture.DeclaredFileSize;
        out["header_version"] = texture.HeaderVersion;
        out["tex_chunk_offset"] = texture.TexChunkOffset;
        out["payload_offset"] = texture.PayloadOffset;
        out["payload_size"] = texture.PayloadSize;
        out["flags"] = texture.Flags;
        out["width"] = texture.Width;
        out["height"] = texture.Height;
        out["texture_format"] = texture.TextureFormat;
        out["data_type"] = texture.DataType;
        out["sampler_word"] = texture.SamplerWord;
        out["rgba8_decoded"] = texture.Rgba8Decoded;
        out["rgba8_size"] = texture.Rgba8.size();
        out["native_descriptor_slot_contract"] = {
            { "source_kind", "oot3d_code_bin_0x00348A64_descriptor_slot_layout" },
            { "caller_slot_required", true },
            { "slot_zero_layout_sample", NativeCtxbDescriptorSlotJson(descriptorSlotZero) },
        };
    } catch (const std::exception& exc) {
        out["parse_status"] = "parse_failed";
        out["parse_error"] = exc.what();
    }

    return out;
}

std::pair<std::string, std::string> NativeTextureCatalogSourceParts(std::string_view source) {
    const auto separator = source.find('!');
    if (separator == std::string_view::npos) {
        return { std::string(source), "" };
    }
    return { std::string(source.substr(0, separator)), std::string(source.substr(separator + 1)) };
}

nlohmann::json CmbTextureCatalogLoadedModelJson(const CmbModel& model, std::string_view sourceKind) {
    const auto [sourcePath, sourceEntry] = NativeTextureCatalogSourceParts(model.Source);

    nlohmann::json textures = nlohmann::json::array();
    for (const auto& texture : model.Textures) {
        textures.push_back(CmbTextureCatalogTextureJson(texture));
    }

    nlohmann::json materials = nlohmann::json::array();
    for (const auto& material : model.Materials) {
        materials.push_back(CmbTextureCatalogMaterialJson(model, material));
    }

    return {
        { "source_kind", sourceKind },
        { "source", model.Source },
        { "source_path", sourcePath },
        { "source_entry", sourceEntry },
        { "parse_status", "parsed" },
        { "model_name", model.Name },
        { "mesh_count", model.Meshes.size() },
        { "shape_count", model.Shapes.size() },
        { "primitive_count", model.PrimitiveCount() },
        { "triangle_count", model.TriangleCount() },
        { "vertex_count", model.VertexCount() },
        { "texture_count", model.Textures.size() },
        { "decoded_texture_count", NativeDemoDecodedTextureCount(model) },
        { "material_count", model.Materials.size() },
        { "texture_env_table_record_count", model.TextureEnvSettings.size() },
        { "lut_chunk_decoded", model.Luts.Decoded },
        { "lut_chunk_size", model.Luts.ChunkSize },
        { "lut_record_count", model.Luts.Records.size() },
        { "luts", CmbTextureCatalogLutsJson(model.Luts) },
        { "textures", textures },
        { "materials", materials },
        { "draw_primitives", CmbTextureCatalogDrawPrimitivesJson(model) },
    };
}

struct NativeArchiveTextureCatalogInput {
    std::filesystem::path ArchivePath;
    nlohmann::json SourceRefs = nlohmann::json::array();
};

void AppendNativeArchiveTextureCatalogInput(std::vector<NativeArchiveTextureCatalogInput>& inputs,
                                            const std::filesystem::path& archivePath,
                                            nlohmann::json sourceRef) {
    if (archivePath.empty()) {
        return;
    }

    const auto key = archivePath.lexically_normal().string();
    for (auto& input : inputs) {
        if (input.ArchivePath.lexically_normal().string() == key) {
            input.SourceRefs.push_back(std::move(sourceRef));
            return;
        }
    }

    NativeArchiveTextureCatalogInput input;
    input.ArchivePath = archivePath;
    input.SourceRefs.push_back(std::move(sourceRef));
    inputs.push_back(std::move(input));
}

nlohmann::json NativeArchiveTextureCatalogArchiveJson(const NativeArchiveTextureCatalogInput& input) {
    nlohmann::json out = {
        { "archive_path", input.ArchivePath.string() },
        { "source_refs", input.SourceRefs },
        { "archive_available", std::filesystem::is_regular_file(input.ArchivePath) },
        { "archive_parse_status", "not_parsed" },
    };

    if (!out["archive_available"].get<bool>()) {
        out["archive_parse_status"] = "archive_missing";
        return out;
    }

    try {
        const auto archive = ParseZarArchiveFile(input.ArchivePath);
        nlohmann::json cmbs = nlohmann::json::array();
        nlohmann::json ctxbs = nlohmann::json::array();
        size_t cmbCount = 0;
        size_t ctxbCount = 0;
        std::array<bool, 8> kankyoCtxbSetPresent = {};
        for (const auto& file : archive.Files) {
            if (ZarFileEntryHasType(file, "cmb")) {
                ++cmbCount;
                cmbs.push_back(CmbTextureCatalogCmbJson(input.ArchivePath, file));
            } else if (ZarFileEntryHasType(file, "ctxb")) {
                ++ctxbCount;
                if (file.TypeLocalIndex >= 0x44 && file.TypeLocalIndex <= 0x4B) {
                    kankyoCtxbSetPresent[file.TypeLocalIndex - 0x44] = true;
                }
                ctxbs.push_back(CtxbTextureCatalogCtxbJson(input.ArchivePath, file));
            }
        }

        const bool hasNativeKankyoCtxbSet =
            std::all_of(kankyoCtxbSetPresent.begin(), kankyoCtxbSetPresent.end(), [](bool present) {
                return present;
            });

        out["archive_parse_status"] = "parsed";
        out["zar_file_count"] = archive.Files.size();
        out["cmb_count"] = cmbCount;
        out["ctxb_count"] = ctxbCount;
        out["native_kankyo_ctxb_set_present"] = hasNativeKankyoCtxbSet;
        if (hasNativeKankyoCtxbSet) {
            out["native_kankyo_runtime_bridge_contract"] =
                NativeKankyoRuntimeBridgeJson(BuildNativeKankyoRuntimeBridgeContract());
        }
        out["cmbs"] = cmbs;
        out["ctxbs"] = ctxbs;
    } catch (const std::exception& exc) {
        out["archive_parse_status"] = "parse_failed";
        out["parse_error"] = exc.what();
    }

    return out;
}

nlohmann::json NativeArchiveTextureCatalogJson(const Oot3dNativeDemoScene& scene) {
    const auto& graph = scene.AssetGraph;
    std::vector<NativeArchiveTextureCatalogInput> inputs;
    for (const auto& object : graph.RoomObjects) {
        if (!object.ArchiveAvailable) {
            continue;
        }
        AppendNativeArchiveTextureCatalogInput(
            inputs, object.ArchivePath,
            {
                { "source_kind", "room_object" },
                { "room_entry_index", object.Index },
                { "semantic_id", object.ObjectId },
                { "semantic_name", object.ObjectName },
            });
    }
    for (const auto& actor : graph.RoomActors) {
        if (!actor.ArchiveAvailable) {
            continue;
        }
        AppendNativeArchiveTextureCatalogInput(
            inputs, actor.ArchivePath,
            {
                { "source_kind", "room_actor" },
                { "room_entry_index", actor.Index },
                { "semantic_id", actor.ActorId },
                { "semantic_name", actor.ActorName },
            });
    }

    nlohmann::json archives = nlohmann::json::array();
    for (const auto& input : inputs) {
        archives.push_back(NativeArchiveTextureCatalogArchiveJson(input));
    }

    nlohmann::json loadedModels = nlohmann::json::array();
    if (!scene.RoomModel.Source.empty()) {
        loadedModels.push_back(CmbTextureCatalogLoadedModelJson(scene.RoomModel, "room_zsi_embedded_cmb"));
    }
    if (!scene.LinkModel.Source.empty()) {
        loadedModels.push_back(CmbTextureCatalogLoadedModelJson(scene.LinkModel, "link_actor_model_cmb"));
    }

    return {
        { "available", graph.Valid },
        { "source_kind", "oot3d_native_resolved_texture_catalog" },
        { "uses_runtime_n64_asset_substitution", false },
        { "archive_count", archives.size() },
        { "archives", archives },
        { "loaded_model_count", loadedModels.size() },
        { "loaded_models", loadedModels },
        { "basis",
          "resolved native OOT3D room object/actor ZAR archives and loaded CMB/CTXB entries are enumerated through the engine native decoders so texture/material/effect mismatches can be traced to native asset entries rather than to emulator frame state" },
    };
}

bool ShbinHasTexCoord0WOutput(const ShbinShaderBinary& shader) {
    for (const auto& program : shader.Programs) {
        for (const auto& output : program.Outputs) {
            if (output.Type == 4 && output.SemanticName == "out.tex0w") {
                return true;
            }
        }
    }
    return false;
}

nlohmann::json ShbinOutputsJson(const ShbinShaderBinary& shader) {
    nlohmann::json outputs = nlohmann::json::array();
    for (size_t programIndex = 0; programIndex < shader.Programs.size(); ++programIndex) {
        const auto& program = shader.Programs[programIndex];
        for (const auto& output : program.Outputs) {
            outputs.push_back({
                { "program_index", programIndex },
                { "type", output.Type },
                { "semantic", output.SemanticName },
                { "register_id", output.RegisterId },
                { "component_mask", output.ComponentMask },
                { "descriptor", output.Descriptor },
            });
        }
    }
    return outputs;
}

nlohmann::json ShbinUniformsJson(const ShbinShaderBinary& shader) {
    nlohmann::json uniforms = nlohmann::json::array();
    for (size_t programIndex = 0; programIndex < shader.Programs.size(); ++programIndex) {
        const auto& program = shader.Programs[programIndex];
        for (const auto& uniform : program.Uniforms) {
            uniforms.push_back({
                { "program_index", programIndex },
                { "name", uniform.Name },
                { "register_start", uniform.RegisterStart },
                { "register_end", uniform.RegisterEnd },
            });
        }
    }
    return uniforms;
}

size_t ShbinOutputCount(const ShbinShaderBinary& shader) {
    size_t count = 0;
    for (const auto& program : shader.Programs) {
        count += program.Outputs.size();
    }
    return count;
}

size_t ShbinUniformCount(const ShbinShaderBinary& shader) {
    size_t count = 0;
    for (const auto& program : shader.Programs) {
        count += program.Uniforms.size();
    }
    return count;
}

nlohmann::json Oot3dNativeDemoSceneSummaryToJson(const Oot3dNativeDemoScene& scene) {
    const auto renderScene = BuildOot3dNativeDemoRenderScene(scene);
    Oot3dNativeRecordingRenderBackend recordingBackend;
    const auto rendererSubmission = SubmitOot3dNativeDemoRenderScene(renderScene, recordingBackend);
    nlohmann::json linkCsabClips = nlohmann::json::array();
    size_t linkFacebTrackCount = 0;
    size_t linkFacebEventCount = 0;
    for (size_t clipIndex = 0; clipIndex < scene.LinkCsabClips.size(); ++clipIndex) {
        const auto& clip = scene.LinkCsabClips[clipIndex];
        if (clip.FacebTrackAvailable) {
            ++linkFacebTrackCount;
            linkFacebEventCount += clip.FacebTrack.Events.size();
        }
        linkCsabClips.push_back({
            { "index", clipIndex },
            { "id", clip.Id },
            { "csab", clip.CsabName },
            { "resident_byte_count", clip.Bytes.size() },
            { "frame_count", clip.Metadata.FrameCount },
            { "animated_bone_count", clip.Metadata.AnimatedBoneCount },
            { "skeleton_bone_count", clip.Metadata.SkeletonBoneCount },
            { "is_standing", clipIndex == scene.LinkStandingClipIndex },
            { "is_walk", clipIndex == scene.LinkWalkClipIndex },
            { "is_movement", clipIndex == scene.LinkMovementClipIndex },
            { "is_run", clipIndex == scene.LinkMovementClipIndex },
            { "is_walk_end_left", clipIndex == scene.LinkWalkEndLeftClipIndex },
            { "is_walk_end_right", clipIndex == scene.LinkWalkEndRightClipIndex },
            { "faceb_track_available", clip.FacebTrackAvailable },
            { "faceb", clip.FacebName },
            { "faceb_entry_count", clip.FacebTrack.Events.size() },
            { "faceb_size_matches_entry_count", clip.FacebTrack.SizeMatchesEntryCount },
        });
    }
    nlohmann::json linkMaterialAnimations = nlohmann::json::array();
    for (size_t animationIndex = 0; animationIndex < scene.LinkMaterialAnimations.size(); ++animationIndex) {
        const auto& animation = scene.LinkMaterialAnimations[animationIndex];
        const auto binding = BuildCmabMaterialAnimationBinding(scene.LinkModel, animation);
        size_t componentScalarTrackCount = 0;
        size_t decodedComponentScalarTrackCount = 0;
        size_t componentScalarKeyframeCount = 0;
        size_t nativeSourceCurveCount = 0;
        size_t decodedNativeSourceCurveCount = 0;
        size_t nativeSourceCurvePointCount = 0;
        nlohmann::json nativeTypeCounts = nlohmann::json::object();
        nlohmann::json nativeTypeStatusCounts = nlohmann::json::object();
        nlohmann::json targetMaterialCounts = nlohmann::json::object();
        nlohmann::json targetComponentOrStageCounts = nlohmann::json::object();
        nlohmann::json targetSelectorStatusCounts = nlohmann::json::object();
        nlohmann::json targetSelectorCounts = nlohmann::json::object();
        nlohmann::json nativeValueKindCounts = nlohmann::json::object();
        nlohmann::json nativeChannelOffsetCountCounts = nlohmann::json::object();
        nlohmann::json nativeSourceCurveTypeCounts = nlohmann::json::object();
        nlohmann::json nativeSourceCurveStatusCounts = nlohmann::json::object();
        size_t nativeChannelOffsetCount = 0;
        size_t nativeChannelPresentOffsetCount = 0;
        for (const auto& record : animation.MmadRecords) {
            componentScalarTrackCount += record.ScalarTracks.size();
            nativeSourceCurveCount += record.NativeSourceCurves.size();
            const auto nativeTypeKey = std::to_string(record.NativeType);
            nativeTypeCounts[nativeTypeKey] = nativeTypeCounts.value(nativeTypeKey, 0) + 1;
            const auto nativeTypeStatusKey =
                record.NativeTypeFactorySupported ? "factory_supported_1_to_5" : "factory_returns_null";
            nativeTypeStatusCounts[nativeTypeStatusKey] = nativeTypeStatusCounts.value(nativeTypeStatusKey, 0) + 1;
            const auto targetMaterialKey = std::to_string(record.TargetMaterialIndex);
            targetMaterialCounts[targetMaterialKey] = targetMaterialCounts.value(targetMaterialKey, 0) + 1;
            const auto targetComponentKey = std::to_string(record.TargetComponentOrStageIndex);
            targetComponentOrStageCounts[targetComponentKey] =
                targetComponentOrStageCounts.value(targetComponentKey, 0) + 1;
            const auto targetSelectorStatusKey = record.TargetSelectorPresent ? "selector_present" : "selector_absent";
            targetSelectorStatusCounts[targetSelectorStatusKey] =
                targetSelectorStatusCounts.value(targetSelectorStatusKey, 0) + 1;
            if (record.TargetSelectorPresent) {
                const auto targetSelectorKey = std::to_string(record.TargetSelector);
                targetSelectorCounts[targetSelectorKey] = targetSelectorCounts.value(targetSelectorKey, 0) + 1;
            }
            const auto nativeValueKindKey =
                record.NativeValueKind.empty() ? "unsupported_or_truncated" : record.NativeValueKind;
            nativeValueKindCounts[nativeValueKindKey] =
                nativeValueKindCounts.value(nativeValueKindKey, 0) + 1;
            const auto nativeChannelOffsetCountKey = std::to_string(record.NativeChannelOffsets.size());
            nativeChannelOffsetCountCounts[nativeChannelOffsetCountKey] =
                nativeChannelOffsetCountCounts.value(nativeChannelOffsetCountKey, 0) + 1;
            nativeChannelOffsetCount += record.NativeChannelOffsets.size();
            nativeChannelPresentOffsetCount +=
                std::count_if(record.NativeChannelOffsets.begin(), record.NativeChannelOffsets.end(),
                              [](const auto& channelOffset) { return channelOffset.Present; });
            for (const auto& curve : record.NativeSourceCurves) {
                const auto sourceCurveTypeKey = curve.Decoded ? std::to_string(curve.Type) : "undecoded";
                nativeSourceCurveTypeCounts[sourceCurveTypeKey] =
                    nativeSourceCurveTypeCounts.value(sourceCurveTypeKey, 0) + 1;
                const auto sourceCurveStatusKey = curve.Status.empty() ? "unknown" : curve.Status;
                nativeSourceCurveStatusCounts[sourceCurveStatusKey] =
                    nativeSourceCurveStatusCounts.value(sourceCurveStatusKey, 0) + 1;
                if (curve.Decoded) {
                    ++decodedNativeSourceCurveCount;
                    nativeSourceCurvePointCount += curve.Points.size();
                }
            }
            for (const auto& track : record.ScalarTracks) {
                if (track.KeyframesDecoded) {
                    ++decodedComponentScalarTrackCount;
                    componentScalarKeyframeCount += track.Keyframes.size();
                }
            }
        }
        linkMaterialAnimations.push_back({
            { "index", animationIndex },
            { "source", animation.Source },
            { "frame_count_candidate", animation.FrameCountCandidate },
            { "loop_mode_candidate", animation.LoopModeCandidate },
            { "mads_record_offset_count", animation.MadsRecordOffsets.size() },
            { "mmad_record_count", animation.MmadRecords.size() },
            { "mmad_native_type_counts", nativeTypeCounts },
            { "mmad_native_type_status_counts", nativeTypeStatusCounts },
            { "mmad_target_material_counts", targetMaterialCounts },
            { "mmad_target_component_or_stage_counts", targetComponentOrStageCounts },
            { "mmad_target_selector_status_counts", targetSelectorStatusCounts },
            { "mmad_target_selector_counts", targetSelectorCounts },
            { "mmad_native_value_kind_counts", nativeValueKindCounts },
            { "mmad_native_channel_offset_count_counts", nativeChannelOffsetCountCounts },
            { "mmad_native_channel_offset_count", nativeChannelOffsetCount },
            { "mmad_native_channel_offset_present_count", nativeChannelPresentOffsetCount },
            { "mmad_native_source_curve_count", nativeSourceCurveCount },
            { "mmad_native_source_curve_decoded_count", decodedNativeSourceCurveCount },
            { "mmad_native_source_curve_type_counts", nativeSourceCurveTypeCounts },
            { "mmad_native_source_curve_status_counts", nativeSourceCurveStatusCounts },
            { "mmad_native_source_curve_point_count", nativeSourceCurvePointCount },
            { "mmad_component_scalar_track_count", componentScalarTrackCount },
            { "mmad_component_scalar_track_decoded_count", decodedComponentScalarTrackCount },
            { "mmad_component_scalar_keyframe_count", componentScalarKeyframeCount },
            { "string_table_name_count", animation.StringTableNames.size() },
            { "embedded_texture_count", animation.EmbeddedTextures.size() },
            { "texture_payload_decoded", animation.TexturePayloadDecoded },
            { "binding_status", binding.Status },
            { "binding_role", binding.Role },
            { "binding_target_resolved", binding.TargetResolved },
            { "binding_runtime_lane_decoded", binding.RuntimeLaneBindingDecoded },
            { "binding_texture_swap_track_decoded", binding.TextureSwapTrackDecoded },
            { "binding_texture_swap_frames_resolved", binding.TextureSwapFramesResolved },
            { "binding_texture_swap_frame_count", binding.TextureSwapFrames.size() },
            { "binding_target_material_count", binding.TargetMaterialIndices.size() },
            { "binding_runtime_lane_count", binding.NativeRuntimeMaterialLaneIndices.size() },
            { "binding_target_texture_count", binding.TargetTextureIndices.size() },
            { "binding_embedded_texture_count", binding.EmbeddedTextureIndices.size() },
        });
    }
    size_t nativeCameraTableProfileCount = 0;
    size_t nativeCameraTableFunctionCount = 0;
    if (scene.NativeCameraTable.is_object()) {
        if (scene.NativeCameraTable.contains("normal_mode_profiles") &&
            scene.NativeCameraTable.at("normal_mode_profiles").is_array()) {
            nativeCameraTableProfileCount = scene.NativeCameraTable.at("normal_mode_profiles").size();
        }
        if (scene.NativeCameraTable.contains("function_by_setting") &&
            scene.NativeCameraTable.at("function_by_setting").is_object()) {
            nativeCameraTableFunctionCount = scene.NativeCameraTable.at("function_by_setting").size();
        }
    }
    size_t nativePicaLightingByteGroupSemanticCount = 0;
    size_t nativePicaLightingFloatParamSemanticCount = 0;
    size_t nativePicaLightingDebugModeCount = 0;
    size_t nativePicaLightingEngineModeCount = 0;
    if (scene.NativePicaLightingSemantics.is_object()) {
        if (scene.NativePicaLightingSemantics.contains("byte_groups") &&
            scene.NativePicaLightingSemantics.at("byte_groups").is_array()) {
            nativePicaLightingByteGroupSemanticCount = scene.NativePicaLightingSemantics.at("byte_groups").size();
        }
        if (scene.NativePicaLightingSemantics.contains("float_params") &&
            scene.NativePicaLightingSemantics.at("float_params").is_array()) {
            nativePicaLightingFloatParamSemanticCount = scene.NativePicaLightingSemantics.at("float_params").size();
        }
        if (scene.NativePicaLightingSemantics.contains("debug_modes") &&
            scene.NativePicaLightingSemantics.at("debug_modes").is_object()) {
            nativePicaLightingDebugModeCount = scene.NativePicaLightingSemantics.at("debug_modes").size();
        }
        if (scene.NativePicaLightingSemantics.contains("engine_modes") &&
            scene.NativePicaLightingSemantics.at("engine_modes").is_object()) {
            nativePicaLightingEngineModeCount = scene.NativePicaLightingSemantics.at("engine_modes").size();
        }
    }
    size_t nativePicaRegisterTraceWriteCount = 0;
    size_t nativePicaRegisterTraceRegisterCount = 0;
    size_t nativePicaRegisterTraceUniformCount = 0;
    if (scene.NativePicaRegisterTrace.is_object()) {
        if (scene.NativePicaRegisterTrace.contains("writes") &&
            scene.NativePicaRegisterTrace.at("writes").is_array()) {
            nativePicaRegisterTraceWriteCount = scene.NativePicaRegisterTrace.at("writes").size();
        }
        if (scene.NativePicaRegisterTrace.contains("registers") &&
            scene.NativePicaRegisterTrace.at("registers").is_object()) {
            nativePicaRegisterTraceRegisterCount = scene.NativePicaRegisterTrace.at("registers").size();
        }
        if (scene.NativePicaRegisterTrace.contains("uniforms") &&
            scene.NativePicaRegisterTrace.at("uniforms").is_object()) {
            nativePicaRegisterTraceUniformCount += scene.NativePicaRegisterTrace.at("uniforms").size();
        }
        if (scene.NativePicaRegisterTrace.contains("dmp_uniforms") &&
            scene.NativePicaRegisterTrace.at("dmp_uniforms").is_object()) {
            nativePicaRegisterTraceUniformCount += scene.NativePicaRegisterTrace.at("dmp_uniforms").size();
        }
    }

    return {
        { "format", "oot3d_native_mesh_viewer_v1" },
        { "status", "valid" },
        { "runtime_n64_asset_substitution_used", false },
        { "shipwright_replacement_path_used", false },
        { "uses_glb_runtime_meshes", false },
        { "uses_collision", scene.Collision.Valid },
        { "room",
          {
              { "source_kind", "oot3d_zsi_embedded_cmb" },
              { "source", scene.RoomZsiPath.string() },
              { "material_animation_archive", scene.RoomMaterialAnimationArchivePath.string() },
              { "material_animation_room_index", scene.RoomMaterialAnimationRoomIndex },
              { "material_animation_status", scene.RoomMaterialAnimationStatus },
              { "material_animation_count", scene.RoomMaterialAnimations.size() },
              { "material_animation_names", scene.RoomMaterialAnimationNames },
              { "mesh_count", scene.RoomModel.Meshes.size() },
              { "shape_count", scene.RoomModel.Shapes.size() },
              { "primitive_count", scene.RoomModel.PrimitiveCount() },
              { "triangle_count", scene.RoomModel.TriangleCount() },
              { "vertex_count", scene.RoomModel.VertexCount() },
              { "texture_count", scene.RoomModel.Textures.size() },
              { "decoded_texture_count", NativeDemoDecodedTextureCount(scene.RoomModel) },
              { "material_count", scene.RoomModel.Materials.size() },
              { "lut_chunk_decoded", scene.RoomModel.Luts.Decoded },
              { "lut_chunk_size", scene.RoomModel.Luts.ChunkSize },
              { "lut_record_count", scene.RoomModel.Luts.Records.size() },
              { "luts", CmbTextureCatalogLutsJson(scene.RoomModel.Luts) },
              { "bounds", BoundsJson(scene.RoomBounds) },
          } },
        { "native_camera_table",
          {
              { "available", scene.NativeCameraTableAvailable },
              { "path", scene.NativeCameraTablePath.string() },
              { "format", scene.NativeCameraTableFormat },
              { "source_kind", scene.NativeCameraTableSourceKind },
              { "repo_root_default_used", scene.NativeCameraTableRepoRootDefaultUsed },
              { "uses_runtime_n64_asset_substitution", false },
              { "normal_mode_profile_count", nativeCameraTableProfileCount },
              { "function_mapping_count", nativeCameraTableFunctionCount },
          } },
        { "native_pica_lighting_semantics",
          {
              { "available", scene.NativePicaLightingSemanticsAvailable },
              { "path", scene.NativePicaLightingSemanticsPath.string() },
              { "format", scene.NativePicaLightingSemanticsFormat },
              { "source_kind", scene.NativePicaLightingSemanticsSourceKind },
              { "repo_root_default_used", scene.NativePicaLightingSemanticsRepoRootDefaultUsed },
              { "uses_runtime_n64_asset_substitution", false },
              { "byte_group_semantic_count", nativePicaLightingByteGroupSemanticCount },
              { "float_param_semantic_count", nativePicaLightingFloatParamSemanticCount },
              { "engine_mode_count", nativePicaLightingEngineModeCount },
              { "engine_modes",
                scene.NativePicaLightingSemantics.is_object() &&
                        scene.NativePicaLightingSemantics.contains("engine_modes")
                    ? scene.NativePicaLightingSemantics.at("engine_modes")
                    : nlohmann::json::object() },
              { "debug_mode_count", nativePicaLightingDebugModeCount },
          } },
        { "native_pica_register_trace",
          {
              { "available", scene.NativePicaRegisterTraceAvailable },
              { "path", scene.NativePicaRegisterTracePath.string() },
              { "format", scene.NativePicaRegisterTraceFormat },
              { "source_kind", scene.NativePicaRegisterTraceSourceKind },
              { "repo_root_default_used", scene.NativePicaRegisterTraceRepoRootDefaultUsed },
              { "uses_runtime_n64_asset_substitution", false },
              { "write_count", nativePicaRegisterTraceWriteCount },
              { "register_count", nativePicaRegisterTraceRegisterCount },
              { "uniform_count", nativePicaRegisterTraceUniformCount },
          } },
        { "native_cmb_vshader_shbin",
          {
              { "available", scene.NativeCmbVShaderShbinAvailable },
              { "path", scene.NativeCmbVShaderShbinPath.string() },
              { "format", scene.NativeCmbVShaderShbinFormat },
              { "source_kind", scene.NativeCmbVShaderShbinSourceKind },
              { "derived_from_room_zsi_path", scene.NativeCmbVShaderShbinDerivedFromRoomZsiPath },
              { "repo_root_default_used", scene.NativeCmbVShaderShbinRepoRootDefaultUsed },
              { "uses_runtime_n64_asset_substitution", false },
              { "program_count", scene.NativeCmbVShader.ProgramCount },
              { "dvlp_version", scene.NativeCmbVShader.DvlpVersion },
              { "program_code_word_count", scene.NativeCmbVShader.ProgramCode.size() },
              { "swizzle_count", scene.NativeCmbVShader.Swizzles.size() },
              { "filename_count", scene.NativeCmbVShader.Filenames.size() },
              { "filenames", scene.NativeCmbVShader.Filenames },
              { "output_count", ShbinOutputCount(scene.NativeCmbVShader) },
              { "uniform_count", ShbinUniformCount(scene.NativeCmbVShader) },
              { "texcoord0_w_output_decoded", ShbinHasTexCoord0WOutput(scene.NativeCmbVShader) },
              { "outputs", scene.NativeCmbVShaderShbinAvailable ? ShbinOutputsJson(scene.NativeCmbVShader)
                                                                 : nlohmann::json::array() },
              { "uniforms", scene.NativeCmbVShaderShbinAvailable ? ShbinUniformsJson(scene.NativeCmbVShader)
                                                                  : nlohmann::json::array() },
          } },
        { "native_pica_lighting", NativePicaLightingStateJson(scene.NativePicaLighting) },
        { "collision", Oot3dNativeDemoCollisionSceneSummaryToJson(scene.Collision) },
        { "link_child",
          {
              { "source_kind", "oot3d_zar_cmb_csab" },
              { "manifest", scene.LinkManifestPath.string() },
              { "cmb", scene.LinkCmbName },
              { "manifest_csab", scene.LinkManifestCsabName },
              { "standing_csab", scene.LinkStandingCsabName },
              { "standing_csab_resident_byte_count", scene.LinkStandingCsabBytes.size() },
              { "csab_clip_count", scene.LinkCsabClips.size() },
              { "faceb_track_count", linkFacebTrackCount },
              { "faceb_event_count", linkFacebEventCount },
              { "material_animation_count", scene.LinkMaterialAnimations.size() },
              { "material_animations", linkMaterialAnimations },
              { "standing_clip_index", scene.LinkStandingClipIndex },
              { "walk_clip_index", scene.LinkWalkClipIndex },
              { "movement_clip_index", scene.LinkMovementClipIndex },
              { "walk_end_left_clip_index",
                scene.LinkWalkEndLeftClipIndex == kInvalidLinkCsabClipIndex
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(scene.LinkWalkEndLeftClipIndex) },
              { "walk_end_right_clip_index",
                scene.LinkWalkEndRightClipIndex == kInvalidLinkCsabClipIndex
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(scene.LinkWalkEndRightClipIndex) },
              { "csab_clips", linkCsabClips },
              { "standing_pose_frame", scene.LinkStandingPose.Frame },
              { "standing_pose_valid", scene.LinkStandingPose.Valid },
              { "mesh_count", scene.LinkModel.Meshes.size() },
              { "selected_mesh_count", scene.LinkMeshIndices.size() },
              { "selected_rigid_primitive_count",
                NativeDemoSelectedPrimitiveCountBySkinningMode(scene.LinkModel, scene.LinkMeshIndices, 0) },
              { "selected_mode1_primitive_count",
                NativeDemoSelectedPrimitiveCountBySkinningMode(scene.LinkModel, scene.LinkMeshIndices, 1) },
              { "selected_skinned_primitive_count",
                NativeDemoSelectedPrimitiveCountBySkinningMode(scene.LinkModel, scene.LinkMeshIndices, 2) },
              { "shape_count", scene.LinkModel.Shapes.size() },
              { "primitive_count", scene.LinkModel.PrimitiveCount() },
              { "triangle_count", scene.LinkModel.TriangleCount() },
              { "vertex_count", scene.LinkModel.VertexCount() },
              { "texture_count", scene.LinkModel.Textures.size() },
              { "decoded_texture_count", NativeDemoDecodedTextureCount(scene.LinkModel) },
              { "bone_count", scene.LinkModel.BoneCount() },
              { "csab_frame_count", scene.LinkCsab.FrameCount },
              { "csab_animated_bone_count", scene.LinkCsab.AnimatedBoneCount },
              { "csab_skeleton_bone_count", scene.LinkCsab.SkeletonBoneCount },
              { "standing_pose_world_transform_count", scene.LinkStandingPose.WorldTransforms.size() },
              { "bind_world_transform_count", scene.LinkBindWorldTransforms.size() },
              { "skin_transform_count", scene.LinkSkinTransforms.size() },
              { "native_to_scene_scale", scene.LinkScale },
              { "target_height_units", scene.LinkTargetHeight },
              { "radius_units", scene.LinkRadius },
              { "bounds", BoundsJson(scene.LinkBounds) },
          } },
        { "engine_render_scene",
          Oot3dNativeDemoRenderSceneSummaryToJson(renderScene) },
        { "native_pica_trace_asset_bindings",
          NativePicaTraceAssetBindingsJson(scene, renderScene) },
        { "engine_renderer_submission", Oot3dNativeRendererSubmitResultToJson(rendererSubmission) },
        { "native_actor_visuals", ActorVisualsJson(scene) },
        { "native_archive_texture_catalog", NativeArchiveTextureCatalogJson(scene) },
        { "spawn", { { "x", scene.Spawn.X }, { "y", scene.Spawn.Y }, { "z", scene.Spawn.Z } } },
        { "player_start", PlayerStartJson(scene.PlayerStart) },
        { "active_scene_setup_index", scene.ActiveSceneSetupIndex },
        { "active_scene_setup_source", scene.ActiveSceneSetupSource },
        { "asset_graph", AssetGraphJson(scene.AssetGraph) },
    };
}

} // namespace ThreeDsRecomp::Oot3d
