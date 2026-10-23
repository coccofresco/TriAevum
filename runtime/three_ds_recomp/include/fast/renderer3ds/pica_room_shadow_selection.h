#pragma once

#include "fast/renderer3ds/pica_resolved_draw_stream.h"

namespace Fast::Renderer3ds {

enum class PicaRoomShadowCasterClass : uint8_t {
    Room,
    UnknownDomain,
    OtherDomain,
    Actor,
    OtherPass,
    AlphaTest,
    Translucent,
    Count,
};

[[nodiscard]] inline PicaRoomShadowCasterClass ClassifyPicaRoomShadowCaster(const PicaResolvedDrawRecord& draw) {
    if (draw.CompositionDomain == PicaCompositionDomain::Unknown)
        return PicaRoomShadowCasterClass::UnknownDomain;
    if (draw.CompositionDomain != PicaCompositionDomain::Scene)
        return PicaRoomShadowCasterClass::OtherDomain;
    if (draw.Composition.NativeActorGeometry)
        return PicaRoomShadowCasterClass::Actor;
    if (draw.Composition.Layer != PicaCompositionLayer::OpaqueWorld ||
        draw.Composition.Provenance != PicaCompositionProvenance::NativeCmbDrawPass ||
        draw.Composition.NativeProjectedShadow || draw.Composition.IsolatedLighting)
        return PicaRoomShadowCasterClass::OtherPass;
    // The room cache excludes all alpha-tested CMB materials, including an
    // Always comparison. Keep the corresponding native draw in every case.
    if (draw.Material.AlphaTestEnabled)
        return PicaRoomShadowCasterClass::AlphaTest;
    if (!PicaBlendReplacesDestination(draw.Material.Blend))
        return PicaRoomShadowCasterClass::Translucent;
    return PicaRoomShadowCasterClass::Room;
}

struct PicaRoomShadowSelection {
    std::array<size_t, static_cast<size_t>(PicaRoomShadowCasterClass::Count)> Classes{};
    size_t Snapshots = 0;
    size_t ReadySnapshots = 0;
    // First published snapshot, even if its draw is not replaceable. Diagnostics
    // must not mistake rejection of architecture for a publication failure.
    const RoomShadowShellScene* PublishedScene = nullptr;
    const RoomShadowShellScene* ReplacementScene = nullptr;
    const char* Status = "no_room_casters";

    [[nodiscard]] size_t Count(PicaRoomShadowCasterClass kind) const {
        return Classes[static_cast<size_t>(kind)];
    }
};

// All architecture must share one complete snapshot. Pending/missing/mixed
// snapshots keep every native room caster until a coherent replacement exists.
[[nodiscard]] inline PicaRoomShadowSelection
SelectPicaRoomShadowScene(std::span<const PicaResolvedDrawRecord* const> casters) {
    PicaRoomShadowSelection result;
    const RoomShadowShellScene* candidate = nullptr;
    bool missing = false, pending = false, mixed = false;
    for (const auto* draw : casters) {
        const auto* scene = draw->RoomShadows.get();
        if (scene) {
            ++result.Snapshots;
            result.ReadySnapshots += scene->Ready();
            if (!result.PublishedScene)
                result.PublishedScene = scene;
        }
        const auto kind = ClassifyPicaRoomShadowCaster(*draw);
        ++result.Classes[static_cast<size_t>(kind)];
        if (kind != PicaRoomShadowCasterClass::Room)
            continue;
        if (!scene) {
            missing = true;
            continue;
        }
        pending |= !scene->Ready();
        mixed |= candidate && candidate != scene;
        candidate = scene;
    }
    if (result.Count(PicaRoomShadowCasterClass::Room) != 0) {
        result.Status = missing ? "no_snapshot" : pending ? "pending_or_failed" : mixed ? "mixed_snapshots" : "ready";
        if (!missing && !pending && !mixed)
            result.ReplacementScene = candidate;
    }
    return result;
}

} // namespace Fast::Renderer3ds
