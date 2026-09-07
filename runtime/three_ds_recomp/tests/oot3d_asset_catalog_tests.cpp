#include "three_ds_recomp/oot3d/Oot3dAssetCatalog.h"
#include "three_ds_recomp/oot3d/Oot3dSceneProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRoomRenderProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSceneEnvironmentProvider.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderCommand.h"
#include "three_ds_recomp/oot3d/Oot3dNativeActorRenderProvider.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cstring>

namespace {

nlohmann::json CatalogWithRecords(nlohmann::json records) {
    return {{"format", "oot3d_asset_catalog_v1"}, {"status", "complete"}, {"records", std::move(records)}};
}

nlohmann::json Record(std::string id, std::string family, nlohmann::json dependencies = nlohmann::json::array()) {
    return {{"asset_id", id},
            {"family", family},
            {"source_identity", id},
            {"canonical_resources", nlohmann::json::array({"objects/test/resource"})},
            {"dependencies", std::move(dependencies)},
            {"required_engine_capabilities", {"native_pica_material"}},
            {"support_tier", 2},
            {"runtime_state", "packaged_not_bound"}};
}

nlohmann::json SceneRecord(std::string id, std::string family, int sceneId, int roomIndex = -1) {
    auto record = Record(std::move(id), std::move(family));
    record["ownership"] = {{"scene_id", sceneId}, {"scene_stem", "test"},
                           {"room_index", roomIndex}, {"setup_indices", {0, 1}},
                           {"scene_shard_manifest_resource", "oot3d/catalog/shards/test.json"}};
    return record;
}

TEST(Oot3dAssetCatalog, BuildsStableLookupIndices) {
    const auto modelId = "cmb:actor/test.zar!Model/test.cmb";
    const auto animationId = "csab:actor/test.zar!Anim/idle.csab";
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(CatalogWithRecords(nlohmann::json::array(
        {Record(modelId, "actor_model"), Record(animationId, "skeletal_animation", {modelId})})));

    ASSERT_NE(catalog.Find(modelId), nullptr);
    EXPECT_EQ(catalog.Find(modelId)->Family, "actor_model");
    ASSERT_EQ(catalog.FindFamily("skeletal_animation").size(), 1);
    EXPECT_EQ(catalog.FindFamily("skeletal_animation")[0]->AssetId, animationId);
    ASSERT_EQ(catalog.FindDependents(modelId).size(), 1);
    EXPECT_EQ(catalog.FindDependents(modelId)[0]->AssetId, animationId);
    EXPECT_TRUE(catalog.FindFamily("missing").empty());
    EXPECT_TRUE(catalog.MissingCapabilities(*catalog.Find(modelId), {"native_pica_material"}).empty());
    EXPECT_EQ(catalog.MissingCapabilities(*catalog.Find(modelId), {}),
              std::vector<std::string>({"native_pica_material"}));
    EXPECT_TRUE(catalog.MissingEngineCapabilities(*catalog.Find(modelId)).empty());
}

TEST(Oot3dAssetCatalog, PreservesNativeActorContainerAndModelKind) {
    auto model = Record("cmb:actor/link_child.zar!Model/link_child.cmb", "actor_model");
    model["metadata"] = {{"model_kind", "skinned"}};
    auto animation = Record("csab:actor/link_child.zar!Anim/wait.csab",
                            "skeletal_animation", {model["asset_id"]});
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(
        CatalogWithRecords(nlohmann::json::array({model, animation})));

    const auto* record = catalog.Find("cmb:actor/link_child.zar!Model/link_child.cmb");
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->SourceContainer, "actor/link_child.zar");
    EXPECT_EQ(record->SourceMember, "Model/link_child.cmb");
    EXPECT_EQ(record->ModelKind, "skinned");
    ASSERT_EQ(catalog.FindSourceContainer("actor/link_child.zar").size(), 2);
    EXPECT_TRUE(catalog.FindSourceContainer("actor/missing.zar").empty());
}

TEST(Oot3dAssetCatalog, AcceptsNullModelKindOnNonActorRecords) {
    auto texture = Record("ctxb:menu/icon.ctxb", "texture");
    texture["metadata"] = {{"model_kind", nullptr}};
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(
        CatalogWithRecords(nlohmann::json::array({texture})));
    ASSERT_NE(catalog.Find("ctxb:menu/icon.ctxb"), nullptr);
    EXPECT_TRUE(catalog.Find("ctxb:menu/icon.ctxb")->ModelKind.empty());
}

TEST(Oot3dAssetCatalog, AcceptsNullOptionalSceneShardResource) {
    auto room = SceneRecord("room:test_0_info.zsi", "scene_room_source", -1, 0);
    room["ownership"]["scene_shard_manifest_resource"] = nullptr;
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(
        CatalogWithRecords(nlohmann::json::array({room})));
    ASSERT_NE(catalog.Find("room:test_0_info.zsi"), nullptr);
    EXPECT_TRUE(catalog.Find("room:test_0_info.zsi")->SceneShardManifestResource.empty());
}

TEST(Oot3dAssetCatalog, RejectsDuplicateIds) {
    auto document = CatalogWithRecords(nlohmann::json::array(
        {Record("ctxb:menu/icon.ctxb", "texture"), Record("ctxb:menu/icon.ctxb", "texture")}));
    EXPECT_THROW(ThreeDsRecomp::Oot3d::AssetCatalog::Parse(document), std::runtime_error);
}

