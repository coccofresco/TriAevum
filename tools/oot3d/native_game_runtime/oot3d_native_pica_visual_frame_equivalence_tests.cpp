// Equivalence and cost of the render-thread visual-frame preparation (the
// fps60 path that runs before the first draw of every host frame): the draw
// matching between the latest and the new guest frame, the transition
// analysis and the interpolated samples, against a frozen copy of the code it
// replaced (oot3d_native_pica_visual_frame_reference.inc).
//
//   [scale]        equivalence (scale 0: quick run for the sanitizers): thousands of random frames (scenes of 1-420
//                  draws with actors, skinned palettes, repeated draws,
//                  ambiguous groups, swapped pieces, reorders, insertions and
//                  deletions); every pair, every transition statistic and
//                  every sampled view (uniforms bit for bit, viewport, blend,
//                  light binding, vertex bytes) must be identical.
//   --threads      the same on 4 threads at once, each sampling every
//                  transition on another thread than the one that prepared
//                  it (as the render thread may sample what was prepared
//                  earlier): for TSan, no hidden shared or static state.
//   --bench [N]    cost per guest frame (Prepare + samples at 0 and 1/2, as
//                  the fps60 step and in-between frames do) for scenes of 200
//                  and 400 draws, reference vs. current.
//
// Run it under ASan and TSan too (the prepared transition owns scratch that
// is reused between frames).
//
// Ported from the TriAevum-Switch fork (6e0eecbf); the oracle is our own
// pre-rewrite code. Actor and lit-room draws also carry a native camera
// (LightBinding.WorldToView), so the rigid model-view camera path
// (InterpolatePicaRigidCameraMatrix) is compared too.
#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_visual_frame_reference.inc"
#include "oot3d_native_pica_affine_layout.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include <future>
#include <thread>

namespace {

using namespace Oot3dNativeGame;
namespace Ref = Oot3dNativeGame::VisualFrameReference;

[[noreturn]] void Fail(const std::string& message) {
    std::fprintf(stderr, "oot3d_native_pica_visual_frame_equivalence_tests: %s\n", message.c_str());
    std::exit(1);
}

void Require(bool condition, const std::string& message) {
    if (!condition) Fail(message);
}

constexpr uint32_t kTopColor = 0x18000000U;
constexpr uint32_t kShadowColor = 0x18400000U;

using Rows = std::array<std::array<float, 4>, 3>;

Rows Rotation(float angle, float x, float y, float z, int axis = 2) {
    const float c = std::cos(angle), s = std::sin(angle);
    Rows rows{};
    if (axis == 2) rows = {{{c, -s, 0, x}, {s, c, 0, y}, {0, 0, 1, z}}};
    else if (axis == 1) rows = {{{c, 0, s, x}, {0, 1, 0, y}, {-s, 0, c, z}}};
    else rows = {{{1, 0, 0, x}, {0, c, -s, y}, {0, s, c, z}}};
    return rows;
}

void Put(Oot3dPicaVulkanDrawPlan& plan, size_t first, const Rows& rows) {
    for (size_t i = 0; i < 3; ++i) plan.VertexShader.Uniforms.Floats[first + i] = rows[i];
}

struct Rng {
    std::mt19937_64 Engine;
    explicit Rng(uint64_t seed) : Engine(seed) {}
    uint64_t Below(uint64_t n) { return n == 0 ? 0 : Engine() % n; }
    bool Chance(double p) { return std::uniform_real_distribution<double>(0.0, 1.0)(Engine) < p; }
    float Real(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(Engine); }
};

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

// A persistent scene: room geometry, actors drawn in skinned pieces,
// repeated particle draws, CPU-animated meshes, light snapshots. Every
// guest frame is produced from it with smooth motion, like the game.
struct Scene {
    struct Room {
        uint64_t VertexKey = 0, FragmentKey = 0;
        size_t Vertices = 0, Indices = 0, Texture = 0;
        uint32_t VertexCount = 0;
        float Phase = 0;
        bool Cmb = false;
        bool Lit = false;
        bool Offscreen = false;
        bool Versioned = false;
    };
    struct Piece {
        size_t Bones = 0;          // shared bone-index buffer
        size_t Vertices = 0, Indices = 0;
        uint32_t VertexCount = 0;
        uint8_t Influences = 1;
        uint64_t Part = 0, Slot = 0, VertexKey = 0;
    };
    struct Actor {
        uint64_t Owner = 0, Instance = 0, Lifetime = 1;
        bool Native = true;
        float X = 0, Y = 0, Heading = 0, Speed = 0;
        std::vector<Piece> Pieces;
        size_t Variant = 0;        // a swapped piece (open/closed hand)
    };
    struct Particles {
        uint64_t VertexKey = 0;
        size_t Count = 0;
        size_t Vertices = 0;
        bool PerInstance = false;
    };

    std::vector<Bytes> VertexBuffers, IndexBuffers, BoneBuffers, Textures;
    std::shared_ptr<const Oot3dPicaTemporalVertexProgram> CmbProgram, PlainProgram;
    std::vector<Room> Rooms;
    std::vector<Actor> Actors;
    std::vector<Particles> Emitters;
    size_t AnimatedMeshes = 0;
    uint64_t Frame = 0;
    float Camera = 0;
    uint64_t NextSubmission = 1;
    uint64_t NextLifetime = 2;

