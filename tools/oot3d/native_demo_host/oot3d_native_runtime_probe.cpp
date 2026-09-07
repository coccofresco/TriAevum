#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeResourceContract.h"

namespace {

constexpr std::string_view kRuntimeProbeFormat = "oot3d_runtime/three_ds_recomp_native_runtime_probe_v1";
constexpr std::string_view kRuntimeTraceFormat = "oot3d_runtime/three_ds_recomp_native_runtime_trace_v1";

struct Args {
    std::string ManifestPath;
    std::string OutputRoot;
};

struct Vec3 {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
};

struct CollisionPoly {
    int Type = 0;
    int A = 0;
    int B = 0;
    int C = 0;
    int Nx = 0;
    int Ny = 0;
    int Nz = 0;
    int Dist = 0;
};

struct CollisionScene {
    std::vector<Vec3> Vertices;
    std::vector<CollisionPoly> Polygons;
    Vec3 BoundsMin;
    Vec3 BoundsMax;
};

struct FloorHit {
    double Y = 0.0;
    int PolygonIndex = -1;
    int SurfaceType = -1;
};

struct GlbMetadata {
    uint32_t Version = 0;
    uint32_t Length = 0;
    size_t VertexCount = 0;
    size_t TriangleCount = 0;
    size_t MorphFrameCount = 1;
};

void PrintUsage() {
    std::cerr << "usage: oot3d_native_runtime_probe --manifest <demo_manifest.json> --output-root <dir>\n";
}

bool ParseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc) {
            args.ManifestPath = argv[++i];
        } else if (arg == "--output-root" && i + 1 < argc) {
            args.OutputRoot = argv[++i];
        } else {
            return false;
        }
    }
    return !args.ManifestPath.empty() && !args.OutputRoot.empty();
}

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open JSON file: " + path.string());
    }
    return nlohmann::json::parse(file);
}

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write JSON file: " + path.string());
    }
    file << data.dump(2) << "\n";
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not open binary file: " + path.string());
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open text file: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(file), {});
}

uint32_t ReadLe32(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("read past end of buffer");
    }
    return static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) | (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::string Attr(const std::string& text, const std::string& name) {
    const std::regex pattern(name + "=\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) {
        throw std::runtime_error("missing XML attribute: " + name);
    }
    return match[1].str();
}

int AttrInt(const std::string& text, const std::string& name) {
    return std::stoi(Attr(text, name));
}

double AttrDouble(const std::string& text, const std::string& name) {
    return std::stod(Attr(text, name));
}

CollisionScene ParseCollisionXml(const std::filesystem::path& path) {
    const auto text = ReadTextFile(path);
    CollisionScene scene;

    std::smatch headerMatch;
    if (!std::regex_search(text, headerMatch, std::regex(R"(<CollisionHeader[^>]*>)"))) {
        throw std::runtime_error("collision XML missing CollisionHeader");
    }
    const std::string header = headerMatch[0].str();
    scene.BoundsMin = { AttrDouble(header, "MinBoundsX"), AttrDouble(header, "MinBoundsY"),
                        AttrDouble(header, "MinBoundsZ") };
    scene.BoundsMax = { AttrDouble(header, "MaxBoundsX"), AttrDouble(header, "MaxBoundsY"),
                        AttrDouble(header, "MaxBoundsZ") };

    const std::regex vertexPattern(R"(<Vertex\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), vertexPattern), end; it != end; ++it) {
        const std::string node = it->str();
        scene.Vertices.push_back({ AttrDouble(node, "X"), AttrDouble(node, "Y"), AttrDouble(node, "Z") });
    }

    const std::regex polyPattern(R"(<Polygon\s[^>]*/>)");
    for (std::sregex_iterator it(text.begin(), text.end(), polyPattern), end; it != end; ++it) {
        const std::string node = it->str();
        scene.Polygons.push_back({
            AttrInt(node, "Type"),
            AttrInt(node, "VertexA") & 0x1FFF,
            AttrInt(node, "VertexB") & 0x1FFF,
            AttrInt(node, "VertexC") & 0x1FFF,
            AttrInt(node, "NormalX"),
            AttrInt(node, "NormalY"),
            AttrInt(node, "NormalZ"),
            AttrInt(node, "Dist"),
        });
    }

    if (scene.Vertices.empty() || scene.Polygons.empty()) {
        throw std::runtime_error("collision XML contains no vertices or polygons");
    }
    return scene;
}

bool PointInTriXZ(double x, double z, const Vec3& a, const Vec3& b, const Vec3& c) {
    const double v0x = c.X - a.X;
    const double v0z = c.Z - a.Z;
    const double v1x = b.X - a.X;
    const double v1z = b.Z - a.Z;
    const double v2x = x - a.X;
    const double v2z = z - a.Z;
    const double dot00 = v0x * v0x + v0z * v0z;
    const double dot01 = v0x * v1x + v0z * v1z;
    const double dot02 = v0x * v2x + v0z * v2z;
    const double dot11 = v1x * v1x + v1z * v1z;
    const double dot12 = v1x * v2x + v1z * v2z;
    const double denom = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denom) < 1e-8) {
        return false;
    }
    const double inv = 1.0 / denom;
    const double u = (dot11 * dot02 - dot01 * dot12) * inv;
    const double v = (dot00 * dot12 - dot01 * dot02) * inv;
    return u >= -1e-5 && v >= -1e-5 && (u + v) <= 1.00001;
}