TEST(Oot3dAssetCatalog, RejectsUnresolvedDependencies) {
    auto document = CatalogWithRecords(
        nlohmann::json::array({Record("csab:test", "skeletal_animation", {"cmb:missing"})}));
    EXPECT_THROW(ThreeDsRecomp::Oot3d::AssetCatalog::Parse(document), std::runtime_error);
}

TEST(Oot3dAssetCatalog, IndexesNativeSceneAndRoomOwnership) {
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(CatalogWithRecords(nlohmann::json::array(
        {SceneRecord("scene:test_info.zsi", "scene_profile", 0x55),
         SceneRecord("room:test_0_info.zsi", "scene_room_source", 0x55, 0)})));
    ASSERT_NE(catalog.FindScene(0x55), nullptr);
    EXPECT_EQ(catalog.FindScene(0x55)->SceneStem, "test");
    ASSERT_EQ(catalog.FindRooms(0x55, 0).size(), 1);
    EXPECT_EQ(catalog.FindRooms(0x55, 0)[0]->SetupIndices, std::vector<int32_t>({0, 1}));
    EXPECT_EQ(catalog.FindSceneFamily(0x55, "scene_room_source").size(), 1);
    ThreeDsRecomp::Oot3d::SceneProvider provider(catalog);
    const auto selection = provider.Resolve(0x55, 1, 0);
    EXPECT_TRUE(selection.SetupAvailable);
    EXPECT_TRUE(selection.Packaged);
    EXPECT_TRUE(selection.RenderableCandidate);
    EXPECT_FALSE(selection.TraversalCandidate);
    EXPECT_EQ(selection.Rooms.size(), 1);
}

TEST(Oot3dNativeSourceProvider, LoadsAndCachesArchiveBytes) {
    int loadCount = 0;
    ThreeDsRecomp::Oot3d::NativeSourceProvider provider([&loadCount](const std::string&) {
        ++loadCount;
        return std::make_shared<const std::vector<uint8_t>>(
            std::initializer_list<uint8_t>{'z', 's', 'i'});
    });
    const auto first = provider.Load("oot3d/native/scene/test.zsi");
    const auto second = provider.Load("oot3d/native/scene/test.zsi");
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(*first->Bytes, std::vector<uint8_t>({'z', 's', 'i'}));
    EXPECT_EQ(first, second);
    EXPECT_EQ(loadCount, 1);
}

TEST(Oot3dNativeActorRenderProvider, RejectsUnknownModelWithoutLoadingActorArchive) {
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(
        CatalogWithRecords(nlohmann::json::array()));
    int loadCount = 0;
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources([&loadCount](const std::string&) {
        ++loadCount;
        return std::shared_ptr<const std::vector<uint8_t>>();
    });
    ThreeDsRecomp::Oot3d::NativeActorRenderProvider actors(catalog, sources);
    const auto source = actors.Resolve("cmb:actor/missing.zar!Model/missing.cmb");
    EXPECT_FALSE(source->Ready());
    EXPECT_EQ(source->Status, "actor_model_not_cataloged");
    EXPECT_EQ(loadCount, 2); // Constructor probes the shard manifest and EnKo contract.
}

TEST(Oot3dNativeActorRenderProvider, MapsGameplayTimelineToNativeCsabInclusiveLastFrame) {
    ThreeDsRecomp::Oot3d::CsabMetadata animation;
    animation.FrameCount = 30;
    ThreeDsRecomp::Oot3d::NativeActorAnimationTimeInput time;
    time.SourceStartFrame = 10.0f;
    time.SourceEndFrame = 20.0f;
    time.SourceFrame = 15.0f;
    time.PlaybackMode = ThreeDsRecomp::Oot3d::NativeActorAnimationPlaybackMode::Clamp;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationFrame(animation, time), 15.0f);
    time.SourceFrame = 25.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationFrame(animation, time), 30.0f);
    time.PlaybackMode = ThreeDsRecomp::Oot3d::NativeActorAnimationPlaybackMode::Loop;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationFrame(animation, time), 15.0f);
}

TEST(Oot3dNativeActorRenderProvider, KeepsNativeClockAcrossShorterSourceLoop) {
    ThreeDsRecomp::Oot3d::CsabMetadata animation;
    animation.FrameCount = 19;
    ThreeDsRecomp::Oot3d::NativeActorAnimationTimeInput time;
    time.BindingKey = "run";
    time.SourceStartFrame = 0.0f;
    time.SourceEndFrame = 15.0f;
    time.SourceFrame = 14.0f;
    time.PlaybackMode = ThreeDsRecomp::Oot3d::NativeActorAnimationPlaybackMode::Loop;
    ThreeDsRecomp::Oot3d::NativeActorAnimationClockState clock;

    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 14.0f);
    time.SourceFrame = 15.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 15.0f);
    time.SourceFrame = 0.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 16.0f);
    time.SourceFrame = 3.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 19.0f);
    time.SourceFrame = 4.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 0.0f);
    EXPECT_EQ(clock.SourceLoopCount, 1u);
    EXPECT_EQ(clock.SourceRestartCount, 0u);
}