    Scene(Rng& rng, size_t targetDraws) {
        auto cmb = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        cmb->AffineLayout = kOot3dCmbAffineLayout;
        cmb->CmbTextureCoordinates = {true, true, true};
        CmbProgram = cmb;
        PlainProgram = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        for (size_t i = 0; i < 48; ++i) {
            const size_t vertices = 64 + rng.Below(600);
            std::vector<uint8_t> bytes(vertices * 32U);
            for (auto& b : bytes) b = static_cast<uint8_t>(rng.Below(256));
            VertexBuffers.push_back(std::make_shared<const std::vector<uint8_t>>(std::move(bytes)));
            std::vector<uint8_t> indices((64 + rng.Below(1400)) * 2U);
            for (size_t k = 0; k < indices.size(); k += 2) {
                const auto v = static_cast<uint16_t>(rng.Below(vertices));
                indices[k] = static_cast<uint8_t>(v);
                indices[k + 1] = static_cast<uint8_t>(v >> 8);
            }
            IndexBuffers.push_back(std::make_shared<const std::vector<uint8_t>>(std::move(indices)));
            Textures.push_back(std::make_shared<const std::vector<uint8_t>>(64U, static_cast<uint8_t>(i)));
        }
        for (size_t i = 0; i < 24; ++i) {
            // Bone indices of a skinned piece: up to 4 influences per vertex,
            // within the 10-matrix palette (some pieces use only a few bones).
            const size_t span = 2 + rng.Below(9);
            const size_t base = rng.Below(10 - std::min<size_t>(span, 9));
            std::vector<uint8_t> bones(700U * 4U);
            for (auto& b : bones) b = static_cast<uint8_t>(base + rng.Below(span));
            BoneBuffers.push_back(std::make_shared<const std::vector<uint8_t>>(std::move(bones)));
        }
        const size_t actorDraws = targetDraws * 3 / 10;
        const size_t particleDraws = targetDraws / 6;
        AnimatedMeshes = std::max<size_t>(1, targetDraws / 60);
        const size_t roomDraws = targetDraws > actorDraws + particleDraws + AnimatedMeshes
                                     ? targetDraws - actorDraws - particleDraws - AnimatedMeshes
                                     : 1;
        for (size_t i = 0; i < roomDraws; ++i) {
            Room room;
            // Repeated meshes (same keys, buffers): equal strict identities.
            const bool repeat = !Rooms.empty() && rng.Chance(0.15);
            if (repeat) room = Rooms[rng.Below(Rooms.size())];
            else {
                room.VertexKey = 1 + rng.Below(6);
                room.FragmentKey = 100 + rng.Below(14);
                room.Vertices = rng.Below(VertexBuffers.size());
                room.Indices = rng.Below(IndexBuffers.size());
                room.Texture = rng.Below(Textures.size());
                room.VertexCount = static_cast<uint32_t>(IndexBuffers[room.Indices]->size() / 2U);
                room.Cmb = rng.Chance(0.7);
                room.Lit = rng.Chance(0.2);
                room.Offscreen = rng.Chance(0.05);
                room.Versioned = rng.Chance(0.5);
            }
            room.Phase = rng.Real(0, 6.28F);
            Rooms.push_back(room);
        }
        size_t made = 0;
        uint64_t owner = 0x1000;
        while (made < actorDraws) {
            Actor actor;
            actor.Owner = owner++;
            actor.Instance = actor.Owner * 3;
            actor.Native = rng.Chance(0.85);
            actor.X = rng.Real(-300, 300);
            actor.Y = rng.Real(-300, 300);
            actor.Heading = rng.Real(0, 6.28F);
            actor.Speed = rng.Real(0, 3);
            const size_t pieces = 1 + rng.Below(8);
            for (size_t p = 0; p < pieces; ++p) {
                Piece piece;
                piece.Bones = rng.Below(BoneBuffers.size());
                piece.Vertices = rng.Below(VertexBuffers.size());
                piece.Indices = rng.Below(IndexBuffers.size());
                piece.VertexCount = static_cast<uint32_t>(
                    std::min<size_t>(IndexBuffers[piece.Indices]->size() / 2U, 700));
                piece.Influences = static_cast<uint8_t>(1 + rng.Below(4));
                piece.Part = 0x100 + p;
                piece.Slot = p;
                piece.VertexKey = 20 + piece.Influences;
                actor.Pieces.push_back(piece);
            }
            made += pieces;
            Actors.push_back(std::move(actor));
        }
        for (size_t made = 0; made < particleDraws;) {
            Particles emitter;
            emitter.VertexKey = 40 + rng.Below(3);
            emitter.Count = 2 + rng.Below(24);
            emitter.Vertices = rng.Below(VertexBuffers.size());
            emitter.PerInstance = rng.Chance(0.5);
            made += emitter.Count;
            Emitters.push_back(emitter);
        }
    }

    Oot3dPicaVulkanDrawPlan Base(uint64_t vertexKey, uint64_t fragmentKey) {
        Oot3dPicaVulkanDrawPlan plan;
        plan.SubmissionId = NextSubmission++;
        plan.CompositionDomain = Oot3dPicaCompositionDomain::Scene;
        plan.Composition.Layer = Oot3dPicaCompositionLayer::OpaqueWorld;
        plan.VertexShader.StateKey = vertexKey;
        plan.FragmentShader.StateKey = fragmentKey;
        plan.State.Framebuffer.ColorPhysicalAddress = kTopColor;
        plan.State.Framebuffer.DepthPhysicalAddress = kTopColor + 0x100000U;
        plan.State.Framebuffer.Width = 240U;
        plan.State.Framebuffer.Height = 400U;
        plan.State.Viewport.HalfWidth = 120.0F;
        plan.State.Viewport.HalfHeight = 200.0F;
        plan.State.Viewport.DepthRange = 1.0F;
        auto& u = plan.VertexShader.Uniforms;
        // Projection (constant), selectors and the CMB material rows.
        u.Floats[0] = {1.2F, 0, 0, 0};
        u.Floats[1] = {0, 2.1F, 0, 0};
        u.Floats[2] = {0, 0, -1.0F, -2.0F};
        u.Floats[3] = {0, 0, -1.0F, 0};
        u.Floats[89] = {0, 0, 0, 0};
        u.Floats[93] = {0, 1, 2, 3};
        u.Floats[95] = {3, 4, 5, 32};
        for (size_t row : {10U, 11U, 14U, 15U, 17U, 18U}) u.Floats[row] = {1, 0, 0, 0};
        plan.FragmentShader.Uniforms.TevConstants[0] = {0.5F, 0.5F, 0.5F, 1.0F};
        return plan;
    }

