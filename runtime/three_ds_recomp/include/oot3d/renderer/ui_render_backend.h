#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace Oot3d::Renderer {

struct UiOverlayVertex {
    float X = 0.0F;
    float Y = 0.0F;
    float U = 0.0F;
    float V = 0.0F;
    float Red = 1.0F;
    float Green = 1.0F;
    float Blue = 1.0F;
    float Alpha = 1.0F;
};

struct UiOverlayViewport {
    uint32_t X = 0;
    uint32_t Y = 0;
    uint32_t Width = 1;
    uint32_t Height = 1;
};

class UiRenderBackend {
  public:
    virtual ~UiRenderBackend() = default;

    virtual bool PrepareUiOverlay(const UiOverlayViewport& viewport,
                                  std::string* error = nullptr) = 0;
    virtual bool IsUiFramebufferYInverted() const noexcept = 0;
    virtual uint32_t CreateUiTexture(std::span<const uint8_t> rgba8,
                                     uint16_t width, uint16_t height,
                                     std::string* error = nullptr) = 0;
    virtual void DeleteUiTexture(uint32_t textureId) noexcept = 0;
    virtual bool DrawUiTriangles(std::span<const UiOverlayVertex> vertices,
                                 uint32_t textureId,
                                 std::string* error = nullptr) = 0;
};

} // namespace Oot3d::Renderer
