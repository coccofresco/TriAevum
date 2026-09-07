#include "fast/oot3d/pica_rigid_motion.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace Fast::Oot3d {
namespace {
constexpr size_t kFloatUniformOffset = 16U + 4U * 4U * sizeof(uint32_t);
constexpr size_t kVec4Bytes = 4U * sizeof(float);

std::array<float, 16> Identity() {
    return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
}
std::array<float, 16> Multiply(const std::array<float,16>& a,
                              const std::array<float,16>& b) {
    std::array<float,16> result{};
    for(size_t row=0;row<4;++row)for(size_t column=0;column<4;++column)
        for(size_t inner=0;inner<4;++inner)
            result[row*4+column]+=a[row*4+inner]*b[inner*4+column];
    return result;
}

bool PreviousScopedIdentifier(std::string_view token) {
    return token=="uniforms"||token=="conditional_code"||
        token=="address_registers"||token=="get_offset_register"||
        token=="exec_shader"||token=="sanitize_mul"||
        token=="pica_position"||token=="pica_ndc_z"||
        token=="pica_raw_primary_color"||
        token.starts_with("pica_output")||
        token.starts_with("reg_tmp")||token.starts_with("sub_");
}

std::string PreviousScopedSource(std::string_view source) {
    std::string result;result.reserve(source.size()+source.size()/8U);
    for(size_t index=0;index<source.size();){
        const unsigned char value=static_cast<unsigned char>(source[index]);
        if(std::isalpha(value)||source[index]=='_'){
            const size_t start=index++;
            while(index<source.size()){
                const unsigned char next=static_cast<unsigned char>(source[index]);
                if(!std::isalnum(next)&&source[index]!='_')break;
                ++index;
            }
            const std::string_view token=source.substr(start,index-start);
            result.append(token);
            if(PreviousScopedIdentifier(token))result.append("_previous");
        }else result.push_back(source[index++]);
    }
    return result;
}
} // namespace

bool DecodeCommonRigidOriginClip(std::span<const uint8_t> bytes,
                                 bool framebufferFlipped,
                                 std::array<float,4>& originClip) {
    constexpr size_t projectionFirst=0U,viewFirst=4U,modelFirst=20U;
    const auto offset=[](size_t first){return kFloatUniformOffset+first*kVec4Bytes;};
    if(bytes.size()<offset(modelFirst)+3U*kVec4Bytes)return false;
    uint32_t booleanMask=0;
    std::memcpy(&booleanMask,bytes.data(),sizeof(booleanMask));
    if((booleanMask&4U)!=0U)return false; // matrix-palette/skinned path
    auto projection=std::array<float,16>{};auto view=Identity();auto model=Identity();
    std::memcpy(projection.data(),bytes.data()+offset(projectionFirst),4U*kVec4Bytes);
    std::memcpy(view.data(),bytes.data()+offset(viewFirst),3U*kVec4Bytes);
    std::memcpy(model.data(),bytes.data()+offset(modelFirst),3U*kVec4Bytes);
    const auto finite=[](const auto& matrix){return std::all_of(matrix.begin(),matrix.end(),
        [](float value){return std::isfinite(value);});};
    if(!finite(projection)||!finite(view)||!finite(model))return false;
    const auto transform=Multiply(projection,Multiply(view,model));
    originClip={transform[3],transform[7],-transform[11],transform[15]};
    if(framebufferFlipped)originClip[1]=-originClip[1];
    return finite(originClip)&&std::abs(originClip[3])>1.0e-7F;
}

RigidMotionSample PicaRigidMotionTracker::Track(
    uint64_t instanceId,uint64_t frameId,const std::array<float,4>& clip,
    const std::array<float,2>& jitterUv) {
    RigidMotionSample result{};
    const bool currentValid=std::isfinite(clip[0])&&std::isfinite(clip[1])&&
        std::isfinite(clip[3])&&clip[3]>1.0e-7F;
    std::array<float,2> current{};
    if(currentValid)current={clip[0]/clip[3]*0.5F+0.5F+jitterUv[0],
                             clip[1]/clip[3]*0.5F+0.5F+jitterUv[1]};
    if(const auto found=mRecords.find(instanceId);found!=mRecords.end()&&
       found->second.Valid&&currentValid&&found->second.FrameId+1U==frameId){
        result.MotionUv={found->second.ScreenUv[0]-current[0],
                         found->second.ScreenUv[1]-current[1]};
        result.Valid=std::abs(result.MotionUv[0])<=1.0F&&
                     std::abs(result.MotionUv[1])<=1.0F;
    }
    mRecords[instanceId]={frameId,current,currentValid};
    return result;
}
void PicaRigidMotionTracker::Reset(){mRecords.clear();}
void PicaRigidMotionTracker::PruneBeforeFrame(uint64_t frameId){
    std::erase_if(mRecords,[frameId](const auto& item){return item.second.FrameId<frameId;});
}

