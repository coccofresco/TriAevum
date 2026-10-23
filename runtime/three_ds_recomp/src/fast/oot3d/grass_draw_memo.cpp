#include "fast/oot3d/grass_draw_memo.h"
#include "fast/oot3d/grass_surface_extractor.h"
#include "fast/oot3d/pica_uniform_layout.h"
#include "fast/oot3d/directional_shadows.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace Fast::Oot3d {
namespace {
struct Hash {
    uint64_t Value = 14695981039346656037ULL;
    void Bytes(std::span<const uint8_t> b) {
        for (auto x : b)
            Value = (Value ^ x) * 1099511628211ULL;
    }
    template <class T> void Add(T x) {
        Bytes({ reinterpret_cast<const uint8_t*>(&x), sizeof(x) });
    }
    void Part(std::span<const uint8_t> b, size_t offset, size_t count) {
        Add(b.size());
        if (offset <= b.size() && count <= b.size() - offset)
            Bytes(b.subspan(offset, count));
    }
};
using Matrix = std::array<float, 16>;
Matrix Identity() {
    return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
}
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < 4; ++j)
            for (size_t k = 0; k < 4; ++k)
                out[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
    return out;
}
} // namespace
size_t GrassDrawMemoKeyHash::operator()(const GrassDrawMemoKey& k) const noexcept {
    return size_t(k.Geometry ^ k.Content ^ k.Texture ^ k.Material ^ k.Uniforms ^ k.Rules ^ k.Textures ^
                  uint64_t(k.MasksOnly));
}
uint64_t GrassDrawRulesVersion(const InteractiveGrassSettings& s) {
    Hash h;
    h.Add(s.Quality);
    h.Add(s.Rules.size());
    h.Add(s.Masks.size());
    for (const auto& rule : s.Rules)
        h.Add(GrassPlacementRuleVersion(rule, s.Generation));
    for (const auto& mask : s.Masks) {
        h.Add(mask.Mode);
        h.Add(mask.Strength);
        h.Add(GrassPlacementRuleVersion(mask.Coverage, s.Generation));
    }
    return h.Value;
}
std::optional<GrassDrawMemoKey> BuildGrassDrawMemoKey(const Renderer3ds::PicaDrawView& d, uint64_t rules,
                                                      uint64_t textures, bool world, bool texture0, bool perspective,
                                                      bool masksOnly) {
    if (!d.GeometryIdentityAvailable || !d.GeometryIdentity || !d.GeometryContentVersion)
        return {};
    Hash texture, material, uniform;
    texture.Add(d.Textures.size());
    for (const auto& t : d.Textures) {
        if (t.Slot != 0)
            continue;
        if ((!t.NativeContentHashAvailable && !t.NativeBaseLevelContentHashAvailable) || !t.CopyReferences.empty())
            return {};
        texture.Add(t.NativeBaseLevelContentHashAvailable);
        texture.Add(t.NativeBaseLevelContentHashAvailable ? t.NativeBaseLevelContentHash : t.NativeContentHash);
        texture.Add(t.NativeBytes.size());
        texture.Add(t.Width);
        texture.Add(t.Height);
        texture.Add(t.NativeType);
        texture.Add(t.NativeFormat);
        texture.Add(t.NativeWrapS);
        texture.Add(t.NativeWrapT);
        texture.Add(t.MinLinear);
        texture.Add(t.MagLinear);
    }
    material.Add(world);
    material.Add(texture0);
    material.Add(perspective);
    material.Add(d.Composition.NativeActorGeometry);
    material.Add(d.Composition.NativeProjectedShadow);
    material.Add(d.Stencil.Enabled);
    material.Add(d.FragmentOperationMode);
    material.Add(d.ColorWriteMask);
    material.Add(d.DepthTestEnabled);
    material.Add(d.DepthWriteEnabled);
    material.Add(d.DepthCompare);
    material.Add(d.Blend.Enabled);
    material.Add(d.Blend.EquationRgb);
    material.Add(d.Blend.SourceRgb);
    material.Add(d.Blend.DestRgb);
    material.Add(d.AlphaTestEnabled);
    material.Add(d.AlphaCompare);
    material.Add(d.AlphaReference);
    material.Add(d.PositionAttributeLocation);
    material.Add(d.TexCoord0AttributeLocation);
    material.Add(d.Topology);
    material.Add(d.Indexed);
    material.Add(d.IndicesAre16Bit);
    material.Add(d.BaseVertex);
    material.Add(d.VertexCount);
    material.Add(d.IndexBytes.size());
    material.Add(d.CommandListAddress);
    material.Add(d.CommandListOffsetWords);
    material.Add(d.RenderTargetNamespace);
    material.Add(d.FramebufferColorPhysicalAddress);
    for (const auto& a : d.VertexAttributes) {
        material.Add(a.Location);
        material.Add(a.Binding);
        material.Add(a.ComponentCount);
        material.Add(a.ByteOffset);
        material.Add(a.Format);
    }
    for (const auto& b : d.VertexBindings) {
        material.Add(b.Binding);
        material.Add(b.ByteStride);
        material.Add(b.PerInstance);
        material.Add(b.Bytes.size());
    }
    uniform.Add(d.VertexShaderKey);
    uniform.Add(d.FragmentShaderKey);
    uniform.Add(d.VertexShaderSourceIdentity);
    uniform.Add(d.FragmentShaderSourceIdentity);
    uniform.Add(d.TemporalVertexProgram.Hooks.Semantics);
    uniform.Add(d.TemporalVertexProgram.Hooks.SourceSize);
    uniform.Add(d.FragmentShaderHooks.Semantics);
    uniform.Add(d.FragmentShaderHooks.SampledTextureMask);
    for (const auto& sample : d.FragmentShaderHooks.TextureSamples) {
        uniform.Add(sample.Coordinate);
        uniform.Add(sample.Operation);
    }
    uniform.Add(d.TemporalVertexProgram.Hooks.SchemaVersion);
    uniform.Add(d.FragmentShaderHooks.SchemaVersion);
    uniform.Part(d.VertexUniformBytes, 0, 4); // booleans (including skin/UV/tint/light enables)
    const auto scalar = [&](uint8_t reg, uint8_t component) {
        uniform.Part(d.VertexUniformBytes, 80 + size_t(reg) * 16 + component * 4, 4);
    };
    const auto row = [&](uint8_t reg) { uniform.Part(d.VertexUniformBytes, 80 + size_t(reg) * 16, 16); };
    if (const auto* uv = d.TemporalVertexProgram.Hooks.TextureCoordinate(0)) {
        static_assert(std::has_unique_object_representations_v<Renderer3ds::PicaVertexTextureCoordinateLayout>);
        uniform.Add(*uv);
        scalar(uv->SourceSelectorUniform, uv->SourceSelectorComponent);
        for (auto c : uv->SourceSelectorConstantComponents)
            scalar(uv->SourceSelectorConstantUniform, c);
        scalar(uv->CoordinateModeUniform, uv->CoordinateModeComponent);
        for (auto c : uv->CoordinateModeConstantComponents)
            scalar(uv->CoordinateModeConstantUniform, c);
        row(uv->MatrixRowUUniform);
        row(uv->MatrixRowVUniform);
        scalar(uv->HomogeneousUniform, uv->HomogeneousComponent);
        for (const auto& source : uv->Sources)
            scalar(source.ScaleUniform, source.ScaleComponent);
    } else {
        for (uint8_t reg : { 10, 11, 89, 91, 92, 93, 95 })
            row(reg);
    }
    if (masksOnly) {
        // All TEV constants may feed alpha via operand swizzles. Lighting RGB,
        // light directions, fog and depth uniforms cannot, and are excluded.
        uniform.Part(d.FragmentUniformBytes, 0, 6 * 16);
        uniform.Part(d.FragmentUniformBytes, kPicaPackedFragmentCombinerBufferOffset, 16);
        uniform.Part(d.FragmentUniformBytes, kPicaPackedFragmentTevProgramOffset, sizeof(Renderer3ds::PicaTevProgram));
        if (const auto* surface = d.TemporalVertexProgram.Hooks.SurfaceProgram()) {
            static_assert(std::has_unique_object_representations_v<Renderer3ds::PicaVertexSurfaceLayout>);
            uniform.Add(*surface);
            scalar(surface->DiffuseUniform, 3);
            scalar(surface->TintScale.Uniform, surface->TintScale.Component);
            scalar(surface->LightEnabledValue.Uniform, surface->LightEnabledValue.Component);
            for (auto reg : surface->LightDirectionUniforms) {
                scalar(reg, 3);
                scalar(reg + 1, 3);
            }
        }
    }
    return GrassDrawMemoKey{ d.GeometryIdentity, d.GeometryContentVersion,
                             texture.Value,      material.Value,
                             uniform.Value,      rules,
                             textures,           masksOnly };
}
GrassDrawMemo::Entry* GrassDrawMemo::Find(const GrassDrawMemoKey& key, uint64_t frame) {
    const auto it = mEntries.find(key);
    if (it == mEntries.end())
        return nullptr;
    it->second.Frame = frame;
    return &it->second;
}
void GrassDrawMemo::Store(const GrassDrawMemoKey& key, std::optional<GrassSceneMesh> mesh, uint64_t frame) {
    if (mEntries.size() >= 4096 && !mEntries.contains(key)) {
        const auto old = std::min_element(mEntries.begin(), mEntries.end(),
                                          [](const auto& a, const auto& b) { return a.second.Frame < b.second.Frame; });
        mEntries.erase(old);
    }
    mEntries.insert_or_assign(key, Entry{ std::move(mesh), frame });
}
const std::array<float, 16>* GrassDrawMemo::CameraInverse(const Renderer3ds::PicaDrawView& draw) {
    if (!draw.LightBinding.WorldToView)
        return nullptr;
    for (const auto& camera : mCameras)
        if (camera.View == *draw.LightBinding.WorldToView)
            return &camera.Inverse;
    Camera camera{ *draw.LightBinding.WorldToView, {} };
    if (!InvertDirectionalShadowMatrix(camera.View, camera.Inverse))
        return nullptr;
    if (mCameras.size() == 8)
        mCameras.erase(mCameras.begin());
    mCameras.push_back(camera);
    return &mCameras.back().Inverse;
}
bool RefreshGrassMemoDraw(GrassSceneMesh& mesh, const Renderer3ds::PicaDrawView& d,
                          const std::array<float, 16>* cameraInverse) {
    if (d.VertexUniformBytes.size() < 80 + 23 * 16 || !d.LightBinding.WorldToView)
        return false;
    DirectionalShadowMatrix inverse;
    if (cameraInverse)
        inverse = *cameraInverse;
    else if (!InvertDirectionalShadowMatrix(*d.LightBinding.WorldToView, inverse))
        return false;
    Matrix projection{}, view = Identity(), model = Identity(), inverseRows{};
    std::memcpy(projection.data(), d.VertexUniformBytes.data() + 80, 64);
    std::memcpy(view.data(), d.VertexUniformBytes.data() + 80 + 4 * 16, 48);
    std::memcpy(model.data(), d.VertexUniformBytes.data() + 80 + 20 * 16, 48);
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < 4; ++j)
            inverseRows[i * 4 + j] = inverse[j * 4 + i];
    const auto modelView = Multiply(view, model), world = Multiply(inverseRows, modelView);
    // Use exactly the bridge's WORLD-space tolerance. View-space component
    // tolerances admit a different motion after camera rotation. Publish the
    // actual matrix, even within tolerance, so later rule-key changes cannot
    // conceal accumulated animation from the bridge's original history anchor.
    for (size_t i = 0; i < 16; ++i)
        if (!std::isfinite(world[i]) || std::abs(world[i] - mesh.ModelToWorld[i]) > (i % 4 == 3 ? 0.05F : 1e-4F))
            return false;
    const auto clip = Multiply(projection, modelView);
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = 0; j < 4; ++j) {
            if (!std::isfinite(clip[i * 4 + j]))
                return false;
            mesh.PicaModelToClip[j * 4 + i] = clip[i * 4 + j] * (d.FramebufferFlipped && i == 1 ? -1.F : 1.F);
        }
    mesh.ModelToWorld = world;
    if (!mesh.Overlay)
        mesh.Shading =
            DecodeGrassShadingEnvironment(d.VertexUniformBytes, d.FragmentUniformBytes, d.FragmentFeatures, inverse);
    return true;
}
} // namespace Fast::Oot3d
