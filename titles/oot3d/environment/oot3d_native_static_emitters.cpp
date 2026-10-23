#include "oot3d_native_static_emitters.h"

#include "recomp/a32_runtime.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Oot3dNativeGame {
namespace {

bool ParseCategory(const std::string& name, NativeStaticEmitterCategory& category) {
    if (name == "fire-lava") category = NativeStaticEmitterCategory::FireLava;
    else if (name == "window-lamp") category = NativeStaticEmitterCategory::WindowLamp;
    else if (name == "light-shaft") category = NativeStaticEmitterCategory::LightShaft;
    else if (name == "magic-glow") category = NativeStaticEmitterCategory::MagicGlow;
    else if (name == "lamp") category = NativeStaticEmitterCategory::Lamp;
    else if (name == "doorway") category = NativeStaticEmitterCategory::Doorway;
    else return false;
    return true;
}

bool ReadTriple(const nlohmann::json& value, std::array<float, 3>& out) {
    if (!value.is_array() || value.size() != 3U) return false;
    for (size_t i = 0; i < 3U; ++i) {
        if (!value[i].is_number()) return false;
        out[i] = value[i].get<float>();
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}

std::string ReadString(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

bool ReadFinite(const nlohmann::json& object, const char* key, float& out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number()) return false;
    out = found->get<float>();
    return std::isfinite(out);
}

bool ReadFingerprint(const nlohmann::json& value, uint64_t& out) {
    if (!value.is_string()) return false;
    const auto text = value.get<std::string>();
    if (text.size() != 16U) return false;
    out = 0;
    for (const char c : text) {
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0) return false;
        out = (out << 4U) | static_cast<uint64_t>(digit);
    }
    return true;
}

} // namespace

uint64_t NativeRoomFingerprint(std::span<const uint8_t> commands) noexcept {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t byte : commands) hash = (hash ^ byte) * 0x100000001B3ULL;
    return hash;
}

std::shared_ptr<const NativeStaticEmitterCatalog> NativeStaticEmitterCatalog::Parse(std::string_view text,
                                                                                    std::string* error) {
    const auto fail = [&](const std::string& message) -> std::shared_ptr<const NativeStaticEmitterCatalog> {
        if (error) *error = message;
        return nullptr;
    };
    const auto document = nlohmann::json::parse(text, nullptr, false);
    if (document.is_discarded() || !document.is_object() || ReadString(document, "schema") != "triaevum.scene-lights" ||
        !document.contains("version") || document["version"] != 1 || !document.contains("rooms") ||
        !document["rooms"].is_array())
        return fail("not a triaevum.scene-lights version 1 catalog");
    auto catalog = std::make_shared<NativeStaticEmitterCatalog>();
    // Every read below checks its type first; a conversion that still throws
    // refuses the catalog rather than the game.
    try {
        for (const auto& room : document["rooms"]) {
            uint64_t fingerprint = 0;
            if (!room.is_object() || !room.contains("scene") || !room["scene"].is_number_unsigned() ||
                room["scene"].get<uint64_t>() > 0xFFFFU || !room.contains("room") ||
                !room["room"].is_number_unsigned() || room["room"].get<uint64_t>() > 127U ||
                !ReadFingerprint(room.value("fingerprint", nlohmann::json()), fingerprint) ||
                !room.contains("emitters") || !room["emitters"].is_array())
                return fail("a room is malformed");
            const auto scene = room["scene"].get<uint16_t>();
            const auto number = room["room"].get<int>();
            const std::string where = "scene " + std::to_string(scene) + " room " + std::to_string(number);
            auto [entry, added] = catalog->mRooms.try_emplace({scene, number, fingerprint});
            if (!added) return fail(where + " is listed twice");
            for (const auto& e : room["emitters"]) {
                NativeStaticEmitter emitter;
                if (!e.is_object() || !ReadTriple(e.value("position", nlohmann::json()), emitter.Position) ||
                    !ReadTriple(e.value("colour", nlohmann::json()), emitter.Colour) ||
                    !ParseCategory(ReadString(e, "category"), emitter.Category) ||
                    !ReadFinite(e, "radius", emitter.Radius) || !ReadFinite(e, "gain", emitter.Gain))
                    return fail(where + " has a malformed emitter");
                if (!(emitter.Radius > 0.0F) || emitter.Radius > 100000.0F || !(emitter.Gain >= 0.0F) ||
                    emitter.Gain > 4.0F)
                    return fail(where + " has an emitter out of range");
                for (auto& channel : emitter.Colour) channel = std::clamp(channel, 0.0F, 1.0F);
                if (const auto surfaces = e.find("surfaces"); surfaces != e.end()) {
                    if (!surfaces->is_array() || surfaces->size() > emitter.Surfaces.size())
                        return fail(where + " has malformed emitter surfaces");
                    for (size_t i = 0; i < surfaces->size(); ++i)
                        if (!ReadFingerprint((*surfaces)[i], emitter.Surfaces[i]))
                            return fail(where + " has malformed emitter surfaces");
                }
                entry->second.push_back(emitter);
                ++catalog->mCount;
            }
        }
    } catch (const nlohmann::json::exception& exception) {
        return fail(std::string("malformed catalog: ") + exception.what());
    }
    return catalog;
}