TEST(Oot3dNativeActorRenderProvider, ResetsNativeClockOnSemanticSourceRestart) {
    ThreeDsRecomp::Oot3d::CsabMetadata animation;
    animation.FrameCount = 88;
    ThreeDsRecomp::Oot3d::NativeActorAnimationTimeInput time;
    time.BindingKey = "idle";
    time.SourceStartFrame = 0.0f;
    time.SourceEndFrame = 88.0f;
    time.SourceFrame = 28.0f;
    ThreeDsRecomp::Oot3d::NativeActorAnimationClockState clock;

    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 28.0f);
    time.SourceFrame = 3.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 3.0f);
    EXPECT_EQ(clock.SourceRestartCount, 1u);
    EXPECT_EQ(clock.SourceLoopCount, 0u);
}

TEST(Oot3dNativeActorRenderProvider, StartsReversePlaybackAtNativeLastFrame) {
    ThreeDsRecomp::Oot3d::CsabMetadata animation;
    animation.FrameCount = 19;
    ThreeDsRecomp::Oot3d::NativeActorAnimationTimeInput time;
    time.BindingKey = "reverse";
    time.SourceStartFrame = 15.0f;
    time.SourceEndFrame = 0.0f;
    time.SourcePlaybackSpeed = -1.0f;
    time.SourceFrame = 15.0f;
    ThreeDsRecomp::Oot3d::NativeActorAnimationClockState clock;

    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 19.0f);
    time.SourceFrame = 14.0f;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 18.0f);
}

TEST(Oot3dNativeActorRenderProvider, AdvancesNativeClockOnceWhenSemanticSourceFrameIsHeld) {
    ThreeDsRecomp::Oot3d::CsabMetadata animation;
    animation.FrameCount = 19;
    ThreeDsRecomp::Oot3d::NativeActorAnimationTimeInput time;
    time.BindingKey = "run";
    time.SourceFrame = 0.0f;
    time.SourceStartFrame = 0.0f;
    time.SourceEndFrame = 15.0f;
    time.SourcePlaybackSpeed = 1.0f;
    time.SourceUpdateSerialValid = true;
    time.SourceUpdateSerial = 100;
    ThreeDsRecomp::Oot3d::NativeActorAnimationClockState clock;

    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 0.0f);
    time.SourceUpdateSerial = 101;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 1.0f);
    EXPECT_EQ(clock.Status, "advanced_from_playback_speed");
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationClock(animation, time, clock), 1.0f);
    EXPECT_EQ(clock.Status, "render_hold");
    EXPECT_EQ(clock.PlaybackSpeedAdvanceCount, 1u);
    EXPECT_EQ(clock.RenderHoldCount, 1u);
}

TEST(Oot3dNativeActorRenderProvider, AdvancesHeldMorphOncePerSemanticUpdate) {
    ThreeDsRecomp::Oot3d::NativeActorAnimationMorphTimeInput time;
    time.BindingKey = "run";
    time.SourceWeight = 1.0f;
    time.SourceRate = 0.25f;
    time.SourceUpdateSerialValid = true;
    time.SourceUpdateSerial = 20;
    ThreeDsRecomp::Oot3d::NativeActorAnimationMorphClockState clock;

    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationMorphClock(time, clock), 1.0f);
    time.SourceUpdateSerial = 21;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationMorphClock(time, clock), 0.75f);
    EXPECT_EQ(clock.Status, "advanced_from_source_rate");
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationMorphClock(time, clock), 0.75f);
    EXPECT_EQ(clock.Status, "render_hold");
    time.SourceUpdateSerial = 24;
    EXPECT_FLOAT_EQ(ThreeDsRecomp::Oot3d::AdvanceNativeActorAnimationMorphClock(time, clock), 0.0f);
    EXPECT_EQ(clock.SourceHoldAdvanceCount, 2u);
}

TEST(Oot3dNativeRoomRenderProvider, RejectsMissingRoomWithoutLoading) {
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(CatalogWithRecords(nlohmann::json::array()));
    int loadCount = 0;
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources([&loadCount](const std::string&) {
        ++loadCount;
        return std::shared_ptr<const std::vector<uint8_t>>();
    });
    ThreeDsRecomp::Oot3d::NativeRoomRenderProvider rooms(catalog, sources);
    const auto room = rooms.Resolve(0x55, 0, 0);
    EXPECT_FALSE(room->Ready());
    EXPECT_EQ(room->Status, "room_not_cataloged_for_setup");
    EXPECT_EQ(loadCount, 0);
}

TEST(Oot3dNativeActorRenderProvider, ResolvesTypedGameplayAnimationBinding) {
    const nlohmann::json profile = {
        {"format", "oot3d_character_runtime_profile_v1"},
        {"animation_lookup", {{"by_gameplay_state", {{"idle", 1}}}}},
        {"native_resources", {{"animations", {
            {{"csab_name", "actor/unused.csab"}},
            {{"csab_name", "actor/idle.csab"}, {"resource_path", "actor/idle.json"}},
        }}}},
    };

    const auto binding = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationBinding(
        profile, {{"by_gameplay_state", "idle"}});

    EXPECT_TRUE(binding.Available);
    EXPECT_EQ(binding.Status, "ready");
    EXPECT_EQ(binding.CsabName, "actor/idle.csab");
    EXPECT_EQ(binding.ResourcePath, "actor/idle.json");
    EXPECT_EQ(binding.LookupName, "by_gameplay_state");
    EXPECT_EQ(binding.LookupValue, "idle");
}

