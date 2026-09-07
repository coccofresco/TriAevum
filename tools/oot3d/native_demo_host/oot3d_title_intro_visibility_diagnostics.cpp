#include "oot3d_title_intro_visibility_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "oot3d_demo_host_diagnostics.h"
#include "oot3d_demo_math.h"

namespace {

constexpr double kVisibilityEpsilon = 0.000001;

struct ClipPoint {
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 0.0;
};

struct ProjectedBoundsSummary {
    bool Projected = false;
    bool OverlapsViewport = false;
    size_t ProjectedCornerCount = 0;
    size_t ClipInsideCornerCount = 0;
    double NdcMinX = 0.0;
    double NdcMinY = 0.0;
    double NdcMinZ = 0.0;
    double NdcMaxX = 0.0;
    double NdcMaxY = 0.0;
    double NdcMaxZ = 0.0;
    double PixelMinX = 0.0;
    double PixelMinY = 0.0;
    double PixelMaxX = 0.0;
    double PixelMaxY = 0.0;
};

bool IsTitleIntroVisual(const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& model) {
    return model.Name.rfind("title_intro:", 0) == 0;
}

Vec3 BoundsCenter(const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds) {
    return {
        (bounds.Min.X + bounds.Max.X) * 0.5,
        (bounds.Min.Y + bounds.Max.Y) * 0.5,
        (bounds.Min.Z + bounds.Max.Z) * 0.5,
    };
}

double BoundsMaxExtent(const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return 0.0;
    }
    return std::max({
        std::abs(bounds.Max.X - bounds.Min.X),
        std::abs(bounds.Max.Y - bounds.Min.Y),
        std::abs(bounds.Max.Z - bounds.Min.Z),
    });
}

double ClampUnit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

nlohmann::json BoundsToJson(const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds) {
    if (!bounds.Valid) {
        return nullptr;
    }
    return {
        { "min", DemoVec3ToJson(bounds.Min) },
        { "max", DemoVec3ToJson(bounds.Max) },
        { "center", Vec3ToJson(BoundsCenter(bounds)) },
        { "max_extent", BoundsMaxExtent(bounds) },
    };
}

ClipPoint TransformClipPoint(const ThreeDsRecomp::Oot3d::Matrix4f& transform, const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& point) {
    return {
        transform.M[0][0] * point.X + transform.M[0][1] * point.Y +
            transform.M[0][2] * point.Z + transform.M[0][3],
        transform.M[1][0] * point.X + transform.M[1][1] * point.Y +
            transform.M[1][2] * point.Z + transform.M[1][3],
        transform.M[2][0] * point.X + transform.M[2][1] * point.Y +
            transform.M[2][2] * point.Z + transform.M[2][3],
        transform.M[3][0] * point.X + transform.M[3][1] * point.Y +
            transform.M[3][2] * point.Z + transform.M[3][3],
    };
}

ClipPoint TransformClipPoint(const ThreeDsRecomp::Oot3d::Matrix4f& transform, const ThreeDsRecomp::Oot3d::Vec3f& point) {
    return TransformClipPoint(transform, ThreeDsRecomp::Oot3d::Oot3dDemoVec3{ point.X, point.Y, point.Z });
}

std::vector<ThreeDsRecomp::Oot3d::Oot3dDemoVec3> BoundsCorners(const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds) {
    return {
        { bounds.Min.X, bounds.Min.Y, bounds.Min.Z },
        { bounds.Min.X, bounds.Min.Y, bounds.Max.Z },
        { bounds.Min.X, bounds.Max.Y, bounds.Min.Z },
        { bounds.Min.X, bounds.Max.Y, bounds.Max.Z },
        { bounds.Max.X, bounds.Min.Y, bounds.Min.Z },
        { bounds.Max.X, bounds.Min.Y, bounds.Max.Z },
        { bounds.Max.X, bounds.Max.Y, bounds.Min.Z },
        { bounds.Max.X, bounds.Max.Y, bounds.Max.Z },
    };
}