std::optional<FloorHit> FloorHitAt(const CollisionScene& scene, double x, double z, double queryY) {
    std::optional<FloorHit> bestHit;
    double bestY = 0.0;
    for (size_t i = 0; i < scene.Polygons.size(); ++i) {
        const auto& poly = scene.Polygons[i];
        if (poly.Ny < 12000) {
            continue;
        }
        if (poly.A < 0 || poly.B < 0 || poly.C < 0 || static_cast<size_t>(poly.A) >= scene.Vertices.size() ||
            static_cast<size_t>(poly.B) >= scene.Vertices.size() || static_cast<size_t>(poly.C) >= scene.Vertices.size()) {
            continue;
        }
        const auto& a = scene.Vertices[poly.A];
        const auto& b = scene.Vertices[poly.B];
        const auto& c = scene.Vertices[poly.C];
        if (!PointInTriXZ(x, z, a, b, c)) {
            continue;
        }
        const double nx = static_cast<double>(poly.Nx) / 32767.0;
        const double ny = static_cast<double>(poly.Ny) / 32767.0;
        const double nz = static_cast<double>(poly.Nz) / 32767.0;
        if (std::abs(ny) < 1e-5) {
            continue;
        }
        const double y = -(nx * x + nz * z + static_cast<double>(poly.Dist)) / ny;
        if (y <= queryY + 96.0 && (!bestHit.has_value() || y > bestY)) {
            bestY = y;
            bestHit = FloorHit{ y, static_cast<int>(i), poly.Type };
        }
    }
    return bestHit;
}

GlbMetadata ReadGlbMetadata(const std::filesystem::path& path) {
    const auto data = ReadBinaryFile(path);
    if (data.size() < 20 || data[0] != 'g' || data[1] != 'l' || data[2] != 'T' || data[3] != 'F') {
        throw std::runtime_error("not a GLB file: " + path.string());
    }
    GlbMetadata meta;
    meta.Version = ReadLe32(data, 4);
    meta.Length = ReadLe32(data, 8);
    if (meta.Version != 2) {
        throw std::runtime_error("not a GLB v2 file: " + path.string());
    }

    nlohmann::json gltf;
    size_t offset = 12;
    while (offset + 8 <= data.size()) {
        const uint32_t chunkLength = ReadLe32(data, offset);
        const uint32_t chunkType = ReadLe32(data, offset + 4);
        offset += 8;
        if (offset + chunkLength > data.size()) {
            throw std::runtime_error("GLB chunk extends past file: " + path.string());
        }
        if (chunkType == 0x4E4F534A) {
            const std::string jsonText(reinterpret_cast<const char*>(data.data() + offset), chunkLength);
            gltf = nlohmann::json::parse(jsonText);
        }
        offset += chunkLength;
    }

    if (!gltf.is_object()) {
        throw std::runtime_error("GLB missing JSON chunk: " + path.string());
    }
    const auto& accessors = gltf.value("accessors", nlohmann::json::array());
    for (const auto& mesh : gltf.value("meshes", nlohmann::json::array())) {
        for (const auto& primitive : mesh.value("primitives", nlohmann::json::array())) {
            if (primitive.contains("attributes") && primitive["attributes"].contains("POSITION")) {
                const auto posIndex = primitive["attributes"]["POSITION"].get<size_t>();
                if (posIndex < accessors.size()) {
                    meta.VertexCount += accessors[posIndex].value("count", 0u);
                }
            }
            if (primitive.contains("indices")) {
                const auto indexAccessor = primitive["indices"].get<size_t>();
                if (indexAccessor < accessors.size()) {
                    meta.TriangleCount += accessors[indexAccessor].value("count", 0u) / 3;
                }
            } else if (primitive.contains("attributes") && primitive["attributes"].contains("POSITION")) {
                const auto posIndex = primitive["attributes"]["POSITION"].get<size_t>();
                if (posIndex < accessors.size()) {
                    meta.TriangleCount += accessors[posIndex].value("count", 0u) / 3;
                }
            }
            if (primitive.contains("targets") && primitive["targets"].is_array()) {
                meta.MorphFrameCount = std::max(meta.MorphFrameCount, primitive["targets"].size() + 1);
            }
        }
    }
    return meta;
}