TEST(Oot3dNativeActorRenderProvider, RejectsAmbiguousGameplayAnimationBinding) {
    const nlohmann::json profile = {
        {"format", "oot3d_character_runtime_profile_v1"},
        {"animation_lookup", {{"by_gameplay_state", {{"idle", {0, 1}}}}}},
        {"native_resources", {{"animations", {
            {{"csab_name", "actor/idle_a.csab"}},
            {{"csab_name", "actor/idle_b.csab"}},
        }}}},
    };

    const auto binding = ThreeDsRecomp::Oot3d::ResolveNativeActorAnimationBinding(
        profile, {{"by_gameplay_state", "idle"}});

    EXPECT_FALSE(binding.Available);
    EXPECT_EQ(binding.Status, "actor_animation_binding_ambiguous");
}

TEST(Oot3dNativeActorRenderProvider, ResolvesNativePlayerResourceVisibilityFromModelState) {
    ThreeDsRecomp::Oot3d::CmbModel model;
    for (uint8_t visibilityId : {0, 3, 9, 24, 25, 26}) {
        model.Meshes.push_back({static_cast<uint32_t>(model.Meshes.size()), 0, 0, visibilityId});
    }
    const nlohmann::json profile = {
        {"format", "oot3d_player_model_resource_profile_v1"},
        {"resource_id_sentinel", 0xFFFFFFFFu},
        {"body_resource_ids_by_age", {{45, 45, 46, 47}, {24, 24, 25, 26}}},
        {"model_groups", {{{"index", 3}, {"model_types", {0, 8, 18, 20}}}}},
        {"model_types", {
            {{"index", 0}, {"selection_kind", "age"}, {"resource_ids_by_age", {13, 0}}},
            {{"index", 8}, {"selection_kind", "age"}, {"resource_ids_by_age", {20, 3}}},
            {{"index", 18}, {"selection_kind", "shield_then_age"},
             {"shield_variant_resource_ids_by_age", {{31, 14}, {31, 11}, {0, 9}, {2, 14}}}},
            {{"index", 20}, {"selection_kind", "age"},
             {"resource_ids_by_age", {0xFFFFFFFFu, 0xFFFFFFFFu}}},
        }},
    };
    ThreeDsRecomp::Oot3d::NativePlayerModelResourceState state;
    state.AgeIndex = 1;
    state.ModelGroup = 3;
    state.LeftHandType = 0;
    state.RightHandType = 8;
    state.SheathType = 18;
    state.Shield = 2;

    const auto visibility =
        ThreeDsRecomp::Oot3d::ResolveNativePlayerModelResourceVisibility(profile, model, state);
    EXPECT_TRUE(visibility.Available) << visibility.Status << ": " << visibility.Error;
    EXPECT_EQ(visibility.Status, "ready");
    EXPECT_EQ(visibility.WaistType, 20);
    EXPECT_EQ(visibility.ActiveResourceIds, std::vector<uint8_t>({0, 3, 9, 24, 25, 26}));
    for (uint8_t resourceId : visibility.ActiveResourceIds) {
        EXPECT_EQ(visibility.ResourceVisibility[resourceId], 1);
    }
}

TEST(Oot3dNativeActorRenderProvider, ResolvesEnKoModelAnimationAndVisibilityFromNativeContract) {
    const nlohmann::json contract = {
        {"format", "oot3d_enko_native_runtime_contract_v1"},
        {"status", "complete"},
        {"subtypes", {{
            {"index", 1},
            {"model_class_index", 1},
            {"model_asset_id", "cmb:actor/zelda_kw1.zar!Model/kokiripeople.cmb"},
            {"face_model_asset_id", "cmb:actor/zelda_fa.zar!Model/kokiripeople.cmb"},
            {"model_scale", 0.01},
            {"resource_visibility_clear_ids", {2, 3}},
            {"face_animation_selector", 1},
            {"tunic_color", {70, 190, 60, 255}},
            {"boots_color", {100, 30, 0, 255}},
        }}},
        {"draw_callback", {
            {"torso_limb_index", 9},
            {"head_limb_index", 10},
        }},
        {"animations", {{
            {"index", 20},
            {"playback_speed", 1.5},
            {"start_frame", 0.0},
            {"end_frame", -1.0},
            {"playback_mode", 0},
            {"morph_frames", 0.0},
            {"bindings_by_model_class", {{
                {"model_class_index", 1},
                {"source_container", "actor/zelda_kw1.zar"},
                {"csab_member", "Anim/km1_backcyu.csab"},
                {"animation_asset_id", "csab:actor/zelda_kw1.zar!Anim/km1_backcyu.csab"},
            }}},
        }}},
    };

    const auto binding =
        ThreeDsRecomp::Oot3d::ResolveNativeEnKoRuntimeBinding(contract, 1, 20);
    ASSERT_TRUE(binding.Available) << binding.Status << ": " << binding.Error;
    EXPECT_EQ(binding.ModelClassIndex, 1u);
    EXPECT_EQ(binding.ModelAssetId,
              "cmb:actor/zelda_kw1.zar!Model/kokiripeople.cmb");
    EXPECT_EQ(binding.FaceModelAssetId,
              "cmb:actor/zelda_fa.zar!Model/kokiripeople.cmb");
    EXPECT_EQ(binding.TorsoLimbIndex, 9u);
    EXPECT_EQ(binding.HeadLimbIndex, 10u);
    EXPECT_EQ(binding.AnimationMember, "Anim/km1_backcyu.csab");
    EXPECT_FLOAT_EQ(binding.PlaybackSpeed, 1.5f);
    EXPECT_FLOAT_EQ(binding.ModelScale, 0.01f);
    EXPECT_EQ(binding.ResourceVisibilityClearIds,
              std::vector<uint8_t>({2, 3}));

    ThreeDsRecomp::Oot3d::CmbModel model;
    for (uint8_t resourceId : {0, 1, 2, 3, 4}) {
        model.Meshes.push_back({ static_cast<uint32_t>(model.Meshes.size()),
                                0, 0, resourceId });
    }
    const auto visibility =
        ThreeDsRecomp::Oot3d::ResolveNativeEnKoResourceVisibility(binding, model);
    ASSERT_TRUE(visibility.Available) << visibility.Status << ": " << visibility.Error;
    EXPECT_EQ(visibility.ActiveResourceIds,
              std::vector<uint8_t>({0, 1, 4}));
}

