#include "oot3d_legacy_ui_adapters.h"

#include "fast/backends/gfx_rendering_api.h"
#include "fast/oot3d/title_render_backend.h"

#include <array>
#include <vector>

namespace oot3d::ui {
namespace {

bool ConfigureUiSampler(Fast::GfxRenderingAPI& renderingApi,
                        std::string* error) {
    Fast::GfxNativeSamplerState sampler;
    sampler.MinFilter = Fast::GfxNativeTextureFilter::Linear;
    sampler.MagFilter = Fast::GfxNativeTextureFilter::Linear;
    sampler.WrapS = Fast::GfxNativeTextureWrap::ClampToEdge;
    sampler.WrapT = Fast::GfxNativeTextureWrap::ClampToEdge;
    if (renderingApi.SetNativeSamplerParameters(0, sampler)) {
        return true;
    }
    if (error != nullptr) {
        *error = "renderer does not support the native UI sampler contract";
    }
    return false;
}

} // namespace

Fast3dUiRenderBackend::Fast3dUiRenderBackend(
    Fast::GfxRenderingAPI& renderingApi) noexcept
    : rendering_api_(renderingApi),
      title_render_backend_(
          dynamic_cast<Fast::Oot3d::TitleRenderBackend*>(&renderingApi)) {}

bool Fast3dUiRenderBackend::PrepareUiOverlay(
    const Oot3d::Renderer::UiOverlayViewport& viewport, std::string* error) {
    if (title_render_backend_ != nullptr &&
        !title_render_backend_->PrepareOverlay(error)) {
        return false;
    }
    rendering_api_.SetViewport(static_cast<int>(viewport.X),
                               static_cast<int>(viewport.Y),
                               static_cast<int>(viewport.Width),
                               static_cast<int>(viewport.Height));
    rendering_api_.SetScissor(static_cast<int>(viewport.X),
                              static_cast<int>(viewport.Y),
                              static_cast<int>(viewport.Width),
                              static_cast<int>(viewport.Height));
    rendering_api_.SetDepthTestAndMask(false, false);
    rendering_api_.SetZmodeDecal(false);
    rendering_api_.SetNativeCullMode(Fast::GfxNativeCullMode::KeepAll);
    rendering_api_.SetUseAlpha(true);
    return true;
}

bool Fast3dUiRenderBackend::IsUiFramebufferYInverted() const noexcept {
    return rendering_api_.GetClipParameters().invertY;
}

std::uint32_t Fast3dUiRenderBackend::CreateUiTexture(
    std::span<const std::uint8_t> rgba8, std::uint16_t width,
    std::uint16_t height, std::string* error) {
    if (width == 0U || height == 0U ||
        rgba8.size() != static_cast<std::size_t>(width) * height * 4U) {
        if (error != nullptr) {
            *error = "UI texture upload payload is invalid";
        }
        return 0U;
    }
    const std::uint32_t textureId = rendering_api_.NewTexture();
    rendering_api_.SelectTexture(0, textureId);
    if (!ConfigureUiSampler(rendering_api_, error)) {
        rendering_api_.DeleteTexture(textureId);
        return 0U;
    }
    rendering_api_.UploadTexture(rgba8.data(), width, height);
    return textureId;
}

void Fast3dUiRenderBackend::DeleteUiTexture(
    std::uint32_t textureId) noexcept {
    if (textureId != 0U) {
        rendering_api_.DeleteTexture(textureId);
    }
}

Fast::ShaderProgram* Fast3dUiRenderBackend::ResolveShader(
    bool textured, std::string* error) {
    Fast::ShaderProgram*& shader = textured ? textured_shader_ : solid_shader_;
    if (shader == nullptr) {
        const std::uint64_t shaderId0 =
            textured ? N64UiTexturedShaderId0() : N64UiSolidShaderId0();
        shader = rendering_api_.LookupShader(shaderId0, N64UiShaderId1());
        if (shader == nullptr) {
            shader = rendering_api_.CreateAndLoadNewShader(
                shaderId0, N64UiShaderId1());
        }
    }
    if (shader == nullptr) {
        if (error != nullptr) {
            *error = "renderer could not create the UI shader";
        }
        return nullptr;
    }
    rendering_api_.LoadShader(shader);
    return shader;
}

bool Fast3dUiRenderBackend::DrawUiTriangles(
    std::span<const Oot3d::Renderer::UiOverlayVertex> vertices,
    std::uint32_t textureId, std::string* error) {
    if (vertices.empty() || vertices.size() % 3U != 0U) {
        if (error != nullptr) {
            *error = "UI draw has an invalid triangle list";
        }
        return false;
    }
    const bool textured = textureId != 0U;
    if (textured) {
        rendering_api_.SelectTexture(0, textureId);
        if (!ConfigureUiSampler(rendering_api_, error)) {
            return false;
        }
    }
    if (ResolveShader(textured, error) == nullptr) {
        return false;
    }
    static constexpr std::array<float, 16> kClipSpaceIdentity{
        1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
    };
    rendering_api_.SetOot3dNativeTransform(kClipSpaceIdentity.data());

    std::vector<float> packed;
    packed.reserve(vertices.size() * (textured ? 10U : 8U));
    for (const auto& vertex : vertices) {
        packed.push_back(vertex.X);
        packed.push_back(vertex.Y);
        packed.push_back(0.0F);
        packed.push_back(1.0F);
        if (textured) {
            packed.push_back(vertex.U);
            packed.push_back(vertex.V);
        }
        packed.push_back(vertex.Red);
        packed.push_back(vertex.Green);
        packed.push_back(vertex.Blue);
        packed.push_back(vertex.Alpha);
    }
    rendering_api_.DrawTriangles(packed.data(), packed.size(),
                                 vertices.size() / 3U);
    return true;
}

} // namespace oot3d::ui