ProjectedBoundsSummary ProjectBounds(const ThreeDsRecomp::Oot3d::Oot3dDemoBounds& bounds,
                                      const ThreeDsRecomp::Oot3d::Matrix4f& worldToClip,
                                      uint32_t width,
                                      uint32_t height) {
    ProjectedBoundsSummary summary;
    if (!bounds.Valid) {
        return summary;
    }

    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double minZ = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    double maxZ = -std::numeric_limits<double>::infinity();
    size_t projectedCornerCount = 0;
    size_t clipInsideCornerCount = 0;
    for (const auto& corner : BoundsCorners(bounds)) {
        const auto clip = TransformClipPoint(worldToClip, corner);
        if (std::abs(clip.W) <= kVisibilityEpsilon) {
            continue;
        }
        const double invW = 1.0 / clip.W;
        const double ndcX = clip.X * invW;
        const double ndcY = clip.Y * invW;
        const double ndcZ = clip.Z * invW;
        ++projectedCornerCount;
        if (clip.W > 0.0 &&
            ndcX >= -1.0 && ndcX <= 1.0 &&
            ndcY >= -1.0 && ndcY <= 1.0 &&
            ndcZ >= -1.0 && ndcZ <= 1.0) {
            ++clipInsideCornerCount;
        }
        minX = std::min(minX, ndcX);
        minY = std::min(minY, ndcY);
        minZ = std::min(minZ, ndcZ);
        maxX = std::max(maxX, ndcX);
        maxY = std::max(maxY, ndcY);
        maxZ = std::max(maxZ, ndcZ);
    }

    if (projectedCornerCount == 0) {
        return summary;
    }

    const double pixelMinX = (minX * 0.5 + 0.5) * static_cast<double>(width);
    const double pixelMaxX = (maxX * 0.5 + 0.5) * static_cast<double>(width);
    const double pixelMinY = (1.0 - (maxY * 0.5 + 0.5)) * static_cast<double>(height);
    const double pixelMaxY = (1.0 - (minY * 0.5 + 0.5)) * static_cast<double>(height);
    const bool overlapsViewport =
        pixelMaxX >= 0.0 && pixelMinX <= static_cast<double>(width) &&
        pixelMaxY >= 0.0 && pixelMinY <= static_cast<double>(height);

    summary.Projected = true;
    summary.OverlapsViewport = overlapsViewport;
    summary.ProjectedCornerCount = projectedCornerCount;
    summary.ClipInsideCornerCount = clipInsideCornerCount;
    summary.NdcMinX = minX;
    summary.NdcMinY = minY;
    summary.NdcMinZ = minZ;
    summary.NdcMaxX = maxX;
    summary.NdcMaxY = maxY;
    summary.NdcMaxZ = maxZ;
    summary.PixelMinX = pixelMinX;
    summary.PixelMinY = pixelMinY;
    summary.PixelMaxX = pixelMaxX;
    summary.PixelMaxY = pixelMaxY;
    return summary;
}

nlohmann::json ProjectedBoundsToJson(const ProjectedBoundsSummary& projection) {
    if (!projection.Projected) {
        return {
            { "projected", false },
            { "projected_corner_count", projection.ProjectedCornerCount },
            { "clip_inside_corner_count", projection.ClipInsideCornerCount },
        };
    }

    return {
        { "projected", true },
        { "projected_corner_count", projection.ProjectedCornerCount },
        { "clip_inside_corner_count", projection.ClipInsideCornerCount },
        { "overlaps_viewport", projection.OverlapsViewport },
        { "ndc_min", { { "x", projection.NdcMinX }, { "y", projection.NdcMinY }, { "z", projection.NdcMinZ } } },
        { "ndc_max", { { "x", projection.NdcMaxX }, { "y", projection.NdcMaxY }, { "z", projection.NdcMaxZ } } },
        { "pixel_min", { { "x", projection.PixelMinX }, { "y", projection.PixelMinY } } },
        { "pixel_max", { { "x", projection.PixelMaxX }, { "y", projection.PixelMaxY } } },
        { "pixel_width", std::max(0.0, projection.PixelMaxX - projection.PixelMinX) },
        { "pixel_height", std::max(0.0, projection.PixelMaxY - projection.PixelMinY) },
    };
}

