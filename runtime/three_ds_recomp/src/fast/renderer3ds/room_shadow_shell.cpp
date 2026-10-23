#include "fast/renderer3ds/room_shadow_shell.h"
#include "fast/renderer/cache_file.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <stdexcept>

namespace Fast::Renderer3ds {
namespace {
constexpr uint64_t kBasis = 1469598103934665603ULL; // Renderer FNV basis (intentionally nonstandard).
constexpr uint32_t kMagic = 0x48535352U;            // RSSH
constexpr size_t kHeaderBytes = 40;
constexpr size_t kMaximumBytes = kHeaderBytes + kMaximumRoomShadowTriangles * (3 * 12 + 3 * 4 + 8) + 8;
double Dot(const ShadowVector& a, const ShadowVector& b) {
    return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
}
bool Finite(const ShadowVector& p) {
    return std::all_of(p.begin(), p.end(), [](float v) { return std::isfinite(v) && std::abs(v) <= 1.e8F; });
}
uint64_t Checksum(std::span<const uint8_t> bytes) {
    uint64_t hash = kBasis;
    for (auto b : bytes)
        hash = (hash ^ b) * 1099511628211ULL;
    return hash;
}
void Put(std::vector<uint8_t>& out, uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i)
        out.push_back(uint8_t(v >> (i * 8)));
}
uint64_t Get(std::span<const uint8_t> in, size_t at, size_t n) {
    if (at > in.size() || n > in.size() - at)
        throw std::runtime_error("truncated room shadow cache");
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i)
        v |= uint64_t(in[at + i]) << (i * 8);
    return v;
}
void Bounds(RoomShadowShell& mesh) {
    if (mesh.Positions.empty())
        return;
    mesh.Bounds = { mesh.Positions.front(), mesh.Positions.front(), true };
    for (const auto& p : mesh.Positions)
        for (size_t k = 0; k < 3; ++k) {
            mesh.Bounds.Minimum[k] = std::min(mesh.Bounds.Minimum[k], p[k]);
            mesh.Bounds.Maximum[k] = std::max(mesh.Bounds.Maximum[k], p[k]);
        }
}
bool Valid(const RoomShadowShell& m) {
    if (m.RasterSize < 8 || m.RasterSize > 2048 || m.ShellMasks.size() > kMaximumRoomShadowTriangles ||
        m.Indices.size() != m.ShellMasks.size() * 3 || m.Positions.size() > kMaximumRoomShadowTriangles * 3)
        return false;
    for (const auto& p : m.Positions)
        if (!Finite(p))
            return false;
    if (!m.Interior && std::any_of(m.ShellMasks.begin(), m.ShellMasks.end(), [](auto mask) { return mask != 0; }))
        return false;
    for (auto i : m.Indices)
        if (i >= m.Positions.size())
            return false;
    return true;
}
// The exact trial convention: samples (2*centroid + corner)/3, not
// two-thirds toward the corner. Double precision matches NumPy's float64.
using D3 = std::array<double, 3>;
constexpr double kVoxel = 12.0;
constexpr uint8_t kMaxClosing = 20, kFar = kMaxClosing + 1;
constexpr uint64_t kMaximumVoxelSamples = 128ULL * 1024 * 1024;
constexpr uint64_t kMaximumRaySteps = 512ULL * 1024 * 1024;
struct InteriorGrid {
    D3 Low{};
    std::array<uint32_t, 3> Size{};
    std::vector<uint8_t> SolidDistance, Interior, Distance;
    std::vector<uint32_t> Queue;
    uint32_t Row = 0, Plane = 0;
    uint32_t At(uint32_t x, uint32_t y, uint32_t z) const {
        return x * Plane + y * Row + z;
    }
    template <class F> void Neighbours(uint32_t i, F visit) const {
        const auto z = i % Row, x = i / Plane, y = (i % Plane) / Row;
        if (z)
            visit(i - 1);
        if (z + 1 < Size[2])
            visit(i + 1);
        if (y)
            visit(i - Row);
        if (y + 1 < Size[1])
            visit(i + Row);
        if (x)
            visit(i - Plane);
        if (x + 1 < Size[0])
            visit(i + Plane);
    }
    // Separable exact Manhattan distance, capped at 21: six linear passes,
    // independent of sealing radius. No repeated dilation allocations.
    bool Transform(std::vector<uint8_t>& d, std::stop_token stop) const {
        for (uint32_t stride : { 1U, Row, Plane }) {
            const uint32_t axisSize = stride == 1 ? Size[2] : stride == Row ? Size[1] : Size[0];
            for (uint32_t i = 0; i < d.size(); ++i) {
                if ((i & 0xfffffU) == 0 && stop.stop_requested())
                    return false;
                if ((i / stride) % axisSize)
                    d[i] = std::min(d[i], uint8_t(d[i - stride] + 1));
            }
            for (uint32_t i = uint32_t(d.size()); i-- > 0;) {
                if ((i & 0xfffffU) == 0 && stop.stop_requested())
                    return false;
                if ((i / stride) % axisSize + 1 < axisSize)
                    d[i] = std::min(d[i], uint8_t(d[i + stride] + 1));
            }
        }
        return true;
    }
    bool Build(const RoomShadowShell& mesh, RoomInteriorSunStats& stats, std::stop_token stop) {
        uint64_t count = 1;
        for (size_t k = 0; k < 3; ++k) {
            Low[k] = double(mesh.Bounds.Minimum[k]) - kVoxel * (kMaxClosing + 3);
            const double extent = (double(mesh.Bounds.Maximum[k]) - mesh.Bounds.Minimum[k]) / kVoxel;
            const double dim = std::ceil(extent + 2 * (kMaxClosing + 3)) + 1;
            if (dim > kMaximumRoomInteriorVoxels || dim < 1)
                return false;
            Size[k] = uint32_t(dim);
            if (count > kMaximumRoomInteriorVoxels / Size[k])
                return false;
            count *= Size[k];
        }
        stats.Voxels = count;
        stats.PeakWorkingBytes = count * 7; // three byte grids and one u32 flood queue.
        Row = Size[2];
        Plane = Size[1] * Row;
        SolidDistance.assign(size_t(count), kFar);
        Distance.resize(size_t(count));
        Interior.resize(size_t(count));
        Queue.reserve(size_t(count));
        for (size_t t = 0; t < mesh.Indices.size(); t += 3) {
            if (stop.stop_requested())
                return false;
            D3 a{}, u{}, v{};
            double edgeSquared = 0;
            for (size_t k = 0; k < 3; ++k) {
                a[k] = mesh.Positions[mesh.Indices[t]][k];
                u[k] = double(mesh.Positions[mesh.Indices[t + 1]][k]) - a[k];
                v[k] = double(mesh.Positions[mesh.Indices[t + 2]][k]) - a[k];
            }
            double aa = 0, bb = 0, cc = 0;
            for (size_t k = 0; k < 3; ++k) {
                aa += u[k] * u[k];
                bb += v[k] * v[k];
                cc += (v[k] - u[k]) * (v[k] - u[k]);
            }
            edgeSquared = std::max({ aa, bb, cc });
            const uint64_t steps = std::max<uint64_t>(2, uint64_t(std::ceil(std::sqrt(edgeSquared) / (kVoxel * .4))));
            const uint64_t samples = (steps + 1) * (steps + 2) / 2;
            if (samples > kMaximumVoxelSamples - stats.VoxelSamples)
                return false;
            stats.VoxelSamples += samples;
            for (uint32_t i = 0; i <= steps; ++i) {
                if ((i & 63U) == 0 && stop.stop_requested())
                    return false;
                for (uint32_t j = 0; j <= steps - i; ++j) {
                    std::array<uint32_t, 3> p{};
                    for (size_t k = 0; k < 3; ++k)
                        p[k] = uint32_t(
                            std::floor((a[k] + double(i) / steps * u[k] + double(j) / steps * v[k] - Low[k]) / kVoxel));
                    SolidDistance[At(p[0], p[1], p[2])] = 0;
                }
            }
        }
        if (!Transform(SolidDistance, stop))
            return false;
        uint64_t previous = 0, bestNumerator = 0, bestDenominator = 1;
        for (uint8_t closing = 2; closing <= kMaxClosing; closing += 2) {
            std::fill(Distance.begin(), Distance.end(), kFar);
            Queue.clear();
            Queue.push_back(0);
            Distance[0] = 0;
            // The 23-voxel border is connected and remains outside even at
            // closing=20; seeding one corner is identical to all border faces.
            for (size_t q = 0; q < Queue.size(); ++q) {
                if ((q & 0xfffffU) == 0 && stop.stop_requested())
                    return false;
                Neighbours(Queue[q], [&](uint32_t next) {
                    if (SolidDistance[next] > closing && Distance[next] != 0) {
                        Distance[next] = 0;
                        Queue.push_back(next);
                    }
                });
            }
            if (!Transform(Distance, stop))
                return false;
            uint64_t volume = 0;
            for (size_t i = 0; i < count; ++i)
                volume += SolidDistance[i] != 0 && Distance[i] > closing;
            const auto denominator = std::max<uint64_t>(previous, 1);
            if (closing > 2 && (stats.ClosingSteps == 0 || volume * bestDenominator > bestNumerator * denominator)) {
                bestNumerator = volume;
                bestDenominator = denominator;
                stats.ClosingSteps = closing;
                stats.InteriorVoxels = volume;
                for (size_t i = 0; i < count; ++i)
                    Interior[i] = SolidDistance[i] != 0 && Distance[i] > closing;
            }
            previous = volume;
        }
        return true;
    }
    bool Entered(const D3& sample, const D3& direction, RoomInteriorSunStats& stats, std::stop_token stop) const {
        for (uint32_t step = 0;; ++step) {
            if (++stats.RaySteps > kMaximumRaySteps || ((step & 255U) == 0 && stop.stop_requested()))
                throw std::runtime_error("interior-sun trace cancelled or work bound exceeded");
            const double distance = 2 * kVoxel + step * (.5 * kVoxel);
            std::array<int, 3> p{};
            for (size_t k = 0; k < 3; ++k) {
                p[k] = int(std::floor((sample[k] + direction[k] * distance - Low[k]) / kVoxel));
                if (p[k] < 0 || p[k] >= int(Size[k]))
                    return false;
            }
            if (Interior[At(p[0], p[1], p[2])])
                return true;
        }
    }
};
} // namespace

