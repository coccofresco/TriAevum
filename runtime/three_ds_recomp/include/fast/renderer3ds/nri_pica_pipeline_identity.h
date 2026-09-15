#pragma once

#include "fast/renderer3ds/pica_pipeline_identity.h"
#include <algorithm>
#include <map>

namespace Fast::Renderer3ds {

// Device-local identity for the actual NRI input contract. Called on pipeline
// preparation, never per draw. Interning compares full SPIR-V, without hash
// collisions or storing a copy of each program in every pipeline key.
class NriPicaPipelineIdentity {
  public:
    std::vector<uint8_t> Build(const NriPicaGraphicsPipelineDesc& desc) {
        struct LayoutBinding { uint32_t Binding, ByteStride, PerInstance; };
        struct LayoutAttribute {
            uint32_t Location, Binding, Format, ComponentCount, ByteOffset;
        };
        struct Layout {
            std::vector<LayoutBinding> VertexBindings;
            std::vector<LayoutAttribute> VertexAttributes;
        } layout;
        for (const auto& b : desc.VertexBindings)
            layout.VertexBindings.push_back({b.binding, b.stride, uint32_t(b.inputRate)});
        for (const auto& a : desc.VertexAttributes)
            layout.VertexAttributes.push_back({a.location, a.binding, uint32_t(a.format), 0, a.offset});
        PicaPipelineProgramIdentity programs;
        programs.Vertex = {Intern(desc.VertexSpirv), 0, desc.VertexSpirv.size_bytes()};
        programs.Fragment = {Intern(desc.FragmentSpirv), 0, desc.FragmentSpirv.size_bytes()};
        auto state = desc;
        // NRI binds stencil references dynamically. The Vulkan fallback key
        // deliberately retains them because its pipeline bakes the reference.
        state.FrontStencil.reference = state.BackStencil.reference = 0;
        return BuildPicaPipelineIdentity(programs, layout, state, true);
    }

    void Clear() { mPrograms.clear(); }
    size_t ProgramCount() const { return mPrograms.size(); }

  private:
    struct Less {
        using is_transparent = void;
        bool operator()(std::span<const uint32_t> a, std::span<const uint32_t> b) const {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
        }
    };
    uint64_t Intern(std::span<const uint32_t> program) {
        if (program.empty()) throw std::invalid_argument("NRI pipeline requires SPIR-V");
        if (const auto found = mPrograms.find(program); found != mPrograms.end())
            return found->second;
        const uint64_t id = mPrograms.size() + 1;
        mPrograms.emplace(std::vector<uint32_t>(program.begin(), program.end()), id);
        return id;
    }
    std::map<std::vector<uint32_t>, uint64_t, Less> mPrograms;
};

} // namespace Fast::Renderer3ds
