#include "mm3d_native_a32_lighting.h"
#include "mm3d_scene_environment.h"
#include "mm3d_local_light_sources.h"
#include "mm3d_room_emitters.h"
#include "mm3d_synthetic_emitters.h"
#include "recomp/a32_runtime.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <unordered_set>

namespace Oot3dNativeGame {
namespace {
using namespace Fast::Renderer3ds;
using LightSource = PicaNativeLightSource;
using Memory = oot3d::recomp::a32::MemoryBus;
bool Object(uint32_t p, uint32_t size) {
    return p != 0U && !(p & 3U) && p <= UINT32_MAX - size;
}
bool ReadColor(Memory& memory, uint32_t p, std::array<float, 3>& out) {
    for (uint32_t i = 0; i < 3; ++i) {
        uint8_t b = 0;
        if (!memory.Read8(p + i, &b)) return false;
        out[i] = float(b) / 255.0F;
    }
    return true;
}
bool ReadPosition(Memory& memory, uint32_t p, std::array<float, 3>& out) {
    for (uint32_t i = 0; i < 3; ++i) {
        uint32_t b = 0;
        if (!memory.Read32(p + i * 4U, &b)) return false;
        out[i] = std::bit_cast<float>(b);
        if (!std::isfinite(out[i])) return false;
    }
    return true;
}
std::optional<NativeEnvironmentState> ReadNativeEnvironmentState(uint32_t play, Memory& memory) {
    if (!Object(play, 0x11030U)) return std::nullopt;
    const uint32_t env = play + 0x86B0U;
    NativeEnvironmentState state;
    uint32_t weight = 0;
    uint8_t extraOverride = 0;
    if (!memory.Read16(play + 0x148U, &state.Scene) ||
        !memory.Read16(0x7751F8U, &state.DayTime) || !memory.Read8(env + 0x54U, &state.LightMode) ||
        !memory.Read8(env + 0x14CU, &state.Blending) || !memory.Read8(env + 0x14DU, &state.Setting) ||
        !memory.Read8(env + 0x14EU, &state.PreviousSetting) || !memory.Read8(env + 0x14FU, &state.SettingOverride) ||
        !memory.Read32(env + 0x174U, &weight) || !memory.Read8(env + 0x178U, &extraOverride))
        return std::nullopt;
    state.BlendWeight = std::bit_cast<float>(weight);
    if (!std::isfinite(state.BlendWeight)) return std::nullopt;
    // MM's extra blend override must not be treated as the ordinary clock route.
    if (extraOverride) state.LightMode = 0xFFU;
    return state;
}
std::array<int, 3> ClockLightBytes(uint16_t clock) {
    const double angle = double(std::bit_cast<int16_t>(uint16_t(clock - 0x8000U))) *
        (2.0 * std::numbers::pi / 65536.0);
    return {int(-std::sin(angle) * 120.0), int(std::cos(angle) * 120.0), int(std::cos(angle) * 20.0)};
}
bool SceneLightTraceEnabled() {
    static const bool enabled = std::getenv("TRIAEVUM_SCENE_LIGHT_TRACE") != nullptr;
    return enabled;
}
} // namespace

void Mm3dNativeA32LightingProbe::ObserveAcceptedLight(uint32_t owner, uint32_t params, uint32_t record,
                                                     bool point, Memory& memory) {
    const uint64_t first = uint64_t{owner} + 0xCU;
    if (!Object(owner, 0xFCU) || !Object(params, 0x14U) || record < first ||
        record - first >= 10U * 0x18U || (record - first) % 0x18U) return;
    auto& light = mBoundLights[owner].Sources[(record - first) / 0x18U];
    light = {};
    LightSource value;
    if (!ReadColor(memory, params + (point ? 0xCU : 3U), value.UnattenuatedColor)) return;
    if (point) {
        uint16_t radius = 0;
        if (!ReadPosition(memory, params, value.PositionWorld) || !memory.Read16(params + 0x12U, &radius)) return;
        value.Radius = float(std::bit_cast<int16_t>(radius));
        if (value.Radius <= 0.0F) return; // MM -1 is dormant, never abs().
    }
    value.SourceIdentity = params;
    value.Kind = point ? LightSource::Type::Point : LightSource::Type::Directional;
    light = value;
}

void Mm3dNativeA32LightingProbe::ObserveBlockEntry(uint32_t pc,
    const oot3d::recomp::a32::GuestState& state, Memory& memory) {
    switch (pc) {
    case 0x0015E9ECU:
        // Unknown copies/recycled owners cannot inherit a previous frame's sources.
        mBoundLights.clear(); mRenderStates.clear(); mUploadOwners.clear();
        mPreviousDrawnActors = std::move(mDrawnActors); mDrawnActors.clear();
        break;
    case 0x00182C44U: {
        uint32_t callback=0;
        if (Object(state.r[0],0x148U) && memory.Read32(state.r[0]+0x144U,&callback) &&
            callback && callback==state.r[2]) mDrawnActors.insert(state.r[0]);
        break;
    }
    case 0x0040650CU: ObserveAcceptedLight(state.r[0], state.r[1], state.r[3], true, memory); break;
    case 0x004648F4U: ObserveAcceptedLight(state.r[0], state.r[1], state.r[2], false, memory); break;
    case 0x001BFB40U: mBoundLights.erase(state.r[1]); break;
    case 0x001EFC7CU: {
        uint32_t cursor = 0;
        if (Object(state.r[1], 0x50U) && memory.Read32(state.r[1] + 0x4CU, &cursor) && cursor >= 0x100U)
            mBoundLights.erase(cursor - 0x100U);
        break;
    }
    case 0x001E7244U: mBoundLights.erase(state.r[0]); break;
    case 0x001EEAA8U:
        mRenderStates.erase(state.r[1]);
        mUploadOwners[state.r[1]] = state.r[0];
        break;
    case 0x001FD5E8U:
        if (state.r[1] < 4U) mRenderStates[state.r[0]].Sources[state.r[1]] = {};
        break;
    case 0x001F2294U:
        // The scene's other color writer (LR111CC0) is not Lights_Draw.
        // Invalidate that context slot instead of attributing it to a stale source.
        if (state.r[14] != 0x001EEDF0U) {
            for (auto& [context, binding] : mRenderStates)
                for (uint32_t slot = 0; slot < 4U; ++slot)
                    if (uint64_t{context} + 0x70U + slot * 0x118U == state.r[0]) binding.Sources[slot] = {};
        }
        break;
    case 0x001EED64U: {
        const uint32_t slot = state.r[5], context = state.r[8], record = state.r[1];
        if (slot >= 4U || !Object(context, 0x4F8U) ||
            uint64_t{context} + 0x70U + slot * 0x118U != state.r[0]) break;
        auto& target = mRenderStates[context].Sources[slot];
        target = {};
        const auto owner = mUploadOwners.find(context);
        if (owner == mUploadOwners.end()) break;
        const auto bound = mBoundLights.find(owner->second);
        const uint64_t first = uint64_t{owner->second} + 0xCU;
        if (bound == mBoundLights.end() || record < first || record - first >= 240U || (record - first) % 24U) break;
        LightSource source = bound->second.Sources[(record - first) / 24U];
        uint8_t kind = 0;
        if (!memory.Read8(record, &kind) || source.Kind == LightSource::Type::Unavailable ||
            kind != (source.Kind == LightSource::Type::Point ? 1U : 0U) ||
            !ReadColor(memory, record + 1U, source.ReferenceColor)) break;
        target = source;
        break;
    }
    default: break;
    }
}

void Mm3dNativeA32LightingProbe::ContinueSceneLights(Fast::Renderer3ds::PicaSceneDirectionalLights& lights,
                                                 uint32_t playState,
                                                 const std::optional<NativeEnvironmentState>& environment) {
    using namespace Fast::Renderer3ds;
    // Continuous scene light (docs/TRIAEVUM_ANIMATED_LIGHTING_STRATEGY.md,
    // 5.1-5.4), advanced once per guest frame. MM3D's world is Y-up.
    lights.WorldUp = { 0.0F, 1.0F, 0.0F };
    const uint32_t count = std::min<uint32_t>(lights.Count, kMaximumPicaSceneDirectionalLights);
    bool timeDriven = environment && environment->TimeDriven() && count == 2U;
    if (timeDriven) {
        const auto expected = ClockLightBytes(environment->DayTime);
        const uint64_t sun = uint64_t{playState} + 0x86D4U;
        const int index = lights.Lights[0].SourceIdentity == sun ? 0 : lights.Lights[1].SourceIdentity == sun ? 1 : -1;
        timeDriven = index >= 0;
        for (uint32_t axis = 0; timeDriven && axis < 3U; ++axis) {
            const float a = std::round(lights.Lights[index].DirectionTowardSource[axis] * 127.0F);
            const float b = std::round(lights.Lights[1-index].DirectionTowardSource[axis] * 127.0F);
            timeDriven = std::abs(a - float(expected[axis])) <= 1.0F && b == -a;
        }
    }
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
        const uint64_t sunIdentity = uint64_t{playState} + 0x86D0U + 4U;
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
        timeDriven == mContinuityTimeDriven && count == mContinuityCount;
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
    mContinuityTimeDriven = timeDriven;
    lights.ContinuityEpoch = mContinuityEpoch;
    // One guest frame of MM3D's 30 Hz update.
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

void Mm3dNativeA32LightingProbe::CaptureSceneLights(uint32_t play, Memory& memory) {
    using namespace Fast::Renderer3ds;
    const auto current = CaptureMm3dSceneEnvironment(memory);
    if (!current.State || current.Play != play) {
        mScenePoints.reset(); mSceneDirectionals.reset(); mWater.reset(); mLightFunctions.clear();
        mHaveSnapshot = false; mContinuityEnvironment.reset(); mShadowCaster.Reset(); mSelector.Reset();
        if (mEmitterCatalog) PicaSurfaceEmissionRegistry::Instance().Follow({});
        return;
    }
    const uint32_t frame = current.State->GuestFrame;
    if (mHaveSnapshot && mSnapshotPlay == play && mSnapshotFrame == frame &&
        mSnapshotScene == current.State->SceneId && mSnapshotDay == current.State->Day) return;
    const bool dayReset = mSnapshotDay && current.State->Day && *current.State->Day < *mSnapshotDay;
    const bool discontinuity = !mHaveSnapshot || mSnapshotPlay != play ||
        frame != mSnapshotFrame + 1U || mSnapshotScene != current.State->SceneId || dayReset;
    if (discontinuity) {
        mContinuityEnvironment.reset(); mShadowCaster.Reset();
        mPreviousDrawnActors.clear(); mDrawnActors.clear();
        if (mEmitterCatalog) PicaSurfaceEmissionRegistry::Instance().Follow({});
    }
    mHaveSnapshot = true; mSnapshotPlay = play; mSnapshotFrame = frame;
    mSnapshotScene = current.State->SceneId; mSnapshotDay = current.State->Day;
    ++mSceneLightFrames;
    std::unordered_set<uint32_t> infos, nodes;
    mLightFunctions.clear();
    uint32_t node = 0;
    if (memory.Read32(play + 0xA88U, &node)) {
        for (uint32_t n = 0; node && n < 32U; ++n) {
            uint32_t info = 0, next = 0;
            if (!Object(node, 12U) || !nodes.insert(node).second || !memory.Read32(node, &info) ||
                !memory.Read32(node + 8U, &next)) break;
            if (Object(info, 24U)) infos.insert(info);
            node = next;
        }
    }
    PicaSceneDirectionalLights directionals;
    for (uint32_t offset : {0x86D0U, 0x86E8U}) {
        const uint32_t info = play + offset;
        uint8_t kind = 0;
        PicaSceneDirectionalLight light;
        bool valid = infos.contains(info) && memory.Read8(info, &kind) && kind == 1U && ReadColor(memory, info + 7U, light.Color);
        for (uint32_t axis = 0; valid && axis < 3U; ++axis) {
            uint8_t b = 0; valid = memory.Read8(info + 4U + axis, &b);
            light.DirectionTowardSource[axis] = float(std::bit_cast<int8_t>(b)) / 127.0F;
        }
        light.SourceIdentity = info + 4U; // same identity as accepted params
        if (valid) {
            directionals.Lights[directionals.Count++] = light;
            // The environment's own sun/moon records, still linked: scene-wide.
            mLightFunctions[light.SourceIdentity] = {PicaNativeLightOrigin::EnvironmentDirectional,
                                                     PicaNativeLightScope::Scene};
        }
    }
    // Only the environment's records are collected above.
    if (directionals.Count) directionals.Scope = PicaNativeLightScope::Scene;
    ContinueSceneLights(directionals, play, ReadNativeEnvironmentState(play, memory));
    {
        // Outdoor groups bake the environment's lights into the room colour,
        // whether they follow the clock or fixed light settings (OoT3D
        // oot3d_native_a32_lighting.cpp, same rule); Indoor and Unknown do not.
        const auto group = mGroup.Observe(memory, play, current.State->SceneId, frame);
        directionals.BakedOutdoorRoom = group && *group != SceneEnvironmentGroup::Indoor;
    }
    if (!directionals.Count) mSceneDirectionals.reset();
    else if (!mSceneDirectionals || *mSceneDirectionals != directionals)
        mSceneDirectionals = std::make_shared<const PicaSceneDirectionalLights>(directionals);

    PicaScenePointLights points;
    std::vector<PicaScenePointLight> candidates;
    std::optional<std::array<float, 3>> player;
    std::unordered_set<uint32_t> visitedActors, capturedInfos;
    const auto capturePoint = [&](uint32_t actor, uint32_t offset, uint32_t nodeOffset, uint32_t group) {
        const uint32_t info = actor + offset;
        uint32_t ownedNode = 0, owner = 0;
        uint16_t rawRadius = 0;
        uint8_t kind = 0;
        PicaScenePointLight light;
        if (capturedInfos.contains(info) || !infos.contains(info) || !memory.Read32(actor + nodeOffset, &ownedNode) || !nodes.contains(ownedNode) ||
            !memory.Read32(ownedNode, &owner) || owner != info || !memory.Read8(info, &kind) ||
            (kind != 0U && kind != 2U) || !ReadPosition(memory, info + 4U, light.PositionWorld) ||
            !ReadColor(memory, info + 0x10U, light.Color) || !memory.Read16(info + 0x16U, &rawRadius)) return;
        light.SourceIdentity = info + 4U;
        light.Radius = std::max(0.0F, float(std::bit_cast<int16_t>(rawRadius)));
        light.Group = group;
        light.ReplacesNativeLocal = Mm3dLocalLightSourceProved(memory, actor, offset, nodeOffset);
        // Registered by this actor through its own LightNode: local.
        mLightFunctions[light.SourceIdentity] = {PicaNativeLightOrigin::Actor, PicaNativeLightScope::Local};
        if (light.ReplacesNativeLocal) {
            capturedInfos.insert(info);
            if (light.Radius>0) candidates.push_back(light);
        }
    };
    for (uint32_t category = 0; category < 12U; ++category) {
        uint32_t count = 0, actor = 0;
        const uint32_t list = play + 0x20C0U + category * 12U;
        if (!memory.Read32(list, &count) || !memory.Read32(list + 4U, &actor)) continue;
        for (uint32_t n = 0; actor && n < std::min(count, 255U); ++n) {
            uint16_t id = 0, params = 0;
            uint8_t actualCategory = 0;
            uint32_t next = 0, update = 0;
            if (!Object(actor, 0x1000U) || !visitedActors.insert(actor).second || !memory.Read16(actor, &id) ||
                !memory.Read8(actor + 2U, &actualCategory) || actualCategory != category ||
                !memory.Read16(actor + 0x1CU, &params) || !memory.Read32(actor + 0x134U, &next) ||
                !memory.Read32(actor + 0x140U, &update)) break;
            if (update) {
                if (id == 0 && category == 2) {
                    std::array<float, 3> position;
                    if (ReadPosition(memory, actor + 0x24U, position)) player = position;
                }
                const auto firstCandidate=candidates.size();
                for (const auto& source : kMm3dLocalLightSources) {
                    if (source.Actor != id || source.Category != category) continue;
                    const uint32_t group = id == 0x10U ? (params == 0 ? 0U : 1U) : 2U;
                    capturePoint(actor, source.Info, source.Node, group);
                }
                const bool nativePoint=candidates.size()!=firstCandidate;
                if (id==0x10 && !nativePoint && capturedInfos.contains(actor+0x578U))
                    CaptureMm3dVisibleFairy(play,actor,params,memory,candidates);
                CaptureMm3dSyntheticEmitters(play,actor,id,memory,nativePoint,
                    mPreviousDrawnActors.contains(actor),candidates);
            }
            actor = next;
        }
    }
    // MM torches already register native sources. Never add a synthetic copy.
    // Visible dormant fairies and unregistered fire phases use native gates
    // above; native player/carried-fire records remain exact replacements.
    if (!mEmitterCatalogResolved) {
        mEmitterCatalog = PicaEmitterCatalog::FromEnvironment("mm3d");
        mEmitterCatalogResolved = true;
    }
    if (mEmitterCatalog) {
        const auto rooms = CaptureMm3dEmitterRooms(play, memory);
        CapturePicaRoomEmitters(*mEmitterCatalog, current.State->SceneId, rooms, candidates,
                               &PicaSurfaceEmissionRegistry::Instance());
    }
    mSelector.Select(candidates, player, discontinuity, points);
    if (!points.Count) mScenePoints.reset();
    else if (!mScenePoints || *mScenePoints != points) mScenePoints = std::make_shared<const PicaScenePointLights>(points);
    auto water = CaptureMm3dWaterBodies(play, memory);
    if (!water.Count) mWater.reset();
    else if (!mWater || *mWater != water) mWater = std::make_shared<const PicaSceneWaterBodies>(water);
}

bool Mm3dNativeA32LightingProbe::VerifyEnvironmentGroupGuards(Memory& memory) {
    const bool hold = Mm3dEnvironmentGroupGuardsHold(memory);
    mGroup.SetGuardsHold(hold);
    return hold;
}

Fast::Renderer3ds::PicaSceneWaterBodies CaptureMm3dWaterBodies(uint32_t playState,
                                                               oot3d::recomp::a32::MemoryBus& memory) {
    using namespace Fast::Renderer3ds;
    PicaSceneWaterBodies result;
    uint32_t header = 0, boxes = 0;
    uint16_t count = 0;
    if (playState == 0 || playState > std::numeric_limits<uint32_t>::max() - 0xAB0U ||
        !memory.Read32(playState + 0xAB0U, &header) || header == 0 ||
        header > std::numeric_limits<uint32_t>::max() - 0x2CU || !memory.Read16(header + 0x16U, &count) ||
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

std::optional<std::array<float, 16>> Mm3dNativeA32LightingProbe::CaptureWorldToView(Memory& memory) {
    const auto current = CaptureMm3dSceneEnvironment(memory);
    if (!current.State) return std::nullopt;
    std::array<float, 16> result{}; result[15] = 1.0F;
    for (uint32_t row = 0; row < 3; ++row) for (uint32_t column = 0; column < 4; ++column) {
        uint32_t bits = 0;
        if (!memory.Read32(current.View + 0x17CU + (row * 4U + column) * 4U, &bits)) return std::nullopt;
        const float f = std::bit_cast<float>(bits);
        if (!std::isfinite(f)) return std::nullopt;
        result[column * 4U + row] = f;
    }
    return result;
}

Fast::Renderer3ds::PicaNativeLightBinding Mm3dNativeA32LightingProbe::CaptureRenderer(uint32_t renderer, Memory& memory) const {
    Fast::Renderer3ds::PicaNativeLightBinding result;
    uint32_t context = 0;
    if (Object(renderer, 0x14U) && memory.Read32(renderer + 0x10U, &context)) {
        if (const auto found = mRenderStates.find(context); found != mRenderStates.end()) result = found->second;
        uint8_t enabled = 0;
        if (!Object(context, 0x4F8U) || !memory.Read8(context + 0x6CU, &enabled)) result = {};
        else for (uint32_t slot = 0; slot < 4U; ++slot) if (!(enabled & (1U << slot))) result.Sources[slot] = {};
    }
    result.WorldToView = CaptureWorldToView(memory);
    const auto current = CaptureMm3dSceneEnvironment(memory);
    const bool snapshotCurrent = mHaveSnapshot && current.State && current.Play == mSnapshotPlay &&
        current.State->GuestFrame == mSnapshotFrame && current.State->SceneId == mSnapshotScene &&
        current.State->Day == mSnapshotDay;
    // Each selected slot's function, from this frame's LightContext only; an
    // unproved owner (or a stale snapshot) stays Unknown and Local.
    for (auto& source : result.Sources) {
        source.Origin = PicaNativeLightOrigin::Unknown;
        source.Scope = PicaNativeLightScope::Local;
        if (!snapshotCurrent || source.Kind == LightSource::Type::Unavailable) continue;
        if (const auto found = mLightFunctions.find(source.SourceIdentity); found != mLightFunctions.end()) {
            source.Origin = found->second.Origin;
            source.Scope = found->second.Scope;
        }
    }
    if (result.WorldToView && snapshotCurrent) {
        if (mPublishPoints) result.ScenePoints = mScenePoints;
        result.SceneDirectionals = mSceneDirectionals;
        if (mPublishWater) result.Water = mWater;
    }
    return result;
}
void Mm3dNativeA32LightingProbe::Reset() noexcept {
    mBoundLights.clear(); mRenderStates.clear(); mUploadOwners.clear();
    mScenePoints.reset(); mSceneDirectionals.reset(); mWater.reset(); mHaveSnapshot = false;
    mLightFunctions.clear(); mGroup.Reset(); mSelector.Reset();
    mPreviousDrawnActors.clear(); mDrawnActors.clear();
    if (mEmitterCatalog) PicaSurfaceEmissionRegistry::Instance().Follow({});
    mContinuityPlayState = 0; mContinuityEnvironment.reset(); mContinuityCount = 0;
    ++mContinuityEpoch; mShadowCaster.Reset();
}
} // namespace Oot3dNativeGame
