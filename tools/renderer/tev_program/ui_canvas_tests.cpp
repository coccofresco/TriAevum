#include "fast/renderer3ds/pica_ui_canvas.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;
void Near(float a, float b) {
    if (std::abs(a - b) > 0.002F) throw std::runtime_error("UI canvas mismatch");
}

int main() {
    // Physical top target: doubled vertical samples, rotated clockwise at scanout.
    for (const auto output : std::array<std::array<uint32_t, 2>, 5>{
             {{400, 240}, {1280, 720}, {1024, 768}, {3440, 1440}, {720, 1280}}}) {
        for (const float resolution : {0.5F, 1.0F, 2.0F}) {
            const uint32_t width = static_cast<uint32_t>(output[1] * 2 * resolution);
            const uint32_t height = static_cast<uint32_t>(output[0] * resolution);
            const auto ui = ResolvePicaRasterCanvas(PicaCompositionDomain::Ui, 480, 400, width, height);
            const auto host = Oot3d::Renderer::FitUiPresentation(output[0], output[1], 400, 240);
            Near(ui.Y / resolution, host.X);
            Near(ui.X / (2 * resolution), host.Y);
            Near(ui.ScaleY * 400 / resolution, host.Width);
            Near(ui.ScaleX * 480 / (2 * resolution), host.Height);
            // Same mapping for native scissor/viewport subregions and host touch.
            const float px = host.X + host.Width * 0.3F;
            const float py = host.Y + host.Height * 0.7F;
            const auto touch = Oot3d::Renderer::MapUiPresentationPoint(
                output[0], output[1], 400, 240, px, py);
            if (!touch.Inside) throw std::runtime_error("touch outside UI canvas");
            Near(touch.X, 120); Near(touch.Y, 168);
            for (auto domain : {PicaCompositionDomain::Scene, PicaCompositionDomain::Unknown}) {
                const auto scene = ResolvePicaRasterCanvas(domain, 480, 400, width, height);
                Near(scene.X, 0); Near(scene.Y, 0);
                Near(scene.ScaleX * 480, static_cast<float>(width));
                Near(scene.ScaleY * 400, static_cast<float>(height));
            }
        }
    }
    std::cout << "UI native/host canvas and touch parity: passed\n";
}
