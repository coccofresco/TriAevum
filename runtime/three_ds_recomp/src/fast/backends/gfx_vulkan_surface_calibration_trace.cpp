#include "gfx_vulkan_pica_internal.h"

#ifdef ENABLE_OOT3D_VULKAN
#include "fast/renderer3ds/pica_surface_debug.h"

#include <iomanip>
#include <sstream>

namespace Fast {
namespace {
using namespace Renderer3ds;

const char* DomainName(PicaCompositionDomain domain) {
    switch (domain) {
    case PicaCompositionDomain::Scene: return "scene";
    case PicaCompositionDomain::Ui: return "ui";
    case PicaCompositionDomain::Screen: return "screen";
    default: return "unknown";
    }
}
const char* PassName(PicaCompositionLayer layer) {
    switch (layer) {
    case PicaCompositionLayer::OpaqueWorld: return "opaque-world";
    case PicaCompositionLayer::TransparentWorld: return "transparent-world";
    case PicaCompositionLayer::Atmosphere: return "atmosphere";
    case PicaCompositionLayer::Ui: return "ui";
    default: return "unknown";
    }
}
const char* ModeName(Oot3d::SurfaceLightingMode mode) {
    switch (mode) {
    case Oot3d::SurfaceLightingMode::Native: return "Native";
    case Oot3d::SurfaceLightingMode::NativePbr: return "NativePBR";
    case Oot3d::SurfaceLightingMode::PerPixel: return "PerPixel";
    case Oot3d::SurfaceLightingMode::PerPixelPbr: return "PerPixelPBR";
    default: return "unknown";
    }
}
const char* KeyName(PicaEnvironmentKeyDirection direction) {
    switch (direction) {
    case PicaEnvironmentKeyDirection::Authored: return "authored";
    case PicaEnvironmentKeyDirection::SceneMain: return "scene-main";
    case PicaEnvironmentKeyDirection::SceneAmbient: return "scene-ambient";
    default: return "unknown";
    }
}
template <class T, size_t N>
void Vector(std::ostream& out, const std::string& name, const std::array<T, N>& values) {
    out << ' ' << name << "=[";
    for (size_t i = 0; i < N; ++i) out << (i ? "," : "") << values[i];
    out << ']';
}
template <class T>
bool Read(std::span<const uint8_t> bytes, size_t offset, T& value) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) return false;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return true;
}
} // namespace

