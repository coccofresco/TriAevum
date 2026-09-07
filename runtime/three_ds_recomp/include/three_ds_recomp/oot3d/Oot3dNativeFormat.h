#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace ThreeDsRecomp::Oot3d {

enum class NativeFormatKind {
    Unknown,
    Zsi,
    Cmb,
    Zar,
    Csab,
    Cmab,
    Ctxb,
    Shbin,
    CmbPlusCsab,
    DerivedManifest,
};

struct NativeFormatProbe {
    NativeFormatKind Kind = NativeFormatKind::Unknown;
    bool IsRecognized = false;
    bool HeaderMatches = false;
    uintmax_t FileSize = 0;
    uint32_t DeclaredSize = 0;
    uint32_t Version = 0;
    std::string Magic;
    std::string Issue;
};

std::string_view NativeFormatKindName(NativeFormatKind kind);
NativeFormatKind NativeFormatKindFromSourceFormat(std::string_view sourceFormat);
bool IsNativeBinaryFormatKind(NativeFormatKind kind);
bool IsExpectedNativeFormatMatch(NativeFormatKind expected, NativeFormatKind actual);
NativeFormatProbe ProbeOot3dNativeFormatBytes(std::span<const uint8_t> bytes, std::string_view pathHint = {});
NativeFormatProbe ProbeOot3dNativeFormatFile(const std::filesystem::path& path);

} // namespace ThreeDsRecomp::Oot3d
