#include "oot3d_title_intro_actor_diagnostics.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

ThreeDsRecomp::Oot3d::Matrix4f TitleIntroMultiplyMatrix(const ThreeDsRecomp::Oot3d::Matrix4f& left,
                                               const ThreeDsRecomp::Oot3d::Matrix4f& right) {
    ThreeDsRecomp::Oot3d::Matrix4f out{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t col = 0; col < 4; ++col) {
            float value = 0.0f;
            for (size_t k = 0; k < 4; ++k) {
                value += left.M[row][k] * right.M[k][col];
            }
            out.M[row][col] = value;
        }
    }
    return out;
}

ThreeDsRecomp::Oot3d::Oot3dDemoVec3 TitleIntroTransformDemoPoint(
    const ThreeDsRecomp::Oot3d::Matrix4f& transform,
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 point) {
    const double x = transform.M[0][0] * point.X + transform.M[0][1] * point.Y +
                     transform.M[0][2] * point.Z + transform.M[0][3];
    const double y = transform.M[1][0] * point.X + transform.M[1][1] * point.Y +
                     transform.M[1][2] * point.Z + transform.M[1][3];
    const double z = transform.M[2][0] * point.X + transform.M[2][1] * point.Y +
                     transform.M[2][2] * point.Z + transform.M[2][3];
    const double w = transform.M[3][0] * point.X + transform.M[3][1] * point.Y +
                     transform.M[3][2] * point.Z + transform.M[3][3];
    const double invW = std::abs(w) > 0.000001 ? 1.0 / w : 1.0;
    return { x * invW, y * invW, z * invW };
}

nlohmann::json TitleIntroDemoPointJson(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& value) {
    return {
        { "x", value.X },
        { "y", value.Y },
        { "z", value.Z },
    };
}

nlohmann::json TitleIntroMatrix4fJson(const ThreeDsRecomp::Oot3d::Matrix4f& matrix) {
    nlohmann::json out = nlohmann::json::array();
    for (size_t row = 0; row < 4; ++row) {
        nlohmann::json rowJson = nlohmann::json::array();
        for (size_t column = 0; column < 4; ++column) {
            rowJson.push_back(matrix.M[row][column]);
        }
        out.push_back(rowJson);
    }
    return out;
}

nlohmann::json TitleIntroMatrixDiagnosticJson(const ThreeDsRecomp::Oot3d::Matrix4f& matrix) {
    return {
        { "matrix", TitleIntroMatrix4fJson(matrix) },
        { "translation", TitleIntroDemoPointJson(TitleIntroTransformDemoPoint(matrix, {})) },
    };
}

} // namespace

nlohmann::json TitleIntroActorBatchBoneDiagnostics(
    const ThreeDsRecomp::Oot3d::CmbModel& model,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& renderModel,
    size_t maxBatchCount) {
    nlohmann::json batches = nlohmann::json::array();
    const size_t count = std::min(maxBatchCount, renderModel.Batches.size());
    for (size_t batchIndex = 0; batchIndex < count; ++batchIndex) {
        const auto& batch = renderModel.Batches[batchIndex];
        nlohmann::json boneIndices = nlohmann::json::array();
        nlohmann::json influencedBoneIndices = nlohmann::json::array();
        if (batch.ShapeIndex < model.Shapes.size()) {
            const auto& shape = model.Shapes[batch.ShapeIndex];
            if (batch.PrimitiveIndex < shape.Primitives.size()) {
                const auto& primitive = shape.Primitives[batch.PrimitiveIndex];
                for (const auto boneIndex : primitive.BoneIndices) {
                    boneIndices.push_back(boneIndex);
                }

                std::vector<uint16_t> seen;
                for (const auto& influences : primitive.VertexInfluences) {
                    for (const auto& influence : influences) {
                        if (std::find(seen.begin(), seen.end(), influence.BoneIndex) == seen.end()) {
                            seen.push_back(influence.BoneIndex);
                        }
                    }
                }
                for (const auto boneIndex : seen) {
                    influencedBoneIndices.push_back(boneIndex);
                }
            }
        }
        batches.push_back({
            { "batch_index", batchIndex },
            { "mesh_index", batch.MeshIndex },
            { "shape_index", batch.ShapeIndex },
            { "visibility_id", batch.VisibilityId },
            { "primitive_index", batch.PrimitiveIndex },
            { "material_index", batch.MaterialIndex },
            { "skinning_mode", batch.SkinningMode },
            { "vertex_count", batch.Vertices.size() },
            { "primitive_bone_indices", boneIndices },
            { "influenced_bone_indices", influencedBoneIndices },
        });
    }
    return batches;
}

