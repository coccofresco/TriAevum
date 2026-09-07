#include "oot3d_title_intro_logo_render.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oot3d_demo_math.h"
#include "oot3d_title_intro_logo_runtime.h"

extern "C" {
#include "oot3d/title_intro_opening_logo_runtime.h"
}
TitleIntroLogoComponentRuntime* FindTitleIntroLogoComponentRuntime(
    TitleIntroLogoRuntime& runtime,
    uint16_t componentIndex) {
    for (auto& component : runtime.Components) {
        if (component.Row != nullptr && component.Row->componentIndex == componentIndex) {
            return &component;
        }
    }
    return nullptr;
}

ThreeDsRecomp::Oot3d::Matrix4f TitleIntroLogoDrawMatrix(
    const Oot3dTitleIntroOpeningLogoDrawComponentState& draw) {
    ThreeDsRecomp::Oot3d::Matrix4f matrix{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            matrix.M[row][column] = draw.matrix[row * 4 + column];
        }
    }
    return matrix;
}

bool BuildTitleIntroLogoDrawModelToWorld(
    const Oot3dTitleIntroOpeningLogoDrawComponentState& draw,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const Camera& camera,
    double aspect,
    ThreeDsRecomp::Oot3d::Matrix4f& modelToWorld) {
    const double extent =
        std::max(ThreeDsRecomp::Oot3d::NativeDemoBoundsMaxExtent(renderScene.Bounds), 80.0);
    const bool nativeProjectionRangeAvailable = renderScene.PicaFog.ProjectionRangeAvailable;
    const double zNear = nativeProjectionRangeAvailable
                             ? static_cast<double>(renderScene.PicaFog.ProjectionNear)
                             : std::max(1.0, extent / 800.0);
    const double zFar = nativeProjectionRangeAvailable
                            ? static_cast<double>(renderScene.PicaFog.ProjectionFar)
                            : std::max(2000.0, extent * 10.0);
    const auto worldToClip = BuildWorldToClipMatrix(camera, aspect, zNear, zFar);
    ThreeDsRecomp::Oot3d::Matrix4f clipToWorld;
    if (!InvertMatrix(worldToClip, clipToWorld)) {
        return false;
    }

    const auto titleLocalToClip =
        MultiplyMatrix(BuildViewToClipMatrix(camera, aspect, zNear, zFar),
                       TitleIntroLogoDrawMatrix(draw));
    modelToWorld = MultiplyMatrix(clipToWorld, titleLocalToClip);
    return true;
}

uint8_t ColorByteFromNormalizedFloat(float value) {
    const int rounded = static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    return static_cast<uint8_t>(std::clamp(rounded, 0, 255));
}

ThreeDsRecomp::Oot3d::ColorRgba8 TitleIntroLogoDrawMaterialColor(
    const Oot3dTitleIntroOpeningLogoDrawComponentState& draw) {
    return {
        ColorByteFromNormalizedFloat(draw.colorR),
        ColorByteFromNormalizedFloat(draw.colorG),
        ColorByteFromNormalizedFloat(draw.colorB),
        ColorByteFromNormalizedFloat(draw.colorA),
    };
}

std::optional<std::array<float, 16>> ParseTitleIntroLogoLightBlock(
    const char* rowMajorValues) {
    if (rowMajorValues == nullptr || rowMajorValues[0] == '\0') {
        return std::nullopt;
    }
    std::array<float, 16> values{};
    std::string_view remaining(rowMajorValues);
    for (auto& value : values) {
        const size_t comma = remaining.find(',');
        const std::string_view token = remaining.substr(0, comma);
        const auto parse = std::from_chars(token.data(), token.data() + token.size(), value);
        if (parse.ec != std::errc{} || parse.ptr != token.data() + token.size()) {
            return std::nullopt;
        }
        if (comma == std::string_view::npos) {
            remaining = {};
        } else {
            remaining.remove_prefix(comma + 1);
        }
    }
    if (!remaining.empty()) {
        return std::nullopt;
    }
    return values;
}