bool RoomShadowShellScene::Ready() const noexcept {
    return !Rooms.empty() && std::all_of(Rooms.begin(), Rooms.end(), [](const auto& r) {
        return r.Mesh && r.Mesh->Fingerprint == r.Fingerprint && r.Mesh->Interior == r.Interior;
    });
}
const std::array<ShadowVector, kRoomShadowSectorCount>& RoomShadowDirections() {
    static const auto directions = [] {
        std::array<ShadowVector, kRoomShadowSectorCount> result{};
        const double angle = std::numbers::pi * (3 - std::sqrt(5.0));
        for (uint32_t s = 0; s < result.size(); ++s) {
            const double y = (s + .5) / result.size(), r = std::sqrt(1 - y * y);
            result[s] = { float(r * std::cos(s * angle)), float(y), float(r * std::sin(s * angle)) };
        }
        return result;
    }();
    return directions;
}
uint32_t RoomShadowSector(const ShadowVector& light) noexcept {
    double best = -std::numeric_limits<double>::infinity();
    uint32_t result = 0;
    for (uint32_t i = 0; i < kRoomShadowSectorCount; ++i) {
        const double dot = Dot(light, RoomShadowDirections()[i]);
        if (dot > best) {
            best = dot;
            result = i;
        }
    }
    return result;
}
namespace {
// Opaque triangles with exactly coincident positions welded, in input order;
// zero masks. Null on bounds, cancellation or a non-finite position.
std::shared_ptr<RoomShadowShell> WeldRoomShadowShell(uint64_t fingerprint, std::span<const RoomShadowTriangle> input,
                                                     uint32_t size, std::stop_token stop, bool interior) {
    if (input.size() > kMaximumRoomShadowTriangles || size < 8 || size > 2048 || stop.stop_requested())
        return {};
    auto result = std::make_shared<RoomShadowShell>();
    result->Fingerprint = fingerprint;
    result->RasterSize = size;
    result->Interior = interior;
    std::map<ShadowVector, uint32_t> welded;
    for (const auto& t : input) {
        if (stop.stop_requested())
            return {};
        if (!t.Opaque || t.AlphaTest)
            continue;
        for (auto p : t.Positions) {
            if (!Finite(p))
                return {};
            for (auto& v : p)
                if (v == 0)
                    v = 0;
            auto [it, added] = welded.try_emplace(p, uint32_t(result->Positions.size()));
            if (added)
                result->Positions.push_back(p);
            result->Indices.push_back(it->second);
        }
    }
    result->ShellMasks.resize(result->Indices.size() / 3);
    Bounds(*result);
    return result;
}

std::shared_ptr<const RoomShadowShell> Seal(std::shared_ptr<RoomShadowShell> result) {
    const auto encoded = EncodeRoomShadowShell(*result);
    if (encoded.empty())
        return {};
    result->ContentVersion = Get(encoded, encoded.size() - 8, 8);
    return result;
}
} // namespace