double Round4(double value) {
    return std::round(value * 10000.0) / 10000.0;
}

void PutPixel(std::vector<uint8_t>& pixels, int width, int height, int x, int y, std::array<uint8_t, 3> color) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
    }
    const size_t offset = static_cast<size_t>((y * width + x) * 3);
    pixels[offset + 0] = color[0];
    pixels[offset + 1] = color[1];
    pixels[offset + 2] = color[2];
}

void DrawLine(std::vector<uint8_t>& pixels, int width, int height, int x0, int y0, int x1, int y1,
              std::array<uint8_t, 3> color) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        PutPixel(pixels, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void DrawCircle(std::vector<uint8_t>& pixels, int width, int height, int cx, int cy, int radius,
                std::array<uint8_t, 3> color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                PutPixel(pixels, width, height, cx + x, cy + y, color);
            }
        }
    }
}

void WritePpm(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& pixels) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("could not write preview PPM: " + path.string());
    }
    file << "P6\n" << width << " " << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
}

nlohmann::json SimulateRuntime(const CollisionScene& scene, const nlohmann::json& manifest, size_t linkFrameCount,
                               std::vector<Vec3>& path) {
    const auto movement = manifest.value("movement", nlohmann::json::object());
    const auto spawn = movement.value("spawn", nlohmann::json::object());
    const double speed = movement.value("speed_units_per_second", 45.0);
    const double radius = movement.value("radius_units", 12.0);
    const double dt = 1.0 / 30.0;
    double x = spawn.value("x", 0.0);
    double z = spawn.value("z", 0.0);
    auto firstFloor = FloorHitAt(scene, x, z, spawn.value("y", 0.0) + 120.0);
    double y = firstFloor.has_value() ? firstFloor->Y : spawn.value("y", 0.0);
    int lastFloorIndex = firstFloor.has_value() ? firstFloor->PolygonIndex : -1;
    int lastSurfaceType = firstFloor.has_value() ? firstFloor->SurfaceType : -1;

    const std::vector<std::array<double, 3>> commands = {
        { 1.0, 0.0, 60.0 },
        { 0.0, -1.0, 45.0 },
        { -1.0, 0.0, 60.0 },
        { 0.0, 1.0, 45.0 },
    };

    nlohmann::json frames = nlohmann::json::array();
    int frame = 0;
    int floorHits = 0;
    int blocked = 0;
    path.push_back({ x, y, z });

    for (const auto& command : commands) {
        const double stickX = command[0];
        const double stickZ = command[1];
        const int count = static_cast<int>(command[2]);
        const double length = std::hypot(stickX, stickZ);
        const double ndx = length > 0.0 ? stickX / length : 0.0;
        const double ndz = length > 0.0 ? stickZ / length : 0.0;
        for (int i = 0; i < count; ++i) {
            const double oldX = x;
            const double oldY = y;
            const double oldZ = z;
            double nextX = x + ndx * speed * dt;
            double nextZ = z + ndz * speed * dt;
            const double unclampedX = nextX;
            const double unclampedZ = nextZ;
            nextX = std::max(scene.BoundsMin.X + radius, std::min(scene.BoundsMax.X - radius, nextX));
            nextZ = std::max(scene.BoundsMin.Z + radius, std::min(scene.BoundsMax.Z - radius, nextZ));
            const auto floor = FloorHitAt(scene, nextX, nextZ, y + 120.0);
            const bool didBlock = !floor.has_value();
            double vx = 0.0;
            double vy = 0.0;
            double vz = 0.0;
            if (didBlock) {
                ++blocked;
            } else {
                x = nextX;
                y = floor->Y;
                z = nextZ;
                vx = (x - oldX) / dt;
                vy = (y - oldY) / dt;
                vz = (z - oldZ) / dt;
                lastFloorIndex = floor->PolygonIndex;
                lastSurfaceType = floor->SurfaceType;
                ++floorHits;
                path.push_back({ x, y, z });
            }
            const double yaw = length > 0.0 ? std::atan2(ndx, ndz) * 180.0 / 3.14159265358979323846 : 0.0;
            frames.push_back({
                { "frame", frame },
                { "dt", dt },
                { "input", { { "stick_x", ndx }, { "stick_z", ndz }, { "buttons", nlohmann::json::array() } } },
                { "position", { Round4(x), Round4(y), Round4(z) } },
                { "rotation", { { "yaw_degrees", Round4(yaw) } } },
                { "velocity", { Round4(vx), Round4(vy), Round4(vz) } },
                { "action_state", didBlock ? "cpp_native_probe_blocked" : "cpp_native_probe_walk" },
                { "animation",
                  {
                      { "source", movement.value("animation_source", "child/anim/nml_run_free.csab") },
                      { "frame", frame % std::max<size_t>(linkFrameCount, 1) },
                      { "frame_count", linkFrameCount },
                  } },
                { "floor",
                  {
                      { "hit", !didBlock },
                      { "polygon_index", didBlock ? -1 : lastFloorIndex },
                      { "surface_type", didBlock ? -1 : lastSurfaceType },
                  } },
                { "collision_flags",
                  {
                      { "floor_hit", !didBlock },
                      { "bounds_clamped", nextX != unclampedX || nextZ != unclampedZ },
                      { "blocked", didBlock },
                  } },
                { "camera",
                  {
                      { "kind", "cpp_native_probe_follow_camera" },
                      { "position", { Round4(x), Round4(y + 76.0), Round4(z + 180.0) } },
                      { "target", { Round4(x), Round4(y + 32.0), Round4(z) } },
                  } },
            });
            ++frame;
        }
    }

    return {
        { "format", kRuntimeTraceFormat },
        { "status", floorHits > 0 && blocked < frame ? "valid" : "invalid" },
        { "schema", "runtime/three_ds_recomp_cpp_native_runtime_probe_trace_v1" },
        { "frame_count", frame },
        { "floor_hit_count", floorHits },
        { "blocked_step_count", blocked },
        { "frames", frames },
    };
}