void GfxRenderingAPIVulkan::TraceSurfaceCalibrationDraw(const GfxNativePicaDrawView& draw,
    const Oot3d::PicaShaderPipelineResult& variant,
    const std::optional<Renderer3ds::PicaSurfaceLightingUniforms>& lighting,
    bool candidate, bool exempt, bool authoredAppearance, bool isolated, bool perspective, bool uber) {
    if (!mSurfaceCalibrationTrace.Sampling()) return;
    const auto& hooks = draw.TemporalVertexProgram.Hooks;
    const auto* surface = hooks.SurfaceProgram();
    uint32_t booleans = 0;
    const bool booleansKnown = Read(draw.VertexUniformBytes, Oot3d::kPicaPackedVertexBooleanMaskOffset, booleans);
    const auto floats = draw.VertexUniformBytes.size() >= Oot3d::kPicaPackedVertexFloatOffset
        ? draw.VertexUniformBytes.subspan(Oot3d::kPicaPackedVertexFloatOffset) : std::span<const uint8_t>{};
    const auto native = booleansKnown ? ReadPicaNativeSurfaceLighting(hooks, booleans, floats) : std::nullopt;
    const char* nativeClass = !native ? "unknown" : !native->Enabled ? "unlit"
        : native->DirectSlotMask != 0 ? "direct-lit" : "ambient-only";
    const bool pbr = Oot3d::HasPicaShaderInstrumentationFeature(
        variant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::PbrMaterial);
    const bool perPixel = Oot3d::HasPicaShaderInstrumentationFeature(
        variant.AppliedFeatures, Oot3d::PicaShaderInstrumentationFeature::NativePerPixelLighting);
    const bool memory = mNativePicaCompositionSchedule.Purpose() == Renderer3ds::PicaCompositionPurpose::NativeMemory;
    const auto mode = mFrameGraphicsSettings.Effects.SurfaceLighting;
    const auto* scene = draw.LightBinding.SceneDirectionals.get();
    const uint32_t count = scene ? std::min<uint32_t>(scene->Count, scene->Lights.size()) : 0;
    uint32_t skyMask = 0;
    for (uint32_t i = 0; i < count; ++i) if (scene->Lights[i].SkyLight) skyMask |= 1U << i;
    const bool calibratedUniform = lighting && lighting->BakedReferenceUp[3] != 0;
    // The plain evaluator keeps native appearance for water fragments. Mixed
    // meshes still calibrate their non-water fragments; GGX has its own path.
    const bool wholeWater = lighting && !pbr && lighting->WaterViewTranslation[3] >= 8;
    const bool mixedWater = lighting && !pbr && lighting->WaterViewTranslation[3] > 0 && !wholeWater;
    const bool admitted = calibratedUniform && (pbr || perPixel) && !wholeWater;
    const char* reason = "uniform-not-admitted";
    if (admitted) reason = mixedWater ? "admitted-nonwater-fragments" : "admitted";
    else if (memory) reason = "native-memory";
    else if (mNativeFidelityProfile) reason = "native-fidelity";
    else if (!Oot3d::UsesFullSceneLighting(mode)) reason = "native-mode";
    else if (draw.CompositionDomain != PicaCompositionDomain::Scene) reason = "domain-not-scene";
    else if (!PicaCompositionReceivesSurfaceLighting(draw.Composition.Layer)) reason = "pass-not-world";
    else if (!perspective) reason = "no-perspective";
    else if (draw.FragmentOperationMode == 3) reason = "fragment-operation";
    else if (exempt) reason = "debug-exempt";
    else if (authoredAppearance) reason = "authored-appearance";
    else if (!surface) reason = "no-surface-hooks";
    else if (!native) reason = "invalid-native-uniforms";
    else if (!hooks.TransformProgram()) reason = "no-transform-hooks";
    else if (!lighting) reason = draw.LightBinding.WorldToView ? "uniform-builder-rejected" : "no-view-or-builder-rejected";
    else if (isolated) reason = "isolated-lighting";
    else if (draw.FragmentFeatures.FragmentLightingEnabled) reason = "native-fragment-lighting";
    else if (native->DirectSlotMask) reason = "direct-native-response";
    else if (!count) reason = "no-scene-directionals";
    else if (!skyMask) reason = "no-sky-light";
    else if (!(pbr || perPixel)) reason = "shader-fallback";
    else if (wholeWater) reason = "water-native-appearance";

    std::ostringstream classification;
    classification << "domain=" << DomainName(draw.CompositionDomain) << " pass=" << PassName(draw.Composition.Layer)
        << " replay=" << (memory ? "native-memory" : "presentation") << " native=" << nativeClass
        << " vertexLit=" << (surface && booleansKnown ? int((booleans >> surface->LightingBooleanUniform) & 1U) : -1)
        << " fragmentLit=" << draw.FragmentFeatures.FragmentLightingEnabled
        << " path=" << (pbr ? "PBR-GGX" : perPixel ? "plain" : "native")
        << " candidate=" << candidate << " uniforms=" << lighting.has_value()
        << " calibrationUniform=" << calibratedUniform << " admitted=" << admitted << " reason=" << reason;
    mSurfaceCalibrationTrace.Count(classification.str(), draw.VertexCount);
    if (memory || draw.CompositionDomain != PicaCompositionDomain::Scene ||
        !mSurfaceCalibrationTrace.WouldKeep(draw.VertexCount)) return;

    std::ostringstream out;
    out << std::setprecision(6) << classification.str()
        << " program=" << (uber ? "uber" : "specialized") << " submission=" << draw.SubmissionId
        << " elements=" << draw.VertexCount << " indexed=" << draw.Indexed << " indexBytes=" << draw.IndexBytes.size()
        << " geometry=" << std::hex << draw.GeometryIdentity << " vertexProgram=" << draw.CanonicalVertexProgramId
        << " fragmentProgram=" << draw.CanonicalFragmentProgramId << " sourcePc=" << draw.Composition.SourcePc
        << " nativeValue=" << draw.Composition.NativeValue << " target=" << draw.FramebufferColorPhysicalAddress
        << " targetNamespace=" << draw.RenderTargetNamespace << std::dec
        << " targetSize=" << draw.FramebufferWidth << 'x' << draw.FramebufferHeight
        << " provenance=" << unsigned(draw.Composition.Provenance)
        << " actor=" << draw.Composition.NativeActorGeometry << " actorIdentity=" << draw.Composition.NativeActorIdentity
        << " isolated=" << isolated << " perspective=" << perspective << " blend=" << draw.Blend.Enabled
        << " alphaTest=" << draw.AlphaTestEnabled << " depthWrite=" << draw.DepthWriteEnabled
        << " fog=" << draw.FragmentFeatures.FogEnabled << " viewKnown=" << bool(draw.LightBinding.WorldToView)
        << " waterBypass=" << (wholeWater ? "whole" : mixedWater ? "per-fragment" : "none")
        << " fragmentLightCount=" << unsigned(draw.FragmentFeatures.FragmentLighting.ActiveLightCount)
        << " booleans=" << std::hex << booleans << std::dec << " booleansKnown=" << booleansKnown
        << " sceneCount=" << (scene ? scene->Count : 0) << " skyMask=" << skyMask;
    PicaTevProgram tev{};
    const bool tevKnown = Read(draw.FragmentUniformBytes, Oot3d::kPicaPackedFragmentTevProgramOffset, tev);
    out << " tevKnown=" << tevKnown << " tevColorSources=" << std::hex
        << (tevKnown ? PicaTevColorSourceMask(tev) : 0) << std::dec;
    for (const auto& texture : draw.Textures) {
        const auto hash = ResolveNativeTextureBaseLevelContentHash(texture);
        out << " texture" << unsigned(texture.Slot) << '=';
        if (hash) out << std::hex << *hash << std::dec; else out << "unknown";
        out << ':' << texture.Width << 'x' << texture.Height;
    }
    out << " nativePrimarySource=" << (!native ? "unknown" : !native->Enabled ? "vertex-unlit-output"
        : native->DirectSlotMask ? "native-diffuse-plus-ambient-times-tint" : "native-ambient-times-tint");
    if (surface) {
        out << " diffuseUniform=" << unsigned(surface->DiffuseUniform) << " ambientUniform=" << unsigned(surface->AmbientUniform)
            << " tintInput=" << unsigned(surface->TintInputRegister)
            << " tintEnabled=" << (booleansKnown ? int((booleans >> surface->TintBooleanUniform) & 1U) : -1);
        float scale = 0;
        if (Read(floats, size_t(surface->TintScale.Uniform) * 16 + size_t(surface->TintScale.Component) * 4, scale))
            out << " tintScale=" << scale;
    }
    if (native) {
        out << " nativeDirectSlotMask=" << native->DirectSlotMask;
        Vector(out, "nativeMaterialDiffuse", native->MaterialDiffuse);
        Vector(out, "nativeMaterialAmbient", native->MaterialAmbient);
        Vector(out, "nativeResolvedAmbient", native->ResolvedAmbient);
        for (size_t i = 0; i < native->Lights.size(); ++i) {
            Vector(out, "nativeDirection" + std::to_string(i), native->Lights[i].DirectionEnabled);
            Vector(out, "nativeDiffuse" + std::to_string(i), native->Lights[i].Diffuse);
        }
    }
    std::array<float, 4> fragmentAmbient{};
    if (Read(draw.FragmentUniformBytes, Oot3d::kPicaPackedFragmentLightingGlobalAmbientOffset, fragmentAmbient))
        Vector(out, "fragmentGlobalAmbient", fragmentAmbient);
    const bool profile = mFrameEnvironment.Enabled && !isolated;
    out << " profile=" << profile << " profileScope=" << mFrameEnvironment.ScopeIdentity
        << " preserve=" << mFrameEnvironment.PreserveSceneBrightness << " keyDirection=" << KeyName(mFrameEnvironment.KeyDirection)
        << " keySource=" << (!(perPixel || pbr) ? "native-draw" : profile ? "profile" : skyMask ? "native-sun-moon" : "native-static")
        << " keyColorSource=" << (profile && mFrameEnvironment.KeyDirection == PicaEnvironmentKeyDirection::Authored
            ? "authored" : count ? "scene-main" : "authored-fallback");
    if (scene) {
        out << " sceneEpoch=" << scene->ContinuityEpoch << " shadowCaster=" << scene->ShadowCasterIdentity;
        Vector(out, "worldUp", scene->WorldUp);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& source = scene->Lights[i];
            const auto suffix = std::to_string(i);
            out << " source" << suffix << '=' << source.SourceIdentity << " sourceSky" << suffix << '=' << source.SkyLight;
            Vector(out, "sourceDirection" + suffix, source.DirectionTowardSource);
            Vector(out, "sourceColor" + suffix, source.Color);
        }
    }
    if (lighting) {
        Vector(out, "control", lighting->Control);
        Vector(out, "referenceUp", lighting->BakedReferenceUp);
        Vector(out, "referenceKey", lighting->BakedReferenceKey);
        Vector(out, "resolvedAmbient", lighting->ResolvedAmbient);
        Vector(out, "environmentParameters", lighting->EnvironmentParameters);
        Vector(out, "environmentAmbient", lighting->EnvironmentAmbient);
        for (size_t i = 0; i < lighting->EnvironmentDirections.size(); ++i) {
            Vector(out, "profileDirection" + std::to_string(i), lighting->EnvironmentDirections[i]);
            Vector(out, "profileRadiance" + std::to_string(i), lighting->EnvironmentColors[i]);
        }
        for (size_t i = 0; i < lighting->Control[2] && i < lighting->SceneDirections.size(); ++i) {
            Vector(out, "sceneDirection" + std::to_string(i), lighting->SceneDirections[i]);
            Vector(out, "sceneRadiance" + std::to_string(i), lighting->SceneDirectionalColors[i]);
        }
        Vector(out, "giSources", lighting->GlobalIlluminationSources);
        Vector(out, "rim", lighting->Rim);
    }
    mSurfaceCalibrationTrace.Keep(draw.VertexCount, out.str());
}

