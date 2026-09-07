#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeAssets.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint16_t kPicaTextureRgba = 0x6752;
constexpr uint16_t kPicaTextureAlpha = 0x6756;
constexpr uint16_t kPicaTextureLuminanceAlpha = 0x6758;
constexpr uint16_t kPicaTextureEtc1A4 = 0x675B;

struct Args {
    std::filesystem::path ManifestPath;
    std::filesystem::path OutputPath;
    bool SelfTest = false;
};

using Vec3 = ThreeDsRecomp::Oot3d::Oot3dDemoVec3;
using Bounds = ThreeDsRecomp::Oot3d::Oot3dDemoBounds;
using SceneAssets = ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene;

struct Camera {
    Vec3 Position;
    double Yaw = 0.0;
    double Pitch = -0.18;
    double MoveSpeed = 120.0;
};

struct ModelGlResources {
    std::vector<GLuint> TextureIds;
    std::vector<bool> TextureValid;
    std::vector<bool> TextureHasNativeAlpha;
};

void PrintUsage() {
    std::cerr << "usage: oot3d_native_mesh_viewer --manifest <demo_manifest.json> "
                 "[--self-test --output <summary.json>]\n";
}

bool ParseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc) {
            args.ManifestPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.OutputPath = argv[++i];
        } else if (arg == "--self-test") {
            args.SelfTest = true;
        } else {
            return false;
        }
    }
    return !args.ManifestPath.empty();
}

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write JSON file: " + path.string());
    }
    file << data.dump(2) << "\n";
}

Bounds ModelBounds(const ThreeDsRecomp::Oot3d::CmbModel& model, const Vec3& offset = {}) {
    return ThreeDsRecomp::Oot3d::NativeDemoModelBounds(model, offset);
}

Vec3 BoundsCenter(const Bounds& bounds) {
    return ThreeDsRecomp::Oot3d::NativeDemoBoundsCenter(bounds);
}

void ExpandBoundsByBounds(Bounds& bounds, const Bounds& value) {
    ThreeDsRecomp::Oot3d::NativeDemoExpandBoundsByBounds(bounds, value);
}

double BoundsMaxExtent(const Bounds& bounds) {
    return ThreeDsRecomp::Oot3d::NativeDemoBoundsMaxExtent(bounds);
}

ThreeDsRecomp::Oot3d::Vec3f PosedVertexPosition(const ThreeDsRecomp::Oot3d::CmbPrimitive& primitive,
                                       const ThreeDsRecomp::Oot3d::Vec3f& position, uint32_t vertexIndex,
                                       const ThreeDsRecomp::Oot3d::CsabPose* pose,
                                       const std::vector<ThreeDsRecomp::Oot3d::Matrix4f>* skinTransforms) {
    return ThreeDsRecomp::Oot3d::NativeDemoPosedVertexPosition(primitive, position, vertexIndex, pose, skinTransforms);
}

bool MeshIndexSelected(const std::vector<uint32_t>* selectedMeshIndices, uint32_t meshIndex) {
    if (selectedMeshIndices == nullptr || selectedMeshIndices->empty()) {
        return true;
    }
    return std::find(selectedMeshIndices->begin(), selectedMeshIndices->end(), meshIndex) != selectedMeshIndices->end();
}

SceneAssets LoadSceneAssets(const std::filesystem::path& manifestPath) {
    return ThreeDsRecomp::Oot3d::LoadOot3dNativeDemoSceneFromManifest(manifestPath);
}

nlohmann::json SceneSummary(const SceneAssets& assets) {
    return ThreeDsRecomp::Oot3d::Oot3dNativeDemoSceneSummaryToJson(assets);
}

void Perspective(double fovYDegrees, double aspect, double zNear, double zFar) {
    const double fH = std::tan(fovYDegrees * kPi / 360.0) * zNear;
    const double fW = fH * aspect;
    glFrustum(-fW, fW, -fH, fH, zNear, zFar);
}

Vec3 CameraForward(const Camera& camera) {
    const double cp = std::cos(camera.Pitch);
    return { std::sin(camera.Yaw) * cp, std::sin(camera.Pitch), -std::cos(camera.Yaw) * cp };
}

Vec3 CameraRight(const Camera& camera) {
    return { std::cos(camera.Yaw), 0.0, std::sin(camera.Yaw) };
}

void AddScaled(Vec3& value, const Vec3& delta, double scale) {
    value.X += delta.X * scale;
    value.Y += delta.Y * scale;
    value.Z += delta.Z * scale;
}

