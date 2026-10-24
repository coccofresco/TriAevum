#include "fast/extension/diffuse_gi_adapter.h"

// OOT3D adapter of the screen-space diffuse GI: own code. The conventions are
// the renderer's declared guide contracts (pica_geometry_guide.h, the surface
// guide of the per-pixel lighting, the motion vectors, the sample
// perspective); nothing here is a GI algorithm.

#include "fast/renderer/diffuse_gi_pass_shader.h"
#include "fast/renderer3ds/pica_geometry_guide.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace Fast::Extension {
namespace {

using Renderer::DiffuseGi::DepthCamera;
using Renderer::DiffuseGi::DepthEncoding;
using Renderer::DiffuseGi::RigidTransform;

using Matrix = std::array<double, 16>; // column-major: element (row r, column c) at [c * 4 + r]

double At(const Matrix& m, int r, int c) noexcept { return m[c * 4 + r]; }

Matrix Widen(const std::array<float, 16>& m) noexcept {
    Matrix out{};
    for (size_t i = 0; i < 16; ++i) out[i] = m[i];
    return out;
}

Matrix Multiply(const Matrix& a, const Matrix& b) noexcept {
    Matrix out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += At(a, r, k) * At(b, k, c);
            out[c * 4 + r] = sum;
        }
    return out;
}

// Gauss-Jordan with partial pivoting; empty when singular.
std::optional<Matrix> Invert(const Matrix& m) noexcept {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = At(m, r, c);
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    for (int c = 0; c < 4; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[pivot][c])) pivot = r;
        if (!(std::abs(a[pivot][c]) > 1.0e-30) || !std::isfinite(a[pivot][c])) return std::nullopt;
        if (pivot != c)
            for (int k = 0; k < 8; ++k) std::swap(a[c][k], a[pivot][k]);
        const double inverse = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k) a[c][k] *= inverse;
        for (int r = 0; r < 4; ++r) {
            if (r == c) continue;
            const double f = a[r][c];
            for (int k = 0; k < 8; ++k) a[r][k] -= f * a[c][k];
        }
    }
    Matrix out{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = a[r][c + 4];
    return out;
}

float SrgbToLinear(float c) noexcept { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); }

} // namespace

DepthCamera DiffuseGiGuideCamera(const PerspectiveViewState& view, PicaSurfaceCoordinates coordinates, bool wBuffer,
                                 const std::array<float, 2>& jitterUv) noexcept {
    DepthCamera camera;
    camera.Encoding = wBuffer ? DepthEncoding::LinearWBuffer : DepthEncoding::PerspectiveZeroToOne;
    camera.Near = view.NearPlane;
    camera.Far = view.FarPlane;
    const auto zero = coordinates.StorageToViewNdc({0.0F, 0.0F});
    const auto x = coordinates.StorageToViewNdc({1.0F, 0.0F});
    const auto y = coordinates.StorageToViewNdc({0.0F, 1.0F});
    const double p0 = view.Projection[0], p5 = view.Projection[5];
    const double p8 = view.Projection[8], p9 = view.Projection[9];
    // X/z = (ndc.x + P[8]) / P[0], Y/z = (ndc.y + P[9]) / P[5] (w = -Z).
    camera.ViewX = {(x[0] - zero[0]) / p0, (y[0] - zero[0]) / p0, (zero[0] + p8) / p0};
    camera.ViewY = {(x[1] - zero[1]) / p5, (y[1] - zero[1]) / p5, (zero[1] + p9) / p5};
    // A texel at storage uv shows the unjittered point at uv - jitter.
    for (std::array<double, 3>* row : {&camera.ViewX, &camera.ViewY})
        (*row)[2] -= (*row)[0] * jitterUv[0] + (*row)[1] * jitterUv[1];
    return camera;
}

std::optional<RigidTransform> DiffuseGiCurrentToPrevious(const std::array<float, 16>& projection,
                                                         const TemporalViewState& temporal) noexcept {
    const Matrix p = Widen(projection);
    const auto inverseP = Invert(p);
    if (!inverseP) return std::nullopt;
    // V_prev V_cur^-1 = P^-1 W_prev W_cur^-1 P.
    const Matrix x = Multiply(Multiply(*inverseP, Widen(temporal.PreviousWorldToClip)),
                              Multiply(Widen(temporal.InverseCurrentWorldToClip), p));
    RigidTransform t;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) t.R[3 * r + c] = At(x, r, c);
        t.T[r] = At(x, r, 3);
    }
    for (int c = 0; c < 4; ++c)
        if (!(std::abs(At(x, 3, c) - (c == 3 ? 1.0 : 0.0)) <= 1.0e-3)) return std::nullopt;
    for (int a = 0; a < 3; ++a)
        for (int b = 0; b < 3; ++b) {
            double dot = 0.0;
            for (int k = 0; k < 3; ++k) dot += t.R[3 * a + k] * t.R[3 * b + k];
            if (!(std::abs(dot - (a == b ? 1.0 : 0.0)) <= 1.0e-3)) return std::nullopt;
        }
    for (const double v : t.T)
        if (!std::isfinite(v)) return std::nullopt;
    return t;
}