std::shared_ptr<const NativeStaticEmitterCatalog> NativeStaticEmitterCatalog::LoadFile(
    const std::filesystem::path& path, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "cannot open " + path.string();
        return nullptr;
    }
    std::stringstream text;
    text << file.rdbuf();
    auto catalog = Parse(text.str(), error);
    if (!catalog && error) *error = path.string() + ": " + *error;
    return catalog;
}

std::shared_ptr<const NativeStaticEmitterCatalog> NativeStaticEmitterCatalog::FromEnvironment() {
    const char* path = std::getenv("TRIAEVUM_SCENE_LIGHT_CATALOG");
    if (path == nullptr || *path == '\0') return nullptr;
    std::string error;
    auto catalog = LoadFile(path, &error);
    if (catalog)
        std::fprintf(stderr, "TRIAEVUM_SCENE_LIGHT_CATALOG loaded=%zu path=%s\n", catalog->Count(), path);
    else
        std::fprintf(stderr, "TRIAEVUM_SCENE_LIGHT_CATALOG rejected: %s\n", error.c_str());
    return catalog;
}

std::span<const NativeStaticEmitter> NativeStaticEmitterCatalog::Room(uint16_t scene, int room,
                                                                      uint64_t fingerprint) const {
    const auto found = mRooms.find({scene, room, fingerprint});
    return found == mRooms.end() ? std::span<const NativeStaticEmitter>{} : std::span(found->second);
}

std::vector<NativeLoadedRoom> NativeLoadedRooms(uint32_t playState, oot3d::recomp::a32::MemoryBus& memory) {
    std::vector<NativeLoadedRoom> rooms;
    if (playState == 0U) return rooms;
    for (const auto& [number, commands] : {std::pair{0x4C30U, 0x4FFCU}, std::pair{0x500CU, 0x53D8U}}) {
        uint8_t raw = 0;
        uint32_t installed = 0;
        if (!memory.Read8(playState + number, &raw) || !memory.Read32(playState + commands, &installed)) continue;
        const int room = std::bit_cast<int8_t>(raw);
        if (room < 0 || installed == 0U ||
            std::any_of(rooms.begin(), rooms.end(), [&](const auto& loaded) { return loaded.Number == room; }))
            continue;
        std::array<uint8_t, kNativeRoomFingerprintBytes> bytes{};
        bool readable = true;
        for (uint32_t i = 0; i < bytes.size() && readable; ++i) readable = memory.Read8(installed + i, &bytes[i]);
        if (readable) rooms.push_back({room, NativeRoomFingerprint(bytes), installed});
    }
    return rooms;
}

void CaptureStaticEmitters(const NativeStaticEmitterCatalog& catalog, uint16_t scene,
                           std::span<const NativeLoadedRoom> rooms,
                           std::vector<Fast::Renderer3ds::PicaScenePointLight>& lights,
                           Fast::Renderer3ds::PicaSurfaceEmissionRegistry* emission) {
    std::vector<uint64_t> followed;
    for (const auto& room : rooms) {
        const auto emitters = catalog.Room(scene, room.Number, room.Fingerprint);
        for (size_t i = 0; i < emitters.size(); ++i) {
            const auto& e = emitters[i];
            // The strongest of its surfaces, as last drawn; full without any.
            float strength = 1.0F;
            if (emission != nullptr && e.Surfaces[0] != 0U) {
                strength = 0.0F;
                for (const uint64_t surface : e.Surfaces) {
                    if (surface == 0U) continue;
                    followed.push_back(surface);
                    strength = std::max(strength, std::clamp(emission->Strength(surface), 0.0F, 1.0F));
                }
            }
            if (strength <= 0.0F) continue;
            Fast::Renderer3ds::PicaScenePointLight light;
            light.SourceIdentity = (uint64_t{0xA5U} << 56U) | (uint64_t{scene} << 40U) |
                                   (uint64_t(static_cast<uint32_t>(room.Number) & 0xFFU) << 32U) | uint64_t(i);
            light.Group = 3U;
            light.PositionWorld = e.Position;
            light.Radius = e.Radius;
            // A gain above 1 (daylight through a doorway) is a brighter source,
            // not a whiter one: bounded, not clipped to 1.
            for (size_t c = 0; c < 3U; ++c) light.Color[c] = std::clamp(e.Colour[c] * e.Gain, 0.0F, 4.0F) * strength;
            // Lamplight through a window falls past the frame and whatever
            // stands outside it (the light sits off the facade, so the wall
            // does not shadow it), and daylight through a doorway casts the
            // shadows of whatever stands in it; broad sources light without one.
            light.CastsShadow = e.Category == NativeStaticEmitterCategory::WindowLamp ||
                                e.Category == NativeStaticEmitterCategory::Doorway;
            lights.push_back(light);
        }
    }
    if (emission != nullptr) emission->Follow(followed);
}

} // namespace Oot3dNativeGame