void RenderCollisionPreview(const CollisionScene& scene, const std::vector<Vec3>& path,
                            const std::filesystem::path& output) {
    constexpr int width = 960;
    constexpr int height = 720;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 3), 242);
    for (size_t i = 0; i < pixels.size(); i += 3) {
        pixels[i + 0] = 245;
        pixels[i + 1] = 244;
        pixels[i + 2] = 238;
    }

    const double spanX = std::max(scene.BoundsMax.X - scene.BoundsMin.X, 1.0);
    const double spanZ = std::max(scene.BoundsMax.Z - scene.BoundsMin.Z, 1.0);
    const double scale = 0.84 * std::min(width / spanX, height / spanZ);
    const auto toScreen = [&](const Vec3& v) {
        const int sx = static_cast<int>((v.X - scene.BoundsMin.X) * scale + width * 0.08);
        const int sy = static_cast<int>((scene.BoundsMax.Z - v.Z) * scale + height * 0.08);
        return std::pair<int, int>{ sx, sy };
    };

    for (const auto& poly : scene.Polygons) {
        if (poly.Ny < 12000 || static_cast<size_t>(poly.A) >= scene.Vertices.size() ||
            static_cast<size_t>(poly.B) >= scene.Vertices.size() || static_cast<size_t>(poly.C) >= scene.Vertices.size()) {
            continue;
        }
        const auto a = toScreen(scene.Vertices[poly.A]);
        const auto b = toScreen(scene.Vertices[poly.B]);
        const auto c = toScreen(scene.Vertices[poly.C]);
        DrawLine(pixels, width, height, a.first, a.second, b.first, b.second, { 74, 87, 96 });
        DrawLine(pixels, width, height, b.first, b.second, c.first, c.second, { 74, 87, 96 });
        DrawLine(pixels, width, height, c.first, c.second, a.first, a.second, { 74, 87, 96 });
    }

    for (size_t i = 1; i < path.size(); ++i) {
        const auto a = toScreen(path[i - 1]);
        const auto b = toScreen(path[i]);
        DrawLine(pixels, width, height, a.first, a.second, b.first, b.second, { 18, 118, 72 });
    }
    if (!path.empty()) {
        const auto start = toScreen(path.front());
        const auto end = toScreen(path.back());
        DrawCircle(pixels, width, height, start.first, start.second, 5, { 35, 89, 165 });
        DrawCircle(pixels, width, height, end.first, end.second, 7, { 18, 118, 72 });
    }

    WritePpm(output, width, height, pixels);
}

