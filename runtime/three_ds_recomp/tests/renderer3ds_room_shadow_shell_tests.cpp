#include "fast/renderer3ds/room_shadow_shell.h"
#include "fast/renderer3ds/pica_room_shadow_selection.h"
#include <gtest/gtest.h>
#include <bit>
#include <chrono>
#include <fstream>
#include <cmath>
#ifdef ROOM_SHADOW_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

using namespace Fast::Renderer3ds;
namespace {
void Roof(std::vector<RoomShadowTriangle>& tris, float y, bool cull = true, bool alpha = false) {
    tris.push_back({ { { { -100, y, -100 }, { 100, y, -100 }, { 100, y, 100 } } }, cull, alpha });
    tris.push_back({ { { { -100, y, -100 }, { 100, y, 100 }, { -100, y, 100 } } }, cull, alpha });
}
void Quad(std::vector<RoomShadowTriangle>& tris, ShadowVector a, ShadowVector b, ShadowVector c, ShadowVector d) {
    tris.push_back({ { a, b, c } });
    tris.push_back({ { a, c, d } });
}
// Inward winding: ceiling, floor, left, right, back, front (two triangles each).
void Box(std::vector<RoomShadowTriangle>& tris, float half = 100, float bottom = -100, float top = 100,
         bool openFront = false) {
    Quad(tris, { -half, top, -half }, { half, top, -half }, { half, top, half }, { -half, top, half });
    Quad(tris, { -half, bottom, -half }, { -half, bottom, half }, { half, bottom, half }, { half, bottom, -half });
    Quad(tris, { -half, bottom, -half }, { -half, top, -half }, { -half, top, half }, { -half, bottom, half });
    Quad(tris, { half, bottom, -half }, { half, bottom, half }, { half, top, half }, { half, top, -half });
    Quad(tris, { -half, bottom, half }, { -half, top, half }, { half, top, half }, { half, bottom, half });
    if (!openFront)
        Quad(tris, { -half, bottom, -half }, { half, bottom, -half }, { half, top, -half }, { -half, top, -half });
}
void EncloseBelow(std::vector<RoomShadowTriangle>& tris, float top) {
    std::vector<RoomShadowTriangle> box;
    Box(box, 100, -100, top);
    tris.insert(tris.end(), box.begin() + 2, box.end());
}
bool Shell(const RoomShadowShell& m, size_t triangle, uint32_t sector) {
    return (m.ShellMasks[triangle] & (uint64_t{ 1 } << sector)) != 0;
}
} // namespace
TEST(RoomShadowShell, ClassifiesNativeOpaqueReplacementAndRejectedCasterReasons) {
    using Class = PicaRoomShadowCasterClass;
    PicaResolvedDrawRecord room;
    room.CompositionDomain = PicaCompositionDomain::Scene;
    room.Composition = { PicaCompositionLayer::OpaqueWorld, PicaCompositionProvenance::NativeCmbDrawPass };
    // Deku Tree's captured PICA 0x100=0x00e40100, 0x101=0x01010000:
    // enabled blend unit, ADD/ADD, ONE/ZERO for both RGB and alpha.
    room.Material.Blend.Enabled = true;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(room), Class::Room);
    auto draw = room;
    draw.Material.Blend.Enabled = false;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::Room);
    draw = room;
    draw.CompositionDomain = PicaCompositionDomain::Unknown;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::UnknownDomain);
    draw.CompositionDomain = PicaCompositionDomain::Ui;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::OtherDomain);
    draw = room;
    draw.Composition.NativeActorGeometry = true;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::Actor);
    for (const auto layer :
         { PicaCompositionLayer::Unknown, PicaCompositionLayer::Atmosphere, PicaCompositionLayer::TransparentWorld }) {
        draw = room;
        draw.Composition.Layer = layer;
        EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::OtherPass);
    }
    draw = room;
    draw.Composition.Provenance = PicaCompositionProvenance::Unknown;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::OtherPass);
    draw = room;
    draw.Material.AlphaTestEnabled = true;
    for (const auto compare : { PicaCompareFunction::Always, PicaCompareFunction::Greater }) {
        draw.Material.AlphaCompare = compare;
        EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::AlphaTest);
    }
    draw = room;
    draw.Material.Blend.DestRgb = NativeBlendFactor::One;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::Translucent);
    draw = room;
    draw.Material.Blend.SourceRgb = NativeBlendFactor::SourceAlpha;
    draw.Material.Blend.DestRgb = NativeBlendFactor::OneMinusSourceAlpha;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::Translucent);
    draw = room;
    draw.Material.Blend.EquationAlpha = NativeBlendEquation::Subtract;
    EXPECT_EQ(ClassifyPicaRoomShadowCaster(draw), Class::Translucent);
}