    void AddMesh(Oot3dPicaVulkanDrawPlan& plan, size_t vertices, size_t indices, uint32_t vertexCount,
                 bool versioned) {
        Oot3dPicaVulkanVertexBinding binding;
        binding.Binding = 0;
        binding.ByteStride = 32;
        binding.SharedBytes = VertexBuffers[vertices];
        binding.SourcePhysicalAddress = 0x20000000U + static_cast<uint32_t>(vertices) * 0x10000U;
        binding.ContentVersion = 7;
        binding.ContentVersionAvailable = versioned;
        plan.VertexBindings.push_back(std::move(binding));
        plan.VertexAttributes.push_back({0, 0, Oot3dPicaVertexFormat::Float, 3, 0});
        plan.VertexAttributes.push_back({1, 0, Oot3dPicaVertexFormat::SignedShort, 2, 12});
        plan.VertexAttributes.push_back({2, 0, Oot3dPicaVertexFormat::UnsignedByte, 4, 16});
        plan.Indexed = true;
        plan.IndicesAre16Bit = true;
        plan.SharedIndexBytes = IndexBuffers[indices];
        plan.IndexPhysicalAddress = 0x22000000U + static_cast<uint32_t>(indices) * 0x10000U;
        plan.IndexContentVersion = 3;
        plan.IndexContentVersionAvailable = versioned;
        plan.VertexCount = vertexCount;
        plan.State.VertexInput.PhysicalBaseAddress = 0x20000000U + static_cast<uint32_t>(vertices) * 0x10000U;
        plan.State.VertexInput.IndexPhysicalAddress = plan.IndexPhysicalAddress;
    }

    void AddTexture(Oot3dPicaVulkanDrawPlan& plan, size_t texture) {
        Oot3dPicaVulkanTextureBinding binding;
        binding.Slot = 0;
        binding.State.Enabled = true;
        binding.State.PhysicalAddress = 0x21000000U + static_cast<uint32_t>(texture) * 0x4000U;
        binding.State.Width = 64;
        binding.State.Height = 64;
        binding.SharedNativeBytes = Textures[texture];
        plan.Textures.push_back(std::move(binding));
    }

