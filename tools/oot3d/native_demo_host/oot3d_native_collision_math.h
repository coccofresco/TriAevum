#pragma once

#include "oot3d_demo_host_types.h"

struct NativeCollisionSegmentHit {
    bool Hit = false;
    int PolygonIndex = -1;
    Vec3 Position;
    Vec3 Normal;
    double SegmentT = 0.0;
    double DistanceSqFromStart = 0.0;
};

double PolygonYMin(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly);
double PolygonYMax(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly);
bool CollisionPolygonIndicesValid(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                                  const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly);
Vec3 CollisionPolygonNormal(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly);
bool PointInTriangle3D(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c);
bool CollisionSegmentVsPolygon(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
    size_t polygonIndex, const Vec3& from, const Vec3& to, bool checkOneFace,
    NativeCollisionSegmentHit& hit);
