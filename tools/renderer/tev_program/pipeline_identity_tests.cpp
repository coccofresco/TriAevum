#include "fast/renderer3ds/pica_pipeline_identity.h"
#include "fast/renderer3ds/pica_render_backend.h"
#include <iostream>
#include <type_traits>

using namespace Fast::Renderer3ds;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    try {
        static_assert(!std::is_convertible_v<NriPicaPipelineId, VkPipeline>);
        static_assert(!std::is_convertible_v<VkPipeline, NriPicaPipelineId>);
        PicaDevicePipelineRecord ownedOnly;
        ownedOnly.Id={1};ownedOnly.NriPreparationAttempted=true;
        Check(bool(ownedOnly.Id) && ownedOnly.Vulkan==VK_NULL_HANDLE,
              "logical NRI identity requires a Vulkan object");
        PicaPipelineProgramIdentity program{{1,2,100},{3,4,200},{5,6,300},true};
        NriPicaGraphicsPipelineDesc state;
        state.ColorAttachmentCount=1;state.ColorFormats[0]=VK_FORMAT_R8G8B8A8_UNORM;
        state.Colors[0].colorWriteMask=15;
        state.DepthTest=true;state.StencilTest=true;state.LogicOpEnabled=true;
        state.Colors[0].blendEnable=true;
        PicaDrawView draw;
        std::array<PicaVertexBindingView,1> bindings{};
        std::array<PicaVertexAttributeView,1> attributes{};
        bindings[0].ByteStride=16;attributes[0].ComponentCount=4;
        draw.VertexBindings=bindings;draw.VertexAttributes=attributes;
        const auto key=BuildPicaPipelineIdentity(program,draw,state,true);
        for(unsigned material=0;material<1024;++material){
            draw.VertexShaderKey=material;draw.FragmentShaderKey=material*3;
            draw.SubmissionId=material;draw.CanonicalPipelineId=material*7;
            draw.ViewportWidth=float(material+1);draw.GeometryIdentity=material*5;
            Check(key==BuildPicaPipelineIdentity(program,draw,state,true),"draw/material/dynamic state created a pipeline variant");
        }
        unsigned changes=0;
        const auto changed=[&](auto mutate){auto s=state;mutate(s);++changes;
            Check(key!=BuildPicaPipelineIdentity(program,draw,s,true),"immutable GPU state omitted from key");};
        changed([](auto& s){s.Topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;});
        changed([](auto& s){s.CullMode=VK_CULL_MODE_BACK_BIT;});
        changed([](auto& s){s.FrontFace=VK_FRONT_FACE_CLOCKWISE;});
        changed([](auto& s){s.Samples=VK_SAMPLE_COUNT_4_BIT;});
        changed([](auto& s){s.AlphaToCoverage=true;});
        changed([](auto& s){s.DepthTest=false;});
        changed([](auto& s){s.DepthWrite=true;});
        changed([](auto& s){s.DepthCompare=VK_COMPARE_OP_LESS;});
        changed([](auto& s){s.StencilTest=false;});
        for(bool front:{false,true})for(unsigned field=0;field<7;++field)changed([&](auto& s){
            auto& v=front?s.FrontStencil:s.BackStencil;
            switch(field){case 0:v.failOp=VK_STENCIL_OP_REPLACE;break;case 1:v.passOp=VK_STENCIL_OP_REPLACE;break;
                case 2:v.depthFailOp=VK_STENCIL_OP_REPLACE;break;case 3:v.compareOp=VK_COMPARE_OP_EQUAL;break;
                case 4:v.compareMask=3;break;case 5:v.writeMask=3;break;case 6:v.reference=3;break;}
        });
        changed([](auto& s){s.LogicOpEnabled=false;});
        changed([](auto& s){s.LogicOp=VK_LOGIC_OP_XOR;});
        changed([](auto& s){s.DepthStencilFormat=VK_FORMAT_D24_UNORM_S8_UINT;});
        changed([](auto& s){s.ColorAttachmentCount=2;});
        changed([](auto& s){s.ColorFormats[0]=VK_FORMAT_R16G16B16A16_SFLOAT;});
        for(unsigned field=0;field<8;++field)changed([&](auto& s){auto& c=s.Colors[0];
            switch(field){case 0:c.blendEnable=false;break;case 1:c.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;break;
                case 2:c.dstColorBlendFactor=VK_BLEND_FACTOR_ONE;break;case 3:c.colorBlendOp=VK_BLEND_OP_SUBTRACT;break;
                case 4:c.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;break;case 5:c.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;break;
                case 6:c.alphaBlendOp=VK_BLEND_OP_SUBTRACT;break;case 7:c.colorWriteMask=3;break;}
        });
        for(unsigned stage=0;stage<3;++stage)for(unsigned field=0;field<3;++field){auto p=program;
            auto& id=stage==0?p.Vertex:(stage==1?p.Fragment:p.NriFragment);++id[field];
            Check(key!=BuildPicaPipelineIdentity(p,draw,state,true),"effective shader identity omitted");
        }
        auto p=program;p.NriDescriptorContract=false;
        auto dormant=state;
        dormant.DepthTest=false;dormant.StencilTest=false;dormant.LogicOpEnabled=false;
        dormant.Colors[0].blendEnable=false;
        const auto dormantKey=BuildPicaPipelineIdentity(program,draw,dormant,true);
        dormant.DepthCompare=VK_COMPARE_OP_NEVER;
        dormant.FrontStencil={VK_STENCIL_OP_REPLACE,VK_STENCIL_OP_INVERT,VK_STENCIL_OP_ZERO,
                              VK_COMPARE_OP_EQUAL,17,23,31};
        dormant.BackStencil=dormant.FrontStencil;
        dormant.LogicOp=VK_LOGIC_OP_XOR;
        dormant.Colors[0].srcColorBlendFactor=VK_BLEND_FACTOR_DST_ALPHA;
        dormant.Colors[0].dstColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        dormant.Colors[0].colorBlendOp=VK_BLEND_OP_REVERSE_SUBTRACT;
        dormant.Colors[0].srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        dormant.Colors[0].dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        dormant.Colors[0].alphaBlendOp=VK_BLEND_OP_MAX;
        Check(dormantKey==BuildPicaPipelineIdentity(program,draw,dormant,true),
              "dormant register values introduced pipeline variants");
        dormant.DepthWrite=true;
        Check(dormantKey!=BuildPicaPipelineIdentity(program,draw,dormant,true),
              "depth write was incorrectly discarded with depth comparison");
        auto unused=state;unused.Colors.back().colorWriteMask=7;
        Check(key==BuildPicaPipelineIdentity(program,draw,unused,true),"inactive attachment introduced a variant");
        auto mrt=state;mrt.ColorAttachmentCount=2;
        const auto mrtKey=BuildPicaPipelineIdentity(program,draw,mrt,true);
        mrt.Colors[1].colorWriteMask=7;
        Check(mrtKey!=BuildPicaPipelineIdentity(program,draw,mrt,true),"auxiliary output write mask omitted");
        mrt=state;mrt.ColorAttachmentCount=2;mrt.ColorFormats[1]=VK_FORMAT_R16G16B16A16_SFLOAT;
        Check(mrtKey!=BuildPicaPipelineIdentity(program,draw,mrt,true),"auxiliary output format omitted");
        Check(key!=BuildPicaPipelineIdentity(p,draw,state,true),"NRI descriptor contract omitted");
        Check(key!=BuildPicaPipelineIdentity(program,draw,state,false),"render-pass mode omitted");
        bindings[0].ByteStride=32;
        Check(key!=BuildPicaPipelineIdentity(program,draw,state,true),"vertex layout omitted");
        bindings[0].ByteStride=16;attributes[0].ByteOffset=4;
        Check(key!=BuildPicaPipelineIdentity(program,draw,state,true),"vertex attribute offset omitted");
        p=program;p.Vertex[2]=0;bool rejected=false;
        try{(void)BuildPicaPipelineIdentity(p,draw,state,true);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"missing program identity accepted");
        std::cout<<"1024 material identities share a key; "<<changes<<" GPU state mutations, program and layout changes stay distinct\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