std::shared_ptr<const RoomShadowShell> AssembleRoomShadowShell(uint64_t fingerprint,
                                                               std::span<const RoomShadowTriangle> input,
                                                               std::span<const uint64_t> masks, uint32_t size,
                                                               bool interior) {
    auto result = WeldRoomShadowShell(fingerprint, input, size, {}, interior);
    if (!result || masks.size() != result->ShellMasks.size())
        return {};
    if (!interior && std::any_of(masks.begin(), masks.end(), [](uint64_t mask) { return mask != 0; }))
        return {};
    std::copy(masks.begin(), masks.end(), result->ShellMasks.begin());
    return Seal(std::move(result));
}

std::shared_ptr<const RoomShadowShell> BuildRoomShadowShell(uint64_t fingerprint,
                                                            std::span<const RoomShadowTriangle> input, uint32_t size,
                                                            std::stop_token stop, bool interior,
                                                            RoomInteriorSunStats* suppliedStats) {
    RoomInteriorSunStats stats;
    struct Publish {
        RoomInteriorSunStats* Output;
        RoomInteriorSunStats& Value;
        ~Publish() {
            if (Output)
                *Output = Value;
        }
    } publish{ suppliedStats, stats };
    auto result = WeldRoomShadowShell(fingerprint, input, size, stop, interior);
    if (!result)
        return {};
    if (interior && !result->Indices.empty()) {
        try {
            InteriorGrid grid;
            if (!grid.Build(*result, stats, stop))
                return {};
            // No detected interior: conservative two-sided geometry, not all
            // triangles omitted. This is reported in prebuild diagnostics.
            if (stats.InteriorVoxels) {
                const double angle = std::numbers::pi * (3 - std::sqrt(5.0));
                for (uint32_t sector = 0; sector < kRoomShadowSectorCount; ++sector) {
                    const double y = (sector + .5) / kRoomShadowSectorCount, r = std::sqrt(1 - y * y);
                    const D3 d{ r * std::cos(sector * angle), y, r * std::sin(sector * angle) };
                    for (size_t t = 0; t < result->ShellMasks.size(); ++t) {
                        if (stop.stop_requested())
                            return {};
                        std::array<D3, 4> samples{};
                        for (size_t k = 0; k < 3; ++k) {
                            for (size_t j = 0; j < 3; ++j)
                                samples[0][k] += double(result->Positions[result->Indices[3 * t + j]][k]);
                            samples[0][k] /= 3;
                            for (size_t j = 0; j < 3; ++j)
                                samples[j + 1][k] =
                                    (2 * samples[0][k] + result->Positions[result->Indices[3 * t + j]][k]) / 3;
                        }
                        uint32_t entered = 0;
                        for (const auto& sample : samples) {
                            entered += grid.Entered(sample, d, stats, stop);
                            if (entered >= 2)
                                break;
                        }
                        if (entered <= 1)
                            result->ShellMasks[t] |= uint64_t{ 1 } << sector;
                    }
                }
            }
        } catch (const std::exception&) { return {}; }
    }
    return Seal(std::move(result));
}
std::vector<uint8_t> EncodeRoomShadowShell(const RoomShadowShell& mesh) {
    if (!Valid(mesh))
        return {};
    std::vector<uint8_t> bytes;
    bytes.reserve(kHeaderBytes + mesh.Positions.size() * 12 + mesh.Indices.size() * 4 + mesh.ShellMasks.size() * 8 + 8);
    Put(bytes, kMagic, 4);
    Put(bytes, kRoomShadowShellVersion, 4);
    Put(bytes, mesh.Fingerprint, 8);
    Put(bytes, kRoomShadowSectorCount, 4);
    Put(bytes, mesh.RasterSize, 4);
    Put(bytes, mesh.Positions.size(), 4);
    Put(bytes, mesh.ShellMasks.size(), 4);
    Put(bytes, mesh.Interior ? 1 : 0, 4);
    Put(bytes, 0, 4);
    for (const auto& p : mesh.Positions)
        for (auto v : p)
            Put(bytes, std::bit_cast<uint32_t>(v), 4);
    for (auto i : mesh.Indices)
        Put(bytes, i, 4);
    for (auto m : mesh.ShellMasks)
        Put(bytes, m, 8);
    Put(bytes, Checksum(bytes), 8);
    return bytes;
}
std::shared_ptr<const RoomShadowShell> DecodeRoomShadowShell(std::span<const uint8_t> bytes, uint64_t fingerprint,
                                                             bool interior) {
    try {
        if (bytes.size() < kHeaderBytes + 8 || bytes.size() > kMaximumBytes || Get(bytes, 0, 4) != kMagic ||
            Get(bytes, 4, 4) != kRoomShadowShellVersion || Get(bytes, 8, 8) != fingerprint ||
            Get(bytes, 16, 4) != kRoomShadowSectorCount || Get(bytes, 20, 4) < 8 || Get(bytes, 20, 4) > 2048 ||
            Get(bytes, 32, 4) != uint32_t(interior) || Get(bytes, 36, 4) != 0)
            return {};
        const size_t vertices = Get(bytes, 24, 4), triangles = Get(bytes, 28, 4);
        if (triangles > kMaximumRoomShadowTriangles || vertices > kMaximumRoomShadowTriangles * 3 ||
            bytes.size() != kHeaderBytes + vertices * 12 + triangles * 20 + 8)
            return {};
        const uint64_t checksum = Get(bytes, bytes.size() - 8, 8);
        if (Checksum(bytes.first(bytes.size() - 8)) != checksum)
            return {};
        auto mesh = std::make_shared<RoomShadowShell>();
        mesh->Fingerprint = fingerprint;
        mesh->Interior = interior;
        mesh->ContentVersion = checksum;
        mesh->RasterSize = uint32_t(Get(bytes, 20, 4));
        size_t at = kHeaderBytes;
        mesh->Positions.resize(vertices);
        mesh->Indices.resize(triangles * 3);
        mesh->ShellMasks.resize(triangles);
        for (auto& p : mesh->Positions)
            for (auto& v : p) {
                v = std::bit_cast<float>(uint32_t(Get(bytes, at, 4)));
                at += 4;
            }
        for (auto& i : mesh->Indices) {
            i = uint32_t(Get(bytes, at, 4));
            at += 4;
        }
        for (auto& m : mesh->ShellMasks) {
            m = Get(bytes, at, 8);
            at += 8;
        }
        if (!Valid(*mesh))
            return {};
        Bounds(*mesh);
        return mesh;
    } catch (const std::exception&) { return {}; }
}
std::filesystem::path RoomShadowShellPath(const std::filesystem::path& directory, uint64_t fingerprint, bool interior) {
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx%s.bin", static_cast<unsigned long long>(fingerprint),
                  interior ? "" : "-exterior");
    return directory / "shadow-shells" / name;
}
std::shared_ptr<const RoomShadowShell> LoadRoomShadowShell(const std::filesystem::path& directory, uint64_t fingerprint,
                                                           bool interior) {
    if (directory.empty())
        return {};
    std::ifstream file(RoomShadowShellPath(directory, fingerprint, interior), std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (!file || length < 0 || uint64_t(length) > kMaximumBytes)
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        return {};
    auto mesh = DecodeRoomShadowShell(bytes, fingerprint, interior);
    return mesh && mesh->RasterSize == kRoomShadowRasterSize ? mesh : nullptr;
}
bool SaveRoomShadowShell(const std::filesystem::path& directory, const RoomShadowShell& mesh, std::string* error) {
    if (directory.empty()) {
        if (error)
            *error = "cache directory is empty";
        return false;
    }
    const auto bytes = EncodeRoomShadowShell(mesh);
    if (bytes.empty()) {
        if (error)
            *error = "invalid shell mesh";
        return false;
    }
    return Renderer::WriteCacheFileAtomically(RoomShadowShellPath(directory, mesh.Fingerprint, mesh.Interior), {},
                                              bytes, error);
}
std::vector<RoomShadowShellVertex> RoomShadowShellVertices(const RoomShadowShell& mesh) {
    std::vector<RoomShadowShellVertex> result;
    if (!Valid(mesh))
        return result;
    result.reserve(mesh.Indices.size());
    for (size_t i = 0; i < mesh.Indices.size(); ++i) {
        const auto mask = mesh.ShellMasks[i / 3];
        result.push_back({ mesh.Positions[mesh.Indices[i]], { uint32_t(mask), uint32_t(mask >> 32) } });
    }
    return result;
}
std::string_view RoomShadowShellVertexShader() {
    return R"glsl(#version 450
layout(location=0) in vec3 position_world;
layout(location=1) in uvec2 shell_mask;
layout(set=0,binding=30,std140) uniform ShadowTransform {
    mat4 view_to_world;
    mat4 world_to_light_clip;
} shadow;
layout(push_constant) uniform ShadowPage {
    vec4 scale_offset;
    uint sector;
} page;
void main() {
    if ((shell_mask[page.sector >> 5u] & (1u << (page.sector & 31u))) != 0u) {
        gl_Position=vec4(0.0,0.0,0.0,1.0);
    } else {
        gl_Position=shadow.world_to_light_clip*vec4(position_world,1.0);
        gl_Position.xy=gl_Position.xy*page.scale_offset.xy+page.scale_offset.zw*gl_Position.w;
    }
}
)glsl";
}
VirtualShadowCaster RoomShadowCaster(const RoomShadowShell& mesh, uint32_t sector, uint64_t rasterState) noexcept {
    auto hash = [](uint64_t seed, uint64_t value) {
        for (size_t i = 0; i < 8; ++i)
            seed = (seed ^ uint8_t(value >> (i * 8))) * 1099511628211ULL;
        return seed;
    };
    const uint64_t content = hash(hash(mesh.ContentVersion, sector), rasterState);
    return { mesh.Bounds, content ? content : 1, false, hash(0x5348454c4c524f4fULL, mesh.Fingerprint) };
}
} // namespace Fast::Renderer3ds
