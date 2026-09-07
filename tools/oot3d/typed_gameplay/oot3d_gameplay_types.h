#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace oot3d::gameplay {

using GuestAddress = std::uint32_t;

template <typename T = void> struct GuestPtr {
  GuestAddress Address = 0;
};

struct CollisionPoly;
struct DamageTable;
struct ActorOverlayEntry;
struct CmbRenderState;
struct SkeletonAnimationModel;
struct GuestFunction;

struct Vec3f {
  float X = 0.0f;
  float Y = 0.0f;
  float Z = 0.0f;
};

struct Vec3s {
  std::int16_t X = 0;
  std::int16_t Y = 0;
  std::int16_t Z = 0;
};

struct Mtx3x4 {
  float Elements[3][4]{};
};

struct CollisionCheckInfo {
  GuestPtr<DamageTable> DamageTableRef;
  Vec3f Displacement;
  std::int16_t CylinderRadius = 0;
  std::int16_t CylinderHeight = 0;
  std::int16_t CylinderYShift = 0;
  std::uint8_t Mass = 0;
  std::uint8_t Health = 0;
  std::uint8_t Damage = 0;
  std::uint8_t DamageEffect = 0;
  std::uint8_t AttackHitEffect = 0;
  std::uint8_t AcHitEffect = 0;
};

struct ActorShape {
  Vec3s Rotation;
  std::int16_t Face = 0;
  float YOffset = 0.0f;
  GuestPtr<GuestFunction> ShadowDraw;
  float ShadowScale = 0.0f;
  std::uint8_t ShadowAlpha = 0;
  std::uint8_t FeetFloorFlags = 0;
  std::array<std::byte, 2> Reserved16{};
  std::array<Vec3f, 2> FeetPosition{};
};

// Exact little-endian wire layout used by OoT3D code.bin. Guest pointers stay
// 32-bit addresses; host pointers never enter gameplay state.
struct Actor {
  std::int16_t Id = 0;
  std::uint8_t Category = 0;
  std::int8_t Room = 0;
  std::uint32_t Flags = 0;
  Vec3f HomePosition;
  Vec3s HomeRotation;
  std::uint16_t HomePadding = 0;
  std::int16_t Params = 0;
  std::int8_t ObjectSlot = 0;
  std::int8_t TargetMode = 0;
  std::uint8_t BgCheckControlFlags = 0;
  std::array<std::byte, 3> BgCheckControlPadding{};
  std::uint32_t SfxRequest = 0;
  Vec3f WorldPosition;
  Vec3s WorldRotation;
  std::uint16_t WorldPadding = 0;
  Vec3f FocusPosition;
  Vec3s FocusRotation;
  std::uint16_t FocusPadding = 0;
  float TargetArrowOffset = 0.0f;
  Vec3f Scale;
  Vec3f Velocity;
  float SpeedXZ = 0.0f;
  float Gravity = 0.0f;
  float MinimumVelocityY = 0.0f;
  GuestPtr<CollisionPoly> WallPoly;
  GuestPtr<CollisionPoly> FloorPoly;
  std::uint8_t WallBgId = 0;
  std::uint8_t FloorBgId = 0;
  std::int16_t WallYaw = 0;
  float FloorHeight = 0.0f;
  float DepthInWater = 0.0f;
  float WaterSurfaceY = 0.0f;
  std::uint16_t BgCheckFlags = 0;
  std::int16_t YawTowardPlayer = 0;
  float XyzDistanceToPlayerSquared = 0.0f;
  float XzDistanceToPlayer = 0.0f;
  float YDistanceToPlayer = 0.0f;
  CollisionCheckInfo CollisionCheck;
  ActorShape Shape;
  Vec3f ProjectedPosition;
  float ProjectedW = 0.0f;
  float UncullZoneForward = 0.0f;
  float UncullZoneScale = 0.0f;
  float UncullZoneDownward = 0.0f;
  Vec3f PreviousPosition;
  std::uint8_t IsTargeted = 0;
  std::uint8_t TargetPriority = 0;
  std::uint16_t TextId = 0;
  std::uint16_t FreezeTimer = 0;
  std::int16_t ColorFilterTimer = 0;
  std::uint32_t ColorFilterParams = 0;
  std::uint8_t CullingState = 0;
  std::uint8_t IsDrawn = 0;
  std::uint8_t DropFlag = 0;
  std::uint8_t NaviEnemyId = 0;
  GuestPtr<Actor> Parent;
  GuestPtr<Actor> Child;
  GuestPtr<Actor> Previous;
  GuestPtr<Actor> Next;
  GuestPtr<GuestFunction> Init;
  GuestPtr<GuestFunction> Destroy;
  GuestPtr<GuestFunction> Update;
  GuestPtr<GuestFunction> Draw;
  GuestPtr<ActorOverlayEntry> OverlayEntry;
  Mtx3x4 ModelTransform;
  GuestPtr<CmbRenderState> RenderState;
  std::array<GuestPtr<void>, 6> OwnedRuntimeObjects{};
  GuestPtr<SkeletonAnimationModel> ShadowModel;
  std::uint8_t ShadowModelVariant = 0;
  std::uint8_t Reserved199 = 0;
  std::uint8_t ModelResourcesInitialized = 0;
  std::uint8_t HitEffectMode = 0;
  std::int16_t SfxTimer = 0;
  std::uint8_t CullingFadeTimer = 0;
  std::uint8_t CullingFadeDuration = 0;
  std::uint32_t RuntimeWord = 0;
};