void UpdateCamera(GLFWwindow* window, Camera& camera, double dt) {
    const double lookSpeed = 1.8 * dt;
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        camera.Yaw -= lookSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        camera.Yaw += lookSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        camera.Pitch += lookSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        camera.Pitch -= lookSpeed;
    }
    camera.Pitch = std::clamp(camera.Pitch, -1.45, 1.45);

    const double speed = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? camera.MoveSpeed * 2.7
                                                                                : camera.MoveSpeed) *
                         dt;
    const auto forward = CameraForward(camera);
    const auto right = CameraRight(camera);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        AddScaled(camera.Position, forward, speed);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        AddScaled(camera.Position, forward, -speed);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        AddScaled(camera.Position, right, speed);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        AddScaled(camera.Position, right, -speed);
    }
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
        camera.Position.Y += speed;
    }
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {
        camera.Position.Y -= speed;
    }
}

void LookAt(Camera& camera, const Vec3& target) {
    const Vec3 delta{
        target.X - camera.Position.X,
        target.Y - camera.Position.Y,
        target.Z - camera.Position.Z,
    };
    const double length = std::sqrt(delta.X * delta.X + delta.Y * delta.Y + delta.Z * delta.Z);
    if (length <= 0.000001) {
        return;
    }
    camera.Yaw = std::atan2(delta.X, -delta.Z);
    camera.Pitch = std::asin(std::clamp(delta.Y / length, -1.0, 1.0));
}

void ApplyCamera(const Camera& camera) {
    glRotated(-camera.Pitch * 180.0 / kPi, 1.0, 0.0, 0.0);
    glRotated(camera.Yaw * 180.0 / kPi, 0.0, 1.0, 0.0);
    glTranslated(-camera.Position.X, -camera.Position.Y, -camera.Position.Z);
}

void DrawGrid(const Bounds& bounds) {
    if (!bounds.Valid) {
        return;
    }
    const double y = bounds.Min.Y;
    const double minX = std::floor(bounds.Min.X / 100.0) * 100.0;
    const double maxX = std::ceil(bounds.Max.X / 100.0) * 100.0;
    const double minZ = std::floor(bounds.Min.Z / 100.0) * 100.0;
    const double maxZ = std::ceil(bounds.Max.Z / 100.0) * 100.0;
    glColor3d(0.18, 0.20, 0.20);
    glBegin(GL_LINES);
    for (double x = minX; x <= maxX; x += 100.0) {
        glVertex3d(x, y, minZ);
        glVertex3d(x, y, maxZ);
    }
    for (double z = minZ; z <= maxZ; z += 100.0) {
        glVertex3d(minX, y, z);
        glVertex3d(maxX, y, z);
    }
    glEnd();
}

ModelGlResources CreateModelGlResources(const ThreeDsRecomp::Oot3d::CmbModel& model) {
    ModelGlResources resources;
    resources.TextureIds.resize(model.Textures.size(), 0);
    resources.TextureValid.resize(model.Textures.size(), false);
    resources.TextureHasNativeAlpha.resize(model.Textures.size(), false);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t i = 0; i < model.Textures.size(); ++i) {
        const auto& texture = model.Textures[i];
        if (!texture.Rgba8Decoded || texture.Rgba8.size() != static_cast<size_t>(texture.Width) * texture.Height * 4) {
            continue;
        }
        resources.TextureHasNativeAlpha[i] = texture.TextureFormat == kPicaTextureRgba ||
                                             texture.TextureFormat == kPicaTextureAlpha ||
                                             texture.TextureFormat == kPicaTextureLuminanceAlpha ||
                                             texture.TextureFormat == kPicaTextureEtc1A4;
        GLuint textureId = 0;
        glGenTextures(1, &textureId);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.Width, texture.Height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     texture.Rgba8.data());
        resources.TextureIds[i] = textureId;
        resources.TextureValid[i] = true;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return resources;
}

void DestroyModelGlResources(ModelGlResources& resources) {
    for (auto& textureId : resources.TextureIds) {
        if (textureId != 0) {
            glDeleteTextures(1, &textureId);
            textureId = 0;
        }
    }
    resources.TextureIds.clear();
    resources.TextureValid.clear();
    resources.TextureHasNativeAlpha.clear();
}

struct MeshTextureBinding {
    size_t TextureIndex = 0;
    size_t Slot = 0;
    bool Valid = false;
};

