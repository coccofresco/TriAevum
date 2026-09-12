#include "oot3d_native_ui_texture_provider.h"
#include "oot3d_native_a32_memory.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_ui_texture_provider_tests: " << message
              << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

std::uint64_t Fnv1a64(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 0xCBF29CE484222325ULL;
    for (const auto byte : bytes) {
        value ^= byte;
        value *= 0x100000001B3ULL;
    }
    return value;
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t shift = 0U; shift < 64U; shift += 8U) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

} // namespace

int main() {
    using namespace Oot3dNativeGame;

    std::string error;
    const auto pauseTop = ResolveOot3dNativeUiTextureDescriptor(
        "oot3d/native/pause_shared/pause_top_page", &error);
    Require(pauseTop.has_value() && pauseTop->width == 512U &&
                pauseTop->height == 256U &&
                pauseTop->encodings.size() == 1U &&
                pauseTop->encodings.front().native_pica_format == 4U &&
                pauseTop->encodings.front().encoded_byte_count == 262144U,
            "pause-top atlas did not resolve from native CTXB evidence");
    const auto glyphs = ResolveOot3dNativeUiTextureDescriptor(
        "oot3d/native/pause_shared/number_glyphs", &error);
    Require(glyphs.has_value() && glyphs->width == 256U &&
                glyphs->height == 128U &&
                glyphs->encodings.size() == 1U &&
                glyphs->encodings.front().native_pica_format == 4U &&
                glyphs->encodings.front().encoded_byte_count == 65536U,
            "number atlas did not resolve from native CTXB evidence");
    const auto items = ResolveOot3dNativeUiTextureDescriptor(
        "oot3d/native/pause_shared/item_icons", &error);
    Require(items.has_value() && items->width == 512U &&
                items->height == 512U &&
                items->encodings.size() == 1U &&
                items->encodings.front().native_pica_format == 13U &&
                items->encodings.front().encoded_byte_count == 262144U,
            "item atlas did not preserve native ETC1A4 format");
    const auto itemPageAux = ResolveOot3dNativeUiTextureDescriptor(
        "oot3d/native/pause_shared/item_page_aux", &error);
    Require(itemPageAux.has_value() && itemPageAux->width == 64U &&
                itemPageAux->height == 128U &&
                itemPageAux->encodings.size() == 2U &&
                itemPageAux->FindEncoding(2U) != nullptr &&
                itemPageAux->FindEncoding(2U)->encoded_byte_count == 16384U &&
                itemPageAux->FindEncoding(3U) != nullptr &&
                itemPageAux->FindEncoding(3U)->encoded_byte_count == 16384U,
            "localized item-page CTXB variants were collapsed");
    const auto activeFileSelect = ResolveOot3dNativeUiTextureDescriptor(
        "oot3d/native/localized/file_select_active", &error);
    Require(activeFileSelect.has_value() &&
                activeFileSelect->width == 512U &&
                activeFileSelect->height == 256U &&
                activeFileSelect->encodings.size() == 1U &&
                activeFileSelect->encodings.front().native_pica_format == 4U &&
                activeFileSelect->encodings.front().encoded_byte_count ==
                    262144U,
            "active native file-select CTXB contract did not resolve");
    Require(!ResolveOot3dNativeUiTextureDescriptor(
                 "oot3d/native/pause_shared/not_an_asset", &error)
                 .has_value(),
            "unknown native semantic unexpectedly resolved");
    for (const auto& descriptor :
         oot3d::ui::Oot3dLocalizedMenuTextureDescriptors()) {
        if (!descriptor.IsPauseShared()) {
            continue;
        }
        const std::string semantic =
            std::string("oot3d/native/pause_shared/") +
            descriptor.semantic_name;
        const auto resolved =
            ResolveOot3dNativeUiTextureDescriptor(semantic, &error);
        Require(resolved.has_value() && resolved->width == descriptor.width &&
                    resolved->height == descriptor.height &&
                    !resolved->encodings.empty(),
                "a native pause-shared CTXB contract is not renderable");
        for (const auto& encoding : resolved->encodings) {
            Require(encoding.encoded_byte_count != 0U,
                    "a native CTXB encoding has no payload");
        }
    }

    NativeA32Memory memory;
    constexpr std::uint32_t kCtxbSource = 0x08000000U;
    const std::size_t pauseTopByteCount =
        pauseTop->encodings.front().encoded_byte_count;
    Require(memory.MapRegion(
                {"ui-ctxb-source", kCtxbSource, 0x100U, true, false, {}},
                &error),
            "could not map native CTXB source fixture");
    Require(memory.MapRegion({"ui-texture", 0x14000000U,
                              pauseTopByteCount, true, false, {}},
                             &error),
            "could not map native texture fixture");
    const Oot3dPicaPhysicalMemoryView picaMemory(
        memory, {{0x20000000U, 0x14000000U, pauseTopByteCount}});
    Oot3dNativeA32UiTextureProvider provider(picaMemory);

    const auto seedCtxbSource =
        [&](std::uint16_t width, std::uint16_t height,
            std::uint16_t rawFormat, std::uint16_t rawType,
            std::uint32_t surface) {
            return memory.Write16(kCtxbSource + 0x2CU, width) &&
                   memory.Write16(kCtxbSource + 0x2EU, height) &&
                   memory.Write16(kCtxbSource + 0x30U, rawFormat) &&
                   memory.Write16(kCtxbSource + 0x32U, rawType) &&
                   memory.Write32(kCtxbSource + 0x4CU, surface);
        };
    Require(seedCtxbSource(512U, 256U, 0x6752U, 0x8033U,
                           0x20000000U),
            "could not seed pause-top CTXB metadata");

    oot3d::ui::UiTextureIdentity identity;
    identity.semantic_name =
        "oot3d/native/pause_shared/pause_top_page";
    identity.guest_resource_address = kCtxbSource;
    identity.guest_surface_address = 0x20000000U;
    oot3d::ui::UiTexturePixels pixels;
    Require(provider.Resolve(identity, pixels, &error) &&
                pixels.width == 512U && pixels.height == 256U &&
                pixels.rgba8.size() == 512U * 256U * 4U,
            "physical PICA UI surface did not decode");

    Require(seedCtxbSource(512U, 256U, 0x6752U, 0x8033U,
                           0x14000000U),
            "could not relocate pause-top CTXB surface");
    identity.guest_surface_address = 0x14000000U;
    Require(provider.Resolve(identity, pixels, &error),
            "guest-address UI surface did not translate to PICA memory");

    Require(seedCtxbSource(64U, 128U, 0x6754U, 0x8363U,
                           0x20000000U),
            "could not seed localized RGB565 CTXB metadata");
    identity.semantic_name =
        "oot3d/native/pause_shared/item_page_aux";
    identity.guest_surface_address = 0x20000000U;
    Require(provider.Resolve(identity, pixels, &error) &&
                pixels.width == 64U && pixels.height == 128U,
            "runtime RGB565 localization variant did not decode");

    Require(seedCtxbSource(64U, 128U, 0x6752U, 0x8033U,
                           0x20000000U),
            "could not seed mismatched CTXB metadata");
    Require(!provider.Resolve(identity, pixels, &error),
            "a runtime encoding outside the semantic contract was accepted");

    Require(provider.Stats().resolve_calls == 4U &&
                provider.Stats().physical_reads == 2U &&
                provider.Stats().translated_guest_reads == 1U &&
                provider.Stats().bytes_read ==
                    pauseTopByteCount * 2U + 16384U &&
                provider.Stats().runtime_metadata_reads == 4U &&
                provider.Stats().runtime_metadata_failures == 0U &&
                provider.Stats().contract_mismatches == 1U &&
                provider.Stats().decode_failures == 0U,
            "native UI provider diagnostics do not match the read path");

    oot3d::ui::UiTextureIdentity generated{kCtxbSource, 0x20000000U, "oot3d/native/generated_text"};
    Require(seedCtxbSource(16, 16, 0x6756, 0x6761, 0x20000000U) &&
                memory.Write32(kCtxbSource + 0x24, 128) && provider.Resolve(generated, pixels, &error) &&
                pixels.width == 16 && pixels.height == 16 && pixels.rgba8.size() == 1024 &&
                pixels.rgba8[0] == 255 && pixels.rgba8[1] == 255 &&
                pixels.rgba8[2] == 255 && pixels.rgba8[3] == 0,
            "native generated alpha text did not decode from its own descriptor");
    Require(memory.Write32(kCtxbSource + 0x24, 127) && !provider.Resolve(generated, pixels, &error),
            "generated text accepted an inconsistent encoded size");
    Require(seedCtxbSource(15, 16, 0x6756, 0x6761, 0x20000000U) &&
                !provider.Resolve(generated, pixels, &error), "generated text accepted invalid dimensions");
    Require(seedCtxbSource(16, 16, 0x6756, 0x6761, 0x20000080U) &&
                memory.Write32(kCtxbSource + 0x24, 128) && !provider.Resolve(generated, pixels, &error),
            "generated text accepted stale native surface identity");

    oot3d::ui::UiTextureIdentity cameraGlyphIdentity;
    cameraGlyphIdentity.semantic_name =
        "oot3d/topscreen/camera_option_glyphs";
    Require(provider.Resolve(cameraGlyphIdentity, pixels, &error) &&
                pixels.width == 128U && pixels.height == 256U &&
                pixels.rgba8.size() == 128U * 256U * 4U,
            "TopScreen free-camera payload font did not resolve");
    const auto glyphPixel = [&pixels](std::size_t x, std::size_t y,
                                     std::size_t component) {
        return pixels.rgba8[(y * pixels.width + x) * 4U + component];
    };
    Require(glyphPixel(98U, 112U, 0U) == 0xFFU &&
                glyphPixel(98U, 112U, 3U) == 0xFFU &&
                glyphPixel(96U, 112U, 0U) == 0x00U &&
                glyphPixel(96U, 112U, 3U) == 0xFFU &&
                glyphPixel(102U, 112U, 3U) == 0x00U,
            "TopScreen free-camera glyph bits diverge from the payload font");

    std::vector<std::uint8_t> originalPayload(pauseTopByteCount, 0U);
    std::vector<std::uint8_t> replacementPayload(pauseTopByteCount, 0xFFU);
    std::vector<std::uint8_t> packBytes{'O', '3', 'T', 'U'};
    AppendU32(packBytes, 1U);
    AppendU32(packBytes, 1U);
    AppendU64(packBytes, Fnv1a64(originalPayload));
    AppendU64(packBytes, Fnv1a64(replacementPayload));
    AppendU32(packBytes, static_cast<std::uint32_t>(replacementPayload.size()));
    packBytes.insert(packBytes.end(), replacementPayload.begin(),
                     replacementPayload.end());
    TopScreenTextureOverridePack textureOverrides;
    Require(textureOverrides.LoadBytes(packBytes, &error),
            "could not load synthetic TopScreen CTXB override pack");
    Require(memory.WriteBytes(0x14000000U, originalPayload) &&
                seedCtxbSource(512U, 256U, 0x6752U, 0x8033U,
                               0x20000000U),
            "could not restore pause-top override fixture");
    identity.semantic_name = "oot3d/native/pause_shared/pause_top_page";
    identity.guest_surface_address = 0x20000000U;
    Oot3dNativeA32UiTextureProvider overrideProvider(picaMemory,
                                                      &textureOverrides);
    Require(overrideProvider.Resolve(identity, pixels, &error) &&
                !pixels.rgba8.empty() && pixels.rgba8.front() == 0xFFU,
            "TopScreen replacement CTXB was not decoded by the UI provider");
    Require(memory.WriteBytes(0x14000000U, replacementPayload) &&
                overrideProvider.Resolve(identity, pixels, &error),
            "already replaced TopScreen CTXB was not accepted");
    Require(overrideProvider.Stats().override_checks == 2U &&
                overrideProvider.Stats().overrides_applied == 1U &&
                overrideProvider.Stats().overrides_already_applied == 1U &&
                overrideProvider.Stats().override_no_matches == 0U,
            "TopScreen provider override diagnostics are inconsistent");

    constexpr std::string_view profileTextureSemantic =
        "oot3d/topscreen/2.1.1/test_atlas";
    const std::vector<std::uint8_t> profileTexturePayload(256U, 0xFFU);
    std::vector<std::uint8_t> profilePackBytes{'O', '3', 'T', 'U'};
    AppendU32(profilePackBytes, 2U);
    AppendU32(profilePackBytes, 0U);
    AppendU32(profilePackBytes, 1U);
    AppendU32(profilePackBytes,
              static_cast<std::uint32_t>(profileTextureSemantic.size()));
    AppendU16(profilePackBytes, 8U);
    AppendU16(profilePackBytes, 8U);
    profilePackBytes.push_back(0U);
    profilePackBytes.insert(profilePackBytes.end(), 3U, 0U);
    AppendU32(profilePackBytes,
              static_cast<std::uint32_t>(profileTexturePayload.size()));
    AppendU64(profilePackBytes, Fnv1a64(profileTexturePayload));
    profilePackBytes.insert(profilePackBytes.end(),
                            profileTextureSemantic.begin(),
                            profileTextureSemantic.end());
    profilePackBytes.insert(profilePackBytes.end(),
                            profileTexturePayload.begin(),
                            profileTexturePayload.end());
    TopScreenTextureOverridePack profileTexturePack;
    Require(profileTexturePack.LoadBytes(profilePackBytes, &error),
            "could not load synthetic TopScreen profile texture pack");
    Oot3dNativeA32UiTextureProvider profileTextureProvider(
        picaMemory, &profileTexturePack);
    oot3d::ui::UiTextureIdentity profileTextureIdentity;
    profileTextureIdentity.semantic_name = profileTextureSemantic;
    Require(profileTextureProvider.Resolve(profileTextureIdentity, pixels,
                                           &error) &&
                pixels.width == 8U && pixels.height == 8U &&
                pixels.rgba8.size() == 8U * 8U * 4U &&
                pixels.rgba8.front() == 0xFFU &&
                profileTextureProvider.Stats().profile_asset_resolves == 1U &&
                profileTextureProvider.Stats()
                        .profile_asset_decode_failures == 0U,
            "named TopScreen profile CTXB did not resolve through the common "
            "PICA decoder");

    std::cout << "oot3d_native_ui_texture_provider_tests: ok\n";
    return 0;
}
