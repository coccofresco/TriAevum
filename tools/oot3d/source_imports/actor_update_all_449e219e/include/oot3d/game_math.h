#ifndef OOT3D_GAME_MATH_H
#define OOT3D_GAME_MATH_H

#include <stdbool.h>

#include "oot3d/actor.h"
#include "oot3d/flags.h"

typedef struct Oot3dVec3s {
    s16 x;
    s16 y;
    s16 z;
} Oot3dVec3s;

typedef struct Oot3dVecSphGeo {
    float r;
    s16 pitch;
    s16 yaw;
} Oot3dVecSphGeo;

typedef struct Oot3dMathSphere {
    Oot3dActorVec3f center;
    float radius;
} Oot3dMathSphere;

typedef struct Oot3dMathLine {
    Oot3dActorVec3f a;
    Oot3dActorVec3f b;
} Oot3dMathLine;

typedef struct Oot3dMathCylinder {
    float radius;
    float height;
    float yShift;
    Oot3dActorVec3f position;
} Oot3dMathCylinder;

typedef struct Oot3dPath {
    u8 count;
    u8 pad_01[3];
    u32 points;
} Oot3dPath;

typedef struct Oot3dActorPosRot {
    Oot3dActorVec3f pos;
    Oot3dActorRot rot;
    u8 pad_12[2];
} Oot3dActorPosRot;

typedef struct Oot3dMathPlane {
    float normalX;
    float normalY;
    float normalZ;
    float originDistance;
} Oot3dMathPlane;

typedef struct Oot3dMathTriNorm {
    Oot3dActorVec3f vertices[3];
    Oot3dMathPlane plane;
} Oot3dMathTriNorm;

#if defined(__cplusplus)
static_assert(sizeof(Oot3dVecSphGeo) == 0x08, "VecSphGeo size");
static_assert(sizeof(Oot3dMathSphere) == 0x10, "float sphere size");
static_assert(sizeof(Oot3dMathLine) == 0x18, "float line size");
static_assert(sizeof(Oot3dMathCylinder) == 0x18, "float cylinder size");
static_assert(sizeof(Oot3dPath) == 0x08, "Path size");
static_assert(sizeof(Oot3dActorPosRot) == 0x14, "actor PosRot size");
static_assert(sizeof(Oot3dMathPlane) == 0x10, "plane size");
static_assert(sizeof(Oot3dMathTriNorm) == 0x34, "triangle size");
#else
_Static_assert(sizeof(Oot3dVecSphGeo) == 0x08, "VecSphGeo size");
_Static_assert(sizeof(Oot3dMathSphere) == 0x10, "float sphere size");
_Static_assert(sizeof(Oot3dMathLine) == 0x18, "float line size");
_Static_assert(sizeof(Oot3dMathCylinder) == 0x18, "float cylinder size");
_Static_assert(sizeof(Oot3dPath) == 0x08, "Path size");
_Static_assert(sizeof(Oot3dActorPosRot) == 0x14, "actor PosRot size");
_Static_assert(sizeof(Oot3dMathPlane) == 0x10, "plane size");
_Static_assert(sizeof(Oot3dMathTriNorm) == 0x34, "triangle size");
#endif