TEST(Oot3dNativeActorRenderProvider, AppliesNativeLimbRotationAndRecomposesDescendants) {
    ThreeDsRecomp::Oot3d::CmbSkeleton skeleton;
    skeleton.Bones.resize(2);
    skeleton.Bones[0].Index = 0;
    skeleton.Bones[0].ParentIndex = -1;
    skeleton.Bones[1].Index = 1;
    skeleton.Bones[1].ParentIndex = 0;

    ThreeDsRecomp::Oot3d::Matrix4f identity{};
    for (size_t index = 0; index < 4; ++index) {
        identity.M[index][index] = 1.0f;
    }
    ThreeDsRecomp::Oot3d::Matrix4f child = identity;
    child.M[1][3] = 1.0f;
    ThreeDsRecomp::Oot3d::CsabPose pose;
    pose.Valid = true;
    pose.LocalTransforms = { identity, child };
    pose.WorldTransforms = pose.LocalTransforms;

    ASSERT_TRUE(ThreeDsRecomp::Oot3d::ApplyNativeActorPoseRotationOverrides(
        skeleton, pose, {{0, 0, 0x4000}}));
    ASSERT_EQ(pose.WorldTransforms.size(), 2u);
    EXPECT_NEAR(pose.WorldTransforms[1].M[1][3], 0.0f, 0.0001f);
    EXPECT_NEAR(pose.WorldTransforms[1].M[2][3], 1.0f, 0.0001f);
}

TEST(Oot3dNativeActorRenderProvider, AppliesNativeLimbMatrixAndRecomposesDescendants) {
    ThreeDsRecomp::Oot3d::CmbSkeleton skeleton;
    skeleton.Bones.resize(2);
    skeleton.Bones[0].Index = 0;
    skeleton.Bones[0].ParentIndex = -1;
    skeleton.Bones[1].Index = 1;
    skeleton.Bones[1].ParentIndex = 0;

    ThreeDsRecomp::Oot3d::Matrix4f identity{};
    for (size_t index = 0; index < 4; ++index) {
        identity.M[index][index] = 1.0f;
    }
    ThreeDsRecomp::Oot3d::Matrix4f child = identity;
    child.M[1][3] = 1.0f;
    ThreeDsRecomp::Oot3d::Matrix4f rotateX = identity;
    rotateX.M[1][1] = 0.0f;
    rotateX.M[1][2] = -1.0f;
    rotateX.M[2][1] = 1.0f;
    rotateX.M[2][2] = 0.0f;
    ThreeDsRecomp::Oot3d::CsabPose pose;
    pose.Valid = true;
    pose.LocalTransforms = { identity, child };
    pose.WorldTransforms = pose.LocalTransforms;

    ASSERT_TRUE(ThreeDsRecomp::Oot3d::ApplyNativeActorPoseMatrixOverrides(
        skeleton, pose, {{0, rotateX}}));
    ASSERT_EQ(pose.WorldTransforms.size(), 2u);
    EXPECT_NEAR(pose.WorldTransforms[1].M[1][3], 0.0f, 0.0001f);
    EXPECT_NEAR(pose.WorldTransforms[1].M[2][3], 1.0f, 0.0001f);
}

