#pragma once

#include "oot3d/renderer/ui_render_backend.h"
#include "oot3d_n64_ui_renderer.h"

namespace Fast {
class GfxRenderingAPI;
class ShaderProgram;
namespace Oot3d {
class TitleRenderBackend;
}
} // namespace Fast

namespace oot3d::ui {

class Fast3dUiRenderBackend final
    : public Oot3d::Renderer::UiRenderBackend {
  public:
    explicit Fast3dUiRenderBackend(Fast::GfxRenderingAPI& renderingApi) noexcept;

    bool PrepareUiOverlay(const Oot3d::Renderer::UiOverlayViewport& viewport,
                          std::string* error = nullptr) override;
    bool IsUiFramebufferYInverted() const noexcept override;
    std::uint32_t CreateUiTexture(std::span<const std::uint8_t> rgba8,
                                  std::uint16_t width, std::uint16_t height,
                                  std::string* error = nullptr) override;
    void DeleteUiTexture(std::uint32_t textureId) noexcept override;
    bool DrawUiTriangles(
        std::span<const Oot3d::Renderer::UiOverlayVertex> vertices,
        std::uint32_t textureId, std::string* error = nullptr) override;

  private:
    Fast::ShaderProgram* ResolveShader(bool textured, std::string* error);

    Fast::GfxRenderingAPI& rendering_api_;
    Fast::Oot3d::TitleRenderBackend* title_render_backend_ = nullptr;
    Fast::ShaderProgram* solid_shader_ = nullptr;
    Fast::ShaderProgram* textured_shader_ = nullptr;
};

} // namespace oot3d::ui