std::string TitleIntroLogoDrawMaterialColorOverrideSource(
    const Oot3dTitleIntroOpeningLogoDrawComponentState& draw) {
    std::ostringstream source;
    source << "oot3d_enmag_draw_material_slot_" << kOot3dTitleIntroLogoMaterialColorSlot
           << "_color_apply_function_0x00358964"
           << ";draw_index=" << draw.drawIndex
           << ";alpha_field_offset=0x" << std::hex << draw.alphaFieldOffset;
    return source.str();
}

Oot3dTitleIntroOpeningLogoDrawSnapshot GenericCutsceneOverlayDrawBridge(
    const Oot3dSceneCutsceneFrameSnapshot& frame) {
    Oot3dTitleIntroOpeningLogoDrawSnapshot snapshot{};
    snapshot.drawCount = std::min<uint16_t>(
        frame.overlayDrawCount, OOT3D_TITLE_INTRO_OPENING_LOGO_DRAW_CAPACITY);
    for (uint16_t i = 0; i < snapshot.drawCount; ++i) {
        const auto& source = frame.overlayDraws[i];
        auto& draw = snapshot.draws[i];
        draw.drawIndex = source.drawIndex;
        draw.componentIndex = source.componentIndex;
        draw.alphaFieldOffset = source.alphaFieldOffset;
        draw.effectAlphaFieldOffset = source.effectAlphaFieldOffset;
        draw.visible = source.visible;
        draw.alphaNormalized = source.alphaNormalized;
        draw.effectAlphaNormalized = source.effectAlphaNormalized;
        draw.colorR = source.colorR;
        draw.colorG = source.colorG;
        draw.colorB = source.colorB;
        draw.colorA = source.colorA;
        std::copy(source.matrix, source.matrix + 16, draw.matrix);
    }
    return snapshot;
}