TEST(Oot3dNativeSceneEnvironmentProvider, ReadsNativeSetupAndRawLightRecords) {
    auto catalog = ThreeDsRecomp::Oot3d::AssetCatalog::Parse(CatalogWithRecords(
        nlohmann::json::array({SceneRecord("scene:test_info.zsi", "scene_profile", 0x55)})));
    const auto payload = nlohmann::json({{"records", {{{"scene_stem", "test"},
        {"environment_setups", {{{"setup_index", 0}, {"setup_role", "gameplay"},
            {"commands", {{{"command_name", "skybox_settings"},
                            {"decoded", {{"skybox_id", 29}, {"indoors", 0}}}},
                          {{"command_name", "light_settings_list"},
                            {"decoded", {{"records", {{{"index", 0},
                                {"raw_hex", std::string(0x1C * 2, '0')}}}}}}}}}}}}}}}}).dump();
    const auto semantics = nlohmann::json({
        {"format", "oot3d_pica_lighting_semantics_v1"},
        {"source_kind", "oot3d_code_bin_and_zsi"},
        {"layout", "oot3d_pica_light_settings_record_0x1c"},
        {"record_size", 0x1C},
        {"engine_modes", {{"native_pica_lighting_vertex_color",
            {{"record_selector", "scene_environment_setup"},
             {"record_selector_source", "zsi_light_settings_list"},
             {"vertex_color_formula", "material_times_ambient_plus_directional"},
             {"runtime_transition_current_mode", 2}}}}}
    }).dump();
    const std::string transitionTable(5 * 0x36, '\0');
    const std::string transitionFallback(0x2C, '\0');
    std::string kankyoRecords(30 * 0x50, '\0');
    std::string kankyoSchedule(5 * 0x48, '\0');
    std::string kankyoDrawScale(sizeof(float), '\0');
    const std::string kankyoPath = "rom:/kankyo/TestSky.zar";
    std::memcpy(kankyoRecords.data() + 0x50, kankyoPath.c_str(), kankyoPath.size() + 1);
    const uint32_t profileCount = 4;
    const uint32_t layerCount = 2;
    const uint32_t coreCmbCount = 2;
    std::memcpy(kankyoRecords.data() + 0x50 + 0x44, &profileCount, sizeof(profileCount));
    std::memcpy(kankyoRecords.data() + 0x50 + 0x48, &layerCount, sizeof(layerCount));
    std::memcpy(kankyoRecords.data() + 0x50 + 0x4C, &coreCmbCount, sizeof(coreCmbCount));
    const uint16_t scheduleEnd = 0xFFFF;
    std::memcpy(kankyoSchedule.data() + 2, &scheduleEnd, sizeof(scheduleEnd));
    kankyoSchedule[4] = 1;
    kankyoSchedule[5] = 2;
    kankyoSchedule[6] = 3;
    const float drawScale = 320.0f;
    std::memcpy(kankyoDrawScale.data(), &drawScale, sizeof(drawScale));
    ThreeDsRecomp::Oot3d::NativeSourceProvider sources(
        [&payload, &semantics, &transitionTable, &transitionFallback, &kankyoRecords,
         &kankyoSchedule, &kankyoDrawScale](const std::string& resource) {
        const std::string* bytes = &payload;
        if (resource.find("lighting_semantics") != std::string::npos) {
            bytes = &semantics;
        } else if (resource.find("kankyo_skybox_records") != std::string::npos) {
            bytes = &kankyoRecords;
        } else if (resource.find("kankyo_schedule_table") != std::string::npos) {
            bytes = &kankyoSchedule;
        } else if (resource.find("kankyo_draw_scale") != std::string::npos) {
            bytes = &kankyoDrawScale;
        } else if (resource.find("transition_table") != std::string::npos) {
            bytes = &transitionTable;
        } else if (resource.find("transition_fallback") != std::string::npos) {
            bytes = &transitionFallback;
        }
        return std::make_shared<const std::vector<uint8_t>>(
            bytes->begin(), bytes->end());
    });
    ThreeDsRecomp::Oot3d::NativeSceneEnvironmentProvider environments(catalog, sources);
    const auto setup = environments.Resolve(0x55, 0);
    ASSERT_TRUE(setup->Ready()) << setup->Error;
    EXPECT_EQ(setup->SkyboxId, 29);
    EXPECT_EQ(setup->LightSettings.size(), 1);
    EXPECT_TRUE(setup->DecodedLighting.Available);
    EXPECT_TRUE(setup->LightingSemanticsAvailable);
    EXPECT_TRUE(setup->LightingSemanticPlan.Available);
    EXPECT_EQ(setup->LightingSemanticPlan.RecordSize, 0x1C);
    EXPECT_EQ(setup->LightingSemanticPlan.Template.RecordSelector, "scene_environment_setup");
    EXPECT_EQ(setup->LightingSemanticPlan.RuntimeTransitionCurrentMode, 2);
    EXPECT_TRUE(setup->DecodedLighting.NativeRuntimeTransitionTableAvailable);
    EXPECT_TRUE(setup->DecodedLighting.NativeRuntimeTransitionGlobalFallbackStateAvailable);
    ASSERT_EQ(setup->DecodedLighting.LightSettings.size(), 1);
    EXPECT_EQ(setup->DecodedLighting.LightSettings.front().SetupIndex, 0);
    EXPECT_EQ(setup->DecodedLighting.LightSettings.front().EntrySize, 0x1C);
    const auto kankyo = environments.ResolveKankyoTemporalState(1, 0, 0x8000);
    EXPECT_TRUE(kankyo.Available);
    EXPECT_EQ(kankyo.ArchiveName, "TestSky.zar");
    EXPECT_EQ(kankyo.CurrentProfileIndex, 2);
    EXPECT_EQ(kankyo.NextProfileIndex, 3);
    EXPECT_EQ(kankyo.ProfileCount, 4);
    EXPECT_EQ(kankyo.DrawScale, 320.0f);
}

