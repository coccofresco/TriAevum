#include "fast/renderer3ds/pica_emitter_catalog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace Fast::Renderer3ds {
namespace {
bool Triple(const nlohmann::json &v, std::array<float, 3> &out) {
    if (!v.is_array() || v.size() != 3)
        return false;
    for (size_t i = 0; i < 3; ++i) {
        if (!v[i].is_number())
            return false;
        out[i] = v[i].get<float>();
        if (!std::isfinite(out[i]))
            return false;
    }
    return true;
}
std::string String(const nlohmann::json &o, const char *k) {
    const auto p = o.find(k);
    return p != o.end() && p->is_string() ? p->get<std::string>() : std::string{};
}
bool Finite(const nlohmann::json &o, const char *k, float &out) {
    const auto p = o.find(k);
    if (p == o.end() || !p->is_number())
        return false;
    out = p->get<float>();
    return std::isfinite(out);
}
bool Fingerprint(const nlohmann::json &v, uint64_t &out) {
    if (!v.is_string())
        return false;
    const auto s = v.get<std::string>();
    if (s.size() != 16)
        return false;
    out = 0;
    for (char c : s) {
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0)
            return false;
        out = (out << 4) | uint64_t(d);
    }
    return true;
}
bool Category(const std::string &s, PicaEmitterCategory &out) {
    if (s == "fire-lava")
        out = PicaEmitterCategory::FireLava;
    else if (s == "window-lamp")
        out = PicaEmitterCategory::WindowLamp;
    else if (s == "light-shaft")
        out = PicaEmitterCategory::LightShaft;
    else if (s == "magic-glow")
        out = PicaEmitterCategory::MagicGlow;
    else if (s == "lamp")
        out = PicaEmitterCategory::Lamp;
    else if (s == "doorway")
        out = PicaEmitterCategory::Doorway;
    else
        return false;
    return true;
}
} // namespace
uint64_t PicaEmitterRoomFingerprint(std::span<const uint8_t> bytes) noexcept {
    uint64_t h = 1469598103934665603ULL;
    for (auto b : bytes)
        h = (h ^ b) * 0x100000001B3ULL;
    return h;
}
std::shared_ptr<const PicaEmitterCatalog> PicaEmitterCatalog::Parse(std::string_view text,
                                                                    std::string *error) {
    const auto fail = [&](const std::string &s) -> std::shared_ptr<const PicaEmitterCatalog> {
        if (error)
            *error = s;
        return nullptr;
    };
    const auto d = nlohmann::json::parse(text, nullptr, false);
    if (d.is_discarded() || !d.is_object() || String(d, "schema") != "triaevum.scene-lights" ||
        !d.contains("version") || d["version"] != 1 || !d.contains("rooms") || !d["rooms"].is_array())
        return fail("not a triaevum.scene-lights version 1 catalog");
    auto catalog = std::make_shared<PicaEmitterCatalog>();
    catalog->mTitle = String(d, "title");
    try {
        for (const auto &room : d["rooms"]) {
            uint64_t fingerprint = 0;
            if (!room.is_object() || !room.contains("scene") || !room["scene"].is_number_unsigned() ||
                room["scene"].get<uint64_t>() > 0xffff || !room.contains("room") ||
                !room["room"].is_number_unsigned() || room["room"].get<uint64_t>() > 127 ||
                !Fingerprint(room.value("fingerprint", nlohmann::json()), fingerprint) ||
                !room.contains("emitters") || !room["emitters"].is_array())
                return fail("a room is malformed");
            auto [entry, added] = catalog->mRooms.try_emplace(
                {room["scene"].get<uint16_t>(), room["room"].get<int>(), fingerprint});
            if (!added)
                return fail("a room is listed twice");
            for (const auto &value : room["emitters"]) {
                PicaRoomEmitter e;
                if (!value.is_object() || !Triple(value.value("position", nlohmann::json()), e.Position) ||
                    !Triple(value.value("colour", nlohmann::json()), e.Colour) ||
                    !Category(String(value, "category"), e.Category) || !Finite(value, "radius", e.Radius) ||
                    !Finite(value, "gain", e.Gain) || !(e.Radius > 0) || e.Radius > 100000 ||
                    !(e.Gain >= 0) || e.Gain > 4)
                    return fail("a room has an invalid emitter");
                for (auto &c : e.Colour)
                    c = std::clamp(c, 0.0F, 1.0F);
                if (const auto p = value.find("independentEmission"); p != value.end()) {
                    if (!p->is_boolean())
                        return fail("malformed independent emission proof");
                    e.IndependentEmission = p->get<bool>();
                }
                if (const auto p = value.find("surfaces"); p != value.end()) {
                    if (!p->is_array() || p->size() > e.Surfaces.size())
                        return fail("malformed emitter surfaces");
                    for (size_t i = 0; i < p->size(); ++i)
                        if (!Fingerprint((*p)[i], e.Surfaces[i]))
                            return fail("malformed emitter surfaces");
                }
                entry->second.push_back(e);
                ++catalog->mCount;
            }
        }
    } catch (const nlohmann::json::exception &e) {
        return fail(std::string("malformed catalog: ") + e.what());
    }
    return catalog;
}
std::shared_ptr<const PicaEmitterCatalog> PicaEmitterCatalog::LoadFile(const std::filesystem::path &path,
                                                                       std::string *error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error)
            *error = "cannot open " + path.string();
        return nullptr;
    }
    std::stringstream text;
    text << file.rdbuf();
    return Parse(text.str(), error);
}
std::shared_ptr<const PicaEmitterCatalog> PicaEmitterCatalog::FromEnvironment(std::string_view title,
                                                                              bool allowUntitled) {
    const char *path = std::getenv("TRIAEVUM_SCENE_LIGHT_CATALOG");
    if (!path || !*path)
        return nullptr;
    std::string error;
    auto c = LoadFile(path, &error);
    if (c && !title.empty() && !c->MatchesTitle(title) && (!allowUntitled || !c->mTitle.empty())) {
        c.reset();
        error = "catalog title namespace mismatch";
    }
    if (c)
        std::fprintf(stderr, "TRIAEVUM_SCENE_LIGHT_CATALOG loaded=%zu path=%s\n", c->Count(), path);
    else
        std::fprintf(stderr, "TRIAEVUM_SCENE_LIGHT_CATALOG rejected: %s\n", error.c_str());
    return c;
}
std::span<const PicaRoomEmitter> PicaEmitterCatalog::Room(uint16_t scene, int room,
                                                          uint64_t fingerprint) const {
    const auto p = mRooms.find({scene, room, fingerprint});
    return p == mRooms.end() ? std::span<const PicaRoomEmitter>{} : std::span(p->second);
}
void CapturePicaRoomEmitters(const PicaEmitterCatalog &catalog, uint16_t scene,
                             std::span<const PicaEmitterRoom> rooms, std::vector<PicaScenePointLight> &lights,
                             PicaSurfaceEmissionRegistry *emission) {
    std::vector<uint64_t> followed;
    for (const auto &room : rooms) {
        const auto emitters = catalog.Room(scene, room.Number, room.Fingerprint);
        for (size_t i = 0; i < emitters.size(); ++i) {
            const auto &e = emitters[i];
            float strength = 1.0F;
            if (emission && e.Surfaces[0]) {
                strength = 0;
                for (auto surface : e.Surfaces) {
                    if (!surface)
                        continue;
                    followed.push_back(surface);
                    strength = std::max(strength, std::clamp(emission->Strength(surface), 0.0F, 1.0F));
                }
            }
            if (strength <= 0)
                continue;
            PicaScenePointLight light;
            light.SourceIdentity = (uint64_t{0xA5} << 56) | (uint64_t{scene} << 40) |
                                   (uint64_t(static_cast<uint32_t>(room.Number) & 0xff) << 32) | uint64_t(i);
            light.Group = 3;
            light.PositionWorld = e.Position;
            light.Radius = e.Radius;
            light.IndependentEmission = e.IndependentEmission;
            for (size_t c = 0; c < 3; ++c)
                light.Color[c] = std::clamp(e.Colour[c] * e.Gain, 0.0F, 4.0F) * strength;
            light.CastsShadow =
                e.Category == PicaEmitterCategory::WindowLamp || e.Category == PicaEmitterCategory::Doorway;
            lights.push_back(light);
        }
    }
    if (emission)
        emission->Follow(followed);
}
} // namespace Fast::Renderer3ds