struct SkelAnimePoseHook {
  GuestPtr<void> Vtable;
  GuestPtr<void> CallbackArgument;
  GuestPtr<void> Play;
  GuestPtr<void> SkelAnimeRef;
  GuestPtr<GuestFunction> BeforeLimbCallback;
  GuestPtr<GuestFunction> AfterLimbCallback;
};

struct SkelAnime {
  GuestPtr<void> CmbResource;
  GuestPtr<void> ZarArchive;
  SkelAnimePoseHook PoseHookStorage;
  GuestPtr<void> PoseAnchor;
  std::uint32_t Reserved24 = 0;
  GuestPtr<SkeletonAnimationModel> ModelHandle;
  std::uint32_t Reserved2C = 0;
  std::int32_t AnimationIndex = 0;
  float MorphWeight = 0.0f;
  float MorphRate = 0.0f;
  float CurrentFrame = 0.0f;
  float PlaySpeed = 0.0f;
  float StartFrame = 0.0f;
  float EndFrame = 0.0f;
  float AnimationLength = 0.0f;
  std::int8_t MorphTaper = 0;
  std::uint8_t InitFlags = 0;
  std::uint8_t MovementFlags = 0;
  std::uint8_t Reserved53 = 0;
  std::int16_t PreviousRotation = 0;
  std::uint16_t Reserved56 = 0;
  Vec3f PreviousTranslation;
  Vec3f BaseTranslation;
  std::uint8_t AnimationMode = 0;
  std::uint8_t UpdateMode = 0;
  std::array<std::byte, 2> Reserved72{};
  std::uint8_t LimbCount = 0;
  std::uint8_t SpecialLimb = 0;
  std::uint8_t FrameDataPath = 0;
  std::uint8_t RegistrationActive = 0;
  GuestPtr<void> JointMatrices;
  GuestPtr<void> MorphMatrices;
  std::uint8_t TransformFormat = 0;
  std::uint8_t FrameOffsetMode = 0;
  std::uint8_t OwnsMatrices = 0;
  std::uint8_t Reserved83 = 0;
};

static_assert(sizeof(GuestPtr<void>) == 4);
static_assert(sizeof(Vec3f) == 0xC);
static_assert(sizeof(Vec3s) == 0x6);
static_assert(sizeof(Mtx3x4) == 0x30);
static_assert(sizeof(CollisionCheckInfo) == 0x1C);
static_assert(offsetof(CollisionCheckInfo, Displacement) == 0x4);
static_assert(sizeof(ActorShape) == 0x30);
static_assert(offsetof(ActorShape, FeetPosition) == 0x18);
static_assert(sizeof(Actor) == 0x1A4);
static_assert(offsetof(Actor, WorldPosition) == 0x28);
static_assert(offsetof(Actor, WorldRotation) == 0x34);
static_assert(offsetof(Actor, Velocity) == 0x60);
static_assert(offsetof(Actor, SpeedXZ) == 0x6C);
static_assert(offsetof(Actor, Gravity) == 0x70);
static_assert(offsetof(Actor, MinimumVelocityY) == 0x74);
static_assert(offsetof(Actor, CollisionCheck) == 0xA0);
static_assert(offsetof(Actor, Shape) == 0xBC);
static_assert(offsetof(Actor, ModelTransform) == 0x148);
static_assert(sizeof(SkelAnimePoseHook) == 0x18);
static_assert(sizeof(SkelAnime) == 0x84);
static_assert(offsetof(SkelAnime, CurrentFrame) == 0x3C);
static_assert(offsetof(SkelAnime, PlaySpeed) == 0x40);
static_assert(offsetof(SkelAnime, MovementFlags) == 0x52);
static_assert(offsetof(SkelAnime, PreviousTranslation) == 0x58);
static_assert(offsetof(SkelAnime, AnimationMode) == 0x70);
static_assert(offsetof(SkelAnime, JointMatrices) == 0x78);
static_assert(std::is_standard_layout_v<Actor>);
static_assert(std::is_trivially_copyable_v<Actor>);
static_assert(std::is_standard_layout_v<SkelAnime>);
static_assert(std::is_trivially_copyable_v<SkelAnime>);

} // namespace oot3d::gameplay