nlohmann::json RoomOcclusionEstimateToJson(const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& room,
                                           const ProjectedBoundsSummary& actorProjection,
                                           const ThreeDsRecomp::Oot3d::Matrix4f& worldToClip,
                                           uint32_t width,
                                           uint32_t height) {
    if (!actorProjection.Projected || !actorProjection.OverlapsViewport) {
        return {
            { "available", false },
            { "reason", "actor_projection_not_on_viewport" },
        };
    }

    size_t testedVertexCount = 0;
    size_t projectedRoomVertexInActorRectCount = 0;
    size_t closerRoomVertexInActorRectCount = 0;
    double closestRoomNdcZ = std::numeric_limits<double>::infinity();
    const double actorNearestNdcZ = actorProjection.NdcMinZ;
    for (const auto& batch : room.Batches) {
        for (const auto& vertex : batch.Vertices) {
            ++testedVertexCount;
            const auto world = TransformClipPoint(room.ModelToWorld, vertex.Position);
            const auto clip = TransformClipPoint(
                worldToClip, ThreeDsRecomp::Oot3d::Oot3dDemoVec3{ world.X, world.Y, world.Z });
            if (std::abs(clip.W) <= kVisibilityEpsilon) {
                continue;
            }
            const double invW = 1.0 / clip.W;
            const double ndcX = clip.X * invW;
            const double ndcY = clip.Y * invW;
            const double ndcZ = clip.Z * invW;
            const double pixelX = (ndcX * 0.5 + 0.5) * static_cast<double>(width);
            const double pixelY = (1.0 - (ndcY * 0.5 + 0.5)) * static_cast<double>(height);
            if (pixelX < actorProjection.PixelMinX || pixelX > actorProjection.PixelMaxX ||
                pixelY < actorProjection.PixelMinY || pixelY > actorProjection.PixelMaxY) {
                continue;
            }
            ++projectedRoomVertexInActorRectCount;
            closestRoomNdcZ = std::min(closestRoomNdcZ, ndcZ);
            if (ndcZ < actorNearestNdcZ) {
                ++closerRoomVertexInActorRectCount;
            }
        }
    }

    return {
        { "available", true },
        { "method", "room_vertex_projection_inside_actor_projected_bounds" },
        { "tested_room_vertex_count", testedVertexCount },
        { "projected_room_vertex_in_actor_rect_count", projectedRoomVertexInActorRectCount },
        { "closer_room_vertex_in_actor_rect_count", closerRoomVertexInActorRectCount },
        { "actor_nearest_ndc_z", actorNearestNdcZ },
        { "closest_room_ndc_z_in_actor_rect",
          std::isfinite(closestRoomNdcZ) ? nlohmann::json(closestRoomNdcZ) : nlohmann::json(nullptr) },
        { "occlusion_plausible", closerRoomVertexInActorRectCount > 0 },
    };
}

} // namespace