    // One guest frame of the scene.
    Oot3dPicaVisualFrame Next(uint64_t sequence, Rng& rng, bool chaos) {
        ++Frame;
        Camera += 0.013F;
        const float t = static_cast<float>(Frame) * 0.05F;
        Oot3dPicaVisualFrame frame;
        frame.Sequence = sequence;
        auto lights = std::make_shared<Fast::Renderer3ds::PicaScenePointLights>();
        lights->Count = 3;
        for (uint32_t i = 0; i < lights->Count; ++i) {
            auto& light = lights->Lights[i];
            light.SourceIdentity = 0x500 + i;
            light.Group = i % 2;
            light.PositionWorld = {std::sin(t + i) * 50, std::cos(t + i) * 50, 10.0F * i};
            light.Color = {1, 0.8F, 0.5F};
            light.Radius = 80 + 5 * std::sin(t);
        }
        const std::shared_ptr<const Fast::Renderer3ds::PicaScenePointLights> scenePoints = lights;
        const Rows view = Rotation(Camera, std::sin(Camera) * 40, 0, -200, 1);
        // The native world-to-view (column-major, rigid) captured with lit draws.
        std::array<float, 16> worldToView{};
        for (size_t i = 0; i < 3; ++i)
            for (size_t j = 0; j < 4; ++j) worldToView[j * 4 + i] = view[i][j];
        worldToView[15] = 1.0F;

        for (const auto& room : Rooms) {
            auto plan = Base(room.VertexKey, room.FragmentKey);
            if (room.Cmb) plan.VertexShader.TemporalProgram = CmbProgram;
            AddMesh(plan, room.Vertices, room.Indices, room.VertexCount, room.Versioned);
            AddTexture(plan, room.Texture);
            auto modelView = view;
            modelView[0][3] += room.Phase * 10;
            Put(plan, 4, modelView);
            if (room.Offscreen) {
                plan.State.Framebuffer.ColorPhysicalAddress = kShadowColor;
                plan.Composition.Layer = Oot3dPicaCompositionLayer::Unknown;
            }
            if (room.Lit) {
                plan.LightBinding.ScenePoints = scenePoints;
                plan.LightBinding.WorldToView = worldToView;
                plan.FragmentShader.Uniforms.TevConstants[1] = {std::sin(t + room.Phase), 0.5F, 0.25F, 1};
            }
            frame.Draws.push_back(std::move(plan));
        }
        for (auto& actor : Actors) {
            actor.Heading += 0.02F * actor.Speed;
            actor.X += std::cos(actor.Heading) * actor.Speed;
            actor.Y += std::sin(actor.Heading) * actor.Speed;
            if (rng.Chance(0.04)) actor.Variant = rng.Below(actor.Pieces.size() + 1);
            if (rng.Chance(0.004)) actor.Lifetime = NextLifetime++;   // respawn
            const Rows modelView = Rotation(actor.Heading + Camera, actor.X, actor.Y, -150);
            std::array<Rows, 10> bones;
            for (size_t b = 0; b < bones.size(); ++b)
                bones[b] = Rotation(std::sin(t * (1 + 0.3F * b) + actor.Owner) * 0.6F, b * 3.0F, 0, 0,
                                    static_cast<int>(b % 3));
            for (size_t p = 0; p < actor.Pieces.size(); ++p) {
                const auto& piece = actor.Pieces[p];
                const bool swapped = actor.Variant == p + 1;
                auto plan = Base(piece.VertexKey, 120 + p % 3);
                plan.VertexShader.TemporalProgram = CmbProgram;
                AddMesh(plan, piece.Vertices, piece.Indices, piece.VertexCount, true);
                AddTexture(plan, (piece.Vertices + p) % Textures.size());
                auto& u = plan.VertexShader.Uniforms;
                u.BooleanMask = 4U | (piece.Influences > 1 ? 8U : 0U);
                u.Floats[89] = {0, 0, 0, 1};
                u.Floats[92] = {0, 0, 0, static_cast<float>(std::max<uint8_t>(piece.Influences, 2))};
                Put(plan, 4, modelView);
                for (size_t b = 0; b < bones.size(); ++b) Put(plan, 20 + 3 * b, bones[b]);
                Oot3dPicaVulkanVertexBinding bonesBinding;
                bonesBinding.Binding = 1;
                bonesBinding.ByteStride = 4;
                bonesBinding.SharedBytes = BoneBuffers[swapped ? (piece.Bones + 1) % BoneBuffers.size()
                                                               : piece.Bones];
                plan.VertexBindings.push_back(std::move(bonesBinding));
                plan.VertexAttributes.push_back({6, 1, Oot3dPicaVertexFormat::UnsignedByte,
                                                 std::max<uint8_t>(piece.Influences, 2), 0});
                if (actor.Native) {
                    plan.Composition.TemporalIdentity = {actor.Owner, actor.Instance,
                                                         piece.Part + (swapped ? 0x40U : 0U), piece.Slot,
                                                         actor.Lifetime};
                }
                plan.Composition.NativeActorGeometry = true;
                plan.Composition.NativeActorIdentity = actor.Owner;
                if (p == 0) {
                    plan.LightBinding.ScenePoints = scenePoints;
                    plan.LightBinding.WorldToView = worldToView;
                }
                frame.Draws.push_back(std::move(plan));
            }
        }
        for (auto& emitter : Emitters) {
            if (rng.Chance(0.2)) {
                emitter.Count = static_cast<size_t>(std::clamp<int64_t>(
                    static_cast<int64_t>(emitter.Count) + static_cast<int64_t>(rng.Below(5)) - 2, 1, 40));
            }
            for (size_t i = 0; i < emitter.Count; ++i) {
                auto plan = Base(emitter.VertexKey, 140);
                plan.Composition.Layer = Oot3dPicaCompositionLayer::Unknown;
                Put(plan, 4, view);
                Oot3dPicaVulkanVertexBinding quad;
                quad.Binding = 0;
                quad.ByteStride = 16;
                quad.SharedBytes = VertexBuffers[emitter.Vertices];
                plan.VertexBindings.push_back(std::move(quad));
                plan.VertexAttributes.push_back({0, 0, Oot3dPicaVertexFormat::Float, 3, 0});
                plan.VertexCount = 6;
                if (emitter.PerInstance) {
                    Oot3dPicaVulkanVertexBinding instance;
                    instance.Binding = 1;
                    instance.ByteStride = 12;
                    instance.InputRate = Oot3dPicaVertexInputRate::PerInstance;
                    std::array<float, 3> position{std::sin(t + i) * 30, std::cos(t * 0.7F + i) * 30, 5};
                    instance.Bytes.resize(sizeof(position));
                    std::memcpy(instance.Bytes.data(), position.data(), sizeof(position));
                    plan.VertexBindings.push_back(std::move(instance));
                    plan.VertexAttributes.push_back({3, 1, Oot3dPicaVertexFormat::Float, 3, 0});
                }
                plan.FragmentShader.Uniforms.TevConstants[2] = {1, 1, 1, std::fabs(std::sin(t + i))};
                frame.Draws.push_back(std::move(plan));
            }
        }
        for (size_t m = 0; m < AnimatedMeshes; ++m) {
            auto plan = Base(50, 150);
            Put(plan, 4, view);
            Oot3dPicaVulkanVertexBinding vertices;
            vertices.Binding = 0;
            vertices.ByteStride = 12;
            const size_t count = 120 + 40 * m;
            vertices.Bytes.resize(count * 12U);
            for (size_t v = 0; v < count; ++v) {
                std::array<float, 3> p{static_cast<float>(v), std::sin(t + v * 0.1F) * 4,
                                       static_cast<float>(m)};
                std::memcpy(vertices.Bytes.data() + v * 12U, p.data(), 12U);
            }
            plan.VertexBindings.push_back(std::move(vertices));
            plan.VertexAttributes.push_back({0, 0, Oot3dPicaVertexFormat::Float, 3, 0});
            plan.Indexed = true;
            plan.IndicesAre16Bit = false;
            plan.IndexBytes.resize(count);
            for (size_t v = 0; v < count; ++v) plan.IndexBytes[v] = static_cast<uint8_t>(v % 250);
            plan.VertexCount = static_cast<uint32_t>(count);
            plan.State.Viewport.HalfWidth = 120.0F + std::sin(t) * 0.5F * static_cast<float>(m % 2);
            frame.Draws.push_back(std::move(plan));
        }

        if (chaos) Disturb(frame, rng);
        frame.TopTransfer.CompletionId = sequence;
        frame.TopTransfer.InputPhysicalAddress = kTopColor;
        frame.TopTransfer.AfterDrawSubmissionId = NextSubmission - 1;
        if (!rng.Chance(chaos ? 0.2 : 0.0)) {
            for (const auto& plan : frame.Draws)
                frame.StrictDrawIdentities.push_back(ComputeOot3dPicaVisualDrawIdentity(plan));
        }
        return frame;
    }