DiffuseGiAdapterParams MakeDiffuseGiAdapterParams(const Renderer::DiffuseGi::PassExtent& extent,
                                                  const DepthCamera& camera, bool shading) noexcept {
    DiffuseGiAdapterParams p;
    const auto output = Renderer::DiffuseGi::PassOutputExtent(extent);
    const auto motion = Renderer::DiffuseGi::WorkMotionScale(extent);
    p.Extent = {output[0], output[1], extent.BufferWidth, extent.BufferHeight};
    p.Scale = {std::max(extent.WorkFactor, 1U), extent.WorkPhase,
               camera.Encoding == DepthEncoding::LinearWBuffer ? 3U : 1U,
               (extent.WorkFactor > 1U ? 1U : 0U) | (shading ? 2U : 0U)};
    p.Depth = {static_cast<float>(camera.Near), static_cast<float>(camera.Far), 0.0F, 0.0F};
    p.Motion = {static_cast<float>(motion[0]), static_cast<float>(motion[1]), 0.0F, 0.0F};
    return p;
}

DiffuseGiAdapterTexel DiffuseGiAdapterTexelF32(const DiffuseGiAdapterParams& p, float d,
                                               const std::array<uint32_t, 4>& geometry,
                                               const std::array<float, 4>& surface,
                                               const std::array<float, 2>& motion,
                                               const std::array<float, 4>& normalGuide) noexcept {
    DiffuseGiAdapterTexel out;
    out.Depth = d;
    out.Class = !(d < 1.0F) ? 3U : (surface[3] > 0.0F ? 1U : 0U);
    const auto n = geometry[2] != 0U ? Renderer3ds::DecodePicaGeometryGuideNormal(geometry[1])
                                     : std::array<float, 3>{};
    out.Normal = n;
    const float nearPlane = p.Depth[0], farPlane = p.Depth[1];
    const float z = p.Scale[2] == 3U ? nearPlane + d * (farPlane - nearPlane)
                                     : nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
    const bool receiver = out.Class == 1U && z > 0.0F && std::isfinite(z) && n[0] * n[0] + n[1] * n[1] + n[2] * n[2] > 0.0F;
    if (receiver) out.Guide = {std::bit_cast<uint32_t>(z), Renderer::DiffuseGi::EncodeDiffuseGiNormal(n)};
    if ((p.Scale[3] & 2U) != 0U) {
        const std::array<float, 3> raw{normalGuide[0] * 2.0F - 1.0F, normalGuide[1] * 2.0F - 1.0F,
                                       normalGuide[2] * 2.0F - 1.0F};
        const float length2 = raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2];
        if (normalGuide[3] > 0.0F && length2 > 0.0625F && std::isfinite(length2)) {
            const float inverse = 1.0F / std::sqrt(length2);
            out.Shading = {raw[0] * inverse, raw[1] * inverse, raw[2] * inverse};
            if (out.Shading[0] * n[0] + out.Shading[1] * n[1] + out.Shading[2] * n[2] < 0.0F)
                out.Shading = {-out.Shading[0], -out.Shading[1], -out.Shading[2]};
        }
        if ((p.Scale[3] & 1U) != 0U && receiver)
            out.ShadingGuide = Renderer::DiffuseGi::EncodeDiffuseGiNormal(out.Shading);
    }
    for (int c = 0; c < 3; ++c) {
        const float bits = std::floor(surface[c] * 65535.0F + 0.5F);
        out.Radiance[c] = SrgbToLinear(std::fmod(bits, 256.0F) / 255.0F);
    }
    out.Motion = {motion[0] * p.Motion[0], motion[1] * p.Motion[1]};
    return out;
}

} // namespace Fast::Extension

