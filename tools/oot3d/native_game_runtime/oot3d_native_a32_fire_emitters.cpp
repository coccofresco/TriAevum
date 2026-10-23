#include "oot3d_native_a32_fire_emitters.h"

#include "recomp/a32_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace Oot3dNativeGame {
namespace {

using Fast::Renderer3ds::PicaScenePointLight;
using Vec3 = std::array<float, 3>;

constexpr Vec3 kFire{1.0F, 0.6F, 0.25F};
constexpr Vec3 kBlueFire{0.35F, 0.7F, 1.0F};
constexpr Vec3 kWarpCrystal{0.5F, 0.8F, 1.0F};
constexpr Vec3 kLightRing{0.6F, 0.9F, 1.0F};

// Actor fields (EUR): world position, home Y, world yaw, scale, shape yaw,
// shape offset and the draw callback.
constexpr uint32_t kPosition = 0x28U;
constexpr uint32_t kHomeY = 0x0CU;
constexpr uint32_t kWorldYaw = 0x36U;
constexpr uint32_t kScale = 0x54U;
constexpr uint32_t kShapeOffset = 0xC4U;
constexpr uint32_t kShapeYaw = 0xBEU;
constexpr uint32_t kUpdate = 0x13CU;
constexpr uint32_t kDraw = 0x140U;

class Reader {
public:
    Reader(oot3d::recomp::a32::MemoryBus& memory, uint32_t actor) : mMemory(memory), mActor(actor) {}
    bool F(uint32_t offset, float& value) const {
        uint32_t bits = 0;
        if (!mMemory.Read32(mActor + offset, &bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    bool V(uint32_t offset, Vec3& value) const {
        return F(offset, value[0]) && F(offset + 4U, value[1]) && F(offset + 8U, value[2]);
    }
    bool U32(uint32_t offset, uint32_t& value) const { return mMemory.Read32(mActor + offset, &value); }
    bool S16(uint32_t offset, int16_t& value) const {
        uint16_t raw = 0;
        if (!mMemory.Read16(mActor + offset, &raw)) return false;
        value = std::bit_cast<int16_t>(raw);
        return true;
    }
    bool U8(uint32_t offset, uint8_t& value) const { return mMemory.Read8(mActor + offset, &value); }
    bool NonZero32(uint32_t offset) const {
        uint32_t value = 0;
        return U32(offset, value) && value != 0U;
    }

private:
    oot3d::recomp::a32::MemoryBus& mMemory;
    uint32_t mActor;
};

// Binary angle to the title's forward direction (sin yaw, 0, cos yaw).
Vec3 Forward(int32_t yaw) {
    const float angle = static_cast<float>(static_cast<int16_t>(yaw)) * (2.0F * std::numbers::pi_v<float> / 65536.0F);
    return {std::sin(angle), 0.0F, std::cos(angle)};
}

Vec3 Add(const Vec3& a, const Vec3& b, float scale = 1.0F) {
    return {a[0] + b[0] * scale, a[1] + b[1] * scale, a[2] + b[2] * scale};
}

struct Emitter {
    std::vector<PicaScenePointLight>& Lights;
    uint32_t Actor;
    void Add(uint32_t slot, const Vec3& position, float radius, float level, const Vec3& color = kFire) {
        if (!(radius > 0.0F) || !(level > 0.0F) || !std::isfinite(radius + level + position[0] + position[1] + position[2]))
            return;
        PicaScenePointLight light;
        light.SourceIdentity = (uint64_t{slot} << 32U) | (Actor + kPosition);
        light.Group = 2U;
        light.PositionWorld = position;
        light.Radius = radius;
        const float clamped = std::min(level, 1.0F);
        light.Color = {color[0] * clamped, color[1] * clamped, color[2] * clamped};
        Lights.push_back(light);
    }
};

// Centroid of jet fireballs weighted by their size.
struct Centroid {
    Vec3 Sum{};
    float Weight = 0.0F;
    float MaxSize = 0.0F;
    void Add(const Vec3& point, float size) {
        for (size_t i = 0; i < 3; ++i) Sum[i] += point[i] * size;
        Weight += size;
        MaxSize = std::max(MaxSize, size);
    }
    [[nodiscard]] Vec3 Point() const { return {Sum[0] / Weight, Sum[1] / Weight, Sum[2] / Weight}; }
};

// Bg_Hidan_Sekizou's fireballs along one jet (helper 0x0031F978): segments
// [4 - t, 4) while it starts, [0, min(4, 54 - t)) after; EUR skips the
// fourth. q follows the rolling phase (+0x1C8).
Centroid SekizouJet(const Vec3& origin, int32_t yaw, int timer, int phase) {
    Centroid jet;
    if (timer <= 0) return jet;
    const int first = timer < 4 ? 4 - timer : 0;
    const int end = timer < 4 ? 4 : std::min(4, 54 - timer);
    const Vec3 forward = Forward(yaw);
    for (int j = first; j < end; ++j) {
        if (j == 3) continue;
        const float q = static_cast<float>(j + 1) + static_cast<float>(4 - phase) / 4.0F;
        const float size = 0.5F + 0.7F * q;
        const float distance = 20.0F + 10.0F * size * q;
        Vec3 point = Add(origin, forward, distance);
        point[1] += 30.0F + 0.7F * q;
        jet.Add(point, size * size);
    }
    return jet;
}

} // namespace

bool IsNativeFireEmitterActor(uint16_t id) noexcept {
    switch (id) {
    case 0x000U: case 0x00CU: case 0x012U: case 0x013U: case 0x038U: case 0x044U: case 0x045U: case 0x046U:
    case 0x049U: case 0x05DU: case 0x08BU: case 0x08DU: case 0x099U: case 0x09FU: case 0x0A3U: case 0x0B5U:
    case 0x0E1U: case 0x10AU: case 0x11CU: case 0x176U: case 0x04AU:
        return true;
    default:
        return false;
    }
}

void CaptureNativeFireEmitters(oot3d::recomp::a32::MemoryBus& memory, uint32_t playState, uint32_t actor,
                               uint16_t id, uint16_t params, std::vector<PicaScenePointLight>& lights) {
    const Reader a(memory, actor);
    Vec3 p{}, scale{};
    float shapeOffset = 0.0F;
    uint32_t draw = 0;
    if (!a.V(kPosition, p) || !a.V(kScale, scale) || !a.F(kShapeOffset, shapeOffset) || !a.U32(kDraw, draw) ||
        draw == 0U)
        return;
    // The ordinary actor matrix: position raised by the shape offset.
    const Vec3 o{p[0], p[1] + shapeOffset * scale[1], p[2]};
    Emitter out{lights, actor};
    switch (id) {
    case 0x013U: {
        // En_Firefly: a Keese while its aura (u8 +0x250) is fire; the invisible
        // draw (0x002A3F04) needs the lens (PlayState +0x208F). Between the
        // burning wings (+0x1A4, +0x1B0, stored 5 lower) when they are fresh.
        uint8_t aura = 0;
        if (!a.U8(0x250U, aura) || aura != 1U) return;
        if (draw == 0x002A3F04U) {
            uint8_t lens = 0;
            if (!memory.Read8(playState + 0x208FU, &lens) || lens == 0U) return;
        }
        Vec3 position = o, left{}, right{};
        if (a.V(0x1A4U, left) && a.V(0x1B0U, right)) {
            const Vec3 wings{(left[0] + right[0]) * 0.5F, (left[1] + right[1]) * 0.5F + 5.0F,
                             (left[2] + right[2]) * 0.5F};
            const float dx = wings[0] - o[0], dy = wings[1] - o[1], dz = wings[2] - o[2];
            if (dx * dx + dy * dy + dz * dz < 100.0F * 100.0F) position = wings;
        }
        out.Add(0U, position, 300.0F, 1.0F);
        return;
    }
    case 0x11CU: {
        // En_Honotrap's flames (params 1, 2) grow from 0.0001 to 0.004 / 0.0048
        // and shrink as they vanish; the eye (0) is no flame.
        if (params != 1U && params != 2U) return;
        const float grown = params == 1U ? 0.004F : 0.0048F;
        const float level = std::clamp((scale[0] - 0.0001F) / (grown - 0.0001F), 0.0F, 1.0F);
        const float size = scale[0] / 0.004F;
        out.Add(0U, {o[0], o[1] + 20.0F * size, o[2]}, 500.0F * size, level);
        return;
    }
    case 0x00CU: {
        // Bg_Hidan_Firewall while drawn: height scale 0.01 (countdown) to 0.1.
        const float level = std::clamp(scale[1] / 0.1F, 0.0F, 1.0F);
        out.Add(0U, {o[0], o[1] + 40.0F * level, o[2]}, 1000.0F * std::sqrt(level), level);
        return;
    }
    case 0x0B5U: {
        // En_Fire_Rock's burning kinds (s16 +0x1E4: 0, 3 falling, 6 on the
        // floor); its flames spawn around the position.
        int16_t kind = 0;
        if (!a.S16(0x1E4U, kind) || (kind != 0 && kind != 3 && kind != 6) || !(scale[0] > 0.0F)) return;
        out.Add(0U, p, 400.0F * std::clamp(scale[0] / 0.03F, 0.3F, 2.0F), 1.0F);
        return;
    }
    case 0x038U: {
        // En_Bw (Torch Slug): the flame (scale +0x46C, 0.6 lit, 0 out) above
        // the body (EnBw_Draw 0x001ADEEC).
        float flame = 0.0F;
        if (!a.F(0x46CU, flame)) return;
        const float g = std::max(0.0F, flame / 0.6F);
        out.Add(0U, {p[0], p[1] + (scale[1] - 0.013F) * 1000.0F + 20.0F * g, p[2]}, 500.0F * g, g);
        return;
    }
    case 0x012U: {
        // En_Dodongo breathing fire (action 0x003DC074, state +0x978 == 3):
        // emits while its animation frame (+0x1E0) is 29..43, fading by 10.
        uint32_t action = 0, state = 0;
        float frame = 0.0F;
        int16_t yaw = 0;
        if (!a.U32(0x97CU, action) || action != 0x003DC074U || !a.U32(0x978U, state) || state != 3U ||
            !a.F(0x1E0U, frame) || frame < 29.0F || frame > 43.0F || !a.S16(kWorldYaw, yaw))
            return;
        const float alpha = 255.0F - 10.0F * std::trunc(frame - 29.0F);
        Vec3 jet = Add(p, Forward(yaw), 70.0F);
        jet[1] += 22.0F;
        out.Add(0U, jet, 400.0F, alpha / 255.0F);
        return;
    }
    case 0x049U: {
        // Bg_Hidan_Curtain, a fire circle: material alpha u8 +0x1AB follows its
        // rise and sink; scale 0.09 or 0.045 (EUR table 0x00511648).
        uint8_t alpha = 0;
        if (!a.NonZero32(0x208U) || !a.U8(0x1ABU, alpha) || alpha == 0U) return;
        out.Add(0U, {o[0], o[1] + 40.0F * scale[1] / 0.09F, o[2]}, 600.0F * scale[0] / 0.09F, alpha / 255.0F);
        return;
    }
    case 0x08DU: {
        // Bg_Hidan_Fwbig, the great fire wall: 7920 * scale X wide along its
        // local X (shape yaw), 2400 * scale Y tall, buried below its home Y
        // as it lowers. Three points across the exposed part.
        float homeY = 0.0F;
        int16_t yaw = 0;
        if (!a.NonZero32(0x204U) || !a.F(kHomeY, homeY) || !a.S16(kShapeYaw, yaw) || !(scale[1] > 0.0F)) return;
        const float height = 2400.0F * scale[1];
        const float exposed = std::clamp(o[1] + height - homeY, 0.0F, height);
        const float level = exposed / height;
        const Vec3 forward = Forward(yaw);
        const Vec3 across{forward[2], 0.0F, -forward[0]};
        const float radius = 500.0F * (scale[0] / 0.1F) * std::sqrt(level);
        for (uint32_t slot = 0; slot < 3U; ++slot) {
            Vec3 point = Add(o, across, (static_cast<float>(slot) - 1.0F) * 2640.0F * scale[0]);
            point[1] = homeY + exposed * 0.5F;
            out.Add(slot, point, radius, level);
        }
        return;
    }
    case 0x045U: {
        // Bg_Hidan_Sekizou, the fire-breathing statues: params 0 two jets at
        // shape yaw +-0x2000 on one timer (+0x1C0), else four cardinal jets
        // with their own timers (+0x1C0 + 2i); light at each visible jet.
        int16_t yaw = 0, phase = 0;
        if (!a.S16(kShapeYaw, yaw) || !a.S16(0x1C8U, phase)) return;
        const uint32_t jets = params == 0U ? 2U : 4U;
        for (uint32_t i = 0; i < jets; ++i) {
            int16_t timer = 0;
            if (!a.S16(params == 0U ? 0x1C0U : 0x1C0U + 2U * i, timer)) continue;
            const int32_t jetYaw = params == 0U ? yaw + (i == 0U ? 0x2000 : -0x2000) : yaw + static_cast<int32_t>(i) * 0x4000;
            const auto jet = SekizouJet(p, jetYaw, timer, phase);
            if (jet.Weight > 0.0F) out.Add(i, jet.Point(), 450.0F, std::sqrt(jet.Weight / 13.0F));
        }
        return;
    }
    case 0x044U: {
        // Bg_Hidan_Rsekizou, the rotating fire statue: two curved arms of four
        // fireballs (phase s16 +0x1BC, shape yaw); one light per arm.
        int16_t yaw = 0, phase = 0;
        if (!a.S16(kShapeYaw, yaw) || !a.S16(0x1BCU, phase)) return;
        for (uint32_t arm = 0; arm < 2U; ++arm) {
            Centroid centroid;
            for (int n = 1; n <= 4; ++n) {
                const float q = n == 4 ? 4.0F : static_cast<float>(n) + static_cast<float>(3 - phase) / 3.0F;
                const float size = 0.5F + 0.7F * q;
                const float distance = 20.0F + 10.0F * size * q;
                Vec3 point = Add(p, Forward(yaw - 1500 * n), arm == 0U ? distance : -distance);
                point[1] += 30.0F + 0.7F * q;
                centroid.Add(point, size);
            }
            out.Add(arm, centroid.Point(), 400.0F, 1.0F);
        }
        return;
    }
    case 0x046U: {
        // Bg_Hidan_Sima's flame jet (params != 0, action 0x003DFD44): fireballs
        // behind its world yaw at height 40, spread by the 90-count timer.
        uint32_t action = 0;
        int16_t yaw = 0, timer = 0;
        if (params == 0U || !a.U32(0x1BCU, action) || action != 0x003DFD44U || !a.S16(kWorldYaw, yaw) ||
            !a.S16(0x1C0U, timer))
            return;
        const int t = timer;
        const int lo = std::max(0, 3 - (t >> 1));
        const int hi = std::min(3, (90 - t) >> 1);
        const Vec3 back = Forward(yaw - 0x8000);
        Centroid centroid;
        const auto ball = [&](float distance, float size) {
            Vec3 point = Add(p, back, distance);
            point[1] += 40.0F;
            centroid.Add(point, size);
        };
        for (int j = lo; j < hi; ++j)
            ball(80.0F + 25.0F * j + 4.0F * static_cast<float>(9 - t % 9), 1.0F + 0.4F * (j + 1));
        ball(80.0F + 25.0F * std::max(hi, 0), 1.0F + 0.4F * std::max(hi, 0));
        out.Add(0U, centroid.Point(), 450.0F * std::clamp(centroid.MaxSize / 2.2F, 0.5F, 1.0F), 1.0F);
        return;
    }
    case 0x10AU: {
        // Arrow_Fire: drawn with its parent arrow alive and timer +0x1BE < 255;
        // flame alpha u8 +0x1C0, charge radius s16 +0x1BC (0..10); actions
        // Charge 0x003F2F1C, Fly 0x0010E930, Hit 0x0010ECE4. At the arrow
        // (the parent, or this actor once the arrow stops, parent +0x305 & 2).
        uint32_t parent = 0, action = 0;
        uint16_t timer = 0;
        uint8_t alpha = 0;
        int16_t radius = 0;
        if (!a.U32(0x124U, parent) || parent == 0U || !a.NonZero32(0x1C4U) || !a.U32(0x1B8U, action) ||
            !memory.Read16(actor + 0x1BEU, &timer) || timer >= 255U || !a.U8(0x1C0U, alpha) || alpha == 0U ||
            !a.S16(0x1BCU, radius))
            return;
        const Reader arrow(memory, parent);
        uint8_t stopped = 0;
        if (!arrow.NonZero32(kUpdate)) return;
        Vec3 at = p;
        if (!(arrow.U8(0x305U, stopped) && (stopped & 2U) != 0U) && !arrow.V(kPosition, at)) return;
        float level = alpha / 255.0F;
        float reach = 250.0F;
        if (action == 0x003F2F1CU) {
            const float charge = std::clamp(radius / 10.0F, 0.0F, 1.0F);
            level *= charge;
            reach *= charge;
        } else if (action == 0x0010ECE4U) {
            const float k = (static_cast<float>(timer) - 8.0F) / 24.0F;
            reach += 350.0F * std::clamp(1.0F - k * k, 0.0F, 1.0F);
        }
        out.Add(0U, at, reach, level);
        return;
    }
    case 0x09FU: {
        // Magic_Fire (Din's Fire): the sphere is drawn while s16 +0x208 > 0,
        // alpha float +0x1FC, growing with the actor scale.
        int16_t phase = 0;
        float alpha = 0.0F;
        if (!a.S16(0x208U, phase) || phase <= 0 || !a.NonZero32(0x210U) || !a.F(0x1FCU, alpha) ||
            !(scale[0] > 0.0F))
            return;
        out.Add(0U, o, 1000.0F * scale[0], std::clamp(alpha, 0.0F, 1.0F));
        return;
    }
    case 0x0E1U: {
        // En_Anubice_Fire: a trail of six points (+0x1B8 + 12i) from the first
        // surviving one (s16 +0x1B6), each drawn at scale X - 0.2i when >= 0.1.
        int16_t first = 0;
        if (!a.NonZero32(0x258U) || !a.S16(0x1B6U, first) || first < 0 || first >= 6) return;
        const float v = scale[0] - 0.2F * static_cast<float>(first);
        Vec3 point{};
        if (v < 0.1F || !a.V(0x1B8U + 12U * static_cast<uint32_t>(first), point)) return;
        out.Add(0U, point, 400.0F * v, v);
        return;
    }
    case 0x0A3U: {
        // En_Fd_Fire (Volvagia's and the Flare Dancer's fire): size float
        // +0x204 starts at 3 and shrinks; params & 0x8000 is the blue flame.
        float size = 0.0F;
        if (!a.NonZero32(0x210U) || !a.F(0x204U, size) || !(size > 0.0F)) return;
        out.Add(0U, p, 350.0F * size / 3.0F, size / 3.0F, (params & 0x8000U) != 0U ? kBlueFire : kFire);
        return;
    }
    case 0x000U: {
        // Player: the lit Deku stick (held item s8 +0x1A9 == 6, flame timer
        // s16 +0x2248 > 0, flame f32 +0x2244 > 0), at the stick tip the post-
        // limb draw stores (+0x22A8, from the previous draw). The flame grows
        // in and dies out as the title's (literals 0x0025133C..364, frame
        // divisor s16 at [0x0051B2F4] + 0x110).
        uint8_t item = 0;
        int16_t timer = 0;
        float flame = 0.0F;
        Vec3 tip{};
        if (!a.U8(0x1A9U, item) || item != 6U || !a.S16(0x2248U, timer) || timer <= 0 || !a.F(0x2244U, flame) ||
            !(flame > 0.0F) || !a.V(0x22A8U, tip))
            return;
        uint32_t clock = 0;
        uint16_t divisorRaw = 1;
        float q = 1.0F;
        if (memory.Read32(0x0051B2F4U, &clock) && clock != 0U && memory.Read16(clock + 0x110U, &divisorRaw) &&
            std::bit_cast<int16_t>(divisorRaw) > 0)
            q = static_cast<float>(std::bit_cast<int16_t>(divisorRaw));
        const auto n = [&](float x) { return static_cast<float>(static_cast<int>(x / q + 0.5F)); };
        float level = 1.0F;
        const float t = static_cast<float>(timer);
        if (t > n(600.0F)) level = (n(30.0F) - (t - n(600.0F))) / std::max(1.0F, n(30.0F));
        else if (t < n(60.0F)) level = t / std::max(1.0F, n(60.0F));
        out.Add(0U, tip, 250.0F, std::clamp(level, 0.0F, 1.0F) * 0.6F);
        return;
    }
    case 0x099U: {
        // En_Fd, the Flare Dancer: its flame effects (100 at +0xE58, stride
        // 0x3C; type byte 2, scale f32 +4, RGBA bytes +0xC, position +0x14),
        // drawn at effect alpha times the body fade (f32 +0x628 / 255). One
        // light at their weighted centre.
        float fade = 0.0F;
        if (!a.F(0x628U, fade)) return;
        Centroid centroid;
        Vec3 colour{};
        float strongest = 0.0F;
        for (uint32_t i = 0; i < 100U; ++i) {
            const uint32_t e = 0xE58U + i * 0x3CU;
            uint8_t type = 0, r = 0, g = 0, b = 0, alpha = 0;
            float size = 0.0F;
            Vec3 at{};
            if (!a.U8(e, type) || type != 2U || !a.U8(e + 0xFU, alpha) || alpha == 0U || !a.F(e + 4U, size) ||
                !(size > 0.0F) || !a.V(e + 0x14U, at) || !a.U8(e + 0xCU, r) || !a.U8(e + 0xDU, g) ||
                !a.U8(e + 0xEU, b))
                continue;
            const float opacity = (alpha / 255.0F) * std::clamp(fade / 255.0F, 0.0F, 1.0F);
            const float weight = opacity * size * size;
            if (!(weight > 0.0F)) continue;
            centroid.Add(at, weight);
            colour = {colour[0] + r / 255.0F * weight, colour[1] + g / 255.0F * weight,
                      colour[2] + b / 255.0F * weight};
            strongest = std::max(strongest, opacity);
        }
        if (!(centroid.Weight > 0.0F)) return;
        Vec3 tint{colour[0] / centroid.Weight, colour[1] / centroid.Weight, colour[2] / centroid.Weight};
        const float peak = std::max({tint[0], tint[1], tint[2], 1.0e-3F});
        for (auto& channel : tint) channel /= peak;
        out.Add(0U, centroid.Point(), 500.0F, strongest * 0.7F, tint);
        return;
    }
    case 0x05DU: {
        // Door_Warp1's blue warp crystal (params -2, no native light): crystal
        // alpha f32 +0x478 / 255 (DoorWarp1_Draw 0x002456FC).
        float alpha = 0.0F;
        if (params != 0xFFFEU || !a.F(0x478U, alpha) || !(alpha > 0.0F)) return;
        out.Add(0U, {p[0], p[1] + 40.0F, p[2]}, 250.0F, std::clamp(alpha / 255.0F, 0.0F, 1.0F) * 0.5F,
                kWarpCrystal);
        return;
    }
    case 0x08BU: {
        // Demo_Effect's light rings (params 7, 16, 17): draw 0x00262D34
        // installed at +0x2A0, model +0x2A8, alpha u8 +0x28A.
        const uint32_t kind = params & 0xFFU;
        uint32_t ringDraw = 0;
        uint8_t alpha = 0;
        if ((kind != 7U && kind != 16U && kind != 17U) || !a.U32(0x2A0U, ringDraw) || ringDraw != 0x00262D34U ||
            !a.NonZero32(0x2A8U) || !a.U8(0x28AU, alpha) || alpha == 0U)
            return;
        out.Add(0U, p, 400.0F, alpha / 255.0F * 0.5F, kLightRing);
        return;
    }
    case 0x176U: {
        // Efc_Erupc, the volcano's eruption (a cutscene): its particles (100 at
        // +0x1B4, stride 0x4C: active byte +0x34, size f32 +4, position +0x10,
        // alpha s16 +0x36, RGB +0x3C), one light at their weighted centre.
        Centroid centroid;
        Vec3 colour{};
        float strongest = 0.0F;
        for (uint32_t i = 0; i < 100U; ++i) {
            const uint32_t e = 0x1B4U + i * 0x4CU;
            uint8_t active = 0, r = 0, g = 0, b = 0;
            int16_t alpha = 0;
            float size = 0.0F;
            Vec3 at{};
            if (!a.U8(e + 0x34U, active) || active == 0U || !a.S16(e + 0x36U, alpha) || alpha <= 0 ||
                !a.F(e + 4U, size) || !(size > 0.0F) || !a.V(e + 0x10U, at) || !a.U8(e + 0x3CU, r) ||
                !a.U8(e + 0x3DU, g) || !a.U8(e + 0x3EU, b))
                continue;
            const float opacity = std::clamp(alpha / 255.0F, 0.0F, 1.0F);
            const float weight = opacity * size;
            centroid.Add(at, weight);
            colour = {colour[0] + r / 255.0F * weight, colour[1] + g / 255.0F * weight,
                      colour[2] + b / 255.0F * weight};
            strongest = std::max(strongest, opacity);
        }
        if (!(centroid.Weight > 0.0F)) return;
        Vec3 tint{colour[0] / centroid.Weight, colour[1] / centroid.Weight, colour[2] / centroid.Weight};
        const float peak = std::max({tint[0], tint[1], tint[2], 1.0e-3F});
        for (auto& channel : tint) channel /= peak;
        out.Add(0U, centroid.Point(), 800.0F, strongest * 0.6F, tint);
        return;
    }
    case 0x04AU: {
        // Bg_Spot00_Hanebasi, Hyrule Field's drawbridge: the bridge itself
        // (params -1) draws the two torch flames beside the gate at world
        // (+-260, 128, 690) (BgSpot00Hanebasi_Draw 0x0038AF04, literals
        // 0x0038B2F8..304), scaled by the static at 0x005135B4: 0 while the
        // bridge is down, growing to 0.008 as it rises at night, 0.008 in
        // the later scene layers. Layer 12 (save context +0x14E8 at
        // 0x00588E40) draws none and leaves the static as it was.
        constexpr uint32_t kFlameScale = 0x005135B4U, kSceneLayer = 0x00588E40U;
        uint32_t scaleBits = 0, layer = 0;
        if (params != 0xFFFFU || !memory.Read32(kSceneLayer, &layer) || layer == 12U ||
            !memory.Read32(kFlameScale, &scaleBits))
            return;
        const float level = std::bit_cast<float>(scaleBits) / 0.008F;
        if (!std::isfinite(level)) return;
        for (uint32_t slot = 0; slot < 2U; ++slot)
            out.Add(slot, {slot == 0U ? 260.0F : -260.0F, 128.0F + 40.0F, 690.0F}, 500.0F, level);
        return;
    }
    default:
        return;
    }
}

} // namespace Oot3dNativeGame