MeshTextureBinding PrimaryTextureBinding(const ThreeDsRecomp::Oot3d::CmbModel& model, const ThreeDsRecomp::Oot3d::CmbMesh& mesh) {
    if (mesh.MaterialIndex >= model.Materials.size()) {
        return {};
    }
    const auto& material = model.Materials[mesh.MaterialIndex];
    const size_t mapperCount = material.TextureMappersUsed != 0 ? std::min<size_t>(3, material.TextureMappersUsed) : 3;
    for (size_t slot = 0; slot < mapperCount; ++slot) {
        const int textureIndex = material.TextureMappers[slot].TextureIndex;
        if (textureIndex >= 0 && static_cast<size_t>(textureIndex) < model.Textures.size()) {
            return { static_cast<size_t>(textureIndex), slot, true };
        }
    }
    return {};
}

bool BindMeshTexture(const ThreeDsRecomp::Oot3d::CmbModel& model, const ModelGlResources& resources,
                     const ThreeDsRecomp::Oot3d::CmbMesh& mesh, MeshTextureBinding& binding) {
    binding = PrimaryTextureBinding(model, mesh);
    if (!binding.Valid || binding.TextureIndex >= resources.TextureValid.size() ||
        !resources.TextureValid[binding.TextureIndex]) {
        glDisable(GL_TEXTURE_2D);
        return false;
    }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, resources.TextureIds[binding.TextureIndex]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    return true;
}

void ConfigureAlphaTest(const ModelGlResources& resources, const ThreeDsRecomp::Oot3d::CmbMaterial* material,
                        MeshTextureBinding binding, bool textured, bool wire) {
    if (wire) {
        glDisable(GL_ALPHA_TEST);
        return;
    }

    const bool materialAlphaTest = material != nullptr && material->AlphaTest;
    const bool textureHasNativeAlpha = textured && binding.Valid &&
                                       binding.TextureIndex < resources.TextureHasNativeAlpha.size() &&
                                       resources.TextureHasNativeAlpha[binding.TextureIndex];
    if (!materialAlphaTest && !textureHasNativeAlpha) {
        glDisable(GL_ALPHA_TEST);
        return;
    }

    float reference = 0.5f;
    if (materialAlphaTest && material->AlphaReference > 0) {
        reference = std::max(reference, static_cast<float>(material->AlphaReference) / 255.0f);
    }
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, reference);
}

GLenum GlCompareFunction(uint16_t function) {
    switch (function) {
        case 0x0200:
            return GL_NEVER;
        case 0x0201:
            return GL_LESS;
        case 0x0202:
            return GL_EQUAL;
        case 0x0203:
            return GL_LEQUAL;
        case 0x0204:
            return GL_GREATER;
        case 0x0205:
            return GL_NOTEQUAL;
        case 0x0206:
            return GL_GEQUAL;
        case 0x0207:
            return GL_ALWAYS;
        default:
            return GL_LESS;
    }
}

void ConfigureDepthState(const ThreeDsRecomp::Oot3d::CmbMaterial* material, bool wire) {
    if (wire) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        return;
    }
    if (material != nullptr && !material->DepthTest) {
        glDisable(GL_DEPTH_TEST);
    } else {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(material != nullptr ? GlCompareFunction(material->DepthFunction) : GL_LESS);
    }
    glDepthMask(material == nullptr || material->DepthWrite ? GL_TRUE : GL_FALSE);
}

ThreeDsRecomp::Oot3d::Vec2f ApplyTextureCoord(const ThreeDsRecomp::Oot3d::CmbMaterial* material, MeshTextureBinding binding,
                                     ThreeDsRecomp::Oot3d::Vec2f uv) {
    if (material == nullptr || !binding.Valid || binding.Slot >= 3) {
        return uv;
    }
    if (material->TextureCoordsUsed != 0 && binding.Slot >= material->TextureCoordsUsed) {
        return uv;
    }
    const auto& coord = material->TextureCoords[binding.Slot];
    if (coord.CoordinateIndex != 0) {
        return uv;
    }

    const double cosR = std::cos(coord.Rotation);
    const double sinR = std::sin(coord.Rotation);
    const double s = coord.Scale.X *
                     (cosR * uv.X - sinR * uv.Y + 0.5 * sinR - 0.5 * cosR + 0.5 - coord.Translation.X);
    const double t = coord.Scale.Y *
                     (sinR * uv.X + cosR * uv.Y - 0.5 * sinR - 0.5 * cosR + 0.5 - coord.Translation.Y);
    return { static_cast<float>(s), static_cast<float>(t) };
}

