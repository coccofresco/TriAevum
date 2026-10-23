#include <unordered_map>
#include "fast/oot3d/grass_overlay_mask.h"
#include "fast/oot3d/grass_surface_extractor.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace Fast::Oot3d {
namespace {
using Point = std::array<float, 3>;
Point Sub(const Point& a, const Point& b) {
    return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
}
Point Cross(const Point& a, const Point& b) {
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
float Dot(const Point& a, const Point& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
unsigned Count(uint32_t op) {
    return op == 0 ? 1 : op == 4 || op == 8 || op == 9 ? 3 : 2;
}
uint32_t Selector(const std::array<uint32_t, 4>& words, size_t stage, unsigned input) {
    auto source = (words[0] >> (16 + 4 * input)) & 15;
    return stage == 0 && input < 2 && source == 15 ? (words[0] >> 24) & 15 : source;
}
unsigned Channel(unsigned operand) {
    return operand < 2 ? 3 : operand / 2 - 1;
}
float Quantize(float x) {
    return std::floor(std::clamp(x, 0.0F, 1.0F) * 255 + 0.5F) / 255;
}
float Operation(unsigned op, const std::array<float, 3>& v) {
    switch (op) {
        case 0:
            return v[0];
        case 1:
            return v[0] * v[1];
        case 2:
            return v[0] + v[1];
        case 3:
            return v[0] + v[1] - .5F;
        case 4:
            return v[0] * v[2] + v[1] * (1 - v[2]);
        case 5:
            return v[0] - v[1];
        case 8:
            return std::fma(v[0], v[1], v[2]);
        case 9:
            return std::min(v[0] + v[1], 1.0F) * v[2];
        default:
            return std::numeric_limits<float>::quiet_NaN();
    }
}
int WrapIndex(int x, int size, GrassTextureWrap wrap) {
    if (wrap == GrassTextureWrap::Clamp)
        return std::clamp(x, 0, size - 1);
    const int period = wrap == GrassTextureWrap::Mirror ? size * 2 : size;
    x = (x % period + period) % period;
    return x < size ? x : period - 1 - x;
}
float Sample(const GrassScalarMask& mask, const std::array<float, 2>& uv, bool linear, GrassTextureWrap wrapS,
             GrassTextureWrap wrapT) {
    if (!mask.Width || !mask.Height || mask.Samples.size() != size_t(mask.Width) * mask.Height ||
        !std::isfinite(uv[0]) || !std::isfinite(uv[1]))
        return 0;
    // Reduce before integer conversion; authored UV repeats may be large.
    const auto coordinate = [](float x, GrassTextureWrap wrap) {
        if (wrap == GrassTextureWrap::Clamp)
            return std::clamp(x, 0.0F, 1.0F);
        return std::fmod(x, wrap == GrassTextureWrap::Mirror ? 2.0F : 1.0F);
    };
    const float x = coordinate(uv[0], wrapS) * mask.Width - (linear ? .5F : 0);
    const float y = coordinate(uv[1], wrapT) * mask.Height - (linear ? .5F : 0);
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const auto at = [&](int a, int b) {
        return mask.Samples[size_t(WrapIndex(b, mask.Height, wrapT)) * mask.Width + WrapIndex(a, mask.Width, wrapS)] /
               255.0F;
    };
    if (!linear)
        return at(ix, iy);
    return std::lerp(std::lerp(at(ix, iy), at(ix + 1, iy), x - ix),
                     std::lerp(at(ix, iy + 1), at(ix + 1, iy + 1), x - ix), y - iy);
}
bool Compare(float a, const GrassOverlayAlpha& p) {
    const int byte = int(std::clamp(a, 0.0F, 1.0F) * 255); // native alpha_byte truncates
    using C = Renderer3ds::PicaCompareFunction;
    switch (p.Compare) {
        case C::Never:
            return false;
        case C::Always:
            return true;
        case C::Equal:
            return byte == p.Reference;
        case C::NotEqual:
            return byte != p.Reference;
        case C::Less:
            return byte < p.Reference;
        case C::LessOrEqual:
            return byte <= p.Reference;
        case C::Greater:
            return byte > p.Reference;
        case C::GreaterOrEqual:
            return byte >= p.Reference;
    }
    return false;
}
} // namespace

float SampleGrassOverlayTexture(const GrassScalarMask& mask, const std::array<float, 2>& uv,
                                const GrassOverlayAlpha& alpha) {
    return Sample(mask, uv, alpha.Linear, alpha.WrapS, alpha.WrapT);
}

float EvaluateGrassOverlayCoverage(const GrassOverlayAlpha& alpha, const std::array<float, 2>& uv, float primary) {
    if (alpha.Rule.Mode == GrassMaskMode::Off)
        return 0;
    if (alpha.Rule.Mode == GrassMaskMode::Automatic)
        return EvaluateGrassOverlayNativeCoverage(alpha, uv, primary);
    if (!alpha.CustomMask)
        return 0;
    const auto& rule = alpha.Rule.Coverage;
    const auto wrapS = ResolveGrassTextureWrap(rule.Wrap, alpha.WrapS);
    const auto wrapT = ResolveGrassTextureWrap(rule.Wrap, alpha.WrapT);
    const float u = WrapGrassTextureCoordinate(uv[0], wrapS);
    const float v = WrapGrassTextureCoordinate(uv[1], wrapT);
    return std::clamp(EvaluateGrassMaskLevel(Sample(*alpha.CustomMask, uv, alpha.Linear, wrapS, wrapT), rule, u, v) *
                          alpha.Rule.Strength,
                      0.0F, 1.0F);
}

GrassOverlayPrimaryAlpha DecodeGrassOverlayPrimaryAlpha(const Renderer3ds::PicaVertexShaderHookLayout& hooks,
                                                        std::span<const uint8_t> uniforms) {
    const auto* surface = hooks.SurfaceProgram();
    if (!surface || uniforms.size() < 80 + 96 * 16)
        return {};
    uint32_t booleans = 0;
    std::memcpy(&booleans, uniforms.data(), 4);
    const auto scalar = [&](uint8_t slot, uint8_t component) {
        float x;
        std::memcpy(&x, uniforms.data() + 80 + size_t(slot) * 16 + component * 4, 4);
        return x;
    };
    const bool lit = (booleans & (1U << surface->LightingBooleanUniform)) != 0;
    const bool tint = (booleans & (1U << surface->TintBooleanUniform)) != 0;
    float alpha = scalar(surface->DiffuseUniform, 3);
    // CMB native instructions 77..115: alpha sums diffuse alpha without a
    // normal dot product. Lit tint multiplies; unlit tint replaces material.
    if (lit) {
        float sum = 0;
        const auto enabled = scalar(surface->LightEnabledValue.Uniform, surface->LightEnabledValue.Component);
        for (auto slot : surface->LightDirectionUniforms)
            if (scalar(slot, 3) == enabled)
                sum += scalar(slot + 1, 3);
        alpha *= sum;
    } else if (tint)
        alpha = 1;
    if (tint)
        alpha *= scalar(surface->TintScale.Uniform, surface->TintScale.Component);
    if (!std::isfinite(alpha))
        return {};
    return { true, tint ? surface->TintInputRegister : uint8_t(0xff), alpha };
}

GrassOverlayAlphaInputs GrassOverlayRequiredAlphaInputs(const GrassOverlayAlpha& alpha) {
    if (!alpha.Translucent && !alpha.AlphaTest)
        return {};
    constexpr uint32_t unsupported = 0x80000000U, primary = 16;
    uint32_t previous = 0, buffer = 0, next = 0;
    for (size_t stage = 0; stage < 6; ++stage) {
        const auto& w = alpha.Program.Stages[stage];
        const auto op = (w[2] >> 16) & 15;
        uint32_t value = 0;
        if ((w[2] & 15) == 7 || op > 9 || op == 6 || op == 7)
            value = unsupported;
        else
            for (unsigned i = 0; i < Count(op); ++i) {
                const auto source = Selector(w, stage, i), channel = Channel((w[1] >> (12 + 4 * i)) & 7);
                if (source == 0)
                    value |= channel == 3 ? primary : unsupported;
                else if (source == 3)
                    value |= 1U << channel;
                else if (source == 14) {
                } else if (source == 13)
                    value |= channel == 3 ? buffer : unsupported;
                else if (source == 15)
                    value |= channel == 3 ? previous : unsupported;
                else
                    value |= unsupported;
            }
        previous = value;
        buffer = next;
        if (stage < 4 && (alpha.Program.Control[0] & (1U << (12 + stage))))
            next = value;
    }
    return { (previous & unsupported) == 0, (previous & primary) != 0, uint8_t(previous & 15) };
}

float EvaluateGrassOverlayNativeCoverage(const GrassOverlayAlpha& alpha, const std::array<float, 2>& uv,
                                         float primary) {
    if (!alpha.NativeSupported)
        return 0;
    if (!alpha.Translucent && !alpha.AlphaTest)
        return 1;
    std::array<float, 4> texture{};
    for (size_t c = 0; c < 4; ++c)
        if (alpha.Texture[c])
            texture[c] = Sample(*alpha.Texture[c], uv, alpha.Linear, alpha.WrapS, alpha.WrapT);
    primary = Quantize(primary);
    float previous = 0, buffer = 0, next = alpha.Buffer[3];
    const float unknown = std::numeric_limits<float>::quiet_NaN();
    for (size_t stage = 0; stage < 6; ++stage) {
        const auto& w = alpha.Program.Stages[stage];
        const unsigned op = (w[2] >> 16) & 15;
        std::array<float, 3> v{};
        for (unsigned i = 0; i < Count(op); ++i) {
            const auto source = Selector(w, stage, i), operand = (w[1] >> (12 + 4 * i)) & 7, channel = Channel(operand);
            float x = unknown;
            if (source == 0 && channel == 3)
                x = primary;
            else if (source == 3)
                x = texture[channel];
            else if (source == 14)
                x = alpha.Constants[stage][channel];
            else if (source == 13 && channel == 3)
                x = buffer;
            else if (source == 15 && channel == 3)
                x = previous;
            v[i] = (operand & 1) ? 1 - x : x;
        }
        const auto scale = (w[3] >> 16) & 3;
        previous = (w[2] & 15) == 7
                       ? unknown
                       : std::clamp(Quantize(Operation(op, v)) * float(scale < 3 ? 1U << scale : 1U), 0.0F, 1.0F);
        buffer = next;
        if (stage < 4 && (alpha.Program.Control[0] & (1U << (12 + stage))))
            next = previous;
    }
    if (!std::isfinite(previous) || (alpha.AlphaTest && !Compare(previous, alpha)))
        return 0;
    return alpha.Translucent ? previous : 1.0F;
}
bool GrassOverlayCanCover(const GrassOverlayAlpha& alpha) noexcept {
    switch (alpha.Rule.Mode) {
        case GrassMaskMode::Off:
            return false;
        case GrassMaskMode::Custom:
            return true;
        case GrassMaskMode::Automatic:
            break;
    }
    return alpha.NativeSupported;
}

uint64_t GrassOverlayAlphaVersion(const GrassOverlayAlpha& alpha, uint64_t textureIdentity) {
    uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&](auto value) {
        for (size_t i = 0; i < sizeof(value); ++i)
            hash = (hash ^ uint8_t(uint64_t(value) >> (i * 8))) * 1099511628211ULL;
    };
    mix(kGrassOverlayVersion);
    mix(alpha.NativeSupported);
    mix(static_cast<uint8_t>(alpha.Rule.Mode));
    if (alpha.Rule.Mode == GrassMaskMode::Custom) {
        mix(GrassPlacementRuleVersion(alpha.Rule.Coverage, GrassGenerationSettings{}));
        mix(std::bit_cast<uint32_t>(alpha.Rule.Strength));
    }
    mix(textureIdentity);
    for (const auto& stage : alpha.Program.Stages)
        for (auto w : stage)
            mix(w);
    mix(alpha.Program.Control[0]);
    for (const auto& c : alpha.Constants)
        for (float x : c)
            mix(std::bit_cast<uint32_t>(x));
    for (float x : alpha.Buffer)
        mix(std::bit_cast<uint32_t>(x));
    mix(uint8_t(alpha.WrapS));
    mix(uint8_t(alpha.WrapT));
    mix(alpha.Linear);
    mix(alpha.Translucent);
    mix(alpha.AlphaTest);
    mix(uint8_t(alpha.Compare));
    mix(alpha.Reference);
    return hash ? hash : 1;
}
bool PrepareGrassOverlayTriangle(GrassOverlayTriangle& t) {
    for (const auto& v : t.Vertices) {
        for (float x : v.Position)
            if (!std::isfinite(x))
                return false;
        for (float x : v.Uv)
            if (!std::isfinite(x))
                return false;
        if (!std::isfinite(v.Alpha))
            return false;
    }
    t.Edge1 = Sub(t.Vertices[1].Position, t.Vertices[0].Position);
    t.Edge2 = Sub(t.Vertices[2].Position, t.Vertices[0].Position);
    t.Normal = Cross(t.Edge1, t.Edge2);
    const float size = std::sqrt(Dot(t.Normal, t.Normal));
    if (!std::isfinite(size) || size < 1e-6F)
        return false;
    for (float& x : t.Normal)
        x /= size;
    t.D11 = Dot(t.Edge1, t.Edge1);
    t.D12 = Dot(t.Edge1, t.Edge2);
    t.D22 = Dot(t.Edge2, t.Edge2);
    const double det = double(t.D11) * t.D22 - double(t.D12) * t.D12;
    if (!std::isfinite(det) || det <= 1e-12)
        return false;
    t.InverseDeterminant = float(1 / det);
    for (size_t a = 0; a < 3; ++a) {
        t.Low[a] = std::min({ t.Vertices[0].Position[a], t.Vertices[1].Position[a], t.Vertices[2].Position[a] });
        t.High[a] = std::max({ t.Vertices[0].Position[a], t.Vertices[1].Position[a], t.Vertices[2].Position[a] });
    }
    return true;
}
std::vector<const GrassOverlayTriangle*> GrassOverlaysForTriangle(const std::array<Point, 3>& source,
                                                                  const GrassOverlaySet& overlays) {
    std::vector<const GrassOverlayTriangle*> result;
    auto normal = Cross(Sub(source[1], source[0]), Sub(source[2], source[0]));
    const auto length = std::sqrt(Dot(normal, normal));
    if (!(length > 1e-6F))
        return result;
    for (float& x : normal)
        x /= length;
    for (const auto& t : overlays.Triangles) {
        if (std::abs(Dot(normal, t.Normal)) < 0.99985F)
            continue; // one degree
        bool overlap = true;
        for (size_t a = 0; a < 3; ++a) {
            const auto low = std::min({ source[0][a], source[1][a], source[2][a] });
            const auto high = std::max({ source[0][a], source[1][a], source[2][a] });
            overlap &= low <= t.High[a] + kGrassOverlayPlaneTolerance && high >= t.Low[a] - kGrassOverlayPlaneTolerance;
        }
        for (const auto& v : t.Vertices)
            overlap &= std::abs(Dot(Sub(v.Position, source[0]), normal)) <= kGrassOverlayPlaneTolerance;
        if (overlap)
            result.push_back(&t);
    }
    // Group a draw's triangles for union coverage, independent of capture order.
    std::stable_sort(result.begin(), result.end(), [](auto a, auto b) { return a->Draw < b->Draw; });
    return result;
}
void MeasureGrassOverlayFootprint(const std::array<Point, 3>& source, const GrassOverlayTriangle& overlay,
                                  GrassOverlayFootprint& footprint) {
    const auto e1 = Sub(source[1], source[0]), e2 = Sub(source[2], source[0]);
    const double d11 = Dot(e1, e1), d12 = Dot(e1, e2), d22 = Dot(e2, e2);
    const double determinant = d11 * d22 - d12 * d12;
    if (!(determinant > 1e-12))
        return;
    struct Vertex {
        GrassOverlayVertex Value;
        std::array<float, 3> Bary;
    };
    std::vector<Vertex> polygon;
    for (const auto& v : overlay.Vertices) {
        const auto p = Sub(v.Position, source[0]);
        const double a = Dot(p, e1), b = Dot(p, e2);
        const float u = float((a * d22 - b * d12) / determinant), v1 = float((b * d11 - a * d12) / determinant);
        polygon.push_back({ v, { 1 - u - v1, u, v1 } });
    }
    // Sutherland-Hodgman in the source's barycentric plane. Unlike an AABB,
    // this measures only the atlas portions that actually cover grass.
    for (size_t edge = 0; edge < 3 && !polygon.empty(); ++edge) {
        std::vector<Vertex> clipped;
        auto previous = polygon.back();
        for (const auto& current : polygon) {
            const float a = previous.Bary[edge], b = current.Bary[edge];
            if ((a >= 0) != (b >= 0)) {
                const float t = a / (a - b);
                Vertex intersection;
                for (size_t axis = 0; axis < 3; ++axis) {
                    intersection.Bary[axis] = std::lerp(previous.Bary[axis], current.Bary[axis], t);
                    intersection.Value.Position[axis] =
                        std::lerp(previous.Value.Position[axis], current.Value.Position[axis], t);
                }
                for (size_t axis = 0; axis < 2; ++axis)
                    intersection.Value.Uv[axis] = std::lerp(previous.Value.Uv[axis], current.Value.Uv[axis], t);
                intersection.Value.Alpha = std::lerp(previous.Value.Alpha, current.Value.Alpha, t);
                clipped.push_back(intersection);
            }
            if (b >= 0)
                clipped.push_back(current);
            previous = current;
        }
        polygon = std::move(clipped);
    }
    const auto bin = [](float x) -> size_t {
        if (x <= 0)
            return 0;
        if (x >= 1)
            return 15;
        return 1 + std::min(size_t(x * 14), size_t(13));
    };
    for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        const std::array<GrassOverlayVertex, 3> v{ polygon[0].Value, polygon[i].Value, polygon[i + 1].Value };
        const auto cross = Cross(Sub(v[1].Position, v[0].Position), Sub(v[2].Position, v[0].Position));
        const double area = .5 * std::sqrt(Dot(cross, cross));
        if (!(area > 1e-8))
            continue;
        footprint.Area += area;
        // Bound editor-only wireframe storage independently of scene size.
        if (footprint.Triangles.size() < 4096)
            footprint.Triangles.push_back({ v[0].Position, v[1].Position, v[2].Position });
        for (unsigned row = 0; row < 8; ++row)
            for (unsigned column = 0; column < 8; ++column) {
                const float r = std::sqrt((row + .5F) / 8), t = (column + .5F) / 8;
                const std::array<float, 3> weights{ 1 - r, r * (1 - t), r * t };
                std::array<float, 2> uv{};
                float primary = 0;
                for (size_t corner = 0; corner < 3; ++corner) {
                    primary += weights[corner] * v[corner].Alpha;
                    for (size_t axis = 0; axis < 2; ++axis)
                        uv[axis] += weights[corner] * v[corner].Uv[axis];
                }
                const auto* alpha = overlay.Alpha.get();
                const float texture =
                    alpha && alpha->Texture[3] ? SampleGrassOverlayTexture(*alpha->Texture[3], uv, *alpha) : 1;
                const float native = alpha ? EvaluateGrassOverlayNativeCoverage(*alpha, uv, primary) : 1;
                const float effective = alpha ? EvaluateGrassOverlayCoverage(*alpha, uv, primary) : 1;
                footprint.TextureAlphaArea[bin(texture)] += area / 64;
                footprint.NativeAlphaArea[bin(native)] += area / 64;
                footprint.CoverageArea += area / 64 * effective;
            }
    }
}

