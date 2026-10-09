#include "fast/renderer3ds/pica_scene_water.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;

static void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static PicaSceneWaterBody Body(float minX, float minZ, float maxX, float maxZ, float y) {
    PicaSceneWaterBody body;
    body.MinimumXZ = {minX, minZ};
    body.MaximumXZ = {maxX, maxZ};
    body.SurfaceY = y;
    return body;
}

int main() {
    int checks = 0;
    PicaSceneWaterBodies bodies;
    // Zora's Domain (live boxes): the upper pools and the main pool.
    bodies.Bodies[0] = Body(-348, -1746, 205, -966, 877);
    bodies.Bodies[1] = Body(205, -1746, 1052, -1226, 877);
    bodies.Bodies[2] = Body(-1262, -1068, 1218, 350, 0);
    bodies.Count = 3;
    const auto match = [&](std::array<float, 3> minimum, std::array<float, 3> maximum) {
        ++checks;
        return MatchPicaWaterSurface(minimum, maximum, bodies);
    };
    // The main pool's overlay (actor, flat at y 0) lies on body 2.
    expect(match({-1136, 0, -1027}, {1085, 0, 165}) == 2U, "pool surface");
    // A few units off the gameplay height is still the surface.
    expect(match({-500, 3, -500}, {500, 3.5F, 0}) == 2U, "surface within tolerance");
    // A flat floor 20 units under an upper pool (its bed) is not water.
    expect(!match({-275, 857, -1626}, {205, 857, -966}), "pool bed");
    // A mixed room mesh (water and banks, y 0 to 410) is not one surface.
    expect(!match({-1645, -2, -1126}, {938, 411, 598}), "mixed room mesh");
    // A flat draw at the height but mostly outside the footprint (a bank).
    expect(!match({1100, 0, 300}, {2000, 0, 900}), "bank at water height");
    // A line (degenerate footprint) inside the body counts as covered.
    expect(match({10, 0, -400}, {10, 0, -100}) == 2U, "degenerate footprint");
    // No bodies: nothing is water.
    PicaSceneWaterBodies none;
    expect(!MatchPicaWaterSurface({0, 0, 0}, {10, 0, 10}, none), "no bodies");
    ++checks;
    // Two bodies at one height: the one covering most.
    expect(match({150, 877, -1700}, {900, 877, -1300}) == 1U, "largest coverage");
    // Flow: texture units per second from native frames.
    PicaWaterFlowTrack track;
    AdvancePicaWaterFlow(track, 100, 30.0F, {0.0F, 0.0F});
    expect(!track.Valid(), "one frame has no velocity");
    AdvancePicaWaterFlow(track, 100, 30.0F, {0.5F, 0.5F});
    expect(!track.Valid() && track.Translation[0] == 0.0F, "a repeated native frame adds nothing");
    AdvancePicaWaterFlow(track, 101, 30.0F, {0.01F, -0.02F});
    expect(track.Valid() && std::abs(track.Velocity[0] - 0.3F) < 1e-4F && std::abs(track.Velocity[1] + 0.6F) < 1e-4F,
           "velocity per second");
    // Wrapping at a texture repeat (0.99 -> 0.01) is a small step forward.
    PicaWaterFlowTrack wrap;
    AdvancePicaWaterFlow(wrap, 1, 30.0F, {0.99F, 0.0F});
    AdvancePicaWaterFlow(wrap, 2, 30.0F, {0.01F, 0.0F});
    expect(std::abs(wrap.Velocity[0] - 0.6F) < 1e-3F, "wrapped translation");
    // A long gap (a warp, a pause) restarts the track.
    AdvancePicaWaterFlow(wrap, 20, 30.0F, {0.5F, 0.0F});
    expect(!wrap.Valid(), "gap restarts");
    // Smoothing: a step that holds for a frame halves nothing at once.
    PicaWaterFlowTrack step;
    AdvancePicaWaterFlow(step, 1, 30.0F, {0.0F, 0.0F});
    AdvancePicaWaterFlow(step, 2, 30.0F, {0.02F, 0.0F});
    AdvancePicaWaterFlow(step, 3, 30.0F, {0.02F, 0.0F});
    expect(std::abs(step.Velocity[0] - 0.42F) < 1e-4F, "smoothed velocity");
    checks += 6;
    // Decorative water without a body: a flat draw is a plane at its middle
    // height (the castle moat); a tall one (a waterfall) is not.
    const auto moat = PicaWaterSurfaceFromBounds({-1946, 1508, -964}, {800, 1511, 669});
    expect(moat && moat->SurfaceY == 1509.5F && moat->MinimumXZ[1] == -964.0F && moat->MaximumXZ[0] == 800.0F,
           "flat water plane");
    expect(!PicaWaterSurfaceFromBounds({362, -15, -568}, {409, 274, -496}), "waterfall is no plane");
    // The water texture catalog: hex hash, size and roles; malformed is none.
    const auto catalog = PicaWaterTextureCatalog::Parse(
        "triaevum-water-textures 1\r\n7304e8d17265fb40 64 64 3\n0110f86224e7ac39 128 128 4\n\n");
    expect(catalog && catalog->Size() == 2U, "catalog entries");
    expect(catalog->Roles(0x7304e8d17265fb40ULL, 64, 64) == (kPicaWaterTextureSurface | kPicaWaterTextureRiver),
           "catalog roles");
    expect(catalog->Roles(0x0110f86224e7ac39ULL, 128, 128) == kPicaWaterTextureWaterfall, "second entry");
    expect(catalog->Roles(0x7304e8d17265fb40ULL, 64, 32) == 0U, "size is part of the identity");
    expect(!PicaWaterTextureCatalog::Parse("triaevum-water-textures 2\n"), "unknown version");
    expect(!PicaWaterTextureCatalog::Parse("triaevum-water-textures 1\n7304e8d17265fb4g 64 64 1\n"), "bad hash");
    expect(!PicaWaterTextureCatalog::Parse("triaevum-water-textures 1\n7304e8d17265fb40 64 64\n"), "missing role");
    expect(!PicaWaterTextureCatalog::Parse("triaevum-water-textures 1\n7304e8d17265fb40 64 64 1 9\n"), "extra field");
    checks += 10;
    // Ripples: a mover whose cylinder crosses the main pool's surface (y 0).
    PicaWaterRippleField field;
    const auto live = [&](float strength) {
        size_t count = 0;
        for (const auto& ring : field.Rings()) count += ring.Strength >= strength ? 1U : 0U;
        return count;
    };
    PicaWaterMover link{7, {0.0F, 60.0F, -500.0F}, 15.0F, 50.0F};
    field.Advance(1.0F, &link, 1, bodies);
    expect(live(0.01F) == 0U, "above the water: no ring");
    link.Base[1] = -30.0F;
    field.Advance(1.1F, &link, 1, bodies);
    expect(live(1.0F) == 1U, "entering: a splash");
    link.Base[0] = 4.0F;
    field.Advance(1.2F, &link, 1, bodies);
    expect(live(0.01F) == 1U, "less than the spacing: nothing more");
    link.Base[0] = 20.0F;
    field.Advance(1.3F, &link, 1, bodies);
    expect(live(0.01F) == 2U, "travelling: a ring");
    field.Advance(2.5F, &link, 1, bodies);
    expect(live(0.01F) == 3U && live(0.25F) == 2U, "at rest: a faint ring");
    link.Base[1] = -200.0F;
    field.Advance(2.6F, &link, 1, bodies);
    expect(live(0.01F) == 3U, "under the water: none");
    field.Advance(4.45F, &link, 1, bodies);
    expect(live(0.01F) == 1U, "rings die after their lifetime");
    field.Advance(1.0F, &link, 1, bodies);
    expect(live(0.01F) == 0U, "time going back restarts");
    PicaWaterMover outside{8, {5000.0F, -10.0F, 0.0F}, 15.0F, 50.0F};
    expect(!PicaWaterRippleField::CrossesSurface(outside, bodies), "outside every footprint");
    checks += 9;
    std::cout << "pica_scene_water_tests: " << checks << " checks passed\n";
    return 0;
}