TEST(Oot3dNativeSceneEnvironmentProvider, DecodesLogicalLightRecordIndexFromSingleRecordBytes) {
    const std::vector<uint8_t> recordBytes(0x1C, 0);
    const auto record = ThreeDsRecomp::Oot3d::DecodeOot3dNativePicaLightSettingsRecordForRuntime(
        recordBytes, 3, 7);
    EXPECT_EQ(record.SetupIndex, 3);
    EXPECT_EQ(record.Index, 7);
    EXPECT_EQ(record.Offset, 0);
    EXPECT_EQ(record.RawBytes.size(), 0x1C);
}

TEST(Oot3dNativeSceneEnvironmentProvider, DecodesContiguousNativeLightSettingTable) {
    std::vector<uint8_t> tableBytes(3 * 0x1C, 0);
    tableBytes[0x10 + 0x0A] = 11;
    tableBytes[0x10 + 0x0B] = 22;
    tableBytes[0x10 + 0x0C] = 33;
    tableBytes[0x1C + 0x1A] = 44;
    tableBytes[0x1C + 0x1B] = 55;
    tableBytes[2 * 0x1C] = 66;

    const auto records = ThreeDsRecomp::Oot3d::DecodeOot3dNativePicaLightSettingsTableForRuntime(
        tableBytes, 4);

    ASSERT_EQ(records.size(), 3);
    EXPECT_EQ(records[0].SetupIndex, 4);
    EXPECT_EQ(records[0].Index, 0);
    EXPECT_EQ(records[2].Index, 2);
    EXPECT_TRUE(records[0].NativeRuntimeEnvironmentLightSettingsAvailable);
    EXPECT_EQ(records[0].NativeRuntimeEnvironmentRecordOffset, 0x10);
    EXPECT_EQ(records[0].NativeRuntimeEnvironmentRecordStartDelta, 0x10);
    EXPECT_EQ(records[0].NativeRuntimeAmbientColor.R, 11);
    EXPECT_EQ(records[0].NativeRuntimeAmbientColor.G, 22);
    EXPECT_EQ(records[0].NativeRuntimeAmbientColor.B, 33);
    EXPECT_FALSE(records[2].NativeRuntimeEnvironmentLightSettingsAvailable);
    EXPECT_TRUE(records[2].NativeActorVsAmbientColorCandidateAvailable);
    EXPECT_EQ(records[2].NativeActorVsAmbientColor.R, 44);
    EXPECT_EQ(records[2].NativeActorVsAmbientColor.G, 55);
    EXPECT_EQ(records[2].NativeActorVsAmbientColor.B, 66);
}

TEST(Oot3dNativeRenderCommand, OwnsQueuedSceneUntilConsumedOrCancelled) {
    ThreeDsRecomp::Oot3d::ClearNativeRenderScenes();
    auto scene = std::make_shared<ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene>();
    const auto first = ThreeDsRecomp::Oot3d::QueueNativeRenderScene(scene);
    const auto second = ThreeDsRecomp::Oot3d::QueueNativeRenderScene(scene);
    ThreeDsRecomp::Oot3d::Oot3dNativePicaLightingRenderState runtimeLighting;
    runtimeLighting.Available = true;
    runtimeLighting.RuntimeEnvironmentTimeInputAvailable = true;
    runtimeLighting.RuntimeEnvironmentDayTime = 0x8000;
    const auto dynamic = ThreeDsRecomp::Oot3d::QueueNativeRenderScene(scene, runtimeLighting);
    ThreeDsRecomp::Oot3d::Oot3dNativePicaFogState runtimeFog;
    runtimeFog.Available = true;
    runtimeFog.Color = { 20, 30, 40, 255 };
    const auto dynamicFog = ThreeDsRecomp::Oot3d::QueueNativeRenderScene(
        scene, runtimeLighting, runtimeFog);
    auto environmentOwner = std::make_shared<int>(1);
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel environmentModel;
    ThreeDsRecomp::Oot3d::Oot3dNativeEnvironmentOverlay environmentOverlay;
    environmentOverlay.Model = &environmentModel;
    const auto dynamicEnvironment = ThreeDsRecomp::Oot3d::QueueNativeRenderScene(
        scene, runtimeLighting, runtimeFog, environmentOwner, { environmentOverlay });

    EXPECT_NE(first, 0);
    EXPECT_NE(second, first);
    EXPECT_NE(dynamic, 0);
    EXPECT_NE(dynamicFog, 0);
    EXPECT_NE(dynamicEnvironment, 0);
    EXPECT_EQ(ThreeDsRecomp::Oot3d::PendingNativeRenderSceneCount(), 5);
    EXPECT_TRUE(ThreeDsRecomp::Oot3d::CancelNativeRenderScene(first));
    EXPECT_FALSE(ThreeDsRecomp::Oot3d::CancelNativeRenderScene(first));
    EXPECT_EQ(ThreeDsRecomp::Oot3d::PendingNativeRenderSceneCount(), 4);
    ThreeDsRecomp::Oot3d::ClearNativeRenderScenes();
    EXPECT_EQ(ThreeDsRecomp::Oot3d::PendingNativeRenderSceneCount(), 0);
}

