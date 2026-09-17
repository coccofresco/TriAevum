#include "oot3d_native_hd_font_runtime.h"

#include <algorithm>
#include <cstring>
#include <nlohmann/json.hpp>
#include <zip.h>

namespace Oot3dNativeGame {
namespace {
constexpr std::size_t kMaxFontBytes = 32U * 1024U * 1024U;
std::size_t Pixel(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
    const auto morton = [](std::uint32_t v) { return (v & 1U) | ((v & 2U) << 1U) | ((v & 4U) << 2U); };
    return ((y / 8) * (width / 8) + x / 8) * 64 + morton(x & 7U) + 2 * morton(y & 7U);
}
std::uint8_t ReadA4(std::span<const std::uint8_t> bytes, std::size_t pixel) {
    return (bytes[pixel / 2] >> ((pixel & 1U) * 4)) & 15U;
}
void WriteA4(std::vector<std::uint8_t>& bytes, std::size_t pixel, std::uint8_t value) {
    const auto shift = (pixel & 1U) * 4;
    bytes[pixel / 2] = (bytes[pixel / 2] & ~(15U << shift)) | (value << shift);
}
std::vector<std::uint8_t> ReadZip(zip_t* zip, const std::string& name) {
    zip_stat_t info{};
    if (zip_stat(zip, name.c_str(), 0, &info) != 0 || info.size > kMaxFontBytes)
        throw std::runtime_error("missing or oversized HD font entry: " + name);
    auto* file = zip_fopen(zip, name.c_str(), 0);
    if (!file) throw std::runtime_error("cannot read HD font entry");
    std::vector<std::uint8_t> data(static_cast<std::size_t>(info.size));
    const auto count = zip_fread(file, data.data(), data.size());
    zip_fclose(file);
    if (count < 0 || static_cast<std::size_t>(count) != data.size()) throw std::runtime_error("truncated HD font entry");
    return data;
}
}

bool NativeHdFontRuntime::Load(const std::filesystem::path& path, std::string* error) {
    const auto utf8 = path.u8string();
    const std::string name(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    auto* zip = zip_open(name.c_str(), ZIP_RDONLY, nullptr);
    if (!zip) { if (error) *error = "cannot open HD font coverage pack"; return false; }
    try {
        const auto bytes = ReadZip(zip, "manifest.json");
        const auto manifest = nlohmann::json::parse(bytes);
        if (manifest.at("format") != "oot3d_qbf_coverage_v1" || manifest.at("fonts").size() > 8)
            throw std::runtime_error("invalid HD font manifest");
        std::vector<Font> fonts;
        for (const auto& entry : manifest.at("fonts")) {
            Font font;
            font.Native = ReadZip(zip, entry.at("native_file").get<std::string>());
            const auto hd = ReadZip(zip, entry.at("coverage_file").get<std::string>());
            std::string reason;
            if (!font.Coverage.Load(font.Native, hd, &reason)) throw std::runtime_error(reason);
            fonts.push_back(std::move(font));
        }
        if (fonts.empty()) throw std::runtime_error("empty HD font pack");
        mFonts = std::move(fonts);
        Reset();
        zip_close(zip);
        return true;
    } catch (const std::exception& exception) {
        zip_close(zip);
        if (error) *error = exception.what();
        return false;
    }
}

void NativeHdFontRuntime::Reset() { mSurfaces.clear(); }

void NativeHdFontRuntime::ObserveBlit(const oot3d::recomp::a32::GuestState& state,
                                    NativeA32Memory& memory) {
    // AAPCS: owner, destination, atlas width/height in r0-r3; the source glyph,
    // cell width/height and placement follow on the stack. Guest memory is read-only.
    std::uint32_t args[5]{};
    if (!memory.ReadBytes(state.r[13], std::span(reinterpret_cast<std::uint8_t*>(args), sizeof(args)))) return;
    const auto width = state.r[2], height = state.r[3];
    if (!width || !height || width > 1024 || height > 1024 || width % 8 || height % 8 ||
        args[3] > width || args[4] > height || args[1] > width - args[3] || args[2] > height - args[4]) return;
    for (std::size_t index = 0; index < mFonts.size(); ++index) {
        const auto& font = mFonts[index];
        if (args[1] != font.Coverage.LogicalWidth() || args[2] != font.Coverage.LogicalHeight()) continue;
        // Both regular and ruby font slots exist; only an exact validated QBF
        // image is accepted, so reused pointers cannot inherit a font identity.
        for (const auto slot : {0x10U, 0x14U}) {
            std::uint32_t object = 0, qbf = 0;
            if (!memory.Read32(state.r[0] + slot, &object) || !object ||
                !memory.Read32(object + 4, &qbf) || !qbf || args[0] < qbf) continue;
            const auto* source = memory.GetReadPointer(qbf, font.Native.size());
            if (!source || std::memcmp(source, font.Native.data(), font.Native.size()) != 0) continue;
            const auto offset = args[0] - qbf;
            if (font.Coverage.Glyph(offset).empty()) continue;
            if (mSurfaces.size() >= 128 && !mSurfaces.contains(state.r[1])) mSurfaces.clear();
            auto& surface = mSurfaces[state.r[1]];
            if (surface.Width != width || surface.Height != height) surface = {};
            surface.Width = width; surface.Height = height;
            auto existing = std::find_if(surface.Placements.begin(), surface.Placements.end(),
                [&](const auto& p) { return p.X == args[3] && p.Y == args[4]; });
            const Placement placement{index, offset, args[3], args[4]};
            if (existing == surface.Placements.end()) {
                if (surface.Placements.size() >= 4096) surface.Placements.clear();
                surface.Placements.push_back(placement);
                surface.Dirty = true;
            } else if (existing->FontIndex != index || existing->Offset != offset) {
                *existing = placement; surface.Dirty = true;
            }
            ++Blits;
            return;
        }
    }
}

void NativeHdFontRuntime::Resolve(const Oot3dPicaPhysicalMemoryView& memory,
                                  const Oot3dPicaTextureState& texture,
                                  Oot3dPicaResourceSnapshot& resource) {
    if (mOutputHeight < 480 || texture.Format != 11 || texture.MaxMipLevel != 0) return;
    const auto native = resource.ResolvedBytes();
    const auto guest = memory.Translate(texture.PhysicalAddress, native.size());
    if (!guest) return;
    auto found = mSurfaces.find(*guest);
    if (found == mSurfaces.end()) {
        // Native text uploads may copy the heap atlas into linear GPU memory.
        // Require full byte identity with an observed owner, not a font hash
        // guessed from a finished image. Glyph provenance still comes from blits.
        for (auto it = mSurfaces.begin(); it != mSurfaces.end(); ++it) {
            if (it->second.Width != texture.Width || it->second.Height != texture.Height) continue;
            const auto source = memory.ViewGuest(it->first, native.size());
            if (source.size() == native.size() && std::equal(native.begin(), native.end(), source.begin())) {
                found = it;
                break;
            }
        }
    }
    if (found == mSurfaces.end()) return;
    auto& surface = found->second;
    if (surface.Width != texture.Width || surface.Height != texture.Height ||
        native.size() != static_cast<std::size_t>(surface.Width) * surface.Height / 2) return;
    if (surface.Dirty || surface.LastNative.size() != native.size() ||
        !std::equal(native.begin(), native.end(), surface.LastNative.begin())) {
        surface.Encoded.reset();
        std::vector<const Placement*> valid;
        std::uint32_t density = 0;
        for (const auto& p : surface.Placements) {
            const auto& font = mFonts[p.FontIndex];
            const auto w = font.Coverage.LogicalWidth(), h = font.Coverage.LogicalHeight();
            bool matches = true;
            for (std::uint32_t y = 0; matches && y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x) {
                    const auto value = (font.Native[p.Offset + (y * w + x) / 2] >> ((1 - (x & 1U)) * 4)) & 15U;
                    if (ReadA4(native, Pixel(p.X + x, p.Y + y, surface.Width)) != value) { matches = false; break; }
                }
            if (matches && (!density || density == font.Coverage.Density())) {
                valid.push_back(&p); density = font.Coverage.Density();
            }
        }
        if (!valid.empty() && static_cast<std::uint64_t>(surface.Width) * surface.Height * density * density / 2 <= kMaxFontBytes) {
            const auto hdWidth = surface.Width * density, hdHeight = surface.Height * density;
            auto encoded = std::make_shared<std::vector<std::uint8_t>>(hdWidth * hdHeight / 2, 0);
            for (std::uint32_t y = 0; y < hdHeight; ++y)
                for (std::uint32_t x = 0; x < hdWidth; ++x)
                    WriteA4(*encoded, Pixel(x, y, hdWidth), ReadA4(native, Pixel(x / density, y / density, surface.Width)));
            for (const auto* p : valid) {
                const auto& font = mFonts[p->FontIndex];
                const auto glyph = font.Coverage.Glyph(p->Offset);
                const auto w = font.Coverage.LogicalWidth() * density, h = font.Coverage.LogicalHeight() * density;
                for (std::uint32_t y = 0; y < h; ++y)
                    for (std::uint32_t x = 0; x < w; ++x)
                        WriteA4(*encoded, Pixel(p->X * density + x, p->Y * density + y, hdWidth), glyph[y * w + x] / 17);
            }
            surface.Encoded = std::move(encoded);
            surface.ContentHash = HashOot3dPicaSnapshot(*surface.Encoded);
            surface.Density = density;
            ++Rebuilds;
        }
        surface.LastNative.assign(native.begin(), native.end());
        surface.Dirty = false;
    }
    if (!surface.Encoded) return;
    resource.SharedBytes = surface.Encoded;
    resource.Bytes.clear();
    resource.ReplacementWidth = static_cast<std::uint16_t>(surface.Width * surface.Density);
    resource.ReplacementHeight = static_cast<std::uint16_t>(surface.Height * surface.Density);
    resource.ContentHash = resource.BaseLevelContentHash = surface.ContentHash;
    resource.ContentHashAvailable = resource.BaseLevelContentHashAvailable = true;
    ++Replacements;
}
} // namespace Oot3dNativeGame