void SetVertexColor(const ThreeDsRecomp::Oot3d::CmbShape& shape, uint32_t index, double r, double g, double b, double shade,
                    bool textured) {
    if (index < shape.Colors.size()) {
        const auto& color = shape.Colors[index];
        if (textured) {
            glColor4d(1.0, 1.0, 1.0, 1.0);
        } else {
            const double alpha = color.A <= 1 ? 1.0 : static_cast<double>(color.A) / 255.0;
            glColor4d((static_cast<double>(color.R) / 255.0) * r * shade,
                      (static_cast<double>(color.G) / 255.0) * g * shade,
                      (static_cast<double>(color.B) / 255.0) * b * shade,
                      alpha);
        }
        return;
    }
    if (textured) {
        glColor4d(1.0, 1.0, 1.0, 1.0);
    } else {
        glColor4d(r * shade, g * shade, b * shade, 1.0);
    }
}

void EmitVertex(const ThreeDsRecomp::Oot3d::CmbShape& shape, const ThreeDsRecomp::Oot3d::CmbPrimitive& primitive,
                const ThreeDsRecomp::Oot3d::CmbMaterial* material, MeshTextureBinding binding, uint32_t index,
                const Vec3& offset, double modelScale, double r, double g, double b, double shade, bool textured,
                bool wire, const ThreeDsRecomp::Oot3d::CsabPose* pose,
                const std::vector<ThreeDsRecomp::Oot3d::Matrix4f>* skinTransforms) {
    if (wire) {
        glColor4d(r * shade, g * shade, b * shade, 1.0);
    } else {
        SetVertexColor(shape, index, r, g, b, shade, textured);
    }

    ThreeDsRecomp::Oot3d::Vec2f uv{};
    if (index < shape.Uv0.size()) {
        uv = ApplyTextureCoord(material, binding, shape.Uv0[index]);
    }
    glTexCoord2f(uv.X, 1.0f - uv.Y);

    const auto position = PosedVertexPosition(primitive, shape.Positions[index], index, pose, skinTransforms);
    glVertex3d(position.X * modelScale + offset.X, position.Y * modelScale + offset.Y,
               position.Z * modelScale + offset.Z);
}

