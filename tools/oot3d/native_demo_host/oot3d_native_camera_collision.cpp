#include "oot3d_native_camera_collision.h"

#include <cmath>
#include <limits>

#include "oot3d_demo_math.h"
#include "oot3d_native_collision_math.h"

namespace {

struct NativeCameraLineHit {
    bool Hit = false;
    int PolygonIndex = -1;
    Vec3 Position;
    Vec3 Normal;
    double SegmentT = 0.0;
    double DistanceSqFromStart = std::numeric_limits<double>::infinity();
};

bool CollisionPolygonIgnoredByCamera(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    return poly.IgnoreCamera || (poly.RawVertexA & kNativeColPolyIgnoreCameraRawFlag) != 0;
}

bool NativeCameraLineVsPoly(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision, size_t polyIndex,
                            const Vec3& from, const Vec3& to, bool chkOneFace,
                            NativeCameraLineHit& hit) {
    const auto& poly = collision.Polygons[polyIndex];
    if (!CollisionPolygonIndicesValid(collision, poly)) {
        return false;
    }

    const Vec3 normal = CollisionPolygonNormal(poly);
    if (Dot(normal, normal) <= 0.000001) {
        return false;
    }

    const double planeDistA = Dot(normal, from) + static_cast<double>(poly.Dist);
    const double planeDistB = Dot(normal, to) + static_cast<double>(poly.Dist);
    const double planeDistDelta = planeDistA - planeDistB;
    if ((planeDistA >= 0.0 && planeDistB >= 0.0) || (planeDistA < 0.0 && planeDistB < 0.0) ||
        (chkOneFace && planeDistA < 0.0 && planeDistB > 0.0) || std::abs(planeDistDelta) <= 0.000001) {
        return false;
    }

    const double t = planeDistA / planeDistDelta;
    if (t < 0.0 || t > 1.0) {
        return false;
    }

    const Vec3 line = Subtract(to, from);
    const Vec3 planeIntersect = Add(from, Scale(line, t));
    const Vec3 a = ToVec3(collision.Vertices[poly.VertexA]);
    const Vec3 b = ToVec3(collision.Vertices[poly.VertexB]);
    const Vec3 c = ToVec3(collision.Vertices[poly.VertexC]);
    if (!PointInTriangle3D(planeIntersect, a, b, c)) {
        return false;
    }

    hit.Hit = true;
    hit.PolygonIndex = static_cast<int>(polyIndex);
    hit.Position = planeIntersect;
    hit.Normal = normal;
    hit.SegmentT = t;
    hit.DistanceSqFromStart = Dot(Subtract(planeIntersect, from), Subtract(planeIntersect, from));
    return true;
}

bool FindNativeCameraLineHit(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const Vec3& from, const Vec3& to,
                             int preferredPolygonIndex, NativeCameraLineHit& bestHit,
                             int& ignoredCameraPolygonCount, bool& retainedPreviousPolygon) {
    bestHit = {};
    ignoredCameraPolygonCount = 0;
    retainedPreviousPolygon = false;
    if (!scene.Collision.Valid) {
        return false;
    }

    bool hitAny = false;
    for (size_t polyIndex = 0; polyIndex < scene.Collision.Polygons.size(); ++polyIndex) {
        if (CollisionPolygonIgnoredByCamera(scene.Collision.Polygons[polyIndex])) {
            ++ignoredCameraPolygonCount;
            continue;
        }
        NativeCameraLineHit hit;
        if (!NativeCameraLineVsPoly(scene.Collision, polyIndex, from, to, true, hit)) {
            continue;
        }
        if (!hitAny || hit.DistanceSqFromStart < bestHit.DistanceSqFromStart) {
            bestHit = hit;
            hitAny = true;
        }
    }
    if (preferredPolygonIndex >= 0 &&
        static_cast<size_t>(preferredPolygonIndex) < scene.Collision.Polygons.size() &&
        !CollisionPolygonIgnoredByCamera(scene.Collision.Polygons[static_cast<size_t>(preferredPolygonIndex)])) {
        NativeCameraLineHit preferredHit;
        if (NativeCameraLineVsPoly(scene.Collision, static_cast<size_t>(preferredPolygonIndex), from, to, true,
                                   preferredHit) &&
            (!hitAny || preferredHit.DistanceSqFromStart <= bestHit.DistanceSqFromStart + 0.000001)) {
            bestHit = preferredHit;
            hitAny = true;
            retainedPreviousPolygon = true;
        }
    }
    return hitAny;
}

} // namespace

bool ApplyNativeCameraCollision(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const NativeCameraConfig& config,
                                Camera& camera) {
    const int previousPolygonIndex = camera.NativeCollisionActive ? camera.NativeCollisionPolygonIndex : -1;
    camera.NativeCollisionSupported = config.CollisionLineTestSupported;
    camera.NativeCollisionPolygonFlagsSupported = config.CollisionPolygonFlagsSupported;
    camera.NativeCollisionRayExtend = config.CollisionRayExtend;
    camera.NativeCollisionSurfacePush = config.CollisionSurfacePush;
    camera.NativePreCollisionPosition = camera.Position;
    camera.NativeCollisionActive = false;
    camera.NativeCollisionPreviousPolygonIndex = previousPolygonIndex;
    camera.NativeCollisionPolygonIndex = -1;
    camera.NativeCollisionHitPosition = {};
    camera.NativeCollisionNormal = {};
    camera.NativeCollisionDisplacement = 0.0;
    camera.NativeCollisionIgnoredCameraPolygonCount = 0;
    camera.NativeCollisionRetainedPreviousPolygon = false;
    if (!config.CollisionLineTestSupported) {
        return false;
    }

    const Vec3 atToEye = Subtract(camera.Position, camera.Target);
    const double distance = std::sqrt(Dot(atToEye, atToEye));
    if (distance <= 0.000001) {
        return false;
    }

    const Vec3 rayDir = Scale(atToEye, 1.0 / distance);
    const Vec3 rayEnd = Add(camera.Target, Scale(rayDir, distance + config.CollisionRayExtend));
    NativeCameraLineHit hit;
    int ignoredCameraPolygonCount = 0;
    bool retainedPreviousPolygon = false;
    if (!FindNativeCameraLineHit(scene, camera.Target, rayEnd, previousPolygonIndex, hit,
                                 ignoredCameraPolygonCount, retainedPreviousPolygon)) {
        camera.NativeCollisionIgnoredCameraPolygonCount = ignoredCameraPolygonCount;
        return false;
    }

    const Vec3 resolvedPosition = Add(hit.Position, Scale(hit.Normal, config.CollisionSurfacePush));
    camera.Position = resolvedPosition;
    camera.NativeCollisionActive = true;
    camera.NativeCollisionPolygonIndex = hit.PolygonIndex;
    camera.NativeCollisionHitPosition = hit.Position;
    camera.NativeCollisionNormal = hit.Normal;
    camera.NativeCollisionIgnoredCameraPolygonCount = ignoredCameraPolygonCount;
    camera.NativeCollisionRetainedPreviousPolygon = retainedPreviousPolygon;
    camera.NativeCollisionDisplacement =
        std::sqrt(Dot(Subtract(camera.NativePreCollisionPosition, camera.Position),
                      Subtract(camera.NativePreCollisionPosition, camera.Position)));
    LookAt(camera, camera.Target);
    return true;
}
