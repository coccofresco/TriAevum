#include "oot3d_native_a32_lighting.h"

#include "recomp/a32_runtime.h"
#include "oot3d_native_a32_fire_emitters.h"
#include "oot3d/actor_spawn.h"
#include <cstddef>

#include <algorithm>
#include <string_view>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>

namespace Oot3dNativeGame {
namespace {

using LightSource = Fast::Renderer3ds::PicaNativeLightSource;

// EUR Environment_Update (0045DD50): the environment context is PlayState +
// 0x3190 and the clock is gSaveContext (00587958) + 0xC. With lightMode
// (+0x20) zero and no setting override (+0xA7 == 0xFF) the update derives
// the first light's direction from the clock (0045E438..0045E4A0: -sin,
// cos and cos of clock - 0x8000, times 120, 120 and 20, truncated to bytes)
// and the second light is its negation; otherwise it copies light setting
// +0xA5, blended from +0xA6 by the weight at +0xC8 while +0xA4 is set.
constexpr uint32_t kEnvironmentContextOffset = 0x3190U;
constexpr uint32_t kSaveContextDayTime = 0x00587964U;

std::optional<NativeEnvironmentState> ReadNativeEnvironmentState(uint32_t playState,
                                                                 oot3d::recomp::a32::MemoryBus& memory) {
    if (playState == 0 || playState > std::numeric_limits<uint32_t>::max() - kEnvironmentContextOffset - 0xCCU)
        return std::nullopt;
    const uint32_t environment = playState + kEnvironmentContextOffset;
    NativeEnvironmentState state;
    uint32_t weight = 0;
    if (!memory.Read16(playState + 0x104U, &state.Scene) || !memory.Read16(kSaveContextDayTime, &state.DayTime) ||
        !memory.Read8(environment + 0x20U, &state.LightMode) ||
        !memory.Read8(environment + 0xA7U, &state.SettingOverride) ||
        !memory.Read8(environment + 0xA5U, &state.Setting) ||
        !memory.Read8(environment + 0xA6U, &state.PreviousSetting) ||
        !memory.Read8(environment + 0xA4U, &state.Blending) || !memory.Read32(environment + 0xC8U, &weight))
        return std::nullopt;
    state.BlendWeight = std::bit_cast<float>(weight);
    return state;
}

// The bytes the time-driven update stores for this clock, up to the guest's
// sine table: a component may differ by one unit at a truncation boundary.
std::array<int, 3> ClockLightBytes(uint16_t dayTime) {
    const double angle = double(std::bit_cast<int16_t>(uint16_t(dayTime - 0x8000U))) *
        (2.0 * std::numbers::pi / 65536.0);
    return {int(-std::sin(angle) * 120.0), int(std::cos(angle) * 120.0), int(std::cos(angle) * 20.0)};
}

bool SceneLightTraceEnabled() {
    static const bool enabled = std::getenv("TRIAEVUM_SCENE_LIGHT_TRACE") != nullptr;
    return enabled;
}

// TRIAEVUM_SCENE_LIGHT_TRACE=1: one stderr line per guest frame whose native
// environment light state changed (scene, mode, clock, settings, both
// directional records), with whether the records match the clock; and one
// line per guest frame with the continuous lights published for it.
void TraceSceneLights(uint64_t frame, uint32_t playState, oot3d::recomp::a32::MemoryBus& memory,
                      NativeSceneLightTraceRecord& last) {
    if (!SceneLightTraceEnabled()) return;
    const auto environment = ReadNativeEnvironmentState(playState, memory);
    if (!environment) return;
    NativeSceneLightTraceRecord record{*environment};
    for (uint32_t i = 0; i < 2U; ++i) {
        const uint32_t info = playState + (i == 0U ? 0x31BCU : 0x31D4U);
        uint8_t kind = 0;
        if (!memory.Read8(info, &kind) || kind != 1U) continue;
        bool valid = true;
        for (uint32_t b = 0; valid && b < 6U; ++b) valid = memory.Read8(info + 4U + b, &record.Lights[i][b]);
        if (valid) record.LightCount |= 1U << i;
    }
    if (record == last) return;
    last = record;
    const auto& e = record.Environment;
    const auto expected = ClockLightBytes(e.DayTime);
    bool clockMatch = (record.LightCount & 3U) == 3U;
    for (uint32_t axis = 0; clockMatch && axis < 3U; ++axis) {
        const int first = std::bit_cast<int8_t>(record.Lights[0][axis]);
        const int second = std::bit_cast<int8_t>(record.Lights[1][axis]);
        clockMatch = std::abs(first - expected[axis]) <= 1 && second == -first;
    }
    std::fprintf(stderr,
                 "TRIAEVUM_SCENE_LIGHT_TRACE frame=%llu scene=%04x mode=%s light_mode=%u override=%u setting=%u "
                 "previous=%u blending=%u weight=%.4f clock=%04x clock_match=%d",
                 static_cast<unsigned long long>(frame), e.Scene, e.TimeDriven() ? "time" : "settings", e.LightMode,
                 e.SettingOverride, e.Setting, e.PreviousSetting, e.Blending, e.BlendWeight, e.DayTime,
                 clockMatch ? 1 : 0);
    for (uint32_t i = 0; i < 2U; ++i) {
        const auto& l = record.Lights[i];
        if ((record.LightCount & (1U << i)) == 0U) {
            std::fprintf(stderr, " L%u=none", i);
            continue;
        }
        std::fprintf(stderr, " L%u=%d,%d,%d/%u,%u,%u", i, std::bit_cast<int8_t>(l[0]), std::bit_cast<int8_t>(l[1]),
                     std::bit_cast<int8_t>(l[2]), l[3], l[4], l[5]);
    }
    std::fprintf(stderr, "\n");
}

bool ReadColor(oot3d::recomp::a32::MemoryBus& memory, uint32_t address, std::array<float, 3>& color) {
    for (uint32_t channel = 0; channel < 3U; ++channel) {
        uint8_t value = 0;
        if (!memory.Read8(address + channel, &value))
            return false;
        color[channel] = static_cast<float>(value) / 255.0F;
    }
    return true;
}

bool ReadPosition(oot3d::recomp::a32::MemoryBus& memory, uint32_t address, std::array<float, 3>& position) {
    for (uint32_t axis = 0; axis < 3U; ++axis) {
        uint32_t bits = 0;
        if (!memory.Read32(address + axis * 4U, &bits))
            return false;
        const float value = std::bit_cast<float>(bits);
        if (!std::isfinite(value))
            return false;
        position[axis] = value;
    }
    return true;
}

bool CaptureFairyEmitter(oot3d::recomp::a32::MemoryBus& memory, uint32_t actor,
                         Fast::Renderer3ds::PicaScenePointLight& light) {
    // EnElf_Draw (EUR 001D6EC4) gates the glow with phase, flags, scale
    // and core opacity. Non-Navi variants leave their native LightInfos
    // dormant, so the extension emitter follows the live actor instead.
    uint16_t phase = 0, flags = 0;
    uint32_t draw = 0;
    const auto scalar = [&](uint32_t offset, float& value) {
        uint32_t bits = 0;
        if (!memory.Read32(actor + offset, &bits))
            return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    };
    float scale = 0, opacity = 0, alpha = 0;
    std::array<float, 3> position{}, color{};
    if (!memory.Read32(actor + offsetof(Oot3dActor, draw), &draw) || draw == 0 ||
        !memory.Read16(actor + 0x9A8U, &phase) || phase == 8U || !memory.Read16(actor + 0x9C4U, &flags) ||
        (flags & 8U) != 0U || !scalar(0x54U, scale) || scale == 0 || !scalar(0x924U, opacity) || opacity <= 0 ||
        !scalar(0x934U, alpha) || alpha <= 0 || !ReadPosition(memory, actor + 0x28U, position) ||
        !ReadPosition(memory, actor + 0x928U, color))
        return false;
    const float emission = std::clamp(opacity, 0.0F, 1.0F) * std::clamp(alpha / 255.0F, 0.0F, 1.0F);
    light.PositionWorld = position;
    for (size_t channel = 0; channel < color.size(); ++channel)
        light.Color[channel] = std::clamp(color[channel] / 255.0F, 0.0F, 1.0F) * emission;
    // Title extension default, in world units, matching Navi's normal reach.
    // Environment owns the user's radius multiplier; native state is untouched.
    light.Radius = 100.0F;
    return true;
}

} // namespace

void NativeA32LightingProbe::ObserveAcceptedLight(uint32_t lights, uint32_t source, uint32_t record, bool point,
                                                  oot3d::recomp::a32::MemoryBus& memory) {
    // Lights holds a 16-byte header and seven 16-byte native records. The
    // observed instruction runs only after the guest accepted this record.
    const uint64_t first = static_cast<uint64_t>(lights) + 0x10U;
    if (record < first || record - first >= 7U * 0x10U || ((record - first) & 0xFU) != 0U ||
        source > std::numeric_limits<uint32_t>::max() - 0x14U)
        return;
    const size_t slot = static_cast<size_t>((record - first) / 0x10U);
    auto& bound = mBoundLights[lights];
    auto& light = bound.Sources[slot];
    light = {};
    bound.Count = std::max(bound.Count, static_cast<uint8_t>(slot + 1U));
    if (!ReadColor(memory, source + (point ? 0xCU : 3U), light.UnattenuatedColor))
        return;
    if (point) {
        uint16_t radius = 0;
        if (!ReadPosition(memory, source, light.PositionWorld) || !memory.Read16(source + 0x10U, &radius))
            return;
        light.Radius = std::abs(static_cast<float>(std::bit_cast<int16_t>(radius)));
        if (light.Radius == 0.0F)
            return;
    }
    light.Kind = point ? LightSource::Type::Point : LightSource::Type::Directional;
    light.SourceIdentity = source;
}

void NativeA32LightingProbe::ObserveAssignment(const oot3d::recomp::a32::GuestState& state,
                                               oot3d::recomp::a32::MemoryBus& memory) {
    if (state.r[1] >= 3U)
        return;
    if (const auto found = mRenderStates.find(state.r[0]); found != mRenderStates.end())
        found->second.Sources[state.r[1]] = {};
    // At the native Cmb direction setter's demonstrated callsite, r5 is the
    // selected Light_t and r6 is its Lights owner. Selection (including the
    // >3-light strongest-point branch and its tie behavior) remains native.
    if (state.r[14] != 0x0035BEE8U)
        return;
    const auto found = mBoundLights.find(state.r[6]);
    if (found == mBoundLights.end())
        return;
    const uint64_t first = static_cast<uint64_t>(state.r[6]) + 0x10U;
    const uint32_t record = state.r[5];
    if (record < first || ((record - first) & 0xFU) != 0U)
        return;
    const uint64_t slot = (record - first) / 0x10U;
    if (slot >= found->second.Count)
        return;
    auto light = found->second.Sources[static_cast<size_t>(slot)];
    if (!ReadColor(memory, record, light.ReferenceColor))
        return;
    mRenderStates[state.r[0]].Sources[state.r[1]] = light;
}

void NativeA32LightingProbe::ObserveBlockEntry(uint32_t pc, const oot3d::recomp::a32::GuestState& state,
                                               oot3d::recomp::a32::MemoryBus& memory) {
    switch (pc) {
        case 0x00213DFCU:
        case 0x00253A74U:
            ObserveAcceptedLight(state.r[0], state.r[1], state.r[2], pc == 0x00213DFCU, memory);
            break;
        case 0x002CF63CU:
            if (const auto found = mBoundLights.find(state.r[1]); found != mBoundLights.end())
                found->second = {};
            break;
        case 0x0035BFB4U: {
            uint32_t cursor = 0;
            if (state.r[1] <= std::numeric_limits<uint32_t>::max() - 0xCU &&
                memory.Read32(state.r[1] + 0xCU, &cursor) && cursor >= 0x80U) {
                if (const auto found = mBoundLights.find(cursor - 0x80U); found != mBoundLights.end())
                    found->second = {};
            }
            break;
        }
        case 0x0035BBE0U:
        case 0x00340F44U:
            // The ambient-only owner never binds source directions.
            if (const auto found = mRenderStates.find(state.r[1]); found != mRenderStates.end())
                found->second = {};
            break;
        case 0x00310F7CU:
            if (const auto found = mRenderStates.find(state.r[1]); found != mRenderStates.end())
                mRenderStates[state.r[0]] = found->second;
            else if (const auto destination = mRenderStates.find(state.r[0]); destination != mRenderStates.end())
                destination->second = {};
            break;
        case 0x0033D174U:
            if (state.r[1] < 3U) {
                if (const auto found = mRenderStates.find(state.r[0]); found != mRenderStates.end())
                    found->second.Sources[state.r[1]] = {};
            }
            break;
        case 0x0033D14CU:
            ObserveAssignment(state, memory);
            break;
        default:
            break;
    }
}

void NativeA32LightingProbe::ContinueSceneLights(Fast::Renderer3ds::PicaSceneDirectionalLights& lights,
                                                 uint32_t playState,
                                                 const std::optional<NativeEnvironmentState>& environment) {
    using namespace Fast::Renderer3ds;
    // Continuous scene light (docs/TRIAEVUM_ANIMATED_LIGHTING_STRATEGY.md,
    // 5.1-5.4), advanced once per guest frame. OOT3D's world is Y-up.
    lights.WorldUp = { 0.0F, 1.0F, 0.0F };
    const uint32_t count = std::min<uint32_t>(lights.Count, kMaximumPicaSceneDirectionalLights);
    const bool timeDriven = environment && environment->TimeDriven();
    // The native records in byte units (directions /127, colours /255).
    std::array<std::array<float, 3>, kMaximumPicaSceneDirectionalLights> directionBytes{};
    std::array<std::array<float, 3>, kMaximumPicaSceneDirectionalLights> colourBytes{};
    for (uint32_t i = 0; i < count; ++i) {
        lights.Lights[i].SkyLight = timeDriven;
        for (size_t axis = 0; axis < 3U; ++axis) {
            directionBytes[i][axis] = std::round(lights.Lights[i].DirectionTowardSource[axis] * 127.0F);
            colourBytes[i][axis] = std::round(lights.Lights[i].Color[axis] * 255.0F);
        }
    }
    // A time-driven environment's lights are the sun (the first record) and
    // its opposite, from the clock. Publish the clock's direction, in the
    // records' scale, unless another writer changed the records.
    std::array<std::optional<std::array<float, 3>>, kMaximumPicaSceneDirectionalLights> exact{};
    if (timeDriven && count == 2U) {
        const uint64_t sunIdentity = uint64_t{playState} + 0x31BCU + 4U;
        const int sun = lights.Lights[0].SourceIdentity == sunIdentity ? 0
            : lights.Lights[1].SourceIdentity == sunIdentity          ? 1
                                                                       : -1;
        const auto expected = ClockLightBytes(environment->DayTime);
        bool match = sun >= 0;
        for (size_t axis = 0; match && axis < 3U; ++axis)
            match = std::abs(directionBytes[sun][axis] - float(expected[axis])) <= 1.0F &&
                directionBytes[1 - sun][axis] == -directionBytes[sun][axis];
        if (match) {
            const double angle = double(std::bit_cast<int16_t>(uint16_t(environment->DayTime - 0x8000U))) *
                (2.0 * std::numbers::pi / 65536.0);
            const std::array<float, 3> value{ float(-std::sin(angle) * 120.0 / 127.0),
                                              float(std::cos(angle) * 120.0 / 127.0),
                                              float(std::cos(angle) * 20.0 / 127.0) };
            exact[sun] = value;
            exact[1 - sun] = std::array<float, 3>{ -value[0], -value[1], -value[2] };
        }
    }
    // A discontinuity: another play state, scene or light mode, other
    // records, or a direction that turned more than kCutDegrees in a frame.
    constexpr float kCutCosine = 0.990268F; // cos(8 degrees)
    bool continuous = playState != 0 && playState == mContinuityPlayState && environment &&
        mContinuityEnvironment && environment->Scene == mContinuityEnvironment->Scene &&
        timeDriven == mContinuityEnvironment->TimeDriven() && count == mContinuityCount;
    std::array<ContinuityLight, kMaximumPicaSceneDirectionalLights> next{};
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t identity = lights.Lights[i].SourceIdentity;
        const auto found = std::find_if(mContinuityLights.begin(), mContinuityLights.begin() + mContinuityCount,
                                        [&](const auto& light) { return light.Identity == identity; });
        std::array<float, 3> direction = exact[i] ? *exact[i]
            : std::array<float, 3>{ directionBytes[i][0] / 127.0F, directionBytes[i][1] / 127.0F,
                                    directionBytes[i][2] / 127.0F };
        if (found == mContinuityLights.begin() + mContinuityCount) {
            continuous = false;
        } else {
            next[i] = *found;
            const auto unit = [](std::array<float, 3> value) {
                const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
                if (length > 1.0e-6F) for (float& component : value) component /= length;
                return value;
            };
            const auto before = unit(found->Direction);
            const auto after = unit(direction);
            if (before[0] * after[0] + before[1] * after[1] + before[2] * after[2] < kCutCosine) continuous = false;
        }
        next[i].Identity = identity;
        next[i].Direction = direction;
    }
    if (!continuous) ++mContinuityEpoch;
    for (uint32_t i = 0; i < count; ++i) {
        auto& light = lights.Lights[i];
        auto& signals = next[i].Signals;
        for (size_t axis = 0; axis < 3U; ++axis) {
            if (exact[i]) {
                light.DirectionTowardSource[axis] = (*exact[i])[axis];
                signals[axis].Update((*exact[i])[axis] * 127.0F, 1.0F, false);
            } else {
                light.DirectionTowardSource[axis] =
                    signals[axis].Update(directionBytes[i][axis], 1.0F, continuous) / 127.0F;
            }
            light.Color[axis] = signals[3U + axis].Update(colourBytes[i][axis], 1.0F, continuous) / 255.0F;
        }
    }
    mContinuityLights = next;
    mContinuityCount = count;
    mContinuityPlayState = playState;
    mContinuityEnvironment = environment;
    lights.ContinuityEpoch = mContinuityEpoch;
    // One guest frame of OOT3D's 30 Hz update.
    mShadowCaster.Update(lights, 1.0F / 30.0F, continuous);
    if (SceneLightTraceEnabled()) {
        std::fprintf(stderr,
                     "TRIAEVUM_SCENE_LIGHT_CONTINUITY frame=%llu epoch=%llu continuous=%d caster=%llx strength=%.4f",
                     static_cast<unsigned long long>(mSceneLightFrames),
                     static_cast<unsigned long long>(lights.ContinuityEpoch), continuous ? 1 : 0,
                     static_cast<unsigned long long>(lights.ShadowCasterIdentity), lights.ShadowStrength);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& light = lights.Lights[i];
            std::fprintf(stderr, " L%u=%llx/%.6f,%.6f,%.6f/%.4f,%.4f,%.4f/sky=%d/exact=%d", i,
                         static_cast<unsigned long long>(light.SourceIdentity), light.DirectionTowardSource[0],
                         light.DirectionTowardSource[1], light.DirectionTowardSource[2], light.Color[0],
                         light.Color[1], light.Color[2], light.SkyLight ? 1 : 0, exact[i] ? 1 : 0);
        }
        std::fprintf(stderr, "\n");
    }
}