nlohmann::json TitleIntroActorTransformDiagnostics(
    const TitleIntroNativeActor& actor,
    const ThreeDsRecomp::Oot3d::Oot3dNativeRenderModel& renderModel,
    const ThreeDsRecomp::Oot3d::CsabPose& pose,
    const std::vector<ThreeDsRecomp::Oot3d::Matrix4f>& skinTransforms,
    float poseFrame,
    ThreeDsRecomp::Oot3d::Oot3dDemoVec3 drawPosition) {
    nlohmann::json poseMatrices = nlohmann::json::array();
    nlohmann::json modelPoseMatrices = nlohmann::json::array();
    nlohmann::json skinMatrices = nlohmann::json::array();
    nlohmann::json modelSkinMatrices = nlohmann::json::array();
    std::vector<uint16_t> referencedBoneIndices;
    for (const auto& batch : renderModel.Batches) {
        if (batch.ShapeIndex >= actor.Model.Shapes.size()) {
            continue;
        }
        const auto& shape = actor.Model.Shapes[batch.ShapeIndex];
        if (batch.PrimitiveIndex >= shape.Primitives.size()) {
            continue;
        }
        const auto& primitive = shape.Primitives[batch.PrimitiveIndex];
        for (const auto boneIndex : primitive.BoneIndices) {
            if (std::find(referencedBoneIndices.begin(), referencedBoneIndices.end(), boneIndex) ==
                referencedBoneIndices.end()) {
                referencedBoneIndices.push_back(boneIndex);
            }
        }
    }
    const size_t poseCount = std::min<size_t>(16, pose.WorldTransforms.size());
    for (size_t i = 0; i < poseCount; ++i) {
        poseMatrices.push_back({
            { "bone_index", i },
            { "pose_world", TitleIntroMatrixDiagnosticJson(pose.WorldTransforms[i]) },
        });
        modelPoseMatrices.push_back({
            { "bone_index", i },
            { "model_pose_world", TitleIntroMatrixDiagnosticJson(
                  TitleIntroMultiplyMatrix(renderModel.ModelToWorld, pose.WorldTransforms[i])) },
        });
    }
    const size_t skinCount = std::min<size_t>(16, skinTransforms.size());
    for (size_t i = 0; i < skinCount; ++i) {
        skinMatrices.push_back({
            { "bone_index", i },
            { "skin", TitleIntroMatrixDiagnosticJson(skinTransforms[i]) },
        });
        modelSkinMatrices.push_back({
            { "bone_index", i },
            { "model_skin", TitleIntroMatrixDiagnosticJson(
                  TitleIntroMultiplyMatrix(renderModel.ModelToWorld, skinTransforms[i])) },
        });
    }
    nlohmann::json referencedMatrices = nlohmann::json::array();
    for (const auto boneIndex : referencedBoneIndices) {
        nlohmann::json bone = {
            { "bone_index", boneIndex },
        };
        if (boneIndex < pose.WorldTransforms.size()) {
            bone["model_pose_world"] = TitleIntroMatrixDiagnosticJson(
                TitleIntroMultiplyMatrix(renderModel.ModelToWorld, pose.WorldTransforms[boneIndex]));
        }
        if (boneIndex < skinTransforms.size()) {
            bone["model_skin"] = TitleIntroMatrixDiagnosticJson(
                TitleIntroMultiplyMatrix(renderModel.ModelToWorld, skinTransforms[boneIndex]));
        }
        referencedMatrices.push_back(std::move(bone));
    }

    return {
        { "format", "oot3d_title_intro_actor_transform_diagnostics_v1" },
        { "basis", "diagnostic only: model_to_world, native CMB primitive bone indices, sampled CSAB pose matrices, and inverse-bind skin matrices for comparison against Azahar PICA f20+ shader uniforms" },
        { "role", actor.Role },
        { "cmb_name", actor.CmbName },
        { "csab_name", actor.CsabName },
        { "actor_scale", actor.Scale },
        { "pose_frame", poseFrame },
        { "draw_position", TitleIntroDemoPointJson(drawPosition) },
        { "model_to_world", TitleIntroMatrixDiagnosticJson(renderModel.ModelToWorld) },
        { "batch_bone_map_first16", TitleIntroActorBatchBoneDiagnostics(actor.Model, renderModel, 16) },
        { "pose_world_first16", poseMatrices },
        { "model_pose_world_first16", modelPoseMatrices },
        { "skin_first16", skinMatrices },
        { "model_skin_first16", modelSkinMatrices },
        { "model_referenced_bone_matrices", referencedMatrices },
    };
}