TEST(RoomShadowShell, SelectsPublishedSnapshotIndependentlyOfRoomCasterRejections) {
    std::vector<RoomShadowTriangle> triangles;
    Roof(triangles, 0);
    auto mesh = BuildRoomShadowShell(5, triangles, 32);
    ASSERT_TRUE(mesh);
    auto scene = std::make_shared<RoomShadowShellScene>();
    scene->ScopeIdentity = 123;
    scene->Rooms.push_back({ 5, std::make_shared<const RoomShadowShell>(*mesh) });
    PicaResolvedDrawRecord room;
    room.CompositionDomain = PicaCompositionDomain::Scene;
    room.Composition = { PicaCompositionLayer::OpaqueWorld, PicaCompositionProvenance::NativeCmbDrawPass };
    room.Material.Blend.Enabled = true;
    room.RoomShadows = scene;
    auto actor = room;
    actor.Composition.NativeActorGeometry = true;
    auto alpha = room;
    alpha.Material.AlphaTestEnabled = true;
    const std::array<const PicaResolvedDrawRecord*, 3> draws{ &actor, &alpha, &room };
    auto selected = SelectPicaRoomShadowScene(draws);
    EXPECT_EQ(selected.ReplacementScene, scene.get());
    EXPECT_STREQ(selected.Status, "ready");
    EXPECT_EQ(selected.Count(PicaRoomShadowCasterClass::Room), 1U);
    EXPECT_EQ(selected.Count(PicaRoomShadowCasterClass::Actor), 1U);
    EXPECT_EQ(selected.Count(PicaRoomShadowCasterClass::AlphaTest), 1U);
    EXPECT_EQ(selected.Snapshots, 3U);
    EXPECT_EQ(selected.ReadySnapshots, 3U);
    selected = SelectPicaRoomShadowScene(std::span(draws).first(2));
    EXPECT_EQ(selected.ReplacementScene, nullptr);
    EXPECT_EQ(selected.PublishedScene, scene.get());
    EXPECT_STREQ(selected.Status, "no_room_casters");
    // An actor's ready snapshot cannot authorize removal of an unbound room.
    room.RoomShadows.reset();
    selected = SelectPicaRoomShadowScene(draws);
    EXPECT_EQ(selected.ReplacementScene, nullptr);
    EXPECT_EQ(selected.PublishedScene, scene.get());
    EXPECT_STREQ(selected.Status, "no_snapshot");
    auto pending = std::make_shared<RoomShadowShellScene>(*scene);
    pending->Rooms.push_back({ 6, {} });
    room.RoomShadows = pending;
    selected = SelectPicaRoomShadowScene(draws);
    EXPECT_EQ(selected.ReplacementScene, nullptr);
    EXPECT_STREQ(selected.Status, "pending_or_failed");
    room.RoomShadows = scene;
    auto otherRoom = room;
    otherRoom.RoomShadows = std::make_shared<RoomShadowShellScene>(*scene);
    const std::array<const PicaResolvedDrawRecord*, 2> mixed{ &room, &otherRoom };
    selected = SelectPicaRoomShadowScene(mixed);
    EXPECT_EQ(selected.ReplacementScene, nullptr);
    EXPECT_STREQ(selected.Status, "mixed_snapshots");
    // Disabling shells must not recover a stale snapshot from the last frame.
    room.RoomShadows.reset();
    otherRoom.RoomShadows.reset();
    selected = SelectPicaRoomShadowScene(mixed);
    EXPECT_EQ(selected.PublishedScene, nullptr);
    EXPECT_EQ(selected.Snapshots, 0U);
    EXPECT_STREQ(selected.Status, "no_snapshot");
}