TEST(Oot3dNativePicaLighting, EvaluatesRuntimeColorWithoutMutatingSourceVertex) {
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel model;
    model.TransformBakedIntoVertices = true;
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderBatch batch;
    batch.Material.NativePicaLightingApplied = true;
    batch.Material.FragmentLightingEnabled = true;
    batch.Material.EmissionColor = { 0, 0, 0, 255 };
    batch.Material.AmbientColor = { 255, 255, 255, 255 };
    batch.Material.DiffuseColor = { 0, 0, 0, 255 };
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderVertex vertex;
    vertex.Color = { 200, 200, 200, 255 };
    vertex.NativePicaLightingInputColor = { 255, 255, 255, 255 };
    vertex.NativePicaLightingInputColorAvailable = true;
    batch.Vertices.push_back(vertex);

    ThreeDsRecomp::Oot3d::Oot3dNativePicaLightingRenderState lighting;
    lighting.Available = true;
    lighting.MaterialLightingEnableSource = "oot3d_cmb_material_fragment_lighting_flag";
    lighting.VertexColorFormula =
        "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb / 255, 0, 255) / 255)";
    lighting.AmbientColor = { 32, 64, 96, 255 };
    lighting.DiffuseColor = { 0, 0, 0, 255 };

    const auto output = ThreeDsRecomp::Oot3d::EvaluateOot3dNativePicaLightingVertexColor(
        lighting, model, batch, ThreeDsRecomp::Oot3d::Oot3dNativeRenderIdentityMatrix(), vertex);
    EXPECT_EQ(output.R, 32);
    EXPECT_EQ(output.G, 64);
    EXPECT_EQ(output.B, 96);
    EXPECT_EQ(vertex.Color.R, 200);
    EXPECT_EQ(vertex.Color.G, 200);
    EXPECT_EQ(vertex.Color.B, 200);
}

TEST(Oot3dNativePicaFog, BuildsFromPackagedRuntimeDefaultsWithoutCodeBinPath) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene scene;
    ThreeDsRecomp::Oot3d::Oot3dNativePicaLightingRenderState lighting;
    lighting.ActorVsLightPacket.PicaFogColorAvailable = true;
    lighting.ActorVsLightPacket.PicaFogColor = { 12, 34, 56, 255 };
    lighting.ActorVsLightPacket.PicaFogColorSource = "native_actor_vs_packet";
    ThreeDsRecomp::Oot3d::Oot3dNativeFogRuntimeDefaults defaults;
    defaults.Available = true;
    defaults.SourceRgbScaleR = 0.8f;
    defaults.SourceRgbScaleG = 0.8f;
    defaults.SourceRgbScaleB = 0.8f;
    defaults.SourceNear = 1.0f;
    defaults.SourceFar = 4000.0f;
    defaults.ProjectionNear = 1.0f;
    defaults.ProjectionFar = 4000.0f;
    defaults.SceneProjectionFar = 4000.0f;
    defaults.SourceKind = "oot3d_packaged_code_bin_fog_runtime_defaults";

    const auto fog = ThreeDsRecomp::Oot3d::BuildOot3dNativePicaFogState(scene, lighting, &defaults);
    EXPECT_TRUE(fog.Available) << fog.BlockedReason;
    EXPECT_TRUE(fog.CodeBinSourceDecoded);
    EXPECT_EQ(fog.SourceKind, defaults.SourceKind);
    EXPECT_EQ(fog.Color.R, 12);
    EXPECT_EQ(fog.Color.G, 34);
    EXPECT_EQ(fog.Color.B, 56);
}

TEST(Oot3dNativeRenderer, SubmitsEnvironmentOverlayBeforeRoomWithoutCopyingModel) {
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene scene;
    scene.Room.Name = "room";
    scene.Room.Batches.emplace_back();
    scene.Room.Batches[0].Vertices.resize(3);
    ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel environment;
    environment.Name = "environment";
    environment.Batches.emplace_back();
    environment.Batches[0].Vertices.resize(3);
    const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel* overlay = &environment;
    ThreeDsRecomp::Oot3d::Oot3dNativeRecordingRenderBackend backend;

    const auto result = ThreeDsRecomp::Oot3d::SubmitOot3dNativeDemoRenderScene(
        scene, backend, std::span<const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel* const>(&overlay, 1));
    ASSERT_TRUE(result.IsValid);
    ASSERT_EQ(backend.SubmittedBatches().size(), 2);
    EXPECT_EQ(backend.SubmittedBatches()[0].ModelName, "environment");
    EXPECT_EQ(backend.SubmittedBatches()[1].ModelName, "room");
}

TEST(Oot3dNativeRenderer, MatchesNativeKankyoCmabFamiliesWithoutSceneSpecificRules) {
    EXPECT_TRUE(ThreeDsRecomp::Oot3d::Oot3dNativeKankyoCmabAppliesToCmb(
        "fine_kumo_a.cmab", "fine_kumo_a0.cmb"));
    EXPECT_TRUE(ThreeDsRecomp::Oot3d::Oot3dNativeKankyoCmabAppliesToCmb(
        "holy_kumo_b.cmab", "holy_kumo_b1.cmb"));
    EXPECT_FALSE(ThreeDsRecomp::Oot3d::Oot3dNativeKankyoCmabAppliesToCmb(
        "fine_kumo_a.cmab", "fine_tenkyu0.cmb"));
}

} // namespace
