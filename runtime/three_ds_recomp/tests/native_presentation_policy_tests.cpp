#include "fast/oot3d/native_presentation_policy.h"

#include <iostream>
#include <stdexcept>

int main() {
    using namespace Fast::Oot3d;
    const auto require = [](bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    };
    try {
        GraphicsSettings configured;
        configured.Preset = GraphicsPreset::Custom;
        configured.FrameRate = FrameRateMode::Interpolated3x;
        configured.FovMultiplier = 1.1F;
        configured.AntiAliasing = AntiAliasingMode::Msaa;
        configured.MsaaSamples = 4;
        configured.Grass.Quality = GrassQuality::High;
        configured.Effects.Toon = ToonMode::PicaMaterial;
        configured.Effects.ToonStyle.OutlineEnabled = true;
        configured.Effects.AmbientOcclusion = AmbientOcclusionMode::Cacao;
        configured.Effects.Reflections = ReflectionMode::FidelityFxSssr;
        configured.TexturePacks.Azahar.LoadCustomTextures = true;

        NativePresentationPolicy desktop(false), mobile(true);
        require(!desktop.Active() && !desktop.Required(), "desktop default changed");
        require(desktop.Toggle() && desktop.Active(), "F2 activation");
        require(mobile.Active() && mobile.Required() && !mobile.Toggle(), "mobile policy can be bypassed");
        for (const auto& policy : {desktop, mobile}) {
            auto effective = configured;
            policy.Apply(effective);
            require(effective.Grass.Quality == GrassQuality::Off &&
                        effective.Effects.Toon == ToonMode::Off &&
                        !effective.Effects.ToonStyle.OutlineEnabled &&
                        effective.Effects.AmbientOcclusion == AmbientOcclusionMode::Off &&
                        effective.Effects.Reflections == ReflectionMode::Off, "F2 mask incomplete");
            require(effective.Preset == configured.Preset && effective.FrameRate == configured.FrameRate &&
                        effective.FovMultiplier == configured.FovMultiplier &&
                        effective.AntiAliasing == configured.AntiAliasing && effective.MsaaSamples == 4 &&
                        effective.TexturePacks.Azahar.LoadCustomTextures, "unrelated presentation settings changed");
        }
        require(configured.Grass.Quality == GrassQuality::High, "configured values mutated");
        require(desktop.Toggle() && !desktop.Active(), "F2 restoration");
        auto restored = configured;
        desktop.Apply(restored);
        require(restored.Grass.Quality == GrassQuality::High && restored.Effects.ToonStyle.OutlineEnabled,
                "configured effects not restored");
        NativePresentationPolicy compiled;
        require(compiled.Required() == BuildRequiresNativePresentation(), "build policy not applied");
        std::cout << "Native presentation policy tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