#ifdef __cplusplus
extern "C" {
#endif

void Math_ApproachF(
    float* value,
    float target,
    float fraction,
    float maxStep
);
void Math_ApproachZeroF(
    float* value,
    float fraction,
    float maxStep
);
float Math_SmoothStepToF(
    float* value,
    float target,
    float fraction,
    float maxStep,
    float minStep
);
s16 Math_SmoothStepToS(
    s16* value,
    s16 target,
    s16 scale,
    s16 maxStep,
    s16 minStep
);
void Math_ApproachS(
    s16* value,
    s16 target,
    s16 scale,
    s16 maxStep
);
s32 Math_StepToF(float* value, float target, float step);
s32 Math_StepToS(s16* value, s16 target, s16 step);
s32 Math_ScaledStepToS(s16* value, s16 target, s16 step);
s32 Math_StepToAngleS(s16* value, s16 target, s16 step);

#if defined(OOT3D_HOST_GAMEPLAY_MATH_PTR32_RESOLVER)
void* oot3d_host_gameplay_math_ptr32_resolve(u32 address);
u16 oot3d_host_gameplay_math_atan2_table(u32 index);
#endif

extern Oot3dActorVec3f gOot3dMath3dPerpendicularPoint;
extern Oot3dActorVec3f gOot3dMath3dPlaneNormal;
extern const u16 gOot3dMathAtan2Table[1025];

u16 Math_GetAtan2Tbl(float numerator, float denominator);
float Math_FAtan2F(float y, float x);
s16 Math_Atan2S(float y, float x);
float sins(float angle);
float coss(float angle);
float Math_SinF(float radians);
float Math_CosF(float radians);

float Lerp(float start, float end, float amount);
float Camera_LERPCeilF(
    float target,
    float current,
    float stepScale,
    float minDiff
);
void Camera_LERPCeilVec3f(
    const Oot3dActorVec3f* target,
    Oot3dActorVec3f* current,
    float yStepScale,
    float xzStepScale,
    float minDiff
);
float Camera_CalcSlopeYAdj(
    const Oot3dActorVec3f* floorNormal,
    s16 playerYaw,
    s16 eyeAtYaw,
    float adjustment
);

float OLib_ClampMaxDist(float value, float maxDistance);
float OLib_Vec3fDistXZ(
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b
);
void OLib_Vec3fToVecSph(
    Oot3dVecSphGeo* destination,
    const Oot3dActorVec3f* vector
);
void OLib_Vec3fToVecSphGeo(
    Oot3dVecSphGeo* destination,
    const Oot3dActorVec3f* vector
);
void OLib_Vec3fDiffToVecSphGeo(
    Oot3dVecSphGeo* destination,
    const Oot3dActorVec3f* from,
    const Oot3dActorVec3f* to
);
Oot3dActorVec3f OLib_VecSphGeoToVec3f(
    const Oot3dVecSphGeo* spherical
);
Oot3dActorVec3f Camera_AddVecGeoToVec3f(
    const Oot3dActorVec3f* origin,
    const Oot3dVecSphGeo* offset
);
Oot3dActorVec3f FUN_002bfd84(
    const Oot3dActorVec3f* origin,
    const Oot3dVecSphGeo* offset
);

float Actor_WorldDistXZToPoint(
    const Oot3dActor* actor,
    const Oot3dActorVec3f* point
);
float Actor_WorldDistXZToActor(
    const Oot3dActor* actor,
    const Oot3dActor* other
);
float Actor_WorldDistXYZToPoint(
    const Oot3dActor* actor,
    const Oot3dActorVec3f* point
);
float Actor_WorldDistXYZToActor(
    const Oot3dActor* actor,
    const Oot3dActor* other
);
s16 Actor_WorldYawTowardPoint(
    const Oot3dActor* actor,
    const Oot3dActorVec3f* point
);
s16 Actor_WorldYawTowardActor(
    const Oot3dActor* actor,
    const Oot3dActor* other
);
s16 Actor_WorldPitchTowardPoint(
    const Oot3dActor* actor,
    const Oot3dActorVec3f* point
);

float Math3D_DistPlaneToPos(
    float normalX,
    float normalY,
    float normalZ,
    float originDistance,
    const Oot3dActorVec3f* position
);
s32 Math3D_PointInSquare2D(
    float upperLeftX,
    float lowerRightX,
    float upperLeftY,
    float lowerRightY,
    float x,
    float y
);
s32 Math3D_PointDistToLine2D(
    float pointX,
    float pointY,
    float lineStartX,
    float lineStartY,
    float lineEndX,
    float lineEndY,
    float* distanceSquared
);
bool Math3D_SphVsSphOverlapCenter(
    const Oot3dMathSphere* sphereA,
    const Oot3dMathSphere* sphereB,
    float* overlapSize,
    float* centerDistance
);
bool Math3D_CirSquareVsTriSquare(
    float centerX,
    float centerY,
    float radius,
    float ax,
    float ay,
    float bx,
    float by,
    float cx,
    float cy
);
void Math3D_RotateXZPlane(
    const Oot3dActorVec3f* point,
    s16 angle,
    float* normalX,
    float* normalZ,
    float* originDistance
);
void Math3D_DefPlane(
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b,
    const Oot3dActorVec3f* c,
    float* normalX,
    float* normalY,
    float* normalZ,
    float* originDistance
);
void Math3D_TriNorm(
    Oot3dMathTriNorm* triangle,
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b,
    const Oot3dActorVec3f* c
);
bool Math3D_LineSegVsPlane(
    float normalX,
    float normalY,
    float normalZ,
    float originDistance,
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b,
    Oot3dActorVec3f* intersection,
    s32 fromFront
);
bool Math3D_LineVsSph(
    const Oot3dMathSphere* sphere,
    const Oot3dMathLine* line
);
bool Math3D_TriLineIntersect(
    const Oot3dActorVec3f* v0,
    const Oot3dActorVec3f* v1,
    const Oot3dActorVec3f* v2,
    float normalX,
    float normalY,
    float normalZ,
    float originDistance,
    const Oot3dActorVec3f* linePointA,
    const Oot3dActorVec3f* linePointB,
    Oot3dActorVec3f* intersection,
    s32 fromFront
);
bool Math3D_TriVsTriIntersect(
    const Oot3dMathTriNorm* triangleA,
    const Oot3dMathTriNorm* triangleB,
    Oot3dActorVec3f* intersection
);
bool Math3D_LineVsCube(
    const Oot3dActorVec3f* minimum,
    const Oot3dActorVec3f* maximum,
    const Oot3dActorVec3f* a,
    const Oot3dActorVec3f* b
);
u8 Math3D_PointRelativeToCubeVertices(
    const Oot3dActorVec3f* point,
    const Oot3dActorVec3f* minimum,
    const Oot3dActorVec3f* maximum
);
u32 Math3D_PointRelativeToCubeEdges(
    const Oot3dActorVec3f* point,
    const Oot3dActorVec3f* minimum,
    const Oot3dActorVec3f* maximum
);

/* Address-preserving target identities. */
u8 FUN_00356fa4(
    const Oot3dActorVec3f* point,
    const Oot3dActorVec3f* minimum,
    const Oot3dActorVec3f* maximum
);
u32 FUN_0035708c(
    const Oot3dActorVec3f* point,
    const Oot3dActorVec3f* minimum,
    const Oot3dActorVec3f* maximum
);
bool Math3D_TriChkLineSegParaYIntersect(
    const Oot3dActorVec3f* v0,
    const Oot3dActorVec3f* v1,
    const Oot3dActorVec3f* v2,
    float normalX,
    float normalY,
    float normalZ,
    float originDistance,
    float z,
    float x,
    float* yIntersection,
    float y0,
    float y1
);
void Math3D_GetSphVsTriIntersectPoint(
    const Oot3dMathSphere* sphere,
    const Oot3dMathTriNorm* triangle,
    Oot3dActorVec3f* intersection
);
bool Math3D_CylTriVsIntersect(
    const Oot3dMathCylinder* cylinder,
    const Oot3dMathTriNorm* triangle,
    Oot3dActorVec3f* intersection
);
bool Math3D_TriVsSphIntersect(
    const Oot3dMathSphere* sphere,
    const Oot3dMathTriNorm* triangle,
    Oot3dActorVec3f* intersection
);
void Math3D_Vec3fNormalizedReflect(
    const Oot3dActorVec3f* input,
    const Oot3dActorVec3f* normal,
    Oot3dActorVec3f* reflected
);

float Path_OrientAndGetDistSq(
    const Oot3dActor* actor,
    const Oot3dPath* path,
    s16 waypoint,
    s16* yaw
);
void Path_CopyLastPoint(
    const Oot3dPath* path,
    Oot3dActorVec3f* destination
);

float Player_GetHeight(const void* player);
void Actor_GetWorldPosShapeRot(
    Oot3dActorPosRot* destination,
    const Oot3dActor* actor
);
s32 Actor_IsFacingAndNearPlayer(
    const Oot3dActor* actor,
    float range,
    s16 maxAngle
);

#ifdef __cplusplus
}
#endif

#endif
