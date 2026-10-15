#include "fast/oot3d/effect_graph.h"
#include "fast/renderer3ds/pica_ui_canvas.h"
#include "fast/renderer3ds/pica_draw_coverage.h"
#include "fast/renderer3ds/pica_shadow_geometry.h"
#include "fast/renderer3ds/pica_surface_emission.h"
#include "fast/renderer3ds/pica_scene_light_continuity.h"
#include "fast/renderer3ds/virtual_shadow_keyframes.h"
#include "fast/renderer3ds/pica_geometry_guide.h"
#include "fast/oot3d/pica_directional_shadow_lighting.h"
#include "oot3d/renderer/ui_presentation_layout.h"
#include "fast/renderer3ds/pica_surface_passthrough_scale.h"
#include "fast/oot3d/display_effect_plan.h"
#include "fast/oot3d/directional_shadows.h"
#include "fast/oot3d/contact_shadows.h"
#include "fast/oot3d/sample_view.h"
#include "fast/oot3d/ambient_occlusion_composite.h"
#include "fast/oot3d/anti_aliasing_frame_policy.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/oot3d/graphics_settings_persistence.h"
#include <nlohmann/json.hpp>
#include "fast/oot3d/gpu_profile_plan.h"
#include "fast/oot3d/hiz_depth_pyramid.h"
#include "fast/oot3d/hiz_reflection.h"
#include "fast/oot3d/linear_scene_color.h"
#include "fast/oot3d/motion_vectors.h"
#include "fast/oot3d/grass_blade_geometry.h"
#include "fast/oot3d/grass_blade_shape.h"
#include "fast/oot3d/grass_collision_resolver.h"
#include "fast/oot3d/grass_interaction_bridge.h"
#include "fast/oot3d/grass_interaction_field.h"
#include "fast/oot3d/grass_motion.h"
#include "fast/oot3d/grass_placement_cache.h"
#include "fast/oot3d/grass_primitive_topology.h"
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/grass_shading_environment.h"
#include "fast/oot3d/grass_surface_eligibility.h"
#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/grass_mask_interior.h"
#include "fast/oot3d/grass_texture_source_cache.h"
#include "fast/oot3d/grass_texture_twins.h"
#include "fast/oot3d/grass_visibility.h"
#include "fast/oot3d/pica_shader_instrumentation.h"
#include "fast/oot3d/outline_occlusion_pass.h"
#include "fast/oot3d/grass_wind.h"
#include "fast/oot3d/scene_view_bridge.h"
#include "fast/oot3d/scene_surface_registry.h"
#include "fast/oot3d/scene_composite.h"
#include "fast/oot3d/resource_state_tracker.h"
#include "fast/oot3d/smaa_1x.h"
#include "fast/oot3d/smaa_lookup_data.h"
#include "fast/oot3d/visual_clock.h"
#include "fast/oot3d/temporal_history_manager.h"
#include "fast/oot3d/temporal_jitter.h"
#include "fast/oot3d/temporal_aa.h"
#include "fast/oot3d/texture_preview_artifact.h"
#include "fast/oot3d/pica_toon_shader.h"
#include "fast/oot3d/toon_surface_response.h"
#include "fast/oot3d/grass_draw_state.h"
#include "fast/oot3d/grass_shader_sources.h"
#include "fast/oot3d/renderer_validation_telemetry.h"
#include "fast/oot3d/reflection_ibl.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include "fast/oot3d/reflection_debug.h"
#include "fast/oot3d/reflection_provider.h"
#include "fast/oot3d/pica_scanout_effects.h"
#include "fast/oot3d/pica_nri_shader_contract.h"
#include "fast/oot3d/pica_nri_draw_ownership.h"
#include "fast/oot3d/pica_nri_texture_upload.h"
#include "fast/oot3d/pica_nri_upload.h"
#include "fast/oot3d/pica_nri_vertex_input.h"
#include "fast/oot3d/pica_ambient_occlusion_guide.h"
#include "fast/oot3d/pica_grass_texture_coordinates.h"
#include "fast/oot3d/pica_scene_domain_guide.h"
#include "fast/oot3d/perspective_fov_policy.h"
#include "fast/oot3d/presentation_pacing_policy.h"
#include "fast/oot3d/render_resolution_policy.h"
#include "oot3d/renderer/ui_presentation_layout.h"
#ifndef ENABLE_OOT3D_VULKAN
#define OOT3D_TEST_DEFINED_VULKAN
#define ENABLE_OOT3D_VULKAN
#endif
#include "fast/oot3d/pica_dynamic_rendering_scope.h"
#include "fast/oot3d/pica_alpha_coverage_policy.h"
#include "fast/oot3d/pica_guide_sampling_barriers.h"
#include "fast/oot3d/nri_pica_display_copy_pass.h"
#include "fast/oot3d/nri_pica_memory_fill_clear_pass.h"
#include "fast/oot3d/nri_pica_render_target_init_pass.h"
#include "fast/oot3d/nri_pica_render_target_owner.h"
#include "fast/oot3d/nri_swapchain.h"
#include "fast/oot3d/nri_upscaler_instances.h"
#include "fast/oot3d/presentation_settings_transaction.h"
#include "fast/oot3d/renderer_presentation_controller.h"
#include "fast/oot3d/vulkan_adapter_policy.h"
#ifdef OOT3D_TEST_DEFINED_VULKAN
#undef ENABLE_OOT3D_VULKAN
#undef OOT3D_TEST_DEFINED_VULKAN
#endif
#include "fast/oot3d/pica_shadow2d.h"
#include "fast/oot3d/pica_fragment_lighting.h"
#include "fast/oot3d/pica_reactive_mask.h"
#include "fast/oot3d/pica_rigid_motion.h"
#include "fast/oot3d/pica_reflection_material.h"
#include "fast/oot3d/reflection_material_profile.h"
#include "fast/oot3d/toon_outline_shader.h"
#include "fast/oot3d/upscaler_guides.h"
#include "fast/oot3d/upscaler_orientation.h"
#include "fast/oot3d/upscaler_descriptor_key.h"
#include "fast/oot3d/native_ui_overlay.h"

#include <gtest/gtest.h>
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <random>
#include <numbers>
#include <cstring>
#include <limits>
#include <memory>
#include <map>
#include <vector>
#include <unordered_map>
#include <sstream>
#include "fast/renderer3ds/pica_rigid_interpolation.h"

namespace {

TEST(Oot3dUiPresentationLayout, FitsNativeTopScreenAtCommonOutputAspects) {
    using Oot3d::Renderer::FitUiPresentationViewport;

    const auto widescreen =
        FitUiPresentationViewport(1280U, 720U, 400.0F, 240.0F);
    EXPECT_EQ(widescreen.X, 40U);
    EXPECT_EQ(widescreen.Y, 0U);
    EXPECT_EQ(widescreen.Width, 1200U);
    EXPECT_EQ(widescreen.Height, 720U);

    const auto standard =
        FitUiPresentationViewport(1024U, 768U, 400.0F, 240.0F);
    EXPECT_EQ(standard.X, 0U);
    EXPECT_EQ(standard.Y, 77U);
    EXPECT_EQ(standard.Width, 1024U);
    EXPECT_EQ(standard.Height, 614U);

    const auto ultrawide =
        FitUiPresentationViewport(3440U, 1440U, 400.0F, 240.0F);
    EXPECT_EQ(ultrawide.X, 520U);
    EXPECT_EQ(ultrawide.Y, 0U);
    EXPECT_EQ(ultrawide.Width, 2400U);
    EXPECT_EQ(ultrawide.Height, 1440U);
}

TEST(Oot3dUiPresentationLayout, KeepsWidescreenCanvasIndependent) {
    const auto viewport = Oot3d::Renderer::FitUiPresentationViewport(
        1024U, 768U, 1280.0F / 3.0F, 240.0F);
    EXPECT_EQ(viewport.X, 0U);
    EXPECT_EQ(viewport.Y, 96U);
    EXPECT_EQ(viewport.Width, 1024U);
    EXPECT_EQ(viewport.Height, 576U);
}

TEST(Oot3dPicaRasterCanvas, EachScreenContentHasItsMappingAndPlacedUiKeepsNativeFit) {
    using Fast::Renderer3ds::PicaCompositionDomain;
    using Fast::Renderer3ds::PicaScreenContent;
    using ::Oot3d::Renderer::ResolvePicaRasterCanvas;

    for (const auto [width, height] : {std::pair{1280U, 720U}, std::pair{960U, 720U},
                                       std::pair{1720U, 720U}}) {
        // Placed UI keeps the uniform native canvas: HUD and menu layout, and
        // the pointer mapping that shares the same rectangle.
        const auto ui = ResolvePicaRasterCanvas(PicaCompositionDomain::Ui, PicaScreenContent::None,
                                                400U, 240U, width, height);
        const float fit = std::min(width / 400.0F, height / 240.0F);
        EXPECT_NEAR(ui.X, (width - 400.0F * fit) * 0.5F, 1.0e-3F);
        EXPECT_NEAR(ui.Y, (height - 240.0F * fit) * 0.5F, 1.0e-3F);
        EXPECT_FLOAT_EQ(ui.ScaleX, fit);
        EXPECT_FLOAT_EQ(ui.ScaleY, fit);

        // The perspective scene keeps its own full-target stretch, compensated
        // by the frame's field-of-view policy.
        const auto scene = ResolvePicaRasterCanvas(PicaCompositionDomain::Scene, PicaScreenContent::None,
                                                   400U, 240U, width, height);
        EXPECT_FLOAT_EQ(scene.X, 0.0F);
        EXPECT_FLOAT_EQ(scene.Y, 0.0F);
        EXPECT_FLOAT_EQ(scene.ScaleX, width / 400.0F);
        EXPECT_FLOAT_EQ(scene.ScaleY, height / 240.0F);

        // Screen content showing the rendered frame takes the scene's mapping
        // at every aspect ratio: a redraw of the copied frame lands on the
        // pixels it copied, and a wash reaches every output pixel.
        const auto frame = ResolvePicaRasterCanvas(PicaCompositionDomain::Screen,
                                                   PicaScreenContent::RenderedFrame, 400U, 240U,
                                                   width, height);
        EXPECT_FLOAT_EQ(frame.X, scene.X);
        EXPECT_FLOAT_EQ(frame.Y, scene.Y);
        EXPECT_FLOAT_EQ(frame.ScaleX, scene.ScaleX);
        EXPECT_FLOAT_EQ(frame.ScaleY, scene.ScaleY);

        // A picture of its own (a movie frame) is shown whole at its own
        // proportions, on the same canvas as placed UI; the renderer paints
        // the surround black.
        const auto picture = ResolvePicaRasterCanvas(PicaCompositionDomain::Screen,
                                                     PicaScreenContent::OwnPicture, 400U, 240U,
                                                     width, height);
        EXPECT_FLOAT_EQ(picture.X, ui.X);
        EXPECT_FLOAT_EQ(picture.Y, ui.Y);
        EXPECT_FLOAT_EQ(picture.ScaleX, ui.ScaleX);
        EXPECT_FLOAT_EQ(picture.ScaleY, ui.ScaleY);

        // An overlay with its own alpha covers the target without distortion:
        // one uniform scale, the excess centred.
        const auto overlay = ResolvePicaRasterCanvas(PicaCompositionDomain::Screen,
                                                     PicaScreenContent::PictureOverlay, 400U, 240U,
                                                     width, height);
        const float cover = std::max(width / 400.0F, height / 240.0F);
        EXPECT_FLOAT_EQ(overlay.ScaleX, cover);
        EXPECT_FLOAT_EQ(overlay.ScaleY, cover);
        EXPECT_NEAR(overlay.X, (width - 400.0F * cover) * 0.5F, 1.0e-3F);
        EXPECT_NEAR(overlay.Y, (height - 240.0F * cover) * 0.5F, 1.0e-3F);
    }
}

TEST(Oot3dPicaRasterCanvas, PresentedDensityTopTargetKeepsThePresentedProportions) {
    using Fast::Renderer3ds::PicaCompositionDomain;
    using Fast::Renderer3ds::PicaScreenContent;
    using namespace Fast::Oot3d;
    using ::Oot3d::Renderer::ResolvePicaRasterCanvas;

    // The same output with the former 2x top target and the presented-density
    // one: placed UI and pictures land on the same presented rectangle (half
    // the storage X), and the scene keeps its per-axis stretch.
    for (const RenderExtent output : {RenderExtent{1280U, 720U}, {960U, 720U}, {1720U, 720U}, {3840U, 2160U}}) {
        const RenderExtent native{kNativeTopScreenPhysicalWidth, kNativeTopScreenPhysicalHeight};
        const float sampling = NativeTopScreenStorageSamplingX(native, output);
        EXPECT_FLOAT_EQ(sampling, 0.5F);
        const auto target = ResolveNativePicaRenderExtent(native, output, 1.0F);
        const RenderExtent doubled{target.Width * 2U, target.Height};
        for (const auto content : {PicaScreenContent::None, PicaScreenContent::OwnPicture,
                                   PicaScreenContent::PictureOverlay}) {
            const auto domain = content == PicaScreenContent::None ? PicaCompositionDomain::Ui
                                                                   : PicaCompositionDomain::Screen;
            const auto former = ResolvePicaRasterCanvas(domain, content, native.Width, native.Height,
                                                        doubled.Width, doubled.Height);
            const auto current = ResolvePicaRasterCanvas(domain, content, native.Width, native.Height,
                                                         target.Width, target.Height, sampling);
            EXPECT_NEAR(current.X * 2.0F, former.X, 1.0e-3F);
            EXPECT_NEAR(current.Y, former.Y, 1.0e-3F);
            EXPECT_FLOAT_EQ(current.ScaleX * 2.0F, former.ScaleX);
            EXPECT_FLOAT_EQ(current.ScaleY, former.ScaleY);
        }
        const auto scene = ResolvePicaRasterCanvas(PicaCompositionDomain::Scene, PicaScreenContent::None,
                                                   native.Width, native.Height, target.Width, target.Height,
                                                   sampling);
        EXPECT_FLOAT_EQ(scene.ScaleX, float(target.Width) / native.Width);
        EXPECT_FLOAT_EQ(scene.ScaleY, float(target.Height) / native.Height);
    }
    EXPECT_FLOAT_EQ(NativeTopScreenStorageSamplingX({480U, 400U}, {400U, 240U}), 1.0F);
    EXPECT_FLOAT_EQ(NativeTopScreenStorageSamplingX({240U, 320U}, {3840U, 2160U}), 1.0F);
}

TEST(Oot3dPicaRasterCanvas, SurroundCoversEveryPixelOutsideTheFittedPicture) {
    using Fast::Renderer3ds::PicaCompositionDomain;
    using Fast::Renderer3ds::PicaScreenContent;
    using Fast::Renderer3ds::SurroundPicaCanvas;
    using ::Oot3d::Renderer::ResolvePicaRasterCanvas;

    struct Case {
        uint32_t Width, Height, Bars;
    };
    // 16:9 and 21:9 put bars beside the picture, 4:3 above and below it, and
    // the native aspect leaves nothing to paint.
    for (const Case c : {Case{1280U, 720U, 2U}, Case{1720U, 720U, 2U}, Case{960U, 720U, 2U},
                         Case{400U, 240U, 0U}, Case{1200U, 720U, 0U}}) {
        const auto canvas = ResolvePicaRasterCanvas(PicaCompositionDomain::Screen,
                                                    PicaScreenContent::OwnPicture, 400U, 240U,
                                                    c.Width, c.Height);
        const auto surround = SurroundPicaCanvas(canvas, 400U, 240U, c.Width, c.Height);
        EXPECT_EQ(surround.Count, c.Bars) << c.Width << 'x' << c.Height;
        // Every target pixel lies in exactly one of: the picture, a bar.
        std::vector<uint8_t> hits(size_t{c.Width} * c.Height, 0U);
        const auto inner = Fast::Renderer3ds::ClipToPicaCanvas({0, 0, c.Width, c.Height}, canvas,
                                                               400U, 240U);
        const auto mark = [&](const Fast::Renderer3ds::PicaCanvasClip& rect) {
            ASSERT_GE(rect.X, 0);
            ASSERT_GE(rect.Y, 0);
            ASSERT_LE(uint64_t(rect.X) + rect.Width, c.Width);
            ASSERT_LE(uint64_t(rect.Y) + rect.Height, c.Height);
            for (uint32_t y = 0; y < rect.Height; ++y)
                for (uint32_t x = 0; x < rect.Width; ++x)
                    ++hits[size_t(rect.Y + y) * c.Width + rect.X + x];
        };
        mark(inner);
        for (uint32_t index = 0; index < surround.Count; ++index)
            mark(surround.Rects[index]);
        EXPECT_TRUE(std::all_of(hits.begin(), hits.end(), [](uint8_t hit) { return hit == 1U; }))
            << c.Width << 'x' << c.Height;
    }
    // 16:9: the picture spans 1200 of 1280 columns, centred.
    const auto canvas = ResolvePicaRasterCanvas(PicaCompositionDomain::Screen,
                                                PicaScreenContent::OwnPicture, 400U, 240U, 1280U,
                                                720U);
    const auto surround = SurroundPicaCanvas(canvas, 400U, 240U, 1280U, 720U);
    ASSERT_EQ(surround.Count, 2U);
    EXPECT_EQ(surround.Rects[0].X, 0);
    EXPECT_EQ(surround.Rects[0].Width, 40U);
    EXPECT_EQ(surround.Rects[0].Height, 720U);
    EXPECT_EQ(surround.Rects[1].X, 1240);
    EXPECT_EQ(surround.Rects[1].Width, 40U);
}

TEST(Oot3dPicaRasterCanvas, OnlyContentCoveringTheFrameIsScreenContent) {
    using Fast::Renderer3ds::PicaCompositionDomain;
    using Fast::Renderer3ds::PicaScreenContent;
    using Fast::Renderer3ds::PicaUiFrameContent;
    using Fast::Renderer3ds::PicaUiFrameContentEvidence;
    using Fast::Renderer3ds::ResolvePicaUiFrameContent;
    const auto evidence = [](bool viewport, bool scene, bool geometry,
                             bool untextured, bool copies, bool alphaFree = false) {
        PicaUiFrameContentEvidence value;
        value.CoversWholeTarget = viewport;
        value.SceneDrewOnTarget = scene;
        value.GeometryCoversViewport = geometry;
        value.Untextured = untextured;
        value.SamplesOnlyRenderedCopies = copies;
        value.SamplesOnlyAlphaFreeTextures = alphaFree;
        return value;
    };
    const auto screen = [](PicaScreenContent content) {
        return PicaUiFrameContent{PicaCompositionDomain::Screen, content};
    };
    const auto keep = [](PicaCompositionDomain domain) {
        return PicaUiFrameContent{domain, PicaScreenContent::None};
    };

    // The title's full-frame effect over its copy of the rendered scene, and a
    // fade washing the whole frame, show the rendered frame.
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui, evidence(true, true, true, false, true)),
              screen(PicaScreenContent::RenderedFrame));
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui, evidence(true, true, true, true, false)),
              screen(PicaScreenContent::RenderedFrame));
    // A movie frame covering the screen (RGB8 textures, no alpha) is an opaque
    // picture of its own; a covering texture with alpha (the mask over the
    // revelation's first flashback frames, IA8) is an overlay.
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui,
                                        evidence(true, true, true, false, false, true)),
              screen(PicaScreenContent::OwnPicture));
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui, evidence(true, true, true, false, false)),
              screen(PicaScreenContent::PictureOverlay));
    // Copies decide before alpha: a redraw of copied output shows the frame.
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui,
                                        evidence(true, true, true, false, true, true)),
              screen(PicaScreenContent::RenderedFrame));
    for (const uint8_t format : {1U, 3U, 6U, 7U, 10U, 12U})
        EXPECT_FALSE(Fast::Renderer3ds::PicaTextureFormatCarriesAlpha(format)) << int(format);
    for (const uint8_t format : {0U, 2U, 4U, 5U, 8U, 9U, 11U, 13U, 14U, 255U})
        EXPECT_TRUE(Fast::Renderer3ds::PicaTextureFormatCarriesAlpha(format)) << int(format);
    // Placed UI drawn over the scene with a whole-target viewport (HUD
    // buttons, minimap, dialogue) covers part of it, whatever it samples.
    for (const bool untextured : {false, true}) {
        for (const bool copies : {false, true}) {
            EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui,
                                                evidence(true, true, false, untextured, copies)),
                      keep(PicaCompositionDomain::Ui));
        }
    }
    // Without a scene on the target (title, file select, menus) or with a
    // partial viewport, nothing is frame content.
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui, evidence(true, false, true, true, false)),
              keep(PicaCompositionDomain::Ui));
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Ui, evidence(false, true, true, true, false)),
              keep(PicaCompositionDomain::Ui));
    // Only UI-attributed draws are ever reclassified.
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Scene, evidence(true, true, true, true, false)),
              keep(PicaCompositionDomain::Scene));
    EXPECT_EQ(ResolvePicaUiFrameContent(PicaCompositionDomain::Unknown, evidence(true, true, true, true, false)),
              keep(PicaCompositionDomain::Unknown));
}

namespace {

// A draw through the typed model-view-projection layout (projection f[0..3],
// view f[4..6], model f[20..22], position in v0), with an orthographic
// projection of the 400x240 native screen and the given model rows.
struct CoverageDraw {
    std::array<float, 96 * 4> Uniforms{};
    std::vector<float> Positions;
    std::array<uint16_t, 4> Indices{0, 1, 2, 3};
    Fast::Renderer3ds::PicaVertexAttributeView Attribute{};
    Fast::Renderer3ds::PicaVertexBindingView Binding{};
    Fast::Renderer3ds::PicaDrawView View;

    CoverageDraw(std::vector<float> positions, std::array<float, 12> model)
        : Positions(std::move(positions)) {
        const auto row = [this](size_t uniform, std::array<float, 4> values) {
            std::copy(values.begin(), values.end(), Uniforms.begin() + uniform * 4U);
        };
        row(0, {2.0F / 400.0F, 0.0F, 0.0F, -1.0F});
        row(1, {0.0F, 2.0F / 240.0F, 0.0F, -1.0F});
        row(2, {0.0F, 0.0F, 1.0F, 0.0F});
        row(3, {0.0F, 0.0F, 0.0F, 1.0F});
        row(4, {1.0F, 0.0F, 0.0F, 0.0F});
        row(5, {0.0F, 1.0F, 0.0F, 0.0F});
        row(6, {0.0F, 0.0F, 1.0F, 0.0F});
        row(20, {model[0], model[1], model[2], model[3]});
        row(21, {model[4], model[5], model[6], model[7]});
        row(22, {model[8], model[9], model[10], model[11]});
        auto& transform = View.TemporalVertexProgram.Hooks.Transform;
        transform.Operation =
            Fast::Renderer3ds::PicaVertexTransformOperation::ModelViewProjection3x4;
        transform.ProjectionFirstUniform = 0U;
        transform.ProjectionRowCount = 4U;
        transform.ViewFirstUniform = 4U;
        transform.ViewRowCount = 3U;
        transform.ModelFirstUniform = 20U;
        transform.ModelRowCount = 3U;
        transform.PositionInputRegister = 0U;
        transform.NormalInputRegister = 1U;
        Attribute.Location = 0U;
        Attribute.Binding = 0U;
        Attribute.Format = Fast::Renderer3ds::PicaVertexFormat::Float;
        Attribute.ComponentCount = 3U;
        Binding.Binding = 0U;
        Binding.ByteStride = 3U * sizeof(float);
        Binding.Bytes = {reinterpret_cast<const uint8_t*>(Positions.data()),
                         Positions.size() * sizeof(float)};
        View.VertexAttributes = {&Attribute, 1U};
        View.VertexBindings = {&Binding, 1U};
        View.IndexBytes = {reinterpret_cast<const uint8_t*>(Indices.data()),
                           Indices.size() * sizeof(uint16_t)};
        View.Indexed = true;
        View.IndicesAre16Bit = true;
        View.VertexCount = 4U;
    }

    bool Covers(uint32_t booleans = 0U) const {
        return Fast::Renderer3ds::PicaDrawGeometryCoversViewport(
            View, booleans,
            {reinterpret_cast<const uint8_t*>(Uniforms.data()),
             Uniforms.size() * sizeof(float)});
    }
};

constexpr std::array<float, 12> kIdentityModel{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

} // namespace

TEST(Oot3dPicaDrawCoverage, ProvesFullScreenQuadsAndRejectsPlacedElements) {
    // The copied-frame redraw: the native screen rectangle itself.
    EXPECT_TRUE(CoverageDraw({0, 0, 0, 400, 0, 0, 0, 240, 0, 400, 240, 0},
                             kIdentityModel)
                    .Covers());
    // A fade: a unit quad the model matrix scales over the whole screen.
    EXPECT_TRUE(CoverageDraw({-0.5F, -0.5F, 0, 0.5F, -0.5F, 0, -0.5F, 0.5F, 0,
                              0.5F, 0.5F, 0},
                             {400, 0, 0, 200, 0, 240, 0, 120, 0, 0, 1, 0})
                    .Covers());
    // A placed element, such as a button or a text box, covers part of it.
    EXPECT_FALSE(CoverageDraw({300, 180, 0, 380, 180, 0, 300, 230, 0, 380,
                               230, 0},
                              kIdentityModel)
                     .Covers());
    // A full-width bar is not the whole screen either.
    EXPECT_FALSE(CoverageDraw({0, 0, 0, 400, 0, 0, 0, 30, 0, 400, 30, 0},
                              kIdentityModel)
                     .Covers());
    // Nothing is proven without the typed transform, or with an enabled
    // skeleton, or when the positions cannot be read.
    CoverageDraw untyped({0, 0, 0, 400, 0, 0, 0, 240, 0, 400, 240, 0},
                         kIdentityModel);
    untyped.View.TemporalVertexProgram.Hooks.Transform = {};
    EXPECT_FALSE(untyped.Covers());
    CoverageDraw skinned({0, 0, 0, 400, 0, 0, 0, 240, 0, 400, 240, 0},
                         kIdentityModel);
    auto& skeleton = skinned.View.TemporalVertexProgram.Hooks.Skeleton;
    skeleton.Operation =
        Fast::Renderer3ds::PicaVertexSkeletonOperation::MatrixPalette3x4;
    skeleton.EnableBooleanUniform = 2U;
    skeleton.PaletteFirstUniform = 30U;
    skeleton.RowsPerMatrix = 3U;
    skeleton.BoneIndexInputRegister = 2U;
    skeleton.BoneWeightInputRegister = 3U;
    skeleton.MaximumInfluences = 1U;
    EXPECT_TRUE(skinned.Covers(0U));
    EXPECT_FALSE(skinned.Covers(1U << 2U));
    CoverageDraw truncated({0, 0, 0, 400, 0, 0, 0, 240, 0}, kIdentityModel);
    EXPECT_FALSE(truncated.Covers());
}

TEST(Oot3dUiPresentationLayout, RejectsInvalidExtents) {
    EXPECT_FALSE(Oot3d::Renderer::FitUiPresentationViewport(
                     0U, 720U, 400.0F, 240.0F)
                     .Valid());
    EXPECT_FALSE(Oot3d::Renderer::FitUiPresentation(
                     1280U, 720U, 0.0F, 240.0F)
                     .Valid());
}

TEST(Oot3dUiPresentationLayout, MapsTargetPointsIntoLogicalCanvas) {
    using Oot3d::Renderer::MapUiPresentationPoint;

    const auto widescreenOrigin =
        MapUiPresentationPoint(1280U, 720U, 400.0F, 240.0F, 40.0F, 0.0F);
    ASSERT_TRUE(widescreenOrigin.Inside);
    EXPECT_FLOAT_EQ(widescreenOrigin.X, 0.0F);
    EXPECT_FLOAT_EQ(widescreenOrigin.Y, 0.0F);

    const auto widescreenTouch =
        MapUiPresentationPoint(1280U, 720U, 400.0F, 240.0F, 400.0F, 663.0F);
    ASSERT_TRUE(widescreenTouch.Inside);
    EXPECT_NEAR(widescreenTouch.X, 120.0F, 0.001F);
    EXPECT_NEAR(widescreenTouch.Y, 221.0F, 0.001F);

    const auto standardCenter =
        MapUiPresentationPoint(1024U, 768U, 400.0F, 240.0F, 512.0F, 384.0F);
    ASSERT_TRUE(standardCenter.Inside);
    EXPECT_NEAR(standardCenter.X, 200.0F, 0.001F);
    EXPECT_NEAR(standardCenter.Y, 120.0F, 0.001F);

    EXPECT_FALSE(MapUiPresentationPoint(
                     1280U, 720U, 400.0F, 240.0F, 39.0F, 360.0F)
                     .Inside);
    EXPECT_FALSE(MapUiPresentationPoint(
                     1024U, 768U, 400.0F, 240.0F, 512.0F, 76.0F)
                     .Inside);
}

TEST(Oot3dDirectionalShadows, RejectsMissingDirectionAndDisabledMode) {
    using namespace Fast::Oot3d;
    DirectionalShadowFrameInput input;
    EXPECT_EQ(BuildDirectionalShadowFramePlan(input).Status, DirectionalShadowPlanStatus::Disabled);

    input.Settings.Mode = DirectionalShadowMode::VirtualShadowMaps;
    EXPECT_EQ(BuildDirectionalShadowFramePlan(input).Status, DirectionalShadowPlanStatus::InvalidLightDirection);
}

TEST(Oot3dContactShadows, ProjectsNativeViewToReverseZ) {
    using namespace Fast::Oot3d;
    // A native perspective (column-major): x and y scales, w = -z_view.
    DirectionalShadowMatrix projection{};
    projection[0] = 1.5F;
    projection[5] = 2.0F;
    projection[10] = -1.0F;
    projection[11] = -1.0F;
    projection[14] = -2.0F;
    DirectionalShadowMatrix clipToWorld{};
    ASSERT_TRUE(InvertDirectionalShadowMatrix(projection, clipToWorld));
    const auto contact = BuildContactShadowProjection(clipToWorld, IdentityDirectionalShadowMatrix());
    ASSERT_TRUE(contact.has_value());
    // A view point 10 units ahead keeps the native x and y; its depth is
    // near / distance.
    const std::array<float, 4> point{ 3.0F, -2.0F, -10.0F, 1.0F };
    std::array<float, 4> clip{};
    for (size_t row = 0; row < 4U; ++row)
        for (size_t column = 0; column < 4U; ++column)
            clip[row] += (*contact)[column * 4U + row] * point[column];
    ASSERT_NEAR(clip[3], 10.0F, 1.0e-4F);
    EXPECT_NEAR(clip[0] / clip[3], 0.45F, 1.0e-5F);
    EXPECT_NEAR(clip[1] / clip[3], -0.4F, 1.0e-5F);
    EXPECT_NEAR(clip[2] / clip[3], kContactShadowNearPlane / 10.0F, 1.0e-6F);

    const DirectionalShadowMatrix singular{};
    EXPECT_FALSE(BuildContactShadowProjection(singular, IdentityDirectionalShadowMatrix()).has_value());
}

TEST(Oot3dContactShadows, ResolvesGodotDispatchForConvergingDivergingAndParallelLight) {
    using namespace Fast::Oot3d;
    DirectionalShadowMatrix projection{};
    projection[0] = 1.0F;
    projection[5] = 1.0F;
    projection[11] = -1.0F;
    projection[14] = kContactShadowNearPlane;
    ContactShadowSettings settings;
    settings.Enabled = true;

    // A light ahead on the view axis: converging rays toward the screen
    // centre, groups around it covering the whole screen.
    const auto ahead = ResolveContactShadowDispatch(projection, { 0.0F, 0.0F, -1.0F }, 1920U, 1080U, settings, 7U);
    ASSERT_TRUE(ahead.Valid());
    EXPECT_FLOAT_EQ(ahead.Constants.LightCoordinate[0], 960.0F);
    EXPECT_FLOAT_EQ(ahead.Constants.LightCoordinate[1], 540.0F);
    EXPECT_FLOAT_EQ(ahead.Constants.LightCoordinate[3], 1.0F);
    EXPECT_EQ(ahead.Groups[0], kContactShadowWaveSize);
    EXPECT_EQ(ahead.Constants.LightOffset[0], (-15 - 1) * 64);
    EXPECT_EQ(ahead.Constants.LightOffset[1], (-9 - 1) * 64);
    EXPECT_EQ(ahead.Groups[1], uint32_t(15 + 2 + 16));
    EXPECT_EQ(ahead.Groups[2], uint32_t(9 + 2 + 10));
    EXPECT_EQ(ahead.Constants.ScreenSize[0], 1920);
    EXPECT_EQ(ahead.Constants.ScreenSize[1], 1080);
    EXPECT_FLOAT_EQ(ahead.Constants.FrameCount, 7.0F);

    // Behind the camera: diverging rays.
    const auto behind = ResolveContactShadowDispatch(projection, { 0.0F, 0.0F, 1.0F }, 1920U, 1080U, settings, 7U);
    ASSERT_TRUE(behind.Valid());
    EXPECT_FLOAT_EQ(behind.Constants.LightCoordinate[3], -1.0F);

    // Across the view: parallel screen rays, normalised to the major axis.
    const auto across = ResolveContactShadowDispatch(projection, { 1.0F, 0.5F, 0.0F }, 1920U, 1080U, settings, 7U);
    ASSERT_TRUE(across.Valid());
    EXPECT_FLOAT_EQ(across.Constants.LightCoordinate[3], 0.0F);
    EXPECT_FLOAT_EQ(std::max(std::abs(across.Constants.LightCoordinate[0]),
                             std::abs(across.Constants.LightCoordinate[1])),
                    1.0F);

    EXPECT_FALSE(ResolveContactShadowDispatch(projection, { 0.0F, 0.0F, -1.0F }, 0U, 1080U, settings, 7U).Valid());
    EXPECT_FALSE(ResolveContactShadowDispatch(projection, { 0.0F, 0.0F, 0.0F }, 1920U, 1080U, settings, 7U).Valid());
}

TEST(Oot3dContactShadows, ClampsShaderParameters) {
    using namespace Fast::Oot3d;
    DirectionalShadowMatrix projection{};
    projection[0] = 1.0F;
    projection[5] = 1.0F;
    projection[11] = -1.0F;
    ContactShadowSettings settings;
    settings.SurfaceThickness = 0.0F;
    settings.Opacity = 2.0F;
    settings.Blur = -1.0F;
    const auto dispatch = ResolveContactShadowDispatch(projection, { 0.0F, 0.0F, -1.0F }, 64U, 64U, settings, 2048U);
    ASSERT_TRUE(dispatch.Valid());
    EXPECT_GT(dispatch.Constants.SurfaceThickness, 0.0F);
    EXPECT_FLOAT_EQ(dispatch.Constants.Opacity, 1.0F);
    EXPECT_FLOAT_EQ(dispatch.Constants.Blur, 0.0F);
    EXPECT_FLOAT_EQ(dispatch.Constants.FrameCount, 0.0F);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dContactShadows, ComputeShadersCompileForEveryLength) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    for (uint32_t length = 0; length < Fast::Oot3d::kContactShadowLengthCount; ++length) {
        const auto source = Fast::Oot3d::ContactShadowComputeShader(length);
        EXPECT_NE(source.find("#define READ_COUNT " + std::to_string(2U + length)), std::string::npos);
        const auto result = compiler.CompileGlslToSpv(source, shaderc_compute_shader, "contact_shadows.comp", options);
        EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success) << result.GetErrorMessage();
    }
}
#endif

TEST(Renderer3dsVirtualShadows, RetainsWorldPagesAndRetriesUncommittedCasterChanges) {
    using namespace Fast::Renderer3ds;
    auto layout = BuildVirtualShadowLayout({0.25F, 0.25F, 0}, {0, 0, 1}, 4096, 65536);
    std::array<VirtualShadowCaster, 2> casters{{
        {{{-3000, -3000, -10}, {3000, 3000, 10}, true}, 1},
        {{{20, 20, 0}, {40, 40, 80}, true}, 2, true}}};
    VirtualShadowMapCache cache;
    cache.Prepare(layout, casters, 1);
    cache.Commit();
    const auto pageAtWorldOrigin = cache.Table()[4 * 8 + 4];
    auto shifted = BuildVirtualShadowLayout({64.25F, 0.25F, 0}, {0, 0, 1}, 4096, 65536, &layout);
    cache.Prepare(shifted, casters, 2);
    cache.Commit();
    EXPECT_EQ(cache.Table()[4 * 8 + 3], pageAtWorldOrigin);
    casters[1].WorldBounds.Minimum[0] += 256;
    casters[1].WorldBounds.Maximum[0] += 256;
    casters[1].ContentVersion = 3;
    cache.Prepare(shifted, casters, 3);
    const auto oldFootprint = std::find_if(cache.Updates().begin(), cache.Updates().end(),
        [](const auto& page) { return page.Key == VirtualShadowPageKey{0, 0, 0}; });
    const auto newFootprint = std::find_if(cache.Updates().begin(), cache.Updates().end(),
        [](const auto& page) { return page.Key == VirtualShadowPageKey{4, 0, 0}; });
    ASSERT_NE(oldFootprint, cache.Updates().end());
    ASSERT_NE(newFootprint, cache.Updates().end());
    EXPECT_FALSE(oldFootprint->RefreshStatic);
    EXPECT_FALSE(newFootprint->RefreshStatic);
    EXPECT_TRUE(cache.StaticCasters(*oldFootprint).empty());
    EXPECT_TRUE(cache.DynamicCasters(*oldFootprint).empty());
    EXPECT_TRUE(cache.StaticCasters(*newFootprint).empty());
    ASSERT_EQ(cache.DynamicCasters(*newFootprint).size(), 1U);
    EXPECT_EQ(cache.DynamicCasters(*newFootprint).front(), 1U);
    EXPECT_EQ(cache.Statistics().StaticUpdated, 0U);
    const auto pending = cache.Updates().size();
    cache.Prepare(shifted, casters, 4);
    EXPECT_EQ(cache.Updates().size(), pending);
    cache.Commit();
    auto roundoff = BuildVirtualShadowLayout({64.25F, 0.25F, 0}, {1.0e-7F, 0, 1}, 4096, 65536, &shifted);
    cache.Prepare(roundoff, casters, 5);
    EXPECT_TRUE(cache.Updates().empty());
}

TEST(Renderer3dsVirtualShadows, PreservesCoarseCoverageWhenPhysicalPagesAreExhausted) {
    using namespace Fast::Renderer3ds;
    auto layout = BuildVirtualShadowLayout({}, {0, 0, 1}, 2048, 65536);
    const std::array<VirtualShadowCaster, 1> casters{{
        {{{-65536, -65536, -10}, {65536, 65536, 10}, true}, 1}}};
    VirtualShadowMapCache cache;
    cache.Prepare(layout, casters, 1);
    ASSERT_GT(cache.Statistics().Unmapped, 0U);
    const uint32_t side = layout.AtlasResolution / kVirtualShadowPhysicalPageTexels;
    for (auto entry : cache.Table())
        if (entry != kVirtualShadowEmptyPage) EXPECT_LE(entry, side * side);
    const auto& coarse = layout.Levels[layout.LevelCount - 1];
    const int x = int(std::floor(62000.0F / coarse.PageWorldSize)) - coarse.OriginX;
    const int y = -coarse.OriginY;
    EXPECT_NE(cache.Table()[((layout.LevelCount - 1) * 8 + y) * 8 + x], 0U);
    const uint32_t nearPage = cache.Table()[4 * 8 + 4];
    ASSERT_NE(nearPage, 0U);
    cache.Commit();
    layout = BuildVirtualShadowLayout({64.25F, 0, 0}, {0, 0, 1}, 2048, 65536, &layout);
    cache.Prepare(layout, casters, 2);
    const auto& fine = layout.Levels[0];
    EXPECT_EQ(cache.Table()[(-fine.OriginY) * 8 - fine.OriginX], nearPage);
}

TEST(Renderer3dsShadowGeometry, BoundsFixedSkinnedInputsInCapturedWorldSpace) {
    using namespace Fast::Renderer3ds;
    PicaVertexShaderHookLayout hooks;
    hooks.Transform = {PicaVertexTransformOperation::ModelViewProjection3x4, 0, 4, 4, 3, 20, 3, 0, 1};
    hooks.Skeleton = {PicaVertexSkeletonOperation::MatrixPalette3x4, 0, 1, 20, 3, 6, 7, 4};
    auto& surface = hooks.Surface;
    surface.ViewNormalTemporary = 0;
    surface.LightingBooleanUniform = 2;
    surface.DiffuseUniform = 10;
    surface.AmbientUniform = 11;
    surface.TintBooleanUniform = 3;
    surface.TintInputRegister = 3;
    surface.TintScale = surface.LightEnabledValue = {90, 1};
    surface.LightDirectionUniforms = {30, 33, 36};
    surface.BoneIndexScale = surface.BoneWeightScale = surface.SkinnedNormalScale = {90, 1};
    surface.SkinnedPositionScale = {90, 0};
    surface.InfluenceCount = {90, 2};
    surface.AdditionalInfluenceThresholds = {PicaVertexUniformComponent{91, 0}, PicaVertexUniformComponent{91, 1}};
    std::array<std::array<float, 4>, 96> uniforms{};
    for (const auto first : {4, 20})
        for (int axis = 0; axis < 3; ++axis) uniforms[first + axis][axis] = 1;
    uniforms[20][3] = 440;
    uniforms[90] = {0.25F, 1, 2, 0};
    uniforms[91] = {3, 4, 0, 0};
    std::array<std::array<float, 4>, 4> data{{
        {8, 12, 16, 1}, {16, 20, 24, 1}, {0, 0, 0, 0}, {1, 0, 0, 0}}};
    const std::array<PicaSceneVertexBufferBinding, 3> bindings{{
        {0, 0, 32, 16}, {1, 32, 16, 16}, {2, 48, 16, 16}}};
    const std::array<PicaNriPackedVertexAttribute, 3> attributes{{{0, 0, 0}, {6, 1, 0}, {7, 2, 0}}};
    const std::array<PicaNriPackedVertexBinding, 3> inputs{{{0, 16, false}, {1, 16, true}, {2, 16, true}}};
    auto viewToWorld = Fast::Oot3d::IdentityDirectionalShadowMatrix();
    viewToWorld[12] = -100;
    PicaShadowCasterCache cache;
    const PicaShadowGeometryView geometry{
        {reinterpret_cast<const uint8_t*>(data.data()), sizeof(data)}, bindings, attributes, inputs, 0, 0, 2, 0, false};
    const auto bytes = std::span(reinterpret_cast<const uint8_t*>(uniforms.data()), sizeof(uniforms));
    const auto first = BuildPicaShadowCaster(geometry, hooks, 3, bytes, viewToWorld, 1, cache);
    ASSERT_TRUE(first.WorldBounds.Available);
    EXPECT_LE(first.WorldBounds.Minimum[0], 12);
    EXPECT_GT(first.WorldBounds.Minimum[0], 11.5F);
    EXPECT_GE(first.WorldBounds.Maximum[0], 14);
    EXPECT_LT(first.WorldBounds.Maximum[0], 14.5F);
    uniforms[20][3] = 840;
    viewToWorld[12] = -200;
    const auto moved = BuildPicaShadowCaster(geometry, hooks, 3, bytes, viewToWorld, 1, cache);
    ASSERT_TRUE(moved.WorldBounds.Available);
    EXPECT_NEAR(moved.WorldBounds.Minimum[0], first.WorldBounds.Minimum[0], 1.0e-5F);
    EXPECT_NEAR(moved.WorldBounds.Maximum[0], first.WorldBounds.Maximum[0], 1.0e-5F);
    // Native skinning restores homogeneous W after blending; camera
    // translation must not be multiplied by the sum of bone weights.
    data[3] = {0.25F, 0.25F, 0, 0};
    const auto partialWeights = BuildPicaShadowCaster(geometry, hooks, 3, bytes, viewToWorld, 1, cache);
    ASSERT_TRUE(partialWeights.WorldBounds.Available);
    EXPECT_LE(partialWeights.WorldBounds.Minimum[0], -94);
    EXPECT_GT(partialWeights.WorldBounds.Minimum[0], -94.5F);
    EXPECT_GE(partialWeights.WorldBounds.Maximum[0], -93);
    EXPECT_LT(partialWeights.WorldBounds.Maximum[0], -92.5F);
}

TEST(Oot3dDirectionalShadows, RejectsOrthographicScreenDrawsAsCasters) {
    using namespace Fast::Oot3d;
    constexpr size_t floatOffset = 80U;
    constexpr size_t vec4Bytes = 4U * sizeof(float);
    std::vector<uint8_t> uniforms(floatOffset + 96U * vec4Bytes);
    const auto writeProjectionRow = [&](uint32_t row, const std::array<float, 4>& value) {
        std::memcpy(uniforms.data() + floatOffset + static_cast<size_t>(row) * vec4Bytes, value.data(), vec4Bytes);
    };

    writeProjectionRow(3U, { 0.0F, 0.0F, -1.0F, 0.0F });
    EXPECT_TRUE(UsesPicaPerspectiveProjection(uniforms));

    writeProjectionRow(3U, { 0.0F, 0.0F, 0.0F, 1.0F });
    EXPECT_FALSE(UsesPicaPerspectiveProjection(uniforms));
}

std::string ReflectionMaterialShader(bool consumesSpecular) {
    std::string shader = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D source_texture;
layout(location=0) out vec4 pica_color;
layout(location=2) out vec4 pica_material_guide;
void main() {
    vec4 secondary_fragment_color=vec4(0.25,0.1,0.05,1.0);
    // OOT3D_PICA_MATERIAL_TOON_POINT
    vec4 combiner_output=)glsl";
    shader += consumesSpecular
        ? "secondary_fragment_color;\n"
        : "vec4(1.0);\n";
    shader += R"glsl(    pica_color=texture(source_texture,vec2(0.5))*combiner_output;
    float oot3d_specular_signal = clamp(max(max(secondary_fragment_color.r, secondary_fragment_color.g), secondary_fragment_color.b) * 4.0, 0.0, 1.0);
    pica_material_guide = vec4(oot3d_specular_signal, clamp(1.0 - oot3d_specular_signal * 0.75, 0.08, 1.0), 1.0, 0.0);
}
)glsl";
    return shader;
}

std::string VertexLitReflectionMaterialShader() {
    return R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D source_texture;
layout(location=0) out vec4 pica_color;
layout(location=2) out vec4 pica_material_guide;
void main() {
    pica_color=texture(source_texture,vec2(0.5));
    pica_material_guide = vec4(0.0);
}
)glsl";
}

TEST(Oot3dReflectionMaterialProfile,
     ResolvesExactHashDimensionsAndLowestMapperSlot) {
    using namespace Fast::Oot3d;
    ReflectionMaterialRule water;
    water.RuleId = 4U;
    water.Target = {0x1122U, 64U, 32U, 0x07U};
    ApplyReflectionMaterialProfileDefaults(
        water, ReflectionMaterialProfile::Water);
    ReflectionMaterialRule metal;
    metal.RuleId = 8U;
    metal.Target = {0x3344U, 0U, 0U, 0x02U};
    ApplyReflectionMaterialProfileDefaults(
        metal, ReflectionMaterialProfile::Metal);
    const std::array rules{water, metal};
    const std::array textures{
        ReflectionMaterialTextureIdentity{
            0x1122U, 64U, 32U, 2U},
        ReflectionMaterialTextureIdentity{
            0x3344U, 128U, 128U, 1U},
    };
    const auto resolved =
        ResolveReflectionMaterialProfile(rules, textures);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->RuleId, 8U);
    EXPECT_EQ(resolved->MapperSlot, 1U);
    EXPECT_EQ(resolved->Profile,
              ReflectionMaterialProfile::Metal);
    EXPECT_FLOAT_EQ(resolved->Parameters.Reflectivity, 0.88F);
    EXPECT_FLOAT_EQ(resolved->Parameters.Roughness, 0.22F);
}

TEST(Oot3dReflectionMaterialProfile,
     RejectsDimensionAndMapperMismatches) {
    using namespace Fast::Oot3d;
    ReflectionMaterialRule rule;
    rule.RuleId = 1U;
    rule.Target = {0xAABBCCDDU, 32U, 32U, 0x01U};
    const std::array rules{rule};
    const std::array textures{
        ReflectionMaterialTextureIdentity{
            0xAABBCCDDU, 64U, 32U, 0U},
        ReflectionMaterialTextureIdentity{
            0xAABBCCDDU, 32U, 32U, 1U},
    };
    EXPECT_FALSE(
        ResolveReflectionMaterialProfile(rules, textures)
            .has_value());
}

TEST(Oot3dReflectionMaterial, RequiresWorldDepthAndOriginalTevSpecularUse) {
    Fast::Oot3d::PicaReflectionMaterialDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        0U, true, true, 0xFU};
    EXPECT_EQ(
        Fast::Oot3d::ClassifyPicaReflectionMaterialDraw(
            ReflectionMaterialShader(true), draw, true),
        Fast::Oot3d::PicaReflectionMaterialEligibility::Eligible);
    EXPECT_EQ(
        Fast::Oot3d::ClassifyPicaReflectionMaterialDraw(
            ReflectionMaterialShader(false), draw, true),
        Fast::Oot3d::PicaReflectionMaterialEligibility::
            NoSpecularTevUse);
    draw.DepthWriteEnabled = false;
    EXPECT_EQ(
        Fast::Oot3d::ClassifyPicaReflectionMaterialDraw(
            ReflectionMaterialShader(true), draw, true),
        Fast::Oot3d::PicaReflectionMaterialEligibility::NoDepth);
    draw.DepthWriteEnabled = true;
    draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Ui;
    EXPECT_EQ(
        Fast::Oot3d::ClassifyPicaReflectionMaterialDraw(
            ReflectionMaterialShader(true), draw, true),
        Fast::Oot3d::PicaReflectionMaterialEligibility::OutsideScene);
}

TEST(Oot3dReflectionMaterial, ChangesCacheIdentityForCalibratedGuide) {
    const std::string source = ReflectionMaterialShader(true);
    const auto variant =
        Fast::Oot3d::BuildPicaReflectionMaterialShaderVariant(
            source, 19U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true);
    ASSERT_TRUE(variant.Applied());
    EXPECT_NE(variant.FragmentKey, 19U);
}

TEST(Oot3dReflectionMaterial,
     ExplicitTextureProfileCalibratesVertexLitGuideOnly) {
    using namespace Fast::Oot3d;
    const std::string source = VertexLitReflectionMaterialShader();
    const auto profile = DefaultReflectionMaterialParameters(
        ReflectionMaterialProfile::Water);
    const auto variant =
        BuildPicaReflectionMaterialShaderVariant(
            source, 23U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true,
            &profile);
    ASSERT_TRUE(variant.Applied());
    EXPECT_EQ(
        variant.Eligibility,
        PicaReflectionMaterialEligibility::
            ExplicitTextureProfile);
}

TEST(Oot3dReflectionMaterial,
     ExplicitProfileParametersProduceDistinctShaderVariants) {
    using namespace Fast::Oot3d;
    const ReflectionMaterialParameters glossy{
        0.90F, 0.08F};
    const ReflectionMaterialParameters rough{
        0.35F, 0.80F};
    const auto glossyVariant =
        BuildPicaReflectionMaterialShaderVariant(
            VertexLitReflectionMaterialShader(), 41U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true,
            &glossy);
    const auto roughVariant =
        BuildPicaReflectionMaterialShaderVariant(
            VertexLitReflectionMaterialShader(), 41U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true,
            &rough);
    ASSERT_TRUE(glossyVariant.Applied());
    ASSERT_TRUE(roughVariant.Applied());
    EXPECT_NE(glossyVariant.FragmentKey,
              roughVariant.FragmentKey);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dReflectionMaterial, CalibratedFragmentShaderCompiles) {
    const auto variant =
        Fast::Oot3d::BuildPicaReflectionMaterialShaderVariant(
            ReflectionMaterialShader(true), 3U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true);
    ASSERT_TRUE(variant.Applied());
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "pica_reflection_material.frag");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}

TEST(Oot3dReflectionMaterial,
     ExplicitVertexLitFragmentShaderCompiles) {
    using namespace Fast::Oot3d;
    const auto profile = DefaultReflectionMaterialParameters(
        ReflectionMaterialProfile::Polished);
    const auto variant =
        BuildPicaReflectionMaterialShaderVariant(
            VertexLitReflectionMaterialShader(), 5U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, true, true, 0xFU},
            true,
            &profile);
    ASSERT_TRUE(variant.Applied());
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "pica_reflection_texture_material.frag");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

TEST(Oot3dReflectionIbl, DerivesStableEnvironmentFromPicaUniforms) {
    std::vector<uint8_t> uniforms(
        Fast::Oot3d::kPicaPackedFragmentUniformSize, 0U);
    const std::array<float, 4> fog{0.4F, 0.5F, 0.6F, 1.0F};
    const std::array<float, 4> ambient{0.2F, 0.3F, 0.4F, 1.0F};
    std::memcpy(uniforms.data() +
                    Fast::Oot3d::kPicaPackedFragmentFogColorOffset,
                fog.data(), sizeof(fog));
    std::memcpy(uniforms.data() +
                    Fast::Oot3d::
                        kPicaPackedFragmentLightingGlobalAmbientOffset,
                ambient.data(),
                sizeof(ambient));

    Fast::Oot3d::PicaReflectionEnvironmentAccumulator accumulator;
    accumulator.Observe(
        7U, "fog_factor lighting_global_ambient", uniforms, true);
    const auto profile = accumulator.Resolve(7U);
    EXPECT_TRUE(profile.PicaDerived);
    EXPECT_EQ(profile.ObservationCount, 2U);
    EXPECT_NE(profile.Signature, 0U);
    for (size_t channel = 0; channel < 3U; ++channel) {
        EXPECT_NEAR(profile.Horizon[channel], fog[channel], 1.0e-6F);
        EXPECT_NEAR(
            profile.Sky[channel],
            fog[channel] * 0.75F + ambient[channel] * 0.25F,
            1.0e-6F);
        EXPECT_NEAR(
            profile.Ground[channel],
            ambient[channel] * 0.65F + fog[channel] * 0.10F,
            1.0e-6F);
    }

    const auto retained = accumulator.Resolve(8U);
    EXPECT_EQ(retained.Signature, profile.Signature);
    EXPECT_TRUE(retained.PicaDerived);
    accumulator.Observe(
        9U, "fog_factor lighting_global_ambient", uniforms, false);
    EXPECT_EQ(accumulator.Resolve(9U).Signature, profile.Signature);

    // A program key analyzes its source once: the keyed path gives the same
    // profile, and a later draw of that program reuses the terms.
    Fast::Oot3d::PicaReflectionEnvironmentAccumulator keyed;
    keyed.Observe(7U, "fog_factor lighting_global_ambient", uniforms, true, 0x1234U);
    EXPECT_EQ(keyed.Resolve(7U).Signature, profile.Signature);
    keyed.Observe(10U, "", uniforms, true, 0x1234U);
    EXPECT_EQ(keyed.Resolve(10U).ObservationCount, 2U);
}

TEST(Oot3dReflectionIbl, SplitSumBrdfIsFiniteAndMaterialDependent) {
    const auto smooth =
        Fast::Oot3d::IntegrateReflectionBrdf(0.8F, 0.1F, 512U);
    const auto rough =
        Fast::Oot3d::IntegrateReflectionBrdf(0.3F, 0.9F, 512U);
    for (float value :
         {smooth.Scale, smooth.Bias, rough.Scale, rough.Bias}) {
        EXPECT_TRUE(std::isfinite(value));
        EXPECT_GE(value, 0.0F);
        EXPECT_LE(value, 1.1F);
    }
    EXPECT_GT(smooth.Scale + smooth.Bias, 0.0F);
    EXPECT_NE(smooth.Scale, rough.Scale);
}

TEST(Oot3dReflectionDebug,
     SeparatesReflectionWeightFromTemporalReactiveAlpha) {
    using namespace Fast::Oot3d;
    const std::array<float, 4> material{
        0.60F, 0.25F, 0.75F, 1.0F};
    const std::array<float, 4> reflection{
        0.10F, 0.20F, 0.30F, 0.40F};
    EXPECT_EQ(
        ResolveReflectionDebugColor(1U, material, reflection),
        (std::array<float, 3>{0.60F, 0.75F, 0.0F}));
    EXPECT_EQ(
        ResolveReflectionDebugColor(
            1U, {0.60F, 0.25F, 0.75F, 0.0F},
            reflection),
        ResolveReflectionDebugColor(
            1U, material, reflection));
    EXPECT_EQ(
        ResolveReflectionDebugColor(2U, material, reflection),
        (std::array<float, 3>{0.25F, 0.25F, 0.25F}));
    EXPECT_EQ(
        ResolveReflectionDebugColor(3U, material, reflection),
        (std::array<float, 3>{0.10F, 0.20F, 0.30F}));
    EXPECT_EQ(
        ResolveReflectionDebugColor(4U, material, reflection),
        (std::array<float, 3>{0.40F, 0.40F, 0.40F}));
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dReflectionIbl, ComputeShadersCompile) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(
        shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
    const std::array<std::pair<std::string, const char*>, 3> shaders{{
        {Fast::Oot3d::BuildReflectionEnvironmentComputeShader(),
         "reflection_environment.comp"},
        {Fast::Oot3d::BuildReflectionBrdfComputeShader(),
         "reflection_brdf.comp"},
        {Fast::Oot3d::BuildReflectionMaterialResolveComputeShader(),
         "reflection_material_resolve.comp"},
    }};
    for (const auto& [source, name] : shaders) {
        const auto result = compiler.CompileGlslToSpv(
            source, shaderc_compute_shader, name, options);
        EXPECT_EQ(
            result.GetCompilationStatus(),
            shaderc_compilation_status_success)
            << name << ": " << result.GetErrorMessage();
    }
}
#endif

TEST(Oot3dGrassDepth, RasterDepthProgramOnlyWhereDepthIsUnchanged) {
    using namespace Fast::Oot3d;
    GrassPushConstants push;
    EXPECT_TRUE(GrassDepthIsRasterDepth(push));  // no PICA depth mapping
    push.Flags[0] = 1U;
    push.DepthState = {-1.0F, 0.0F, 0.0F, 1.0F};
    EXPECT_TRUE(GrassDepthIsRasterDepth(push));
    push.DepthState = {-1.0F, -3.0517578125e-05F, 0.0F, 1.0F};
    EXPECT_FALSE(GrassDepthIsRasterDepth(push));  // decal offset
    push.DepthState = {-1.0F, 0.0F, 1.0F, 1.0F};
    EXPECT_FALSE(GrassDepthIsRasterDepth(push));  // W-buffering
    push.DepthState = {-0.5F, 0.0F, 0.0F, 1.0F};
    EXPECT_FALSE(GrassDepthIsRasterDepth(push));
    const auto explicitDepth = BuildGrassFragmentShader();
    const auto rasterDepth = BuildGrassFragmentShader(true);
    EXPECT_NE(explicitDepth.find("#ifdef GRASS_RASTER_DEPTH"), std::string::npos);
    EXPECT_EQ(explicitDepth.find("#define GRASS_RASTER_DEPTH"), std::string::npos);
    EXPECT_EQ(rasterDepth.rfind("#version 450\n#define GRASS_RASTER_DEPTH 1\n", 0), 0U);
}

TEST(Oot3dGrassMotion, RequiresConsecutiveMatchingTopology) {
    EXPECT_TRUE(Fast::Oot3d::GrassMotionHistoryMatches(10U,11U,180U,180U));
    EXPECT_FALSE(Fast::Oot3d::GrassMotionHistoryMatches(10U,12U,180U,180U));
    EXPECT_FALSE(Fast::Oot3d::GrassMotionHistoryMatches(10U,11U,180U,162U));
}

TEST(Oot3dGrassQuality, PresetsAlwaysRestoreCoherentPerformanceValues) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.MaxInstancesPerRoom = 499999U;
    settings.DrawDistance = 4999.0F;
    settings.FarDensity = 0.99F;
    ApplyGrassQualityPreset(settings, GrassQuality::Low);
    EXPECT_EQ(settings.Quality, GrassQuality::Low);
    EXPECT_EQ(settings.MaxInstancesPerRoom, 25000U);
    EXPECT_FLOAT_EQ(settings.DrawDistance, 600.0F);
    EXPECT_FLOAT_EQ(settings.FarDensity, 0.15F);

    settings.MaxInstancesPerRoom = 12345U;
    ApplyGrassQualityPreset(settings, GrassQuality::Medium);
    EXPECT_EQ(settings.MaxInstancesPerRoom, 75000U);
    EXPECT_FLOAT_EQ(settings.DrawDistance, 1200.0F);
    EXPECT_LE(settings.LodStartFraction,
              settings.LodEndFraction);
}

TEST(Oot3dGrassDensity, SupportsAndValidates4096BladesPerSquareMetre) {
    using namespace Fast::Oot3d;
    EXPECT_FLOAT_EQ(
        kMaximumGrassInstancesPerSquareMeter, 4096.0F);
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    settings.Grass.Rules.emplace_back();
    settings.Grass.Rules.front().Target.Rgba8Hash = 1U;
    settings.Grass.Generation.InstancesPerSquareMeter =
        8192.0F;
    const auto validated = GraphicsSettingsService::Validate(
        settings, GraphicsCapabilities{});
    ASSERT_EQ(validated.Value.Grass.Rules.size(), 1U);
    EXPECT_FLOAT_EQ(
        validated.Value.Grass.Generation
            .InstancesPerSquareMeter,
        4096.0F);
}

TEST(Oot3dGrassShape, BoundsStaticCurvatureAndDynamicBendingTogether) {
    using namespace Fast::Oot3d;
    EXPECT_FLOAT_EQ(GrassBladeRadiusScale(0.0F, 0.0F, 0.0F), 1.0F);
    EXPECT_GT(GrassBladeRadiusScale(2.0F, 1.0F, 0.0F), 4.35F);
    EXPECT_GT(GrassBladeRadiusScale(2.0F, 1.0F, 2.0F),
              GrassBladeRadiusScale(2.0F, 1.0F, 0.0F));
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    settings.Grass.Appearance.BladeCurvature = 100.0F;
    settings.Grass.Appearance.BladeDroop = 2.0F;
    settings.Grass.Appearance.ShapeVariation = -1.0F;
    settings.Grass.Appearance.BladeTwistDegrees = 300.0F;
    const auto shape = GraphicsSettingsService::Validate(settings, {}).Value.Grass.Appearance;
    EXPECT_FLOAT_EQ(shape.BladeCurvature, 2.0F);
    EXPECT_FLOAT_EQ(shape.BladeDroop, 0.95F);
    EXPECT_FLOAT_EQ(shape.ShapeVariation, 0.0F);
    EXPECT_FLOAT_EQ(shape.BladeTwistDegrees, 180.0F);
}

TEST(Oot3dPicaToon, UsesOneIsotropicKernelAndResolutionIndependentWidth) {
    using namespace Fast::Oot3d;
    const auto kernel = ToonOutlineShaderLibrary();
    EXPECT_NE(BuildSceneCompositeComputeShader().find(kernel), std::string::npos);
    EXPECT_NE(BuildPicaScanoutFragmentShader().find(kernel), std::string::npos);
    EXPECT_NE(kernel.find("directions[8]"), std::string::npos);
    EXPECT_NE(kernel.find("depth_sensitivity <= 0.0 && normal_sensitivity <= 0.0"), std::string::npos);
    EXPECT_FLOAT_EQ(ToonOutlineRenderWidth(3.0F, 1920, 1080), 3.0F);
    EXPECT_FLOAT_EQ(ToonOutlineRenderWidth(3.0F, 1080, 1920), 3.0F);
    EXPECT_FLOAT_EQ(ToonOutlineRenderWidth(3.0F, 3840, 2160), 6.0F);
    EXPECT_FLOAT_EQ(ToonOutlineRenderWidth(3.0F, 960, 540), 1.5F);
}

TEST(Oot3dGrassSurfaceEligibility,
     RejectsNonWorldAndTranslucentTextureDraws) {
    Fast::Oot3d::GrassSurfaceDrawInfo draw{
        true, true, true, true, true, true,
        true, true, false, true, true, false, true};
    EXPECT_TRUE(
        Fast::Oot3d::IsGrassSurfaceDrawEligible(draw));
    draw.WorldSurface = false;
    EXPECT_FALSE(
        Fast::Oot3d::IsGrassSurfaceDrawEligible(draw));
    draw.WorldSurface = true;
    draw.DepthWrite = false;
    EXPECT_FALSE(
        Fast::Oot3d::IsGrassSurfaceDrawEligible(draw));
    draw.DepthWrite = true;
    draw.TranslucentBlend = true;
    EXPECT_FALSE(
        Fast::Oot3d::IsGrassSurfaceDrawEligible(draw));
    draw.TranslucentBlend = false;
    draw.PerspectiveProjection = false;
    EXPECT_FALSE(
        Fast::Oot3d::IsGrassSurfaceDrawEligible(draw));
    EXPECT_FALSE(
        Fast::Oot3d::PicaFragmentSamplesGrassTexture0(
            "layout(binding=1) uniform sampler2D pica_texture0;"));
    EXPECT_TRUE(
        Fast::Oot3d::PicaFragmentSamplesGrassTexture0(
            "vec4 c = texture(pica_texture0, uv);"));
    EXPECT_TRUE(
        Fast::Oot3d::PicaFragmentSamplesGrassTexture0(
            "vec4 c = textureProj(pica_texture0, uvw);"));
}

TEST(Oot3dGrassPrimitiveTopology,
     ExpandsEveryPicaWorldTopologyForSurfaceExtraction) {
    using Topology = Oot3d::Renderer::PicaTopology;
    const std::vector<uint32_t> source{0U, 1U, 2U, 3U};

    EXPECT_EQ(
        Fast::Oot3d::ExpandGrassPrimitiveIndices(
            source, Topology::TriangleList),
        (std::vector<uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(
        Fast::Oot3d::ExpandGrassPrimitiveIndices(
            source, Topology::TriangleStrip),
        (std::vector<uint32_t>{
            0U, 1U, 2U, 2U, 1U, 3U}));
    EXPECT_EQ(
        Fast::Oot3d::ExpandGrassPrimitiveIndices(
            source, Topology::TriangleFan),
        (std::vector<uint32_t>{
            0U, 1U, 2U, 0U, 2U, 3U}));
    EXPECT_EQ(
        Fast::Oot3d::ExpandGrassPrimitiveIndices(
            source, Topology::GeometryShader),
        (std::vector<uint32_t>{0U, 1U, 2U}));
    EXPECT_TRUE(
        Fast::Oot3d::IsGrassPrimitiveTopologySupported(
            Topology::GeometryShader));
}

TEST(Oot3dGrassTextureWrap, HonorsMaterialAndOverrideModes) {
    using namespace Fast::Oot3d;
    EXPECT_EQ(
        ResolveGrassTextureWrap(
            GrassWrapOverride::Material,
            GrassTextureWrap::Clamp),
        GrassTextureWrap::Clamp);
    EXPECT_EQ(
        ResolveGrassTextureWrap(
            GrassWrapOverride::Repeat,
            GrassTextureWrap::Clamp),
        GrassTextureWrap::Repeat);
    EXPECT_FLOAT_EQ(
        WrapGrassTextureCoordinate(
            1.25F, GrassTextureWrap::Clamp),
        1.0F);
    EXPECT_FLOAT_EQ(
        WrapGrassTextureCoordinate(
            1.25F, GrassTextureWrap::Repeat),
        0.25F);
    EXPECT_FLOAT_EQ(
        WrapGrassTextureCoordinate(
            1.25F, GrassTextureWrap::Mirror),
        0.75F);
    EXPECT_FLOAT_EQ(
        WrapGrassTextureCoordinate(
            -0.25F, GrassTextureWrap::Mirror),
        0.25F);
}

TEST(Oot3dGrassMaskLevels,
     RemapsSelectedChannelToContinuousPlacementProbability) {
    using namespace Fast::Oot3d;
    GrassPlacementRule rule;
    rule.InputBlack = 0.25F;
    rule.InputWhite = 0.75F;
    rule.ResponseExponent = 2.0F;
    rule.OutputBlack = 0.1F;
    rule.OutputWhite = 0.9F;
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.0F, rule), 0.1F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.25F, rule), 0.1F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.50F, rule), 0.3F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.75F, rule), 0.9F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(1.0F, rule), 0.9F);
    rule.Invert = true;
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.25F, rule), 0.9F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.75F, rule), 0.1F);
}

TEST(Oot3dGrassMaskLevels,
     TreatsPreviewBlackAsExactAbsence) {
    using namespace Fast::Oot3d;
    GrassPlacementRule rule;
    rule.InputBlack = 0.0F;
    rule.InputWhite = 1.0F;
    rule.ResponseExponent = 1.0F;
    rule.OutputBlack = 0.0F;
    rule.OutputWhite = 1.0F;
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.0F, rule), 0.0F);
    EXPECT_FLOAT_EQ(
        EvaluateGrassMaskLevel(0.25F / 255.0F, rule),
        0.0F);
    EXPECT_GT(
        EvaluateGrassMaskLevel(1.0F / 255.0F, rule),
        0.0F);
}

TEST(Oot3dGrassMaskLevels,
     ActiveRectanglesKeepTheMaskOnlyInsideThem) {
    using namespace Fast::Oot3d;
    GrassPlacementRule rule;
    rule.InputBlack = 0.0F;
    rule.InputWhite = 1.0F;
    // None listed: all of the mask applies.
    EXPECT_TRUE(GrassMaskPositionActive(rule, 0.9F, 0.9F));
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(1.0F, rule, 0.9F, 0.9F), 1.0F);
    rule.ActiveRects = {{0.1F, 0.1F, 0.4F, 0.3F}, {0.6F, 0.5F, 0.9F, 1.0F}};
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(1.0F, rule, 0.2F, 0.2F), 1.0F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(0.5F, rule, 0.8F, 0.9F), 0.5F);
    // Outside every rectangle: zero, as for a black mask.
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(1.0F, rule, 0.5F, 0.5F), 0.0F);
    EXPECT_FLOAT_EQ(EvaluateGrassMaskLevel(1.0F, rule, 0.2F, 0.4F), 0.0F);
    // Inactive texels are never interior for midrange grouping.
    const std::vector<uint8_t> white(16U * 16U, 255U);
    const GrassMaskInterior interior(16U, 16U, white, [](uint8_t) { return true; },
                                     [&](float u, float v) { return GrassMaskPositionActive(rule, u, v); });
    EXPECT_TRUE(interior.Contains({0.15F, 0.15F}, {0.35F, 0.25F}));
    EXPECT_FALSE(interior.Contains({0.45F, 0.45F}, {0.55F, 0.55F}));
}

namespace {

// A ground-like texture: smooth blobs plus fine grain, from a seed; tinted by
// a green gain.
std::vector<uint8_t> GrassTwinPattern(uint32_t seed, uint32_t side, float greenGain = 1.0F) {
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> unit(0.0F, 1.0F);
    std::vector<float> coarse(16U * 16U);
    for (auto& value : coarse) value = unit(random);
    std::vector<uint8_t> rgba(size_t(side) * side * 4U);
    std::mt19937 grain(seed * 7U + 1U);
    for (uint32_t y = 0; y < side; ++y)
        for (uint32_t x = 0; x < side; ++x) {
            const float fx = float(x) * 16.0F / float(side), fy = float(y) * 16.0F / float(side);
            const uint32_t x0 = uint32_t(fx) % 16U, y0 = uint32_t(fy) % 16U;
            const uint32_t x1 = (x0 + 1U) % 16U, y1 = (y0 + 1U) % 16U;
            const float ax = fx - std::floor(fx), ay = fy - std::floor(fy);
            const float blob = std::lerp(std::lerp(coarse[y0 * 16U + x0], coarse[y0 * 16U + x1], ax),
                                         std::lerp(coarse[y1 * 16U + x0], coarse[y1 * 16U + x1], ax), ay);
            const float value = 0.6F * blob + 0.4F * unit(grain);
            uint8_t* pixel = rgba.data() + (size_t(y) * side + x) * 4U;
            pixel[0] = uint8_t(std::lround(60.0F * value));
            pixel[1] = uint8_t(std::clamp(std::lround(greenGain * (60.0F + 140.0F * value)), 0L, 255L));
            pixel[2] = uint8_t(std::lround(30.0F * value));
            pixel[3] = 255U;
        }
    return rgba;
}

std::vector<uint8_t> GrassTwinHalf(const std::vector<uint8_t>& rgba, uint32_t side) {
    std::vector<uint8_t> half(size_t(side / 2U) * (side / 2U) * 4U);
    for (uint32_t y = 0; y < side / 2U; ++y)
        for (uint32_t x = 0; x < side / 2U; ++x)
            for (uint32_t c = 0; c < 4U; ++c) {
                uint32_t sum = 0U;
                for (uint32_t k = 0; k < 4U; ++k)
                    sum += rgba[((size_t(2U * y + k / 2U)) * side + 2U * x + k % 2U) * 4U + c];
                half[(size_t(y) * (side / 2U) + x) * 4U + c] = uint8_t((sum + 2U) / 4U);
            }
    return half;
}

class FakeGrassSignatures final : public Fast::Oot3d::GrassTextureSignatureSource {
  public:
    std::shared_ptr<const Fast::Oot3d::GrassTextureSignature> Find(uint64_t hash, bool persist) override {
        ++Finds;
        if (persist) Persisted.push_back(hash);
        const auto found = Signatures.find(hash);
        return found == Signatures.end() ? nullptr : found->second;
    }
    uint64_t Generation() const override { return Serial; }
    void Add(uint64_t hash, uint32_t side, const std::vector<uint8_t>& rgba) {
        Signatures[hash] = std::make_shared<const Fast::Oot3d::GrassTextureSignature>(
            Fast::Oot3d::ComputeGrassTextureSignature(uint16_t(side), uint16_t(side), rgba));
        ++Serial;
    }
    std::unordered_map<uint64_t, std::shared_ptr<const Fast::Oot3d::GrassTextureSignature>> Signatures;
    std::vector<uint64_t> Persisted;
    uint64_t Serial = 1U;
    size_t Finds = 0U;
};

} // namespace

TEST(Oot3dGrassTextureTwins, SameImageScoresAboveDifferentImages) {
    using namespace Fast::Oot3d;
    const auto original = GrassTwinPattern(11U, 128U);
    const auto sig = ComputeGrassTextureSignature(128U, 128U, original);
    // Another tint, half the size, a patch painted over: the same image.
    const auto tinted = ComputeGrassTextureSignature(128U, 128U, GrassTwinPattern(11U, 128U, 0.55F));
    const auto half = ComputeGrassTextureSignature(64U, 64U, GrassTwinHalf(original, 128U));
    auto patched = original;
    for (uint32_t y = 70U; y < 100U; ++y)
        for (uint32_t x = 10U; x < 60U; ++x)
            for (uint32_t c = 0; c < 3U; ++c) patched[(size_t(y) * 128U + x) * 4U + c] = 0U;
    const auto patchedSig = ComputeGrassTextureSignature(128U, 128U, patched);
    EXPECT_GT(GrassTextureTwinScore(sig, tinted), 0.95F);
    EXPECT_GT(GrassTextureTwinScore(sig, half), kGrassTextureTwinThreshold);
    EXPECT_GT(GrassTextureTwinScore(sig, patchedSig), kGrassTextureTwinThreshold);
    // Another pattern of the same kind and colours: not the same image.
    const auto other = ComputeGrassTextureSignature(128U, 128U, GrassTwinPattern(12U, 128U));
    EXPECT_LT(GrassTextureTwinScore(sig, other), 0.3F);
    // Shapes that cannot be the same image.
    const auto quarter = ComputeGrassTextureSignature(32U, 32U, GrassTwinHalf(GrassTwinHalf(original, 128U), 64U));
    EXPECT_EQ(GrassTextureTwinScore(sig, quarter), -1.0F);
    const auto wide = ComputeGrassTextureSignature(128U, 64U, std::vector<uint8_t>(original.begin(), original.begin() + 128U * 64U * 4U));
    EXPECT_EQ(GrassTextureTwinScore(sig, wide), -1.0F);
}

TEST(Oot3dGrassTextureTwins, InheritedRuleMapsTheTwinsTintOntoTheSource) {
    using namespace Fast::Oot3d;
    const auto source = ComputeGrassTextureSignature(128U, 128U, GrassTwinPattern(5U, 128U));
    const auto twin = ComputeGrassTextureSignature(128U, 128U, GrassTwinPattern(5U, 128U, 0.5F));
    GrassPlacementRule rule;
    rule.RuleId = 3U;
    rule.Target.Rgba8Hash = 0xAAU;
    rule.Target.Width = rule.Target.Height = 128U;
    rule.Channel = GrassSampleChannel::Green;
    rule.InputBlack = 0.4F;
    rule.InputWhite = 0.7F;
    rule.ActiveRects = {{0.1F, 0.1F, 0.6F, 0.6F}};
    const auto inherited = DeriveGrassTwinRule(rule, source, twin, 0xBBU);
    EXPECT_EQ(inherited.Target.Rgba8Hash, 0xBBU);
    EXPECT_EQ(inherited.InheritedFrom, 3U);
    EXPECT_NE(inherited.RuleId & kGrassInheritedRuleIdBit, 0U);
    EXPECT_EQ(inherited.ActiveRects, rule.ActiveRects);
    // The same texel: green g on the source, about g / 2 on the twin.
    for (const float green : {0.35F, 0.5F, 0.6F, 0.72F}) {
        EXPECT_NEAR(EvaluateGrassMaskLevel(green * 0.5F, inherited),
                    EvaluateGrassMaskLevel(green, rule), 0.06F) << green;
    }
    // Its own rule from it keeps the mask (the mapping folded into levels).
    for (const bool invert : {false, true}) {
        auto from = inherited;
        from.Invert = invert;
        const auto own = OwnGrassRuleFromInherited(from, 9U);
        EXPECT_EQ(own.RuleId, 9U);
        EXPECT_EQ(own.InheritedFrom, 0U);
        EXPECT_FLOAT_EQ(own.SampleScale, 1.0F);
        for (const float sample : {0.18F, 0.25F, 0.3F, 0.34F})
            EXPECT_NEAR(EvaluateGrassMaskLevel(sample, own), EvaluateGrassMaskLevel(sample, from), 1.0e-3F)
                << invert << " " << sample;
    }
}

TEST(Oot3dGrassTextureTwins, SignatureStoreRoundTrips) {
    using namespace Fast::Oot3d;
    const auto a = ComputeGrassTextureSignature(128U, 128U, GrassTwinPattern(1U, 128U));
    const auto b = ComputeGrassTextureSignature(64U, 64U, GrassTwinPattern(2U, 64U));
    std::stringstream stream;
    WriteGrassTextureSignature(stream, 0x1234U, a);
    WriteGrassTextureSignature(stream, 0x5678U, b);
    std::string bytes = stream.str();
    std::stringstream input(bytes);
    const auto read = ReadGrassTextureSignatures(input);
    ASSERT_EQ(read.size(), 2U);
    EXPECT_EQ(read[0].first, 0x1234U);
    EXPECT_EQ(read[0].second, a);
    EXPECT_EQ(read[1].second, b);
    // A truncated last record is dropped, the others kept.
    std::stringstream truncated(bytes.substr(0, bytes.size() - 10U));
    EXPECT_EQ(ReadGrassTextureSignatures(truncated).size(), 1U);
}

TEST(Oot3dGrassTextureTwins, TexturesWithoutTheirOwnRuleInheritTheirTwinsRule) {
    using namespace Fast::Oot3d;
    FakeGrassSignatures signatures;
    auto& twins = GrassTwinRules::Instance();
    twins.SetSource(&signatures);
    const auto pattern = GrassTwinPattern(21U, 128U);
    signatures.Add(0xB0U, 128U, GrassTwinPattern(21U, 128U, 0.6F));  // twin
    signatures.Add(0xC0U, 128U, GrassTwinPattern(22U, 128U));        // another image
    std::vector<GrassPlacementRule> rules(1);
    rules[0].RuleId = 1U;
    rules[0].Target.Rgba8Hash = 0xA0U;
    rules[0].Target.Width = rules[0].Target.Height = 128U;
    rules[0].Target.MapperSlotMask = 0x01U;
    // The rule's texture not known yet: nothing to inherit from.
    EXPECT_FALSE(twins.Configured(rules, 0xB0U, 128U, 128U, 0U));
    signatures.Add(0xA0U, 128U, pattern);
    EXPECT_TRUE(twins.Configured(rules, 0xA0U, 128U, 128U, 0U));
    EXPECT_TRUE(twins.Configured(rules, 0xB0U, 128U, 128U, 0U));
    EXPECT_FALSE(twins.Configured(rules, 0xB0U, 128U, 128U, 1U));  // the source's slots
    EXPECT_FALSE(twins.Configured(rules, 0xC0U, 128U, 128U, 0U));
    EXPECT_NE(std::find(signatures.Persisted.begin(), signatures.Persisted.end(), 0xA0U),
              signatures.Persisted.end());
    auto inherited = twins.Inherited(rules, 0xB0U, 128U, 128U);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_EQ(inherited->InheritedFrom, 1U);
    EXPECT_EQ(inherited->Target.Rgba8Hash, 0xB0U);
    ASSERT_EQ(twins.AllInherited(rules).size(), 1U);
    // The source's settings are read when asked, without matching again.
    const size_t finds = signatures.Finds;
    rules[0].InputBlack = 0.123F;
    inherited = twins.Inherited(rules, 0xB0U, 128U, 128U);
    ASSERT_TRUE(inherited.has_value());
    EXPECT_FLOAT_EQ(inherited->InputBlack, 0.123F);
    EXPECT_EQ(signatures.Finds, finds);
    // A rule of its own wins.
    rules.push_back(rules[0]);
    rules[1].RuleId = 2U;
    rules[1].Target.Rgba8Hash = 0xB0U;
    EXPECT_FALSE(twins.Inherited(rules, 0xB0U, 128U, 128U).has_value());
    EXPECT_TRUE(twins.Configured(rules, 0xB0U, 128U, 128U, 0U));
    EXPECT_TRUE(twins.AllInherited(rules).empty());
    twins.SetSource(nullptr);
}

TEST(Oot3dGrassTextureCoordinates,
     DecodesCmbUvScaleMatrixAndNativeVFlip) {
    using namespace Fast::Oot3d;
    constexpr std::string_view vertexSource = R"glsl(
void main() {
    reg_tmp10.xy = (uniforms.f[91].xxxx * pica_input3.xyyy).xy;
    reg_tmp3.x = dot(uniforms.f[10].xyzw, reg_tmp10.xyzw);
    reg_tmp3.y = dot(uniforms.f[11].xyzw, reg_tmp10.xyzw);
    pica_output2.xy = (reg_tmp3.xyyy).xy;
}
)glsl";
    constexpr std::string_view fragmentSource = R"glsl(
void main() {
    pica_color = texture(pica_texture0, vec2(
        pica_texcoord0.x, 1.0 - pica_texcoord0.y));
}
)glsl";
    constexpr size_t floatOffset =
        16U + 4U * 4U * sizeof(uint32_t);
    std::vector<uint8_t> uniformBytes(
        floatOffset + 96U * 4U * sizeof(float));
    const uint32_t booleanMask = 2U | 64U;
    std::memcpy(
        uniformBytes.data(), &booleanMask,
        sizeof(booleanMask));
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[10] = {1.0F, 0.0F, 0.0F, 0.25F};
    uniforms[11] = {0.0F, 1.0F, 0.0F, 0.125F};
    uniforms[89][0] = 0.0F;
    uniforms[91][0] = 1.0F / 32768.0F;
    uniforms[92][0] = 1.0F;
    uniforms[93][1] = 1.0F;
    uniforms[95][0] = 3.0F;
    uniforms[95][1] = 4.0F;
    std::memcpy(
        uniformBytes.data() + floatOffset,
        uniforms.data(), sizeof(uniforms));

    const auto transform =
        DecodePicaGrassTextureCoordinateTransform(
            vertexSource, fragmentSource,
            uniformBytes, 3U);
    ASSERT_TRUE(transform.Applied());
    const auto uv =
        ApplyPicaGrassTextureCoordinateTransform(
            transform, {8192.0F, 16384.0F});
    EXPECT_FLOAT_EQ(uv[0], 0.5F);
    EXPECT_FLOAT_EQ(uv[1], 0.375F);
    EXPECT_NE(
        PicaGrassTextureCoordinateTransformVersion(transform),
        0U);
}

TEST(Oot3dGrassTextureCoordinates,
     DeclinesProceduralAndProjectiveCoordinates) {
    using namespace Fast::Oot3d;
    constexpr std::string_view vertexSource = R"glsl(
void main() {
    reg_tmp10.xy = (uniforms.f[91].xxxx * pica_input3.xyyy).xy;
    reg_tmp3.x = dot(uniforms.f[10].xyzw, reg_tmp10.xyzw);
    reg_tmp3.y = dot(uniforms.f[11].xyzw, reg_tmp10.xyzw);
    pica_output2.xy = (reg_tmp3.xyyy).xy;
}
)glsl";
    constexpr std::string_view fragmentSource =
        "texture(pica_texture0, vec2(pica_texcoord0.x, "
        "1.0 - pica_texcoord0.y))";
    constexpr size_t floatOffset =
        16U + 4U * 4U * sizeof(uint32_t);
    std::vector<uint8_t> uniformBytes(
        floatOffset + 96U * 4U * sizeof(float));
    const uint32_t booleanMask = 2U | 64U;
    std::memcpy(
        uniformBytes.data(), &booleanMask,
        sizeof(booleanMask));
    std::array<std::array<float, 4>, 96> uniforms{};
    uniforms[10][0] = 1.0F;
    uniforms[11][1] = 1.0F;
    uniforms[89][0] = 0.0F;
    uniforms[91][0] = 1.0F;
    uniforms[92][0] = 3.0F;
    uniforms[93][1] = 1.0F;
    uniforms[95][0] = 3.0F;
    uniforms[95][1] = 4.0F;
    std::memcpy(
        uniformBytes.data() + floatOffset,
        uniforms.data(), sizeof(uniforms));
    EXPECT_EQ(
        DecodePicaGrassTextureCoordinateTransform(
            vertexSource, fragmentSource,
            uniformBytes, 3U).Eligibility,
        PicaGrassTextureCoordinateEligibility::
            ProceduralCoordinates);

    uniforms[92][0] = 1.0F;
    std::memcpy(
        uniformBytes.data() + floatOffset,
        uniforms.data(), sizeof(uniforms));
    EXPECT_EQ(
        DecodePicaGrassTextureCoordinateTransform(
            vertexSource,
            "textureProj(pica_texture0, vec3(0.0))",
            uniformBytes, 3U).Eligibility,
        PicaGrassTextureCoordinateEligibility::
            UnsupportedFragmentSampling);
}

TEST(Oot3dGrassTextureCoordinates,
     VersionCanonicalizesSignedZero) {
    using namespace Fast::Oot3d;
    PicaGrassTextureCoordinateTransform positiveZero;
    positiveZero.RawToSample = {
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F};
    auto negativeZero = positiveZero;
    negativeZero.RawToSample[1] = -0.0F;
    negativeZero.RawToSample[3] = -0.0F;
    negativeZero.RawToSample[5] = -0.0F;

    EXPECT_EQ(
        PicaGrassTextureCoordinateTransformVersion(positiveZero),
        PicaGrassTextureCoordinateTransformVersion(negativeZero));

    negativeZero.RawToSample[1] = 1.0e-8F;
    EXPECT_NE(
        PicaGrassTextureCoordinateTransformVersion(positiveZero),
        PicaGrassTextureCoordinateTransformVersion(negativeZero));
}

TEST(Oot3dGrassMotion, ComputesPreviousMinusCurrentUv) {
    const auto motion=Fast::Oot3d::ComputeGrassMotionUv(
        {0.4F,-0.2F,0,2.0F},{0.0F,0.2F,0,1.0F},true);
    EXPECT_NEAR(motion[0],-0.1F,1.0e-6F);
    EXPECT_NEAR(motion[1],0.15F,1.0e-6F);
    EXPECT_EQ(Fast::Oot3d::ComputeGrassMotionUv(
        {0,0,0,1},{0,0,0,1},false),(std::array<float,2>{}));
}

TEST(Oot3dGrassBladeGeometry, HonorsSegmentCountAndEndpoints) {
    using namespace Fast::Oot3d;
    for (const uint8_t segments : {1U, 2U, 5U, 12U}) {
        std::vector<GrassBladeGeometryVertex> vertices;
        AppendGrassBladeGeometry(
            vertices,
            {{10.0F, 20.0F, 30.0F}, 2.0F, 40.0F,
             {1.0F, 0.0F}, {4.0F, 8.0F}, segments});
        ASSERT_EQ(vertices.size(), GrassBladeVertexCount(segments));
        EXPECT_FLOAT_EQ(vertices.front().Position[1], 20.0F);
        EXPECT_FLOAT_EQ(vertices.front().HeightFactor, 0.0F);
        EXPECT_FLOAT_EQ(vertices.back().Position[0], 14.0F);
        EXPECT_FLOAT_EQ(vertices.back().Position[1], 60.0F);
        EXPECT_FLOAT_EQ(vertices.back().Position[2], 38.0F);
        EXPECT_FLOAT_EQ(vertices.back().HeightFactor, 1.0F);
    }
}

TEST(Oot3dGrassPlacementCache,
     FingerprintIncludesUnifiedGenerationSettings) {
    Fast::Oot3d::GrassPlacementRule rule;
    Fast::Oot3d::GrassGenerationSettings generation;
    const uint64_t baseline =
        Fast::Oot3d::GrassPlacementRuleVersion(
            rule, generation);
    generation.BladeHeightMax += 1.0F;
    EXPECT_NE(
        Fast::Oot3d::GrassPlacementRuleVersion(
            rule, generation),
        baseline);
}

TEST(Oot3dGrassPlacement,
     EnforcesSpacingAndBuildsDeterministicSpatialClusters) {
    using namespace Fast::Oot3d;
    const std::array<GrassSourceVertex, 3> vertices{{
        {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 0.0F}},
        {{1000.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {1.0F, 0.0F}},
        {{0.0F, 0.0F, 1000.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 1.0F}},
    }};
    const std::array<uint32_t, 3> indices{0U, 1U, 2U};
    GrassSourceSurface surface;
    surface.GeometryId = 11U;
    surface.ContentVersion = 3U;
    surface.TransformBakedIntoVertices = true;
    surface.Vertices = vertices;
    surface.Indices = indices;
    GrassScalarMask mask;
    mask.Width = 1U;
    mask.Height = 1U;
    mask.Samples = {255U};
    GrassPlacementRule rule;
    rule.InputBlack = 0.0F;
    rule.InputWhite = 1.0F;
    GrassGenerationSettings generation;
    generation.InstancesPerSquareMeter = 80.0F;
    generation.MinimumSpacing = 45.0F;
    generation.Seed = 77U;
    const auto first =
        GrassSurfaceExtractor::Extract(
            surface, rule, generation, mask, 1000U);
    const auto second =
        GrassSurfaceExtractor::Extract(
            surface, rule, generation, mask, 1000U);
    ASSERT_FALSE(first.empty());
    ASSERT_EQ(first.size(), second.size());
    for (size_t index = 0U; index < first.size(); ++index) {
        EXPECT_EQ(first[index].StableId, second[index].StableId);
        EXPECT_EQ(first[index].LocalPosition,
                  second[index].LocalPosition);
        EXPECT_EQ(first[index].WidthAxis,
                  second[index].WidthAxis);
        for (size_t other = index + 1U; other < first.size();
             ++other) {
            const float dx =
                first[index].LocalPosition[0] -
                first[other].LocalPosition[0];
            const float dy =
                first[index].LocalPosition[1] -
                first[other].LocalPosition[1];
            const float dz =
                first[index].LocalPosition[2] -
                first[other].LocalPosition[2];
            EXPECT_GE(
                dx * dx + dy * dy + dz * dz,
                generation.MinimumSpacing *
                        generation.MinimumSpacing -
                    1.0e-3F);
        }
    }
    const auto placement =
        BuildGrassPlacementSet(first, 200.0F);
    EXPECT_EQ(placement.Anchors.size(), first.size());
    EXPECT_GT(placement.Clusters.size(), 1U);
    for (const auto& cluster : placement.Clusters) {
        const size_t end =
            cluster.FirstAnchor + cluster.AnchorCount;
        for (size_t index =
                 cluster.FirstAnchor + 1U;
             index < end; ++index) {
            EXPECT_LE(
                GrassStableVisibilityValue(
                    placement.Anchors[index - 1U].StableId),
                GrassStableVisibilityValue(
                    placement.Anchors[index].StableId));
        }
    }
}

TEST(Oot3dGrassPlacement,
     ProducesNoAnchorsFromAnExactBlackMask) {
    using namespace Fast::Oot3d;
    const std::array<GrassSourceVertex, 3> vertices{{
        {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 0.0F}},
        {{1000.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {1.0F, 0.0F}},
        {{0.0F, 0.0F, 1000.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 1.0F}},
    }};
    const std::array<uint32_t, 3> indices{0U, 1U, 2U};
    GrassSourceSurface surface;
    surface.GeometryId = 12U;
    surface.ContentVersion = 4U;
    surface.TransformBakedIntoVertices = true;
    surface.Vertices = vertices;
    surface.Indices = indices;
    GrassScalarMask mask;
    mask.Width = 1U;
    mask.Height = 1U;
    mask.Samples = {0U};
    GrassPlacementRule rule;
    rule.OutputBlack = 0.0F;
    rule.OutputWhite = 1.0F;
    GrassGenerationSettings generation;
    generation.InstancesPerSquareMeter = 1024.0F;
    generation.MinimumSpacing = 0.0F;
    EXPECT_TRUE(
        GrassSurfaceExtractor::Extract(
            surface, rule, generation, mask, 10000U)
            .empty());
}

TEST(Oot3dGrassPlacementCache,
     ReplacesSupersededVersionsOfTheSameSurfaceRule) {
    using namespace Fast::Oot3d;
    GrassPlacementCache cache;
    const GrassPlacementKey first{
        11U, 101U, 22U, 7U};
    const GrassPlacementKey replacement{
        11U, 102U, 22U, 7U};
    EXPECT_NE(
        cache.Replace(first, GrassPlacementSet{}),
        nullptr);
    EXPECT_NE(cache.Find(first), nullptr);
    EXPECT_NE(
        cache.Replace(replacement, GrassPlacementSet{}),
        nullptr);
    EXPECT_EQ(cache.Find(first), nullptr);
    EXPECT_NE(cache.Find(replacement), nullptr);
}

TEST(Oot3dGrassPlacement,
     SeparatesIndividualJitterFromGroupDistribution) {
    using namespace Fast::Oot3d;
    const std::array<GrassSourceVertex, 3> vertices{{
        {{0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 0.0F}},
        {{1000.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
         {1.0F, 0.0F}},
        {{0.0F, 0.0F, 1000.0F}, {0.0F, 1.0F, 0.0F},
         {0.0F, 1.0F}},
    }};
    const std::array<uint32_t, 3> indices{0U, 1U, 2U};
    GrassSourceSurface surface;
    surface.GeometryId = 31U;
    surface.ContentVersion = 7U;
    surface.TransformBakedIntoVertices = true;
    surface.Vertices = vertices;
    surface.Indices = indices;
    GrassScalarMask mask;
    mask.Width = 1U;
    mask.Height = 1U;
    mask.Samples = {255U};

    GrassPlacementRule source;
    source.InputBlack = 0.0F;
    source.InputWhite = 1.0F;
    GrassGenerationSettings stratifiedGeneration;
    stratifiedGeneration.InstancesPerSquareMeter = 10.0F;
    stratifiedGeneration.MinimumSpacing = 0.0F;
    stratifiedGeneration.IndividualRandomness = 0.0F;
    const auto stratified = GrassSurfaceExtractor::Extract(
        surface, source, stratifiedGeneration, mask, 1000U);

    auto jitteredGeneration = stratifiedGeneration;
    jitteredGeneration.IndividualRandomness = 1.0F;
    const auto jittered = GrassSurfaceExtractor::Extract(
        surface, source, jitteredGeneration, mask, 1000U);
    ASSERT_EQ(stratified.size(), jittered.size());
    ASSERT_FALSE(stratified.empty());
    EXPECT_NE(stratified.front().LocalPosition,
              jittered.front().LocalPosition);
    EXPECT_NE(
        GrassPlacementRuleVersion(
            source, stratifiedGeneration),
        GrassPlacementRuleVersion(
            source, jitteredGeneration));

    auto clusteredGeneration = jitteredGeneration;
    clusteredGeneration.ClusterStrength = 1.0F;
    clusteredGeneration.ClusterScale = 150.0F;
    clusteredGeneration.ClusterCoverage = 0.10F;
    clusteredGeneration.Seed = 19U;
    const auto clustered = GrassSurfaceExtractor::Extract(
        surface, source, clusteredGeneration, mask, 1000U);
    const auto clusteredAgain = GrassSurfaceExtractor::Extract(
        surface, source, clusteredGeneration, mask, 1000U);
    EXPECT_EQ(clustered.size(), clusteredAgain.size());
    EXPECT_LT(clustered.size(), jittered.size());
    EXPECT_NE(
        GrassPlacementRuleVersion(
            source, jitteredGeneration),
        GrassPlacementRuleVersion(
            source, clusteredGeneration));
}

TEST(Oot3dGrassTextureColor, UsesAlphaWeightedAverage) {
    auto& cache =
        Fast::Oot3d::GrassTextureSourceCache::Instance();
    cache.Clear();
    const std::array<uint8_t, 8> pixels{
        255U, 32U, 16U, 255U,
        0U, 0U, 255U, 0U};
    cache.ObserveDecoded(91U, 2U, 1U, pixels);
    const auto redMask =
        cache.AcquireMaskShared(
            91U, Fast::Oot3d::GrassSampleChannel::Red);
    const auto redMaskAgain =
        cache.AcquireMaskShared(
            91U, Fast::Oot3d::GrassSampleChannel::Red);
    ASSERT_NE(redMask, nullptr);
    EXPECT_EQ(redMask, redMaskAgain);
    EXPECT_EQ(
        redMask->Samples,
        (std::vector<uint8_t>{255U, 0U}));
    const auto average = cache.AcquireAverageColor(91U);
    ASSERT_TRUE(average.has_value());
    EXPECT_FLOAT_EQ((*average)[0], 1.0F);
    EXPECT_NEAR((*average)[1], 32.0F / 255.0F, 1.0e-6F);
    EXPECT_NEAR((*average)[2], 16.0F / 255.0F, 1.0e-6F);
    cache.Clear();
}

TEST(Oot3dGrassTextureColor, GridUsesOnlyTwoNearestDistanceWeightedPoints) {
    using namespace Fast::Oot3d;
    auto grid = std::make_shared<GrassTextureColorGrid>();
    for (auto& rgb : grid->Rgb) rgb = {0, 255, 255};
    grid->Rgb[0] = {0, 0, 0};
    grid->Rgb[1] = {100, 0, 0};
    grid->Rgb[3] = {240, 0, 0};
    const GrassTextureColorSource source{grid};
    EXPECT_EQ(source.SamplePacked(0.125F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp), 0xff000000U);
    EXPECT_EQ(source.SamplePacked(0.2F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp), 0xff00001eU);
    EXPECT_EQ(source.SamplePacked(0.25F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp), 0xff000032U);
    EXPECT_EQ(source.SamplePacked(0.0F, 0.125F, GrassTextureWrap::Repeat, GrassTextureWrap::Clamp), 0xff000078U);
    EXPECT_EQ(source.SamplePacked(1.0F, 0.125F, GrassTextureWrap::Repeat, GrassTextureWrap::Clamp), 0xff000078U);
    EXPECT_EQ(source.SamplePacked(-0.2F, 0.125F, GrassTextureWrap::Mirror, GrassTextureWrap::Clamp),
              source.SamplePacked(0.2F, 0.125F, GrassTextureWrap::Mirror, GrassTextureWrap::Clamp));
    EXPECT_EQ(source.SamplePacked(-1.0F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp),
              source.SamplePacked(0.0F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp));
    EXPECT_EQ(GrassTextureColorSource{}.SamplePacked(0, 0, GrassTextureWrap::Repeat, GrassTextureWrap::Repeat), 0U);
}

TEST(Oot3dGrassTextureColor, GridIsSharedAndSurvivesSourceCacheReset) {
    using namespace Fast::Oot3d;
    auto& cache = GrassTextureSourceCache::Instance();
    cache.Clear();
    std::vector<uint8_t> pixels(8U * 8U * 4U);
    for (size_t y = 0; y < 8; ++y) for (size_t x = 0; x < 8; ++x) {
        pixels[(y * 8 + x) * 4] = static_cast<uint8_t>((x / 2) * 60);
        pixels[(y * 8 + x) * 4 + 3] = 255;
    }
    cache.ObserveDecoded(99, 8, 8, pixels);
    auto source = cache.AcquireColorSource(99);
    ASSERT_NE(source.Grid, nullptr);
    EXPECT_EQ(source.Grid, cache.AcquireColorSource(99).Grid);
    EXPECT_FLOAT_EQ(source.Grid->Rgb[0][0], 0);
    EXPECT_FLOAT_EQ(source.Grid->Rgb[3][0], 180);
    cache.Clear();
    EXPECT_EQ(source.SamplePacked(0.375F, 0.125F, GrassTextureWrap::Clamp, GrassTextureWrap::Clamp), 0xff00003cU);
    const std::array<uint8_t, 4> black{0, 0, 0, 255};
    cache.ObserveDecoded(100, 1, 1, black);
    EXPECT_EQ(cache.AcquireColorSource(100).SamplePacked(0.5F, 0.5F, GrassTextureWrap::Repeat, GrassTextureWrap::Repeat), 0xff000000U);
    cache.Clear();
}

TEST(Oot3dGrassVisibility, CullsOutsideAndReducesFarGeometry) {
    using namespace Fast::Oot3d;
    const std::array<float, 16> identity{
        1, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 1};
    EXPECT_TRUE(GrassSphereIntersectsFrustum(
        identity, {0.0F, 0.0F, 0.0F}, 0.1F));
    EXPECT_FALSE(GrassSphereIntersectsFrustum(
        identity, {3.0F, 0.0F, 0.0F}, 0.1F));
    const auto radiusScale =
        BuildGrassFrustumRadiusScale(identity);
    EXPECT_EQ(
        ClassifyGrassSphereInFrustum(
            identity, radiusScale,
            {0.0F, 0.0F, 0.0F}, 0.1F),
        GrassFrustumRelation::Inside);
    EXPECT_EQ(
        ClassifyGrassSphereInFrustum(
            identity, radiusScale,
            {0.95F, 0.0F, 0.0F}, 0.1F),
        GrassFrustumRelation::Intersecting);
    EXPECT_EQ(
        ClassifyGrassSphereInFrustum(
            identity, radiusScale,
            {3.0F, 0.0F, 0.0F}, 0.1F),
        GrassFrustumRelation::Outside);
    for (const auto& center : {
             std::array<float, 3>{0.0F, 0.0F, 0.0F},
             std::array<float, 3>{3.0F, 0.0F, 0.0F},
             std::array<float, 3>{0.8F, -0.7F, 0.4F}}) {
        EXPECT_EQ(
            GrassSphereIntersectsFrustum(
                identity, center, 0.1F),
            GrassSphereIntersectsFrustum(
                identity, radiusScale, center, 0.1F));
    }

    InteractiveGrassSettings settings;
    settings.FarTuftsEnabled = false;
    settings.DrawDistance = 1000.0F;
    settings.TuftTransitionFraction = 0.0F;
    settings.Appearance.BladeSegments = 6U;
    settings.FarBladeSegments = 1U;
    settings.LodStartFraction = 0.4F;
    settings.LodEndFraction = 0.8F;
    settings.FarDensity = 1.0F;
    const auto near = ResolveGrassLod(settings, 100.0F, 1U);
    const auto far = ResolveGrassLod(settings, 900.0F, 1U);
    const auto policy = BuildGrassLodPolicy(settings);
    const auto nearFromStable = ResolveGrassLodWithStableVisibility(settings, 100.0F, GrassStableVisibilityValue(1U));
    const auto farFromStable = ResolveGrassLodWithStableVisibility(settings, 900.0F, GrassStableVisibilityValue(1U));
    const auto nearFromPolicy = ResolveGrassLodWithStableVisibility(policy, 100.0F, GrassStableVisibilityValue(1U));
    const auto farFromPolicy = ResolveGrassLodWithStableVisibility(policy, 900.0F, GrassStableVisibilityValue(1U));
    EXPECT_EQ(near.Visible, nearFromStable.Visible);
    EXPECT_EQ(near.BladeSegments, nearFromStable.BladeSegments);
    EXPECT_EQ(near.PlaneCount, nearFromStable.PlaneCount);
    EXPECT_FLOAT_EQ(near.Retention, nearFromStable.Retention);
    EXPECT_EQ(far.Visible, farFromStable.Visible);
    EXPECT_EQ(far.BladeSegments, farFromStable.BladeSegments);
    EXPECT_EQ(far.PlaneCount, farFromStable.PlaneCount);
    EXPECT_FLOAT_EQ(far.Retention, farFromStable.Retention);
    EXPECT_EQ(near.Visible, nearFromPolicy.Visible);
    EXPECT_EQ(near.BladeSegments, nearFromPolicy.BladeSegments);
    EXPECT_EQ(near.PlaneCount, nearFromPolicy.PlaneCount);
    EXPECT_FLOAT_EQ(near.Retention, nearFromPolicy.Retention);
    EXPECT_EQ(far.Visible, farFromPolicy.Visible);
    EXPECT_EQ(far.BladeSegments, farFromPolicy.BladeSegments);
    EXPECT_EQ(far.PlaneCount, farFromPolicy.PlaneCount);
    EXPECT_FLOAT_EQ(far.Retention, farFromPolicy.Retention);
    EXPECT_TRUE(near.Visible);
    EXPECT_EQ(near.BladeSegments, 6U);
    EXPECT_EQ(near.PlaneCount, 2U);
    EXPECT_TRUE(far.Visible);
    EXPECT_EQ(far.BladeSegments, 1U);
    EXPECT_EQ(far.PlaneCount, 1U);
    settings.FarDensity = 0.2F;
    EXPECT_FLOAT_EQ(
        GrassLodRetention(settings, 100.0F), 1.0F);
    EXPECT_LT(
        GrassLodRetention(settings, 900.0F),
        GrassLodRetention(settings, 500.0F));
}

TEST(Oot3dGrassVisibility, SegmentDistancesAreIndependentOfDensityAndDrawRange) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.DrawDistance = 5000.0F;
    settings.Appearance.BladeSegments = 8U;
    settings.FarBladeSegments = 2U;
    settings.SegmentLodStartDistance = 100.0F;
    settings.SegmentLodEndDistance = 700.0F;
    EXPECT_EQ(ResolveGrassLod(settings, 100.0F, 1U).BladeSegments, 8U);
    EXPECT_EQ(ResolveGrassLod(settings, 400.0F, 1U).BladeSegments, 4U);
    EXPECT_EQ(ResolveGrassLod(settings, 700.0F, 1U).BladeSegments, 2U);
    settings.DrawDistance = 1000.0F;
    settings.LodStartFraction = 0.9F;
    settings.FarDensity = 0.1F;
    EXPECT_EQ(ResolveGrassLod(settings, 400.0F, 1U).BladeSegments, 4U);
    EXPECT_EQ(ResolveGrassLod(settings, 700.0F, 1U).BladeSegments, 2U);
    settings.SegmentLodEndDistance = 100.0F;
    EXPECT_EQ(ResolveGrassLod(settings, 101.0F, 1U).BladeSegments, 2U);
}

TEST(Oot3dGrassVisibility, UsesAtMostThreeOrderedTopologyBandsAcrossSupportedLodPolicies) {
    using namespace Fast::Oot3d;
    constexpr float drawDistance = 4096.0F;
    constexpr float lodStart = 0.23F;
    constexpr float lodEnd = 0.91F;

    for (uint8_t nearSegments = kMinimumGrassBladeSegments; nearSegments <= kMaximumGrassBladeSegments;
         ++nearSegments) {
        for (uint8_t farSegments = kMinimumGrassBladeSegments; farSegments <= nearSegments; ++farSegments) {
            GrassLodPolicy policy{
                .DrawDistance = drawDistance,
                .LodStart = lodStart,
                .LodEnd = lodEnd,
                .FarDensity = 1.0F,
                .SegmentStartDistance = drawDistance * lodStart,
                .SegmentEndDistance = drawDistance * lodEnd,
                .NearBladeSegments = nearSegments,
                .FarBladeSegments = farSegments,
                .SegmentLodSoftness = 0.0F,
            };
            for (uint32_t distanceStep = 0U; distanceStep <= static_cast<uint32_t>(drawDistance); ++distanceStep) {
                const float distance = static_cast<float>(distanceStep);
                const float normalized = distance / drawDistance;
                const float progress = normalized > lodStart
                                           ? std::clamp((normalized - lodStart) / (lodEnd - lodStart), 0.0F, 1.0F)
                                           : 0.0F;
                const auto middle = std::max<uint8_t>(farSegments,(nearSegments+1U)/2U);
                const auto expected = progress < 1.0F/3.0F ? nearSegments : progress < 2.0F/3.0F ? middle : farSegments;

                EXPECT_EQ(ResolveGrassLodWithStableVisibility(policy, distance, 0.0F).BladeSegments, expected);
            }
        }
    }
}

TEST(Oot3dGrassVisibility, DistantDensityFallsQuarticallyWithoutAffectingNearCoverage) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.FarTuftsEnabled = false;
    settings.DrawDistance = 1000;
    settings.LodStartFraction = 0.1F;
    settings.FarDensity = 0.01F;
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,100),1.0F);
    EXPECT_NEAR(GrassLodRetention(settings,550),0.071875F,1.0e-6F);
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,1000),0.01F);
    EXPECT_FLOAT_EQ(GrassLodRetention(settings,1001),0.0F);
    float previous=1;
    for (int distance=0; distance<=1000; ++distance) {
        const auto retention=GrassLodRetention(settings,static_cast<float>(distance));
        EXPECT_LE(retention,previous);
        previous=retention;
    }
}

TEST(Oot3dGrassShading, SamplesNativeFogAndCmbLights) {
    using namespace Fast::Oot3d;
    std::vector<uint8_t> vertexUniforms(
        80U + (0x56U + 3U) * 16U, 0U);
    const uint32_t lightingEnabled = 1U << 9U;
    std::memcpy(vertexUniforms.data(), &lightingEnabled,
                sizeof(lightingEnabled));
    const std::array<float, 4> direction{
        0.0F, -1.0F, 0.0F, 0.0F};
    const std::array<float, 4> diffuse{
        0.6F, 0.5F, 0.4F, 1.0F};
    const std::array<float, 4> ambient{
        0.2F, 0.2F, 0.2F, 1.0F};
    constexpr size_t lightOffset = 80U + 0x50U * 16U;
    std::memcpy(vertexUniforms.data() + lightOffset,
                direction.data(), sizeof(direction));
    std::memcpy(vertexUniforms.data() + lightOffset + 16U,
                diffuse.data(), sizeof(diffuse));
    std::memcpy(vertexUniforms.data() + lightOffset + 32U,
                ambient.data(), sizeof(ambient));

    std::vector<uint8_t> fragmentUniforms(
        144U + 128U * 2U * sizeof(float), 0U);
    const float depthScale = 1.0F;
    const float depthOffset = 0.0F;
    std::memcpy(fragmentUniforms.data() + 116U,
                &depthScale, sizeof(depthScale));
    std::memcpy(fragmentUniforms.data() + 120U,
                &depthOffset, sizeof(depthOffset));
    const std::array<float, 4> fogColor{
        0.1F, 0.2F, 0.3F, 1.0F};
    std::memcpy(fragmentUniforms.data() + 128U,
                fogColor.data(), sizeof(fogColor));
    for (size_t entry = 0U; entry < 128U; ++entry) {
        const size_t pair = entry / 2U;
        const size_t component = (entry % 2U) * 2U;
        std::array<float, 4> packed{};
        std::memcpy(
            packed.data(),
            fragmentUniforms.data() + 144U +
                pair * sizeof(packed),
            sizeof(packed));
        packed[component] =
            static_cast<float>(entry) / 127.0F;
        std::memcpy(
            fragmentUniforms.data() + 144U +
                pair * sizeof(packed),
            packed.data(), sizeof(packed));
    }
    const std::array<float, 16> viewToWorld{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const auto environment =
        DecodeGrassShadingEnvironment(
            vertexUniforms, fragmentUniforms,
            {Oot3d::Renderer::kPicaFragmentFeatureSchemaVersion,
             false, true, false, 5U},
            viewToWorld);
    ASSERT_EQ(environment.ActiveLightCount, 1U);
    EXPECT_TRUE(environment.Fog.Enabled);
    const auto lit = EvaluateGrassLighting(
        environment, {0.0F, 1.0F, 0.0F});
    EXPECT_TRUE(lit.NativeLighting);
    EXPECT_GT(lit.Rgb[0], ambient[0]);
    EXPECT_GT(SampleGrassPicaFog(environment.Fog, 0.75F),
              SampleGrassPicaFog(environment.Fog, 0.25F));
    // A captured camera can roll without changing Eye/At. Native light
    // directions must follow that full basis, independently of live input.
    const std::array<float, 16> rolledViewToWorld{
        0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 100, 200, 300, 1};
    const auto rolled = DecodeGrassShadingEnvironment(
        vertexUniforms, fragmentUniforms,
        {Oot3d::Renderer::kPicaFragmentFeatureSchemaVersion, false, true, false, 5U},
        rolledViewToWorld);
    const auto rolledLit = EvaluateGrassLighting(rolled, {-1.0F, 0.0F, 0.0F});
    for (size_t channel = 0; channel < 3; ++channel)
        EXPECT_NEAR(rolledLit.Rgb[channel], lit.Rgb[channel], 1.0e-6F);
}

TEST(Oot3dGrassWind, RandomnessControlsPerBladePhase) {
    Fast::Oot3d::InteractiveGrassSettings settings;
    settings.WindStrength = 1.0F;
    settings.WindSpeed = 1.0F;
    settings.MaximumBend = 4.0F;
    settings.WindRandomness = 0.0F;
    const std::array<float, 3> position{100.0F, 0.0F, 200.0F};
    const auto uniformA =
        Fast::Oot3d::EvaluateGrassWind(
            settings, position, 0.25F, 2.0);
    const auto uniformB =
        Fast::Oot3d::EvaluateGrassWind(
            settings, position, 4.25F, 2.0);
    EXPECT_FLOAT_EQ(uniformA[0], uniformB[0]);
    EXPECT_FLOAT_EQ(uniformA[1], uniformB[1]);

    settings.WindRandomness = 1.0F;
    const auto randomized =
        Fast::Oot3d::EvaluateGrassWind(
            settings, position, 4.25F, 2.0);
    EXPECT_TRUE(randomized[0] != uniformA[0] ||
                randomized[1] != uniformA[1]);
}

TEST(Oot3dRigidMotion, TracksConsecutiveInstanceMotionAndRejectsGaps) {
    Fast::Oot3d::PicaRigidMotionTracker tracker;
    EXPECT_FALSE(tracker.Track(7U, 10U, {0,0,0,1}, {}).Valid);
    const auto moved=tracker.Track(7U,11U,{0.2F,-0.4F,0,1},{});
    ASSERT_TRUE(moved.Valid);
    EXPECT_NEAR(moved.MotionUv[0],-0.1F,1.0e-6F);
    EXPECT_NEAR(moved.MotionUv[1],0.2F,1.0e-6F);
    EXPECT_FALSE(tracker.Track(7U,13U,{0.3F,-0.4F,0,1},{}).Valid);
}

TEST(Oot3dRigidMotion, DecodesCommonRigidUniformOrigin) {
    std::vector<uint8_t> bytes(448U, 0U);
    const std::array<float,16> identity{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    constexpr size_t floats=80U;
    std::memcpy(bytes.data()+floats,identity.data(),sizeof(identity));
    std::memcpy(bytes.data()+floats+4U*16U,identity.data(),3U*16U);
    std::memcpy(bytes.data()+floats+20U*16U,identity.data(),3U*16U);
    std::array<float,4> clip{};
    ASSERT_TRUE(Fast::Oot3d::DecodeCommonRigidOriginClip(bytes,false,clip));
    EXPECT_FLOAT_EQ(clip[0],0.0F);
    EXPECT_FLOAT_EQ(clip[1],0.0F);
    EXPECT_FLOAT_EQ(clip[3],1.0F);
}

TEST(Oot3dRigidMotion, ShaderVariantAddsGuideWithoutChangingTextures) {
    const auto variant=Fast::Oot3d::BuildPicaRigidMotionShaderVariant(
        "#version 450\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1);}\n",9U);
    ASSERT_TRUE(variant.Applied);
    EXPECT_NE(variant.FragmentKey,9U);
    EXPECT_NE(variant.Source.find("layout(location=3) out vec4"),std::string::npos);
    EXPECT_NE(variant.Source.find("pica_rigid_motion_guide ="),std::string::npos);
}

TEST(Oot3dRigidMotion, VertexVariantAddsHomogeneousJitter) {
    const auto variant=Fast::Oot3d::BuildPicaTemporalVertexVariant(
        "#version 450\nlayout(set=0,binding=0) uniform U{vec4 f[96];} uniforms;\n"
        "vec4 pica_output0 = vec4(0);\nvoid exec_shader(){}\n"
        "void main(){exec_shader();gl_Position=pica_output0;}\n",11U);
    ASSERT_TRUE(variant.Applied);
    EXPECT_NE(variant.Source.find("gl_Position.xy +="),std::string::npos);
    EXPECT_NE(variant.FragmentKey,11U);
}

TEST(Oot3dTemporalJitter, IsBoundedDeterministicAndRepeatsAfterEightFrames) {
    for (uint64_t frame = 0; frame < 8; ++frame) {
        const auto sample = Fast::Oot3d::TemporalJitterForFrame(frame);
        EXPECT_GE(sample.PixelOffset[0], -0.5F);
        EXPECT_LT(sample.PixelOffset[0], 0.5F);
        EXPECT_GE(sample.PixelOffset[1], -0.5F);
        EXPECT_LT(sample.PixelOffset[1], 0.5F);
        EXPECT_EQ(sample.PixelOffset,
                  Fast::Oot3d::TemporalJitterForFrame(frame + 8)
                      .PixelOffset);
    }
}

TEST(Oot3dTemporalJitter, SupportsProviderSpecificPhaseCount) {
    for (uint64_t frame = 0; frame < 18U; ++frame) {
        EXPECT_EQ(Fast::Oot3d::TemporalJitterForFrame(frame, 18U).PixelOffset,
                  Fast::Oot3d::TemporalJitterForFrame(frame + 18U, 18U)
                      .PixelOffset);
    }
    EXPECT_NE(Fast::Oot3d::TemporalJitterForFrame(0U, 18U).PixelOffset,
              Fast::Oot3d::TemporalJitterForFrame(8U, 18U).PixelOffset);
}

TEST(Oot3dTemporalJitter, AppliesClipSpaceOffsetThroughHomogeneousW) {
    const std::array<float, 16> matrix{
        1, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 1, -1, 0, 0, 0, 0};
    const auto jittered = Fast::Oot3d::ApplyTemporalJitterToClipMatrix(
        matrix, {0.5F, -0.25F}, 400.0F, 200.0F);
    EXPECT_FLOAT_EQ(jittered[8], -0.0025F);
    EXPECT_FLOAT_EQ(jittered[9], 0.0025F);
    EXPECT_TRUE(Fast::Oot3d::HasPerspectiveClipW(matrix));
}

TEST(Oot3dPerspectiveFov,
     ScalesBothAxesAndPreservesOpticalCenters) {
    using namespace Fast::Oot3d;
    const PerspectiveFovInput input{
        {-2.0F, 4.0F, -1.0F, 3.0F, 2.0F, 1000.0F},
        1.5F, ResolveScenePresentationPolicy(400U, 240U), true};
    const auto result = ResolvePerspectiveFov(input);
    ASSERT_TRUE(result.Applied);
    EXPECT_GT(result.PerspectiveScale, 1.0F);
    EXPECT_FLOAT_EQ(
        (result.Frustum.Left + result.Frustum.Right) * 0.5F,
        1.0F);
    EXPECT_FLOAT_EQ(
        (result.Frustum.Bottom + result.Frustum.Top) * 0.5F,
        1.0F);
    EXPECT_NEAR(
        (result.Frustum.Right - result.Frustum.Left) / 6.0F,
        result.PerspectiveScale, 1.0e-5F);
    EXPECT_NEAR(
        (result.Frustum.Top - result.Frustum.Bottom) / 4.0F,
        result.PerspectiveScale, 1.0e-5F);

    auto aboveContract = input;
    aboveContract.Multiplier = 9.0F;
    const auto clamped = ResolvePerspectiveFov(aboveContract);
    EXPECT_NEAR(clamped.PerspectiveScale,
                result.PerspectiveScale, 1.0e-6F);
}

TEST(Oot3dPerspectiveFov,
     LeavesOrthographicProjectionUntouched) {
    using namespace Fast::Oot3d;
    const PerspectiveFovInput input{
        {-2.0F, 4.0F, -1.0F, 3.0F, 2.0F, 1000.0F},
        1.5F, ResolveScenePresentationPolicy(1280U, 720U), false};
    const auto result = ResolvePerspectiveFov(input);
    EXPECT_FALSE(result.Applied);
    EXPECT_FLOAT_EQ(result.Frustum.Left, input.Frustum.Left);
    EXPECT_FLOAT_EQ(result.Frustum.Right, input.Frustum.Right);
    EXPECT_FLOAT_EQ(result.Frustum.Bottom, input.Frustum.Bottom);
    EXPECT_FLOAT_EQ(result.Frustum.Top, input.Frustum.Top);
}

TEST(Oot3dPerspectiveFov,
     ExtendsHorizontalAt16x9AndVerticalAt4x3) {
    using namespace Fast::Oot3d;
    const PerspectiveFrustum native{
        -2.0F, 2.0F, -1.0F, 1.0F, 2.0F, 1000.0F};

    const auto widescreenPolicy =
        ResolveScenePresentationPolicy(1920U, 1080U);
    ASSERT_TRUE(widescreenPolicy.Valid);
    EXPECT_EQ(widescreenPolicy.Extension,
              SceneAspectExtension::Horizontal);
    EXPECT_NEAR(widescreenPolicy.HorizontalFovExpansion,
                16.0F / 15.0F, 1.0e-6F);
    EXPECT_FLOAT_EQ(widescreenPolicy.VerticalFovExpansion, 1.0F);
    const auto widescreen = ResolvePerspectiveFov(
        {native, 1.0F, widescreenPolicy, true});
    EXPECT_NEAR(widescreen.Frustum.Left, -32.0F / 15.0F, 1.0e-6F);
    EXPECT_NEAR(widescreen.Frustum.Right, 32.0F / 15.0F, 1.0e-6F);
    EXPECT_FLOAT_EQ(widescreen.Frustum.Bottom, -1.0F);
    EXPECT_FLOAT_EQ(widescreen.Frustum.Top, 1.0F);

    const auto standardPolicy =
        ResolveScenePresentationPolicy(1024U, 768U);
    ASSERT_TRUE(standardPolicy.Valid);
    EXPECT_EQ(standardPolicy.Extension,
              SceneAspectExtension::Vertical);
    EXPECT_FLOAT_EQ(standardPolicy.HorizontalFovExpansion, 1.0F);
    EXPECT_NEAR(standardPolicy.VerticalFovExpansion, 1.25F, 1.0e-6F);
    const auto standard = ResolvePerspectiveFov(
        {native, 1.0F, standardPolicy, true});
    EXPECT_FLOAT_EQ(standard.Frustum.Left, -2.0F);
    EXPECT_FLOAT_EQ(standard.Frustum.Right, 2.0F);
    EXPECT_FLOAT_EQ(standard.Frustum.Bottom, -1.25F);
    EXPECT_FLOAT_EQ(standard.Frustum.Top, 1.25F);
}

TEST(Oot3dPerspectiveFov,
     ComposesGeneralMultiplierWithAspectWithoutMovingOpticalCenter) {
    using namespace Fast::Oot3d;
    const auto result = ResolvePerspectiveFov({
        {-2.0F, 4.0F, -1.0F, 3.0F, 2.0F, 1000.0F},
        1.25F,
        ResolveScenePresentationPolicy(1024U, 768U),
        true,
    });
    ASSERT_TRUE(result.Applied);
    EXPECT_FLOAT_EQ(
        (result.Frustum.Left + result.Frustum.Right) * 0.5F, 1.0F);
    EXPECT_FLOAT_EQ(
        (result.Frustum.Bottom + result.Frustum.Top) * 0.5F, 1.0F);
    EXPECT_NEAR(result.HorizontalScale,
                result.PerspectiveScale, 1.0e-6F);
    EXPECT_NEAR(result.VerticalScale,
                result.PerspectiveScale * 1.25F, 1.0e-6F);
}

TEST(Oot3dPresentationPacing,
     MapsSettingsWithoutChangingSimulationPolicy) {
    using namespace Fast::Oot3d;
    const auto original =
        ResolvePresentationPacingPolicy(FrameRateMode::Original30);
    EXPECT_TRUE(original.Enabled);
    EXPECT_EQ(original.TargetRateHz, 30U);
    EXPECT_FALSE(original.Composition.InterpolationEnabled());
    EXPECT_EQ(original.Composition.FixedSampleMultiplier, 1U);

    const auto interpolated2x =
        ResolvePresentationPacingPolicy(FrameRateMode::Interpolated2x);
    EXPECT_TRUE(interpolated2x.Enabled);
    EXPECT_EQ(interpolated2x.TargetRateHz, 60U);
    EXPECT_EQ(interpolated2x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed2x);
    EXPECT_EQ(interpolated2x.Composition.FixedSampleMultiplier, 2U);

    const auto interpolated3x = ResolvePresentationPacingPolicy(FrameRateMode::Interpolated3x);
    EXPECT_TRUE(interpolated3x.Enabled);
    EXPECT_EQ(interpolated3x.TargetRateHz, 90U);
    EXPECT_EQ(interpolated3x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed3x);
    EXPECT_EQ(interpolated3x.Composition.FixedSampleMultiplier, 3U);

    const auto interpolated4x = ResolvePresentationPacingPolicy(FrameRateMode::Interpolated4x);
    EXPECT_TRUE(interpolated4x.Enabled);
    EXPECT_EQ(interpolated4x.TargetRateHz, 120U);
    EXPECT_EQ(interpolated4x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed4x);
    EXPECT_EQ(interpolated4x.Composition.FixedSampleMultiplier, 4U);

    const auto interpolated6x = ResolvePresentationPacingPolicy(FrameRateMode::Interpolated6x);
    EXPECT_TRUE(interpolated6x.Enabled);
    EXPECT_EQ(interpolated6x.TargetRateHz, 180U);
    EXPECT_EQ(interpolated6x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed6x);
    EXPECT_EQ(interpolated6x.Composition.FixedSampleMultiplier, 6U);

    const auto interpolated8x = ResolvePresentationPacingPolicy(FrameRateMode::Interpolated8x);
    EXPECT_TRUE(interpolated8x.Enabled);
    EXPECT_EQ(interpolated8x.TargetRateHz, 240U);
    EXPECT_EQ(interpolated8x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed8x);
    EXPECT_EQ(interpolated8x.Composition.FixedSampleMultiplier, 8U);

    const auto interpolated12x = ResolvePresentationPacingPolicy(FrameRateMode::Interpolated12x);
    EXPECT_TRUE(interpolated12x.Enabled);
    EXPECT_EQ(interpolated12x.TargetRateHz, 360U);
    EXPECT_EQ(interpolated12x.Composition.Interpolation, NativeVisualInterpolationMode::Fixed12x);
    EXPECT_EQ(interpolated12x.Composition.FixedSampleMultiplier, 12U);

    const auto uncapped =
        ResolvePresentationPacingPolicy(FrameRateMode::Uncapped);
    EXPECT_FALSE(uncapped.Enabled);
    EXPECT_EQ(uncapped.TargetRateHz, 0U);
    EXPECT_EQ(uncapped.Composition.Interpolation, NativeVisualInterpolationMode::Adaptive);
    EXPECT_EQ(uncapped.Composition.FixedSampleMultiplier, 0U);

    EXPECT_FALSE(PresentationPacingPolicyChanged(original, original));
    EXPECT_TRUE(PresentationPacingPolicyChanged(original, interpolated2x));
    EXPECT_TRUE(PresentationPacingPolicyChanged(interpolated2x, interpolated3x));
    EXPECT_TRUE(PresentationPacingPolicyChanged(interpolated3x, interpolated4x));
    EXPECT_TRUE(PresentationPacingPolicyChanged(interpolated4x, uncapped));

    const auto x2Sample = BuildNativeFrameTemporalSample(interpolated2x.Composition,
                                                         NativeFrameTemporalSampleKind::Transition, 10U, 11U, 2U, 0.5F);
    EXPECT_TRUE(x2Sample.Available());
    EXPECT_TRUE(x2Sample.Synthetic);
    EXPECT_EQ(x2Sample.SampleOrdinal, 1U);
    EXPECT_FLOAT_EQ(x2Sample.SampleDeltaSeconds, 1.0F / 60.0F);

    const auto x3Sample = BuildNativeFrameTemporalSample(
        interpolated3x.Composition, NativeFrameTemporalSampleKind::Transition, 11U, 12U, 2U, 2.0F / 3.0F);
    EXPECT_TRUE(x3Sample.Available());
    EXPECT_TRUE(x3Sample.Synthetic);
    EXPECT_EQ(x3Sample.SampleOrdinal, 2U);
    EXPECT_FLOAT_EQ(x3Sample.SampleDeltaSeconds, 1.0F / 90.0F);
}

TEST(Oot3dRenderResolution,
     SeparatesOutputAndScaledNativePicaExtent) {
    using namespace Fast::Oot3d;
    const auto ordinary = ResolveNativePicaRenderExtent(
        {320U, 240U}, {1920U, 1080U}, 1.5F);
    EXPECT_EQ(ordinary.Width, 480U);
    EXPECT_EQ(ordinary.Height, 360U);

    // From 480 presented lines the top screen renders at the output's
    // density (one sample per output pixel at scale 1, rotated); below, at
    // the game's own 2x.
    const auto topScreen = ResolveNativePicaRenderExtent(
        {480U, 400U}, {1920U, 1080U}, 1.5F);
    EXPECT_EQ(topScreen.Width, 1620U);
    EXPECT_EQ(topScreen.Height, 2880U);

    const auto standardTopScreen = ResolveNativePicaRenderExtent(
        {480U, 400U}, {1024U, 768U}, 1.0F);
    EXPECT_EQ(standardTopScreen.Width, 768U);
    EXPECT_EQ(standardTopScreen.Height, 1024U);

    const auto uhdTopScreen = ResolveNativePicaRenderExtent(
        {480U, 400U}, {3840U, 2160U}, 1.0F);
    EXPECT_EQ(uhdTopScreen.Width, 2160U);
    EXPECT_EQ(uhdTopScreen.Height, 3840U);

    const auto nativeTopScreen = ResolveNativePicaRenderExtent(
        {480U, 400U}, {400U, 240U}, 1.0F);
    EXPECT_EQ(nativeTopScreen.Width, 480U);
    EXPECT_EQ(nativeTopScreen.Height, 400U);
}

TEST(Oot3dRenderResolution,
     UpscalerQualityOverridesButPreservesUserScale) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    settings.InternalResolutionScale = 1.75F;
    EXPECT_FLOAT_EQ(ResolveInternalResolutionScale(settings), 1.75F);

    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.UpscalerMode = UpscalerQuality::Performance;
    EXPECT_FLOAT_EQ(
        ResolveInternalResolutionScale(settings),
        1.0F / UpscalerScalingFactor(UpscalerQuality::Performance));
    EXPECT_FLOAT_EQ(settings.InternalResolutionScale, 1.75F);

    // Every quality's output is the display extent: Ultra Performance renders a
    // third of it per axis (a 0.5 floor made its output 1.5x the display).
    for (const auto quality : { UpscalerQuality::Native, UpscalerQuality::UltraQuality, UpscalerQuality::Quality,
                                UpscalerQuality::Balanced, UpscalerQuality::Performance,
                                UpscalerQuality::UltraPerformance }) {
        settings.UpscalerMode = quality;
        const auto render = ResolveNativePicaRenderExtent({480U, 400U}, {3840U, 2160U},
                                                          ResolveInternalResolutionScale(settings));
        // The top-screen target is at the presented density: its transfer
        // copies, so the upscaler reads the whole target extent.
        const auto contract = ResolveUpscalerContractFromRender(UpscalerProvider::Dlss, quality, render.Width,
                                                                render.Height);
        EXPECT_NEAR(double(contract.Output.Width), 2160.0, 2.0) << int(quality);
        EXPECT_NEAR(double(contract.Output.Height), 3840.0, 2.0) << int(quality);
    }
    settings.UpscalerMode = UpscalerQuality::UltraPerformance;
    EXPECT_FLOAT_EQ(ResolveInternalResolutionScale(settings), 1.0F / 3.0F);
    // The user's own scale keeps its range.
    settings.AntiAliasing = AntiAliasingMode::Taa;
    settings.InternalResolutionScale = 0.25F;
    EXPECT_FLOAT_EQ(ResolveInternalResolutionScale(settings), 0.5F);
}

TEST(Oot3dAntiAliasingFramePolicy,
     KeepsSpatialTemporalAndMultisampleModesExclusive) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    const AntiAliasingFrameCapabilities capabilities{
        true, true, true, false};

    settings.AntiAliasing = AntiAliasingMode::Fxaa;
    auto policy =
        ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_EQ(policy.SpatialAaMode, 1U);
    EXPECT_FALSE(policy.TemporalJitter);
    EXPECT_FALSE(policy.PresentationWorldSurface);
    EXPECT_FALSE(policy.SceneAntiAliasing);
    EXPECT_EQ(policy.MsaaSamples, 1U);

    settings.AntiAliasing = AntiAliasingMode::Taa;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_EQ(policy.SpatialAaMode, 0U);
    EXPECT_TRUE(policy.TemporalJitter);
    EXPECT_TRUE(policy.TemporalMotion);
    EXPECT_EQ(policy.TemporalJitterPhases, 8U);

    EXPECT_TRUE(policy.PresentationWorldSurface);
    // Same-extent AA resolves with the scene, before native UI.
    EXPECT_TRUE(policy.SceneAntiAliasing);
    EXPECT_FALSE(policy.PresentationUpscaler);

    settings.AntiAliasing = AntiAliasingMode::Smaa1x;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_FALSE(policy.TemporalJitter);
    EXPECT_TRUE(policy.PresentationWorldSurface);
    EXPECT_TRUE(policy.SceneAntiAliasing);
    settings.AntiAliasing = AntiAliasingMode::Off;
    EXPECT_FALSE(ResolveAntiAliasingFramePolicy(settings, capabilities).PresentationWorldSurface);
    EXPECT_TRUE(ResolveAntiAliasingFramePolicy(settings, capabilities, false, true).PresentationWorldSurface);
    EXPECT_FALSE(ResolveAntiAliasingFramePolicy(settings, capabilities, false, true).SceneAntiAliasing);
    EXPECT_TRUE(ResolveAntiAliasingFramePolicy(settings, capabilities, true, false).SceneAntiAliasing);

    settings.AntiAliasing = AntiAliasingMode::Msaa;
    settings.MsaaSamples = 4U;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_EQ(policy.MsaaSamples, 4U);
    EXPECT_FALSE(policy.TemporalJitter);
    EXPECT_FALSE(policy.PresentationWorldSurface);
    settings.MsaaSamples = 8U;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_EQ(policy.MsaaSamples, 1U);

    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.Upscaler = UpscalerProvider::Nis;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_FALSE(policy.TemporalJitter);
    EXPECT_TRUE(policy.PresentationWorldSurface);
    EXPECT_TRUE(policy.PresentationUpscaler);
    settings.Upscaler = UpscalerProvider::Fsr;
    settings.UpscalerMode = UpscalerQuality::Quality;
    policy = ResolveAntiAliasingFramePolicy(settings, capabilities);
    EXPECT_TRUE(policy.TemporalJitter);
    EXPECT_TRUE(policy.TemporalMotion);
    EXPECT_TRUE(policy.PresentationWorldSurface);
    // Upscalers change the extent and stay with the presentation.
    EXPECT_FALSE(policy.SceneAntiAliasing);
    EXPECT_TRUE(policy.PresentationUpscaler);
    EXPECT_EQ(
        policy.TemporalJitterPhases,
        ResolveUpscalerContract(
            UpscalerProvider::Fsr, UpscalerQuality::Quality, 1U, 1U)
            .MinimumJitterPhases);
}

TEST(Oot3dUpscalerGuides, CoverTheDisplayTransferFootprint) {
    using namespace Fast::Oot3d;
    // The native top-screen transfer halves the target's first axis: each
    // colour texel covers two guide texels, the second axis maps 1:1.
    for (uint32_t pixel = 0U; pixel < 640U; ++pixel) {
        const auto span = ResolveUpscalerGuideSpan(pixel, 640U, 1280U);
        EXPECT_EQ(span.First, 2U * pixel);
        EXPECT_EQ(span.Last, 2U * pixel + 1U);
        const auto same = ResolveUpscalerGuideSpan(pixel, 640U, 640U);
        EXPECT_EQ(same.First, pixel);
        EXPECT_EQ(same.Last, pixel);
    }
    // Fractional ratios cover every texel the output texel overlaps, and the
    // spans tile the source without gaps.
    uint32_t next = 0U;
    for (uint32_t pixel = 0U; pixel < 960U; ++pixel) {
        const auto span = ResolveUpscalerGuideSpan(pixel, 960U, 1280U);
        EXPECT_LE(span.First, next);
        EXPECT_GE(span.Last + 1U, next);
        EXPECT_LE(span.Last - span.First + 1U, 2U);
        next = span.Last + 1U;
    }
    EXPECT_EQ(next, 1280U);
    // A larger guide is bounded, an upsampled one repeats its texel.
    const auto wide = ResolveUpscalerGuideSpan(1U, 10U, 100U);
    EXPECT_EQ(wide.First, 10U);
    EXPECT_EQ(wide.Last, 10U + kUpscalerGuideMaxSpan - 1U);
    const auto upsampled = ResolveUpscalerGuideSpan(3U, 1280U, 640U);
    EXPECT_EQ(upsampled.First, 1U);
    EXPECT_EQ(upsampled.Last, 1U);
    EXPECT_EQ(ResolveUpscalerGuideSpan(1279U, 1280U, 640U).Last, 639U);

    EXPECT_TRUE(UpscalerGuidesNeedResample(640U, 1067U, 1280U, 1067U));
    EXPECT_FALSE(UpscalerGuidesNeedResample(1280U, 1067U, 1280U, 1067U));
    // Jitter applied in target pixels is half a colour texel per target texel.
    const auto jitter = UpscalerJitterInInputPixels({0.5F, -0.25F}, 1280U, 1067U, 640U, 1067U);
    EXPECT_FLOAT_EQ(jitter[0], 0.25F);
    EXPECT_FLOAT_EQ(jitter[1], -0.25F);
}

// The upscalers take motion without the jitter. A still surface's motion is
// the step between its jittered positions and becomes zero; texels without
// motion (the sky, backdrops drawn without depth) stay still instead of
// moving against the jitter.
// A glow card the title fades with the time of day: colour = texture x
// vertex colour, alpha = the stage's constant alpha, blended SRC_ALPHA/ONE.
// Its light follows: off by day (alpha 0), full at night, half for a shaft.
namespace {
Fast::Renderer3ds::PicaTevProgram FadedGlowProgram() {
    Fast::Renderer3ds::PicaTevProgram program{};
    // Stage 0: rgb = texture0 * primary (modulate), alpha = constant.
    program.Stages[0] = {0x3U | (0x0U << 4U) | (0xEU << 16U), 0U, 0x1U | (0x0U << 16U), 0U};
    // Stages 1-5 pass the previous result on.
    for (size_t stage = 1; stage < 6; ++stage) program.Stages[stage] = {0xFU | (0xFU << 16U), 0U, 0U, 0U};
    return program;
}
} // namespace

TEST(Oot3dSurfaceEmission, FollowsTheCombinerConstant) {
    using namespace Fast::Renderer3ds;
    NativeBlendState additive;
    additive.Enabled = true;
    additive.SourceRgb = NativeBlendFactor::SourceAlpha;
    additive.DestRgb = NativeBlendFactor::One;
    std::array<std::array<float, 4>, 6> constants{};
    const auto emission = [&](float alpha) {
        constants[0] = {0.0F, 0.0F, 0.0F, alpha};
        return PicaUnitSurfaceEmission(FadedGlowProgram(), constants, {}, additive);
    };
    EXPECT_FLOAT_EQ(emission(0.0F), 0.0F);
    EXPECT_FLOAT_EQ(emission(1.0F), 1.0F);
    EXPECT_NEAR(emission(0.498F), 0.498F, 1.0F / 255.0F);
    // An opaque surface shows its colour whatever its alpha.
    EXPECT_FLOAT_EQ(PicaUnitSurfaceEmission(FadedGlowProgram(), constants, {}, NativeBlendState{}), 1.0F);
    // The CPU combiner agrees with the native operations.
    PicaTevInputs inputs;
    inputs.Primary = {0.5F, 1.0F, 1.0F, 1.0F};
    inputs.Textures[0] = {0.5F, 0.25F, 1.0F, 1.0F};
    inputs.Constants[0] = {0.0F, 0.0F, 0.0F, 0.6F};
    const auto out = EvaluatePicaTevProgram(FadedGlowProgram(), inputs);
    EXPECT_NEAR(out[0], 0.25F, 1.0F / 255.0F);
    EXPECT_NEAR(out[1], 0.25F, 1.0F / 255.0F);
    EXPECT_NEAR(out[3], 0.6F, 1.0F / 255.0F);
}

TEST(Oot3dSurfaceEmission, StrengthIsHowTheLastFrameDrewTheSurface) {
    using namespace Fast::Renderer3ds;
    constexpr uint64_t halo = 0x264dcff34c144b3dULL, other = 0x1234U;
    PicaSurfaceEmissionRegistry registry;
    const std::array<uint64_t, 1> followed{halo};
    registry.Follow(followed);
    // Never drawn: full; only followed surfaces are observed.
    EXPECT_FLOAT_EQ(registry.Strength(halo), 1.0F);
    registry.BeginFrame();
    EXPECT_TRUE(registry.Follows(halo));
    EXPECT_FALSE(registry.Follows(other));
    // Drawn twice in a frame: the stronger counts, from the next frame on.
    registry.Observe(halo, 0.0F);
    registry.Observe(halo, 0.25F);
    EXPECT_FLOAT_EQ(registry.Strength(halo), 1.0F);
    registry.BeginFrame();
    EXPECT_FLOAT_EQ(registry.Strength(halo), 0.25F);
    // A frame that does not draw it (out of view) keeps the last strength.
    registry.BeginFrame();
    EXPECT_FLOAT_EQ(registry.Strength(halo), 0.25F);
    // No longer followed: forgotten.
    registry.Follow({});
    registry.BeginFrame();
    EXPECT_FLOAT_EQ(registry.Strength(halo), 1.0F);
    EXPECT_FALSE(registry.Follows(halo));
}

TEST(Oot3dUpscalerGuides, MotionLeavesTheJitterOut) {
    using namespace Fast::Oot3d;
    const std::array<float, 2> current{0.25F / 480.0F, -0.125F / 853.0F};
    const std::array<float, 2> previous{-0.375F / 480.0F, 0.3F / 853.0F};
    const auto correction = UpscalerMotionJitterCorrection(current, previous);
    // Still surface: previous jittered uv minus current jittered uv.
    const std::array<float, 4> still{previous[0] - current[0], previous[1] - current[1], 0.0F, 0.0F};
    const auto unjittered = UpscalerMotionWithoutJitter(still, correction);
    EXPECT_NEAR(unjittered[0], 0.0F, 1.0e-9F);
    EXPECT_NEAR(unjittered[1], 0.0F, 1.0e-9F);
    // A moving surface keeps its motion.
    const std::array<float, 4> moving{0.01F + still[0], -0.02F + still[1], 0.0F, 0.5F};
    const auto kept = UpscalerMotionWithoutJitter(moving, correction);
    EXPECT_NEAR(kept[0], 0.01F, 1.0e-7F);
    EXPECT_NEAR(kept[1], -0.02F, 1.0e-7F);
    EXPECT_FLOAT_EQ(kept[3], 0.5F);
    // A disoccluded texel has no motion and keeps none.
    const auto sky = UpscalerMotionWithoutJitter({0.0F, 0.0F, 1.0F, 0.0F}, correction);
    EXPECT_FLOAT_EQ(sky[0], 0.0F);
    EXPECT_FLOAT_EQ(sky[1], 0.0F);
}

TEST(Oot3dUpscalerDescriptors, RecycledImageCannotSelectRetiredViews) {
    using namespace Fast::Oot3d;
    // Same image values, formats and extents after guide recreation. Only the
    // owner's lifetime distinguishes the new views from the retired set.
    std::array<UpscalerDescriptorBinding, 2> bindings{{{0x10U, 1U, false}, {0x20U, 2U, true}}};
    const auto oldKey = BuildUpscalerDescriptorKey(true, bindings);
    ASSERT_TRUE(oldKey);
    std::map<std::vector<uint64_t>, int> sets;
    sets.emplace(*oldKey, 11);
    EXPECT_EQ(sets.at(*BuildUpscalerDescriptorKey(true, bindings)), 11);

    bindings[0].Generation = 3U;
    const auto replacement = BuildUpscalerDescriptorKey(true, bindings);
    ASSERT_TRUE(replacement);
    EXPECT_EQ(sets.find(*replacement), sets.end());
    sets.emplace(*replacement, 22);
    EXPECT_EQ(sets.at(*replacement), 22);
    // Allocate a new set, do not update a set an earlier submission can hold.
    EXPECT_EQ(sets.at(*oldKey), 11);
    bindings[1].Generation = 4U;
    EXPECT_EQ(sets.find(*BuildUpscalerDescriptorKey(true, bindings)), sets.end());
}

TEST(Oot3dUpscalerDescriptors, MissingLifetimeAndDifferentBindingContractsCannotHit) {
    using namespace Fast::Oot3d;
    std::array<UpscalerDescriptorBinding, 1> bindings{{{0x10U, 1U, false}}};
    const auto sampled = BuildUpscalerDescriptorKey(true, bindings);
    ASSERT_TRUE(sampled);
    EXPECT_NE(sampled, BuildUpscalerDescriptorKey(false, bindings));
    bindings[0].Storage = true;
    EXPECT_NE(sampled, BuildUpscalerDescriptorKey(true, bindings));
    bindings[0].Generation = 0U;
    EXPECT_FALSE(BuildUpscalerDescriptorKey(true, bindings));
    bindings[0] = {0U, 1U, false};
    EXPECT_FALSE(BuildUpscalerDescriptorKey(true, bindings));
    EXPECT_FALSE(BuildUpscalerDescriptorKey(true, {}));
}

TEST(Oot3dUpscalerInstances, SpareCapacityDoesNotDrainRecordedWork) {
    using namespace Fast::Oot3d;
    UpscalerInstances<int*> instances;
    int a = 0, b = 0;
    const NriDlssFeatureKey keyA{3840U, 2160U};
    const NriDlssFeatureKey keyB{3840U, 4320U};
    instances.Add(keyA, &a);
    bool completed = false, destroyed = false;
    EXPECT_TRUE(instances.MakeRoom(keyB, [&] { completed = true; return true; },
                                  [&](int*) { destroyed = true; }));
    EXPECT_FALSE(completed);
    EXPECT_FALSE(destroyed);
    instances.Add(keyB, &b);
    EXPECT_EQ(instances.Find(keyA), &a);
    EXPECT_EQ(instances.Find(keyB), &b);
}

TEST(Oot3dUpscalerInstances, FailedCompletionPreservesFeaturesAndHistory) {
    using namespace Fast::Oot3d;
    UpscalerInstances<int*> instances;
    int a = 0, b = 0, c = 0;
    const NriDlssFeatureKey keyA{3840U, 2160U}, keyB{3840U, 4320U}, keyC{1920U, 1080U};
    const NriDlssFeatureKey keyD{2560U, 1440U};
    instances.Add(keyA, &a);
    instances.Add(keyB, &b);
    instances.Add(keyC, &c);
    EXPECT_TRUE(instances.Resumes(&a));
    const auto generation = instances.Generation();
    bool destroyed = false;
    EXPECT_FALSE(instances.MakeRoom(keyD, [] { return false; }, [&](int*) { destroyed = true; }));
    EXPECT_FALSE(destroyed);
    EXPECT_EQ(instances.Size(), 3U);
    EXPECT_EQ(instances.Generation(), generation);
    EXPECT_EQ(instances.Find(keyA), &a);
    EXPECT_EQ(instances.Find(keyB), &b);
    EXPECT_EQ(instances.Find(keyC), &c);
    EXPECT_FALSE(instances.Resumes(&a));
}

TEST(Oot3dUpscalerInstances, EvictionCompletesPrefixBeforeReleasingTheLeastRecentlyUsedFeature) {
    using namespace Fast::Oot3d;
    UpscalerInstances<int*> instances;
    int a = 0, b = 0, c = 0;
    const NriDlssFeatureKey keyA{3840U, 2160U}, keyB{3840U, 4320U}, keyC{1920U, 1080U};
    const NriDlssFeatureKey keyD{2560U, 1440U};
    instances.Add(keyA, &a);
    instances.Add(keyB, &b);
    instances.Add(keyC, &c);
    EXPECT_EQ(instances.Find(keyA), &a); // B is now oldest.
    bool completed = false;
    int* retired = nullptr;
    ASSERT_TRUE(instances.MakeRoom(keyD, [&] {
        EXPECT_EQ(instances.Size(), 3U);
        completed = true;
        return true;
    }, [&](int* feature) {
        EXPECT_TRUE(completed);
        retired = feature;
    }));
    EXPECT_EQ(retired, &b);
    EXPECT_EQ(instances.Find(keyB), nullptr);
    EXPECT_EQ(instances.Find(keyA), &a);
    EXPECT_EQ(instances.Find(keyC), &c);
}

TEST(Oot3dUpscalerInstances, SettingsRetirementCompletesOnceBeforeAllReleases) {
    using namespace Fast::Oot3d;
    UpscalerInstances<int*> instances;
    int a = 0, b = 0;
    instances.Add({3840U, 2160U}, &a);
    instances.Add({3840U, 4320U}, &b);
    NriDlssFeatureKey changed{3840U, 2160U};
    changed.Preset = 1U;
    unsigned completed = 0U, released = 0U;
    ASSERT_TRUE(instances.MakeRoom(changed, [&] { ++completed; return true; }, [&](int*) {
        EXPECT_EQ(completed, 1U);
        ++released;
    }));
    EXPECT_EQ(completed, 1U);
    EXPECT_EQ(released, 2U);
    EXPECT_EQ(instances.Size(), 0U);
}

// The upscaler sees the surface the way the screen shows it: an upright texel
// is the surface texel PresentationToStorage names at its centre, for both
// display-transfer flips, and the output turn is the exact inverse.
TEST(Oot3dUpscalerOrientation, UprightTexelsAreThePresentedSurface) {
    using namespace Fast::Oot3d;
    constexpr uint32_t width = 720U, height = 1280U;
    const auto upright = UprightExtent(width, height);
    EXPECT_EQ(upright[0], 1280U);
    EXPECT_EQ(upright[1], 720U);
    for (const bool flip : {false, true}) {
        const PicaSurfaceCoordinates coordinates{flip};
        for (const auto texel : {std::array<uint32_t, 2>{0U, 0U}, std::array<uint32_t, 2>{1279U, 719U},
                                 std::array<uint32_t, 2>{17U, 3U}, std::array<uint32_t, 2>{640U, 360U},
                                 std::array<uint32_t, 2>{1000U, 701U}}) {
            const std::array<float, 2> uv{(float(texel[0]) + 0.5F) / float(upright[0]),
                                          (float(texel[1]) + 0.5F) / float(upright[1])};
            const auto storage = coordinates.PresentationToStorage(uv);
            const auto surface = UprightToSurfaceTexel(texel, width, height, coordinates);
            EXPECT_EQ(surface[0], uint32_t(storage[0] * float(width))) << flip;
            EXPECT_EQ(surface[1], uint32_t(storage[1] * float(height))) << flip;
            const auto back = SurfaceToUprightTexel(surface, width, height, coordinates);
            EXPECT_EQ(back[0], texel[0]);
            EXPECT_EQ(back[1], texel[1]);
        }
        // A displacement follows the texels: one upright step right and one
        // down land where the turned offsets say on the surface.
        const std::array<uint32_t, 2> origin{100U, 200U};
        const auto from = UprightToSurfaceTexel(origin, width, height, coordinates);
        for (const auto step : {std::array<uint32_t, 2>{1U, 0U}, std::array<uint32_t, 2>{0U, 1U}}) {
            const auto to = UprightToSurfaceTexel({origin[0] + step[0], origin[1] + step[1]}, width, height,
                                                  coordinates);
            const std::array<float, 2> surfaceStep{float(int(to[0]) - int(from[0])), float(int(to[1]) - int(from[1]))};
            const auto turned = UprightOffset(surfaceStep, coordinates);
            EXPECT_FLOAT_EQ(turned[0], float(step[0])) << flip;
            EXPECT_FLOAT_EQ(turned[1], float(step[1])) << flip;
        }
    }
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dUpscalerOrientation, ShadersCompile) {
    shaderc::Compiler compiler;
    for (const auto& [source, name] :
         {std::pair{Fast::Oot3d::BuildUpscalerUprightInputShader(), "oot3d_upscaler_upright_inputs.comp"},
          std::pair{Fast::Oot3d::BuildUpscalerUprightOutputShader(), "oot3d_upscaler_upright_output.comp"}}) {
        const auto result = compiler.CompileGlslToSpv(source, shaderc_compute_shader, name);
        EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success) << result.GetErrorMessage();
    }
}
#endif

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dUpscalerGuides, ResampleShaderCompiles) {
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        Fast::Oot3d::BuildUpscalerGuideResampleShader(), shaderc_compute_shader,
        "oot3d_upscaler_guides.comp");
    EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

TEST(Oot3dNativeUiOverlay, MapsTheUpscaledTexelOntoItsDisplayFootprint) {
    using namespace Fast::Oot3d;
    // An identity extent lands on the texel itself with no neighbour weight.
    for (uint32_t pixel = 0U; pixel < 16U; ++pixel) {
        const auto tap = ResolveNativeUiOverlayTap(pixel, 16U, 16U);
        EXPECT_EQ(tap.First, int32_t(pixel));
        EXPECT_FLOAT_EQ(tap.Fraction, 0.0F);
    }
    // FSR Quality (1.5x): output texel centres fall between display texels,
    // and the footprint never skips a display texel.
    int32_t previous = -1;
    for (uint32_t pixel = 0U; pixel < 1440U; ++pixel) {
        const auto tap = ResolveNativeUiOverlayTap(pixel, 1440U, 960U);
        EXPECT_GE(tap.Fraction, 0.0F);
        EXPECT_LT(tap.Fraction, 1.0F);
        EXPECT_LE(tap.First - previous, 1);
        previous = tap.First;
    }
    const auto first = ResolveNativeUiOverlayTap(0U, 1440U, 960U);
    EXPECT_EQ(first.First, -1); // clamped to the edge by the shader
    EXPECT_NEAR(first.Fraction, 5.0F / 6.0F, 1.0e-6F);
    const auto exact = ResolveNativeUiOverlayTap(1U, 3U, 2U);
    EXPECT_EQ(exact.First, 0);
    EXPECT_NEAR(exact.Fraction, 0.5F, 1.0e-6F);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dNativeUiOverlay, ShaderCompiles) {
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        Fast::Oot3d::BuildNativeUiOverlayShader(), shaderc_compute_shader,
        "oot3d_native_ui_overlay.comp");
    EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

TEST(Oot3dPicaAlphaCoveragePolicy,
     EnablesOnlyMultisampledOpaqueAlphaTestedWorldDraws) {
    using namespace Fast::Oot3d;

    const auto enabled = ResolvePicaAlphaCoveragePolicy(
        VK_SAMPLE_COUNT_4_BIT, true, true, true, false, 0xFU);
    EXPECT_TRUE(enabled.AlphaTested);
    EXPECT_TRUE(enabled.Enable);

    EXPECT_FALSE(ResolvePicaAlphaCoveragePolicy(
        VK_SAMPLE_COUNT_1_BIT, true, true, true, false, 0xFU).Enable);
    EXPECT_FALSE(ResolvePicaAlphaCoveragePolicy(
        VK_SAMPLE_COUNT_4_BIT, true, true, true, true, 0xFU).Enable);
    EXPECT_FALSE(ResolvePicaAlphaCoveragePolicy(
        VK_SAMPLE_COUNT_4_BIT, true, true, false, false, 0xFU).Enable);
    EXPECT_FALSE(ResolvePicaAlphaCoveragePolicy(
        VK_SAMPLE_COUNT_4_BIT, false, true, true, false, 0xFU).AlphaTested);
}

TEST(Oot3dAmbientOcclusionComposite,
     AttenuatesOnlyTheEncodedAmbientResponse) {
    using namespace Fast::Oot3d;
    EXPECT_FLOAT_EQ(EncodeAmbientOcclusionResponse(0.0F), 0.5F);
    EXPECT_FLOAT_EQ(EncodeAmbientOcclusionResponse(1.0F), 1.0F);
    EXPECT_FLOAT_EQ(
        DecodeAmbientOcclusionResponse(
            EncodeAmbientOcclusionResponse(0.25F)),
        0.25F);
    EXPECT_FLOAT_EQ(
        ResolveAmbientOcclusionVisibility(0.2F, 0.5F),
        1.0F)
        << "a material with no ambient response must preserve direct light";
    EXPECT_FLOAT_EQ(
        ResolveAmbientOcclusionVisibility(0.2F, 0.75F),
        0.6F);
    EXPECT_FLOAT_EQ(
        ResolveAmbientOcclusionVisibility(0.2F, 1.0F),
        0.2F)
        << "legacy normal-guide alpha keeps the full-color fallback";
    EXPECT_FLOAT_EQ(
        ResolveAmbientOcclusionVisibility(
            std::numeric_limits<float>::quiet_NaN(), 1.0F),
        1.0F);
}

TEST(Oot3dAmbientOcclusionComposite,
     ResolvesExactRgbResponseWithLegacyFallback) {
    using namespace Fast::Oot3d;
    const auto exact = ResolveAmbientOcclusionVisibilityRgb(
        0.2F, 1.0F, {0.0F, 0.5F, 1.0F, 1.0F});
    EXPECT_FLOAT_EQ(exact[0], 1.0F);
    EXPECT_FLOAT_EQ(exact[1], 0.6F);
    EXPECT_FLOAT_EQ(exact[2], 0.2F);

    const auto fallback = ResolveAmbientOcclusionVisibilityRgb(
        0.2F, 0.75F, {0.0F, 0.0F, 0.0F, 0.0F});
    EXPECT_FLOAT_EQ(fallback[0], 0.6F);
    EXPECT_FLOAT_EQ(fallback[1], 0.6F);
    EXPECT_FLOAT_EQ(fallback[2], 0.6F);

    const auto library = AmbientOcclusionCompositeShaderLibrary();
    EXPECT_NE(library.find(
                  "oot3d_ambient_occlusion_visibility_rgb"),
              std::string_view::npos);
    EXPECT_NE(library.find("ambient_guide.a >= 0.5"),
              std::string_view::npos);
}

TEST(Oot3dAmbientOcclusionComposite,
     AppliesAcrossSceneCoverageOnly) {
    using namespace Fast::Oot3d;
    EXPECT_FLOAT_EQ(
        ResolveSceneAmbientOcclusionVisibility(0.2F, 0.0F),
        1.0F);
    EXPECT_FLOAT_EQ(
        ResolveSceneAmbientOcclusionVisibility(0.2F, 0.5F),
        0.6F);
    EXPECT_FLOAT_EQ(
        ResolveSceneAmbientOcclusionVisibility(0.2F, 1.0F),
        0.2F);
    EXPECT_FLOAT_EQ(
        ResolveSceneAmbientOcclusionVisibility(
            0.2F, std::numeric_limits<float>::quiet_NaN()),
        1.0F);
}


TEST(Oot3dPicaAmbientOcclusionGuide,
     PreservesNormalGuideAndMarksSceneCoverage) {
    using namespace Fast::Oot3d;
    constexpr std::string_view source = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D source_texture;
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
void main() {
    vec3 oot3d_normal_guide=vec3(0.0,0.0,1.0);
    float oot3d_ao_response=0.25;
    vec3 oot3d_ao_response_rgb=vec3(0.1,0.25,0.75);
    // OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY
    pica_color=texture(source_texture,vec2(0.5));
    pica_normal_guide = vec4(oot3d_normal_guide * 0.5 + 0.5, 1.0);
}
)glsl";
    const auto disabled =
        BuildPicaAmbientOcclusionGuideVariant(
            source, 31U, false);
    EXPECT_FALSE(disabled.Applied());
    EXPECT_EQ(disabled.FragmentKey, 31U);
    const auto variant =
        BuildPicaAmbientOcclusionGuideVariant(
            source, 31U, true);
    ASSERT_TRUE(variant.Applied());
    EXPECT_TRUE(variant.Exact());
    EXPECT_EQ(variant.Eligibility,
              PicaAmbientOcclusionGuideEligibility::AppliedExact);
    EXPECT_NE(variant.FragmentKey, 31U);
    EXPECT_NE(variant.Source.find(
                  "pica_color=texture(source_texture"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "layout(location=4) out vec4 pica_ambient_guide"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "pica_ambient_guide = vec4(clamp("
                  "oot3d_ao_response_rgb"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "pica_normal_guide = vec4("
                  "oot3d_normal_guide * 0.5 + 0.5, 1.0)"),
              std::string::npos);

    const auto fallback =
        BuildPicaAmbientOcclusionGuideVariant(
            "void main(){}", 32U, true);
    EXPECT_TRUE(fallback.Applied());
    EXPECT_FALSE(fallback.Exact());
    EXPECT_EQ(
        fallback.Eligibility,
        PicaAmbientOcclusionGuideEligibility::
            AppliedFallback);
    EXPECT_NE(fallback.Source.find(
                  "pica_ambient_guide = vec4(1.0, 1.0, 1.0, 1.0)"),
              std::string::npos);
}

TEST(Oot3dPicaSceneDomainGuide,
     AttenuatesGuidesOnlyForSceneDomainOverlays) {
    using namespace Fast::Oot3d;
    constexpr std::string_view source = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
void main() {
    pica_color=vec4(1.0,0.5,0.25,0.4);
    pica_normal_guide=vec4(0.5,0.5,1.0,1.0);
}
)glsl";
    PicaSceneDomainDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        0U, true, true, 0xFU};
    const PicaSceneDomainGuideFeatures features{true, true};
    const auto world = BuildPicaSceneDomainGuideVariant(
        source, 41U, draw, features);
    EXPECT_FALSE(world.Applied());
    EXPECT_EQ(
        world.Eligibility,
        PicaSceneDomainGuideEligibility::SceneGeometry);

    draw.DepthWriteEnabled = false;
    const auto overlay = BuildPicaSceneDomainGuideVariant(
        source, 41U, draw, features);
    ASSERT_TRUE(overlay.Applied());
    EXPECT_EQ(
        overlay.Eligibility,
        PicaSceneDomainGuideEligibility::AppliedBlendedOverlay);
    EXPECT_NE(
        overlay.Source.find(
            kPicaSceneDomainBlendedOverlayMarker),
        std::string::npos);
    EXPECT_NE(
        overlay.Source.find(
            "pica_normal_guide.a = pica_color.a"),
        std::string::npos);
    EXPECT_NE(
        overlay.Source.find(
            "pica_ambient_guide = vec4("
            "1.0, 1.0, 1.0, pica_color.a)"),
        std::string::npos);

    draw.BlendEnabled = false;
    const auto opaque = BuildPicaSceneDomainGuideVariant(
        source, 41U, draw, features);
    ASSERT_TRUE(opaque.Applied());
    EXPECT_EQ(
        opaque.Eligibility,
        PicaSceneDomainGuideEligibility::AppliedOpaqueOverlay);
    EXPECT_NE(
        opaque.Source.find("pica_normal_guide.a = 0.0"),
        std::string::npos);

    draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Ui;
    const auto ui = BuildPicaSceneDomainGuideVariant(
        source, 41U, draw, features);
    EXPECT_FALSE(ui.Applied());
    EXPECT_EQ(
        ui.Eligibility,
        PicaSceneDomainGuideEligibility::OutsideScene);
}

TEST(Oot3dOutlineGeometryGuide, OcclusionPassCannotWriteSceneDepthColorOrGeometryGuides) {
    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
    depth.stencilTestEnable = VK_TRUE;
    depth.front.writeMask = depth.back.writeMask = 255;
    depth.front.passOp = depth.back.passOp = VK_STENCIL_OP_REPLACE;
    depth.front.compareOp = depth.back.compareOp = VK_COMPARE_OP_EQUAL;
    std::array<VkPipelineColorBlendAttachmentState, Fast::Renderer3ds::kPicaColorAttachmentCount> colors{};
    for (auto& color : colors) color.colorWriteMask = 15;
    Fast::Oot3d::ConfigureOutlineOcclusionPass(depth, colors);
    EXPECT_FALSE(depth.depthTestEnable);
    EXPECT_FALSE(depth.depthWriteEnable);
    EXPECT_TRUE(depth.stencilTestEnable);
    for (const auto* stencil : {&depth.front, &depth.back}) {
        EXPECT_EQ(stencil->writeMask, 0U);
        EXPECT_EQ(stencil->passOp, VK_STENCIL_OP_KEEP);
        EXPECT_EQ(stencil->compareOp, VK_COMPARE_OP_EQUAL);
    }
    for (size_t i = 0; i < colors.size(); ++i) {
        const bool occlusion = i == Fast::Renderer3ds::PicaAttachmentIndex(
            Fast::Renderer3ds::PicaColorAttachment::RigidMotionGuide);
        EXPECT_EQ(colors[i].colorWriteMask, occlusion ? VK_COLOR_COMPONENT_A_BIT : 0U);
        if (occlusion) EXPECT_EQ(colors[i].alphaBlendOp, VK_BLEND_OP_MAX);
    }
}

TEST(Oot3dOutlineGeometryGuide, ExtensionCoverageKeepsNearestDepthAndReplacesMotionOnly) {
    VkPipelineColorBlendAttachmentState color{};
    Fast::Oot3d::ConfigureOutlineCoverageBlend(color,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT);
    EXPECT_EQ(color.colorWriteMask, 15U);
    EXPECT_TRUE(color.blendEnable);
    EXPECT_EQ(color.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(color.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(color.dstColorBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(color.alphaBlendOp, VK_BLEND_OP_MAX);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Renderer3dsGeometryGuide, OctahedralNormalsKeepTheirDirection) {
    using namespace Fast::Renderer3ds;
    EXPECT_EQ(EncodePicaGeometryGuideNormal({0.0F, 0.0F, 0.0F}), 0U);
    EXPECT_EQ(DecodePicaGeometryGuideNormal(0U), (std::array<float, 3>{}));
    double worst = 0.0;
    for (int i = 0; i < 4096; ++i) {
        // Fibonacci sphere, both hemispheres and the fold.
        const double z = 1.0 - (i + 0.5) * 2.0 / 4096.0, r = std::sqrt(1.0 - z * z);
        const double phi = i * 2.399963229728653;
        const std::array<float, 3> normal{float(r * std::cos(phi)), float(r * std::sin(phi)), float(z)};
        const uint32_t bits = EncodePicaGeometryGuideNormal(normal);
        ASSERT_NE(bits, 0U);
        const auto decoded = DecodePicaGeometryGuideNormal(bits);
        // atan2 of cross and dot: acos near 1 would measure float rounding.
        const double a0 = decoded[0], a1 = decoded[1], a2 = decoded[2];
        const double cx = a1 * normal[2] - a2 * normal[1], cy = a2 * normal[0] - a0 * normal[2],
                     cz = a0 * normal[1] - a1 * normal[0];
        const double dot = a0 * normal[0] + a1 * normal[1] + a2 * normal[2];
        worst = std::max(worst, std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz), dot) * 180.0 / std::numbers::pi);
    }
    // Well below the 0.1 degree the GI self-intersection test tolerates.
    EXPECT_LT(worst, 0.01);
    EXPECT_EQ(DecodePicaGeometryGuideDepth(kPicaGeometryGuideClear[3]), 1.0F);
}

TEST(Oot3dOutlineGeometryGuide, FaceNormalFollowsDepthWritingDraws) {
    using namespace Fast::Oot3d;
    shaderc::Compiler compiler;
    PicaFragmentInstrumentationRequest request;
    request.Source = "#version 450\nlayout(location=5) in vec4 pica_normquat;\n"
                     "layout(location=6) in vec3 pica_view;\n"
                     "layout(location=0) out vec4 pica_color;\n"
                     "void main() { pica_color=vec4(0.2,0.4,0.8,1.0);\n}\n";
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
    request.Draw.DepthTestEnabled = true;
    request.Draw.ColorWriteMask = 0xFU;
    request.Draw.DepthWriteEnabled = true;
    request.RequestedFeatures = PicaShaderInstrumentationFeature::FaceNormalGuide;
    const auto variant = BuildPicaFragmentInstrumentationVariant(request);
    ASSERT_TRUE(variant.Outputs.WritesFaceNormalGuide);
    EXPECT_FALSE(variant.Outputs.WritesOutlineGeometryGuide);
    EXPECT_NE(variant.Source.find("layout(location=6) out uvec4 pica_geometry_guide"), std::string::npos);
    EXPECT_NE(variant.Source.find("pica_geometry_guide.b = 1u;"), std::string::npos);
    const auto compiled = compiler.CompileGlslToSpv(variant.Source, shaderc_fragment_shader, "face_normal.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success) << compiled.GetErrorMessage();
    // Blended depth writers follow the depth too; the outline does not.
    request.Draw.Blend.Enabled = true;
    request.Draw.Blend.SourceRgb = Oot3d::Renderer::NativeBlendFactor::SourceAlpha;
    request.Draw.Blend.DestRgb = Oot3d::Renderer::NativeBlendFactor::OneMinusSourceAlpha;
    EXPECT_TRUE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesFaceNormalGuide);
    request.Draw.Blend = {};
    // Nothing without a depth write, outside the scene or for shadow producers.
    request.Draw.DepthWriteEnabled = false;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesFaceNormalGuide);
    request.Draw.DepthWriteEnabled = true;
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Ui;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesFaceNormalGuide);
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
    request.Draw.FragmentOperationMode = 3U;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesFaceNormalGuide);
    request.Draw.FragmentOperationMode = 0U;
    // A raster initializer stores no surface.
    request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Always;
    const auto initializer = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_TRUE(initializer.Outputs.WritesFaceNormalGuide);
    EXPECT_EQ(initializer.Source.find("pica_geometry_guide.b = 1u;"), std::string::npos);
    // Requested together with the outline: one output, both channel pairs.
    request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Less;
    request.RequestedFeatures |= PicaShaderInstrumentationFeature::OutlineGeometryGuide;
    const auto both = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_TRUE(both.Outputs.WritesFaceNormalGuide && both.Outputs.WritesOutlineGeometryGuide);
    const auto first = both.Source.find("layout(location=6) out");
    EXPECT_EQ(both.Source.find("layout(location=6) out", first + 1), std::string::npos);
    const auto compiledBoth = compiler.CompileGlslToSpv(both.Source, shaderc_fragment_shader, "geometry.frag");
    EXPECT_EQ(compiledBoth.GetCompilationStatus(), shaderc_compilation_status_success)
        << compiledBoth.GetErrorMessage();
}

TEST(Oot3dOutlineGeometryGuide, PreservesNativeDepthAndColorInBothDepthModes) {
    using namespace Fast::Oot3d;
    shaderc::Compiler compiler;
    for (bool explicitDepth : { false, true }) {
        const std::string source = std::string("#version 450\nlayout(location=5) in vec4 pica_normquat;\n"
                                               "layout(location=0) out vec4 pica_color;\n"
                                               "void main() { pica_color=vec4(0.2,0.4,0.8,1.0);\n") +
                                   (explicitDepth ? "gl_FragDepth=0.625;\n" : "") + "}\n";
        PicaFragmentInstrumentationRequest request;
        request.Source = source;
        request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
        request.Draw.DepthTestEnabled = true;
        request.Draw.ColorWriteMask = 0xFU;
        request.Draw.DepthWriteEnabled = true;
        request.RequestedFeatures = PicaShaderInstrumentationFeature::OutlineGeometryGuide;
        const auto variant = BuildPicaFragmentInstrumentationVariant(request);
        ASSERT_TRUE(variant.Outputs.WritesOutlineGeometryGuide);
        EXPECT_NE(variant.Source.find("layout(location=6) out uvec4 pica_geometry_guide"), std::string::npos);
        EXPECT_NE(variant.Source.find("pica_color=vec4(0.2,0.4,0.8,1.0);"), std::string::npos);
        EXPECT_NE(variant.Source.find(explicitDepth ? "floatBitsToUint(gl_FragDepth);"
                                                    : "floatBitsToUint(gl_FragCoord.z);"),
                  std::string::npos);
        const auto compiled =
            compiler.CompileGlslToSpv(variant.Source, shaderc_fragment_shader, "outline_native_geometry.frag");
        EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success) << compiled.GetErrorMessage();
        request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Always;
        const auto clear = BuildPicaFragmentInstrumentationVariant(request);
        // A raster initializer stores the cleared value: no surface, depth 1.
        EXPECT_NE(clear.Source.find("pica_geometry_guide = uvec4(0u, 0u, 0u, 0x3F800000u)"), std::string::npos);
        EXPECT_EQ(clear.Source.find("pica_geometry_guide.r ="), std::string::npos);
        EXPECT_NE(clear.FragmentKey, variant.FragmentKey);
        request.Draw.DepthCompare = Oot3d::Renderer::PicaCompareFunction::Less;
        request.Draw.Blend.Enabled = true;
        EXPECT_TRUE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesOutlineGeometryGuide);
        request.Draw.Blend.SourceRgb = Oot3d::Renderer::NativeBlendFactor::SourceAlpha;
        request.Draw.Blend.DestRgb = Oot3d::Renderer::NativeBlendFactor::OneMinusSourceAlpha;
        EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesOutlineGeometryGuide);
        request.Draw.Blend = {};
        request.Draw.DepthTestEnabled = false;
        EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesOutlineGeometryGuide);
        request.Draw.DepthTestEnabled = true;
        request.Draw.DepthWriteEnabled = false;
        EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesOutlineGeometryGuide);
        request.Draw.DepthWriteEnabled = true;
        request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Ui;
        EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesOutlineGeometryGuide);
        request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
        request.RequestedFeatures = PicaShaderInstrumentationFeature::None;
        EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Applied());
    }
}

TEST(Oot3dDiffuseAlbedoGuide, DepthWritingSceneSurfacesPublishItAndOverlaysLeaveIt) {
    using namespace Fast::Oot3d;
    shaderc::Compiler compiler;
    const std::string source = "#version 450\nlayout(location=5) in vec4 pica_normquat;\n"
                               "layout(location=0) out vec4 pica_color;\n"
                               "void main() { pica_color=vec4(0.2,0.4,0.8,1.0);\n}\n";
    PicaFragmentInstrumentationRequest request;
    request.Source = source;
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
    request.Draw.DepthTestEnabled = true;
    request.Draw.DepthWriteEnabled = true;
    request.Draw.ColorWriteMask = 0xFU;
    request.RequestedFeatures = PicaShaderInstrumentationFeature::DiffuseAlbedoGuide;
    const auto variant = BuildPicaFragmentInstrumentationVariant(request);
    ASSERT_TRUE(variant.Outputs.WritesDiffuseAlbedoGuide);
    EXPECT_NE(variant.Source.find("layout(location=7) out vec4 pica_diffuse_albedo_guide"), std::string::npos);
    // Without per-pixel lighting the albedo is unknown: publish zero.
    EXPECT_NE(variant.Source.find("pica_diffuse_albedo_guide = vec4(0.0);"), std::string::npos);
    EXPECT_NE(variant.Source.find("pica_color=vec4(0.2,0.4,0.8,1.0);"), std::string::npos);
    const auto compiled = compiler.CompileGlslToSpv(variant.Source, shaderc_fragment_shader, "albedo_guide.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success) << compiled.GetErrorMessage();
    // A blended surface that writes depth (a fading NPC) owns the depth and
    // normal the trace reads: it replaces the guide below it.
    request.Draw.Blend.Enabled = true;
    request.Draw.Blend.SourceRgb = Oot3d::Renderer::NativeBlendFactor::SourceAlpha;
    request.Draw.Blend.DestRgb = Oot3d::Renderer::NativeBlendFactor::OneMinusSourceAlpha;
    const auto blended = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_TRUE(blended.Outputs.WritesDiffuseAlbedoGuide);
    EXPECT_FALSE(blended.Outputs.SceneDomainAlbedoOverlay);
    EXPECT_NE(blended.FragmentKey, variant.FragmentKey);
    const auto blendedCompiled = compiler.CompileGlslToSpv(blended.Source, shaderc_fragment_shader, "albedo_blended.frag");
    EXPECT_EQ(blendedCompiled.GetCompilationStatus(), shaderc_compilation_status_success)
        << blendedCompiled.GetErrorMessage();
    // Without depth it is an overlay: the surface below keeps its guide.
    request.Draw.DepthWriteEnabled = false;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesDiffuseAlbedoGuide);
    request.Draw.DepthWriteEnabled = true;
    request.Draw.Blend = {};
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Ui;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesDiffuseAlbedoGuide);
    request.Draw.CompositionDomain = Oot3d::Renderer::PicaCompositionDomain::Scene;
    request.Draw.DepthWriteEnabled = false;
    EXPECT_FALSE(BuildPicaFragmentInstrumentationVariant(request).Outputs.WritesDiffuseAlbedoGuide);
}

TEST(Oot3dNativeFogGuide, CopiesNativeFogResultWithoutChangingNativeColorOrDepth) {
    using namespace Fast::Oot3d;
    constexpr std::string_view source = R"glsl(#version 450
layout(set=0,binding=0,std140) uniform F { vec4 fog_color; } fragment_uniforms;
layout(location=0) out vec4 pica_color;
void main() {
    float fog_factor=0.375;
    vec4 combiner_output=vec4(0.8);
    combiner_output.rgb=mix(fragment_uniforms.fog_color.rgb,combiner_output.rgb,fog_factor);
    gl_FragDepth=0.5;
    pica_color=combiner_output;
}
)glsl";
    PicaFragmentInstrumentationRequest request;
    request.Source = source;
    request.RequestedFeatures = PicaShaderInstrumentationFeature::NativeFogGuide;
    const auto variant = BuildPicaFragmentInstrumentationVariant(request);
    ASSERT_TRUE(variant.Outputs.WritesFogGuide);
    EXPECT_NE(variant.Source.find("pica_fog_guide = vec4(fragment_uniforms.fog_color.rgb, fog_factor)"), std::string::npos);
    EXPECT_NE(variant.Source.find("gl_FragDepth=0.5;"), std::string::npos);
    EXPECT_NE(variant.Source.find("pica_color=combiner_output;"), std::string::npos);
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(variant.Source, shaderc_fragment_shader, "native_fog_guide.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success) << compiled.GetErrorMessage();
    request.RequestedFeatures = PicaShaderInstrumentationFeature::None;
    const auto disabled = BuildPicaFragmentInstrumentationVariant(request);
    EXPECT_FALSE(disabled.Applied());
    EXPECT_TRUE(disabled.Source.empty()); // Cache retains the canonical shader.
    EXPECT_FALSE(disabled.Outputs.WritesFogGuide);
}

TEST(Oot3dNativeFogGuide, NativeFogDisabledWritesNeutralGuide) {
    using namespace Fast::Oot3d;
    PicaFragmentInstrumentationRequest request;
    request.Source = "#version 450\nlayout(location=0) out vec4 pica_color;\n"
                     "void main() { pica_color=vec4(1); }\n";
    request.RequestedFeatures = PicaShaderInstrumentationFeature::NativeFogGuide;
    const auto variant = BuildPicaFragmentInstrumentationVariant(request);
    ASSERT_TRUE(variant.Outputs.WritesFogGuide);
    EXPECT_NE(variant.Source.find("pica_fog_guide = vec4(0.0, 0.0, 0.0, 1.0)"), std::string::npos);
    shaderc::Compiler compiler;
    const auto compiled = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader, "neutral_fog_guide.frag");
    EXPECT_EQ(compiled.GetCompilationStatus(), shaderc_compilation_status_success)
        << compiled.GetErrorMessage();
}

TEST(Oot3dRigidMotion, TemporalShaderVariantsCompile) {
    shaderc::Compiler compiler;
    const auto vertex=Fast::Oot3d::BuildPicaTemporalVertexVariant(
        "#version 450\nlayout(set=0,binding=0,std140) uniform U{uint b;int flip_viewport;uvec4 i[4];vec4 f[96];} uniforms;\n"
        "vec4 pica_output0 = vec4(0,0,0,1);\nvoid exec_shader(){}\n"
        "void main(){exec_shader();gl_Position=pica_output0;}\n",1U);
    const auto fragment=Fast::Oot3d::BuildPicaRigidMotionShaderVariant(
        "#version 450\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1);}\n",2U);
    const auto vertexResult=compiler.CompileGlslToSpv(
        vertex.Source,shaderc_vertex_shader,"rigid_motion.vert");
    const auto fragmentResult=compiler.CompileGlslToSpv(
        fragment.Source,shaderc_fragment_shader,"rigid_motion.frag");
    EXPECT_EQ(vertexResult.GetCompilationStatus(),shaderc_compilation_status_success)
        << vertexResult.GetErrorMessage();
    EXPECT_EQ(fragmentResult.GetCompilationStatus(),shaderc_compilation_status_success)
        << fragmentResult.GetErrorMessage();
}

TEST(Oot3dPicaAmbientOcclusionGuide,
     AmbientGuideFragmentVariantCompiles) {
    constexpr std::string_view source = R"glsl(#version 450
layout(set=0,binding=0) uniform sampler2D source_texture;
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
void main() {
    vec3 oot3d_normal_guide=vec3(0.0,0.0,1.0);
    float oot3d_ao_response=0.25;
    vec3 oot3d_ao_response_rgb=vec3(0.1,0.25,0.75);
    // OOT3D_PICA_AMBIENT_OCCLUSION_GUIDE_READY
    pica_color=texture(source_texture,vec2(0.5));
    pica_normal_guide = vec4(oot3d_normal_guide * 0.5 + 0.5, 1.0);
}
)glsl";
    const auto variant =
        Fast::Oot3d::BuildPicaAmbientOcclusionGuideVariant(
            source, 33U, true);
    ASSERT_TRUE(variant.Applied());
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "pica_ambient_occlusion_guide.frag");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}

TEST(Oot3dPicaSceneDomainGuide,
     OverlayFragmentVariantCompiles) {
    constexpr std::string_view source = R"glsl(#version 450
layout(location=0) out vec4 pica_color;
layout(location=1) out vec4 pica_normal_guide;
void main() {
    pica_color=vec4(1.0,0.5,0.25,0.4);
    pica_normal_guide=vec4(0.5,0.5,1.0,1.0);
}
)glsl";
    const auto variant =
        Fast::Oot3d::BuildPicaSceneDomainGuideVariant(
            source, 42U,
            {Oot3d::Renderer::PicaCompositionDomain::Scene,
             0U, false, true, 0xFU},
            {true, true});
    ASSERT_TRUE(variant.Applied());
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "pica_scene_domain_guide.frag");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}

TEST(Oot3dSceneComposite, ComputeShaderCompiles) {
    const std::string shader =
        Fast::Oot3d::BuildSceneCompositeComputeShader();
    EXPECT_NE(shader.find("binding=4) uniform texture2D normal_guide"),
              std::string::npos);
    EXPECT_NE(shader.find("binding=5) uniform texture2D depth_guide"),
              std::string::npos);
    EXPECT_NE(shader.find("binding=6) uniform texture2D ambient_guide"),
              std::string::npos);
    EXPECT_NE(shader.find("binding=8) uniform sampler"), std::string::npos);
    EXPECT_NE(shader.find(
                  "oot3d_scene_ambient_occlusion_visibility"),
              std::string::npos);
    EXPECT_NE(shader.find("oot3d_toon_outline_edge"),
              std::string::npos);
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        shader,
        shaderc_compute_shader, "scene_composite.comp");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}

TEST(Oot3dLinearSceneColor, TransferFunctionsAndShaderAreExplicit) {
    const auto unknownPolicy =
        Fast::Oot3d::BuildLinearSceneColorPolicy(
            Fast::Oot3d::SceneColorEncoding::Unknown);
    EXPECT_FALSE(unknownPolicy.Valid);
    const auto linearPolicy =
        Fast::Oot3d::BuildLinearSceneColorPolicy(
            Fast::Oot3d::SceneColorEncoding::Linear);
    EXPECT_EQ(linearPolicy.Source,
              Fast::Oot3d::SceneColorEncoding::Linear);
    EXPECT_TRUE(linearPolicy.Valid);
    EXPECT_FALSE(linearPolicy.DecodeSrgb);
    const auto srgbPolicy =
        Fast::Oot3d::BuildLinearSceneColorPolicy(
            Fast::Oot3d::NativePicaSceneColorEncoding());
    EXPECT_EQ(srgbPolicy.Source,
              Fast::Oot3d::SceneColorEncoding::Srgb);
    EXPECT_TRUE(srgbPolicy.Valid);
    EXPECT_TRUE(srgbPolicy.DecodeSrgb);
    EXPECT_NEAR(Fast::Oot3d::SrgbToLinear(0.04045F),
                0.0031308F, 1.0e-6F);
    EXPECT_NEAR(Fast::Oot3d::SrgbToLinear(0.5F),
                0.214041F, 1.0e-5F);
    EXPECT_NEAR(
        Fast::Oot3d::LinearToSrgb(
            Fast::Oot3d::SrgbToLinear(0.5F)),
        0.5F, 1.0e-5F);
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        Fast::Oot3d::BuildLinearSceneColorComputeShader(),
        shaderc_compute_shader, "linear_scene_color.comp");
    EXPECT_EQ(result.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

TEST(Oot3dTemporalHistory, PreservesPreviousViewAndDeduplicatesSceneFrame) {
    Fast::Oot3d::TemporalHistoryManager history;
    Fast::Oot3d::TemporalViewInput input{};
    input.Key = {1U, 2U, 3U};
    input.RenderedFrameId = 10U;
    input.Width = 1280U;
    input.Height = 720U;
    input.Projection = {1, 0, 0, 0, 0, 1, 0, 0,
                        0, 0, 1, 0, 0, 0, 0, 1};
    input.WorldToClip = input.Projection;
    auto first = history.Prepare(input);
    EXPECT_TRUE(first.CameraCut);
    EXPECT_EQ(first.ResetReason,
              Fast::Oot3d::TemporalResetReason::FirstFrame);

    input.RenderedFrameId = 11U;
    input.WorldToClip[12] = 0.25F;
    input.JitterUv = {0.001F, -0.002F};
    auto second = history.Prepare(input);
    EXPECT_TRUE(second.HistoryValid);
    EXPECT_FLOAT_EQ(second.PreviousWorldToClip[12], 0.0F);
    EXPECT_FLOAT_EQ(second.CurrentJitterUv[0], 0.001F);
    EXPECT_FLOAT_EQ(second.PreviousJitterUv[0], 0.0F);
    const auto duplicate = history.Prepare(input);
    EXPECT_EQ(duplicate.HistoryEpoch, second.HistoryEpoch);
    EXPECT_FLOAT_EQ(duplicate.PreviousWorldToClip[12], 0.0F);
}

TEST(Oot3dTemporalHistory, InvalidatesOnProjectionResizeTeleportAndReset) {
    Fast::Oot3d::TemporalHistoryManager history;
    Fast::Oot3d::TemporalViewInput input{};
    input.Key = {4U, 5U, 6U};
    input.Width = 640U;
    input.Height = 480U;
    input.Projection = {1, 0, 0, 0, 0, 1, 0, 0,
                        0, 0, 1, 0, 0, 0, 0, 1};
    input.WorldToClip = input.Projection;
    input.RenderedFrameId = 1U;
    (void)history.Prepare(input);
    input.RenderedFrameId++;
    (void)history.Prepare(input);
    input.RenderedFrameId++;
    input.Width = 800U;
    EXPECT_EQ(history.Prepare(input).ResetReason,
              Fast::Oot3d::TemporalResetReason::ResolutionChanged);
    input.RenderedFrameId++;
    input.Projection[0] = 1.2F;
    input.WorldToClip[0] = 1.2F;
    EXPECT_EQ(history.Prepare(input).ResetReason,
              Fast::Oot3d::TemporalResetReason::ProjectionChanged);
    input.RenderedFrameId++;
    input.Eye[0] = 500.0F;
    EXPECT_EQ(history.Prepare(input).ResetReason,
              Fast::Oot3d::TemporalResetReason::CameraTeleport);
    const uint64_t oldEpoch = history.Epoch();
    history.Reset();
    EXPECT_GT(history.Epoch(), oldEpoch);
    input.RenderedFrameId++;
    EXPECT_TRUE(history.Prepare(input).CameraCut);
}

TEST(Oot3dTemporalHistory, RejectsNonInvertibleViewProjection) {
    Fast::Oot3d::TemporalHistoryManager history;
    Fast::Oot3d::TemporalViewInput input{};
    input.Key = {7U, 8U, 9U};
    input.RenderedFrameId = 1U;
    input.Width = 320U;
    input.Height = 240U;
    const auto state = history.Prepare(input);
    EXPECT_FALSE(state.HistoryValid);
    EXPECT_EQ(state.ResetReason,
              Fast::Oot3d::TemporalResetReason::NonInvertibleViewProjection);
}

TEST(Oot3dMotionVectors, StaticCameraProducesZeroHistoryOffset) {
    const std::array<float, 16> identity{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const auto motion = Fast::Oot3d::ComputeCameraMotionUv(
        {0.25F, 0.75F}, 1.0F, identity, identity,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, true);
    EXPECT_FALSE(motion.Disoccluded);
    EXPECT_NEAR(motion.MotionUv[0], 0.0F, 1.0e-6F);
    EXPECT_NEAR(motion.MotionUv[1], 0.0F, 1.0e-6F);
}

TEST(Oot3dMotionVectors, InvalidHistoryEmitsDisocclusion) {
    const std::array<float, 16> identity{
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const auto motion = Fast::Oot3d::ComputeCameraMotionUv(
        {0.5F, 0.5F}, 1.0F, identity, identity,
        {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, false);
    EXPECT_TRUE(motion.Disoccluded);
}

TEST(Oot3dMotionVectors,
     ReactiveVariantFollowsNativeBlendAndCompositionSemantics) {
    using namespace Fast::Oot3d;
    using BlendEquation = Oot3d::Renderer::NativeBlendEquation;
    using BlendFactor = Oot3d::Renderer::NativeBlendFactor;
    using CompositionDomain = Oot3d::Renderer::PicaCompositionDomain;
    const std::string source =
        "layout(location=0) out vec4 pica_color;\n"
        "layout(location=2) out vec4 pica_material_guide;\n"
        "void main(){ pica_color=vec4(0.5); "
        "pica_material_guide=vec4(0.0); }\n";
    Oot3d::Renderer::NativeBlendState alphaBlend;
    alphaBlend.Enabled = true;
    alphaBlend.SourceRgb = BlendFactor::SourceAlpha;
    alphaBlend.DestRgb = BlendFactor::OneMinusSourceAlpha;
    const PicaReactiveDrawInfo worldDraw{
        alphaBlend, CompositionDomain::Scene, 0U, true, 0xFU};
    const auto world = BuildPicaReactiveShaderVariant(
        source, 42U, worldDraw);
    EXPECT_TRUE(world.Reactive);
    EXPECT_NE(world.FragmentKey, 42U);
    EXPECT_NE(world.Source.find("clamp(pica_color.a"),
              std::string::npos);

    auto hudDraw = worldDraw;
    hudDraw.CompositionDomain = CompositionDomain::Ui;
    const auto hud = BuildPicaReactiveShaderVariant(
        source, 42U, hudDraw);
    EXPECT_FALSE(hud.Reactive);
    EXPECT_TRUE(hud.Source.empty());

    auto opaqueReplacement = worldDraw;
    opaqueReplacement.Blend.SourceRgb = BlendFactor::One;
    opaqueReplacement.Blend.DestRgb = BlendFactor::Zero;
    const auto opaque = BuildPicaReactiveShaderVariant(
        source, 42U, opaqueReplacement);
    EXPECT_FALSE(opaque.Reactive);

    auto sourceColor = worldDraw;
    sourceColor.Blend.SourceRgb = BlendFactor::SourceColor;
    sourceColor.Blend.DestRgb = BlendFactor::One;
    const auto colorPlan = ResolvePicaReactiveMaskPlan(sourceColor);
    EXPECT_EQ(colorPlan.Coverage, PicaReactiveCoverage::SourceColor);
    EXPECT_NE(BuildPicaReactiveMaskAssignment(colorPlan).find(
                  "abs(pica_color.r)"),
              std::string::npos);

    auto minimum = opaqueReplacement;
    minimum.Blend.EquationRgb = BlendEquation::Min;
    const auto minimumPlan = ResolvePicaReactiveMaskPlan(minimum);
    EXPECT_EQ(minimumPlan.Coverage, PicaReactiveCoverage::Full);

    auto noDepth = worldDraw;
    noDepth.DepthTestEnabled = false;
    EXPECT_FALSE(ResolvePicaReactiveMaskPlan(noDepth).Reactive());
}

TEST(Oot3dTemporalAa, RejectsHistoryForDisocclusionAndReactivePixels) {
    EXPECT_FLOAT_EQ(Fast::Oot3d::TemporalHistoryWeight(
                        0.9F, 0.0F, 0.0F, false), 0.0F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::TemporalHistoryWeight(
                        0.9F, 1.0F, 0.0F, true), 0.0F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::TemporalHistoryWeight(
                        0.9F, 0.0F, 1.0F, true), 0.0F);
    EXPECT_NEAR(Fast::Oot3d::TemporalHistoryWeight(
                    0.9F, 0.25F, 0.5F, true), 0.3375F, 1.0e-6F);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dTemporalAa, ComputeShaderCompilesWithClampAndReactiveRejection) {
    const std::string shader = Fast::Oot3d::BuildTemporalAaComputeShader();
    EXPECT_NE(shader.find("minimum_color-span"), std::string::npos);
    EXPECT_NE(shader.find("motion.a"), std::string::npos);
    EXPECT_NE(shader.find("edge_support"), std::string::npos);
    EXPECT_NE(shader.find("relative_luma_span"), std::string::npos);
    EXPECT_NE(shader.find("*edge_support"), std::string::npos);
    EXPECT_NE(shader.find("binding=4) uniform sampler"), std::string::npos);
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        shader, shaderc_compute_shader, "temporal_aa.comp");
    EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dMotionVectors, ComputeShaderCompilesAndDeclaresReactiveContract) {
    const std::string shader = Fast::Oot3d::BuildCameraMotionComputeShader();
    EXPECT_NE(shader.find("previous_uv-uv"), std::string::npos);
    EXPECT_NE(shader.find("disocclusion"), std::string::npos);
    EXPECT_NE(shader.find("material_guide,source_sampler"), std::string::npos);
    EXPECT_NE(shader.find("binding=5,r16f"), std::string::npos);
    EXPECT_NE(shader.find("binding=6) uniform sampler"), std::string::npos);
    EXPECT_NE(shader.find("imageStore(reactive_output"), std::string::npos);
    shaderc::Compiler compiler;
    const auto result = compiler.CompileGlslToSpv(
        shader, shaderc_compute_shader, "motion_vectors.comp");
    EXPECT_EQ(result.GetCompilationStatus(), shaderc_compilation_status_success)
        << result.GetErrorMessage();
}
#endif

TEST(Oot3dPicaLighting, DecodesStructuralAndDynamicRegisters) {
    std::array<uint32_t, 0x300> registers{};
    registers[0x08F] = 1U;
    registers[0x1C6] = 0U;
    registers[0x1C2] = 1U;
    registers[0x1D9] = 3U | (6U << 4U);
    registers[0x1C0] = (255U << 20U) | (128U << 10U) | 64U;
    registers[0x1C3] = 1U | (2U << 2U) | (5U << 4U) | (1U << 16U) |
                       (2U << 22U) | (1U << 27U) | (1U << 28U);
    registers[0x1C4] = (1U << 3U) | (1U << 11U) | (1U << 27U);
    const size_t light3 = 0x140U + 3U * 0x10U;
    registers[light3 + 2U] = (255U << 20U) | (64U << 10U) | 32U;
    registers[light3 + 4U] = 0x40003C00U; // x=1, y=2 in float16
    registers[light3 + 5U] = 0x00004200U; // z=3 in float16
    registers[light3 + 6U] = 0x08000400U; // inverse spotlight direction
    registers[light3 + 7U] = 0x00001C00U;
    registers[light3 + 9U] = 0xFU;
    registers[light3 + 10U] = 0x3F000U; // float20 1.0
    registers[light3 + 11U] = 0x40000U; // float20 2.0
    registers[0x1D0] = 1U << 1U; // D0 absolute disabled
    registers[0x1D1] = 3U;       // D0 uses L dot N
    registers[0x1D2] = 2U;       // D0 scale 4

    const auto state = Fast::Oot3d::DecodePicaFragmentLighting(registers);
    ASSERT_TRUE(state.Enabled);
    EXPECT_EQ(state.ActiveLightCount, 2U);
    EXPECT_EQ(state.LightPermutation[0], 3U);
    EXPECT_EQ(state.LightPermutation[1], 6U);
    EXPECT_FLOAT_EQ(state.GlobalAmbient.Rgb[0], 1.0F);
    EXPECT_NEAR(state.GlobalAmbient.Rgb[1], 128.0F / 255.0F, 1.0e-6F);
    EXPECT_EQ(state.EnvironmentConfiguration, 5U);
    EXPECT_EQ(state.FresnelSelector, 2U);
    EXPECT_EQ(state.BumpMode, Fast::Oot3d::PicaLightingBumpMode::NormalMap);
    EXPECT_EQ(state.BumpTextureUnit, 2U);
    EXPECT_TRUE(state.ClampHighlights);
    EXPECT_TRUE(state.ShadowPrimary);
    const auto& light = state.Lights[3];
    EXPECT_TRUE(light.Directional);
    EXPECT_TRUE(light.TwoSidedDiffuse);
    EXPECT_TRUE(light.GeometricFactor0);
    EXPECT_TRUE(light.GeometricFactor1);
    EXPECT_FLOAT_EQ(light.Position[0], 1.0F);
    EXPECT_FLOAT_EQ(light.Position[1], 2.0F);
    EXPECT_FLOAT_EQ(light.Position[2], 3.0F);
    EXPECT_NEAR(light.SpotDirection[0], 1024.0F / 2047.0F, 1.0e-6F);
    EXPECT_NEAR(light.SpotDirection[1], 2048.0F / 2047.0F, 1.0e-6F);
    EXPECT_NEAR(light.SpotDirection[2], -1024.0F / 2047.0F, 1.0e-6F);
    EXPECT_FLOAT_EQ(light.DistanceAttenuationBias, 1.0F);
    EXPECT_FLOAT_EQ(light.DistanceAttenuationScale, 2.0F);
    EXPECT_FALSE(light.ShadowEnabled);
    EXPECT_FALSE(light.SpotAttenuationEnabled);
    EXPECT_FALSE(light.DistanceAttenuationEnabled);
    EXPECT_FALSE(state.LutSamplers[0].AbsoluteInput);
    EXPECT_EQ(state.LutSamplers[0].Input,
              Fast::Oot3d::PicaLightingLutInput::LightNormal);
    EXPECT_FLOAT_EQ(state.LutSamplers[0].Scale, 4.0F);
}

TEST(Oot3dPicaLighting, DecodesLutCursorEntriesAndValidTables) {
    const auto cursor = Fast::Oot3d::DecodePicaLightingLutWriteCursor(
        (17U << 8U) | 231U);
    EXPECT_EQ(cursor.Index, 231U);
    EXPECT_EQ(cursor.Table, 17U);
    const auto increasing = Fast::Oot3d::DecodePicaLightingLutEntry(
        2048U | (1024U << 12U));
    EXPECT_NEAR(increasing.Value, 2048.0F / 4095.0F, 1.0e-6F);
    EXPECT_NEAR(increasing.Delta, 1024.0F / 2047.0F, 1.0e-6F);
    const auto decreasing = Fast::Oot3d::DecodePicaLightingLutEntry(
        4095U | (0xC00U << 12U));
    EXPECT_FLOAT_EQ(decreasing.Value, 1.0F);
    EXPECT_NEAR(decreasing.Delta, -1024.0F / 2047.0F, 1.0e-6F);
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutTableValid(0U));
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutTableValid(23U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutTableValid(2U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutTableValid(24U));
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(0U, 0U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(1U, 0U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(3U, 8U));
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(4U, 8U));
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(8U, 4U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(0U, 4U));
    EXPECT_TRUE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(0U, 16U));
    EXPECT_FALSE(Fast::Oot3d::IsPicaLightingLutSamplerSupported(7U, 16U));
}

TEST(Oot3dPicaLighting, StructuralKeyIgnoresDynamicLightValues) {
    std::array<uint32_t, 0x300> first{};
    first[0x08F] = 1U;
    first[0x1C6] = 0U;
    auto second = first;
    second[0x140] = 0x0FF000FFU;
    second[0x144] = 0x3C003C00U;
    EXPECT_EQ(Fast::Oot3d::ComputePicaFragmentLightingStructuralKey(first),
              Fast::Oot3d::ComputePicaFragmentLightingStructuralKey(second));
    second[0x149] = 1U;
    EXPECT_NE(Fast::Oot3d::ComputePicaFragmentLightingStructuralKey(first),
              Fast::Oot3d::ComputePicaFragmentLightingStructuralKey(second));
}

TEST(Oot3dPicaShadow2d, DetilesPackedDepthAndComparesPenumbra) {
    std::vector<uint8_t> tiled(8U * 8U * sizeof(uint32_t));
    const uint32_t packed = (0x654321U << 8U) | 0x80U;
    const size_t mortonX3Y2 = 13U;
    std::memcpy(tiled.data() + mortonX3Y2 * sizeof(uint32_t),
                &packed, sizeof(packed));
    std::vector<uint32_t> linear;
    std::string error;
    ASSERT_TRUE(Fast::Oot3d::DetilePicaShadow2d(
        tiled, 5U, 4U, linear, &error)) << error;
    ASSERT_EQ(linear.size(), 20U);
    EXPECT_EQ(linear[2U * 5U + 3U], packed);
    EXPECT_FLOAT_EQ(Fast::Oot3d::ComparePicaShadow2d(
                        packed, 0x654321U), 0.0F);
    EXPECT_NEAR(Fast::Oot3d::ComparePicaShadow2d(
                    packed, 0x123456U), 128.0F / 255.0F, 1.0e-6F);
    EXPECT_FALSE(Fast::Oot3d::DetilePicaShadow2d(
        std::span<const uint8_t>(tiled).first(12U), 5U, 4U,
        linear, &error));
}

TEST(Oot3dPicaShadow2d, PacksAndUpdatesDepthPenumbraAtomically) {
    EXPECT_FLOAT_EQ(Fast::Oot3d::DecodePicaFloat16(0x3C00U), 1.0F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::DecodePicaFloat16(0x3800U), 0.5F);
    EXPECT_EQ(Fast::Oot3d::PackPicaShadow2d(0x123456U, 0x80U),
              0x12345680U);

    const uint32_t initial = Fast::Oot3d::PackPicaShadow2d(0xF00000U, 200U);
    EXPECT_EQ(Fast::Oot3d::UpdatePicaShadow2d(
                  initial, 0xE00000U, 0U, 1.0F, 0.0F),
              Fast::Oot3d::PackPicaShadow2d(0xE00000U, 200U));
    EXPECT_EQ(Fast::Oot3d::UpdatePicaShadow2d(
                  initial, 0xE00000U, 100U, 1.0F, 0.0F),
              Fast::Oot3d::PackPicaShadow2d(0xF00000U, 100U));
    EXPECT_EQ(Fast::Oot3d::UpdatePicaShadow2d(
                  initial, 0xF10000U, 1U, 1.0F, 0.0F),
              initial);
}

TEST(Oot3dPicaToon, ToonPresetKeepsApprovedStyleDefaults) {
    const auto preset = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Toon);
    EXPECT_EQ(preset.Effects.Toon,
              Fast::Oot3d::ToonMode::PostProcessPreview);
    EXPECT_EQ(preset.Effects.ToonStyle.LightBands, 4U);
    EXPECT_FLOAT_EQ(preset.Effects.ToonStyle.BandSoftness, 0.228F);
    EXPECT_FLOAT_EQ(preset.Effects.ToonStyle.Saturation, 1.09F);
    EXPECT_FLOAT_EQ(preset.Effects.ToonStyle.ShadowStrength, 1.0F);
    EXPECT_FLOAT_EQ(preset.Effects.ToonStyle.RimStrength, 0.34F);
    EXPECT_FLOAT_EQ(preset.Effects.ToonStyle.RimWidth, 3.17F);
}

TEST(Oot3dPicaToon, ScanoutComposesOutlineBeforeFxaaOnly) {
    const auto shader = Fast::Oot3d::BuildPicaScanoutFragmentShader();
    const auto ao = shader.find(
        "color.rgb *= oot3d_scene_ambient_occlusion_visibility");
    const auto outline = shader.find(
        "color.rgb = mix(color.rgb, outline_color");
    const auto fxaa = shader.find("oot3d_fxaa(source_uv, center)");
    ASSERT_NE(ao, std::string::npos);
    ASSERT_NE(outline, std::string::npos);
    ASSERT_NE(fxaa, std::string::npos);
    EXPECT_EQ(
        shader.find("oot3d_smaa1x(source_uv, center)"),
        std::string::npos);
    EXPECT_LT(ao, outline);
    EXPECT_LT(outline, fxaa);
    EXPECT_NE(shader.find("texelFetch(scene_depth"), std::string::npos);
    EXPECT_NE(shader.find("texelFetch(outline_geometry_guide"), std::string::npos);
    EXPECT_NE(shader.find("outline_normal_sensitivity"), std::string::npos);
    EXPECT_NE(shader.find("oot3d_outline_excluded"), std::string::npos);
    EXPECT_NE(shader.find("dot(center_guide.xyz, center_guide.xyz) < 0.000001"), std::string::npos);
    EXPECT_EQ(shader.find("luma_center", 0, outline), std::string::npos)
        << "outline eligibility must not use HUD or texture color edges";
}

TEST(Oot3dPicaToon, OutlineRestoresSceneGuidesWithNativeWidthProtection) {
    const auto shader =
        Fast::Oot3d::ToonOutlineComputeShaderLibrary();
    EXPECT_NE(
        shader.find("sampler2D(depth_guide,composite_sampler)"),
        std::string::npos);
    EXPECT_NE(shader.find("oot3d_outline_excluded(center_guide.a)"), std::string::npos);
    EXPECT_NE(shader.find("!excluded || center_depth + 0.0000001 < depth"), std::string::npos);
    // The integer geometry guide is fetched without the linear sampler.
    EXPECT_NE(shader.find("texelFetch(outline_geometry_guide,"), std::string::npos);
    EXPECT_EQ(shader.find("sampler2D(outline_geometry_guide,"), std::string::npos);
    EXPECT_NE(shader.find("sampler2D(normal_guide,composite_sampler)"), std::string::npos);
    const auto gather = shader.substr(shader.find("for (int index = 0; index < 8; ++index)"));
    EXPECT_NE(gather.find("oot3d_outline_sample_geometry(neighbor_uv)"), std::string::npos);
    EXPECT_NE(gather.find("oot3d_outline_world_depth(neighbor_uv, guide.a)"), std::string::npos);
    EXPECT_NE(shader.find("oot3d_outline_excluded(guide_alpha) ? oot3d_outline_sample_scene_depth(uv)"),
              std::string::npos);
    EXPECT_NE(gather.find("if (extension_in_footprint)"), std::string::npos);
    EXPECT_NE(gather.find("if (native_contour.x > 0.0) contour = native_contour"), std::string::npos);
}

TEST(Oot3dPicaToon, CoverageOnlyHidesFinishedNativeContours) {
    const auto kernel = Fast::Oot3d::ToonOutlineShaderLibrary();
    const auto visibility = kernel.find("float oot3d_toon_outline_edge(");
    ASSERT_NE(visibility, std::string::npos);
    const auto detector = kernel.substr(0, visibility);
    EXPECT_NE(detector.find("oot3d_toon_outline_native_edge"), std::string::npos);
    EXPECT_NE(detector.find("oot3d_toon_outline_scene_edge"), std::string::npos);
    const auto composite = kernel.substr(visibility);
    EXPECT_NE(composite.find("1.0 - occlusion < contour.y - 0.00001 ? 0.0 : contour.x"), std::string::npos);
    EXPECT_EQ(composite.find("oot3d_outline_sample_geometry(uv).a"), std::string::npos);
    EXPECT_EQ(composite.find("neighbor_uv"), std::string::npos);
}

TEST(Oot3dPicaScanout, PolicySuppressesAllEffectsAlreadyComposited) {
    Fast::Oot3d::PicaScanoutPolicyInput input;
    input.Width = 960;
    input.Height = 540;
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaScanoutPolicyInput(input));
    input.InputEncoding = Fast::Oot3d::SceneColorEncoding::Srgb;
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaScanoutPolicyInput(input));
    input.AmbientOcclusionAvailable = true;
    input.OutlineAvailable = true;
    input.ReflectionsAvailable = true;
    input.TemporalOutput = true;
    input.EffectsComposited = true;
    auto output = Fast::Oot3d::BuildPicaScanoutPushConstants(input);
    EXPECT_EQ(output.AmbientOcclusion, 0U);
    EXPECT_EQ(output.Reflections, 0U);
    EXPECT_EQ(output.Outline, 0U);

    input.EffectsComposited = false;
    output = Fast::Oot3d::BuildPicaScanoutPushConstants(input);
    EXPECT_EQ(output.AmbientOcclusion, 1U);
    EXPECT_EQ(output.Reflections, 1U);
    EXPECT_EQ(output.Outline, 1U);

    // An MXAO debug view is shown only by the scene composite, never applied
    // as occlusion at scanout.
    input.Effects.AmbientOcclusion = Fast::Oot3d::AmbientOcclusionMode::Mxao;
    input.Effects.MxaoDebug = Fast::Oot3d::MxaoDebugView::DepthNormals;
    EXPECT_EQ(Fast::Oot3d::BuildPicaScanoutPushConstants(input).AmbientOcclusion, 0U);
    input.Effects.MxaoDebug = Fast::Oot3d::MxaoDebugView::Off;
    EXPECT_EQ(Fast::Oot3d::BuildPicaScanoutPushConstants(input).AmbientOcclusion, 1U);

    input.InputEncoding = Fast::Oot3d::SceneColorEncoding::Linear;
    input.TargetSrgb = false;
    output = Fast::Oot3d::BuildPicaScanoutPushConstants(input);
    EXPECT_EQ(output.InputLinear, 1U);
    EXPECT_EQ(output.EncodeSrgb, 1U);
    input.TargetSrgb = true;
    output = Fast::Oot3d::BuildPicaScanoutPushConstants(input);
    EXPECT_EQ(output.InputLinear, 1U);
    EXPECT_EQ(output.EncodeSrgb, 0U)
        << "an sRGB attachment performs the final encode";
}

TEST(Oot3dPicaScanout, PolicyCopiesViewAndArtSettings) {
    Fast::Oot3d::PicaScanoutPolicyInput input;
    input.TransferFlags = 1U;
    input.AaMode = 2U;
    input.Width = 1280U;
    input.Height = 720U;
    input.InputEncoding = Fast::Oot3d::SceneColorEncoding::Srgb;
    input.HiZMipCount = 11U;
    input.Effects.ToonStyle.OutlineWidth = 2.5F;
    input.Effects.ToonStyle.OutlineTint = {0.2F, 0.3F, 0.4F};
    input.Effects.ReflectionStrength = 0.75F;
    Fast::Oot3d::PerspectiveViewState perspective;
    perspective.Projection[0] = 1.25F;
    perspective.Projection[5] = 2.0F;
    perspective.Projection[8] = 0.1F;
    perspective.Projection[9] = -0.2F;
    perspective.NearPlane = 0.25F;
    perspective.FarPlane = 900.0F;
    input.Perspective = perspective;

    const auto output =
        Fast::Oot3d::BuildPicaScanoutPushConstants(input);
    EXPECT_EQ(output.FlipY, 0U);
    EXPECT_EQ(output.AaMode, 2U);
    EXPECT_FLOAT_EQ(output.InvWidth, 1.0F / 1280.0F);
    EXPECT_FLOAT_EQ(output.InvHeight, 1.0F / 720.0F);
    EXPECT_FLOAT_EQ(output.OutlineWidth, 2.5F * 720.0F / 1080.0F);
    EXPECT_FLOAT_EQ(output.OutlineColor[1], 0.3F);
    EXPECT_FLOAT_EQ(output.ReflectionStrength, 0.75F);
    EXPECT_EQ(output.HiZMipCount, 11U);
    EXPECT_FLOAT_EQ(output.ProjectionScaleX, 1.25F);
    EXPECT_FLOAT_EQ(output.ProjectionScaleY, 2.0F);
    EXPECT_FLOAT_EQ(output.ProjectionOffsetX, 0.1F);
    EXPECT_FLOAT_EQ(output.ProjectionOffsetY, -0.2F);
    EXPECT_FLOAT_EQ(output.NearPlane, 0.25F);
    EXPECT_FLOAT_EQ(output.FarPlane, 900.0F);
}

TEST(Oot3dPicaScanout, NriShaderUsesSeparateTexturesAndSampler) {
    const auto vertex = Fast::Oot3d::BuildPicaScanoutVertexShader();
    const auto fragment =
        Fast::Oot3d::BuildPicaScanoutFragmentShader(true);
    EXPECT_NE(vertex.find("gl_VertexIndex"), std::string::npos);
    EXPECT_NE(fragment.find("uniform texture2D physical_scanout"),
              std::string::npos);
    EXPECT_NE(fragment.find("binding = 7) uniform texture2D ambient_guide"),
              std::string::npos);
    EXPECT_NE(fragment.find("binding = 11) uniform sampler scanout_sampler"), std::string::npos);
    EXPECT_NE(fragment.find(
                  "texture(sampler2D(physical_scanout, scanout_sampler),"),
              std::string::npos);
    EXPECT_EQ(fragment.find("uniform sampler2D physical_scanout"),
              std::string::npos);
    // The integer geometry guide is fetched without the linear sampler.
    EXPECT_NE(fragment.find("binding = 10) uniform utexture2D outline_geometry_guide"), std::string::npos);
    EXPECT_NE(fragment.find("texelFetch(outline_geometry_guide,"), std::string::npos);
    EXPECT_EQ(fragment.find("usampler2D(outline_geometry_guide"), std::string::npos);
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    const auto vertexSpirv = compiler.CompileGlslToSpv(
        vertex, shaderc_vertex_shader, "nri_pica_scanout.vert", options);
    const auto fragmentSpirv = compiler.CompileGlslToSpv(
        fragment, shaderc_fragment_shader, "nri_pica_scanout.frag",
        options);
    EXPECT_EQ(vertexSpirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << vertexSpirv.GetErrorMessage();
    EXPECT_EQ(fragmentSpirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << fragmentSpirv.GetErrorMessage();
#endif
}

TEST(Oot3dPicaNri, CompilesMixedSamplerTypesUnderTheNriContract) {
    const std::string source =
        "#version 450\n"
        "layout(set=0,binding=1) uniform usampler2D pica_texture0;\n"
        "layout(set=0,binding=2) uniform sampler2D pica_texture1;\n"
        "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){uvec4 a=texelFetch(pica_texture0,ivec2(0),0);"
        "vec4 b=texture(pica_texture1,vec2(0));"
        "ivec2 s=textureSize(pica_texture2,0);"
        "color=vec4(a)+b+vec4(s,0,0);}\n";
    const auto variant =
        Fast::Oot3d::BuildPicaNriFragmentShaderVariant(source);
    ASSERT_TRUE(variant.Applied) << variant.Error;
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaNriDescriptorContract());
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    const auto spirv = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "nri_pica_contract.frag", options);
    EXPECT_EQ(spirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << spirv.GetErrorMessage();
#endif
}

TEST(Oot3dPicaNri, PreservesNativeShadowStorageImageWithUnsignedSampler) {
    const std::string source =
        "#version 450\n"
        "layout(set=0,binding=1) uniform usampler2D pica_texture0;\n"
        "layout(set=0,binding=2) uniform sampler2D pica_texture1;\n"
        "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n"
        "layout(set=0,binding=5,r32ui) uniform uimage2D pica_shadow_buffer;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){ivec2 p=ivec2(0);uint old=imageLoad(pica_shadow_buffer,p).x;"
        "uint seen=imageAtomicCompSwap(pica_shadow_buffer,p,old,old);"
        "color=vec4(texelFetch(pica_texture0,p,0))+vec4(seen)"
        "+texture(pica_texture1,vec2(0))+texture(pica_texture2,vec2(0));}\n";
    const auto variant =
        Fast::Oot3d::BuildPicaNriFragmentShaderVariant(source);
    ASSERT_TRUE(variant.Applied) << variant.Error;
    EXPECT_NE(variant.Source.find(
                  "binding=1) uniform utexture2D pica_texture0_image"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "binding=5,r32ui) uniform uimage2D pica_shadow_buffer"),
              std::string::npos);
    EXPECT_NE(variant.Source.find("imageAtomicCompSwap(pica_shadow_buffer"),
              std::string::npos);
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    const auto spirv = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "nri_pica_native_shadow_contract.frag", options);
    EXPECT_EQ(spirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << spirv.GetErrorMessage();
#endif
}


TEST(Oot3dPicaNri, ConvertsNativeLightingLutSamplerWhenPresent) {
    const std::string source =
        "#version 450\n"
        "layout(set=0,binding=1) uniform sampler2D pica_texture0;\n"
        "layout(set=0,binding=2) uniform sampler2D pica_texture1;\n"
        "layout(set=0,binding=3) uniform sampler2D pica_texture2;\n"
        "layout(set=0,binding=13) uniform usampler2D pica_lighting_lut;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){uint v=texelFetch(pica_lighting_lut,ivec2(0),0).r;"
        "color=vec4(v)+texture(pica_texture0,vec2(0))"
        "+texture(pica_texture1,vec2(0))+texture(pica_texture2,vec2(0));}\n";
    const auto variant =
        Fast::Oot3d::BuildPicaNriFragmentShaderVariant(source);
    ASSERT_TRUE(variant.Applied) << variant.Error;
    EXPECT_NE(variant.Source.find(
                  "binding=13) uniform utexture2D pica_lighting_lut_image"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "binding=14) uniform sampler pica_lighting_lut_sampler"),
              std::string::npos);
    EXPECT_NE(variant.Source.find(
                  "#define pica_lighting_lut usampler2D(pica_lighting_lut_image,pica_lighting_lut_sampler)"),
              std::string::npos);
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    const auto spirv = compiler.CompileGlslToSpv(
        variant.Source, shaderc_fragment_shader,
        "nri_pica_lighting_lut_contract.frag", options);
    EXPECT_EQ(spirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << spirv.GetErrorMessage();
#endif
}

TEST(Oot3dPicaNri, RejectsIncompleteContractForVulkanFallback) {
    const std::string source =
        "#version 450\n"
        "layout(set=0,binding=1) uniform sampler2D pica_texture0;\n"
        "void main(){}\n";
    const auto variant =
        Fast::Oot3d::BuildPicaNriFragmentShaderVariant(source);
    EXPECT_FALSE(variant.Applied);
    EXPECT_FALSE(variant.Error.empty());
    EXPECT_EQ(variant.Source, source);
}

TEST(Oot3dPicaNri, CanonicalVertexInputPreservesScaledValuesAndDefaults) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriSourceVertexBinding, 1> bindings{{
        {2U, 8U, false},
    }};
    const std::array<PicaNriSourceVertexAttribute, 2> attributes{{
        {5U, 2U, PicaNriVertexScalar::SignedByte, 3U, 0U},
        {1U, 2U, PicaNriVertexScalar::SignedShort, 2U, 4U},
    }};
    const auto layout =
        BuildPicaNriVertexInputLayout(bindings, attributes);
    ASSERT_TRUE(layout.Valid()) << layout.Error;
    ASSERT_EQ(layout.Bindings.size(), 1U);
    EXPECT_EQ(layout.Bindings[0].ByteStride, 32U);
    ASSERT_EQ(layout.Attributes.size(), 2U);
    EXPECT_EQ(layout.Attributes[0].Location, 1U);
    EXPECT_EQ(layout.Attributes[0].ByteOffset, 0U);
    EXPECT_EQ(layout.Attributes[1].Location, 5U);
    EXPECT_EQ(layout.Attributes[1].ByteOffset, 16U);

    const std::array<uint8_t, 8> source{{
        0xFEU, 0x7FU, 0x80U, 0U,
        0x34U, 0x12U, 0x00U, 0x80U,
    }};
    std::vector<uint8_t> packed;
    std::string error;
    ASSERT_TRUE(RepackPicaNriVertexBinding(
        bindings[0], layout.Attributes, layout.Bindings[0].ByteStride,
        source, packed, &error)) << error;
    ASSERT_EQ(packed.size(), 32U);
    std::array<float, 8> values{};
    std::memcpy(values.data(), packed.data(), packed.size());
    EXPECT_FLOAT_EQ(values[0], 4660.0F);
    EXPECT_FLOAT_EQ(values[1], -32768.0F);
    EXPECT_FLOAT_EQ(values[2], 0.0F);
    EXPECT_FLOAT_EQ(values[3], 1.0F);
    EXPECT_FLOAT_EQ(values[4], -2.0F);
    EXPECT_FLOAT_EQ(values[5], 127.0F);
    EXPECT_FLOAT_EQ(values[6], -128.0F);
    EXPECT_FLOAT_EQ(values[7], 1.0F);
}

TEST(Oot3dPicaNri, VertexInputLayoutOrdersBindingsAndRejectsDuplicates) {
    using namespace Fast::Oot3d;
    const std::array<PicaNriSourceVertexBinding, 2> bindings{{
        {3U, 12U, false},
        {1U, 4U, true},
    }};
    const std::array<PicaNriSourceVertexAttribute, 3> attributes{{
        {7U, 1U, PicaNriVertexScalar::UnsignedByte, 4U, 0U},
        {4U, 3U, PicaNriVertexScalar::Float, 1U, 8U},
        {2U, 3U, PicaNriVertexScalar::Float, 2U, 0U},
    }};
    const auto layout = BuildPicaNriVertexInputLayout(bindings, attributes);
    ASSERT_TRUE(layout.Valid()) << layout.Error;
    // Bindings keep input order; attributes are grouped per binding and
    // sorted by location inside it.
    ASSERT_EQ(layout.Bindings.size(), 2U);
    EXPECT_EQ(layout.Bindings[0].Binding, 3U);
    EXPECT_EQ(layout.Bindings[0].ByteStride, 32U);
    EXPECT_EQ(layout.Bindings[1].Binding, 1U);
    EXPECT_TRUE(layout.Bindings[1].PerInstance);
    ASSERT_EQ(layout.Attributes.size(), 3U);
    EXPECT_EQ(layout.Attributes[0].Location, 2U);
    EXPECT_EQ(layout.Attributes[1].Location, 4U);
    EXPECT_EQ(layout.Attributes[1].ByteOffset, 16U);
    EXPECT_EQ(layout.Attributes[2].Location, 7U);
    EXPECT_EQ(layout.Attributes[2].Binding, 1U);

    const std::array<PicaNriSourceVertexBinding, 2> duplicateBindings{{
        {3U, 12U, false},
        {3U, 8U, false},
    }};
    EXPECT_EQ(BuildPicaNriVertexInputLayout(duplicateBindings, attributes).Error,
              "invalid or duplicate PICA vertex binding");
    const std::array<PicaNriSourceVertexAttribute, 2> duplicateLocations{{
        {4U, 3U, PicaNriVertexScalar::Float, 1U, 0U},
        {4U, 1U, PicaNriVertexScalar::UnsignedByte, 1U, 0U},
    }};
    EXPECT_EQ(BuildPicaNriVertexInputLayout(bindings, duplicateLocations).Error,
              "invalid PICA vertex attribute");
}

TEST(Oot3dPicaNri, BuildsCanonicalVertexStreamsForSubmission) {
    using namespace Fast::Oot3d;
    const std::array<uint8_t, 8> source{{
        0xFEU, 0x03U, 0x04U, 0x00U,
        0x05U, 0x06U, 0x07U, 0x00U,
    }};
    const std::array<PicaNriSourceVertexStream, 1> streams{{
        {{2U, 4U, false}, source},
    }};
    const std::array<PicaNriSourceVertexAttribute, 1> attributes{{
        {0U, 2U, PicaNriVertexScalar::SignedByte, 3U, 0U},
    }};
    std::vector<PicaNriPackedVertexStream> packed;
    std::string error;

    ASSERT_TRUE(BuildPicaNriPackedVertexStreams(
        streams, attributes, packed, &error)) << error;
    ASSERT_EQ(packed.size(), 1U);
    EXPECT_EQ(packed[0].Binding, 2U);
    EXPECT_EQ(packed[0].ByteStride, 4U * sizeof(float));
    ASSERT_EQ(packed[0].Bytes.size(), 2U * packed[0].ByteStride);
    std::array<float, 8> values{};
    std::memcpy(
        values.data(), packed[0].Bytes.data(), packed[0].Bytes.size());
    EXPECT_FLOAT_EQ(values[0], -2.0F);
    EXPECT_FLOAT_EQ(values[1], 3.0F);
    EXPECT_FLOAT_EQ(values[2], 4.0F);
    EXPECT_FLOAT_EQ(values[3], 1.0F);
    EXPECT_FLOAT_EQ(values[4], 5.0F);
    EXPECT_FLOAT_EQ(values[5], 6.0F);
    EXPECT_FLOAT_EQ(values[6], 7.0F);
    EXPECT_FLOAT_EQ(values[7], 1.0F);
}

TEST(Oot3dPicaRendering, ValidatesSingleAndMultisampleAttachmentContracts) {
    EXPECT_EQ(Fast::Oot3d::kPicaColorAttachmentCount, 8U);
    EXPECT_EQ(Fast::Oot3d::kPicaBaseInstrumentedColorAttachmentCount, 7U);
    EXPECT_EQ(Fast::Oot3d::PicaAttachmentIndex(Fast::Oot3d::PicaColorAttachment::DiffuseAlbedoGuide), 7U);
    EXPECT_EQ(Fast::Oot3d::PicaAttachmentIndex(Fast::Oot3d::PicaColorAttachment::FogGuide), 5U);
    EXPECT_EQ(Fast::Oot3d::PicaAttachmentIndex(
                  Fast::Oot3d::PicaColorAttachment::AmbientGuide),
              4U);
    const auto view =
        reinterpret_cast<VkImageView>(static_cast<uintptr_t>(1U));
    Fast::Oot3d::PicaDynamicRenderingTarget target;
    target.Colors.fill(view);
    target.Depth = view;
    target.Width = 1280U;
    target.Height = 720U;
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaDynamicRenderingTarget(
        target, VK_RESOLVE_MODE_NONE));
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaNriRenderingTarget(
        target, VK_RESOLVE_MODE_NONE));
    target.ColorImages.fill(
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(2U)));
    target.ColorFormats.fill(VK_FORMAT_R8G8B8A8_UNORM);
    target.DepthImage =
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(3U));
    target.DepthFormat = VK_FORMAT_D32_SFLOAT;
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaNriRenderingTarget(
        target, VK_RESOLVE_MODE_NONE));

    target.Samples = VK_SAMPLE_COUNT_4_BIT;
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaDynamicRenderingTarget(
        target, VK_RESOLVE_MODE_MIN_BIT));
    target.Resolves.fill(view);
    target.ResolveImages.fill(
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(4U)));
    target.DepthResolve = view;
    target.DepthResolveImage =
        reinterpret_cast<VkImage>(static_cast<uintptr_t>(5U));
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaDynamicRenderingTarget(
        target, VK_RESOLVE_MODE_NONE));
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaDynamicRenderingTarget(
        target, VK_RESOLVE_MODE_MIN_BIT));
    EXPECT_TRUE(Fast::Oot3d::ValidatePicaNriRenderingTarget(
        target, VK_RESOLVE_MODE_MIN_BIT));
    target.Resolves[2] = VK_NULL_HANDLE;
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaDynamicRenderingTarget(
        target, VK_RESOLVE_MODE_MIN_BIT));
    EXPECT_FALSE(Fast::Oot3d::ValidatePicaNriRenderingTarget(
        target, VK_RESOLVE_MODE_MIN_BIT));
}

TEST(Oot3dPicaNriDrawOwnership,
     PreparesBeforeAndFinalizesAgainstTheCurrentRenderingScope) {
    using Fast::Oot3d::FinalizePicaNriDrawOwnership;
    using Fast::Oot3d::PicaNriVulkanFallbackAllowed;
    using Fast::Oot3d::PreparePicaNriDrawOwnership;

    const auto beforeScope =
        PreparePicaNriDrawOwnership(true, true);
    EXPECT_TRUE(beforeScope.Prepared);
    EXPECT_FALSE(beforeScope.SubmitOwned);

    const auto currentScope =
        FinalizePicaNriDrawOwnership(beforeScope, true);
    EXPECT_TRUE(currentScope.Prepared);
    EXPECT_TRUE(currentScope.ScopeRequiresOwnedSubmission);
    EXPECT_TRUE(currentScope.SubmitOwned);
    EXPECT_FALSE(PicaNriVulkanFallbackAllowed(currentScope));

    const auto unavailableInOwnedScope =
        FinalizePicaNriDrawOwnership(
            PreparePicaNriDrawOwnership(false, true), true);
    EXPECT_FALSE(unavailableInOwnedScope.Prepared);
    EXPECT_TRUE(
        unavailableInOwnedScope.ScopeRequiresOwnedSubmission);
    EXPECT_FALSE(unavailableInOwnedScope.SubmitOwned);
    EXPECT_FALSE(
        PicaNriVulkanFallbackAllowed(unavailableInOwnedScope));

    EXPECT_FALSE(
        PreparePicaNriDrawOwnership(true, false).Prepared);

    const auto explicitVulkanScope =
        FinalizePicaNriDrawOwnership(
            PreparePicaNriDrawOwnership(true, true), false);
    EXPECT_TRUE(PicaNriVulkanFallbackAllowed(explicitVulkanScope));
    EXPECT_FALSE(explicitVulkanScope.SubmitOwned);
}

TEST(Oot3dPicaGuideSamplingBarriers,
      CoversFiveGuidesAndComputeConsumersBidirectionally) {
    using namespace Fast::Oot3d;
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    input.Reflections = ReflectionMode::FidelityFxSssr;
    input.ForceMotion = true;
    const auto plan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    DisplayEffectResourceTable resources;
    const auto bind = [&resources](EffectResource resource,
                                   uintptr_t image,
                                   uintptr_t view) {
        return resources.BindImage(
            resource, image, view, 37U, 400U, 240U, 1U);
    };
    ASSERT_TRUE(bind(EffectResource::NativeDepth, 1U, 11U));
    ASSERT_TRUE(bind(EffectResource::NormalGuide, 2U, 12U));
    ASSERT_TRUE(bind(EffectResource::MaterialGuide, 3U, 13U));
    ASSERT_TRUE(bind(EffectResource::RigidMotionGuide, 4U, 14U));
    ASSERT_TRUE(bind(EffectResource::AmbientGuide, 5U, 15U));
    const auto* ao = plan.FindPass(DisplayEffectPass::AmbientOcclusion);
    const auto* motion = plan.FindPass(DisplayEffectPass::Motion);
    const auto* scanout = plan.FindPass(DisplayEffectPass::Scanout);
    ASSERT_NE(ao, nullptr);
    ASSERT_NE(motion, nullptr);
    ASSERT_NE(scanout, nullptr);
    const auto aoSampled = BuildPicaGuideSamplingBarriers(
        *ao, resources, 0U);
    ASSERT_EQ(aoSampled.Count, 2U);
    EXPECT_TRUE(aoSampled.Complete());
    EXPECT_EQ(aoSampled.Images[0].image,
              reinterpret_cast<VkImage>(1U));
    EXPECT_EQ(aoSampled.Images[0].oldLayout,
              VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EXPECT_EQ(aoSampled.Images[0].newLayout,
              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    const auto motionSampled = BuildPicaGuideSamplingBarriers(
        *motion, resources, aoSampled.ResourceMask);
    ASSERT_EQ(motionSampled.Count, 2U);
    const uint32_t firstFourMask = aoSampled.ResourceMask |
                                   motionSampled.ResourceMask;
    const auto scanoutSampled = BuildPicaGuideSamplingBarriers(
        *scanout, resources, firstFourMask);
    ASSERT_EQ(scanoutSampled.Count, 1U);
    EXPECT_EQ(scanoutSampled.Images[0].image,
              reinterpret_cast<VkImage>(5U));
    EXPECT_EQ(scanoutSampled.Images[0].subresourceRange.aspectMask,
              VK_IMAGE_ASPECT_COLOR_BIT);
    EXPECT_NE(aoSampled.DestinationStages &
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              0U);
    EXPECT_NE(aoSampled.DestinationStages &
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              0U);

    const uint32_t sampledMask = firstFourMask |
                                 scanoutSampled.ResourceMask;
    const auto attachments = BuildPicaGuideAttachmentBarriers(
        resources, sampledMask);
    ASSERT_EQ(attachments.Count, 5U);
    EXPECT_EQ(attachments.ResourceMask, sampledMask);
    for (size_t index = 0; index < attachments.Count; ++index) {
        EXPECT_EQ(attachments.Images[index].srcAccessMask,
                  VK_ACCESS_SHADER_READ_BIT);
    }
    EXPECT_NE(attachments.SourceStages &
                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
              0U)
        << "scene composite compute reads must complete before raster";
    EXPECT_NE(attachments.SourceStages &
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
              0U)
        << "direct scanout fragment reads must complete before raster";

    input.Reflections = ReflectionMode::Off;
    input.ForceMotion = false;
    const auto selectedPlan = BuildDisplayEffectPlan(input);
    ASSERT_TRUE(selectedPlan.Graph.Valid()) << selectedPlan.Graph.Error;
    const auto* selectedAo = selectedPlan.FindPass(
        DisplayEffectPass::AmbientOcclusion);
    const auto* selectedScanout = selectedPlan.FindPass(
        DisplayEffectPass::Scanout);
    ASSERT_NE(selectedAo, nullptr);
    ASSERT_NE(selectedScanout, nullptr);
    const auto selected = BuildPicaGuideSamplingBarriers(
        *selectedAo, resources, 0U);
    ASSERT_EQ(selected.Count, 2U);
    EXPECT_EQ(selected.Images[0].image,
              reinterpret_cast<VkImage>(1U));
    EXPECT_EQ(selected.Images[1].image,
              reinterpret_cast<VkImage>(2U));
    const auto selectedFinal = BuildPicaGuideSamplingBarriers(
        *selectedScanout, resources, selected.ResourceMask);
    ASSERT_EQ(selectedFinal.Count, 1U);
    EXPECT_EQ(selectedFinal.Images[0].image,
              reinterpret_cast<VkImage>(5U));
}

TEST(Oot3dPicaNriRenderTargets, ValidatesTransactionalAttachmentBundles) {
    const auto image = [](uintptr_t value) {
        return reinterpret_cast<VkImage>(value);
    };
    Fast::Oot3d::NriPicaRenderTargetImages target;
    target.Color = image(1U);
    target.NormalGuide = image(2U);
    target.MaterialGuide = image(3U);
    target.RigidMotionGuide = image(4U);
    target.AmbientGuide = image(5U);
    target.FogGuide = image(14U);
    target.OutlineGeometryGuide = image(16U);
    target.Shadow = image(6U);
    target.Depth = image(7U);
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaRenderTargetImages(
        target, VK_SAMPLE_COUNT_1_BIT));
    EXPECT_EQ(Fast::Oot3d::CountNriPicaRenderTargetImages(target), 9U);
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaRenderTargetImages(
        target, VK_SAMPLE_COUNT_4_BIT));

    target.MsaaColor = image(8U);
    target.MsaaNormalGuide = image(9U);
    target.MsaaMaterialGuide = image(10U);
    target.MsaaRigidMotionGuide = image(11U);
    target.MsaaAmbientGuide = image(12U);
    target.MsaaFogGuide = image(15U);
    target.MsaaOutlineGeometryGuide = image(17U);
    target.MsaaDepth = image(13U);
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaRenderTargetImages(
        target, VK_SAMPLE_COUNT_4_BIT));
    EXPECT_EQ(Fast::Oot3d::CountNriPicaRenderTargetImages(target), 17U);
    target.MsaaDepth = VK_NULL_HANDLE;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaRenderTargetImages(
        target, VK_SAMPLE_COUNT_4_BIT));
}

TEST(Oot3dPicaNriDisplayCopy, RejectsInvalidOrAliasedCopies) {
    const auto image = [](uintptr_t value) {
        return reinterpret_cast<VkImage>(value);
    };
    Fast::Oot3d::NriPicaDisplayCopyDesc desc;
    desc.SourceImage = image(1U);
    desc.DestinationImage = image(2U);
    desc.Format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.Plan.SourceWidth = 400U;
    desc.Plan.SourceHeight = 240U;
    desc.Plan.DestinationWidth = 400U;
    desc.Plan.DestinationHeight = 240U;
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.DestinationColorAttachment = true;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.DestinationInitialized = true;
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.Plan.SourceWidth = 800U;
    desc.Plan.HorizontalSamples = 2U;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.DestinationColorAttachment = false;
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.DestinationImage = desc.SourceImage;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
    desc.DestinationImage = image(2U);
    desc.Plan.SourceWidth = 0U;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaDisplayCopyDesc(desc));
}

TEST(Oot3dPicaNriMemoryFillClear, RequiresImagesAndOwnedRenderingScopes) {
    const auto image = [](uintptr_t value) {
        return reinterpret_cast<VkImage>(value);
    };
    Fast::Oot3d::NriPicaShadowClearDesc shadow;
    shadow.Image = image(1U);
    shadow.Width = 400U;
    shadow.Height = 240U;
    EXPECT_TRUE(Fast::Oot3d::ValidateNriPicaShadowClearDesc(shadow));
    shadow.Image = VK_NULL_HANDLE;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriPicaShadowClearDesc(shadow));

    Fast::Oot3d::NriPicaAttachmentClearDesc attachments;
    attachments.Width = 400U;
    attachments.Height = 240U;
    attachments.ClearColor = true;
    attachments.NriRenderingScope = true;
    EXPECT_TRUE(
        Fast::Oot3d::ValidateNriPicaAttachmentClearDesc(attachments));
    attachments.NriRenderingScope = false;
    EXPECT_FALSE(
        Fast::Oot3d::ValidateNriPicaAttachmentClearDesc(attachments));
    attachments.NriRenderingScope = true;
    attachments.ClearColor = false;
    EXPECT_FALSE(
        Fast::Oot3d::ValidateNriPicaAttachmentClearDesc(attachments));
}

TEST(Oot3dNriSwapchain, ValidatesAtomicOwnershipAndWorkerPolicy) {
    Fast::Oot3d::NriSwapchainDesc desc;
    desc.NativeWindow = reinterpret_cast<void*>(1U);
    desc.Width = 1280U;
    desc.Height = 720U;
    desc.DesiredImageCount = 3U;
    desc.FrameCount = 2U;
    EXPECT_TRUE(Fast::Oot3d::ValidateNriSwapchainDesc(desc));
    desc.NativeWindow = nullptr;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriSwapchainDesc(desc));
    desc.NativeWindow = reinterpret_cast<void*>(1U);
    desc.DesiredImageCount = 1U;
    EXPECT_FALSE(Fast::Oot3d::ValidateNriSwapchainDesc(desc));

    EXPECT_FALSE(Fast::Oot3d::ShouldUseVulkanPresentWorker(true, true));
    EXPECT_TRUE(Fast::Oot3d::ShouldUseVulkanPresentWorker(false, true));
    EXPECT_FALSE(Fast::Oot3d::ShouldUseVulkanPresentWorker(false, false));
}

TEST(Oot3dPicaNriRenderTargetInitialization,
     RequiresCompleteMatchingBundle) {
    const auto image = [](uintptr_t value) {
        return reinterpret_cast<VkImage>(value);
    };
    Fast::Oot3d::NriPicaRenderTargetInitDesc desc;
    desc.Width = 400U;
    desc.Height = 240U;
    desc.DepthFormat = VK_FORMAT_D32_SFLOAT;
    desc.Images.Color = image(1U);
    desc.Images.NormalGuide = image(2U);
    desc.Images.MaterialGuide = image(3U);
    desc.Images.RigidMotionGuide = image(4U);
    desc.Images.AmbientGuide = image(5U);
    desc.Images.FogGuide = image(8U);
    desc.Images.OutlineGeometryGuide = image(9U);
    desc.Images.Shadow = image(6U);
    desc.Images.Depth = image(7U);
    EXPECT_TRUE(
        Fast::Oot3d::ValidateNriPicaRenderTargetInitDesc(desc));
    desc.Samples = VK_SAMPLE_COUNT_4_BIT;
    EXPECT_FALSE(
        Fast::Oot3d::ValidateNriPicaRenderTargetInitDesc(desc));
    desc.Samples = VK_SAMPLE_COUNT_1_BIT;
    desc.Height = 0U;
    EXPECT_FALSE(
        Fast::Oot3d::ValidateNriPicaRenderTargetInitDesc(desc));
}

TEST(Oot3dPicaNriUpload, CopiesSparseRangesTransactionally) {
    std::array<uint8_t, 32> source{};
    std::array<uint8_t, 32> destination{};
    for (size_t index = 0; index < source.size(); ++index)
        source[index] = static_cast<uint8_t>(index + 1U);
    const std::array<Fast::Oot3d::PicaNriUploadRange, 2> ranges{{
        {2U, 5U}, {20U, 4U},
    }};
    uint64_t copied = 0;
    ASSERT_TRUE(Fast::Oot3d::CopyPicaNriUploadRanges(
        source, destination, ranges, &copied));
    EXPECT_EQ(copied, 9U);
    EXPECT_TRUE(std::equal(
        destination.begin() + 2, destination.begin() + 7,
        source.begin() + 2));
    EXPECT_TRUE(std::equal(
        destination.begin() + 20, destination.begin() + 24,
        source.begin() + 20));
    EXPECT_EQ(destination[1], 0U);
    EXPECT_EQ(destination[7], 0U);

    const auto before = destination;
    const std::array<Fast::Oot3d::PicaNriUploadRange, 2> invalid{{
        {4U, 2U}, {31U, 2U},
    }};
    copied = 99U;
    EXPECT_FALSE(Fast::Oot3d::CopyPicaNriUploadRanges(
        source, destination, invalid, &copied));
    EXPECT_EQ(copied, 0U);
    EXPECT_EQ(destination, before);
}

TEST(Oot3dPicaNriTextureUpload, AlignsAndPacksRowsWithoutTouchingPadding) {
    const auto layout = Fast::Oot3d::PlanPicaNriTextureUpload(
        3U, 128U, 3U, 2U, 4U, 16U, 32U);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->Offset, 32U);
    EXPECT_EQ(layout->RowPitch, 16U);
    EXPECT_EQ(layout->SlicePitch, 32U);
    EXPECT_EQ(layout->RequiredSize, 64U);

    std::array<uint8_t, 24> pixels{};
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<uint8_t>(i + 1U);
    std::array<uint8_t, 128> destination{};
    destination.fill(0xCDU);
    ASSERT_TRUE(Fast::Oot3d::PackPicaNriTextureUpload(
        pixels, 3U, 2U, 4U, *layout, destination));
    EXPECT_TRUE(std::equal(
        pixels.begin(), pixels.begin() + 12,
        destination.begin() + 32));
    EXPECT_TRUE(std::equal(
        pixels.begin() + 12, pixels.end(),
        destination.begin() + 48));
    EXPECT_EQ(destination[31], 0xCDU);
    EXPECT_EQ(destination[44], 0xCDU);
    EXPECT_EQ(destination[47], 0xCDU);
    EXPECT_EQ(destination[60], 0xCDU);

    EXPECT_FALSE(Fast::Oot3d::PlanPicaNriTextureUpload(
        3U, 63U, 3U, 2U, 4U, 16U, 32U).has_value());
    const auto before = destination;
    EXPECT_FALSE(Fast::Oot3d::PackPicaNriTextureUpload(
        std::span<const uint8_t>(pixels).first(23),
        3U, 2U, 4U, *layout, destination));
    EXPECT_EQ(destination, before);
}

TEST(Oot3dPicaNriTextureUpload,
     PlansFreshSegmentWhenA4kTextureCannotShareTheBaseArena) {
    constexpr uint64_t capacity = 64ULL * 1024ULL * 1024ULL;
    EXPECT_FALSE(Fast::Oot3d::PlanPicaNriTextureUpload(
                     16U, capacity, 4096U, 4096U, 4U, 256U, 512U)
                     .has_value());

    const auto fresh = Fast::Oot3d::PlanPicaNriTextureUpload(
        0U, capacity, 4096U, 4096U, 4U, 256U, 512U);
    ASSERT_TRUE(fresh.has_value());
    EXPECT_EQ(fresh->Offset, 0U);
    EXPECT_EQ(fresh->RequiredSize, capacity);
}


TEST(Oot3dSmaa1x, DecodesOfficialLookupTexturesExactly) {
    using namespace Fast::Oot3d;
    const auto lookups = DecodeSmaaLookupData();
    ASSERT_TRUE(lookups.Valid());
    const auto fnv1a = [](const std::vector<uint8_t>& bytes) {
        uint64_t hash = 14695981039346656037ULL;
        for (const uint8_t value : bytes) {
            hash ^= value;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    EXPECT_EQ(lookups.AreaRgba8.size(),
              static_cast<size_t>(160U * 560U * 4U));
    EXPECT_EQ(lookups.SearchRgba8.size(),
              static_cast<size_t>(64U * 16U * 4U));
    EXPECT_EQ(fnv1a(lookups.AreaRgba8),
              0x7EEA377052D0ACEDULL);
    EXPECT_EQ(fnv1a(lookups.SearchRgba8),
              0x00A22028DB3F6325ULL);
}

#ifdef OOT3D_FOUNDATION_HAS_SHADERC
TEST(Oot3dSmaa1x, OfficialThreeStageComputeShadersCompile) {
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(
        shaderc_target_env_vulkan,
        shaderc_env_version_vulkan_1_2);
    options.SetOptimizationLevel(
        shaderc_optimization_level_performance);
    for (const auto stage : {
             Fast::Oot3d::Smaa1xStage::EdgeDetection,
             Fast::Oot3d::Smaa1xStage::BlendWeightCalculation,
             Fast::Oot3d::Smaa1xStage::NeighborhoodBlending}) {
        const std::string source =
            Fast::Oot3d::BuildSmaa1xComputeShader(stage);
        ASSERT_NE(
            source.find("SMAABlendingWeightCalculationPS"),
            std::string::npos);
        const auto result = compiler.CompileGlslToSpv(
            source, shaderc_compute_shader,
            "oot3d_smaa_1x_test.comp", options);
        EXPECT_EQ(
            result.GetCompilationStatus(),
            shaderc_compilation_status_success)
            << result.GetErrorMessage();
    }
}
#endif

// Two selectable lighting models: Native (NativePbr) and Modern (PerPixelPbr).
// Saved Per-pixel becomes Modern and plain native shading becomes Native;
// plain native shading stays the capability fallback and the Authentic look.
TEST(Oot3dGraphicsSettings, LightingModelsAreNativeAndModern) {
    using namespace Fast::Oot3d;
    GraphicsCapabilities capabilities;
    capabilities.Set(GraphicsCapability::NativeSurfaceLighting, true);
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Effects.SurfaceLighting = SurfaceLightingMode::PerPixel;
    EXPECT_EQ(GraphicsSettingsService::Validate(settings, capabilities).Value.Effects.SurfaceLighting,
              SurfaceLightingMode::PerPixelPbr);
    settings.Effects.SurfaceLighting = SurfaceLightingMode::Native;
    EXPECT_EQ(GraphicsSettingsService::Validate(settings, capabilities).Value.Effects.SurfaceLighting,
              SurfaceLightingMode::NativePbr);
    const auto withoutRelighting = GraphicsSettingsService::Validate(settings, {});
    EXPECT_EQ(withoutRelighting.Value.Effects.SurfaceLighting, SurfaceLightingMode::Native);
    EXPECT_TRUE(std::none_of(withoutRelighting.Issues.begin(), withoutRelighting.Issues.end(),
                             [](const auto& issue) { return issue.Field == "Graphics.Effects.SurfaceLighting"; }));
    auto authentic = GraphicsSettingsService::Preset(GraphicsPreset::Authentic);
    EXPECT_EQ(GraphicsSettingsService::Validate(authentic, capabilities).Value.Effects.SurfaceLighting,
              SurfaceLightingMode::Native);
}

TEST(Oot3dGraphicsSettings, GatesSmaaOnThreeStageNriCapability) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    auto settings = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    settings.AntiAliasing = Fast::Oot3d::AntiAliasingMode::Smaa1x;
    settings.MsaaSamples = 8U;
    auto result = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities);
    EXPECT_EQ(result.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Off);
    capabilities.Set(
        Fast::Oot3d::GraphicsCapability::Smaa1x, true);
    result = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities);
    EXPECT_EQ(result.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Smaa1x);
    EXPECT_EQ(result.Value.MsaaSamples, 1U);
}

TEST(Oot3dGraphicsSettings, GatesMsaaOnSampleAndDepthResolveCapabilities) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    capabilities.Set(Fast::Oot3d::GraphicsCapability::Msaa4x, true);
    capabilities.Set(
        Fast::Oot3d::GraphicsCapability::MultisampledDepthResolve, true);
    auto settings = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    settings.AntiAliasing = Fast::Oot3d::AntiAliasingMode::Msaa;
    settings.MsaaSamples = 4U;
    auto result = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities);
    EXPECT_EQ(result.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Msaa);
    EXPECT_EQ(result.Value.MsaaSamples, 4U);

    capabilities.Set(
        Fast::Oot3d::GraphicsCapability::MultisampledDepthResolve, false);
    result = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities);
    EXPECT_EQ(result.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Off);
    EXPECT_EQ(result.Value.MsaaSamples, 1U);
}

TEST(Oot3dGraphicsSettings, VisualPresetsPreserveHostAndContentIdentityState) {
    using namespace Fast::Oot3d;
    auto current = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    current.Window = WindowMode::Borderless;
    current.DisplayIndex = 2U;
    current.OutputWidth = 2560U;
    current.OutputHeight = 1440U;
    current.RefreshRate = 144U;
    current.VSync = false;
    current.FovMultiplier = 1.25F;
    current.TexturePacks.Azahar.LoadCustomTextures = true;
    current.TexturePacks.Azahar.LoadDirectory = "custom-textures";
    current.Grass.Rules.emplace_back();
    current.Grass.Rules.front().RuleId = 17U;
    current.GrassSavedPreset.Rules.emplace_back();
    current.GrassSavedPreset.Rules.front().RuleId = 19U;
    current.Effects.ReflectionMaterials.emplace_back();
    current.Effects.ReflectionMaterials.front().RuleId = 23U;

    const auto toon = GraphicsSettingsService::PresetForCurrent(
        GraphicsPreset::Toon, current);
    EXPECT_EQ(toon.Preset, GraphicsPreset::Toon);
    EXPECT_EQ(toon.Window, WindowMode::Borderless);
    EXPECT_EQ(toon.DisplayIndex, 2U);
    EXPECT_EQ(toon.OutputWidth, 2560U);
    EXPECT_EQ(toon.OutputHeight, 1440U);
    EXPECT_EQ(toon.RefreshRate, 144U);
    EXPECT_FALSE(toon.VSync);
    // Motion and camera belong to the preset.
    EXPECT_FLOAT_EQ(toon.FovMultiplier, 1.0F);
    EXPECT_TRUE(toon.TexturePacks.Azahar.LoadCustomTextures);
    EXPECT_EQ(toon.TexturePacks.Azahar.LoadDirectory,
              "custom-textures");
    ASSERT_EQ(toon.Grass.Rules.size(), 1U);
    EXPECT_EQ(toon.Grass.Rules.front().RuleId, 17U);
    ASSERT_EQ(toon.GrassSavedPreset.Rules.size(), 1U);
    EXPECT_EQ(toon.GrassSavedPreset.Rules.front().RuleId, 19U);
    ASSERT_EQ(toon.Effects.ReflectionMaterials.size(), 1U);
    EXPECT_EQ(toon.Effects.ReflectionMaterials.front().RuleId, 23U);
}

TEST(Oot3dGraphicsSettings, AuthenticModeDisablesEffectsWithoutErasingAssignments) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Preset = GraphicsPreset::Authentic;
    settings.Grass.Quality = GrassQuality::High;
    settings.Grass.Rules.emplace_back();
    settings.Grass.Rules.front().RuleId = 31U;
    settings.Effects.Reflections = ReflectionMode::HiZ;
    settings.Effects.ReflectionMaterials.emplace_back();
    settings.Effects.ReflectionMaterials.front().RuleId = 37U;

    const auto result = GraphicsSettingsService::Validate(
        settings, GraphicsCapabilities{});
    EXPECT_EQ(result.Value.Grass.Quality, GrassQuality::Off);
    EXPECT_EQ(result.Value.Effects.Reflections, ReflectionMode::Off);
    ASSERT_EQ(result.Value.Grass.Rules.size(), 1U);
    EXPECT_EQ(result.Value.Grass.Rules.front().RuleId, 31U);
    ASSERT_EQ(result.Value.Effects.ReflectionMaterials.size(), 1U);
    EXPECT_EQ(result.Value.Effects.ReflectionMaterials.front().RuleId, 37U);
}

TEST(Oot3dGraphicsSettings, GatesDirectionalShadowsOnTheirBackendOwner) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Effects.DirectionalShadows.Mode =
        DirectionalShadowMode::VirtualShadowMaps;

    GraphicsCapabilities capabilities;
    auto result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.DirectionalShadows.Mode,
              DirectionalShadowMode::Off);

    capabilities.Set(GraphicsCapability::NriDirectionalShadows, true);
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.DirectionalShadows.Mode,
              DirectionalShadowMode::VirtualShadowMaps);
}

TEST(Oot3dGraphicsSettings, EnhancedIsTheCapturedProfileAndToonKeepsItsBase) {
    using namespace Fast::Oot3d;
    const auto enhanced = GraphicsSettingsService::Preset(GraphicsPreset::Enhanced);
    EXPECT_EQ(enhanced.Preset, GraphicsPreset::Enhanced);
    EXPECT_EQ(enhanced.Effects.SurfaceLighting, SurfaceLightingMode::PerPixelPbr);
    EXPECT_EQ(enhanced.Effects.AmbientOcclusion, AmbientOcclusionMode::Mxao);
    EXPECT_EQ(enhanced.Effects.MxaoNormals, MxaoNormalSource::Geometry);
    EXPECT_EQ(enhanced.Effects.DirectionalShadows.Mode, DirectionalShadowMode::VirtualShadowMaps);
    EXPECT_EQ(enhanced.Effects.PointShadows.Mode, PointShadowMode::CubeMaps);
    EXPECT_TRUE(enhanced.Effects.MxaoTemporal);
    EXPECT_EQ(enhanced.FrameRate, FrameRateMode::Interpolated2x);
    EXPECT_FLOAT_EQ(enhanced.FovMultiplier, 1.1F);
    EXPECT_EQ(enhanced.AntiAliasing, AntiAliasingMode::Off);
    EXPECT_EQ(enhanced.Grass.Quality, GrassQuality::Custom);
    EXPECT_EQ(enhanced.UpscalerDlssModel, DlssModel::Transformer);
    EXPECT_EQ(DlssRenderPreset(DlssModel::Transformer, UpscalerQuality::UltraPerformance), 11U);
    EXPECT_EQ(DlssRenderPreset(DlssModel::Cnn, UpscalerQuality::UltraPerformance), 6U);
    EXPECT_EQ(DlssRenderPreset(DlssModel::Cnn, UpscalerQuality::Quality), 5U);
    EXPECT_EQ(DlssRenderPreset(DlssModel::Default, UpscalerQuality::Performance), 0U);
    // Every captured field survives decoding: re-encoding the preset gives the
    // document back.
    const auto document = nlohmann::json::parse(EnhancedPresetDocument());
    const auto encoded = SerializeGraphicsSettings(enhanced);
    for (const auto& [key, value] : document.items()) {
        if (key == "Grass") {
            for (const auto& [field, entry] : value.items())
                EXPECT_EQ(encoded["Grass"][field], entry) << "Grass." << field;
        } else if (key == "Effects") {
            for (const auto& [field, entry] : value.items()) {
                if (field == "Reflections") {
                    for (const auto& [option, setting] : entry.items())
                        EXPECT_EQ(encoded["Effects"]["Reflections"][option], setting) << option;
                } else {
                    EXPECT_EQ(encoded["Effects"][field], entry) << "Effects." << field;
                }
            }
        } else {
            EXPECT_EQ(encoded[key], value) << key;
        }
    }
    // A preset keeps the user's own state and sets motion and camera.
    auto current = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    current.FovMultiplier = 1.2F;
    current.VSync = false;
    current.TexturePacks.Azahar.LoadCustomTextures = true;
    const auto chosen = GraphicsSettingsService::PresetForCurrent(GraphicsPreset::Enhanced, current);
    EXPECT_FLOAT_EQ(chosen.FovMultiplier, 1.1F);
    EXPECT_FALSE(chosen.VSync);
    EXPECT_TRUE(chosen.TexturePacks.Azahar.LoadCustomTextures);
    EXPECT_EQ(chosen.Grass.Rules.size(), current.Grass.Rules.size());
    const auto toon = GraphicsSettingsService::Preset(GraphicsPreset::Toon);
    EXPECT_EQ(toon.AntiAliasing, AntiAliasingMode::Fxaa);
    EXPECT_EQ(toon.Effects.AmbientOcclusion, AmbientOcclusionMode::Gtao);
    EXPECT_EQ(toon.Effects.Toon, ToonMode::PostProcessPreview);
    EXPECT_EQ(toon.Effects.SurfaceLighting, SurfaceLightingMode::Native);
}

TEST(Oot3dGraphicsSettings, ClampsTheOriginalMxaoOptions) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    EXPECT_EQ(settings.Effects.MxaoNormals, MxaoNormalSource::Geometry);
    EXPECT_EQ(settings.Effects.MxaoDebug, MxaoDebugView::Off);
    settings.Effects.MxaoQuality = 9U;
    settings.Effects.MxaoShadingRate = 7U;
    settings.Effects.MxaoScreenRadius = 40.0F;
    settings.Effects.MxaoFadeDistance = -1.0F;
    settings.Effects.MxaoNormals = static_cast<MxaoNormalSource>(9);
    settings.Effects.MxaoDebug = static_cast<MxaoDebugView>(42);
    const auto value = GraphicsSettingsService::Validate(settings, GraphicsCapabilities{}).Value.Effects;
    EXPECT_EQ(value.MxaoQuality, 6U);
    EXPECT_EQ(value.MxaoShadingRate, 2U);
    EXPECT_FLOAT_EQ(value.MxaoScreenRadius, 10.0F);
    EXPECT_FLOAT_EQ(value.MxaoFadeDistance, 0.0F);
    EXPECT_EQ(value.MxaoNormals, MxaoNormalSource::Geometry);
    EXPECT_EQ(value.MxaoDebug, MxaoDebugView::Off);
    // The face-normal source and views are valid values.
    settings.Effects.MxaoNormals = MxaoNormalSource::Geometry;
    settings.Effects.MxaoDebug = MxaoDebugView::FaceDisagreement;
    const auto face = GraphicsSettingsService::Validate(settings, GraphicsCapabilities{}).Value.Effects;
    EXPECT_EQ(face.MxaoNormals, MxaoNormalSource::Geometry);
    EXPECT_EQ(face.MxaoDebug, MxaoDebugView::FaceDisagreement);
}

TEST(Oot3dGraphicsSettings, GatesAndClampsPointShadows) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    EXPECT_EQ(settings.Effects.PointShadows.Mode, PointShadowMode::Off);
    settings.Effects.PointShadows.Mode = PointShadowMode::CubeMaps;
    settings.Effects.PointShadows.Resolution = 300U;
    settings.Effects.PointShadows.MaxLights = 12U;
    settings.Effects.PointShadows.Range = 0.1F;
    settings.Effects.PointShadows.Strength = 3.0F;

    GraphicsCapabilities capabilities;
    auto result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.PointShadows.Mode, PointShadowMode::Off);

    capabilities.Set(GraphicsCapability::NriDirectionalShadows, true);
    result = GraphicsSettingsService::Validate(settings, capabilities);
    const auto& points = result.Value.Effects.PointShadows;
    EXPECT_EQ(points.Mode, PointShadowMode::CubeMaps);
    EXPECT_EQ(points.Resolution, PointShadowSettings{}.Resolution);
    EXPECT_EQ(points.MaxLights, 8U);
    EXPECT_FLOAT_EQ(points.Range, 0.25F);
    EXPECT_FLOAT_EQ(points.Strength, 1.0F);
}

TEST(Oot3dSampleView, UsesTheRenderedCameraAndTheMajorityView) {
    using namespace Fast::Oot3d;
    // Guest camera: eye (0,0,10) looking at the origin, one step ahead.
    PerspectiveViewState guest{};
    guest.Serial = 7U;
    guest.NearPlane = 1.0F;
    guest.FarPlane = 100.0F;
    guest.Projection = { 1.5F, 0, 0, 0, 0, 2.0F, 0, 0, 0, 0, -1.01F, -1.0F, 0, 0, -1.01F, 0 };
    guest.Eye = { 0.0F, 0.0F, 10.0F };
    guest.At = { 0.0F, 0.0F, 0.0F };
    guest.CameraAvailable = true;
    // Rendered geometry: translated camera at (3,0,8), same orientation.
    const SampleViewMatrix rendered{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -3, 0, -8, 1 };
    const SampleViewMatrix stray{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    SampleViewAccumulator accumulator;
    accumulator.Observe(5U, stray);
    accumulator.Observe(5U, rendered);
    accumulator.Observe(5U, rendered);
    ASSERT_TRUE(accumulator.Resolve(5U).has_value());
    EXPECT_EQ(*accumulator.Resolve(5U), rendered);
    EXPECT_EQ(accumulator.DistinctViews(5U), 2U);
    EXPECT_FALSE(accumulator.Resolve(6U).has_value());
    accumulator.Observe(6U, stray);
    EXPECT_EQ(*accumulator.Resolve(6U), stray);

    const auto sample = ResolveSamplePerspective(guest, rendered);
    EXPECT_EQ(sample.Serial, 0U);
    EXPECT_FLOAT_EQ(sample.Eye[0], 3.0F);
    EXPECT_FLOAT_EQ(sample.Eye[2], 8.0F);
    EXPECT_FLOAT_EQ(sample.At[2], 8.0F - 10.0F); // guest look distance, rendered direction
    // WorldToClip = Projection * WorldToView: the world point (3,0,0) lies on
    // the rendered view axis, 8 units ahead.
    const float x = 3.0F, z = 0.0F;
    const float clipX = sample.WorldToClip[0] * x + sample.WorldToClip[8] * z + sample.WorldToClip[12];
    const float clipW = sample.WorldToClip[3] * x + sample.WorldToClip[11] * z + sample.WorldToClip[15];
    EXPECT_NEAR(clipX, 0.0F, 1.0e-5F);
    EXPECT_NEAR(clipW, 8.0F, 1.0e-5F);
    EXPECT_EQ(sample.Projection, guest.Projection);
}

TEST(Oot3dSampleView, NativeDrawsSetTheProjectionScale) {
    using namespace Fast::Oot3d;
    // OOT3D's projection rows c0-c3, rotated with the framebuffer: the Kokiri
    // walk's draws (clip x from view y, clip y from view x).
    std::vector<uint8_t> uniforms(kPicaPackedVertexUniformSize);
    const std::array<float, 16> rotated{ 0, 2.0503F, 0, 0, -1.1533F, 0, 0, 0, 0, 0, 1.0001F, 7.0009F, 0, 0, -1, 0 };
    std::memcpy(uniforms.data() + kPicaPackedVertexFloatOffset, rotated.data(), sizeof(rotated));
    const auto decoded = DecodePicaPerspectiveProjection(uniforms);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FLOAT_EQ(decoded->X, 1.1533F);
    EXPECT_FLOAT_EQ(decoded->Y, 2.0503F);
    // Unrotated rows decode the same way; an orthographic projection does not.
    const std::array<float, 16> plain{ 1.1533F, 0, 0, 0, 0, 2.0503F, 0, 0, 0, 0, 1.0001F, 7.0009F, 0, 0, -1, 0 };
    std::memcpy(uniforms.data() + kPicaPackedVertexFloatOffset, plain.data(), sizeof(plain));
    EXPECT_EQ(DecodePicaPerspectiveProjection(uniforms), decoded);
    const std::array<float, 16> orthographic{ 0, 0.005F, 0, 0, -0.008F, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    std::memcpy(uniforms.data() + kPicaPackedVertexFloatOffset, orthographic.data(), sizeof(orthographic));
    EXPECT_FALSE(DecodePicaPerspectiveProjection(uniforms).has_value());

    // The draws most of the frame agree on win.
    SampleProjectionAccumulator accumulator;
    accumulator.Observe(3U, { 0.5F, 0.9F });
    accumulator.Observe(3U, *decoded);
    accumulator.Observe(3U, *decoded);
    EXPECT_EQ(accumulator.Resolve(3U), decoded);
    EXPECT_FALSE(accumulator.Resolve(4U).has_value());

    // The guest frustum the camera requested (+-5.2056 x +-2.9282 at near 7):
    // the native rows widen it by 1.166 on both axes; depth is unchanged.
    PerspectiveViewState guest;
    guest.Left = -5.2056F;
    guest.Right = 5.2056F;
    guest.Bottom = -2.9282F;
    guest.Top = 2.9282F;
    guest.NearPlane = 7.0F;
    guest.FarPlane = 12000.5F;
    guest.Projection = { 7.0F / 5.2056F, 0, 0, 0, 0, 7.0F / 2.9282F, 0, 0, 0, 0, -1.0006F, -1.0F, 0, 0, -7.004F, 0 };
    guest.WorldToClip = guest.Projection;
    const auto applied = ApplySampleProjection(guest, *decoded);
    EXPECT_FLOAT_EQ(applied.Projection[0], 1.1533F);
    EXPECT_FLOAT_EQ(applied.Projection[5], 2.0503F);
    EXPECT_FLOAT_EQ(applied.WorldToClip[0], 1.1533F);
    EXPECT_FLOAT_EQ(applied.Projection[10], guest.Projection[10]);
    EXPECT_FLOAT_EQ(applied.Projection[14], guest.Projection[14]);
    EXPECT_NEAR(applied.Right, 7.0F / 1.1533F, 1.0e-4F);
    EXPECT_NEAR(applied.Top, 7.0F / 2.0503F, 1.0e-4F);
    // A projection of another camera is not an adjustment of this one.
    EXPECT_EQ(ApplySampleProjection(guest, { 50.0F, 90.0F }).Projection, guest.Projection);
}

TEST(Oot3dGraphicsSettings, GatesGlobalIlluminationOnItsInputsAndProvider) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    settings.Effects.GlobalIllumination.Mode = GlobalIlluminationMode::BrixelizerGi;
    settings.Effects.GlobalIllumination.DiffuseIntensity = 9.0F; // above the 8 limit

    GraphicsCapabilities capabilities;
    for (const auto capability :
         {GraphicsCapability::NriInterop, GraphicsCapability::SampledDepth, GraphicsCapability::NormalGuide,
          GraphicsCapability::MaterialGuide, GraphicsCapability::MotionVectors,
          GraphicsCapability::ValidViewMetadata})
        capabilities.Set(capability, true);
    capabilities.Set(GraphicsCapability::BrixelizerGlobalIllumination, false, "not built");
    auto result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
    ASSERT_FALSE(result.Issues.empty());
    EXPECT_EQ(result.Issues.back().Message, "not built");
    EXPECT_FLOAT_EQ(result.Value.Effects.GlobalIllumination.DiffuseIntensity, 8.0F);

    capabilities.Set(GraphicsCapability::BrixelizerGlobalIllumination, true);
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::BrixelizerGi);

    capabilities.Set(GraphicsCapability::MotionVectors, false, "no motion");
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
}

// Screen-space GI: its own provider capability (the normal and material
// guides are not its inputs), capability-safe Off, and its parameters kept on
// the pass's supported grid.
TEST(Oot3dGraphicsSettings, GatesScreenSpaceGlobalIlluminationAndClampsItsParameters) {
    using namespace Fast::Oot3d;
    auto settings = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    auto& gi = settings.Effects.GlobalIllumination;
    gi.Mode = GlobalIlluminationMode::ScreenSpace;
    GraphicsCapabilities capabilities;
    for (const auto capability : {GraphicsCapability::NriInterop, GraphicsCapability::SampledDepth,
                                  GraphicsCapability::MotionVectors, GraphicsCapability::ValidViewMetadata})
        capabilities.Set(capability, true);
    capabilities.Set(GraphicsCapability::ScreenSpaceGlobalIllumination, false, "no storage buffers");
    auto result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
    ASSERT_FALSE(result.Issues.empty());
    EXPECT_EQ(result.Issues.back().Message, "no storage buffers");

    capabilities.Set(GraphicsCapability::ScreenSpaceGlobalIllumination, true);
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::ScreenSpace);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.ScreenSpace, ScreenSpaceGiSettings{});

    // Out-of-grid values land on the nearest supported value.
    gi.ScreenSpace = {9, 17, 100, 5000.0F, -3.0F, true, 7, false};
    result = GraphicsSettingsService::Validate(settings, capabilities);
    const auto& clamped = result.Value.Effects.GlobalIllumination.ScreenSpace;
    EXPECT_EQ(clamped.Sections, 4U);
    EXPECT_EQ(clamped.RadialCandidates, 16U);
    EXPECT_EQ(clamped.Sectors, 128U);
    EXPECT_FLOAT_EQ(clamped.Radius, 1000.0F);
    EXPECT_FLOAT_EQ(clamped.Thickness, 1.0F);
    EXPECT_EQ(clamped.FilterLevels, 4U);
    gi.ScreenSpace.HighlightClamp = 99.0F;
    EXPECT_FLOAT_EQ(GraphicsSettingsService::Validate(settings, capabilities)
                        .Value.Effects.GlobalIllumination.ScreenSpace.HighlightClamp,
                    16.0F);
    gi.ScreenSpace.HighlightClamp = -1.0F;
    EXPECT_FLOAT_EQ(GraphicsSettingsService::Validate(settings, capabilities)
                        .Value.Effects.GlobalIllumination.ScreenSpace.HighlightClamp,
                    0.0F);
    gi.ScreenSpace.HighlightClamp = 0.0F;
    gi.ScreenSpace.Radius = std::numeric_limits<float>::quiet_NaN();
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_FLOAT_EQ(result.Value.Effects.GlobalIllumination.ScreenSpace.Radius, ScreenSpaceGiSettings{}.Radius);

    capabilities.Set(GraphicsCapability::ValidViewMetadata, false, "no view");
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
    // An unknown persisted mode value is corrected to Off.
    capabilities.Set(GraphicsCapability::ValidViewMetadata, true);
    gi.Mode = static_cast<GlobalIlluminationMode>(7);
    result = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(result.Value.Effects.GlobalIllumination.Mode, GlobalIlluminationMode::Off);
}

// The screen-space mode has no ray that leaves the scene: no sky source, and
// the sky gain never raises the composition gain. The face normal is its
// geometric normal.
TEST(Oot3dGraphicsSettings, ScreenSpaceGlobalIlluminationHasNoSkySource) {
    using namespace Fast::Oot3d;
    GlobalIlluminationSettings gi;
    gi.SkyLight = 4.0F;
    gi.Mode = GlobalIlluminationMode::BrixelizerGi;
    EXPECT_FLOAT_EQ(ResolveGlobalIlluminationSources(gi).Gain, 4.0F);
    EXPECT_FALSE(GlobalIlluminationReadsFaceNormal(gi));
    gi.Mode = GlobalIlluminationMode::ScreenSpace;
    const auto mix = ResolveGlobalIlluminationSources(gi);
    EXPECT_FLOAT_EQ(mix.Gain, 1.0F);
    EXPECT_EQ(mix.Weights, (std::array<float, 4>{1.0F, 1.0F, 1.0F, 0.0F}));
    EXPECT_TRUE(GlobalIlluminationReadsFaceNormal(gi));
}

TEST(Oot3dGraphicsSettings, GlobalIlluminationSourcesSplitIntoWeightsAndGain) {
    using namespace Fast::Oot3d;
    GlobalIlluminationSettings gi;
    auto mix = ResolveGlobalIlluminationSources(gi);
    EXPECT_FLOAT_EQ(mix.Gain, 1.0F);
    EXPECT_EQ(mix.Weights, (std::array<float, 4>{1.0F, 1.0F, 1.0F, 0.25F}));

    // The largest gain applies when composed; the guide carries the rest
    // relative to it, never above one.
    gi.SceneLight = 0.5F;
    gi.KeyLight = 2.0F;
    gi.DynamicLights = 0.0F;
    mix = ResolveGlobalIlluminationSources(gi);
    EXPECT_FLOAT_EQ(mix.Gain, 2.0F);
    EXPECT_EQ(mix.Weights, (std::array<float, 4>{0.25F, 1.0F, 0.0F, 0.125F})); // sky light 0.25

    // A single source changes its gain without changing the weights (no
    // history restart).
    gi = {};
    gi.KeyLight = gi.DynamicLights = gi.SkyLight = 0.0F;
    gi.SceneLight = 3.0F;
    const auto before = ResolveGlobalIlluminationSources(gi);
    gi.SceneLight = 0.75F;
    const auto after = ResolveGlobalIlluminationSources(gi);
    EXPECT_EQ(before.Weights, after.Weights);
    EXPECT_FLOAT_EQ(after.Gain, 0.75F);

    gi.SceneLight = 0.0F;
    mix = ResolveGlobalIlluminationSources(gi);
    EXPECT_FLOAT_EQ(mix.Gain, 0.0F);
    EXPECT_EQ(mix.Weights, (std::array<float, 4>{}));
}

TEST(Oot3dGraphicsSettings, GlobalIlluminationHistoryRestartsOnlyWhereBlendingWouldLinger) {
    using Fast::Oot3d::RestartsGlobalIlluminationHistory;
    using Weights = std::array<float, 4>;
    EXPECT_FALSE(RestartsGlobalIlluminationHistory({1.0F, 0.5F, 0.0F}, {1.0F, 0.5F, 0.0F}));
    // Light returning to an empty history.
    EXPECT_TRUE(RestartsGlobalIlluminationHistory(Weights{}, {1.0F, 0.0F, 0.0F}));
    // A source switched off: its light would linger for seconds.
    EXPECT_TRUE(RestartsGlobalIlluminationHistory({1.0F, 0.25F, 0.25F}, {0.0F, 0.0F, 1.0F}));
    EXPECT_TRUE(RestartsGlobalIlluminationHistory({1.0F, 1.0F, 1.0F}, Weights{}));
    // Other changes blend in (a restart would only add a dark dip).
    EXPECT_FALSE(RestartsGlobalIlluminationHistory({1.0F, 1.0F, 1.0F}, {1.0F, 0.25F, 0.25F}));
    EXPECT_FALSE(RestartsGlobalIlluminationHistory({1.0F, 0.0F, 0.0F}, {0.5F, 0.0F, 1.0F}));
}

TEST(Oot3dPicaToon, ValidationClampsOutlineControls) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    auto settings = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    settings.Effects.ToonStyle.OutlineWidth = 100.0F;
    settings.Effects.ToonStyle.OutlineSoftness = -2.0F;
    settings.Effects.ToonStyle.OutlineDepthSensitivity = -1.0F;
    settings.Effects.ToonStyle.OutlineNormalSensitivity = 100.0F;
    settings.Effects.ToonStyle.OutlineTint = { -1.0F, 0.5F, 4.0F };
    settings.Effects.ToonStyle.OutlineOpacity = 2.0F;
    const auto validated = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities).Value.Effects.ToonStyle;
    EXPECT_FLOAT_EQ(validated.OutlineWidth, 12.0F);
    EXPECT_FLOAT_EQ(validated.OutlineSoftness, 0.0F);
    EXPECT_FLOAT_EQ(validated.OutlineDepthSensitivity, 0.0F);
    EXPECT_FLOAT_EQ(validated.OutlineNormalSensitivity, 8.0F);
    EXPECT_FLOAT_EQ(validated.OutlineTint[0], 0.0F);
    EXPECT_FLOAT_EQ(validated.OutlineTint[1], 0.5F);
    EXPECT_FLOAT_EQ(validated.OutlineTint[2], 1.0F);
    EXPECT_FLOAT_EQ(validated.OutlineOpacity, 1.0F);
}

TEST(Oot3dPicaToon, ValidatesCustomLightBandProfile) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    auto settings = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    auto& toon = settings.Effects.ToonStyle;
    toon.LightBands = 4U;
    toon.CustomLightBands = true;
    toon.LightBandLevels = {-1.0F, 0.25F, 2.0F, 0.8F, 1.0F, 1.0F};
    toon.LightBandThresholds = {0.9F, 0.2F, 0.5F, 1.0F, 1.0F};
    const auto validated = Fast::Oot3d::GraphicsSettingsService::Validate(
        settings, capabilities).Value.Effects.ToonStyle;
    EXPECT_FLOAT_EQ(validated.LightBandLevels[0], 0.0F);
    EXPECT_FLOAT_EQ(validated.LightBandLevels[1], 0.25F);
    EXPECT_FLOAT_EQ(validated.LightBandLevels[2], 1.0F);
    EXPECT_FLOAT_EQ(validated.LightBandLevels[3], 0.8F);
    EXPECT_LT(validated.LightBandThresholds[0],
              validated.LightBandThresholds[1]);
    EXPECT_LT(validated.LightBandThresholds[1],
              validated.LightBandThresholds[2]);
}

TEST(Oot3dPicaToon, ResetsLightBandsToEqualQuantization) {
    Fast::Oot3d::ToonStyleSettings style;
    style.LightBands = 5U;
    Fast::Oot3d::ResetToonLightBandProfile(style);
    EXPECT_FLOAT_EQ(style.LightBandLevels[0], 0.0F);
    EXPECT_FLOAT_EQ(style.LightBandLevels[1], 0.25F);
    EXPECT_FLOAT_EQ(style.LightBandLevels[4], 1.0F);
    EXPECT_FLOAT_EQ(style.LightBandThresholds[0], 0.125F);
    EXPECT_FLOAT_EQ(style.LightBandThresholds[3], 0.875F);
}

TEST(Oot3dPicaToon, PreservesAlphaAndFogOrdering) {
    const std::string source =
        "layout(location=5) in vec4 pica_normquat;\n"
        "layout(location=6) in vec3 pica_view;\n"
        "void main() {\n"
        "    vec4 rounded_primary_color = pica_primary_color;\n"
        "    vec3 lit = rounded_primary_color.rgb;\n"
        "    vec4 combiner_output = vec4(1.0);\n"
        "    if (combiner_output.a < 0.5) discard;\n"
        "    float pica_z_over_w = -gl_FragCoord.z;\n"
        "    float fog_index = pica_z_over_w * 128.0;\n"
        "    pica_color = combiner_output;\n}\n";
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0xFU };
    Fast::Oot3d::ToonStyleSettings style;
    const auto variant = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 42U, draw, Fast::Oot3d::ToonMode::PostProcessPreview, style);
    ASSERT_TRUE(variant.Applied());
    const auto alpha = variant.Source.find("discard");
    const auto toon = variant.Source.find("combiner_output.rgb = oot3d_apply_toon");
    const auto fog = variant.Source.find("float fog_index");
    ASSERT_NE(alpha, std::string::npos);
    ASSERT_NE(toon, std::string::npos);
    ASSERT_NE(fog, std::string::npos);
    EXPECT_LT(alpha, toon);
    EXPECT_LT(toon, fog);
    EXPECT_NE(variant.Source.find(
                  "vec3 banded = sourceColor * lightingScale"),
              std::string::npos);
    EXPECT_EQ(variant.Source.find(
                  "dot(sourceColor, lumaWeights)"),
              std::string::npos);
    EXPECT_NE(variant.FragmentKey, 42U);
}

TEST(Oot3dPicaToon, UsesDepthWriteToSeparateWorldFromOverlays) {
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, true, 0xFU };
    EXPECT_EQ(Fast::Oot3d::ClassifyPicaToonDraw(
                  draw, Fast::Oot3d::ToonMode::PostProcessPreview),
              Fast::Oot3d::PicaToonEligibility::Eligible);
    draw.DepthWriteEnabled = false;
    EXPECT_EQ(Fast::Oot3d::ClassifyPicaToonDraw(
                  draw, Fast::Oot3d::ToonMode::PostProcessPreview),
              Fast::Oot3d::PicaToonEligibility::NoDepth);
}

TEST(Oot3dPicaToon, UsesTypedSceneDomainInsteadOfTargetExtent) {
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0xFU };
    EXPECT_EQ(Fast::Oot3d::ClassifyPicaToonDraw(
                  draw, Fast::Oot3d::ToonMode::PostProcessPreview),
              Fast::Oot3d::PicaToonEligibility::Eligible);
    draw.CompositionDomain =
        Oot3d::Renderer::PicaCompositionDomain::Ui;
    EXPECT_EQ(Fast::Oot3d::ClassifyPicaToonDraw(
                  draw, Fast::Oot3d::ToonMode::PostProcessPreview),
              Fast::Oot3d::PicaToonEligibility::OutsideScene);
}

TEST(Oot3dPicaToon, SettingsChangeShaderVariantKey) {
    const std::string source = "void main() {\n    float pica_z_over_w = -gl_FragCoord.z;\n}";
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0x7U };
    Fast::Oot3d::ToonStyleSettings fourBands;
    auto sixBands = fourBands;
    sixBands.LightBands = 6;
    const auto first = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 8U, draw, Fast::Oot3d::ToonMode::PostProcessPreview, fourBands);
    const auto second = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 8U, draw, Fast::Oot3d::ToonMode::PostProcessPreview, sixBands);
    ASSERT_TRUE(first.Applied());
    ASSERT_TRUE(second.Applied());
    EXPECT_NE(first.FragmentKey, second.FragmentKey);
}

TEST(Oot3dPicaToon, CustomBandValuesChangeShaderAndVariantKey) {
    const std::string source =
        "void main() {\n"
        "    float pica_z_over_w = -gl_FragCoord.z;\n"
        "}";
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0x7U };
    Fast::Oot3d::ToonStyleSettings firstStyle;
    firstStyle.CustomLightBands = true;
    Fast::Oot3d::ResetToonLightBandProfile(firstStyle);
    auto secondStyle = firstStyle;
    secondStyle.LightBandLevels[1] = 0.2F;
    secondStyle.LightBandThresholds[0] = 0.12F;
    const auto first = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 9U, draw, Fast::Oot3d::ToonMode::PostProcessPreview,
        firstStyle);
    const auto second = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 9U, draw, Fast::Oot3d::ToonMode::PostProcessPreview,
        secondStyle);
    ASSERT_TRUE(first.Applied());
    ASSERT_TRUE(second.Applied());
    EXPECT_NE(first.FragmentKey, second.FragmentKey);
    EXPECT_NE(second.Source.find(
                  "vec4(0.120000,"),
              std::string::npos);
    EXPECT_NE(second.Source.find(
                  "float oot3d_toon_band_luminance"),
              std::string::npos);
}


TEST(Oot3dGrassToon, CollapsedNativeScalePreservesEveryByteAndValidStageCombination) {
    for (uint32_t combination = 0; combination < 729; ++combination) {
        uint32_t remaining = combination, packed = 0;
        for (uint32_t stage = 0; stage < 6; ++stage) {
            packed |= (remaining % 3) << (stage * 2);
            remaining /= 3;
        }
        const float scale = static_cast<float>(Fast::Renderer3ds::PicaSurfacePassthroughScale(packed));
        for (uint32_t byte = 0; byte < 256; ++byte) {
            float reference = static_cast<float>(byte) / 255.0F;
            for (uint32_t stage = 0; stage < 6; ++stage) {
                reference = std::floor(std::clamp(reference, 0.0F, 1.0F) * 255.0F + 0.5F) / 255.0F;
                reference = std::clamp(reference * static_cast<float>(1U << ((packed >> (stage * 2)) & 3U)), 0.0F, 1.0F);
            }
            EXPECT_FLOAT_EQ(reference, std::clamp(static_cast<float>(byte) / 255.0F * scale, 0.0F, 1.0F));
        }
    }
}


TEST(Oot3dGrassToon, RimDistanceValidationKeepsSmoothstepEdgesOrdered) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings;
    settings.Preset = GraphicsPreset::Custom;
    settings.Grass.Appearance.ToonRimFadeStart = 1000;
    settings.Grass.Appearance.ToonRimFadeEnd = 100;
    const auto checked = GraphicsSettingsService::Validate(settings, {}).Value.Grass.Appearance;
    EXPECT_EQ(checked.ToonRimFadeStart, 1000);
    EXPECT_EQ(checked.ToonRimFadeEnd, 1001);
    settings.Grass.Appearance.ToonRimFadeStart = -10;
    settings.Grass.Appearance.ToonRimFadeEnd = 0;
    const auto zero = GraphicsSettingsService::Validate(settings, {}).Value.Grass.Appearance;
    EXPECT_EQ(zero.ToonRimFadeStart, 0);
    EXPECT_EQ(zero.ToonRimFadeEnd, 1);
}

TEST(Oot3dGrassToon, PacksTheExistingStyleAndKeepsOffAndLightingIndependent) {
    using namespace Fast::Oot3d;
    ToonStyleSettings style;
    style.LightBands = 6;
    style.CustomLightBands = true;
    style.LightBandLevels = {0.03F, 0.2F, 0.3F, 0.4F, 0.8F, 1.0F};
    style.LightBandThresholds = {0.1F, 0.25F, 0.35F, 0.6F, 0.9F};
    style.BandSoftness = 0;
    const auto packed = PackToonSurfaceParameters(ToonMode::PicaMaterial, style, true);
    EXPECT_EQ(packed.Control, (std::array<float, 4>{5, 0, style.Saturation, 1}));
    EXPECT_EQ(packed.Flags, (std::array<float, 4>{1, 1, style.RimWidth, 0}));
    for (size_t i = 0; i < style.LightBandLevels.size(); ++i)
        EXPECT_EQ(packed.Levels[i / 4][i % 4], style.LightBandLevels[i]);
    for (size_t i = 0; i < style.LightBandThresholds.size(); ++i)
        EXPECT_EQ(packed.Thresholds[i / 4][i % 4], style.LightBandThresholds[i]);
    EXPECT_FLOAT_EQ(packed.Shadow[3], style.ShadowStrength);
    EXPECT_FLOAT_EQ(packed.Rim[3], style.RimStrength);
    EXPECT_FLOAT_EQ(PackToonSurfaceParameters(ToonMode::Off, style, true).Flags[0], 0);
    EXPECT_FLOAT_EQ(PackToonSurfaceParameters(ToonMode::PicaMaterial, style, false).Flags[1], 0);
    style.OutlineEnabled = !style.OutlineEnabled;
    style.OutlineWidth = 10;
    const auto outlineOnly = PackToonSurfaceParameters(ToonMode::PicaMaterial, style, true);
    EXPECT_EQ(packed.Flags, outlineOnly.Flags);
    EXPECT_EQ(packed.Control, outlineOnly.Control);
    EXPECT_EQ(packed.Shadow, outlineOnly.Shadow);
    EXPECT_EQ(packed.Rim, outlineOnly.Rim);
}

TEST(Oot3dPicaToon, MaterialModeInjectsBeforeTevAndNeverPostprocessesTexels) {
    const std::string source =
        "layout(location=5) in vec4 pica_normquat;\n"
        "layout(location=6) in vec3 pica_view;\n"
        "void main() {\n"
        "    vec3 normal = vec3(0.0, 0.0, 1.0);\n"
        "    vec4 primary_fragment_color = vec4(0.4);\n"
        "    vec4 secondary_fragment_color = vec4(0.2);\n"
        "    // OOT3D_PICA_MATERIAL_TOON_POINT\n"
        "    vec4 combiner_output = primary_fragment_color;\n"
        "    float pica_z_over_w = -gl_FragCoord.z;\n}\n";
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0xFU };
    const auto variant = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 91U, draw, Fast::Oot3d::ToonMode::PicaMaterial,
        Fast::Oot3d::ToonStyleSettings{});
    ASSERT_TRUE(variant.Applied());
    EXPECT_TRUE(variant.MaterialPath);
    const auto material = variant.Source.find(
        "primary_fragment_color.rgb = oot3d_material_bands");
    const auto tev = variant.Source.find("vec4 combiner_output");
    ASSERT_NE(material, std::string::npos);
    ASSERT_NE(tev, std::string::npos);
    EXPECT_LT(material, tev);
    EXPECT_EQ(variant.Source.find(
                  "combiner_output.rgb = oot3d_apply_toon"),
              std::string::npos);
}

TEST(Oot3dPicaToon, MaterialModeFallsBackForVertexLitPicaShaders) {
    const std::string source =
        "layout(location=5) in vec4 pica_normquat;\n"
        "layout(location=6) in vec3 pica_view;\n"
        "void main() {\n"
        "    vec4 rounded_primary_color = pica_primary_color;\n"
        "    vec4 combiner_output = rounded_primary_color;\n"
        "    float pica_z_over_w = -gl_FragCoord.z;\n}\n";
    Fast::Oot3d::PicaToonDrawInfo draw{
        Oot3d::Renderer::PicaCompositionDomain::Scene,
        true, true, false, 0xFU };
    const auto variant = Fast::Oot3d::BuildPicaToonShaderVariant(
        source, 92U, draw, Fast::Oot3d::ToonMode::PicaMaterial,
        Fast::Oot3d::ToonStyleSettings{});
    ASSERT_TRUE(variant.Applied());
    EXPECT_FALSE(variant.MaterialPath);
    EXPECT_NE(variant.Source.find(
                  "combiner_output.rgb = oot3d_apply_toon"),
              std::string::npos);
}

TEST(Oot3dGraphicsSettings, AuthenticForcesCompatibilityDefaults) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    auto candidate = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Enhanced);
    candidate.Preset = Fast::Oot3d::GraphicsPreset::Authentic;
    candidate.FovMultiplier = 1.5F;
    candidate.ExtendedDrawDistance = true;
    const auto result = Fast::Oot3d::GraphicsSettingsService::Validate(
        candidate, capabilities);
    EXPECT_TRUE(result.Accepted());
    EXPECT_FALSE(result.Value.ExtendedDrawDistance);
    EXPECT_EQ(result.Value.AntiAliasing,
              Fast::Oot3d::AntiAliasingMode::Off);
    EXPECT_EQ(result.Value.FrameRate,
              Fast::Oot3d::FrameRateMode::Original30);
    EXPECT_FLOAT_EQ(result.Value.FovMultiplier, 1.0F);
    EXPECT_EQ(result.Value.Effects.AmbientOcclusion,
              Fast::Oot3d::AmbientOcclusionMode::Off);
}

TEST(Oot3dGraphicsSettings, CapabilityGatesInvalidCombinations) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    capabilities.Set(Fast::Oot3d::GraphicsCapability::ExclusiveFullscreen,
                     false, "exclusive mode unavailable");
    capabilities.Set(Fast::Oot3d::GraphicsCapability::PresentTearing,
                     false, "tearing unavailable");
    auto candidate = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    candidate.Window = Fast::Oot3d::WindowMode::ExclusiveFullscreen;
    candidate.FrameRate = Fast::Oot3d::FrameRateMode::Uncapped;
    candidate.VSync = false;
    candidate.Effects.AmbientOcclusion =
        Fast::Oot3d::AmbientOcclusionMode::Gtao;
    const auto result = Fast::Oot3d::GraphicsSettingsService::Validate(
        candidate, capabilities);
    EXPECT_EQ(result.Value.Window, Fast::Oot3d::WindowMode::Borderless);
    EXPECT_TRUE(result.Value.VSync);
    EXPECT_EQ(result.Value.Effects.AmbientOcclusion,
              Fast::Oot3d::AmbientOcclusionMode::Off);
    EXPECT_EQ(result.Issues.size(), 3U);
}

TEST(Oot3dGraphicsSettings, MxaoRequiresItsProviderAndClampsItsControls) {
    using namespace Fast::Oot3d;
    GraphicsCapabilities capabilities;
    for (const auto capability : {GraphicsCapability::NriInterop, GraphicsCapability::SampledDepth,
                                  GraphicsCapability::NormalGuide, GraphicsCapability::ValidViewMetadata,
                                  GraphicsCapability::ImmerseMxao})
        capabilities.Set(capability, true);
    auto candidate = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    candidate.Effects.AmbientOcclusion = AmbientOcclusionMode::Mxao;
    candidate.Effects.MxaoMethod = static_cast<MxaoEstimator>(9);
    candidate.Effects.MxaoQuality = 9;
    candidate.Effects.MxaoFilter = 7;
    auto result = GraphicsSettingsService::Validate(candidate, capabilities);
    EXPECT_EQ(result.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Mxao);
    EXPECT_EQ(result.Value.Effects.MxaoMethod, MxaoEstimator::VisibilityBitmask);
    EXPECT_EQ(result.Value.Effects.MxaoQuality, 6U);
    EXPECT_EQ(result.Value.Effects.MxaoFilter, 2U);
    capabilities.Set(GraphicsCapability::ImmerseMxao, false, "provider unavailable");
    result = GraphicsSettingsService::Validate(candidate, capabilities);
    EXPECT_EQ(result.Value.Effects.AmbientOcclusion, AmbientOcclusionMode::Off);
    EXPECT_TRUE(std::any_of(result.Issues.begin(), result.Issues.end(), [](const auto& issue) {
        return issue.Field == "Graphics.Effects.AO.Mode" && issue.Message == "provider unavailable";
    }));
}

TEST(Oot3dGraphicsSettings, AoTextureReliefKeepsItsLevelAndResetsUnknownOnes) {
    using namespace Fast::Oot3d;
    GraphicsCapabilities capabilities;
    for (const auto capability : {GraphicsCapability::NriInterop, GraphicsCapability::SampledDepth,
                                  GraphicsCapability::NormalGuide, GraphicsCapability::ValidViewMetadata})
        capabilities.Set(capability, true);
    auto candidate = GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    candidate.Effects.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    candidate.Effects.AoRelief = AoTextureRelief::Subtle;
    EXPECT_EQ(GraphicsSettingsService::Validate(candidate, capabilities).Value.Effects.AoRelief,
              AoTextureRelief::Subtle);
    candidate.Effects.AoRelief = static_cast<AoTextureRelief>(7);
    EXPECT_EQ(GraphicsSettingsService::Validate(candidate, capabilities).Value.Effects.AoRelief,
              AoTextureRelief::Off);
    EXPECT_EQ(AoTextureReliefGain(AoTextureRelief::Off), 0.0F);
    EXPECT_LT(AoTextureReliefGain(AoTextureRelief::Subtle), AoTextureReliefGain(AoTextureRelief::Medium));
    EXPECT_LT(AoTextureReliefGain(AoTextureRelief::Medium), AoTextureReliefGain(AoTextureRelief::Strong));
}

TEST(Oot3dGraphicsSettings, AcceptsHiZOnlyWithMaterialAndViewContract) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    using Capability = Fast::Oot3d::GraphicsCapability;
    for (const auto capability : {Capability::NriInterop,
                                  Capability::SampledSceneColor,
                                  Capability::SampledDepth,
                                  Capability::NormalGuide,
                                  Capability::MaterialGuide,
                                  Capability::ValidViewMetadata}) {
        capabilities.Set(capability, true);
    }
    auto candidate = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    candidate.Effects.Reflections = Fast::Oot3d::ReflectionMode::HiZ;
    candidate.Effects.ReflectionMaxSteps = 255U;
    candidate.Effects.ReflectionThickness = -2.0F;
    const auto accepted = Fast::Oot3d::GraphicsSettingsService::Validate(
        candidate, capabilities);
    EXPECT_EQ(accepted.Value.Effects.Reflections,
              Fast::Oot3d::ReflectionMode::HiZ);
    EXPECT_EQ(accepted.Value.Effects.ReflectionMaxSteps, 64U);
    EXPECT_FLOAT_EQ(accepted.Value.Effects.ReflectionThickness, 0.1F);

    capabilities.Set(Capability::MaterialGuide, false);
    const auto rejected = Fast::Oot3d::GraphicsSettingsService::Validate(
        candidate, capabilities);
    EXPECT_EQ(rejected.Value.Effects.Reflections,
              Fast::Oot3d::ReflectionMode::Off);
}

TEST(Oot3dReflections, SelectsFidelityFxAndFallsBackDeterministically) {
    using Fast::Oot3d::ReflectionMode;
    using Fast::Oot3d::ReflectionProvider;
    Fast::Oot3d::ReflectionProviderAvailability available{
        true, true, true, true, true, true};
    EXPECT_EQ(Fast::Oot3d::SelectReflectionProvider(
                  ReflectionMode::FidelityFxSssr, available),
              ReflectionProvider::FidelityFxSssr);

    available.PerspectiveDepth = false;
    EXPECT_EQ(Fast::Oot3d::SelectReflectionProvider(
                  ReflectionMode::FidelityFxSssr, available),
              ReflectionProvider::HiZ);

    available.HiZ = false;
    EXPECT_EQ(Fast::Oot3d::SelectReflectionProvider(
                  ReflectionMode::FidelityFxSssr, available),
              ReflectionProvider::Off);
    EXPECT_EQ(Fast::Oot3d::SelectReflectionProvider(
                  ReflectionMode::Off,
                  {true, true, true, true, true, true}),
              ReflectionProvider::Off);
}

TEST(Oot3dGraphicsSettings,
     NormalizesReflectionMaterialRulesWithoutDroppingSelections) {
    using namespace Fast::Oot3d;
    auto settings =
        GraphicsSettingsService::Preset(GraphicsPreset::Custom);
    ReflectionMaterialRule first;
    first.RuleId = 7U;
    first.Target.ContentHash = 0x1234U;
    first.Target.MapperSlotMask = 0U;
    first.Reflectivity = 4.0F;
    first.Roughness = 0.0F;
    ReflectionMaterialRule second = first;
    second.Target.ContentHash = 0x5678U;
    second.Profile = ReflectionMaterialProfile::Metal;
    second.Reflectivity =
        std::numeric_limits<float>::quiet_NaN();
    second.Roughness =
        std::numeric_limits<float>::infinity();
    settings.Effects.ReflectionMaterials = {first, second};

    const auto validated = GraphicsSettingsService::Validate(
        settings, GraphicsCapabilities{});
    ASSERT_EQ(
        validated.Value.Effects.ReflectionMaterials.size(), 2U);
    const auto& normalized =
        validated.Value.Effects.ReflectionMaterials;
    EXPECT_EQ(normalized[0].RuleId, 7U);
    EXPECT_NE(normalized[1].RuleId, normalized[0].RuleId);
    EXPECT_EQ(normalized[0].Target.MapperSlotMask, 0x07U);
    EXPECT_FLOAT_EQ(normalized[0].Reflectivity, 1.0F);
    EXPECT_FLOAT_EQ(normalized[0].Roughness, 0.02F);
    EXPECT_EQ(normalized[0].Target.ContentHash, 0x1234U);
    EXPECT_EQ(normalized[1].Target.ContentHash, 0x5678U);
    EXPECT_FLOAT_EQ(normalized[1].Reflectivity, 0.88F);
    EXPECT_FLOAT_EQ(normalized[1].Roughness, 0.22F);
}

TEST(Oot3dGraphicsSettings, AcceptsFidelityFxOnlyWithTemporalProvider) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    using Capability = Fast::Oot3d::GraphicsCapability;
    for (const auto capability : {
             Capability::NriInterop, Capability::SampledSceneColor,
             Capability::SampledDepth,
             Capability::NormalGuide, Capability::MaterialGuide,
             Capability::ValidViewMetadata, Capability::MotionVectors,
             Capability::TemporalHistory,
             Capability::LinearHdrWorkingColor,
             Capability::FidelityFxSssr}) {
        capabilities.Set(capability, true);
    }
    auto candidate = Fast::Oot3d::GraphicsSettingsService::Preset(
        Fast::Oot3d::GraphicsPreset::Custom);
    candidate.Effects.Reflections =
        Fast::Oot3d::ReflectionMode::FidelityFxSssr;
    EXPECT_EQ(Fast::Oot3d::GraphicsSettingsService::Validate(
                  candidate, capabilities).Value.Effects.Reflections,
              Fast::Oot3d::ReflectionMode::FidelityFxSssr);

    capabilities.Set(Capability::MotionVectors, false);
    EXPECT_EQ(Fast::Oot3d::GraphicsSettingsService::Validate(
                  candidate, capabilities).Value.Effects.Reflections,
              Fast::Oot3d::ReflectionMode::HiZ);

    capabilities.Set(Capability::MotionVectors, true);
    capabilities.Set(Capability::LinearHdrWorkingColor, false);
    EXPECT_EQ(Fast::Oot3d::GraphicsSettingsService::Validate(
                  candidate, capabilities).Value.Effects.Reflections,
              Fast::Oot3d::ReflectionMode::HiZ);
}

TEST(Oot3dGraphicsSettings, TransactionCanConfirmAndRollback) {
    Fast::Oot3d::GraphicsCapabilities capabilities;
    Fast::Oot3d::GraphicsSettingsService service;
    auto custom = service.Current();
    custom.Preset = Fast::Oot3d::GraphicsPreset::Custom;
    custom.FovMultiplier = 1.25F;
    EXPECT_TRUE(service.Stage(custom, capabilities).Accepted());
    EXPECT_TRUE(service.ApplyPending());
    service.ConfirmCurrent();
    custom.FovMultiplier = 1.5F;
    service.Stage(custom, capabilities);
    service.ApplyPending();
    service.Rollback();
    EXPECT_FLOAT_EQ(service.Current().FovMultiplier, 1.25F);
}

TEST(Oot3dPresentationTransaction,
     RequiresBackendAcknowledgementBeforeConfirmation) {
    Fast::Oot3d::PresentationSettingsTransaction transaction(1000U);
    Fast::Oot3d::PresentationSettingsValue original;
    Fast::Oot3d::PresentationSettingsValue requested = original;
    requested.Window = Fast::Oot3d::WindowMode::Borderless;

    EXPECT_TRUE(transaction.Begin(original, requested));
    EXPECT_EQ(
        transaction.Status(100U).Phase,
        Fast::Oot3d::PresentationTransactionPhase::ApplyRequested);
    EXPECT_FALSE(transaction.Confirm());
    EXPECT_TRUE(transaction.MarkApplied(requested, 100U));
    const auto awaiting = transaction.Status(100U);
    EXPECT_EQ(
        awaiting.Phase,
        Fast::Oot3d::PresentationTransactionPhase::AwaitingConfirmation);
    EXPECT_EQ(awaiting.RemainingMilliseconds, 1000U);
    EXPECT_TRUE(transaction.Confirm());
    EXPECT_EQ(
        transaction.Status(101U).Phase,
        Fast::Oot3d::PresentationTransactionPhase::Idle);
    EXPECT_EQ(transaction.LastKnownGood(), requested);
}

TEST(Oot3dPresentationTransaction,
     TimeoutRequestsAndCompletesExactRollback) {
    Fast::Oot3d::PresentationSettingsTransaction transaction(500U);
    Fast::Oot3d::PresentationSettingsValue original;
    Fast::Oot3d::PresentationSettingsValue requested = original;
    requested.Window = Fast::Oot3d::WindowMode::ExclusiveFullscreen;
    requested.Width = 1920U;
    requested.Height = 1080U;
    requested.VSync = false;

    ASSERT_TRUE(transaction.Begin(original, requested));
    ASSERT_TRUE(transaction.MarkApplied(requested, 200U));
    EXPECT_FALSE(transaction.Advance(699U));
    EXPECT_TRUE(transaction.Advance(700U));
    EXPECT_EQ(
        transaction.Status(700U).Phase,
        Fast::Oot3d::PresentationTransactionPhase::RollbackRequested);
    EXPECT_EQ(transaction.Target(), original);
    EXPECT_TRUE(transaction.MarkApplied(original, 700U));
    EXPECT_EQ(
        transaction.Status(700U).Phase,
        Fast::Oot3d::PresentationTransactionPhase::Idle);
}

TEST(Oot3dPresentationTransaction,
     WindowedResolutionCommitsWithoutSilentTimeoutRollback) {
    Fast::Oot3d::PresentationSettingsTransaction transaction(500U);
    Fast::Oot3d::PresentationSettingsValue original;
    Fast::Oot3d::PresentationSettingsValue requested = original;
    requested.Width = 1920U;
    requested.Height = 1080U;

    ASSERT_TRUE(transaction.Begin(original, requested));
    ASSERT_TRUE(transaction.MarkApplied(requested, 200U));
    EXPECT_EQ(transaction.Status(200U).Phase,
              Fast::Oot3d::PresentationTransactionPhase::Idle);
    EXPECT_EQ(transaction.LastKnownGood(), requested);
    EXPECT_FALSE(transaction.Advance(1000U));
}

TEST(Oot3dPresentationTransaction,
     RetargetPreservesOriginalLastKnownGood) {
    Fast::Oot3d::PresentationSettingsTransaction transaction;
    Fast::Oot3d::PresentationSettingsValue original;
    Fast::Oot3d::PresentationSettingsValue borderless = original;
    borderless.Window = Fast::Oot3d::WindowMode::Borderless;
    Fast::Oot3d::PresentationSettingsValue fullscreen = borderless;
    fullscreen.Window =
        Fast::Oot3d::WindowMode::ExclusiveFullscreen;

    ASSERT_TRUE(transaction.Begin(original, borderless));
    ASSERT_TRUE(transaction.MarkApplied(borderless, 10U));
    ASSERT_TRUE(transaction.Begin(borderless, fullscreen));
    EXPECT_EQ(transaction.LastKnownGood(), original);
    ASSERT_TRUE(transaction.Begin(fullscreen, original));
    EXPECT_EQ(
        transaction.Status(20U).Phase,
        Fast::Oot3d::PresentationTransactionPhase::RollbackRequested);
    EXPECT_EQ(transaction.Target(), original);
}

TEST(Oot3dRendererPresentationController,
     AppliesInitialSettingsAndOnlyPendingTransactionsAfterwards) {
    Fast::Oot3d::GraphicsSettings settings;
    settings.Window = Fast::Oot3d::WindowMode::Borderless;
    settings.OutputWidth = 1920U;
    settings.OutputHeight = 1080U;
    settings.VSync = false;
    Fast::Oot3d::PresentationTransactionStatus idle;

    const auto initial =
        Fast::Oot3d::ResolveRendererPresentationRequest(
            settings, idle, false);
    ASSERT_TRUE(initial.has_value());
    EXPECT_EQ(initial->Kind,
              Fast::Oot3d::RendererPresentationRequestKind::Initial);
    EXPECT_EQ(initial->Value,
              Fast::Oot3d::GetPresentationSettings(settings));
    EXPECT_FALSE(initial->RequiresAcknowledgement());
    EXPECT_FALSE(
        Fast::Oot3d::ResolveRendererPresentationRequest(
            settings, idle, true).has_value());

    Fast::Oot3d::PresentationTransactionStatus candidate;
    candidate.Phase =
        Fast::Oot3d::PresentationTransactionPhase::ApplyRequested;
    candidate.Requested = initial->Value;
    candidate.Requested.Width = 2560U;
    candidate.Requested.Height = 1440U;
    const auto apply =
        Fast::Oot3d::ResolveRendererPresentationRequest(
            settings, candidate, true);
    ASSERT_TRUE(apply.has_value());
    EXPECT_EQ(apply->Kind,
              Fast::Oot3d::RendererPresentationRequestKind::Candidate);
    EXPECT_EQ(apply->Value, candidate.Requested);
    EXPECT_TRUE(apply->RequiresAcknowledgement());

    candidate.Phase =
        Fast::Oot3d::PresentationTransactionPhase::RollbackRequested;
    candidate.LastKnownGood = initial->Value;
    const auto rollback =
        Fast::Oot3d::ResolveRendererPresentationRequest(
            settings, candidate, true);
    ASSERT_TRUE(rollback.has_value());
    EXPECT_EQ(rollback->Kind,
              Fast::Oot3d::RendererPresentationRequestKind::Rollback);
    EXPECT_EQ(rollback->Value, candidate.LastKnownGood);
}

TEST(Oot3dSceneViewBridge, TracksHistoryPerViewAndTarget) {
    Fast::Oot3d::SceneViewBridge bridge;
    Fast::Oot3d::SceneViewInfo first;
    first.ViewId = 7;
    first.Target = { 1, 2, 3 };
    first.ViewProjectionMatrix[0] = 10.0F;
    bridge.Publish(first);
    auto second = first;
    second.CameraCut = false;
    second.ViewProjectionMatrix[0] = 20.0F;
    bridge.Publish(second);
    const auto resolved = bridge.Find(first.Target);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_FLOAT_EQ(resolved->PreviousViewProjectionMatrix[0], 10.0F);
    EXPECT_FLOAT_EQ(resolved->ViewProjectionMatrix[0], 20.0F);
}

TEST(Oot3dEffectGraph, CompilesDependenciesAndRejectsCycles) {
    Fast::Oot3d::EffectGraph graph;
    EXPECT_TRUE(graph.Add({ "depth", {} }));
    EXPECT_TRUE(graph.Add({ "ao", { "depth" } }));
    EXPECT_TRUE(graph.Add({ "composite", { "ao" } }));
    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.Valid()) << compiled.Error;
    ASSERT_EQ(compiled.Order.size(), 3U);
    EXPECT_EQ(compiled.Order.front(), "depth");
    EXPECT_EQ(compiled.Order.back(), "composite");

    graph.Clear();
    graph.Add({ "a", { "b" } });
    graph.Add({ "b", { "a" } });
    EXPECT_FALSE(graph.Compile().Valid());
}

TEST(Oot3dDisplayEffectPlan,
     BuildsDeterministicAdvancedDependencies) {
    using namespace Fast::Oot3d;
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    input.Reflections = ReflectionMode::FidelityFxSssr;
    input.Toon = ToonMode::PicaMaterial;
    input.OutlineEnabled = true;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto plan = BuildDisplayEffectPlan(input);
    EXPECT_TRUE(plan.AmbientOcclusion);
    EXPECT_TRUE(plan.FidelityFxSssr);
    EXPECT_TRUE(plan.MotionVectors);
    EXPECT_TRUE(plan.RequiresGuideTarget);
    ASSERT_TRUE(plan.Graph.Valid()) << plan.Graph.Error;
    const std::vector<std::string> expected{
        "Guides", "WorkingColor", "AmbientOcclusion", "HiZ", "Motion",
        "Reflections", "Outline", "Composite", "TAA", "Scanout"};
    EXPECT_EQ(plan.Graph.Order, expected);
}

TEST(Oot3dDisplayEffectPlan,
     SuppressesOverlayAndCapabilityGatesSmaa) {
    using namespace Fast::Oot3d;
    DisplayEffectPlanInput input;
    input.AlphaOverlay = true;
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    input.AmbientOcclusion = AmbientOcclusionMode::Gtao;
    input.AntiAliasing = AntiAliasingMode::Taa;
    const auto overlay = BuildDisplayEffectPlan(input);
    EXPECT_FALSE(overlay.AmbientOcclusion);
    EXPECT_FALSE(overlay.Taa);
    EXPECT_FALSE(overlay.RequiresGuideTarget);
    EXPECT_EQ(overlay.Graph.Order,
              std::vector<std::string>{"Scanout"});

    input = {};
    input.WorldSurface = true;
    input.AntiAliasing = AntiAliasingMode::Smaa1x;
    EXPECT_FALSE(BuildDisplayEffectPlan(input).Smaa);
    input.SmaaAvailable = true;
    const auto smaa = BuildDisplayEffectPlan(input);
    EXPECT_TRUE(smaa.Smaa);
    EXPECT_EQ(smaa.Graph.Order,
              (std::vector<std::string>{"SMAA", "Scanout"}));
}

TEST(Oot3dDisplayEffectPlan,
     SeparatesSpatialAndTemporalUpscalerRequirements) {
    using namespace Fast::Oot3d;
    DisplayEffectPlanInput input;
    input.WorldSurface = true;
    input.AntiAliasing = AntiAliasingMode::Upscaler;
    input.Upscaler = UpscalerProvider::Nis;
    auto plan = BuildDisplayEffectPlan(input);
    EXPECT_TRUE(plan.Nis);
    EXPECT_FALSE(plan.MotionVectors);
    EXPECT_EQ(plan.Graph.Order,
              (std::vector<std::string>{"NIS", "Scanout"}));

    input.Upscaler = UpscalerProvider::Fsr;
    EXPECT_FALSE(BuildDisplayEffectPlan(input).Fsr)
        << "temporal providers require authoritative camera metadata";
    input.PerspectiveAvailable = true;
    input.CameraAvailable = true;
    plan = BuildDisplayEffectPlan(input);
    EXPECT_TRUE(plan.Fsr);
    EXPECT_TRUE(plan.TemporalUpscaler);
    EXPECT_TRUE(plan.MotionVectors);
    EXPECT_EQ(plan.Graph.Order,
              (std::vector<std::string>{
                  "Guides", "WorkingColor", "HiZ", "Motion",
                  "TemporalUpscaler", "Scanout"}));
}

TEST(Oot3dSceneSurfaces, ReplacementRetiresOldGeneration) {
    uint32_t retired = 0;
    Fast::Oot3d::SceneSurfaceRegistry registry(
        [&](const Fast::Oot3d::SceneSurface&) { ++retired; });
    Fast::Oot3d::SceneSurface surface{ { 4, 8, Fast::Oot3d::SceneSurfaceKind::Depth },
                                       100, 320, 240, 1, 0, true };
    const auto firstGeneration = registry.Publish(surface).Generation;
    EXPECT_EQ(registry.Publish(surface).Generation, firstGeneration);
    surface.NativeImage = 200;
    EXPECT_GT(registry.Publish(surface).Generation, firstGeneration);
    EXPECT_EQ(retired, 1U);
}

TEST(Oot3dSceneSurfaces, EncodingParticipatesInSurfaceIdentity) {
    uint32_t retired = 0;
    Fast::Oot3d::SceneSurfaceRegistry registry(
        [&](const Fast::Oot3d::SceneSurface&) { ++retired; });
    Fast::Oot3d::SceneSurface surface{
        {4, 8, Fast::Oot3d::SceneSurfaceKind::Composite},
        100, 320, 240, 97, 0, true,
        Fast::Oot3d::SceneColorEncoding::Srgb};
    const auto srgbGeneration = registry.Publish(surface).Generation;
    EXPECT_EQ(registry.Publish(surface).Generation, srgbGeneration);
    surface.ColorEncoding = Fast::Oot3d::SceneColorEncoding::Linear;
    EXPECT_GT(registry.Publish(surface).Generation, srgbGeneration);
    EXPECT_EQ(retired, 1U);
}

TEST(Oot3dResourceStates, EmitsOnlyRealTransitions) {
    Fast::Oot3d::ResourceStateTracker tracker;
    const Fast::Oot3d::ResourceState sampled{ Fast::Oot3d::ResourceAccess::ShaderRead, 2 };
    const auto planned = tracker.PlanTransition(55, sampled);
    EXPECT_TRUE(planned.Required());
    EXPECT_FALSE(tracker.Find(55).has_value());
    tracker.Commit(planned);
    ASSERT_TRUE(tracker.Find(55).has_value());
    EXPECT_EQ(tracker.Find(55)->Access,
              Fast::Oot3d::ResourceAccess::ShaderRead);
    EXPECT_FALSE(tracker.Transition(55, sampled).Required());
    EXPECT_TRUE(tracker.Transition(55, { Fast::Oot3d::ResourceAccess::ComputeWrite, 2 }).Required());
}

TEST(Oot3dGrassSceneBridge, PreservesStaticAnchorAcrossPresentationGaps) {
    auto& bridge = Fast::Oot3d::GrassSceneBridge::Instance();
    bridge.Clear();
    Fast::Oot3d::GrassSceneMesh mesh;
    mesh.GeometryId = 10U;
    mesh.InstanceId = 20U;
    mesh.RenderTargetNamespace = 7U;
    mesh.FramebufferColorPhysicalAddress = 0x123400U;
    mesh.FrameId = 100U;
    mesh.TextureHash = 30U;
    mesh.TextureWidth = 1U;
    mesh.TextureHeight = 1U;
    mesh.MapperSlot = 0U;
    mesh.ModelToWorld = {1, 0, 0, 10, 0, 1, 0, 0,
                         0, 0, 1, 0, 0, 0, 0, 1};
    mesh.PicaModelToClip = {1, 0, 0, 1, 0, 1, 0, 0,
                            0, 0, 1, 0, 0, 0, 0, 1};
    mesh.PicaModelToClipAvailable = true;
    mesh.TransformBakedIntoVertices = false;
    mesh.PreserveWorldAnchor = true;
    mesh.Vertices = std::make_shared<const std::vector<
        Fast::Oot3d::GrassSourceVertex>>(3U);
    mesh.Indices = std::make_shared<const std::vector<uint32_t>>(
        std::initializer_list<uint32_t>{0U, 1U, 2U});
    bridge.Publish(mesh);

    mesh.FrameId = 101U;
    mesh.ModelToWorld[3] = 100.0F;
    mesh.PicaModelToClip[3] = 2.0F;
    bridge.Publish(mesh);
    Fast::Oot3d::GrassTextureSelector selector;
    selector.Rgba8Hash = 30U;
    selector.Width = 1U;
    selector.Height = 1U;
    const Fast::Oot3d::GrassSceneScope currentScope{
        101U, 7U, 0x123400U};
    const auto continuous =
        bridge.Matching(selector, &currentScope);
    ASSERT_EQ(continuous.size(), 1U);
    const auto indexed =
        bridge.Match(selector, &currentScope);
    EXPECT_EQ(indexed.MatchingMeshes, 1U);
    EXPECT_EQ(indexed.ScopedMeshes.size(), 1U);
    EXPECT_FLOAT_EQ(continuous.front().ModelToWorld[3], 10.0F);
    EXPECT_FLOAT_EQ(continuous.front().PicaModelToClip[3], 2.0F);
    const Fast::Oot3d::GrassSceneScope staleScope{
        100U, 7U, 0x123400U};
    EXPECT_TRUE(
        bridge.Matching(selector, &staleScope).empty());
    const Fast::Oot3d::GrassSceneScope otherTarget{
        101U, 7U, 0x567800U};
    EXPECT_TRUE(
        bridge.Matching(selector, &otherTarget).empty());

    mesh.FrameId = 102U;
    mesh.ContentVersion = 2U;
    mesh.ModelToWorld[3] = 200.0F;
    mesh.Vertices = std::make_shared<const std::vector<
        Fast::Oot3d::GrassSourceVertex>>(4U);
    bridge.Publish(mesh);
    const Fast::Oot3d::GrassSceneScope refreshedScope{
        102U, 7U, 0x123400U};
    const auto refreshed =
        bridge.Matching(selector, &refreshedScope);
    ASSERT_EQ(refreshed.size(), 1U);
    EXPECT_EQ(refreshed.front().ContentVersion, 2U);
    EXPECT_EQ(refreshed.front().Vertices->size(), 4U);
    EXPECT_FLOAT_EQ(refreshed.front().ModelToWorld[3], 10.0F);

    bridge.PruneBeforeFrame(103U);
    EXPECT_EQ(bridge.Size(), 0U);
    EXPECT_TRUE(bridge.Matching(selector).empty());
    mesh.FrameId = 105U;
    mesh.ModelToWorld[3] = 100.0F;
    bridge.Publish(mesh);
    const Fast::Oot3d::GrassSceneScope afterGapScope{
        105U, 7U, 0x123400U};
    const auto afterGap =
        bridge.Matching(selector, &afterGapScope);
    ASSERT_EQ(afterGap.size(), 1U);
    EXPECT_FLOAT_EQ(afterGap.front().ModelToWorld[3], 10.0F);
    EXPECT_TRUE(bridge.Matching(selector, &refreshedScope).empty());
    bridge.RemoveRoom(7U);
    mesh.FrameId = 200U;
    bridge.Publish(mesh);
    EXPECT_FLOAT_EQ(bridge.Matching(selector).front().ModelToWorld[3], 100.0F);
    bridge.RemoveInstance(mesh.InstanceId);
    mesh.ModelToWorld[3] = 300.0F;
    bridge.Publish(mesh);
    EXPECT_FLOAT_EQ(bridge.Matching(selector).front().ModelToWorld[3], 300.0F);
    mesh.ModelToWorld[3] = 400.0F;
    mesh.AnchorVersion = 2U;
    bridge.Publish(mesh);
    EXPECT_FLOAT_EQ(bridge.Matching(selector).front().ModelToWorld[3], 400.0F);
    mesh.InstanceId = 10U;
    bridge.Publish(mesh);
    auto ordered = bridge.Matching(selector);
    ASSERT_EQ(ordered.size(), 2U);
    EXPECT_EQ(ordered[0].InstanceId, 10U);
    EXPECT_EQ(ordered[1].InstanceId, 20U);
    bridge.RemoveInstance(20U);
    mesh.InstanceId = 20U;
    bridge.Publish(mesh);
    ordered = bridge.Matching(selector);
    ASSERT_EQ(ordered.size(), 2U);
    EXPECT_EQ(ordered[0].InstanceId, 10U);
    EXPECT_EQ(ordered[1].InstanceId, 20U);
    bridge.Clear();
}

TEST(Oot3dGrassSceneBridge, KeepsWorldAnchorWhenRedrawnIntoAnotherTarget) {
    // A pause page redraws the scene into another framebuffer: the same world
    // instance must keep its anchor there instead of re-deriving the matrix
    // from that target's projection, while target scoping stays per target.
    auto& bridge = Fast::Oot3d::GrassSceneBridge::Instance();
    bridge.Clear();
    Fast::Oot3d::GrassSceneMesh mesh;
    mesh.GeometryId = 10U;
    mesh.InstanceId = 20U;
    mesh.WorldInstanceId = 99U;
    mesh.RenderTargetNamespace = 7U;
    mesh.FramebufferColorPhysicalAddress = 0x123400U;
    mesh.FrameId = 100U;
    mesh.TextureHash = 30U;
    mesh.TextureWidth = 1U;
    mesh.TextureHeight = 1U;
    mesh.ModelToWorld = {1, 0, 0, 10, 0, 1, 0, 0,
                         0, 0, 1, 0, 0, 0, 0, 1};
    mesh.PicaModelToClipAvailable = true;
    mesh.TransformBakedIntoVertices = false;
    mesh.PreserveWorldAnchor = true;
    mesh.Vertices = std::make_shared<const std::vector<
        Fast::Oot3d::GrassSourceVertex>>(3U);
    mesh.Indices = std::make_shared<const std::vector<uint32_t>>(
        std::initializer_list<uint32_t>{0U, 1U, 2U});
    bridge.Publish(mesh);

    mesh.InstanceId = 21U;
    mesh.RenderTargetNamespace = 8U;
    mesh.FramebufferColorPhysicalAddress = 0x567800U;
    mesh.FrameId = 101U;
    mesh.ModelToWorld[3] = 10.00006F;
    bridge.Publish(mesh);
    Fast::Oot3d::GrassTextureSelector selector;
    selector.Rgba8Hash = 30U;
    selector.Width = 1U;
    selector.Height = 1U;
    const Fast::Oot3d::GrassSceneScope redrawn{101U, 8U, 0x567800U};
    const auto scoped = bridge.Matching(selector, &redrawn);
    ASSERT_EQ(scoped.size(), 1U);
    EXPECT_EQ(scoped.front().InstanceId, 21U);
    EXPECT_FLOAT_EQ(scoped.front().ModelToWorld[3], 10.0F);
    const Fast::Oot3d::GrassSceneScope original{101U, 7U, 0x123400U};
    EXPECT_TRUE(bridge.Matching(selector, &original).empty());

    // Without a world instance the anchor stays per render target.
    mesh.WorldInstanceId = 0U;
    mesh.InstanceId = 22U;
    mesh.FrameId = 102U;
    mesh.ModelToWorld[3] = 10.00006F;
    bridge.Publish(mesh);
    const Fast::Oot3d::GrassSceneScope legacy{102U, 8U, 0x567800U};
    const auto unanchored = bridge.Matching(selector, &legacy);
    ASSERT_EQ(unanchored.size(), 1U);
    EXPECT_FLOAT_EQ(unanchored.front().ModelToWorld[3], 10.00006F);
    bridge.Clear();
}

TEST(Oot3dVisualClock, AdvancesOncePerPresentationAndFreezesOnPause) {
    auto& clock = Fast::Oot3d::VisualClock::Instance();
    clock.Reset();
    clock.Publish(0.02, false, 10U);
    const auto first = clock.Snapshot();
    EXPECT_TRUE(first.Available);
    EXPECT_NEAR(first.Seconds, 0.02, 1.0e-6);
    EXPECT_NEAR(first.DeltaSeconds, 0.02F, 1.0e-6F);

    clock.Publish(0.02, false, 10U);
    const auto duplicate = clock.Snapshot();
    EXPECT_NEAR(duplicate.Seconds, first.Seconds, 1.0e-6);
    EXPECT_FLOAT_EQ(duplicate.DeltaSeconds, 0.0F);

    clock.Publish(0.02, true, 11U);
    const auto paused = clock.Snapshot();
    EXPECT_TRUE(paused.Paused);
    EXPECT_NEAR(paused.Seconds, first.Seconds, 1.0e-6);
    clock.Reset();
}

TEST(Oot3dGrassInteractionBridge, PreservesSweepAndDetectsTeleport) {
    auto& bridge = Fast::Oot3d::GrassInteractionBridge::Instance();
    bridge.Reset();
    bridge.PublishLink({0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F},
                       35.0F, 85.0F, 100U, 7U, 9U);
    ASSERT_TRUE(bridge.LatestLink().has_value());
    EXPECT_TRUE(bridge.LatestLink()->Teleported);
    bridge.PublishLink({40.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F},
                       35.0F, 85.0F, 101U, 7U, 9U);
    const auto moved = bridge.LatestLink();
    ASSERT_TRUE(moved.has_value());
    EXPECT_FALSE(moved->Teleported);
    EXPECT_FLOAT_EQ(moved->PreviousPosition[0], 0.0F);

    bridge.PublishLink({40.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F},
                       35.0F, 85.0F, 101U, 7U, 9U);
    EXPECT_FLOAT_EQ(bridge.LatestLink()->PreviousPosition[0], 0.0F);
    bridge.PublishLink({1000.0F, 0.0F, 0.0F}, {},
                       35.0F, 85.0F, 102U, 7U, 9U);
    EXPECT_TRUE(bridge.LatestLink()->Teleported);
    bridge.Reset();
}

TEST(Oot3dGrassInteractionBridge, SamplesAllActorsOnTheGeometryClockAndRemovesDespawnedActors) {
    using namespace Fast::Oot3d;
    auto& bridge = GrassInteractionBridge::Instance();
    bridge.Reset();
    std::array<GrassInteractor, 2> actors;
    actors[0].StableId = 10;
    actors[1].StableId = 20;
    actors[0].Radius = actors[1].Radius = 30;
    actors[1].Position = {100, 0, 0};
    bridge.PublishFrame(10, actors);
    Fast::Renderer3ds::PicaFrameTemporalSample sample;
    sample.CurrentSourceFrameId = sample.PreviousSourceFrameId = 10;
    sample.ContinuityEpoch = 1;
    sample.SampleDeltaSeconds = 1.0F / 60.0F;
    bridge.SelectSample(sample);
    ASSERT_EQ(bridge.LatestActors().size(), 2);
    actors[0].Position[0] = 40;
    actors[1].Position[0] = 180;
    bridge.PublishFrame(11, actors);
    sample.CurrentSourceFrameId = 11;
    sample.Alpha = 0.5F;
    bridge.SelectSample(sample);
    const auto first = bridge.LatestActors();
    ASSERT_EQ(first.size(), 2);
    EXPECT_FLOAT_EQ(first[0].Position[0], 20);
    EXPECT_FLOAT_EQ(first[1].Position[0], 140);
    EXPECT_FLOAT_EQ(first[1].PreviousPosition[0], 100);
    EXPECT_FALSE(first[1].Teleported);
    bridge.SelectSample(sample);
    EXPECT_EQ(bridge.LatestActors()[1].FrameId, first[1].FrameId);
    sample.Alpha = 1;
    bridge.SelectSample(sample);
    EXPECT_FLOAT_EQ(bridge.LatestActors()[1].PreviousPosition[0], 140);
    bridge.PublishFrame(12, std::span<const GrassInteractor>(actors.data(), 1));
    sample.PreviousSourceFrameId = 11;
    sample.CurrentSourceFrameId = 12;
    bridge.SelectSample(sample);
    ASSERT_EQ(bridge.LatestActors().size(), 1);
    bridge.PublishFrame(13, {});
    sample.CurrentSourceFrameId = 13;
    bridge.SelectSample(sample);
    EXPECT_TRUE(bridge.LatestActors().empty());
    bridge.Reset();
}

TEST(Oot3dGrassInteractionBridge, TeleportsDoNotSweepAcrossTheScene) {
    using namespace Fast::Oot3d;
    auto& bridge = GrassInteractionBridge::Instance();
    bridge.Reset();
    GrassInteractor actor;
    actor.Radius = 30;
    bridge.PublishFrame(1, {&actor, 1});
    Fast::Renderer3ds::PicaFrameTemporalSample sample;
    sample.CurrentSourceFrameId = sample.PreviousSourceFrameId = 1;
    sample.ContinuityEpoch = 1;
    bridge.SelectSample(sample);
    actor.Position[0] = 2000;
    bridge.PublishFrame(2, {&actor, 1});
    sample.CurrentSourceFrameId = 2;
    sample.Alpha = 0.5F;
    bridge.SelectSample(sample);
    const auto result = bridge.LatestActors();
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result[0].Teleported);
    EXPECT_FLOAT_EQ(result[0].Position[0], 2000);
    EXPECT_EQ(result[0].Position, result[0].PreviousPosition);
    bridge.Reset();
}

TEST(Oot3dGrassInteractionField, MultipleActorsStampOnceWithoutClearingEachOthersTrail) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.InteractionFieldResolution = 64;
    settings.InteractionFieldRadius = 160;
    settings.CollisionPush = 1;
    settings.MaximumBend = 2;
    GrassInteractionField field;
    std::array<GrassInteractor, 2> actors;
    for (size_t i = 0; i < actors.size(); ++i) {
        auto& a = actors[i];
        a.StableId = i + 1;
        a.FrameId = 1;
        a.Radius = 30;
        a.PreviousPosition = {-40, 0, float(i) * 80};
        a.Position = {40, 0, float(i) * 80};
    }
    field.UpdateActors(0, settings, actors);
    const auto first = field.Sample(5, 10);
    const auto second = field.Sample(5, 90);
    EXPECT_GT(std::hypot(first[0], first[1]), 0);
    EXPECT_GT(std::hypot(second[0], second[1]), 0);
    field.UpdateActors(0, settings, actors);
    EXPECT_EQ(field.Sample(5, 10), first);
    EXPECT_EQ(field.Sample(5, 90), second);
    actors[1].Teleported = true;
    ++actors[1].FrameId;
    field.UpdateActors(0, settings, actors);
    EXPECT_EQ(field.Sample(5, 10), first);
    EXPECT_EQ(field.Sample(5, 10, 200, 5), (std::array<float, 2>{}));
}

TEST(Oot3dGrassCollision, SweptCapsuleCatchesMotionAndRejectsOtherFloors) {
    Fast::Oot3d::InteractiveGrassSettings settings;
    settings.CollisionPush = 1.0F;
    settings.MaximumBend = 1.0F;
    settings.InteractionVerticalMargin = 5.0F;
    Fast::Oot3d::GrassInteractor link;
    link.PreviousPosition = {0.0F, 0.0F, 0.0F};
    link.Position = {100.0F, 0.0F, 0.0F};
    link.Velocity = {100.0F, 0.0F, 0.0F};
    link.Radius = 10.0F;
    link.HalfHeight = 40.0F;
    const auto swept = Fast::Oot3d::ResolveGrassCollision(
        {50.0F, 0.0F, 5.0F}, 20.0F, link, settings);
    EXPECT_TRUE(swept.VerticalOverlap);
    EXPECT_GT(swept.Weight, 0.0F);
    EXPECT_GT(swept.Bend[1], 0.0F);

    const auto otherFloor = Fast::Oot3d::ResolveGrassCollision(
        {50.0F, 250.0F, 5.0F}, 20.0F, link, settings);
    EXPECT_FALSE(otherFloor.VerticalOverlap);
    EXPECT_FLOAT_EQ(otherFloor.Weight, 0.0F);
}

TEST(Oot3dGrassCollision, DirectPushDoesNotDropWhenTheActorStops) {
    Fast::Oot3d::InteractiveGrassSettings settings;
    settings.CollisionPush = 1.0F;
    settings.CollisionVelocityResponse = 3.0F;
    settings.MaximumBend = 1.0F;
    Fast::Oot3d::GrassInteractor link;
    link.PreviousPosition = {0.0F, 0.0F, 0.0F};
    link.Position = {0.0F, 0.0F, 0.0F};
    link.Velocity = {600.0F, 0.0F, 0.0F};
    link.Radius = 10.0F;
    link.HalfHeight = 40.0F;
    const auto running = Fast::Oot3d::ResolveGrassCollision({5.0F, 0.0F, 0.0F}, 20.0F, link, settings);
    link.Velocity = {};
    const auto stopped = Fast::Oot3d::ResolveGrassCollision({5.0F, 0.0F, 0.0F}, 20.0F, link, settings);
    EXPECT_FLOAT_EQ(running.Weight, 0.5F);
    EXPECT_FLOAT_EQ(stopped.Weight, running.Weight);
    EXPECT_EQ(stopped.Bend, running.Bend);
}

TEST(Oot3dGrassInteractionField, KeepsTrailAtWorldCoordinatesWhenRecentering) {
    Fast::Oot3d::InteractiveGrassSettings settings;
    settings.InteractionFieldResolution = 32;
    settings.InteractionFieldRadius = 160.0F;
    settings.CollisionPush = 1.0F;
    settings.MaximumBend = 2.0F;
    settings.RecoverySeconds = 10.0F;
    Fast::Oot3d::GrassInteractionField field;
    Fast::Oot3d::GrassInteractor link;
    link.StableId = 3U;
    link.PreviousPosition = {-40.0F, 0.0F, 0.0F};
    link.Position = {40.0F, 0.0F, 0.0F};
    link.Velocity = {100.0F, 0.0F, 0.0F};
    link.Radius = 30.0F;
    link.FrameId = 1U;
    field.Update(1.0F / 60.0F, settings, link);
    EXPECT_TRUE(field.Contains(5.0F, 15.0F));
    EXPECT_FALSE(field.Contains(1000.0F, 1000.0F));
    const auto before = field.Sample(5.0F, 15.0F, 0.0F, 5.0F);
    EXPECT_GT(std::abs(before[0]) + std::abs(before[1]), 0.0F);

    link.PreviousPosition = {-40.0F, 0.0F, 0.0F};
    link.Position = {80.0F, 0.0F, 0.0F};
    field.Update(0.0F, settings, link);
    const auto after = field.Sample(5.0F, 15.0F, 0.0F, 5.0F);
    EXPECT_NEAR(after[0], before[0], 1.0e-5F);
    EXPECT_NEAR(after[1], before[1], 1.0e-5F);
    const auto otherFloor = field.Sample(5.0F, 15.0F, 200.0F, 5.0F);
    EXPECT_FLOAT_EQ(otherFloor[0], 0.0F);
    EXPECT_FLOAT_EQ(otherFloor[1], 0.0F);
}

TEST(Oot3dGrassInteractionField, KeepsLinksTrailWhenHeStopsAndAFarActorMoves) {
    using namespace Fast::Oot3d;
    InteractiveGrassSettings settings;
    settings.InteractionFieldResolution = 32;
    settings.InteractionFieldRadius = 160.0F;
    settings.CollisionPush = 1.0F;
    settings.MaximumBend = 2.0F;
    settings.RecoverySeconds = 10.0F;
    GrassInteractionField field;
    std::array<GrassInteractor, 2> actors;
    auto& link = actors[0];
    link.StableId = 1U;
    link.PreviousPosition = {-40.0F, 0.0F, 0.0F};
    link.Position = {40.0F, 0.0F, 0.0F};
    link.Velocity = {100.0F, 0.0F, 0.0F};
    link.Radius = 30.0F;
    link.FrameId = 1U;
    auto& kokiri = actors[1];
    kokiri.Type = GrassInteractor::Kind::SceneActor;
    kokiri.StableId = 2U;
    kokiri.PreviousPosition = kokiri.Position = {2000.0F, 0.0F, 2000.0F};
    kokiri.Radius = 20.0F;
    kokiri.FrameId = 1U;
    field.UpdateActors(1.0F / 60.0F, settings, actors);
    const auto before = field.Sample(5.0F, 15.0F, 0.0F, 5.0F);
    EXPECT_GT(std::hypot(before[0], before[1]), 0.0F);

    // Link stops; the far actor is now the fastest. The trail around Link is
    // what the camera sees, so the field must stay on him.
    link.PreviousPosition = link.Position;
    link.Velocity = {};
    link.FrameId = 2U;
    kokiri.PreviousPosition = kokiri.Position;
    kokiri.Position[0] += 2.0F;
    kokiri.Velocity = {120.0F, 0.0F, 0.0F};
    kokiri.FrameId = 2U;
    field.UpdateActors(0.0F, settings, actors);
    const auto after = field.Sample(5.0F, 15.0F, 0.0F, 5.0F);
    EXPECT_NEAR(after[0], before[0], 1.0e-5F);
    EXPECT_NEAR(after[1], before[1], 1.0e-5F);
}

TEST(Oot3dHiZ, BuildsCompleteOddSizedPyramid) {
    const auto layout = Fast::Oot3d::BuildHiZPyramidLayout(5U, 3U);
    ASSERT_EQ(layout.Mips.size(), 3U);
    EXPECT_EQ(layout.Mips[0].Width, 5U);
    EXPECT_EQ(layout.Mips[0].Height, 3U);
    EXPECT_EQ(layout.Mips[1].Width, 2U);
    EXPECT_EQ(layout.Mips[1].Height, 1U);
    EXPECT_EQ(layout.Mips[2].Width, 1U);
    EXPECT_EQ(layout.Mips[2].Height, 1U);
    EXPECT_EQ(layout.TexelCount, 18U);
    EXPECT_EQ(layout.R32ByteSize(), 72U);
}

TEST(Oot3dHiZ, LinearizesNormalReversedAndWBufferDepth) {
    using Fast::Oot3d::DepthConvention;
    constexpr float nearPlane = 1.0F;
    constexpr float farPlane = 101.0F;
    EXPECT_NEAR(Fast::Oot3d::LinearizeDepth(
                    0.0F, nearPlane, farPlane, DepthConvention::Perspective),
                nearPlane, 1.0e-5F);
    EXPECT_NEAR(Fast::Oot3d::LinearizeDepth(
                    1.0F, nearPlane, farPlane, DepthConvention::Perspective),
                farPlane, 1.0e-3F);
    EXPECT_NEAR(Fast::Oot3d::LinearizeDepth(
                    1.0F, nearPlane, farPlane,
                    DepthConvention::ReversedPerspective),
                nearPlane, 1.0e-5F);
    EXPECT_NEAR(Fast::Oot3d::LinearizeDepth(
                    0.0F, nearPlane, farPlane,
                    DepthConvention::ReversedPerspective),
                farPlane, 1.0e-3F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::LinearizeDepth(
                        0.25F, nearPlane, farPlane,
                        DepthConvention::WBuffer),
                    26.0F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::LinearizeDepth(
                        0.25F, nearPlane, farPlane,
                        DepthConvention::ReversedWBuffer),
                    76.0F);
    EXPECT_FLOAT_EQ(Fast::Oot3d::ReduceHiZClosest(8.0F, 3.0F, 7.0F, 4.0F),
                    3.0F);
}

TEST(Oot3dHiZ, EmitsBoundedComputeReductionShader) {
    const auto shader = Fast::Oot3d::BuildHiZReductionComputeShader();
    EXPECT_NE(shader.find("local_size_x = 8"), std::string::npos);
    EXPECT_NE(shader.find("source_is_raw"), std::string::npos);
    EXPECT_NE(shader.find("binding = 2) uniform sampler"), std::string::npos);
    EXPECT_NE(shader.find("texelFetch"), std::string::npos);
    EXPECT_NE(shader.find("imageStore"), std::string::npos);
    EXPECT_NE(shader.find("greaterThanEqual"), std::string::npos);
#ifdef OOT3D_FOUNDATION_HAS_SHADERC
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                 shaderc_env_version_vulkan_1_2);
    const auto spirv = compiler.CompileGlslToSpv(
        shader, shaderc_compute_shader, "oot3d_hiz_reduce.comp", options);
    EXPECT_EQ(spirv.GetCompilationStatus(),
              shaderc_compilation_status_success)
        << spirv.GetErrorMessage();
#endif
}


TEST(Oot3dUpscaler, ResolvesNriQualityContractDeterministically) {
    using namespace Fast::Oot3d;
    const auto contract = ResolveUpscalerContract(
        UpscalerProvider::Nis, UpscalerQuality::Quality, 1920U, 1080U);
    EXPECT_EQ(contract.Output, (UpscalerExtent{1920U, 1080U}));
    EXPECT_EQ(contract.Render, (UpscalerExtent{1280U, 720U}));
    EXPECT_FLOAT_EQ(contract.ScalingFactor, 1.5F);
    EXPECT_EQ(contract.MinimumJitterPhases, 18U);
    EXPECT_FALSE(contract.Temporal);
}

TEST(Oot3dUpscaler, SettingsGateProviderAndApplyRenderScale) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings = GraphicsSettingsService::Preset(
        GraphicsPreset::Custom);
    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.Upscaler = UpscalerProvider::Nis;
    settings.UpscalerMode = UpscalerQuality::Balanced;
    GraphicsCapabilities capabilities;
    auto unavailable = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(unavailable.Value.AntiAliasing, AntiAliasingMode::Off);

    capabilities.Set(GraphicsCapability::NriNisUpscaler, true);
    auto available = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(available.Value.AntiAliasing, AntiAliasingMode::Upscaler);
    EXPECT_FLOAT_EQ(available.Value.InternalResolutionScale,
                    settings.InternalResolutionScale);
}

TEST(Oot3dUpscaler, FsrRequiresProviderAndAllTemporalGuides) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings = GraphicsSettingsService::Preset(
        GraphicsPreset::Custom);
    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.Upscaler = UpscalerProvider::Fsr;
    GraphicsCapabilities capabilities;
    capabilities.Set(GraphicsCapability::NriFsrUpscaler, true);
    auto missingGuides = GraphicsSettingsService::Validate(
        settings, capabilities);
    EXPECT_EQ(missingGuides.Value.AntiAliasing, AntiAliasingMode::Off);

    capabilities.Set(GraphicsCapability::MotionVectors, true);
    capabilities.Set(GraphicsCapability::TemporalHistory, true);
    capabilities.Set(GraphicsCapability::ValidViewMetadata, true);
    const auto ready = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(ready.Value.AntiAliasing, AntiAliasingMode::Upscaler);
    EXPECT_TRUE(IsTemporalUpscaler(ready.Value.Upscaler));
}

TEST(Oot3dUpscaler, DlssUsesAnIndependentTemporalCapability) {
    using namespace Fast::Oot3d;
    GraphicsSettings settings = GraphicsSettingsService::Preset(
        GraphicsPreset::Custom);
    settings.AntiAliasing = AntiAliasingMode::Upscaler;
    settings.Upscaler = UpscalerProvider::Dlss;
    GraphicsCapabilities capabilities;
    capabilities.Set(GraphicsCapability::NriFsrUpscaler, true);
    capabilities.Set(GraphicsCapability::MotionVectors, true);
    capabilities.Set(GraphicsCapability::TemporalHistory, true);
    capabilities.Set(GraphicsCapability::ValidViewMetadata, true);
    EXPECT_EQ(GraphicsSettingsService::Validate(settings, capabilities)
                  .Value.AntiAliasing,
              AntiAliasingMode::Off);

    capabilities.Set(GraphicsCapability::NriDlssUpscaler, true);
    const auto ready = GraphicsSettingsService::Validate(settings, capabilities);
    EXPECT_EQ(ready.Value.AntiAliasing, AntiAliasingMode::Upscaler);
    EXPECT_EQ(ready.Value.Upscaler, UpscalerProvider::Dlss);
}

TEST(Oot3dVulkanAdapter, ResolvesSharedAndSeparateQueueTopologies) {
    using Fast::Oot3d::ResolveVulkanQueueTopology;

    const auto dualQueue = ResolveVulkanQueueTopology(3U, 3U, 2U);
    EXPECT_TRUE(dualQueue.SharedFamily);
    EXPECT_EQ(dualQueue.RequestedGraphicsQueueCount, 2U);
    EXPECT_EQ(dualQueue.PresentQueueIndex, 1U);
    EXPECT_TRUE(dualQueue.AsynchronousPresent);
    EXPECT_TRUE(dualQueue.NriSwapchainEligible);

    const auto singleQueue = ResolveVulkanQueueTopology(2U, 2U, 1U);
    EXPECT_TRUE(singleQueue.SharedFamily);
    EXPECT_EQ(singleQueue.RequestedGraphicsQueueCount, 1U);
    EXPECT_EQ(singleQueue.PresentQueueIndex, 0U);
    EXPECT_FALSE(singleQueue.AsynchronousPresent);
    EXPECT_TRUE(singleQueue.NriSwapchainEligible);

    const auto separateFamilies =
        ResolveVulkanQueueTopology(1U, 4U, 8U);
    EXPECT_FALSE(separateFamilies.SharedFamily);
    EXPECT_EQ(separateFamilies.RequestedGraphicsQueueCount, 1U);
    EXPECT_EQ(separateFamilies.PresentQueueIndex, 0U);
    EXPECT_TRUE(separateFamilies.AsynchronousPresent);
    EXPECT_FALSE(separateFamilies.NriSwapchainEligible);
}

TEST(Oot3dGpuProfile, AssignsTwoQueriesPerScopeWithoutOverlap) {
    using namespace Fast::Oot3d;
    std::array<bool, kGpuProfileQueriesPerFrame> used{};
    for (size_t index = 0; index < kGpuProfileScopeCount; ++index) {
        const auto range =
            GpuProfileQueries(static_cast<GpuProfileScope>(index));
        EXPECT_EQ(range.End, range.Begin + 1U);
        ASSERT_LT(range.End, used.size());
        EXPECT_FALSE(used[range.Begin]);
        EXPECT_FALSE(used[range.End]);
        used[range.Begin] = true;
        used[range.End] = true;
    }
    EXPECT_TRUE(std::all_of(
        used.begin(), used.end(), [](bool value) { return value; }));
}

TEST(Oot3dGpuProfile, TracksOneClosedIntervalPerScopeAndFrame) {
    using namespace Fast::Oot3d;
    GpuProfileFramePlan plan;

    EXPECT_TRUE(plan.Begin(GpuProfileScope::Frame));
    EXPECT_TRUE(plan.Open(GpuProfileScope::Frame));
    EXPECT_FALSE(plan.Begin(GpuProfileScope::Frame));

    EXPECT_TRUE(plan.Begin(GpuProfileScope::AmbientOcclusion));
    EXPECT_TRUE(plan.End(GpuProfileScope::AmbientOcclusion));
    EXPECT_TRUE(plan.Written(GpuProfileScope::AmbientOcclusion));
    EXPECT_FALSE(plan.Begin(GpuProfileScope::AmbientOcclusion));
    EXPECT_FALSE(plan.End(GpuProfileScope::AmbientOcclusion));

    EXPECT_TRUE(plan.End(GpuProfileScope::Frame));
    EXPECT_TRUE(plan.Written(GpuProfileScope::Frame));
    plan.Reset();
    EXPECT_FALSE(plan.Open(GpuProfileScope::Frame));
    EXPECT_FALSE(plan.Written(GpuProfileScope::Frame));
    EXPECT_TRUE(plan.Begin(GpuProfileScope::AmbientOcclusion));
}

TEST(Oot3dRendererValidation, SeparatesThreadSafeApiCounters) {
    using namespace Fast::Oot3d;
    RendererValidationTelemetry telemetry;
    telemetry.SetEnabled(RendererValidationSource::Vulkan, true);
    telemetry.SetEnabled(RendererValidationSource::Nri, true);
    telemetry.Record(
        RendererValidationSource::Vulkan,
        RendererValidationSeverity::Warning);
    telemetry.Record(
        RendererValidationSource::Vulkan,
        RendererValidationSeverity::Error);
    telemetry.Record(
        RendererValidationSource::Nri,
        RendererValidationSeverity::Info);
    telemetry.Record(
        RendererValidationSource::Nri,
        RendererValidationSeverity::Error);

    const auto snapshot = telemetry.Snapshot();
    EXPECT_TRUE(snapshot.VulkanEnabled);
    EXPECT_TRUE(snapshot.NriEnabled);
    EXPECT_EQ(snapshot.VulkanWarningCount, 1U);
    EXPECT_EQ(snapshot.VulkanErrorCount, 1U);
    EXPECT_EQ(snapshot.NriInfoCount, 1U);
    EXPECT_EQ(snapshot.NriErrorCount, 1U);
    EXPECT_EQ(snapshot.VulkanInfoCount, 0U);
    EXPECT_EQ(snapshot.NriWarningCount, 0U);

    telemetry.Reset();
    const auto reset = telemetry.Snapshot();
    EXPECT_FALSE(reset.VulkanEnabled);
    EXPECT_FALSE(reset.NriEnabled);
    EXPECT_EQ(reset.VulkanWarningCount, 0U);
    EXPECT_EQ(reset.NriErrorCount, 0U);
}

TEST(Oot3dVulkanAdapter, PrefersACompleteSharedQueueFamily) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanQueueFamilyCandidate> candidates = {
        { 0U, 2U, true, false },
        { 1U, 1U, false, true },
        { 2U, 4U, true, true },
    };

    const auto selected = SelectVulkanQueueFamilies(candidates);
    ASSERT_TRUE(selected.Complete());
    EXPECT_EQ(*selected.GraphicsFamily, 2U);
    EXPECT_EQ(*selected.PresentFamily, 2U);
}

TEST(Oot3dVulkanAdapter, FallsBackToSeparateQueueFamilies) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanQueueFamilyCandidate> candidates = {
        { 0U, 0U, true, true },
        { 3U, 4U, true, false },
        { 6U, 1U, false, true },
    };

    const auto selected = SelectVulkanQueueFamilies(candidates);
    ASSERT_TRUE(selected.Complete());
    EXPECT_EQ(*selected.GraphicsFamily, 3U);
    EXPECT_EQ(*selected.PresentFamily, 6U);
}

TEST(Oot3dVulkanAdapter, RejectsAnIncompleteQueueTopology) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanQueueFamilyCandidate> candidates = {
        { 4U, 1U, true, false },
    };

    const auto selected = SelectVulkanQueueFamilies(candidates);
    EXPECT_FALSE(selected.Complete());
    EXPECT_EQ(*selected.GraphicsFamily, 4U);
    EXPECT_FALSE(selected.PresentFamily.has_value());
}

TEST(Oot3dVulkanAdapter, ParsesOnlyCanonicalEnumeratedIndices) {
    using Fast::Oot3d::ParseVulkanAdapterIndex;

    const auto zero = ParseVulkanAdapterIndex("0");
    ASSERT_TRUE(zero.Valid());
    EXPECT_EQ(*zero.EnumerationIndex, 0U);

    const auto maximum = ParseVulkanAdapterIndex("4294967295");
    ASSERT_TRUE(maximum.Valid());
    EXPECT_EQ(*maximum.EnumerationIndex, UINT32_MAX);

    for (const auto invalid :
         { "", "-1", "+1", " 1", "1 ", "1x", "4294967296" }) {
        const auto result = ParseVulkanAdapterIndex(invalid);
        EXPECT_FALSE(result.Valid()) << invalid;
        EXPECT_NE(result.Reason.find("numeric adapter index"),
                  std::string::npos);
    }
}

TEST(Oot3dVulkanAdapter, AutoSelectionPrefersSuitableDiscreteAdapter) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanAdapterCandidate> candidates = {
        { 0U, "integrated", VulkanAdapterClass::Integrated, 16384U,
          true, true, true, true, true },
        { 1U, "unsuitable discrete", VulkanAdapterClass::Discrete,
          32768U, true, true, false, true, true },
        { 2U, "discrete", VulkanAdapterClass::Discrete, 8192U,
          true, true, true, true, true },
    };

    const auto selected = SelectVulkanAdapter(candidates);
    ASSERT_TRUE(selected.CandidatePosition.has_value());
    EXPECT_EQ(*selected.CandidatePosition, 2U);
    EXPECT_FALSE(selected.Explicit);
    EXPECT_TRUE(selected.Reason.empty());
}

TEST(Oot3dVulkanAdapter, ExplicitSelectionIsStrictAndDeterministic) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanAdapterCandidate> candidates = {
        { 3U, "suitable", VulkanAdapterClass::Integrated, 8192U,
          true, true, true, true, true },
        { 7U, "headless", VulkanAdapterClass::Discrete, 16384U,
          true, false, true, false, false },
    };

    const auto suitable = SelectVulkanAdapter(candidates, 3U);
    ASSERT_TRUE(suitable.CandidatePosition.has_value());
    EXPECT_EQ(*suitable.CandidatePosition, 0U);
    EXPECT_TRUE(suitable.Explicit);

    const auto unsuitable = SelectVulkanAdapter(candidates, 7U);
    EXPECT_FALSE(unsuitable.CandidatePosition.has_value());
    EXPECT_NE(unsuitable.Reason.find("not presentation-capable"),
              std::string::npos);

    const auto missing = SelectVulkanAdapter(candidates, 99U);
    EXPECT_FALSE(missing.CandidatePosition.has_value());
    EXPECT_NE(missing.Reason.find("not enumerated"), std::string::npos);
}

// A driver below Vulkan 1.2 is never selected (the renderer would fail at its
// first render pass), and the refusal names the device, its version and the
// remedy instead of a missing entry point.
TEST(Oot3dVulkanAdapter, RequiresVulkan12AndExplainsAnOutdatedDriver) {
    using namespace Fast::Oot3d;
    const std::vector<VulkanAdapterCandidate> candidates = {
        { 0U, "old integrated", VulkanAdapterClass::Integrated, 16384U, true, true, true, true, true, 1U, 1U },
        { 1U, "current discrete", VulkanAdapterClass::Discrete, 8192U, true, true, true, true, true, 1U, 3U },
    };
    const auto selected = SelectVulkanAdapter(candidates);
    ASSERT_TRUE(selected.CandidatePosition.has_value());
    EXPECT_EQ(*selected.CandidatePosition, 1U);

    const std::vector<VulkanAdapterCandidate> outdated(candidates.begin(), candidates.begin() + 1);
    const auto refused = SelectVulkanAdapter(outdated);
    EXPECT_FALSE(refused.CandidatePosition.has_value());
    EXPECT_NE(refused.Reason.find("old integrated"), std::string::npos);
    EXPECT_NE(refused.Reason.find("Vulkan 1.1"), std::string::npos);
    EXPECT_NE(refused.Reason.find("update the graphics driver"), std::string::npos);

    const auto explicitRefusal = SelectVulkanAdapter(outdated, 0U);
    EXPECT_FALSE(explicitRefusal.CandidatePosition.has_value());
    EXPECT_NE(explicitRefusal.Reason.find("Vulkan 1.2 or newer"), std::string::npos);
}

TEST(Oot3dTexturePreviewArtifact,
     EncodesTopDownBgraBmpWithoutChangingSourcePixels) {
    const std::array<uint8_t, 8> rgba{
        1U, 2U, 3U, 4U,
        10U, 20U, 30U, 40U};
    const auto bmp =
        Fast::Oot3d::EncodeTexturePreviewBmp(
            2U, 1U, rgba);
    ASSERT_EQ(bmp.size(), 62U);
    EXPECT_EQ(bmp[0], 'B');
    EXPECT_EQ(bmp[1], 'M');
    EXPECT_EQ(bmp[10], 54U);
    EXPECT_EQ(bmp[18], 2U);
    EXPECT_EQ(bmp[22], 0xFFU);
    EXPECT_EQ(bmp[23], 0xFFU);
    EXPECT_EQ(bmp[24], 0xFFU);
    EXPECT_EQ(bmp[25], 0xFFU);
    EXPECT_EQ(
        (std::array<uint8_t, 8>{
            bmp[54], bmp[55], bmp[56], bmp[57],
            bmp[58], bmp[59], bmp[60], bmp[61]}),
        (std::array<uint8_t, 8>{
            3U, 2U, 1U, 4U,
            30U, 20U, 10U, 40U}));
    EXPECT_TRUE(
        Fast::Oot3d::EncodeTexturePreviewBmp(
            2U, 2U, rgba).empty());
}

// Continuous scene light (docs/TRIAEVUM_ANIMATED_LIGHTING_STRATEGY.md).
Fast::Renderer3ds::PicaSceneDirectionalLights SunAndMoon(float sunElevationDegrees, bool sky,
                                                         std::array<float, 3> sunColor = {1.0F, 0.9F, 0.8F},
                                                         std::array<float, 3> moonColor = {0.3F, 0.3F, 0.5F}) {
    const float radians = sunElevationDegrees * 3.14159265F / 180.0F;
    Fast::Renderer3ds::PicaSceneDirectionalLights lights;
    lights.Count = 2;
    lights.Lights[0] = {10, {std::cos(radians), std::sin(radians), 0.0F}, sunColor, sky};
    lights.Lights[1] = {20, {-std::cos(radians), -std::sin(radians), 0.0F}, moonColor, sky};
    return lights;
}

TEST(PicaSceneLightContinuity, HorizonFadesShadowsAndSkyLightOnly) {
    using namespace Fast::Renderer3ds;
    const auto elevation = [](float degrees) { return std::sin(degrees * 3.14159265F / 180.0F); };
    EXPECT_EQ(PicaSceneShadowHorizon(elevation(-5.0F)), 0.0F);
    EXPECT_EQ(PicaSceneShadowHorizon(elevation(0.5F)), 0.0F);
    EXPECT_GT(PicaSceneShadowHorizon(elevation(4.0F)), 0.0F);
    EXPECT_LT(PicaSceneShadowHorizon(elevation(4.0F)), 1.0F);
    EXPECT_EQ(PicaSceneShadowHorizon(elevation(10.0F)), 1.0F);
    auto lights = SunAndMoon(-10.0F, true);
    const std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    EXPECT_EQ(PicaSceneDirectHorizon(lights.Lights[0], up), 0.0F);
    EXPECT_EQ(PicaSceneDirectHorizon(lights.Lights[1], up), 1.0F);
    lights = SunAndMoon(-10.0F, false);
    // A scene's own light below the horizon keeps its light.
    EXPECT_EQ(PicaSceneDirectHorizon(lights.Lights[0], up), 1.0F);
}

TEST(PicaSceneLightContinuity, MainLightIsTheStrongestAboveTheHorizon) {
    using namespace Fast::Renderer3ds;
    // Dawn: the sun is brighter but still below; the moon keeps the shadow.
    auto lights = SunAndMoon(-12.0F, true, {0.5F, 0.4F, 0.4F}, {0.3F, 0.2F, 0.3F});
    ASSERT_NE(FindPicaSceneMainDirectional(lights), nullptr);
    EXPECT_EQ(FindPicaSceneMainDirectional(lights)->SourceIdentity, 20U);
    EXPECT_EQ(PicaSceneMainShadowStrength(lights, *FindPicaSceneMainDirectional(lights)), 1.0F);
    // A scene light from below, brighter than the one from above, lights but
    // does not cast.
    lights = SunAndMoon(-45.0F, false, {0.9F, 0.9F, 0.9F}, {0.4F, 0.4F, 0.4F});
    EXPECT_EQ(FindPicaSceneMainDirectional(lights)->SourceIdentity, 20U);
    // A published decision wins.
    lights.ShadowCasterIdentity = 10;
    lights.ShadowStrength = 0.25F;
    EXPECT_EQ(FindPicaSceneMainDirectional(lights)->SourceIdentity, 10U);
    EXPECT_EQ(PicaSceneMainShadowStrength(lights, *FindPicaSceneMainDirectional(lights)), 0.25F);
}

TEST(PicaSceneLightContinuity, SteppedSignalMovesInEveryFrame) {
    Fast::Renderer3ds::PicaSteppedSignal signal;
    float native = 100.0F;
    float previous = signal.Update(native, 1.0F, true);
    EXPECT_EQ(previous, 100.0F);
    uint32_t moving = 0;
    for (uint32_t frame = 1; frame <= 140; ++frame) {
        if (frame % 7 == 0) native += 1.0F;
        const float output = signal.Update(native, 1.0F, true);
        EXPECT_LE(std::abs(output - native), 1.0F);
        EXPECT_GE(output, previous);
        if (frame > 14 && output > previous) ++moving;
        previous = output;
    }
    // After the cadence is known, the value moves in every frame.
    EXPECT_EQ(moving, 126U);
    // A flash is taken at once; so is a discontinuity.
    EXPECT_EQ(signal.Update(native + 40.0F, 1.0F, true), native + 40.0F);
    EXPECT_EQ(signal.Update(native + 41.0F, 1.0F, false), native + 41.0F);
}

TEST(PicaSceneLightContinuity, SkyCasterHandsOverAtTheHorizonContinuously) {
    using namespace Fast::Renderer3ds;
    PicaSceneShadowCasterTracker tracker;
    float previousStrength = -1.0F;
    uint64_t previousCaster = 0;
    uint32_t handovers = 0;
    // The sun sets from 30 degrees to -30 in 0.1-degree steps.
    for (int step = 0; step <= 600; ++step) {
        auto lights = SunAndMoon(30.0F - 0.1F * float(step), true);
        tracker.Update(lights, 1.0F / 30.0F, step != 0);
        ASSERT_NE(lights.ShadowCasterIdentity, 0U);
        const auto* caster = FindPicaSceneMainDirectional(lights);
        ASSERT_NE(caster, nullptr);
        if (lights.ShadowStrength > 0.0F) {
            EXPECT_GT(caster->DirectionTowardSource[1], 0.0F) << "a light below the horizon casts at step " << step;
        }
        if (previousStrength >= 0.0F) EXPECT_LE(std::abs(lights.ShadowStrength - previousStrength), 0.05F);
        if (previousCaster != 0 && previousCaster != lights.ShadowCasterIdentity) {
            ++handovers;
            EXPECT_EQ(previousStrength, 0.0F);
            EXPECT_EQ(lights.ShadowStrength, 0.0F);
        }
        previousStrength = lights.ShadowStrength;
        previousCaster = lights.ShadowCasterIdentity;
    }
    EXPECT_EQ(handovers, 1U);
    EXPECT_EQ(previousCaster, 20U);
    EXPECT_EQ(previousStrength, 1.0F);
}

TEST(PicaSceneLightContinuity, FixedCastersHandOverThroughAFade) {
    using namespace Fast::Renderer3ds;
    PicaSceneShadowCasterTracker tracker;
    const auto scene = [](float first, float second) {
        PicaSceneDirectionalLights lights;
        lights.Count = 2;
        lights.Lights[0] = {10, {0.0F, 1.0F, 0.0F}, {first, first, first}, false};
        lights.Lights[1] = {20, {0.6F, 0.8F, 0.0F}, {second, second, second}, false};
        return lights;
    };
    auto lights = scene(0.8F, 0.2F);
    tracker.Update(lights, 1.0F / 30.0F, false);
    EXPECT_EQ(lights.ShadowCasterIdentity, 10U);
    EXPECT_EQ(lights.ShadowStrength, 1.0F);
    // Inside the margin the caster stays.
    lights = scene(0.8F, 0.85F);
    tracker.Update(lights, 1.0F / 30.0F, true);
    EXPECT_EQ(lights.ShadowCasterIdentity, 10U);
    EXPECT_EQ(lights.ShadowStrength, 1.0F);
    // A clearly stronger light takes over: out, change at zero, in.
    float previous = 1.0F;
    uint32_t frames = 0;
    bool changed = false;
    while (frames++ < 30) {
        lights = scene(0.5F, 0.9F);
        tracker.Update(lights, 1.0F / 30.0F, true);
        EXPECT_LE(std::abs(lights.ShadowStrength - previous), 0.17F);
        if (lights.ShadowCasterIdentity == 20U && !changed) {
            changed = true;
            EXPECT_EQ(lights.ShadowStrength, 0.0F);
        }
        previous = lights.ShadowStrength;
    }
    EXPECT_TRUE(changed);
    EXPECT_EQ(lights.ShadowCasterIdentity, 20U);
    EXPECT_EQ(lights.ShadowStrength, 1.0F);
}

TEST(Renderer3dsVirtualShadows, PageCastersFollowTheExactOverlapRule) {
    using namespace Fast::Renderer3ds;
    std::mt19937 random(7);
    std::uniform_real_distribution<float> position(-9000.0F, 9000.0F), large(100.0F, 3000.0F), small(1.0F, 60.0F),
        unit(-1.0F, 1.0F);
    for (int round = 0; round < 40; ++round) {
        const ShadowVector light{unit(random), std::abs(unit(random)) + 0.2F, unit(random)};
        const ShadowVector camera{position(random) * 0.1F, position(random) * 0.01F, position(random) * 0.1F};
        const auto layout = BuildVirtualShadowLayout(camera, light, 4096, 6550);
        ASSERT_TRUE(layout.Valid());
        std::vector<VirtualShadowCaster> casters(60);
        for (size_t index = 0; index < casters.size(); ++index) {
            const float half = index % 3 == 0 ? large(random) : small(random);
            const float spread = index % 2 == 0 ? 1.0F : 0.05F;
            const ShadowVector center{camera[0] + position(random) * spread, position(random) * 0.02F,
                                      camera[2] + position(random) * spread};
            casters[index].WorldBounds = {{center[0] - half, center[1] - half, center[2] - half},
                                          {center[0] + half, center[1] + half, center[2] + half}, true};
            casters[index].ContentVersion = index + 1;
            casters[index].Dynamic = index % 5 == 0;
        }
        // Bounds that could not be proved reach every page.
        if (round % 2 == 0) casters[7].WorldBounds.Available = false;
        std::vector<ShadowBounds> bounds;
        for (const auto& caster : casters)
            bounds.push_back(TransformShadowBounds(caster.WorldBounds, layout.WorldToLight));
        const auto overlapping = [&](const VirtualShadowPageKey& key) {
            const float size = layout.Levels[key.Level].PageWorldSize;
            const float guard = float(kVirtualShadowPageBorder) * size / kVirtualShadowPageTexels;
            const float x = float(key.X) * size, y = float(key.Y) * size;
            std::array<std::vector<uint32_t>, 2> result;
            for (uint32_t index = 0; index < casters.size(); ++index) {
                const auto& box = bounds[index];
                if (!box.Available || (box.Maximum[0] >= x - guard && box.Minimum[0] <= x + size + guard &&
                                       box.Maximum[1] >= y - guard && box.Minimum[1] <= y + size + guard))
                    result[casters[index].Dynamic ? 1 : 0].push_back(index);
            }
            return result;
        };
        VirtualShadowMapCache cache;
        cache.Prepare(layout, casters, 1);
        // The atlas holds every page: each mapped page is drawn with exactly
        // its overlapping casters, in caster order.
        ASSERT_EQ(cache.Statistics().Unmapped, 0U);
        EXPECT_EQ(cache.Updates().size(), cache.Statistics().Resident);
        for (const auto& page : cache.Updates()) {
            const auto expected = overlapping(page.Key);
            const auto statics = cache.StaticCasters(page), dynamics = cache.DynamicCasters(page);
            EXPECT_EQ(std::vector<uint32_t>(statics.begin(), statics.end()), expected[0]);
            EXPECT_EQ(std::vector<uint32_t>(dynamics.begin(), dynamics.end()), expected[1]);
        }
        size_t mapped = 0;
        for (uint32_t level = 0; level < layout.LevelCount; ++level)
            for (uint32_t y = 0; y < kVirtualShadowGridPages; ++y)
                for (uint32_t x = 0; x < kVirtualShadowGridPages; ++x) {
                    const auto expected = overlapping(
                        {layout.Levels[level].OriginX + int32_t(x), layout.Levels[level].OriginY + int32_t(y), level});
                    const uint32_t entry = cache.Table()[(level * kVirtualShadowGridPages + y) * kVirtualShadowGridPages + x];
                    EXPECT_EQ(entry == kVirtualShadowEmptyPage, expected[0].empty() && expected[1].empty());
                    mapped += entry != kVirtualShadowEmptyPage ? 1U : 0U;
                }
        EXPECT_EQ(mapped, cache.Statistics().Resident);
    }
}

TEST(Renderer3dsVirtualShadows, ConstructsStaticPagesWithinBudgetBeforeDisplay) {
    using namespace Fast::Renderer3ds;
    const auto layout = BuildVirtualShadowLayout({0.25F, 0.25F, 0}, {0, 0, 1}, 4096, 65536);
    const std::array<VirtualShadowCaster, 2> casters{{
        {{{-3000, -3000, -10}, {3000, 3000, 10}, true}, 1},
        {{{20, 20, 0}, {40, 40, 80}, true}, 2, true}}};
    VirtualShadowMapCache reference;
    reference.Prepare(layout, casters, 1);
    const uint32_t total = reference.Statistics().Resident;
    ASSERT_GT(total, 16U);

    VirtualShadowMapCache cache;
    const VirtualShadowConstruction construction{16};
    uint32_t built = 0;
    uint64_t frame = 1;
    for (; frame < 100; ++frame) {
        cache.Prepare(layout, casters, frame, &construction);
        EXPECT_LE(cache.Updates().size(), 16U);
        for (const auto& page : cache.Updates()) {
            EXPECT_TRUE(page.RefreshStatic);
            EXPECT_EQ(page.DynamicCasterCount, 0U);
            EXPECT_FALSE(cache.StaticCasters(page).empty());
        }
        built += static_cast<uint32_t>(cache.Updates().size());
        EXPECT_EQ(cache.Statistics().Pending, total - built);
        const bool complete = cache.Statistics().Pending == 0;
        cache.Commit();
        if (complete) break;
    }
    EXPECT_EQ(built, total);
    EXPECT_EQ(frame, (total + 15U) / 16U);
    // A pass that draws nothing more: the construction is retained.
    cache.Prepare(layout, casters, ++frame, &construction);
    EXPECT_TRUE(cache.Updates().empty());
    EXPECT_EQ(cache.Statistics().Pending, 0U);
    // The construction composed its pages from static depth: its first
    // display composes only the pages the dynamic caster reaches, with the
    // same residency as a layout built at once.
    cache.Prepare(layout, casters, ++frame);
    EXPECT_EQ(cache.Statistics().StaticUpdated, 0U);
    EXPECT_EQ(cache.Statistics().Resident, total);
    ASSERT_FALSE(cache.Updates().empty());
    EXPECT_LT(cache.Updates().size(), total);
    for (const auto& page : cache.Updates()) {
        EXPECT_FALSE(page.RefreshStatic);
        EXPECT_EQ(page.DynamicCasterCount, 1U);
    }
    for (size_t entry = 0; entry < cache.Table().size(); ++entry)
        EXPECT_EQ(cache.Table()[entry] == 0U, reference.Table()[entry] == 0U);
    cache.Commit();
    cache.Prepare(layout, casters, ++frame);
    EXPECT_TRUE(cache.Updates().empty());
    // Another atlas region: composed again, static depth kept.
    cache.InvalidateComposition();
    cache.Prepare(layout, casters, ++frame);
    EXPECT_EQ(cache.Updates().size(), total);
    EXPECT_EQ(cache.Statistics().StaticUpdated, 0U);
}

namespace {
// Drives the keyframe scheduler with a light that turns about the Z axis and
// a construction of `pages` static pages.
struct KeyframeSimulation {
    Fast::Renderer3ds::VirtualShadowKeyframes Keyframes;
    uint32_t Pages = 220;
    uint32_t Built = 0;
    uint64_t Epoch = 1;
    double Angle = 0.0;
    Fast::Renderer3ds::VirtualShadowKeyframeFrame Frame;
    uint32_t MaximumBudget = 0;

    static Fast::Renderer3ds::ShadowVector Direction(double degrees) {
        const double radians = degrees * std::numbers::pi / 180.0;
        return {float(std::cos(radians)), float(std::sin(radians)), 0.0F};
    }
    static double Degrees(const Fast::Renderer3ds::ShadowVector& direction) {
        return std::atan2(double(direction[1]), double(direction[0])) * 180.0 / std::numbers::pi;
    }
    void Step(double turn) {
        Angle += turn;
        Frame = Keyframes.Begin(Direction(Angle), Epoch, 7, Pages);
        if (Frame.ConstructionStarted || Frame.Cut) Built = 0;
        if (Frame.Construction >= 0) {
            MaximumBudget = std::max(MaximumBudget, Frame.ConstructionBudget);
            Built = std::min(Pages, Built + Frame.ConstructionBudget);
            Frame = Keyframes.EndConstruction(Pages - Built);
        }
    }
    // The direction the displayed shadows correspond to.
    double Displayed() const {
        const double newer = Degrees(Keyframes.Direction(Frame.Newer));
        if (!Frame.OlderDisplayed()) return newer;
        const double older = Degrees(Keyframes.Direction(Frame.Older));
        return older + (newer - older) * double(Frame.NewerWeight);
    }
};
} // namespace

TEST(Renderer3dsVirtualShadowKeyframes, StillLightKeepsOneLayout) {
    KeyframeSimulation simulation;
    simulation.Step(30.0);
    EXPECT_TRUE(simulation.Frame.Cut);
    EXPECT_EQ(simulation.Frame.Newer, 0);
    for (int frame = 0; frame < 100; ++frame) {
        // Float roundoff of a still light is not motion.
        simulation.Step(frame % 2 == 0 ? 1.0e-5 : -1.0e-5);
        EXPECT_FALSE(simulation.Frame.Cut);
        EXPECT_EQ(simulation.Frame.Construction, -1);
        EXPECT_FALSE(simulation.Frame.OlderDisplayed());
        EXPECT_EQ(simulation.Frame.NewerWeight, 1.0F);
        EXPECT_EQ(simulation.Frame.Newer, 0);
    }
}

TEST(Renderer3dsVirtualShadowKeyframes, MovingLightIsFollowedEvenlyWithinTheBudget) {
    KeyframeSimulation simulation;
    simulation.Step(30.0);
    constexpr double speed = 0.0165;
    double previous = simulation.Displayed();
    double smallest = 1.0e9, largest = 0.0, lag = 0.0;
    uint32_t rotations = 0;
    for (int frame = 0; frame < 2000; ++frame) {
        simulation.Step(speed);
        EXPECT_FALSE(simulation.Frame.Cut);
        // Displayed layouts and the construction never share a slot.
        EXPECT_NE(simulation.Frame.Newer, simulation.Frame.Construction);
        if (simulation.Frame.Older >= 0) {
            EXPECT_NE(simulation.Frame.Older, simulation.Frame.Newer);
            EXPECT_NE(simulation.Frame.Older, simulation.Frame.Construction);
        }
        rotations += simulation.Frame.Rotated ? 1U : 0U;
        if (simulation.Frame.Rotated)
            EXPECT_EQ(simulation.Frame.NewerWeight, 0.0F);
        const double displayed = simulation.Displayed();
        if (frame >= 200) {
            smallest = std::min(smallest, displayed - previous);
            largest = std::max(largest, displayed - previous);
            lag = std::max(lag, simulation.Angle - displayed);
        }
        previous = displayed;
    }
    EXPECT_GT(rotations, 50U);
    // The displayed shadow moves in every frame, at the speed of the light.
    EXPECT_NEAR(smallest, speed, 1.0e-4);
    EXPECT_NEAR(largest, speed, 1.0e-4);
    EXPECT_LT(lag, 1.0);
    EXPECT_EQ(simulation.MaximumBudget, 16U);
}

TEST(Renderer3dsVirtualShadowKeyframes, FastLightRaisesTheBudgetAndStaysContinuous) {
    KeyframeSimulation simulation;
    simulation.Step(30.0);
    // Twice the speed of the night clock: the spacing law needs more pages.
    constexpr double speed = 0.08;
    double previous = simulation.Displayed();
    double smallest = 1.0e9, largest = 0.0;
    for (int frame = 0; frame < 1000; ++frame) {
        simulation.Step(speed);
        EXPECT_FALSE(simulation.Frame.Cut);
        const double displayed = simulation.Displayed();
        if (frame >= 200) {
            smallest = std::min(smallest, displayed - previous);
            largest = std::max(largest, displayed - previous);
        }
        previous = displayed;
    }
    EXPECT_GT(simulation.MaximumBudget, 16U);
    EXPECT_LE(simulation.MaximumBudget, 64U);
    EXPECT_GT(smallest, 0.9 * speed);
    EXPECT_LT(largest, 1.1 * speed);
}

TEST(Renderer3dsVirtualShadowKeyframes, LightThatStopsSettlesOnItsDirection) {
    KeyframeSimulation simulation;
    simulation.Step(30.0);
    for (int frame = 0; frame < 300; ++frame) simulation.Step(0.0165);
    double previous = simulation.Displayed();
    for (int frame = 0; frame < 300; ++frame) {
        simulation.Step(0.0);
        EXPECT_FALSE(simulation.Frame.Cut);
        EXPECT_GE(simulation.Displayed(), previous - 1.0e-9);
        previous = simulation.Displayed();
    }
    EXPECT_EQ(simulation.Frame.Construction, -1);
    EXPECT_FALSE(simulation.Frame.OlderDisplayed());
    EXPECT_NEAR(simulation.Displayed(), simulation.Angle, 0.002);
}

TEST(Renderer3dsVirtualShadowKeyframes, DiscontinuitiesRebuildOneLayout) {
    KeyframeSimulation simulation;
    simulation.Step(30.0);
    for (int frame = 0; frame < 100; ++frame) simulation.Step(0.0165);
    ASSERT_FALSE(simulation.Frame.Cut);
    // A new continuity epoch.
    ++simulation.Epoch;
    simulation.Step(0.0165);
    EXPECT_TRUE(simulation.Frame.Cut);
    EXPECT_EQ(simulation.Frame.Newer, 0);
    EXPECT_EQ(simulation.Frame.Construction, -1);
    EXPECT_FALSE(simulation.Frame.OlderDisplayed());
    EXPECT_NEAR(simulation.Displayed(), simulation.Angle, 1.0e-4);
    for (int frame = 0; frame < 100; ++frame) simulation.Step(0.0165);
    // A turn above the cut angle in one frame.
    simulation.Step(20.0);
    EXPECT_TRUE(simulation.Frame.Cut);
    EXPECT_NEAR(simulation.Displayed(), simulation.Angle, 1.0e-4);
    // Reset: the next frame is a discontinuity.
    simulation.Step(0.0);
    EXPECT_FALSE(simulation.Frame.Cut);
    simulation.Keyframes.Reset();
    simulation.Step(0.0);
    EXPECT_TRUE(simulation.Frame.Cut);
}

TEST(Renderer3dsVirtualShadowKeyframes, WithoutAnAtlasForThreeLayoutsTheLayoutStepsWithTheLight) {
    Fast::Renderer3ds::VirtualShadowKeyframeSettings settings;
    settings.Keyframed = false;
    KeyframeSimulation simulation{Fast::Renderer3ds::VirtualShadowKeyframes(settings)};
    simulation.Step(30.0);
    uint32_t cuts = 0;
    for (int frame = 0; frame < 100; ++frame) {
        simulation.Step(0.02);
        cuts += simulation.Frame.Cut ? 1U : 0U;
        EXPECT_EQ(simulation.Frame.Construction, -1);
        EXPECT_FALSE(simulation.Frame.OlderDisplayed());
        EXPECT_EQ(simulation.Frame.Newer, 0);
        EXPECT_LT(simulation.Angle - simulation.Displayed(), 0.25);
    }
    // 2 degrees in steps of 0.25.
    EXPECT_GE(cuts, 7U);
    EXPECT_LE(cuts, 8U);
}

TEST(Oot3dDirectionalShadowReceiver, PacksTheOlderLayoutOnlyWhileItDissolves) {
    using namespace Fast::Oot3d;
    const auto identity = IdentityDirectionalShadowMatrix();
    PicaDirectionalShadowHistoryState history;
    history.ProducedFrameId = 1U;
    history.Virtual = Fast::Renderer3ds::BuildVirtualShadowLayout({}, {0, 1, 0}, 2048, 6550);
    history.PageTable[5] = 9U;
    history.Strength = 0.75F;
    auto uniforms = PackPicaDirectionalShadowReceiverUniforms(identity, history);
    EXPECT_EQ(uniforms.Extent[3], kPicaDirectionalShadowBoundFlag);
    EXPECT_EQ(uniforms.Extent[0], 2048U);
    EXPECT_EQ(uniforms.ShadowState[1], 1.0F / 2048.0F);
    EXPECT_EQ(uniforms.Dissolve, (std::array<float, 4>{1.0F, 1.0F / 2048.0F, 0.0F, 0.0F}));
    EXPECT_EQ(uniforms.Layouts[0].PageTable[5], 9U);
    EXPECT_EQ(uniforms.UsedBytes(), sizeof(uniforms) - sizeof(PicaDirectionalShadowReceiverLayout));
    EXPECT_EQ(uniforms.Layouts[1].CameraLight[3], 0.0F);

    // A dissolve: both layouts, each in its region of an atlas three regions wide.
    auto older = std::make_shared<PicaDirectionalShadowOlderLayout>();
    older->Virtual = Fast::Renderer3ds::BuildVirtualShadowLayout({}, {0.01F, 1, 0}, 2048, 6550);
    older->PageTable[6] = 4U;
    older->Region = 2U;
    history.Older = older;
    history.Region = 1U;
    history.AtlasRegions = 3U;
    history.NewerWeight = 0.25F;
    uniforms = PackPicaDirectionalShadowReceiverUniforms(identity, history);
    EXPECT_EQ(uniforms.Extent[3], kPicaDirectionalShadowBoundFlag | kPicaDirectionalShadowDissolveFlag);
    EXPECT_EQ(uniforms.ShadowState[1], 1.0F / 6144.0F);
    EXPECT_EQ(uniforms.Dissolve, (std::array<float, 4>{0.25F, 1.0F / 2048.0F, 2048.0F, 4096.0F}));
    EXPECT_EQ(uniforms.Layouts[1].PageTable[6], 4U);
    EXPECT_EQ(uniforms.Layouts[1].ViewToLight, older->Virtual.WorldToLight);
    EXPECT_EQ(uniforms.UsedBytes(), sizeof(uniforms));
    // The dissolve has reached the newest layout: one layout again.
    history.NewerWeight = 1.0F;
    uniforms = PackPicaDirectionalShadowReceiverUniforms(identity, history);
    EXPECT_EQ(uniforms.Extent[3], kPicaDirectionalShadowBoundFlag);
    EXPECT_EQ(uniforms.Dissolve[0], 1.0F);
    EXPECT_EQ(uniforms.Dissolve[2], 2048.0F);
}

} // namespace

// A model that turns round between two simulated frames: blending the two
// transforms per element flattens it (the rotation rows shrink towards zero);
// the flip-aware blend keeps it rigid and turned half way, with exact ends.
TEST(PicaRigidInterpolation, TurningRoundStaysRigidInsteadOfFlattening) {
    using namespace Fast::Renderer3ds;
    const PicaAffineRows facing{{{1.0F, 0.0F, 0.0F, 10.0F}, {0.0F, 1.0F, 0.0F, 2.0F}, {0.0F, 0.0F, 1.0F, -4.0F}}};
    // Turned 180 degrees about Y, a little further away.
    const PicaAffineRows turned{{{-1.0F, 0.0F, 0.0F, 12.0F}, {0.0F, 1.0F, 0.0F, 2.0F}, {0.0F, 0.0F, -1.0F, -6.0F}}};
    const auto half = InterpolatePicaRigidRows(facing, turned, 0.5F);
    ASSERT_TRUE(half.has_value());
    for (size_t row = 0; row < 3U; ++row) {
        const float length = std::sqrt((*half)[row][0] * (*half)[row][0] + (*half)[row][1] * (*half)[row][1] +
                                        (*half)[row][2] * (*half)[row][2]);
        EXPECT_NEAR(length, 1.0F, 1.0e-4F) << "row " << row;
    }
    // A quarter turn about Y: the X row points along +Z or -Z, never shrunk.
    EXPECT_NEAR(std::abs((*half)[0][2]), 1.0F, 1.0e-4F);
    EXPECT_NEAR((*half)[0][3], 11.0F, 1.0e-4F);
    EXPECT_NEAR((*half)[2][3], -5.0F, 1.0e-4F);
    const auto start = InterpolatePicaRigidRows(facing, turned, 0.0F);
    const auto end = InterpolatePicaRigidRows(facing, turned, 1.0F);
    ASSERT_TRUE(start.has_value() && end.has_value());
    for (size_t row = 0; row < 3U; ++row) {
        for (size_t column = 0; column < 4U; ++column) {
            EXPECT_NEAR((*start)[row][column], facing[row][column], 1.0e-5F);
            EXPECT_NEAR((*end)[row][column], turned[row][column], 1.0e-5F);
        }
    }
    // Scaled rows keep their scale.
    const PicaAffineRows scaled{{{2.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 2.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 2.0F, 0.0F}}};
    const auto grown = InterpolatePicaRigidRows(facing, scaled, 0.5F);
    ASSERT_TRUE(grown.has_value());
    EXPECT_NEAR((*grown)[1][1], 1.5F, 1.0e-4F);
}

TEST(PicaRigidInterpolation, LeavesNonRigidUniformsToPerElementBlending) {
    using namespace Fast::Renderer3ds;
    const PicaAffineRows rigid{{{1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    const PicaAffineRows projection{{{1.8F, 0.0F, 0.0F, 0.0F}, {0.0F, 2.4F, 0.3F, 0.0F}, {0.0F, 0.0F, -1.0F, -0.2F}}};
    const PicaAffineRows mirror{{{-1.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F, 0.0F}}};
    const PicaAffineRows colours{{{0.5F, 0.5F, 0.5F, 1.0F}, {0.2F, 0.2F, 0.2F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F}}};
    EXPECT_FALSE(InterpolatePicaRigidRows(rigid, projection, 0.5F).has_value());
    EXPECT_FALSE(InterpolatePicaRigidRows(rigid, mirror, 0.5F).has_value());
    EXPECT_FALSE(InterpolatePicaRigidRows(colours, colours, 0.5F).has_value());
}