float GrassOverlaySurvival(const Point& point, std::span<const GrassOverlayTriangle* const> triangles) {
    float survival = 1, coverage = 0;
    uint64_t draw = 0;
    for (const auto* t : triangles) {
        if (t->Draw != draw) {
            survival *= 1 - coverage;
            coverage = 0;
            draw = t->Draw;
            if (survival == 0)
                return 0;
        }
        const auto p = Sub(point, t->Vertices[0].Position);
        const float d1 = Dot(p, t->Edge1), d2 = Dot(p, t->Edge2);
        const float u = (t->D22 * d1 - t->D12 * d2) * t->InverseDeterminant;
        const float v = (t->D11 * d2 - t->D12 * d1) * t->InverseDeterminant, w = 1 - u - v;
        if (u < -1e-5F || v < -1e-5F || w < -1e-5F)
            continue;
        std::array<float, 2> uv;
        for (size_t i = 0; i < 2; ++i)
            uv[i] = t->Vertices[0].Uv[i] * w + t->Vertices[1].Uv[i] * u + t->Vertices[2].Uv[i] * v;
        const float alpha = t->Vertices[0].Alpha * w + t->Vertices[1].Alpha * u + t->Vertices[2].Alpha * v;
        coverage = std::max(coverage, t->Alpha ? EvaluateGrassOverlayCoverage(*t->Alpha, uv, alpha) : 1.0F);
    }
    return survival * (1 - coverage);
}
std::shared_ptr<const GrassOverlaySet> ResolveGrassOverlayTextures(std::shared_ptr<const GrassOverlaySet> set) {
    if (!set || std::none_of(set->Triangles.begin(), set->Triangles.end(),
                             [](const auto& t) { return t.Alpha && bool(t.Alpha->LoadTextures); }))
        return set;
    auto copy = std::make_shared<GrassOverlaySet>(*set);
    std::unordered_map<const GrassOverlayAlpha*, std::shared_ptr<const GrassOverlayAlpha>> resolved;
    for (auto& t : copy->Triangles)
        if (t.Alpha && t.Alpha->LoadTextures) {
            auto [it, inserted] = resolved.try_emplace(t.Alpha.get());
            if (inserted) {
                auto alpha = std::make_shared<GrassOverlayAlpha>(*t.Alpha);
                alpha->LoadTextures(*alpha);
                alpha->LoadTextures = {};
                it->second = std::move(alpha);
            }
            t.Alpha = it->second;
        }
    return copy;
}
} // namespace Fast::Oot3d