TEST(RoomShadowShell, InteriorSunRoofPlatformAndInnerRoofStack) {
    std::vector<RoomShadowTriangle> triangles;
    Box(triangles, 200, -200, 200);
    Roof(triangles, 80);
    Roof(triangles, -20);
    RoomInteriorSunStats stats;
    const auto mesh = BuildRoomShadowShell(123, triangles, 512, {}, true, &stats);
    ASSERT_TRUE(mesh);
    EXPECT_GT(stats.InteriorVoxels, 0U);
    EXPECT_GE(stats.ClosingSteps, 4U);
    EXPECT_LE(stats.Voxels, kMaximumRoomInteriorVoxels);
    const auto sector = RoomShadowSector({ 0, 1, 0 });
    EXPECT_TRUE(Shell(*mesh, 0, sector));
    EXPECT_TRUE(Shell(*mesh, 1, sector));
    for (size_t i : { 2U, 3U, 12U, 13U, 14U, 15U })
        EXPECT_FALSE(Shell(*mesh, i, sector));
    // Both-face source materials and reversed winding retain the same criterion.
    for (auto& t : triangles) {
        t.CullBack = false;
        std::swap(t.Positions[1], t.Positions[2]);
    }
    const auto reversed = BuildRoomShadowShell(123, triangles);
    ASSERT_TRUE(reversed);
    EXPECT_EQ(reversed->ShellMasks, mesh->ShellMasks);
    const auto repeated = BuildRoomShadowShell(123, triangles);
    ASSERT_TRUE(repeated);
    EXPECT_EQ(EncodeRoomShadowShell(*reversed), EncodeRoomShadowShell(*repeated));
    // Masks computed earlier for the same content reproduce the built mesh
    // exactly; a different triangle count or outdoor masks are refused.
    const auto assembled = AssembleRoomShadowShell(123, triangles, repeated->ShellMasks);
    ASSERT_TRUE(assembled);
    EXPECT_EQ(EncodeRoomShadowShell(*assembled), EncodeRoomShadowShell(*repeated));
    EXPECT_EQ(assembled->ContentVersion, repeated->ContentVersion);
    auto shorter = repeated->ShellMasks;
    shorter.pop_back();
    EXPECT_FALSE(AssembleRoomShadowShell(123, triangles, shorter));
    EXPECT_FALSE(AssembleRoomShadowShell(123, triangles, repeated->ShellMasks, 512, false));
    const std::vector<uint64_t> none(repeated->ShellMasks.size());
    EXPECT_TRUE(AssembleRoomShadowShell(123, triangles, none, 512, false));
}
TEST(RoomShadowShell, InteriorSunDoesNotRestartAfterLeavingAndReentering) {
    std::vector<RoomShadowTriangle> triangles;
    // The 800-unit gap exceeds even the maximum closing diameter (480).
    Box(triangles, 300, -600, -400);
    Box(triangles, 300, 400, 600);
    const auto mesh = BuildRoomShadowShell(1, triangles);
    ASSERT_TRUE(mesh);
    const auto sector = RoomShadowSector({ 0, 1, 0 });
    // A source ray enters the upper box, exits into the gap and reenters the
    // lower one. Only the upper outer roof is before its first admission.
    EXPECT_TRUE(Shell(*mesh, 12, sector));
    EXPECT_TRUE(Shell(*mesh, 13, sector));
    EXPECT_FALSE(Shell(*mesh, 0, sector));
    EXPECT_FALSE(Shell(*mesh, 1, sector));
    EXPECT_FALSE(Shell(*mesh, 2, sector));
}
TEST(RoomShadowShell, AuthoredOutdoorCourtyardKeepsBothFacesWithoutVoxelWork) {
    std::vector<RoomShadowTriangle> triangles;
    Box(triangles);
    triangles.erase(triangles.begin(), triangles.begin() + 2); // open-sky courtyard
    RoomInteriorSunStats stats;
    const auto mesh = BuildRoomShadowShell(1, triangles, 512, {}, false, &stats);
    ASSERT_TRUE(mesh);
    EXPECT_FALSE(mesh->Interior);
    EXPECT_EQ(stats.Voxels, 0U);
    for (auto mask : mesh->ShellMasks)
        EXPECT_EQ(mask, 0U);
    EXPECT_TRUE(DecodeRoomShadowShell(EncodeRoomShadowShell(*mesh), 1, false));
    EXPECT_FALSE(DecodeRoomShadowShell(EncodeRoomShadowShell(*mesh), 1, true));
    EXPECT_NE(RoomShadowShellPath("cache", 1, false), RoomShadowShellPath("cache", 1, true));
    RoomShadowShellScene scene{ 1, { { 1, mesh, false } } };
    EXPECT_TRUE(scene.Ready());
    scene.Rooms[0].Interior = true;
    EXPECT_FALSE(scene.Ready());
}
TEST(RoomShadowShell, InteriorSunAlphaMalformedBoundsAndCancellation) {
    std::vector<RoomShadowTriangle> triangles;
    Roof(triangles, 300, true, true);
    Box(triangles, 200, -200, 200);
    auto mesh = BuildRoomShadowShell(1, triangles);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->ShellMasks.size(), 12U);
    EXPECT_TRUE(Shell(*mesh, 0, RoomShadowSector({ 0, 1, 0 })));
    triangles[1].Positions[0][0] = std::numeric_limits<float>::quiet_NaN();
    triangles[1].AlphaTest = false;
    EXPECT_FALSE(BuildRoomShadowShell(1, triangles));
    std::stop_source stop;
    stop.request_stop();
    EXPECT_FALSE(BuildRoomShadowShell(1, triangles, 512, stop.get_token()));
    triangles.clear();
    Box(triangles, 10000, -10000, 10000);
    EXPECT_FALSE(BuildRoomShadowShell(1, triangles)); // bounded, native fallback
    triangles.clear();
    Roof(triangles, 0);
    mesh = BuildRoomShadowShell(1, triangles);
    ASSERT_TRUE(mesh);
    for (auto mask : mesh->ShellMasks)
        EXPECT_EQ(mask, 0U); // no interior => conservative
}
TEST(RoomShadowShell, SectorsWeldingFormatCorruptionAndPersistence) {
    const auto& directions = RoomShadowDirections();
    for (uint32_t i = 0; i < 64; ++i) {
        EXPECT_GT(directions[i][1], 0);
        EXPECT_EQ(RoomShadowSector(directions[i]), i);
    }
    std::vector<RoomShadowTriangle> triangles;
    Roof(triangles, 0);
    auto mesh = BuildRoomShadowShell(0xabcdef, triangles);
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->Positions.size(), 4U);
    EXPECT_EQ(mesh->Indices.size(), 6U);
    const auto bytes = EncodeRoomShadowShell(*mesh);
    const auto decoded = DecodeRoomShadowShell(bytes, mesh->Fingerprint);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->ShellMasks, mesh->ShellMasks);
    EXPECT_EQ(decoded->ContentVersion, mesh->ContentVersion);
    EXPECT_FALSE(DecodeRoomShadowShell(bytes, 42));
    // Version 2 is never reused even with a correct checksum.
    auto legacy = bytes;
    legacy[4] = 2;
    uint64_t legacyChecksum = 1469598103934665603ULL;
    for (size_t i = 0; i < legacy.size() - 8; ++i)
        legacyChecksum = (legacyChecksum ^ legacy[i]) * 1099511628211ULL;
    for (size_t i = 0; i < 8; ++i)
        legacy[legacy.size() - 8 + i] = uint8_t(legacyChecksum >> (i * 8));
    EXPECT_FALSE(DecodeRoomShadowShell(legacy, mesh->Fingerprint));
    for (size_t at : { size_t(0), size_t(4), size_t(24), size_t(36), bytes.size() - 1 }) {
        auto bad = bytes;
        bad[at] ^= 1;
        EXPECT_FALSE(DecodeRoomShadowShell(bad, mesh->Fingerprint));
    }
    EXPECT_FALSE(DecodeRoomShadowShell(std::span(bytes).first(bytes.size() - 1), mesh->Fingerprint));
    auto bad = bytes;
    bad.push_back(0);
    EXPECT_FALSE(DecodeRoomShadowShell(bad, mesh->Fingerprint));
    const auto gpu = RoomShadowShellVertices(*mesh);
    ASSERT_EQ(gpu.size(), 6U);
    EXPECT_EQ(gpu[0].Mask, gpu[1].Mask);
    EXPECT_EQ(gpu[1].Mask, gpu[2].Mask);
    EXPECT_EQ(uint64_t(gpu[0].Mask[0]) | (uint64_t(gpu[0].Mask[1]) << 32), mesh->ShellMasks[0]);
    auto path = std::filesystem::temp_directory_path() /
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ASSERT_TRUE(SaveRoomShadowShell(path, *mesh));
    EXPECT_TRUE(LoadRoomShadowShell(path, mesh->Fingerprint));
    {
        std::ofstream file(RoomShadowShellPath(path, mesh->Fingerprint), std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(legacy.data()), std::streamsize(legacy.size()));
        ASSERT_TRUE(file);
    }
    EXPECT_FALSE(LoadRoomShadowShell(path, mesh->Fingerprint));
    ASSERT_TRUE(SaveRoomShadowShell(path, *mesh));
    EXPECT_TRUE(LoadRoomShadowShell(path, mesh->Fingerprint));
    std::filesystem::remove(RoomShadowShellPath(path, mesh->Fingerprint));
    std::filesystem::remove(path / "shadow-shells");
    std::filesystem::remove(path);
}
TEST(RoomShadowShell, PendingRoomPreventsPartialReplacement) {
    std::vector<RoomShadowTriangle> t;
    Roof(t, 0);
    auto mesh = BuildRoomShadowShell(5, t, 32);
    RoomShadowShellScene scene;
    EXPECT_FALSE(scene.Ready());
    scene.Rooms = { { 5, mesh }, { 6, {} } };
    EXPECT_FALSE(scene.Ready());
    scene.Rooms.pop_back();
    EXPECT_TRUE(scene.Ready());
    scene.Rooms[0].Fingerprint = 7;
    EXPECT_FALSE(scene.Ready());
}
TEST(RoomShadowShell, CacheWriteFailureReportsTheFilesystemError) {
    std::vector<RoomShadowTriangle> triangles;
    Roof(triangles, 0);
    auto mesh = BuildRoomShadowShell(5, triangles, 32);
    ASSERT_TRUE(mesh);
    const auto blocked =
        std::filesystem::temp_directory_path() /
        ("shell-cache-file-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream file(blocked);
        file << "file, not cache directory";
    }
    std::string error;
    EXPECT_FALSE(SaveRoomShadowShell(blocked, *mesh, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(DecodeRoomShadowShell(EncodeRoomShadowShell(*mesh), 5));
    std::filesystem::remove(blocked);
}
TEST(RoomShadowShell, StaticPagesReuseUntilSectorOrRasterStateChanges) {
    std::vector<RoomShadowTriangle> triangles;
    Roof(triangles, 0);
    const auto mesh = BuildRoomShadowShell(5, triangles, 32);
    ASSERT_TRUE(mesh);
    const auto decoded = DecodeRoomShadowShell(EncodeRoomShadowShell(*mesh), 5);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->RasterSize, 32U);
    const auto layout = BuildVirtualShadowLayout({ 0, 0, 0 }, { 0, 1, 0 }, 2048, 500);
    VirtualShadowMapCache cache;
    std::array casters{ RoomShadowCaster(*mesh, 63) };
    EXPECT_FALSE(casters[0].Dynamic);
    cache.Prepare(layout, casters, 1);
    EXPECT_GT(cache.Statistics().StaticUpdated, 0U);
    cache.Commit();
    casters[0] = RoomShadowCaster(*decoded, 63);
    cache.Prepare(layout, casters, 2);
    EXPECT_EQ(cache.Statistics().StaticUpdated, 0U);
    EXPECT_GT(cache.Statistics().StaticReused, 0U);
    cache.Commit();
    casters[0] = RoomShadowCaster(*decoded, 62);
    cache.Prepare(layout, casters, 3);
    EXPECT_GT(cache.Statistics().StaticUpdated, 0U);
    cache.Commit();
    casters[0] = RoomShadowCaster(*decoded, 62, 1);
    cache.Prepare(layout, casters, 4);
    EXPECT_GT(cache.Statistics().StaticUpdated, 0U);
}
#ifdef ROOM_SHADOW_HAS_SHADERC
TEST(RoomShadowShell, VertexShaderCompilesForVulkan) {
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(std::string(RoomShadowShellVertexShader()), shaderc_vertex_shader,
                                                  "room_shadow_shell.vert");
    EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success) << result.GetErrorMessage();
}
#endif