#ifdef ENABLE_OOT3D_VULKAN
namespace Fast::Extension {

DiffuseGiAdapterKernel::~DiffuseGiAdapterKernel() { Destroy(); }

bool DiffuseGiAdapterKernel::Create(VkDevice device, const std::vector<uint32_t>& spirv) {
    Destroy();
    mDevice = device;
    const auto start = std::chrono::steady_clock::now();
    if (device == VK_NULL_HANDLE || spirv.size() < 5U || spirv[0] != 0x07230203U) return false;
    std::array<VkDescriptorSetLayoutBinding, 13> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
        bindings[i] = {i,
                       i < 4U || i == 10U ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                       VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    setInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &mSetLayout) != VK_SUCCESS) {
        Destroy();
        return false;
    }
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DiffuseGiAdapterParams)};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &mSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &mPipelineLayout) != VK_SUCCESS) {
        Destroy();
        return false;
    }
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleInfo.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
        Destroy();
        return false;
    }
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module,
                  "main", nullptr};
    info.layout = mPipelineLayout;
    const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &mPipeline);
    vkDestroyShaderModule(device, module, nullptr);
    if (result != VK_SUCCESS) {
        mPipeline = VK_NULL_HANDLE;
        Destroy();
        return false;
    }
    mMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return true;
}

void DiffuseGiAdapterKernel::Destroy() noexcept {
    if (mDevice == VK_NULL_HANDLE) return;
    if (mPipeline != VK_NULL_HANDLE) vkDestroyPipeline(mDevice, mPipeline, nullptr);
    if (mPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(mDevice, mPipelineLayout, nullptr);
    if (mSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(mDevice, mSetLayout, nullptr);
    mPipeline = VK_NULL_HANDLE;
    mPipelineLayout = VK_NULL_HANDLE;
    mSetLayout = VK_NULL_HANDLE;
}

bool RecordDiffuseGiAdapter(VkDevice device, VkCommandBuffer command, const DiffuseGiAdapterKernel& kernel,
                            VkDescriptorSet set, const DiffuseGiGuideViews& guides,
                            const Renderer::DiffuseGi::DiffuseGiPass& pass, const DiffuseGiAdapterParams& params) {
    using Renderer::DiffuseGi::PassResourceId;
    if (!kernel.Available() || set == VK_NULL_HANDLE || guides.Depth == VK_NULL_HANDLE ||
        guides.Geometry == VK_NULL_HANDLE || guides.Surface == VK_NULL_HANDLE || guides.Motion == VK_NULL_HANDLE)
        return false;
    // M8: shading normals need the normal guide; without one the surface guide
    // stands in at its binding (never read: the shading flag is off).
    if ((params.Scale[3] & 2U) != 0U && guides.Normal == VK_NULL_HANDLE) return false;
    std::array<VkDescriptorImageInfo, 5> images{{
        {VK_NULL_HANDLE, guides.Depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, guides.Geometry, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, guides.Surface, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, guides.Motion, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, guides.Normal != VK_NULL_HANDLE ? guides.Normal : guides.Surface,
         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    const std::array<PassResourceId, 8> outputs{
        PassResourceId::Depth,  PassResourceId::Class,       PassResourceId::Radiance,      PassResourceId::Normal,
        PassResourceId::Motion, PassResourceId::OutputGuide, PassResourceId::ShadingNormal, PassResourceId::OutputShading};
    std::array<VkDescriptorBufferInfo, 8> buffers{};
    for (size_t k = 0; k < outputs.size(); ++k) {
        auto view = pass.Buffer(outputs[k]);
        // Without a work scale there is no output guide, without shading
        // normals no shading buffers: the kernel writes none and the binding
        // takes the pass's placeholder.
        if (view.Buffer == VK_NULL_HANDLE) view = pass.Buffer(PassResourceId::Placeholder);
        if (view.Buffer == VK_NULL_HANDLE) return false;
        buffers[k] = {view.Buffer, 0, VK_WHOLE_SIZE};
    }
    std::array<VkWriteDescriptorSet, 13> writes{};
    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        // 0-3 and 10 sampled guides; 4-9 and 11-12 pass buffers.
        if (i < 4U || i == 10U) {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[i].pImageInfo = &images[i < 4U ? i : 4U];
        } else {
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffers[i < 10U ? i - 4U : i - 5U];
        }
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    // Earlier reads of these buffers (the previous frame of this instance) ->
    // these writes.
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.Pipeline());
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.PipelineLayout(), 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(command, kernel.PipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(params), &params);
    vkCmdDispatch(command, (params.Extent[0] + 7U) / 8U, (params.Extent[1] + 7U) / 8U, 1);
    return true;
}

} // namespace Fast::Extension
#endif