// A flame actor's light: En_Light (0x008, fire objects such as the Fire
// Temple's great flames), Obj_Syokudai (0x05E, torches) and Bg_Po_Syokudai
// (0x105, the Forest Temple's coloured torches). Their Init inserts a
// LightContext record, which this capture does not read; the light follows
// the flame the title draws instead. Position: the actor's world position
// (+0x28) raised to the flame; reach from its scale (+0x54); colour from the
// flame's kind (OoT's En_Light table: 2 blue, 3 green, else orange). A torch
// is lit while its timer (s16 +0x27C, OoT's litTimer: -1 for good, a count
// down when the player lit it, 0 unlit) is non-zero: the same test
// ObjSyokudai_Draw (00286590) makes before drawing its flame.
static bool CaptureFlameEmitter(oot3d::recomp::a32::MemoryBus& memory, uint32_t playState, uint32_t actor,
                                uint16_t id, uint16_t params, Fast::Renderer3ds::PicaScenePointLight& light) {
    std::array<float, 3> position{};
    float scale = 0.0F;
    uint32_t bits = 0;
    if (!ReadPosition(memory, actor + 0x28U, position) || !memory.Read32(actor + 0x54U, &bits)) return false;
    scale = std::bit_cast<float>(bits);
    if (!std::isfinite(scale) || scale <= 0.0F) return false;
    light.SourceIdentity = actor + 0x28U;
    light.Group = 2U;
    light.PositionWorld = position;
    if (id == 0x008U) {
        // 0.0075 is the great flame (OoT table scale 75).
        const float size = std::clamp(scale / 0.0075F, 0.2F, 3.0F);
        light.PositionWorld[1] += 40.0F * size;
        light.Radius = 1400.0F * size;
        switch (params & 0xFU) {
        case 2U: light.Color = {0.35F, 0.7F, 1.0F}; break;
        case 3U: light.Color = {0.65F, 1.0F, 0.3F}; break;
        default: light.Color = {1.0F, 0.62F, 0.28F}; break;
        }
        return true;
    }
    if (id == 0x105U) {
        // Bg_Po_Syokudai (the Poe sisters' torches): BgPoSyokudai_Draw
        // (0029D5A0) draws the flame while the room switch flag in params is
        // set (Flags_GetSwitch 0036E864: PlayState +0x2228/+0x222C), coloured
        // by the index at +0x1A4 into its table at 005134FC.
        const uint32_t flag = static_cast<uint32_t>(static_cast<int16_t>(params));
        uint32_t flags = 0;
        uint8_t colour = 0;
        if (flag >= 64U || !memory.Read32(playState + (flag < 32U ? 0x2228U : 0x222CU), &flags) ||
            ((flags >> (flag & 31U)) & 1U) == 0U || !memory.Read8(actor + 0x1A4U, &colour))
            return false;
        static constexpr std::array<std::array<float, 3>, 4> kPoeFlames{{
            {1.0F, 0.67F, 1.0F}, {1.0F, 0.78F, 0.0F}, {0.0F, 0.67F, 1.0F}, {0.67F, 1.0F, 0.0F}}};
        light.Color = kPoeFlames[colour & 3U];
        light.PositionWorld[1] += 90.0F;
        light.Radius = 600.0F;
        return true;
    }
    uint16_t litTimer = 0;
    if (!memory.Read16(actor + 0x27CU, &litTimer) || litTimer == 0U) return false;
    light.PositionWorld[1] += 55.0F;
    light.Radius = 500.0F;
    light.Color = {1.0F, 0.6F, 0.25F};
    return true;
}