PicaRigidMotionShaderVariant BuildPicaRigidMotionShaderVariant(
    std::string_view source,uint64_t fragmentKey) {
    PicaRigidMotionShaderVariant result{};
    const size_t main=source.find("void main()");
    const size_t end=source.find_last_of('}');
    if(main==std::string_view::npos||end==std::string_view::npos||end<main)return result;
    result.Source=source;
    result.Source.insert(main,
        "layout(location=3) out vec4 pica_rigid_motion_guide;\n"
        "layout(location=7) in vec4 oot3d_current_clip;\n"
        "layout(location=8) in vec4 oot3d_previous_clip;\n"
        "layout(push_constant) uniform Oot3dDrawState { vec4 rigid_motion; vec4 jitter; } oot3d_draw;\n");
    const size_t adjustedEnd=result.Source.find_last_of('}');
    result.Source.insert(adjustedEnd,
        "    vec4 oot3d_motion = oot3d_draw.rigid_motion;\n"
        "    if (oot3d_draw.rigid_motion.a > 0.5 &&\n"
        "        oot3d_current_clip.w > 1.0e-7 && oot3d_previous_clip.w > 1.0e-7) {\n"
        "        vec2 oot3d_current_uv = oot3d_current_clip.xy / oot3d_current_clip.w * 0.5 + 0.5;\n"
        "        vec2 oot3d_previous_uv = oot3d_previous_clip.xy / oot3d_previous_clip.w * 0.5 + 0.5;\n"
        "        oot3d_motion = vec4(oot3d_previous_uv - oot3d_current_uv, 1.0, 1.0);\n"
        "    }\n"
        "    pica_rigid_motion_guide = vec4(oot3d_motion.xy, oot3d_motion.z, 0.0);\n");
    result.FragmentKey=fragmentKey^0x52494749444d4f54ULL;
    result.Applied=true;
    return result;
}

static PicaRigidMotionShaderVariant BuildTemporalVertexVariantImpl(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView* program,
    bool allowCompatibilityAnalysis) {
    using ::Oot3d::Renderer::PicaVertexShaderHook;

    PicaRigidMotionShaderVariant result{};
    size_t state = std::string_view::npos;
    size_t main = std::string_view::npos;
    size_t mainBody = std::string_view::npos;
    size_t end = std::string_view::npos;
    std::string previousStateStorage;
    std::string previousMainStorage;
    std::string_view previousState;
    std::string_view previousMain;
    if (program != nullptr && program->ValidFor(source)) {
        state = program->Hooks.Offset(
            PicaVertexShaderHook::RegisterStateBegin);
        main = program->Hooks.Offset(
            PicaVertexShaderHook::RegisterStateEnd);
        mainBody = program->Hooks.Offset(
            PicaVertexShaderHook::MainBodyBegin);
        end = program->Hooks.Offset(
            PicaVertexShaderHook::MainBodyEnd);
        previousState = program->PreviousRegisterState;
        previousMain = program->PreviousMainBody;
        result.UsedProvidedProgram = true;
    } else {
        if (!allowCompatibilityAnalysis) {
            return result;
        }
        main = source.find("void main()");
        end = source.find_last_of('}');
        const size_t mainBrace =
            main == std::string_view::npos
                ? std::string_view::npos
                : source.find('{', main);
        state = source.find("vec4 pica_output0 =");
        if (main == std::string_view::npos ||
            mainBrace == std::string_view::npos ||
            state == std::string_view::npos ||
            end == std::string_view::npos || end < mainBrace) {
            return result;
        }
        mainBody = mainBrace + 1U;
        const std::string_view fallbackState =
            source.substr(state, main - state);
        const std::string_view fallbackMain =
            source.substr(mainBody, end - mainBody);
        previousStateStorage = PreviousScopedSource(fallbackState);
        previousMainStorage = PreviousScopedSource(fallbackMain);
        previousState = previousStateStorage;
        previousMain = previousMainStorage;
    }
    if (state > main || main > mainBody || mainBody > end ||
        end > source.size()) {
        return result;
    }
    const std::string_view prefix = source.substr(0U, state);
    const std::string_view currentState =
        source.substr(state, main - state);
    const std::string_view currentMain =
        source.substr(mainBody, end - mainBody);
    result.Source.reserve(source.size()*2U);
    result.Source.append(prefix);
    result.Source.append(
        "layout(location=7) out vec4 oot3d_current_clip;\n"
        "layout(location=8) out vec4 oot3d_previous_clip;\n"
        "layout(set=0,binding=6,std140) uniform PicaPreviousVertexUniforms {\n"
        "    uint b; int flip_viewport; uvec4 i[4]; vec4 f[96];\n"
        "} uniforms_previous;\n"
        "layout(push_constant) uniform Oot3dDrawState { vec4 rigid_motion; vec4 jitter; } oot3d_draw;\n");
    result.Source.append(currentState);
    result.Source.append(previousState);
    result.Source.append("void main() {\n");
    result.Source.append(previousMain);
    result.Source.append(
        "    vec4 oot3d_previous_position = gl_Position;\n");
    result.Source.append(currentMain);
    result.Source.append(
        "    gl_Position.xy += oot3d_draw.jitter.xy * gl_Position.w;\n"
        "    oot3d_previous_position.xy += oot3d_draw.jitter.zw * oot3d_previous_position.w;\n"
        "    oot3d_current_clip = gl_Position;\n"
        "    oot3d_previous_clip = oot3d_previous_position;\n"
        "}\n");
    result.FragmentKey=vertexKey^0x54454d504a495454ULL;
    result.Applied=true;
    return result;
}

PicaRigidMotionShaderVariant BuildPicaTemporalVertexInstrumentation(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView& program) {
    return BuildTemporalVertexVariantImpl(
        source, vertexKey, &program, false);
}

PicaRigidMotionShaderVariant BuildPicaTemporalVertexVariant(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView* program) {
    return BuildTemporalVertexVariantImpl(
        source, vertexKey, program, true);
}

} // namespace Fast::Oot3d