    // Structural noise: reorders, deletions, duplicates, changed layouts,
    // ambiguous native keys, odd counts. Exercises every matching stage.
    void Disturb(Oot3dPicaVisualFrame& frame, Rng& rng) {
        auto& draws = frame.Draws;
        if (draws.empty()) return;
        const size_t edits = rng.Below(1 + draws.size() / 8);
        for (size_t e = 0; e < edits && !draws.empty(); ++e) {
            const size_t i = rng.Below(draws.size());
            switch (rng.Below(9)) {
            case 0: draws.erase(draws.begin() + static_cast<std::ptrdiff_t>(i)); break;
            case 1: {
                auto copy = draws[i];
                copy.SubmissionId = NextSubmission++;
                draws.insert(draws.begin() + static_cast<std::ptrdiff_t>(rng.Below(draws.size() + 1)),
                             std::move(copy));
                break;
            }
            case 2: std::swap(draws[i], draws[rng.Below(draws.size())]); break;
            case 3: draws[i].VertexShader.StateKey ^= 1U; break;
            case 4: draws[i].VertexCount = static_cast<uint32_t>(rng.Below(700)); break;
            case 5: draws[i].Composition.TemporalIdentity.Slot ^= rng.Below(2); break;
            case 6: draws[i].VertexShader.Uniforms.Floats[rng.Below(96)][rng.Below(4)] = rng.Real(-4, 4); break;
            case 7: draws[i].BaseVertex = static_cast<int32_t>(rng.Below(3)); break;
            default:
                if (!draws[i].VertexBindings.empty()) {
                    auto& binding = draws[i].VertexBindings.back();
                    auto bytes = std::vector<uint8_t>(binding.ResolvedBytes().begin(), binding.ResolvedBytes().end());
                    if (!bytes.empty()) bytes[rng.Below(bytes.size())] ^= 0x11;
                    binding.SharedBytes.reset();
                    binding.ContentVersionAvailable = false;
                    binding.Bytes = std::move(bytes);
                }
                break;
            }
        }
        if (rng.Chance(0.1)) std::reverse(draws.begin(), draws.end());
    }
};

template <typename T> bool SameBits(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }

bool SameFragment(const Oot3dPicaFragmentUniformState& a, const Oot3dPicaFragmentUniformState& b) {
    return SameBits(a.TevConstants, b.TevConstants) && SameBits(a.CombinerBufferColor, b.CombinerBufferColor) &&
           a.AlphaReference == b.AlphaReference && SameBits(a.FogColor, b.FogColor) &&
           SameBits(a.FogLut, b.FogLut) && SameBits(a.TextureLodBias, b.TextureLodBias) &&
           SameBits(a.Lighting, b.Lighting) && a.ShadowTextureBias == b.ShadowTextureBias &&
           a.ShadowOrthographic == b.ShadowOrthographic && SameBits(a.ShadowBiasConstant, b.ShadowBiasConstant) &&
           SameBits(a.ShadowBiasLinear, b.ShadowBiasLinear) && a.FragmentControl == b.FragmentControl;
}

bool SameLight(const Fast::Renderer3ds::PicaNativeLightBinding& a,
               const Fast::Renderer3ds::PicaNativeLightBinding& b) {
    const auto samePointee = [](const auto& x, const auto& y) {
        return x == y || (x && y && *x == *y);
    };
    const bool sameCamera = a.WorldToView.has_value() == b.WorldToView.has_value() &&
                            (!a.WorldToView || std::memcmp(a.WorldToView->data(), b.WorldToView->data(),
                                                           sizeof(*a.WorldToView)) == 0);
    return a.Sources == b.Sources && sameCamera && samePointee(a.ScenePoints, b.ScenePoints) &&
           samePointee(a.SceneDirectionals, b.SceneDirectionals) && a.Water == b.Water &&
           a.RoomShadows == b.RoomShadows;
}

void CompareStats(const Oot3dPicaVisualTransitionStats& a, const Oot3dPicaVisualTransitionStats& b,
                  const std::string& where) {
    const bool same =
        a.PreviousDraws == b.PreviousDraws && a.CurrentDraws == b.CurrentDraws && a.MatchedDraws == b.MatchedDraws &&
        a.StrictUniqueMatchedDraws == b.StrictUniqueMatchedDraws &&
        a.StrictOrdinalMatchedDraws == b.StrictOrdinalMatchedDraws &&
        a.StructuralOrdinalMatchedDraws == b.StructuralOrdinalMatchedDraws &&
        a.PipelineOrdinalMatchedDraws == b.PipelineOrdinalMatchedDraws &&
        a.ChangedContinuousDraws == b.ChangedContinuousDraws &&
        a.ChangedPerInstanceVertexDraws == b.ChangedPerInstanceVertexDraws &&
        a.ChangedPerVertexDraws == b.ChangedPerVertexDraws && a.AmbiguousPreviousDraws == b.AmbiguousPreviousDraws &&
        a.AmbiguousCurrentDraws == b.AmbiguousCurrentDraws && a.UnmatchedPreviousDraws == b.UnmatchedPreviousDraws &&
        a.UnmatchedCurrentDraws == b.UnmatchedCurrentDraws && a.TopTargetMatchedDraws == b.TopTargetMatchedDraws &&
        SameBits(a.MatchedDrawCoverage, b.MatchedDrawCoverage) &&
        SameBits(a.MedianNormalizedContinuousDelta, b.MedianNormalizedContinuousDelta) &&
        SameBits(a.P90NormalizedContinuousDelta, b.P90NormalizedContinuousDelta) &&
        a.CameraMatchedDraws == b.CameraMatchedDraws && a.CameraDiscontinuousDraws == b.CameraDiscontinuousDraws;
    Require(same, "transition statistics differ " + where);
}

void CompareSamples(const Oot3dPicaVisualFrameViewSample& a, const Oot3dPicaVisualFrameViewSample& b,
                    const std::string& where) {
    Require(a.TopTransfer.CompletionId == b.TopTransfer.CompletionId &&
                a.MemoryFills.data() == b.MemoryFills.data() && a.MemoryFills.size() == b.MemoryFills.size() &&
                a.DisplayTransfers.data() == b.DisplayTransfers.data() &&
                a.DisplayTransfers.size() == b.DisplayTransfers.size(),
            "sample transfers differ " + where);
    Require(a.MatchedDraws == b.MatchedDraws && a.StrictUniqueMatchedDraws == b.StrictUniqueMatchedDraws &&
                a.StrictOrdinalMatchedDraws == b.StrictOrdinalMatchedDraws &&
                a.StructuralOrdinalMatchedDraws == b.StructuralOrdinalMatchedDraws &&
                a.PipelineOrdinalMatchedDraws == b.PipelineOrdinalMatchedDraws &&
                a.InterpolatedDraws == b.InterpolatedDraws &&
                a.InterpolatedPerInstanceVertexDraws == b.InterpolatedPerInstanceVertexDraws &&
                a.InterpolatedPerVertexDraws == b.InterpolatedPerVertexDraws,
            "sample counters differ " + where);
    Require(a.Draws.size() == b.Draws.size(), "sample draw count differs " + where);
    for (size_t i = 0; i < a.Draws.size(); ++i) {
        const auto& x = a.Draws[i];
        const auto& y = b.Draws[i];
        const std::string at = where + " draw " + std::to_string(i);
        Require(x.BasePlan == y.BasePlan, "base plan differs " + at);
        Require(x.VertexUniforms.has_value() == y.VertexUniforms.has_value() &&
                    (!x.VertexUniforms || (x.VertexUniforms->BooleanMask == y.VertexUniforms->BooleanMask &&
                                           x.VertexUniforms->Integers == y.VertexUniforms->Integers &&
                                           SameBits(x.VertexUniforms->Floats, y.VertexUniforms->Floats))),
                "vertex uniforms differ " + at);
        Require(x.FragmentUniforms.has_value() == y.FragmentUniforms.has_value() &&
                    (!x.FragmentUniforms || SameFragment(*x.FragmentUniforms, *y.FragmentUniforms)),
                "fragment uniforms differ " + at);
        Require(x.Viewport.has_value() == y.Viewport.has_value() &&
                    (!x.Viewport || (SameBits(x.Viewport->HalfWidth, y.Viewport->HalfWidth) &&
                                     SameBits(x.Viewport->HalfHeight, y.Viewport->HalfHeight) &&
                                     SameBits(x.Viewport->DepthRange, y.Viewport->DepthRange) &&
                                     SameBits(x.Viewport->NearPlane, y.Viewport->NearPlane) &&
                                     x.Viewport->CornerX == y.Viewport->CornerX &&
                                     x.Viewport->CornerY == y.Viewport->CornerY)),
                "viewport differs " + at);
        Require(x.BlendConstantColor.has_value() == y.BlendConstantColor.has_value() &&
                    (!x.BlendConstantColor || SameBits(*x.BlendConstantColor, *y.BlendConstantColor)),
                "blend constant differs " + at);
        Require(x.LightBinding.has_value() == y.LightBinding.has_value() &&
                    (!x.LightBinding || SameLight(*x.LightBinding, *y.LightBinding)),
                "light binding differs " + at);
        Require(x.HasVertexBindingOverrides == y.HasVertexBindingOverrides, "vertex override flag differs " + at);
        if (x.HasVertexBindingOverrides) {
            Require(x.VertexBindings.size() == y.VertexBindings.size(), "vertex binding count differs " + at);
            for (size_t k = 0; k < x.VertexBindings.size(); ++k) {
                const auto& l = x.VertexBindings[k];
                const auto& r = y.VertexBindings[k];
                const auto lb = l.ResolvedBytes(), rb = r.ResolvedBytes();
                Require(l.Binding == r.Binding && l.ByteStride == r.ByteStride && l.InputRate == r.InputRate &&
                            l.ContentVersionAvailable == r.ContentVersionAvailable &&
                            l.ContentVersion == r.ContentVersion && lb.size() == rb.size() &&
                            std::equal(lb.begin(), lb.end(), rb.begin()),
                        "vertex bytes differ " + at);
            }
        }
    }
}

struct Coverage {
    uint64_t Transitions = 0, Matched = 0, StrictUnique = 0, StrictOrdinal = 0, Structural = 0, Pipeline = 0;
    uint64_t Ambiguous = 0, Unmatched = 0, Interpolated = 0, PerVertex = 0, PerInstance = 0, Carried = 0;
    uint64_t Camera = 0, Samples = 0, RigidCameras = 0;
};

// A sampled native camera that is not the componentwise lerp of its endpoints:
// it came from the rigid (factored model-view) camera path.
bool IsRigidCameraSample(const Oot3dPicaVisualFrame& previous, const Oot3dPicaVisualFrame& current,
                         const Oot3dPicaPreparedVisualTransition& live, size_t index, float alpha,
                         const Oot3dPicaVisualFrameViewSample& sample) {
    const auto& view = sample.Draws[index];
    const int64_t previousIndex = live.PreviousIndexOf(index);
    if (!(alpha > 0.0F && alpha < 1.0F) || previousIndex < 0 || !view.LightBinding ||
        !view.LightBinding->WorldToView)
        return false;
    const auto& before = previous.Draws[static_cast<size_t>(previousIndex)].LightBinding.WorldToView;
    const auto& after = current.Draws[index].LightBinding.WorldToView;
    if (!before || !after) return false;
    for (size_t k = 0; k < 16; ++k) {
        const float lerp = (*before)[k] + ((*after)[k] - (*before)[k]) * alpha;
        if (std::memcmp(&lerp, &(*view.LightBinding->WorldToView)[k], sizeof(float)) != 0) return true;
    }
    return false;
}

void RunEquivalence(uint64_t seed, size_t frames, size_t targetDraws, bool chaos, Coverage& coverage,
                    bool crossThread = false) {
    Rng rng(seed);
    Scene scene(rng, targetDraws);
    Ref::Transition reference;
    Oot3dPicaPreparedVisualTransition live;
    Oot3dPicaVisualFrameViewSample referenceSample, liveSample;
    std::optional<Oot3dPicaVisualFrame> latest;
    const std::array<float, 6> alphas{0.0F, 0.5F, 1.0F / 3.0F, 2.0F / 3.0F, 1.0F, 0.25F};
    for (size_t f = 0; f < frames; ++f) {
        auto current = scene.Next(f + 1, rng, chaos);
        if (latest) {
            const std::string where = "(seed " + std::to_string(seed) + ", frame " + std::to_string(f) + ")";
            const bool a = reference.Prepare(*latest, current);
            if (rng.Chance(0.05)) live.Reset();   // also from a reset transition
            const bool b = live.Prepare(*latest, current);
            Require(a == b && live.Ready() == b, "prepare result differs " + where);
            for (size_t i = 0; i < current.Draws.size(); ++i) {
                uint8_t ka = 0, kb = 0;
                const int64_t pa = reference.PreviousOf(i, ka);
                const int64_t pb = live.PreviousIndexOf(i, &kb);
                Require(pa == pb && (pa < 0 || ka == kb),
                        "matched pair differs " + where + " draw " + std::to_string(i));
            }
            CompareStats(reference.Stats, live.Stats(), where);
            ++coverage.Transitions;
            const auto& s = live.Stats();
            coverage.Matched += s.MatchedDraws;
            coverage.StrictUnique += s.StrictUniqueMatchedDraws;
            coverage.StrictOrdinal += s.StrictOrdinalMatchedDraws;
            coverage.Structural += s.StructuralOrdinalMatchedDraws;
            coverage.Pipeline += s.PipelineOrdinalMatchedDraws;
            coverage.Ambiguous += s.AmbiguousCurrentDraws;
            coverage.Unmatched += s.UnmatchedCurrentDraws;
            coverage.Camera += s.CameraMatchedDraws;
            const size_t samples = 2 + rng.Below(2);
            for (size_t k = 0; k < samples; ++k) {
                const float alpha = k == 0 ? 0.0F : k == 1 ? 0.5F
                                    : rng.Chance(0.5) ? alphas[rng.Below(alphas.size())] : rng.Real(0, 1);
                const bool ra = reference.Sample(*latest, current, alpha, referenceSample);
                const bool rb = crossThread
                    ? std::async(std::launch::async, [&] {
                          return live.Sample(*latest, current, alpha, liveSample);
                      }).get()
                    : live.Sample(*latest, current, alpha, liveSample);
                Require(ra == rb, "sample result differs " + where);
                if (ra) {
                    CompareSamples(referenceSample, liveSample, where + " alpha " + std::to_string(alpha));
                    ++coverage.Samples;
                    coverage.Interpolated += liveSample.InterpolatedDraws;
                    coverage.PerVertex += liveSample.InterpolatedPerVertexDraws;
                    coverage.PerInstance += liveSample.InterpolatedPerInstanceVertexDraws;
                    for (size_t i = 0; i < liveSample.Draws.size(); ++i) {
                        if (live.PreviousIndexOf(i) < 0 && liveSample.Draws[i].VertexUniforms) ++coverage.Carried;
                        if (IsRigidCameraSample(*latest, current, live, i, alpha, liveSample))
                            ++coverage.RigidCameras;
                    }
                }
            }
            // A sample of a different pair must be refused, as before.
            Oot3dPicaVisualFrame other;
            other.Sequence = current.Sequence + 1000;
            Require(!live.Sample(*latest, other, 0.5F, liveSample), "foreign pair sampled " + where);
        }
        latest = std::move(current);
    }
}

double Milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

void Bench(size_t targetDraws, size_t frames) {
    // The same frames for both: built first, then timed.
    Rng rng(0xBE7C4 + targetDraws);
    Scene scene(rng, targetDraws);
    std::vector<Oot3dPicaVisualFrame> sequence;
    for (size_t f = 0; f < frames; ++f) sequence.push_back(scene.Next(f + 1, rng, false));
    size_t draws = 0;
    for (const auto& frame : sequence) draws += frame.Draws.size();
    using Clock = std::chrono::steady_clock;
    const auto run = [&](auto&& prepare, auto&& sample) {
        Oot3dPicaVisualFrameViewSample view;
        double best = 1e30;
        for (int repeat = 0; repeat < 3; ++repeat) {
            const auto start = Clock::now();
            for (size_t f = 1; f < sequence.size(); ++f) {
                prepare(sequence[f - 1], sequence[f]);
                sample(sequence[f - 1], sequence[f], 0.0F, view);   // step frame
                sample(sequence[f - 1], sequence[f], 0.5F, view);   // in-between
            }
            best = std::min(best, Milliseconds(Clock::now() - start) / static_cast<double>(sequence.size() - 1));
        }
        return best;
    };
    Ref::Transition reference;
    const double before = run([&](const auto& a, const auto& b) { (void)reference.Prepare(a, b); },
                              [&](const auto& a, const auto& b, float alpha, auto& view) {
                                  (void)reference.Sample(a, b, alpha, view);
                              });
    Oot3dPicaPreparedVisualTransition live;
    Oot3dPicaVisualInterpolationTiming timing;
    const double after = run([&](const auto& a, const auto& b) { (void)live.Prepare(a, b, &timing); },
                             [&](const auto& a, const auto& b, float alpha, auto& view) {
                                 (void)live.Sample(a, b, alpha, view, &timing);
                             });
    const double transitions = 3.0 * static_cast<double>(sequence.size() - 1);
    std::printf("%zu draws/frame (avg %.0f): reference %.3f ms, current %.3f ms per guest frame "
                "(prepare+2 samples) -> %.1fx; current split: matching %.3f analysis %.3f sampling %.3f ms\n",
                targetDraws, static_cast<double>(draws) / static_cast<double>(sequence.size()), before, after,
                before / after, static_cast<double>(timing.MatchingNanoseconds) / 1e6 / transitions,
                static_cast<double>(timing.AnalysisNanoseconds) / 1e6 / transitions,
                static_cast<double>(timing.SamplingNanoseconds) / 1e6 / transitions);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--bench") {
        const size_t frames = argc > 2 ? static_cast<size_t>(std::strtoul(argv[2], nullptr, 10)) : 300U;
        Bench(200, frames);
        Bench(400, frames);
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--threads") {
        std::vector<std::thread> threads;
        std::vector<Coverage> coverages(4);
        for (size_t t = 0; t < coverages.size(); ++t) {
            threads.emplace_back([t, &coverages] {
                for (uint64_t seed = 1 + t; seed <= 12U; seed += 4U)
                    RunEquivalence(1000U + seed, 40, 60 + 40 * seed, seed % 2 == 0, coverages[t], true);
            });
        }
        for (auto& thread : threads) thread.join();
        uint64_t samples = 0;
        for (const auto& coverage : coverages) samples += coverage.Samples;
        std::printf("threads %zu samples %llu\n", coverages.size(), (unsigned long long)samples);
        std::puts("oot3d_native_pica_visual_frame_equivalence_tests --threads: ok");
        return 0;
    }
    const size_t scale = argc > 1 ? static_cast<size_t>(std::strtoul(argv[1], nullptr, 10)) : 1U;
    Coverage coverage;
    size_t frames = 0;
    // Scale 0: a quick run (8 shorter sequences) for the sanitizers.
    const uint64_t seeds = scale == 0 ? 8U : 24U * scale;
    for (uint64_t seed = 1; seed <= seeds; ++seed) {
        Rng pick(seed * 7919U);
        const size_t draws = seed % 6 == 0 ? 1 + pick.Below(12) : 40 + pick.Below(380);
        const size_t count = scale == 0 ? 30 + pick.Below(20) : 60 + pick.Below(60);
        RunEquivalence(seed, count, draws, seed % 3 != 0, coverage);
        frames += count;
    }
    std::printf("frames %zu transitions %llu samples %llu matched %llu (strict unique %llu, strict ordinal %llu, "
                "structural %llu, pipeline %llu) ambiguous %llu unmatched %llu interpolated %llu per-vertex %llu "
                "per-instance %llu carried %llu camera %llu rigid-camera %llu\n",
                frames, (unsigned long long)coverage.Transitions, (unsigned long long)coverage.Samples,
                (unsigned long long)coverage.Matched, (unsigned long long)coverage.StrictUnique,
                (unsigned long long)coverage.StrictOrdinal, (unsigned long long)coverage.Structural,
                (unsigned long long)coverage.Pipeline, (unsigned long long)coverage.Ambiguous,
                (unsigned long long)coverage.Unmatched, (unsigned long long)coverage.Interpolated,
                (unsigned long long)coverage.PerVertex, (unsigned long long)coverage.PerInstance,
                (unsigned long long)coverage.Carried, (unsigned long long)coverage.Camera,
                (unsigned long long)coverage.RigidCameras);
    Require(coverage.StrictUnique && coverage.StrictOrdinal && coverage.Structural && coverage.Pipeline &&
                coverage.Ambiguous && coverage.PerVertex && coverage.PerInstance && coverage.Carried &&
                coverage.RigidCameras,
            "a matching or sampling path was never exercised");
    std::puts("oot3d_native_pica_visual_frame_equivalence_tests: ok");
    return 0;
}
