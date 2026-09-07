#include "three_ds_recomp/oot3d/Oot3dNativeFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <vector>

namespace ThreeDsRecomp::Oot3d {
namespace {

bool StartsWith(std::span<const uint8_t> bytes, std::string_view magic) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    for (size_t i = 0; i < magic.size(); ++i) {
        if (bytes[i] != static_cast<uint8_t>(magic[i])) {
            return false;
        }
    }
    return true;
}

uint32_t ReadLe32(std::span<const uint8_t> bytes, size_t offset) {
    if (offset + 4 > bytes.size()) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::string ToLower(std::string_view value) {
    std::string lower(value);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool Contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

std::string FourCc(std::span<const uint8_t> bytes) {
    std::ostringstream output;
    const size_t count = std::min<size_t>(bytes.size(), 4);
    bool printable = count == 4;
    for (size_t i = 0; i < count; ++i) {
        printable = printable && bytes[i] >= 32 && bytes[i] < 127;
    }
    if (printable) {
        output << "ascii:";
        for (size_t i = 0; i < count; ++i) {
            output << static_cast<char>(bytes[i]);
        }
        return output.str();
    }

    output << "hex:";
    for (size_t i = 0; i < count; ++i) {
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(bytes[i]);
    }
    return output.str();
}

NativeFormatProbe MakeRecognized(NativeFormatKind kind, std::span<const uint8_t> bytes) {
    NativeFormatProbe probe;
    probe.Kind = kind;
    probe.IsRecognized = true;
    probe.FileSize = bytes.size();
    probe.Magic = FourCc(bytes);
    return probe;
}

void ValidateDeclaredSize(NativeFormatProbe& probe, std::span<const uint8_t> bytes, size_t offset, bool exact) {
    probe.DeclaredSize = ReadLe32(bytes, offset);
    if (probe.DeclaredSize == 0) {
        probe.Issue = "declared_size_zero";
        return;
    }
    if (exact && probe.DeclaredSize != bytes.size()) {
        probe.Issue = "declared_size_mismatch";
        return;
    }
    if (!exact && probe.DeclaredSize > bytes.size()) {
        probe.Issue = "declared_size_exceeds_file";
        return;
    }
    probe.HeaderMatches = true;
}

} // namespace

std::string_view NativeFormatKindName(NativeFormatKind kind) {
    switch (kind) {
        case NativeFormatKind::Zsi:
            return "zsi";
        case NativeFormatKind::Cmb:
            return "cmb";
        case NativeFormatKind::Zar:
            return "zar";
        case NativeFormatKind::Csab:
            return "csab";
        case NativeFormatKind::Cmab:
            return "cmab";
        case NativeFormatKind::Ctxb:
            return "ctxb";
        case NativeFormatKind::Shbin:
            return "shbin";
        case NativeFormatKind::CmbPlusCsab:
            return "cmb_plus_csab";
        case NativeFormatKind::DerivedManifest:
            return "derived_manifest";
        case NativeFormatKind::Unknown:
        default:
            return "unknown";
    }
}

NativeFormatKind NativeFormatKindFromSourceFormat(std::string_view sourceFormat) {
    const auto lower = ToLower(sourceFormat);
    if (Contains(lower, "character_conversion_manifest") || Contains(lower, "validation_manifest") ||
        Contains(lower, "manifest")) {
        return NativeFormatKind::DerivedManifest;
    }
    if (Contains(lower, "zsi")) {
        return NativeFormatKind::Zsi;
    }
    if (Contains(lower, "cmb_plus_csab") || (Contains(lower, "cmb") && Contains(lower, "csab"))) {
        return NativeFormatKind::CmbPlusCsab;
    }
    if (Contains(lower, "csab")) {
        return NativeFormatKind::Csab;
    }
    if (Contains(lower, "cmab")) {
        return NativeFormatKind::Cmab;
    }
    if (Contains(lower, "ctxb")) {
        return NativeFormatKind::Ctxb;
    }
    if (Contains(lower, "shbin") || Contains(lower, "dvlb")) {
        return NativeFormatKind::Shbin;
    }
    if (Contains(lower, "cmb")) {
        return NativeFormatKind::Cmb;
    }
    if (Contains(lower, "zar")) {
        return NativeFormatKind::Zar;
    }
    return NativeFormatKind::Unknown;
}

bool IsNativeBinaryFormatKind(NativeFormatKind kind) {
    switch (kind) {
        case NativeFormatKind::Zsi:
        case NativeFormatKind::Cmb:
        case NativeFormatKind::Zar:
        case NativeFormatKind::Csab:
        case NativeFormatKind::Cmab:
        case NativeFormatKind::Ctxb:
        case NativeFormatKind::Shbin:
        case NativeFormatKind::CmbPlusCsab:
            return true;
        case NativeFormatKind::DerivedManifest:
        case NativeFormatKind::Unknown:
        default:
            return false;
    }
}

bool IsExpectedNativeFormatMatch(NativeFormatKind expected, NativeFormatKind actual) {
    if (expected == actual) {
        return true;
    }
    if (expected == NativeFormatKind::CmbPlusCsab) {
        return actual == NativeFormatKind::Cmb || actual == NativeFormatKind::Csab || actual == NativeFormatKind::Zar;
    }
    return false;
}

NativeFormatProbe ProbeOot3dNativeFormatBytes(std::span<const uint8_t> bytes, std::string_view) {
    NativeFormatProbe probe;
    probe.FileSize = bytes.size();
    probe.Magic = FourCc(bytes);
    if (bytes.size() < 4) {
        probe.Issue = "file_too_small_for_magic";
        return probe;
    }

    if (StartsWith(bytes, std::string_view("ZSI\x01", 4))) {
        probe = MakeRecognized(NativeFormatKind::Zsi, bytes);
        probe.HeaderMatches = true;
        return probe;
    }

    if (StartsWith(bytes, "cmb ")) {
        probe = MakeRecognized(NativeFormatKind::Cmb, bytes);
        if (bytes.size() < 12) {
            probe.Issue = "cmb_header_too_small";
            return probe;
        }
        probe.Version = ReadLe32(bytes, 8);
        ValidateDeclaredSize(probe, bytes, 4, false);
        if (probe.HeaderMatches && probe.Version != 6) {
            probe.HeaderMatches = false;
            probe.Issue = "cmb_version_not_oot3d_v6";
        }
        return probe;
    }

    if (StartsWith(bytes, std::string_view("ZAR\x01", 4))) {
        probe = MakeRecognized(NativeFormatKind::Zar, bytes);
        if (bytes.size() < 0x18) {
            probe.Issue = "zar_header_too_small";
            return probe;
        }
        ValidateDeclaredSize(probe, bytes, 4, false);
        return probe;
    }

    if (StartsWith(bytes, "csab")) {
        probe = MakeRecognized(NativeFormatKind::Csab, bytes);
        if (bytes.size() < 12) {
            probe.Issue = "csab_header_too_small";
            return probe;
        }
        probe.Version = ReadLe32(bytes, 8);
        ValidateDeclaredSize(probe, bytes, 4, false);
        return probe;
    }

    if (StartsWith(bytes, "cmab")) {
        probe = MakeRecognized(NativeFormatKind::Cmab, bytes);
        if (bytes.size() < 12) {
            probe.Issue = "cmab_header_too_small";
            return probe;
        }
        probe.Version = ReadLe32(bytes, 4);
        ValidateDeclaredSize(probe, bytes, 8, false);
        return probe;
    }

    if (StartsWith(bytes, "ctxb")) {
        probe = MakeRecognized(NativeFormatKind::Ctxb, bytes);
        if (bytes.size() < 0x48) {
            probe.Issue = "ctxb_header_too_small";
            return probe;
        }
        probe.Version = ReadLe32(bytes, 8);
        ValidateDeclaredSize(probe, bytes, 4, true);
        if (!probe.HeaderMatches) {
            return probe;
        }
        if (probe.Version != 1) {
            probe.HeaderMatches = false;
            probe.Issue = "ctxb_version_not_oot3d_v1";
            return probe;
        }
        if (ReadLe32(bytes, 0x10) != 0x18 || ReadLe32(bytes, 0x14) != 0x48 || !StartsWith(bytes.subspan(0x18), "tex ")) {
            probe.HeaderMatches = false;
            probe.Issue = "ctxb_texture_chunk_header_invalid";
        }
        return probe;
    }

    if (StartsWith(bytes, "DVLB")) {
        probe = MakeRecognized(NativeFormatKind::Shbin, bytes);
        if (bytes.size() < 0x0C) {
            probe.Issue = "shbin_header_too_small";
            return probe;
        }
        const auto programCount = ReadLe32(bytes, 0x04);
        if (programCount == 0) {
            probe.Issue = "shbin_program_count_zero";
            return probe;
        }
        const auto dvlpOffset64 = 0x08ull + static_cast<uint64_t>(programCount) * sizeof(uint32_t);
        if (dvlpOffset64 > bytes.size() || dvlpOffset64 + 0x1Cull > bytes.size()) {
            probe.Issue = "shbin_dvlp_header_too_small";
            return probe;
        }
        const auto dvlpOffset = static_cast<size_t>(dvlpOffset64);
        if (!StartsWith(bytes.subspan(dvlpOffset), "DVLP")) {
            probe.Issue = "shbin_dvlp_header_invalid";
            return probe;
        }
        probe.Version = ReadLe32(bytes, dvlpOffset + 0x04);
        probe.HeaderMatches = true;
        return probe;
    }

    probe.Issue = "unknown_oot3d_native_magic";
    return probe;
}

NativeFormatProbe ProbeOot3dNativeFormatFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        NativeFormatProbe probe;
        probe.Issue = "file_open_failed";
        return probe;
    }

    std::vector<uint8_t> bytes;
    for (std::istreambuf_iterator<char> it(file), end; it != end; ++it) {
        bytes.push_back(static_cast<uint8_t>(*it));
    }
    return ProbeOot3dNativeFormatBytes(bytes, path.string());
}

} // namespace ThreeDsRecomp::Oot3d