nlohmann::json TitleIntroVisibilityDiagnosticsToJson(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene,
    const Camera& camera,
    uint32_t width,
    uint32_t height) {
    const double aspect =
        static_cast<double>(std::max<uint32_t>(1, width)) /
        static_cast<double>(std::max<uint32_t>(1, height));
    const auto worldToClip = BuildWorldToClipMatrix(camera, aspect, renderScene.Bounds);
    const Vec3 forward = Normalize(Subtract(camera.Target, camera.Position));
    const Vec3 up = Dot(camera.Up, camera.Up) > kVisibilityEpsilon
                        ? Normalize(camera.Up)
                        : Vec3{ 0.0, 1.0, 0.0 };
    const Vec3 right = Normalize(Cross(forward, up));
    const Vec3 cameraUp = Normalize(Cross(right, forward));
    const bool cameraBasisValid =
        Dot(forward, forward) > kVisibilityEpsilon &&
        Dot(right, right) > kVisibilityEpsilon &&
        Dot(cameraUp, cameraUp) > kVisibilityEpsilon;
    const double verticalHalfFovRadians = DegreesToRadians(camera.FovDegrees) * 0.5;
    const double horizontalHalfFovRadians =
        std::atan(std::tan(verticalHalfFovRadians) * std::max(0.1, aspect));

    nlohmann::json visuals = nlohmann::json::array();
    size_t titleVisualCount = 0;
    size_t inFrontCount = 0;
    size_t inFovCount = 0;

    for (const auto& model : renderScene.ActorVisuals) {
        if (!IsTitleIntroVisual(model)) {
            continue;
        }
        ++titleVisualCount;

        const auto worldBounds = ThreeDsRecomp::Oot3d::Oot3dNativeRenderModelWorldBounds(model);
        const auto projectedBounds = ProjectBounds(worldBounds, worldToClip, width, height);
        const Vec3 center = BoundsCenter(worldBounds);
        const Vec3 offset = Subtract(center, camera.Position);
        const double distance = std::sqrt(Dot(offset, offset));
        const double forwardDistance = Dot(offset, forward);
        const double rightDistance = Dot(offset, right);
        const double upDistance = Dot(offset, cameraUp);
        const bool inFront = cameraBasisValid && forwardDistance > 0.0;
        const double horizontalAngle =
            inFront ? std::atan2(rightDistance, forwardDistance) : 0.0;
        const double verticalAngle =
            inFront ? std::atan2(upDistance, forwardDistance) : 0.0;
        const bool insideFov =
            inFront &&
            std::abs(horizontalAngle) <= horizontalHalfFovRadians &&
            std::abs(verticalAngle) <= verticalHalfFovRadians;
        const double centerAngleDegrees =
            distance > kVisibilityEpsilon && cameraBasisValid
                ? std::acos(ClampUnit(forwardDistance / distance)) * 180.0 / kOot3dDemoPi
                : 0.0;
        if (inFront) {
            ++inFrontCount;
        }
        if (insideFov) {
            ++inFovCount;
        }

        visuals.push_back({
            { "name", model.Name },
            { "source", model.Source },
            { "bounds", BoundsToJson(worldBounds) },
            { "projected_bounds", ProjectedBoundsToJson(projectedBounds) },
            { "room_occlusion_estimate",
              RoomOcclusionEstimateToJson(renderScene.Room, projectedBounds, worldToClip, width, height) },
            { "distance_to_camera", distance },
            { "forward_distance", forwardDistance },
            { "right_distance", rightDistance },
            { "up_distance", upDistance },
            { "center_angle_degrees", centerAngleDegrees },
            { "horizontal_angle_degrees", horizontalAngle * 180.0 / kOot3dDemoPi },
            { "vertical_angle_degrees", verticalAngle * 180.0 / kOot3dDemoPi },
            { "in_front_of_camera", inFront },
            { "center_inside_camera_fov", insideFov },
            { "classification",
              !worldBounds.Valid
                  ? "invalid_bounds"
                  : (!cameraBasisValid
                         ? "invalid_camera_basis"
                         : (!inFront ? "behind_camera"
                                     : (insideFov ? "center_inside_camera_fov"
                                                  : "center_outside_camera_fov"))) },
        });
    }

    return {
        { "format", "oot3d_title_intro_visibility_diagnostics_v1" },
        { "source", "demo_host_camera_vs_native_title_intro_actor_visual_bounds" },
        { "camera_basis_valid", cameraBasisValid },
        { "camera_position", Vec3ToJson(camera.Position) },
        { "camera_target", Vec3ToJson(camera.Target) },
        { "camera_forward", Vec3ToJson(forward) },
        { "camera_right", Vec3ToJson(right) },
        { "camera_up_orthonormal", Vec3ToJson(cameraUp) },
        { "vertical_fov_degrees", camera.FovDegrees },
        { "horizontal_fov_degrees", horizontalHalfFovRadians * 2.0 * 180.0 / kOot3dDemoPi },
        { "aspect", aspect },
        { "viewport_width", width },
        { "viewport_height", height },
        { "title_intro_visual_count", titleVisualCount },
        { "title_intro_visual_in_front_count", inFrontCount },
        { "title_intro_visual_center_in_fov_count", inFovCount },
        { "title_intro_visuals", visuals },
    };
}