void NativeA32LightingProbe::SetRoomShadowShellsEnabled(bool enabled) {
    if (mRoomShellsEnabled.exchange(enabled, std::memory_order_relaxed) != enabled)
        std::fprintf(stderr, "TRIAEVUM_ROOM_SHADOW_SHELL event=enabled value=%d\n", enabled);
}

void NativeA32LightingProbe::CaptureSceneLights(uint32_t playState, oot3d::recomp::a32::MemoryBus& memory) {
    using namespace Fast::Renderer3ds;
    TraceSceneLights(++mSceneLightFrames, playState, memory, mSceneLightTrace);
    PicaSceneDirectionalLights directionals;
    // EUR Gameplay_Init (00449218..00449228) passes PlayState+3190 to
    // Environment_Init, which inserts LightInfo records at +2C and +44.
    // Read only records still linked in this PlayState's LightContext.
    if (playState != 0 && playState <= std::numeric_limits<uint32_t>::max() - 0x31DDU) {
        uint32_t node = 0;
        if (memory.Read32(playState + 0xA70U, &node)) {
            for (uint32_t visited = 0; node != 0 && visited < 32U; ++visited) {
                uint32_t info = 0, next = 0;
                if (node > std::numeric_limits<uint32_t>::max() - 8U ||
                    !memory.Read32(node, &info) || !memory.Read32(node + 8U, &next))
                    break;
                if (info == playState + 0x31BCU || info == playState + 0x31D4U) {
                    uint8_t kind = 0;
                    PicaSceneDirectionalLight light;
                    bool valid = memory.Read8(info, &kind) && kind == 1U &&
                        ReadColor(memory, info + 7U, light.Color);
                    for (uint32_t axis = 0; valid && axis < 3U; ++axis) {
                        uint8_t component = 0;
                        valid = memory.Read8(info + 4U + axis, &component);
                        // Native assignment negates /127 before CMB upload
                        // (0035BDF8..0035BEE4); the source itself points toward light.
                        light.DirectionTowardSource[axis] = float(std::bit_cast<int8_t>(component)) / 127.0F;
                    }
                    light.SourceIdentity = info + 4U;
                    const auto end = directionals.Lights.begin() + directionals.Count;
                    if (valid && directionals.Count < directionals.Lights.size() &&
                        std::none_of(directionals.Lights.begin(), end, [&](const auto& existing) {
                            return existing.SourceIdentity == light.SourceIdentity;
                        }))
                        directionals.Lights[directionals.Count++] = light;
                }
                node = next;
            }
        }
    }
    ContinueSceneLights(directionals, playState, ReadNativeEnvironmentState(playState, memory));
    if (directionals.Count == 0)
        mSceneDirectionals.reset();
    else if (!mSceneDirectionals || *mSceneDirectionals != directionals)
        mSceneDirectionals = std::make_shared<const PicaSceneDirectionalLights>(std::move(directionals));
    PicaScenePointLights snapshot;
    std::vector<PicaScenePointLight> candidates;
    std::optional<std::array<float, 3>> player;
    // EUR LightContext_InsertLight (0034FAA8) owns a 32-node pool; only
    // records still linked through their real native LightNode are accepted.
    static_assert(kMaximumPicaScenePointLights >= 32U);
    if (playState != 0 && playState <= std::numeric_limits<uint32_t>::max() - OOT3D_ACTOR_CONTEXT_PLAY_OFFSET -
                                           sizeof(Oot3dActorContext)) {
        const uint32_t context = playState + OOT3D_ACTOR_CONTEXT_PLAY_OFFSET;
        for (uint32_t category = 0; category < OOT3D_ACTOR_CATEGORY_COUNT; ++category) {
            const uint32_t list = context + offsetof(Oot3dActorContext, lists) + category * sizeof(Oot3dActorListEntry);
            uint32_t count = 0, actor = 0;
            if (!memory.Read32(list, &count) || !memory.Read32(list + 4U, &actor))
                continue;
            for (uint32_t i = 0; actor != 0 && i < std::min(count, 255U); ++i) {
                if (actor > std::numeric_limits<uint32_t>::max() - 0x9D0U)
                    break;
                uint16_t id = 0, params = 0;
                uint32_t next = 0, update = 0;
                uint8_t actualCategory = 0;
                if (!memory.Read16(actor + offsetof(Oot3dActor, id), &id) ||
                    !memory.Read8(actor + offsetof(Oot3dActor, category), &actualCategory) ||
                    actualCategory != category || !memory.Read32(actor + offsetof(Oot3dActor, next), &next) ||
                    !memory.Read32(actor + offsetof(Oot3dActor, update), &update))
                    break;
                // En_Elf's native profile owns both records. params 0 is Navi;
                // all other En_Elf variants belong to the fairy control group.
                if (id == 0x18U && update != 0 && memory.Read16(actor + offsetof(Oot3dActor, params), &params)) {
                    for (uint32_t offset : { 0x948U, 0x964U }) {
                        const uint32_t info = actor + offset;
                        uint32_t node = 0, owner = 0;
                        uint16_t radius = 0;
                        PicaScenePointLight light;
                        if (!memory.Read32(info + 0x18U, &node) || node == 0 || !memory.Read32(node, &owner) ||
                            owner != info || !ReadPosition(memory, info + 4U, light.PositionWorld) ||
                            !ReadColor(memory, info + 0x10U, light.Color) || !memory.Read16(info + 0x14U, &radius))
                            continue;
                        light.SourceIdentity = info + 4U;
                        light.Radius = std::max(0.0F, float(std::bit_cast<int16_t>(radius)));
                        light.Group = params == 0 ? 0U : 1U;
                        if (light.Group == 1U && offset == 0x948U && light.Radius == 0.0F)
                            (void)CaptureFairyEmitter(memory, actor, light);
                        candidates.push_back(light);
                    }
                }
                if (id == 0x000U && !player) {
                    std::array<float, 3> position{};
                    if (ReadPosition(memory, actor + 0x28U, position)) player = position;
                }
                // Flames the title draws but never lights (group 2): their
                // light is only baked into room vertex colours natively.
                const bool flame = id == 0x008U || id == 0x05EU || id == 0x105U;
                if ((flame || IsNativeFireEmitterActor(id)) && update != 0 &&
                    memory.Read16(actor + offsetof(Oot3dActor, params), &params)) {
                    PicaScenePointLight light;
                    if (!flame)
                        CaptureNativeFireEmitters(memory, playState, actor, id, params, candidates);
                    else if (CaptureFlameEmitter(memory, playState, actor, id, params, light))
                        candidates.push_back(light);
                }
                actor = next;
            }
        }
    }
    // The architecture's own light, in the rooms the title has loaded.
    const auto environmentState = ReadNativeEnvironmentState(playState, memory);
    const uint16_t scene = environmentState ? environmentState->Scene : uint16_t{0xFFFFU};
    const bool shellsEnabled = mRoomShellsEnabled.load(std::memory_order_relaxed) && environmentState.has_value();
    mRoomShells = shellsEnabled ? mRoomShellCache.Capture(true, (uint64_t(playState) << 16U) | scene,
                                                        NativeLoadedRooms(playState, memory), memory, playState) : nullptr;
    if (!mStaticEmittersResolved) {
        mStaticEmitters = NativeStaticEmitterCatalog::FromEnvironment();
        mStaticEmittersResolved = true;
    }
    if (mStaticEmitters && environmentState) {
        const auto rooms = NativeLoadedRooms(playState, memory);
        CaptureStaticEmitters(*mStaticEmitters, scene, rooms, candidates,
                              &Fast::Renderer3ds::PicaSurfaceEmissionRegistry::Instance());
    }
    // More lights than slots (the Fire Temple's maze room alone places 41
    // flames): keep Navi and the fairies, then choose and fade the added
    // lights by their light at Link, under a brightness ceiling.
    const bool discontinuity = playState != mSelectionPlayState || scene != mSelectionScene;
    mSelectionPlayState = playState;
    mSelectionScene = scene;
    mSelector.Select(candidates, player, discontinuity, snapshot);
    static const bool selectionTrace = std::getenv("TRIAEVUM_LIGHT_SELECTION_TRACE") != nullptr;
    // Every 30 captures, or every N with TRIAEVUM_LIGHT_SELECTION_TRACE_EVERY.
    static const uint64_t traceEvery = [] {
        const char* every = std::getenv("TRIAEVUM_LIGHT_SELECTION_TRACE_EVERY");
        const long value = every ? std::strtol(every, nullptr, 10) : 30L;
        return static_cast<uint64_t>(std::max(1L, value));
    }();
    if (selectionTrace && (mSceneLightFrames % traceEvery) == 0U) {
        uint32_t byGroup[4]{};
        for (const auto& light : candidates) ++byGroup[std::min<uint32_t>(light.Group, 3U)];
        const auto rooms = NativeLoadedRooms(playState, memory);
        const auto& stats = mSelector.Stats();
        std::fprintf(stderr,
                     "[light-selection] scene=%u rooms=%d,%d catalog=%zu candidates=%u/%u/%u/%u selected=%u "
                     "fading=%u snapshot=%u light@link=%.3f exposure=%.3f\n",
                     scene, rooms.size() > 0 ? rooms[0].Number : -1, rooms.size() > 1 ? rooms[1].Number : -1,
                     mStaticEmitters ? mStaticEmitters->Count() : size_t{0}, byGroup[0], byGroup[1], byGroup[2],
                     byGroup[3], stats.Selected, stats.Fading, snapshot.Count, stats.SummedLight, stats.Exposure);
        // TRIAEVUM_LIGHT_SELECTION_TRACE=2: every light the renderer receives.
        static const bool lightsTrace = std::string_view(std::getenv("TRIAEVUM_LIGHT_SELECTION_TRACE")) == "2";
        for (uint32_t i = 0; lightsTrace && i < snapshot.Count; ++i) {
            const auto& light = snapshot.Lights[i];
            std::fprintf(stderr,
                         "[light-selection]   group=%u id=%016llx at=%.0f,%.0f,%.0f radius=%.0f "
                         "colour=%.3f,%.3f,%.3f shadow=%d\n",
                         light.Group, static_cast<unsigned long long>(light.SourceIdentity), light.PositionWorld[0],
                         light.PositionWorld[1], light.PositionWorld[2], light.Radius, light.Color[0], light.Color[1],
                         light.Color[2], light.CastsShadow ? 1 : 0);
        }
    }
    if (snapshot.Count == 0)
        mScenePoints.reset();
    else if (!mScenePoints || *mScenePoints != snapshot)
        mScenePoints = std::make_shared<const PicaScenePointLights>(std::move(snapshot));
    auto water = CaptureNativeWaterBodies(playState, memory);
    if (water.Count == 0)
        mWater.reset();
    else if (!mWater || *mWater != water)
        mWater = std::make_shared<const PicaSceneWaterBodies>(std::move(water));
}