void ApplyTitleIntroOpeningFrameRuntimeLogoDraw(
    TitleIntroPlayback& playback,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const Camera& camera,
    double aspect) {
    playback.OpeningFrameRuntimeLogoDrawUsedForRender = false;
    playback.OpeningFrameRuntimeLogoDrawResolvedComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount = 0;
    playback.OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount = 0;
    playback.OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount = 0;
    playback.OpeningFrameRuntimeLogoDrawRenderStatus =
        Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderNotProcessedStatus();

    if (!playback.OpeningFrameRuntimeStepValid) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderStepNotValidStatus();
        return;
    }
    if (!playback.LogoRuntime.Loaded) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderComponentsNotLoadedStatus();
        return;
    }
    if (!playback.LogoRuntime.DrawRouteDecoded || !playback.LogoRuntime.DrawContextDecoded) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderRouteOrContextNotDecodedStatus();
        return;
    }
    const bool useGenericSnapshot = playback.GenericCutsceneRuntimeRequested;
    if (useGenericSnapshot ? playback.GenericCutsceneSnapshot.overlayValid == 0
                           : playback.OpeningFrameRuntimeStep.logoDrawStatus !=
                                 OOT3D_TITLE_INTRO_OPENING_LOGO_RUNTIME_OK) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderSnapshotNotOkStatus();
        return;
    }

    const auto genericBridge = GenericCutsceneOverlayDrawBridge(playback.GenericCutsceneSnapshot);
    const auto& snapshot =
        useGenericSnapshot ? genericBridge : playback.OpeningFrameRuntimeStep.logoDraw;
    for (uint16_t i = 0; i < snapshot.drawCount; ++i) {
        const auto& draw = snapshot.draws[i];
        auto* component = FindTitleIntroLogoComponentRuntime(playback.LogoRuntime, draw.componentIndex);
        const bool sourceResolved =
            useGenericSnapshot
                ? playback.GenericCutsceneSnapshot.overlayDraws[i].valid != 0
                : draw.sourceDrawRow != nullptr && draw.sourceComponentRow != nullptr;
        if (!sourceResolved || component == nullptr || component->Row == nullptr || !component->Loaded ||
            (!useGenericSnapshot &&
             component->Row->componentIndex != draw.sourceComponentRow->componentIndex)) {
            continue;
        }

        ++playback.OpeningFrameRuntimeLogoDrawResolvedComponentCount;
        if (draw.visible == 0 || draw.alphaNormalized <= 0.0f) {
            ++playback.OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount;
            continue;
        }

        ++playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount;
        ThreeDsRecomp::Oot3d::CsabPose pose;
        std::vector<ThreeDsRecomp::Oot3d::Matrix4f> skinTransforms;
        const double poseSampleFrame = playback.Frame;
        float poseFrame = 0.0f;
        if (!component->CsabBytes.empty() && component->Csab.FrameCount > 0) {
            poseFrame = static_cast<float>(std::clamp(
                poseSampleFrame, 0.0,
                static_cast<double>(component->Csab.FrameCount)));
            pose = ThreeDsRecomp::Oot3d::SampleCsabPoseFrameBytes(component->CsabBytes, component->Model, poseFrame);
            skinTransforms =
                ThreeDsRecomp::Oot3d::BuildOot3dNativeDemoSkinTransforms(component->BindWorldTransforms, pose);
        }

        ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel renderModel;
        if (component->BaseRenderModelBuilt) {
            renderModel = component->BaseRenderModel;
            ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelPose(
                renderModel, component->Model, pose.Valid ? &pose : nullptr,
                !skinTransforms.empty() ? &skinTransforms : nullptr);
        } else {
            ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelBuildOptions options;
            options.BakeTransformIntoVertices = false;
            options.Pose = pose.Valid ? &pose : nullptr;
            options.SkinTransforms = !skinTransforms.empty() ? &skinTransforms : nullptr;
            renderModel = ThreeDsRecomp::Oot3d::BuildOot3dNativeRenderModel(component->Model, options);
        }
        if (!component->MaterialAnimations.empty()) {
            float materialAnimationFrame = static_cast<float>(playback.Frame);
            if (component->MaterialAnimationRuntimeLoopOverrideResolved) {
                const auto& animation = component->MaterialAnimations.front();
                materialAnimationFrame =
                    ThreeDsRecomp::Oot3d::ResolveOot3dNativeCmabMaterialAnimationFrame(
                        animation.FrameCountCandidate,
                        component->MaterialAnimationRuntimeLoopMode,
                        materialAnimationFrame);
            }
            ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelMaterialAnimationFrame(
                renderModel, component->Model, component->MaterialAnimations,
                materialAnimationFrame);
        }
        const char* role = component->Row->componentRole != nullptr ? component->Row->componentRole : "component";
        renderModel.Name = "title_intro:logo:" + std::string(role) + ":" + renderModel.Name;
        std::ostringstream source;
        source << component->ArchivePath.string() << "!" << component->CmbName;
        if (!component->CsabName.empty()) {
            source << "!" << component->CsabName;
        }
        source << ";enmag_draw_index=" << draw.drawIndex
               << ";alpha_field_offset=0x" << std::hex << draw.alphaFieldOffset
               << ";effect_alpha_field_offset=0x" << draw.effectAlphaFieldOffset
               << std::dec
               << ";title_logo_pose_frame=" << poseFrame
               << ";title_logo_pose_frame_source=title_intro_global_frame_native_inclusive_max_frame_clamp"
               << ";title_logo_material_animation_count="
               << component->MaterialAnimations.size()
               << ";title_logo_material_animation_runtime_loop_override="
               << (component->MaterialAnimationRuntimeLoopOverrideResolved ? 1 : 0)
               << ";title_logo_material_animation_runtime_loop_mode="
               << component->MaterialAnimationRuntimeLoopMode
               << ";title_logo_render_bridge=oot3d_enmag_title_local_clip_via_current_world_to_clip_inverse"
               << ";title_logo_projection_source=current_native_cutscene_camera_and_pica_projection_range"
               << ";title_logo_projection_fov_degrees=" << camera.FovDegrees
               << ";title_logo_projection_aspect=" << aspect
               << ";title_logo_projection_near=" << renderScene.PicaFog.ProjectionNear
               << ";title_logo_projection_far=" << renderScene.PicaFog.ProjectionFar;
        renderModel.Source = source.str();
        ThreeDsRecomp::Oot3d::Matrix4f modelToWorld;
        if (!BuildTitleIntroLogoDrawModelToWorld(draw, renderScene, camera, aspect, modelToWorld)) {
            playback.OpeningFrameRuntimeLogoDrawRenderStatus =
                Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderProjectionBridgeFailedStatus();
            return;
        }
        renderModel.ModelToWorld = modelToWorld;
        const Oot3dTitleIntroLogoDrawRow* nativeDrawRow = draw.sourceDrawRow;
        if (nativeDrawRow == nullptr) {
            const auto rowIt = std::find_if(
                playback.LogoRuntime.DrawRows.begin(), playback.LogoRuntime.DrawRows.end(),
                [&](const auto* row) {
                    return row != nullptr && row->drawIndex == draw.drawIndex &&
                           row->componentIndex == draw.componentIndex;
                });
            if (rowIt != playback.LogoRuntime.DrawRows.end()) {
                nativeDrawRow = *rowIt;
            }
        }
        const auto lightBlock = nativeDrawRow != nullptr
                                    ? ParseTitleIntroLogoLightBlock(nativeDrawRow->lightBlockRowMajor)
                                    : std::nullopt;
        if (nativeDrawRow != nullptr && lightBlock.has_value()) {
            const auto* row = nativeDrawRow;
            const auto toColor = [](float value) {
                return static_cast<uint8_t>(std::clamp(
                    static_cast<int>(std::lround(value * 255.0f)), 0, 255));
            };
            const double effect = std::clamp(static_cast<double>(draw.effectAlphaNormalized), 0.0, 1.0);
            const double localX =
                (effect + static_cast<double>(row->effectVectorBias)) *
                static_cast<double>(row->effectVectorDoubleScale);
            const double localY = -localX;
            const double localZ =
                static_cast<double>(row->effectVectorBias) -
                effect * static_cast<double>(row->effectVectorHalfScale);
            ThreeDsRecomp::Oot3d::Vec3f localDirection = {
                static_cast<float>(localX), static_cast<float>(localY), static_cast<float>(localZ),
            };
            const double length = std::sqrt(
                static_cast<double>(localDirection.X) * localDirection.X +
                static_cast<double>(localDirection.Y) * localDirection.Y +
                static_cast<double>(localDirection.Z) * localDirection.Z);
            if (length > 0.000001) {
                localDirection.X = static_cast<float>(localDirection.X / length);
                localDirection.Y = static_cast<float>(localDirection.Y / length);
                localDirection.Z = static_cast<float>(localDirection.Z / length);
            }

            auto logoLighting = renderScene.PicaLighting;
            logoLighting.Available = true;
            logoLighting.Light0Vector = {
                -localDirection.X, -localDirection.Y, -localDirection.Z,
            };
            logoLighting.Light1Vector = { 0.0f, 0.0f, 1.0f };
            logoLighting.DirectionalVectorsUseModelSpace = true;
            logoLighting.DirectionalVectorSpaceSource =
                "oot3d_enmag_draw_pica_vsh_f80_f83_model_space";
            logoLighting.Light0VectorSource =
                "oot3d_enmag_draw_effect_vector_code_bin_literals_pica_vsh_f80_negated_to_incident_vector";
            logoLighting.Light1VectorSource =
                "oot3d_enmag_draw_pica_vsh_f83_model_space_negated_to_incident_vector";
            logoLighting.DirectionalFormula =
                "clamp(base.rgb * clamp(material_emission.rgb + material_ambient.rgb * ambient.rgb / 255 + material_diffuse.rgb * diffuse.rgb * max(dot(normal, light0), 0) / 255, 0, 255) / 255)";
            logoLighting.ActorVsLightPacket.Available = true;
            logoLighting.ActorVsLightPacket.ColorPacketAvailable = true;
            logoLighting.ActorVsLightPacket.VectorOriginResolved = true;
            logoLighting.ActorVsLightPacket.CompactPayloadSourceResolved = false;
            logoLighting.ActorVsLightPacket.AmbientColor = {
                toColor((*lightBlock)[4]), toColor((*lightBlock)[5]),
                toColor((*lightBlock)[6]), 255,
            };
            logoLighting.ActorVsLightPacket.Diffuse0Color = {
                toColor((*lightBlock)[0]), toColor((*lightBlock)[1]),
                toColor((*lightBlock)[2]), 255,
            };
            logoLighting.ActorVsLightPacket.Diffuse1Color = {
                renderScene.PicaLighting.DiffuseColor.R,
                renderScene.PicaLighting.DiffuseColor.G,
                renderScene.PicaLighting.DiffuseColor.B,
                255,
            };
            logoLighting.ActorVsLightPacket.ColorSource =
                "oot3d_enmag_draw_light_block_code_bin_001da8d0_plus_runtime_light_settings_diffuse_f84";
            logoLighting.VertexHemisphereRuntimeColorSource =
                logoLighting.ActorVsLightPacket.ColorSource;
            logoLighting.VertexHemisphereRuntimeVectorSource =
                logoLighting.Light0VectorSource;
            logoLighting.VertexHemisphereLightColorMode =
                "oot3d_draw_local_light_packet_colors_with_explicit_vectors";
            ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(
                logoLighting, renderModel, true);
        } else if (renderScene.PicaLighting.Available) {
            ThreeDsRecomp::Oot3d::ApplyOot3dNativePicaLightingStateToModel(
                renderScene.PicaLighting, renderModel);
        }
        renderModel.SubmitQueue = ThreeDsRecomp::Oot3d::Oot3dNativeSubmitQueue::Small;
        playback.OpeningFrameRuntimeLogoDrawMaterialColorOverrideBatchCount +=
            ThreeDsRecomp::Oot3d::ApplyOot3dNativeRenderModelRuntimeMaterialColorOverride(
                renderModel,
                kOot3dTitleIntroLogoMaterialColorSlot,
                TitleIntroLogoDrawMaterialColor(draw),
                TitleIntroLogoDrawMaterialColorOverrideSource(draw));
        renderModel.Bounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(renderModel);
        renderScene.ActorVisuals.push_back(std::move(renderModel));
        ++playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount;
    }

    playback.OpeningFrameRuntimeLogoDrawUsedForRender =
        snapshot.drawCount > 0 &&
        playback.OpeningFrameRuntimeLogoDrawResolvedComponentCount == snapshot.drawCount;
    if (!playback.OpeningFrameRuntimeLogoDrawUsedForRender) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderUnresolvedComponentRowsStatus();
    } else if (playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount == 0 &&
               playback.OpeningFrameRuntimeLogoDrawSkippedInvisibleComponentCount == snapshot.drawCount) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderAllComponentsInvisibleStatus();
    } else if (playback.OpeningFrameRuntimeLogoDrawSubmittedVisualCount ==
               playback.OpeningFrameRuntimeLogoDrawVisibleComponentCount) {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderBoundVisibleComponentsStatus();
    } else {
        playback.OpeningFrameRuntimeLogoDrawRenderStatus =
            Oot3d_TitleIntroOpeningLogoRuntimeDrawRenderPartiallyBoundStatus();
    }
}