void DrawModel(const ThreeDsRecomp::Oot3d::CmbModel& model, const ModelGlResources& resources, const Vec3& offset,
               const std::vector<uint32_t>* selectedMeshIndices, const ThreeDsRecomp::Oot3d::CsabPose* pose,
               const std::vector<ThreeDsRecomp::Oot3d::Matrix4f>* skinTransforms, double modelScale, double r, double g,
               double b, bool wire) {
    if (wire) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_TEXTURE_2D);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    for (const auto& mesh : model.Meshes) {
        if (!MeshIndexSelected(selectedMeshIndices, mesh.Index) || mesh.ShapeIndex >= model.Shapes.size()) {
            continue;
        }
        const auto& shape = model.Shapes[mesh.ShapeIndex];
        const auto* material = mesh.MaterialIndex < model.Materials.size() ? &model.Materials[mesh.MaterialIndex] : nullptr;
        const double shade = 0.72 + 0.22 * static_cast<double>((mesh.Index % 7)) / 6.0;
        MeshTextureBinding textureBinding;
        const bool textured = !wire && BindMeshTexture(model, resources, mesh, textureBinding);
        ConfigureDepthState(material, wire);
        ConfigureAlphaTest(resources, material, textureBinding, textured, wire);
        for (const auto& primitive : shape.Primitives) {
            glBegin(GL_TRIANGLES);
            for (size_t i = 0; i + 2 < primitive.Indices.size(); i += 3) {
                const uint32_t ia = primitive.Indices[i + 0];
                const uint32_t ib = primitive.Indices[i + 1];
                const uint32_t ic = primitive.Indices[i + 2];
                if (ia >= shape.Positions.size() || ib >= shape.Positions.size() || ic >= shape.Positions.size()) {
                    continue;
                }
                EmitVertex(shape, primitive, material, textureBinding, ia, offset, modelScale, r, g, b, shade,
                           textured, wire, pose, skinTransforms);
                EmitVertex(shape, primitive, material, textureBinding, ib, offset, modelScale, r, g, b, shade,
                           textured, wire, pose, skinTransforms);
                EmitVertex(shape, primitive, material, textureBinding, ic, offset, modelScale, r, g, b, shade,
                           textured, wire, pose, skinTransforms);
            }
            glEnd();
        }
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void DrawAxisAt(const Vec3& origin, double size) {
    glBegin(GL_LINES);
    glColor3d(0.90, 0.15, 0.12);
    glVertex3d(origin.X, origin.Y, origin.Z);
    glVertex3d(origin.X + size, origin.Y, origin.Z);
    glColor3d(0.12, 0.75, 0.24);
    glVertex3d(origin.X, origin.Y, origin.Z);
    glVertex3d(origin.X, origin.Y + size, origin.Z);
    glColor3d(0.15, 0.32, 0.95);
    glVertex3d(origin.X, origin.Y, origin.Z);
    glVertex3d(origin.X, origin.Y, origin.Z + size);
    glEnd();
}

int RunViewer(const SceneAssets& assets) {
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit failed");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    GLFWwindow* window =
        glfwCreateWindow(1280, 720, "OOT3D native mesh viewer - room + child Link", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    auto roomResources = CreateModelGlResources(assets.RoomModel);
    auto linkResources = CreateModelGlResources(assets.LinkModel);
    const auto roomBounds = ModelBounds(assets.RoomModel);
    Bounds sceneBounds;
    ExpandBoundsByBounds(sceneBounds, roomBounds);
    ExpandBoundsByBounds(sceneBounds, assets.LinkBounds);
    const double sceneExtent = std::max(300.0, BoundsMaxExtent(sceneBounds));
    const double nearPlane = 1.0;
    const double farPlane = std::max(1600.0, sceneExtent * 4.0);
    const Vec3 target = assets.LinkBounds.Valid ? BoundsCenter(assets.LinkBounds)
                                                : (roomBounds.Valid ? BoundsCenter(roomBounds) : assets.Spawn);
    Camera camera;
    camera.MoveSpeed = std::max(80.0, sceneExtent * 0.18);
    camera.Position = {
        target.X,
        target.Y + std::max(85.0, sceneExtent * 0.10),
        target.Z + std::max(260.0, sceneExtent * 0.32),
    };
    LookAt(camera, { target.X, target.Y + std::max(30.0, sceneExtent * 0.035), target.Z });

    double lastTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        const double now = glfwGetTime();
        const double dt = std::min(0.05, now - lastTime);
        lastTime = now;
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        UpdateCamera(window, camera, dt);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        const double aspect = height > 0 ? static_cast<double>(width) / static_cast<double>(height) : 1.0;
        glViewport(0, 0, width, height);
        glClearColor(0.04f, 0.045f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        Perspective(62.0, aspect, nearPlane, farPlane);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        ApplyCamera(camera);

        DrawGrid(roomBounds);
        DrawModel(assets.RoomModel, roomResources, {}, nullptr, nullptr, nullptr, 1.0, 0.46, 0.55, 0.62, false);
        DrawModel(assets.RoomModel, roomResources, {}, nullptr, nullptr, nullptr, 1.0, 0.08, 0.10, 0.12, true);
        DrawModel(assets.LinkModel, linkResources, assets.LinkOffset, &assets.LinkMeshIndices,
                  &assets.LinkStandingPose, &assets.LinkSkinTransforms, assets.LinkScale, 0.90, 0.74, 0.34, false);
        DrawModel(assets.LinkModel, linkResources, assets.LinkOffset, &assets.LinkMeshIndices,
                  &assets.LinkStandingPose, &assets.LinkSkinTransforms, assets.LinkScale, 0.15, 0.10, 0.05, true);
        DrawAxisAt(assets.Spawn, std::max(42.0, sceneExtent * 0.035));

        glfwSwapBuffers(window);
    }

    DestroyModelGlResources(linkResources);
    DestroyModelGlResources(roomResources);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
        return 64;
    }

    try {
        const auto assets = LoadSceneAssets(args.ManifestPath);
        const auto summary = SceneSummary(assets);
        if (args.SelfTest) {
            if (!args.OutputPath.empty()) {
                WriteJsonFile(args.OutputPath, summary);
            }
            std::cout << summary.dump(2) << "\n";
            return 0;
        }

        std::cout << summary.dump(2) << "\n";
        std::cout << "Controls: WASD move, Q/E down/up, arrows look, Shift fast, Esc quit\n";
        return RunViewer(assets);
    } catch (const std::exception& ex) {
        nlohmann::json error = {
            { "format", "oot3d_native_mesh_viewer_v1" },
            { "status", "invalid" },
            { "issue_count", 1 },
            { "issues", nlohmann::json::array({ { { "code", "native_mesh_viewer_exception" }, { "message", ex.what() } } }) },
        };
        if (args.SelfTest && !args.OutputPath.empty()) {
            try {
                WriteJsonFile(args.OutputPath, error);
            } catch (...) {
            }
        }
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