Fast::Renderer3ds::PicaSceneWaterBodies CaptureNativeWaterBodies(uint32_t playState,
                                                               oot3d::recomp::a32::MemoryBus& memory) {
    using namespace Fast::Renderer3ds;
    PicaSceneWaterBodies result;
    uint32_t header = 0, boxes = 0;
    uint16_t count = 0;
    if (playState == 0 || playState > std::numeric_limits<uint32_t>::max() - 0xA98U ||
        !memory.Read32(playState + 0xA98U, &header) || header == 0 ||
        header > std::numeric_limits<uint32_t>::max() - 0x2CU || !memory.Read16(header + 0x14U, &count) ||
        !memory.Read32(header + 0x28U, &boxes) || boxes == 0 ||
        boxes > std::numeric_limits<uint32_t>::max() - kMaximumPicaSceneWaterBodies * 0x10U)
        return result;
    for (uint32_t i = 0; i < std::min<uint32_t>(count, kMaximumPicaSceneWaterBodies); ++i) {
        std::array<uint16_t, 5> raw{};
        uint32_t properties = 0;
        bool valid = memory.Read32(boxes + i * 0x10U + 0x0CU, &properties);
        for (uint32_t field = 0; valid && field < raw.size(); ++field)
            valid = memory.Read16(boxes + i * 0x10U + field * 2U, &raw[field]);
        if (!valid) return {};
        const auto value = [&](size_t field) { return float(std::bit_cast<int16_t>(raw[field])); };
        if (value(3) <= 0.0F || value(4) <= 0.0F) continue;
        auto& body = result.Bodies[result.Count++];
        body.MinimumXZ = { value(0), value(2) };
        body.MaximumXZ = { value(0) + value(3), value(2) + value(4) };
        body.SurfaceY = value(1);
        body.Properties = properties;
    }
    return result;
}