std::string JsonStringAt(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object[key].is_string()) {
        throw std::runtime_error("missing string JSON key: " + key);
    }
    return object[key].get<std::string>();
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
        return 64;
    }

    const std::filesystem::path manifestPath(args.ManifestPath);
    const std::filesystem::path outputRoot(args.OutputRoot);
    const auto tracePath = outputRoot / "runtime/three_ds_recomp_native_runtime_trace.json";
    const auto previewPath = outputRoot / "runtime/three_ds_recomp_native_runtime_preview.ppm";
    const auto summaryPath = outputRoot / "runtime/three_ds_recomp_native_runtime_probe.json";

    try {
        const auto manifest = ReadJsonFile(manifestPath);
        const auto assets = manifest.at("assets");
        const auto contractPath = std::filesystem::path(JsonStringAt(assets, "native_resource_contract"));
        const auto contractJson = ReadJsonFile(contractPath);
        const auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(contractJson);
        const auto resourceProbe = ThreeDsRecomp::Oot3d::ProbeNativeDemoResourceFiles(contract);
        if (!resourceProbe.IsValid) {
            throw std::runtime_error("native resource file probe did not pass");
        }

        const auto collisionPath = std::filesystem::path(JsonStringAt(assets, "collision_xml"));
        const auto roomPath = std::filesystem::path(JsonStringAt(assets, "room_glb"));
        const auto linkPath = std::filesystem::path(JsonStringAt(assets, "link_child_runtime_glb"));
        const auto validationPath = std::filesystem::path(JsonStringAt(assets, "link_child_validated_manifest"));

        const auto scene = ParseCollisionXml(collisionPath);
        const auto roomGlb = ReadGlbMetadata(roomPath);
        const auto linkGlb = ReadGlbMetadata(linkPath);
        const auto validationManifest = ReadJsonFile(validationPath);

        std::vector<Vec3> movementPath;
        auto trace = SimulateRuntime(scene, manifest, linkGlb.MorphFrameCount, movementPath);
        WriteJsonFile(tracePath, trace);
        RenderCollisionPreview(scene, movementPath, previewPath);

        const bool traceValid = trace.value("status", "") == "valid";
        nlohmann::json output = {
            { "format", kRuntimeProbeFormat },
            { "status", traceValid ? "valid" : "invalid" },
            { "manifest", args.ManifestPath },
            { "contract", contractPath.string() },
            { "trace", tracePath.string() },
            { "preview_ppm", previewPath.string() },
            { "probe_runtime", "runtime/three_ds_recomp_oot3d_native_runtime_probe_cpp" },
            { "runtime_n64_asset_substitution_used", false },
            { "shipwright_replacement_path_used", false },
            { "current_host_runtime", "standalone_cpp_probe_pending_runtime/three_ds_recomp_window_host" },
            { "claim", "cpp_native_runtime_probe_not_oot3d_movement_parity" },
            { "room",
              {
                  { "vertex_count", roomGlb.VertexCount },
                  { "triangle_count", roomGlb.TriangleCount },
                  { "source", roomPath.string() },
              } },
            { "collision",
              {
                  { "vertex_count", scene.Vertices.size() },
                  { "polygon_count", scene.Polygons.size() },
                  { "source", collisionPath.string() },
              } },
            { "link_child",
              {
                  { "vertex_count", linkGlb.VertexCount },
                  { "triangle_count", linkGlb.TriangleCount },
                  { "morph_frame_count", linkGlb.MorphFrameCount },
                  { "source", linkPath.string() },
                  { "validation_manifest_loaded", validationManifest.is_object() },
              } },
            { "trace_status", trace.value("status", "invalid") },
            { "trace_frame_count", trace.value("frame_count", 0) },
            { "floor_hit_count", trace.value("floor_hit_count", 0) },
            { "blocked_step_count", trace.value("blocked_step_count", 0) },
            { "preview_status", std::filesystem::is_regular_file(previewPath) ? "valid" : "missing" },
            { "issue_count", traceValid ? 0 : 1 },
            { "issues", traceValid ? nlohmann::json::array() : nlohmann::json::array({ "runtime_trace_invalid" }) },
        };

        WriteJsonFile(summaryPath, output);
        std::cout << output.dump(2) << "\n";
        return traceValid ? 0 : 2;
    } catch (const std::exception& ex) {
        nlohmann::json output = {
            { "format", kRuntimeProbeFormat },
            { "status", "invalid" },
            { "manifest", args.ManifestPath },
            { "probe_runtime", "runtime/three_ds_recomp_oot3d_native_runtime_probe_cpp" },
            { "issue_count", 1 },
            { "issues",
              nlohmann::json::array({
                  {
                      { "code", "native_runtime_probe_exception" },
                      { "message", ex.what() },
                  },
              }) },
        };
        try {
            WriteJsonFile(summaryPath, output);
        } catch (...) {
        }
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
