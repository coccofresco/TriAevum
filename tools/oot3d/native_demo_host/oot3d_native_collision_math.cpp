#include "oot3d_native_collision_math.h"

#include <algorithm>
#include <cmath>

#include "oot3d_demo_math.h"

double PolygonYMin(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    return std::min({ collision.Vertices[poly.VertexA].Y, collision.Vertices[poly.VertexB].Y,
                      collision.Vertices[poly.VertexC].Y });
}

double PolygonYMax(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                   const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    return std::max({ collision.Vertices[poly.VertexA].Y, collision.Vertices[poly.VertexB].Y,
                      collision.Vertices[poly.VertexC].Y });
}

bool CollisionPolygonIndicesValid(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
                                  const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    return poly.VertexA >= 0 && poly.VertexB >= 0 && poly.VertexC >= 0 &&
           static_cast<size_t>(poly.VertexA) < collision.Vertices.size() &&
           static_cast<size_t>(poly.VertexB) < collision.Vertices.size() &&
           static_cast<size_t>(poly.VertexC) < collision.Vertices.size();
}

Vec3 CollisionPolygonNormal(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionPolygon& poly) {
    return Normalize({
        static_cast<double>(poly.NormalX) / 32767.0,
        static_cast<double>(poly.NormalY) / 32767.0,
        static_cast<double>(poly.NormalZ) / 32767.0,
    });
}

bool PointInTriangle3D(const Vec3& point, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 v0 = Subtract(c, a);
    const Vec3 v1 = Subtract(b, a);
    const Vec3 v2 = Subtract(point, a);
    const double dot00 = Dot(v0, v0);
    const double dot01 = Dot(v0, v1);
    const double dot02 = Dot(v0, v2);
    const double dot11 = Dot(v1, v1);
    const double dot12 = Dot(v1, v2);
    const double denom = (dot00 * dot11) - (dot01 * dot01);
    if (std::abs(denom) <= 0.000001) {
        return false;
    }

    const double invDenom = 1.0 / denom;
    const double u = ((dot11 * dot02) - (dot01 * dot12)) * invDenom;
    const double v = ((dot00 * dot12) - (dot01 * dot02)) * invDenom;
    constexpr double tolerance = 0.0001;
    return u >= -tolerance && v >= -tolerance && (u + v) <= 1.0 + tolerance;
}

bool CollisionSegmentVsPolygon(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoCollisionScene& collision,
    size_t polygonIndex, const Vec3& from, const Vec3& to, bool checkOneFace,
    NativeCollisionSegmentHit& hit) {
    hit = {};
    if (polygonIndex >= collision.Polygons.size()) {
        return false;
    }
    const auto& poly = collision.Polygons[polygonIndex];
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
    if ((planeDistA >= 0.0 && planeDistB >= 0.0) ||
        (planeDistA < 0.0 && planeDistB < 0.0) ||
        (checkOneFace && planeDistA < 0.0 && planeDistB > 0.0) ||
        std::abs(planeDistDelta) <= 0.000001) {
        return false;
    }

    const double t = planeDistA / planeDistDelta;
    if (t < 0.0 || t > 1.0) {
        return false;
    }
    const Vec3 point = Add(from, Scale(Subtract(to, from), t));
    const Vec3 a = ToVec3(collision.Vertices[poly.VertexA]);
    const Vec3 b = ToVec3(collision.Vertices[poly.VertexB]);
    const Vec3 c = ToVec3(collision.Vertices[poly.VertexC]);
    if (!PointInTriangle3D(point, a, b, c)) {
        return false;
    }

    hit.Hit = true;
    hit.PolygonIndex = static_cast<int>(polygonIndex);
    hit.Position = point;
    hit.Normal = normal;
    hit.SegmentT = t;
    const Vec3 delta = Subtract(point, from);
    hit.DistanceSqFromStart = Dot(delta, delta);
    return true;
}