Fast::Renderer3ds::PicaNativeLightBinding
NativeA32LightingProbe::CaptureRenderer(uint32_t renderer, oot3d::recomp::a32::MemoryBus& memory) const {
    Fast::Renderer3ds::PicaNativeLightBinding result;
    uint32_t renderState = 0;
    if (renderer <= std::numeric_limits<uint32_t>::max() - 0x10U && memory.Read32(renderer + 0x10U, &renderState)) {
        if (const auto found = mRenderStates.find(renderState); found != mRenderStates.end())
            result = found->second;
    }
    result.WorldToView = CaptureWorldToView(memory);
    result.ScenePoints = mScenePoints;
    result.SceneDirectionals = mSceneDirectionals;
    result.Water = mWater;
    result.RoomShadows = mRoomShells;
    return result;
}

std::optional<std::array<float, 16>> NativeA32LightingProbe::CaptureWorldToView(oot3d::recomp::a32::MemoryBus& memory) {
    // EUR revision 0: the matrices returned by 00313644 and 00466F3C.
    // CmbRenderState_PrepareLights multiplies post-view * world-to-view;
    // the bone-palette producer uses the same world-to-view for geometry.
    // Capture at packet submission, before another viewport can replace them.
    constexpr std::array<uint32_t, 2> addresses{ 0x005B5018U, 0x005B5058U };
    std::array<std::array<float, 12>, 2> matrices{};
    for (size_t matrix = 0; matrix < matrices.size(); ++matrix) {
        for (size_t component = 0; component < matrices[matrix].size(); ++component) {
            uint32_t word = 0;
            if (!memory.Read32(addresses[matrix] + static_cast<uint32_t>(component * 4U), &word))
                return std::nullopt;
            matrices[matrix][component] = std::bit_cast<float>(word);
            if (!std::isfinite(matrices[matrix][component]))
                return std::nullopt;
        }
    }
    std::array<float, 16> worldToView{};
    worldToView[15] = 1.0F;
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            float value = column == 3 ? matrices[1][row * 4 + 3] : 0.0F;
            for (size_t axis = 0; axis < 3; ++axis)
                value += matrices[1][row * 4 + axis] * matrices[0][axis * 4 + column];
            worldToView[column * 4 + row] = value;
        }
    }
    return worldToView;
}

void NativeA32LightingProbe::Reset() noexcept {
    mBoundLights.clear();
    mRenderStates.clear();
    mScenePoints.reset();
    mSceneDirectionals.reset();
    mWater.reset();
    mRoomShells.reset();
    mSceneLightTrace = {};
    // A state load is a discontinuity.
    mContinuityPlayState = 0;
    mContinuityEnvironment.reset();
    mContinuityCount = 0;
    ++mContinuityEpoch;
    mShadowCaster.Reset();
    mSelector.Reset();
    mSelectionPlayState = 0;
}

} // namespace Oot3dNativeGame