void GfxRenderingAPIVulkan::FinishSurfaceCalibrationTrace() {
    if (!mSurfaceCalibrationTrace.Sampling()) return;
    const auto& effects = mFrameGraphicsSettings.Effects;
    std::ostringstream context;
    context << std::setprecision(6) << "mode=" << ModeName(effects.SurfaceLighting)
        << " fidelity=" << mNativeFidelityProfile << " profile=" << mFrameEnvironment.Enabled
        << " profileScope=" << mFrameEnvironment.ScopeIdentity << " preserve=" << mFrameEnvironment.PreserveSceneBrightness
        << " nativeDirectGain=" << mFrameEnvironment.NativeDirectStrength
        << " nativeLocalGain=" << mFrameEnvironment.NativeLocalStrength
        << " nativeAmbientGain=" << mFrameEnvironment.NativeAmbientStrength
        << " ambientIntensity=" << mFrameEnvironment.AmbientIntensity
        << " keyIntensity=" << mFrameEnvironment.Lights[0].Intensity
        << " keyDirection=" << KeyName(mFrameEnvironment.KeyDirection)
        << " ao=" << unsigned(effects.AmbientOcclusion) << " aoStrength=" << effects.AoStrength
        << " gi=" << unsigned(effects.GlobalIllumination.Mode) << " reflections=" << unsigned(effects.Reflections)
        << " grass=" << unsigned(mFrameGraphicsSettings.Grass.Quality) << " fogStrength=" << mFrameEnvironment.FogStrength;
    Vector(context, "keyTint", mFrameEnvironment.Lights[0].Color);
    Vector(context, "ambientColor", mFrameEnvironment.AmbientColor);
    const auto text = mSurfaceCalibrationTrace.FinishFrame(context.str());
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
}
} // namespace Fast
#endif
