#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_pica_vulkan_plan.h"
#include "fast/renderer3ds/pica_camera_temporal_policy.h"
#include "oot3d_native_pica_affine_layout.h"
#include <limits>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

namespace {

[[noreturn]] void Fail(std::string_view message) {
    std::cerr << "oot3d_native_pica_vulkan_plan_tests: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, std::string_view message) {
    if (!condition) {
        Fail(message);
    }
}

void WriteFloat(std::vector<uint8_t>& bytes, size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

float ReadFloat(std::span<const uint8_t> bytes, size_t offset) {
    float value = 0.0F;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

uint64_t HashBytes(std::span<const uint8_t> bytes) {
    uint64_t hash = 1469598103934665603ULL;
    for (const uint8_t value : bytes) {
        hash = (hash ^ value) * 1099511628211ULL;
    }
    return hash == 0U ? 1U : hash;
}

void TestFactoredCmbUniformInterpolation() {
    using namespace Oot3dNativeGame;
    using Rows = std::array<std::array<float,4>,3>;
    const Rows identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    const Rows turn{{{-1,0,0,200},{0,-1,0,40},{0,0,1,12}}};
    const auto put = [](auto& plan, size_t first, const Rows& rows) {
        for (size_t i=0; i<3; ++i) plan.VertexShader.Uniforms.Floats[first+i]=rows[i];
    };
    const auto make = [&] {
        Oot3dPicaVulkanDrawPlan plan;
        auto program = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        // Exercise the consumer with the adapter's typed contract. Actual
        // SHBIN -> generator recognition is covered by the frontend suite.
        program->AffineLayout = kOot3dCmbAffineLayout;
        plan.VertexShader.TemporalProgram = program;
        plan.VertexShader.StateKey = 17;
        plan.VertexShader.Uniforms.BooleanMask = 4;
        plan.VertexShader.Uniforms.Floats[93] = {0,1,2,3};
        plan.VertexShader.Uniforms.Floats[95] = {3,4,5,32};
        plan.VertexCount = 10;
        plan.VertexAttributes.push_back({6,0,Oot3dPicaVertexFormat::UnsignedByte,1,0});
        Oot3dPicaVulkanVertexBinding binding;
        binding.ByteStride = 1;
        for (uint8_t i=0;i<10;++i) binding.Bytes.push_back(i);
        plan.VertexBindings.push_back(binding);
        put(plan,4,identity);
        for (size_t i=0;i<10;++i) put(plan,20+3*i,identity);
        return plan;
    };
    const auto sample = [&](const auto& a, const auto& b, float alpha) {
        Oot3dPicaVulkanDrawPlan out;
        Require(InterpolateOot3dPicaVisualDraw(a,b,alpha,out),"factored draws incompatible");
        return out.VertexShader.Uniforms.Floats;
    };
    const auto near = [](float a,float b) { return std::abs(a-b)<0.0003F; };
    const auto scalar = [&](const auto& a,const auto& b) {
        const auto actual=sample(a,b,0.5F);
        for (size_t r=0;r<96;++r) for(size_t c=0;c<4;++c) {
            const float x=a.VertexShader.Uniforms.Floats[r][c], y=b.VertexShader.Uniforms.Floats[r][c];
            const float expected=(r<89 || r==90 || r==91) ? x+(y-x)*0.5F : y;
            Require(near(actual[r][c],expected),"whole-draw scalar fallback changed");
        }
    };
    auto before=make(), after=before;
    for (size_t i=0;i<10;++i) put(after,20+3*i,turn);
    // Orthogonal-looking non-matrices and boundary sentinels must remain scalar.
    for (size_t r : {0U,7U,10U,17U,19U,50U,76U,80U,81U,82U,88U,90U,91U}) {
        before.VertexShader.Uniforms.Floats[r]={1,2,3,4};
        after.VertexShader.Uniforms.Floats[r]={-1,6,-3,8};
    }
    Oot3dPicaVisualFrame aFrame,bFrame;
    aFrame.Sequence=1; bFrame.Sequence=2;
    aFrame.Draws.push_back(before); bFrame.Draws.push_back(after);
    Oot3dPicaPreparedVisualTransition prepared;
    Require(prepared.Prepare(aFrame,bFrame),"factored transition prepare failed");
    for(float alpha : {0.0F,1.0F/3,0.5F,2.0F/3,1.0F}) {
        const auto values=sample(before,after,alpha);
        Oot3dPicaVisualFrameViewSample view;
        Oot3dPicaVisualFrameSample frame;
        Require(prepared.Sample(aFrame,bFrame,alpha,view) && view.Draws.size()==1,
                "factored prepared sample failed");
        Require(SampleOot3dPicaVisualFrame(aFrame,bFrame,alpha,frame) && frame.Draws.size()==1,
                "factored full-frame sample failed");
        const auto& vu=view.Draws[0].VertexUniforms ? *view.Draws[0].VertexUniforms
            : view.Draws[0].BasePlan->VertexShader.Uniforms;
        Require(vu.Floats==values && frame.Draws[0].VertexShader.Uniforms.Floats==values,
                "draw/full-frame/prepared-view factored output differs");
        if(alpha==0 || alpha==1) {
            const auto& endpoint=alpha==0 ? before : after;
            Require(std::memcmp(values.data(),endpoint.VertexShader.Uniforms.Floats.data(),sizeof(values))==0,
                    "factored endpoint is not bit-exact");
        }
        for(size_t slot=0;slot<10;++slot) {
            const size_t first=20+3*slot;
            for(size_t i=0;i<3;++i) for(size_t j=0;j<3;++j) {
                float dot=0;for(size_t k=0;k<3;++k)dot+=values[first+i][k]*values[first+j][k];
                Require(near(dot,i==j?1.0F:0.0F),"whole-body half turn lost rigidity");
            }
            // The screw goes around (100,20,0), advancing 12 along Z.
            for(size_t i=0;i<3;++i) {
                const float pivot=100*values[first+i][0]+20*values[first+i][1]+values[first+i][3];
                Require(near(pivot,i==0?100.0F:i==1?20.0F:12*alpha),"screw pivot/axial translation wrong");
            }
        }
        for(size_t row=0;row<96;++row) if(!(row>=4&&row<7)&&!(row>=20&&row<50))
            for(size_t c=0;c<4;++c) {
                const float x=before.VertexShader.Uniforms.Floats[row][c],y=after.VertexShader.Uniforms.Floats[row][c];
                const float expected=(row<89||row==90||row==91)?x+(y-x)*alpha:y;
                Require(near(values[row][c],expected),"factoring crossed a proven matrix boundary");
            }
    }
    // Local motion about the shared bind-space joint, followed by a global
    // half turn plus translation. One selected matrix is deliberately sheared.
    before=make(); after=before;
    const Rows local{{{1,0.25F,0,0},{0,0,-1,0},{0,1,0,0}}};
    put(after,20,turn);
    Rows composed{};
    for(size_t i=0;i<3;++i)for(size_t j=0;j<4;++j) {
        composed[i][j]=j==3?turn[i][3]:0;
        for(size_t k=0;k<3;++k)composed[i][j]+=turn[i][k]*local[k][j];
    }
    put(after,23,composed);
    for(float alpha : {1.0F/3,0.5F,2.0F/3}) {
        auto values=sample(before,after,alpha);
        for(size_t r=0;r<3;++r)
            Require(near(100*values[20+r][0]+values[20+r][3],100*values[23+r][0]+values[23+r][3]),
                    "shared joint separated during factored local animation");
    }
    // Original aligned fixed-pivot counterexample, including the static bone.
    before=make();after=before;
    put(after,20,Rows{{{-1,0,0,200},{0,-1,0,0},{0,0,1,0}}});
    for(float alpha : {1.0F/3,0.5F,2.0F/3}) {
        auto values=sample(before,after,alpha);
        for(size_t base : {20U,23U})for(size_t r=0;r<3;++r)
            Require(near(100*values[base+r][0]+values[base+r][3],r==0?100.0F:0.0F),
                    "fixed skeletal pivot moved");
    }
    // Unchanged first bone reserves its rows. B's translation must never
    // make [A.row1,A.row2,B.row0] become a reference matrix.
    before=make();after=before;
    put(after,23,Rows{{{1,0,0,1},{0,-1,0,0},{0,0,-1,0}}});
    auto values=sample(before,after,0.5F);
    for(size_t r=0;r<3;++r) Require(values[20+r]==identity[r],"unchanged palette slot was split");
    Require(near(values[23][3],0.5F)&&near(values[24][1],0)&&near(values[25][2],0),
            "local residual unexpectedly used a second per-bone motion");
    // Ref must be selected, not a stale lower-numbered slot; only slot 9 used.
    before=make(); before.VertexBindings[0].Bytes.assign(10,9); after=before;
    put(after,47,turn);
    values=sample(before,after,0.5F);
    Require(near(std::abs(values[47][1]),1),"last active palette slot was not factored");
    for(size_t r=20;r<47;++r) Require(values[r]==before.VertexShader.Uniforms.Floats[r],"unused slot changed");
    // Model-view has its own output space and rigid motion.
    before=make();after=before;put(after,4,turn);
    values=sample(before,after,0.5F);
    Require(near(std::abs(values[4][1]),1),"model-view turn flattened");
    for(size_t r=20;r<50;++r)Require(values[r]==before.VertexShader.Uniforms.Floats[r],"model-view motion leaked into palette");
    // Nonuniform column scale stays in the residual; tiny and pure rotations
    // do not suffer screw-axis cancellation. Reflections fall back as a group.
    before=make();after=before;
    for(size_t slot=0;slot<10;++slot)for(size_t r=0;r<3;++r) {
        before.VertexShader.Uniforms.Floats[20+3*slot+r][r]=float(r+1);
        Rows scaled=turn;
        for(size_t i=0;i<3;++i)for(size_t j=0;j<3;++j)scaled[i][j]*=float(j+1);
        put(after,20+3*slot,scaled);
    }
    values=sample(before,after,0.5F);
    Require(near(std::abs(values[20][1]),2)&&near(std::abs(values[21][0]),1)&&near(values[22][2],3),
            "column scale convention lost");
    for(float angle : {0.0F,1e-7F,1e-4F,3.1415925F}) {
        before=make();after=before;
        Rows motion{{{std::cos(angle),-std::sin(angle),0,10},{std::sin(angle),std::cos(angle),0,2},{0,0,1,3}}};
        for(size_t i=0;i<10;++i)put(after,20+3*i,motion);
        values=sample(before,after,0.5F);
        Require(near(values[20][0],std::cos(angle/2))&&near(values[21][0],std::sin(angle/2)),"near-zero/pi screw rotation unstable");
        Require(near(values[22][3],1.5F),"screw translation lost");
    }
    before=make();after=before;
    for(size_t i=0;i<10;++i)put(after,20+3*i,turn);
    auto unknownBefore=before,unknownAfter=after;
    unknownBefore.VertexShader.TemporalProgram.reset();unknownAfter.VertexShader.TemporalProgram.reset();
    scalar(unknownBefore,unknownAfter);
    auto changed=after;changed.VertexShader.Uniforms.BooleanMask^=8;scalar(before,changed);
    changed=after;changed.VertexShader.Uniforms.Integers[0][0]=1;scalar(before,changed);
    changed=after;changed.VertexShader.Uniforms.Floats[92][3]=3;scalar(before,changed);
    changed=after;changed.VertexShader.Uniforms.Floats[93][3]=4;scalar(before,changed);
    changed=after;changed.VertexBindings[0].Bytes[0]=1;scalar(before,changed);
    changed=after;changed.VertexBindings[0].Bytes.clear();scalar(before,changed);
    changed=after;changed.VertexShader.Uniforms.Floats[4][0]=0;scalar(before,changed);
    for(size_t i=0;i<10;++i) before.VertexShader.Uniforms.Floats[20+3*i][0]=-1;
    scalar(before,after); // all reference candidates reflected
    // Indexed 16-bit fetch uses BaseVertex; smooth skinning consumes exactly
    // the enabled components, including slot 9 and ignoring unused garbage.
    before=make(); before.Indexed=true;before.IndicesAre16Bit=true;before.BaseVertex=-5;before.VertexCount=1;
    before.IndexBytes={5,0};before.VertexBindings[0].ByteStride=4;
    before.VertexBindings[0].Bytes={9,0,255,255};before.VertexAttributes[0].ComponentCount=4;
    before.VertexShader.Uniforms.BooleanMask=12;before.VertexShader.Uniforms.Floats[92][3]=2;
    after=before;put(after,20,turn);put(after,47,turn);
    values=sample(before,after,0.5F);
    Require(near(std::abs(values[47][1]),1)&&near(std::abs(values[20][1]),1),"indexed/smooth palette selection failed");
    for(float count : {3.0F,4.0F}) {
        before.VertexShader.Uniforms.Floats[92][3]=count;
        before.VertexBindings[0].Bytes={9,0,1,2};after=before;
        for(size_t slot : {0U,1U,2U,9U})put(after,20+3*slot,turn);
        values=sample(before,after,0.5F);
        Require(near(std::abs(values[23][1]),1),"third influence was omitted");
        Require(near(std::abs(values[26][1]),count==4?1.0F:0.0F),"fourth influence selection wrong");
    }
    // Unskinned draws consume only slot zero even with no bone-index input.
    before=make();before.VertexShader.Uniforms.BooleanMask=0;before.VertexAttributes.clear();before.VertexBindings.clear();
    after=before;put(after,20,turn);values=sample(before,after,0.5F);
    Require(near(std::abs(values[20][1]),1),"unskinned palette transform flattened");
    // A bad address or changed mode must also reject the independent view group.
    before=make();after=before;put(after,4,turn);
    before.VertexBindings[0].Bytes[0]=10;after.VertexBindings[0].Bytes[0]=10;scalar(before,after);
    before=make();after=before;put(after,4,turn);
    before.VertexShader.Uniforms.Floats[89][3]=2;after.VertexShader.Uniforms.Floats[89][3]=2;scalar(before,after);
    // Non-identity reference endpoints catch the multiplication order:
    // D = M1 * inverse(M0), with an X-oriented source and Y screw motion.
    before=make();after=before;
    const Rows sourcePose{{{1,0,0,7},{0,0,-1,-3},{0,1,0,11}}};
    const Rows targetPose{{{-1,0,0,193},{0,0,-1,3},{0,-1,0,-111}}};
    const Rows expectedMid{{{0,1,0,161},{0,0,-1,0},{-1,0,0,43}}};
    for(size_t slot=0;slot<10;++slot){put(before,20+3*slot,sourcePose);put(after,20+3*slot,targetPose);}
    values=sample(before,after,0.5F);
    for(size_t r=0;r<3;++r)for(size_t c=0;c<4;++c)
        Require(near(values[20+r][c],expectedMid[r][c]),"relative screw used the wrong coordinate space/order");
    // Any non-finite used residual rejects BOTH groups without partial writes.
    before=make();after=before;put(after,4,turn);
    after.VertexShader.Uniforms.Floats[49][3]=std::numeric_limits<float>::quiet_NaN();
    values=sample(before,after,0.5F);
    Require(near(values[4][0],0)&&near(values[4][3],100)&&std::isnan(values[49][3]),
            "invalid residual left partially factored output");
    // A limb swinging 40 degrees in slot 0 against a body whose nine other
    // bones only translate: the common motion must be the body's. The limb as
    // reference would carry the body along the limb's arc (the Iron Knuckle
    // in the title demo trembled by up to 17 pixels on synthetic samples).
    before=make();after=before;
    {
        const float c=std::cos(0.698132F), s=std::sin(0.698132F);
        put(after,20,Rows{{{c,-s,0,30},{s,c,0,-12},{0,0,1,5}}});
        for(size_t slot=1;slot<10;++slot) put(after,20+3*slot,Rows{{{1,0,0,4},{0,1,0,10},{0,0,1,-6}}});
        values=sample(before,after,0.5F);
        for(size_t slot=1;slot<10;++slot) {
            const size_t first=20+3*slot;
            Require(near(values[first][3],2)&&near(values[first+1][3],5)&&near(values[first+2][3],-3),
                    "body bones left the straight path: a swinging limb was the reference");
            Require(near(values[first][0],1)&&near(values[first+1][1],1)&&near(values[first][1],0),
                    "body bones rotated with the swinging limb");
        }
    }
    // Signed-zero and cancellation-heavy endpoint copy (not x+(y-x)*1).
    before=make();after=before;
    before.VertexShader.Uniforms.Floats[20][3]=1e20F;after.VertexShader.Uniforms.Floats[20][3]=1;
    before.VertexShader.Uniforms.Floats[21][3]=-0.0F;
    values=sample(before,after,0);
    Require(std::signbit(values[21][3]),"endpoint lost signed zero");
    values=sample(before,after,1);
    Require(values[20][3]==1,"endpoint suffered cancellation");
}

// An actor drawn in pieces: the body draw references a still bone and the
// arm bone, the hand draw only the arm bone (same matrix, same joint). Each
// draw choosing its own reference would move the shared joint along two
// paths on the synthetic frame (0.5,0.5) vs (0.707,0.707): the hand detached.
// Draws of one actor share the body's motion; without actor evidence each
// draw keeps its own.
void TestSharedActorPaletteMotion() {
    using namespace Oot3dNativeGame;
    using Rows = std::array<std::array<float,4>,3>;
    const Rows identity{{{1,0,0,0},{0,1,0,0},{0,0,1,0}}};
    const Rows quarter{{{0,-1,0,0},{1,0,0,0},{0,0,1,0}}};
    const auto put = [](auto& plan, size_t first, const Rows& rows) {
        for (size_t i=0; i<3; ++i) plan.VertexShader.Uniforms.Floats[first+i]=rows[i];
    };
    const auto make = [&](std::vector<uint8_t> bones, uint64_t owner, uint64_t slot) {
        Oot3dPicaVulkanDrawPlan plan;
        auto program = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        program->AffineLayout = kOot3dCmbAffineLayout;
        plan.VertexShader.TemporalProgram = program;
        plan.VertexShader.StateKey = 17;
        plan.VertexShader.Uniforms.BooleanMask = 4;
        plan.VertexShader.Uniforms.Floats[93] = {0,1,2,3};
        plan.VertexShader.Uniforms.Floats[95] = {3,4,5,32};
        plan.VertexCount = static_cast<uint32_t>(bones.size());
        plan.VertexAttributes.push_back({6,0,Oot3dPicaVertexFormat::UnsignedByte,1,0});
        Oot3dPicaVulkanVertexBinding binding;
        binding.ByteStride = 1;
        binding.Bytes = bones;
        plan.VertexBindings.push_back(binding);
        put(plan,4,identity);
        for (size_t i=0;i<10;++i) put(plan,20+3*i,identity);
        plan.Composition.TemporalIdentity = {owner, 0x100, 0x200, slot, owner ? 1U : 0U};
        if (!owner) plan.Composition.TemporalIdentity = {};
        return plan;
    };
    for (const uint64_t owner : {uint64_t{0x77}, uint64_t{0}}) {
        auto body = make({0,1}, owner, 1), hand = make({0}, owner, 2);
        auto bodyAfter = body, handAfter = hand;
        put(bodyAfter,23,quarter);   // arm bone: body slot 1
        put(handAfter,20,quarter);   // same bone: hand slot 0
        Oot3dPicaVisualFrame aFrame,bFrame;
        aFrame.Sequence=1; bFrame.Sequence=2;
        aFrame.Draws={body,hand}; bFrame.Draws={bodyAfter,handAfter};
        Oot3dPicaPreparedVisualTransition prepared;
        Require(prepared.Prepare(aFrame,bFrame),"actor transition prepare failed");
        for (float alpha : {0.25F,0.5F,0.75F}) {
            Oot3dPicaVisualFrameSample frame;
            Oot3dPicaVisualFrameViewSample view;
            Require(SampleOot3dPicaVisualFrame(aFrame,bFrame,alpha,frame) && frame.Draws.size()==2,
                    "actor full-frame sample failed");
            Require(prepared.Sample(aFrame,bFrame,alpha,view) && view.Draws.size()==2,
                    "actor prepared sample failed");
            const auto viewFloats = [&](size_t i) -> const auto& {
                return view.Draws[i].VertexUniforms ? view.Draws[i].VertexUniforms->Floats
                    : view.Draws[i].BasePlan->VertexShader.Uniforms.Floats;
            };
            const auto& bodyFloats = frame.Draws[0].VertexShader.Uniforms.Floats;
            const auto& handFloats = frame.Draws[1].VertexShader.Uniforms.Floats;
            Require(viewFloats(0)==bodyFloats && viewFloats(1)==handFloats,
                    "actor full-frame and prepared-view outputs differ");
            // Joint (1,0,0) in the arm bone's space.
            float gap = 0;
            for (size_t r=0;r<3;++r)
                gap = std::max(gap, std::abs((bodyFloats[23+r][0]+bodyFloats[23+r][3]) -
                                             (handFloats[20+r][0]+handFloats[20+r][3])));
            if (owner) Require(gap < 1e-5F, "hand separated from the arm across draws of one actor");
            else Require(gap > 0.1F, "draws without actor evidence were grouped");
        }
    }
    // The actor swaps its hand piece between the two frames (open -> closed):
    // the new piece has no previous match. Its bone is the arm bone the body
    // also uses, so it must follow exactly that bone's path instead of being
    // shown at the new pose while the body is still in between.
    // (Without actor evidence the legacy matcher pairs the pieces by order.)
    for (const uint64_t owner : {uint64_t{0x77}}) {
        auto body = make({0,1}, owner, 1);
        auto openHand = make({0}, owner, 2), closedHand = make({0}, owner, 3);
        auto bodyAfter = body;
        put(bodyAfter,23,quarter);
        put(closedHand,20,quarter);
        Oot3dPicaVisualFrame aFrame,bFrame;
        aFrame.Sequence=1; bFrame.Sequence=2;
        aFrame.Draws={body,openHand}; bFrame.Draws={bodyAfter,closedHand};
        Oot3dPicaPreparedVisualTransition prepared;
        Require(prepared.Prepare(aFrame,bFrame),"swapped-piece transition prepare failed");
        Oot3dPicaVisualFrameSample frame;
        Oot3dPicaVisualFrameViewSample view;
        Require(SampleOot3dPicaVisualFrame(aFrame,bFrame,0.5F,frame) && frame.Draws.size()==2,
                "swapped-piece full-frame sample failed");
        Require(prepared.Sample(aFrame,bFrame,0.5F,view) && view.Draws.size()==2,
                "swapped-piece prepared sample failed");
        const auto& bodyFloats = frame.Draws[0].VertexShader.Uniforms.Floats;
        const auto& handFloats = frame.Draws[1].VertexShader.Uniforms.Floats;
        const auto& viewHand = view.Draws[1].VertexUniforms ? view.Draws[1].VertexUniforms->Floats
            : view.Draws[1].BasePlan->VertexShader.Uniforms.Floats;
        Require(viewHand==handFloats, "swapped piece differs between full-frame and prepared view");
        for (size_t r=0;r<3;++r) {
            Require(handFloats[20+r]==bodyFloats[23+r], "swapped hand piece left the arm bone's path");
        }
    }
}

void TestPeriodicTextureInterpolationContract() {
    using namespace Fast::Renderer3ds;
    std::array<std::array<float,4>,96> a{},b{},output{};
    a[10][3]=0.875F; b[10][3]=0.125F;
    PicaTextureTranslations before{},after{};
    before[0].UniformRow=after[0].UniformRow=10;
    before[0].CoordinateSet=after[0].CoordinateSet=0;
    before[0].InputRegister=after[0].InputRegister=3;
    before[0].Wrap=after[0].Wrap=PicaTextureWrap::Repeat;
    const auto run = [&](float alpha) {
        output[10][3]=0.5F;
        InterpolatePicaTextureTranslations(a,b,before,after,alpha,output);
        return output[10][3];
    };
    Require(run(0.5F)==1.0F,"shared renderer did not unwrap a proven UV translation");
    for (float alpha : {0.0F,1.0F,-1.0F,2.0F,std::numeric_limits<float>::quiet_NaN()})
        Require(run(alpha)==0.5F,"periodic refinement changed a caller-owned endpoint/invalid sample");
    // A pre-linear/input-space offset is NOT a proven unit UV translation.
    before[0].CoordinateScale=after[0].CoordinateScale=2;
    Require(run(0.5F)==0.5F,"non-unit output coefficient was guessed periodic");
    before[0].CoordinateScale=after[0].CoordinateScale=1;
    after[0].InputRegister=0;
    Require(run(0.5F)==0.5F,"changed source route was blended periodically");
    after[0].InputRegister=before[0].InputRegister;
    a[10][3]=std::numeric_limits<float>::infinity();
    Require(run(0.5F)==0.5F,"non-finite endpoint was wrapped");
    a[10][3]=0.75F; b[10][3]=0.25F;
    Require(run(0.5F)==0.5F,"exact half-period tie changed scalar output");
}

// A captured CMB board draw: clouds use c10.w as a post-matrix UV
// translation. 0.99888891 -> 0 is a repeat seam, not a half-texture pan.
void TestPeriodicTextureTranslationSampling() {
    using namespace Oot3dNativeGame;
    const auto make = [] {
        Oot3dPicaVulkanDrawPlan plan;
        auto program = std::make_shared<Oot3dPicaTemporalVertexProgram>();
        program->AffineLayout = kOot3dCmbAffineLayout;
        program->CmbTextureCoordinates = {true, true, true};
        plan.VertexShader.TemporalProgram = program;
        plan.VertexShader.StateKey = 0x0F8A8E0C7D2C1963ULL;
        plan.Composition.TemporalIdentity = {0, 0x100, 0x200, 1, 1};
        auto& u = plan.VertexShader.Uniforms;
        u.BooleanMask = 98;
        u.Floats[4] = {1,0,0,0}; u.Floats[5] = {0,1,0,0}; u.Floats[6] = {0,0,1,0};
        for (size_t row : {10U,14U,17U}) {
            u.Floats[row] = {1,-0.0F,0,0};
            u.Floats[row+1] = {0,1,0,0};
        }
        u.Floats[89] = {0,0,0,2}; // board, NOT a factored model palette
        u.Floats[91] = {0.000122073994F,1,1,0};
        u.Floats[92] = {1,0,0,1};
        u.Floats[93] = {0,1,2,3};
        u.Floats[95] = {3,4,5,32};
        plan.State.Textures[0].Enabled = true;
        plan.State.Textures[0].WrapS = plan.State.Textures[0].WrapT = 2;
        plan.Textures.push_back({.Slot=0, .State=plan.State.Textures[0]});
        plan.VertexAttributes.push_back({2,2,Oot3dPicaVertexFormat::UnsignedByte,4,0});
        Oot3dPicaVulkanVertexBinding colors;
        colors.Binding = 2; colors.ByteStride = 4; colors.Bytes = {100,110,120,255};
        plan.VertexBindings.push_back(colors);
        plan.VertexCount = 1;
        return plan;
    };
    // liftedEnd is a known equivalent endpoint, not an implementation of the
    // wrap algorithm. For a seam 0.9989 -> 0 the visible path ends at 1.
    const auto check = [&](Oot3dPicaVulkanDrawPlan a, Oot3dPicaVulkanDrawPlan b,
                           size_t row, float liftedEnd, const char* message) {
        // The bridge consumes these copies of the decoded sampler state.
        for (auto* plan : {&a,&b}) {
            plan->Textures.clear();
            for (uint8_t unit=0; unit<3; ++unit)
                if (a.State.Textures[unit].Enabled || b.State.Textures[unit].Enabled)
                    plan->Textures.push_back({.Slot=unit, .State=plan->State.Textures[unit]});
        }
        const auto aFloats = a.VertexShader.Uniforms.Floats, bFloats = b.VertexShader.Uniforms.Floats;
        Oot3dPicaVisualFrame before, after;
        before.Sequence = 915; after.Sequence = 916;
        before.Draws = {a}; after.Draws = {b};
        Oot3dPicaPreparedVisualTransition prepared;
        Require(prepared.Prepare(before,after), "texture transition prepare failed");
        Oot3dPicaVisualFrameViewSample view; // deliberately reused after alpha 0
        for (float alpha : {0.0F,0.5F,1.0F/3.0F,2.0F/3.0F,1.0F,0.5F,0.0F}) {
            Oot3dPicaVulkanDrawPlan draw;
            Oot3dPicaVisualFrameSample frame;
            Require(InterpolateOot3dPicaVisualDraw(a,b,alpha,draw) &&
                    SampleOot3dPicaVisualFrame(before,after,alpha,frame) &&
                    prepared.Sample(before,after,alpha,view), "texture sample failed");
            Require(frame.MatchedDraws == 1 && view.MatchedDraws == 1 && view.Draws.size() == 1,
                    "texture test lost native matching");
            const auto& values = draw.VertexShader.Uniforms.Floats;
            const auto& viewed = view.Draws[0].VertexUniforms ? view.Draws[0].VertexUniforms->Floats
                : view.Draws[0].BasePlan->VertexShader.Uniforms.Floats;
            Require(values == viewed && values == frame.Draws[0].VertexShader.Uniforms.Floats,
                    "draw/full-frame/prepared texture interpolation differs");
            if (alpha == 0 || alpha == 1) {
                const auto& expected = alpha == 0 ? aFloats : bFloats;
                Require(std::memcmp(values.data(),expected.data(),sizeof(values)) == 0,
                        "texture interpolation changed an endpoint (including signed zero)");
            } else {
                const float expected = aFloats[row][3] + (liftedEnd-aFloats[row][3])*alpha;
                // Compare final S/T at a vertex, including input quantization
                // and a non-unit linear part, rather than a uniform-array hash.
                const auto uv = [&](float translation) {
                    return double(values[row][0])*1024.0*aFloats[91][0] +
                           double(values[row][1])*2048.0*aFloats[91][0] + values[row][2] + translation;
                };
                Require(std::abs(uv(values[row][3])-uv(expected)) < 1e-6, message);
                if (liftedEnd == bFloats[row][3])
                    Require(values[row][3] == expected, "ordinary scalar rounding changed");
                for (size_t r=0; r<96; ++r) for (size_t c=0; c<4; ++c) {
                    if (r == row && c == 3) continue;
                    const float scalar = r < 89 || r == 90 || r == 91
                        ? aFloats[r][c]+(bFloats[r][c]-aFloats[r][c])*alpha : bFloats[r][c];
                    Require(values[r][c] == scalar, "periodic policy touched an unrelated component");
                }
            }
            Require(draw.VertexBindings[0].ResolvedBytes()[0] == b.VertexBindings[0].ResolvedBytes()[0],
                    "texture policy changed discrete vertex colors");
        }
        Require(before.Draws[0].VertexShader.Uniforms.Floats == aFloats &&
                after.Draws[0].VertexShader.Uniforms.Floats == bFloats, "texture endpoints were mutated");
    };
    auto a=make(), b=a;
    a.VertexShader.Uniforms.Floats[10][3] = 0.99888891F;
    b.VertexBindings[0].Bytes[0] = 101;
    a.VertexShader.Uniforms.Floats[20] = {228.074738F,-0.00248193741F,-224.459167F,-0.000244140625F};
    b.VertexShader.Uniforms.Floats[20] = {228.833298F,0.0100636492F,-223.685776F,0.000244140625F};
    a.VertexShader.Uniforms.Floats[76] = {0.712733507F,0.164386511F,0.681900203F,2412.73706F};
    b.VertexShader.Uniforms.Floats[76] = {0.715104043F,0.164238676F,0.679449737F,2412.55225F};
    check(a,b,10,1.0F,"captured cloud scroll crossed half the texture");
    check(b,a,10,-0.00111109F,"reverse repeat seam took the long path");
    auto from=a, to=b;
    from.VertexShader.Uniforms.Floats[10][0]=2; to.VertexShader.Uniforms.Floats[10][0]=4;
    from.VertexShader.Uniforms.Floats[10][1]=to.VertexShader.Uniforms.Floats[10][1]=-3;
    check(from,to,10,1,"post-linear UV period was incorrectly divided by matrix/input scale");
    from=a; to=b;
    from.VertexShader.Uniforms.Floats[10][3]=0.125F; to.VertexShader.Uniforms.Floats[10][3]=0.375F;
    check(from,to,10,0.375F,"non-wrapping scroll changed");
    for (uint8_t mode : {0U,1U,4U,5U,6U,7U}) {
        from=a; to=b; from.State.Textures[0].WrapS=to.State.Textures[0].WrapS=mode;
        check(from,to,10,0,"clamp/border/unproven wrap mode was treated as periodic");
    }
    from=a; to=b; from.State.Textures[0].WrapS=to.State.Textures[0].WrapS=3;
    from.VertexShader.Uniforms.Floats[10][3]=1.875F; to.VertexShader.Uniforms.Floats[10][3]=0.125F;
    check(from,to,10,2.125F,"mirrored repeat did not use period two");
    from.VertexShader.Uniforms.Floats[10][3]=0.875F;
    check(from,to,10,0.125F,"mirrored repeat was incorrectly reduced modulo one");
    from=a; to=b; to.State.Textures[0].WrapS=3;
    check(from,to,10,0,"changed endpoint wrap modes acquired a periodic path");
    from=a; to=b; from.VertexShader.TemporalProgram.reset(); to.VertexShader.TemporalProgram.reset();
    check(from,to,10,0,"unknown shader acquired CMB translation semantics");
    from=a; to=b;
    auto unproven=std::make_shared<Oot3dPicaTemporalVertexProgram>(*a.VertexShader.TemporalProgram);
    unproven->CmbTextureCoordinates={};
    from.VertexShader.TemporalProgram=to.VertexShader.TemporalProgram=unproven;
    check(from,to,10,0,"unproven output mapping acquired translation semantics");
    for (unsigned gate=0; gate<6; ++gate) {
        from=a; to=b;
        for (auto* plan : {&from,&to}) {
            auto& u=plan->VertexShader.Uniforms;
            if (gate==0) u.BooleanMask &= ~2U;
            if (gate==1) u.BooleanMask &= ~64U;
            if (gate==2) u.Floats[89][3]=3;
            if (gate==3) { u.Floats[89][3]=0; u.Floats[92][0]=4; }
            if (gate==4) plan->State.Textures[0].Type=3;
            if (gate==5) plan->FragmentShader.Uniforms.FragmentControl[2] |= 1U << 10U;
        }
        check(from,to,10,0,"unproven/disabled/procedural/projective route was wrapped");
    }
    // Board ignores c92; the model's ordinary UV branch is also proven.
    for (float mode : {0.0F,1.0F,2.0F}) {
        from=make(); to=from;
        from.VertexShader.Uniforms.Floats[10][3]=0.99888891F;
        from.VertexShader.Uniforms.Floats[89][3]=to.VertexShader.Uniforms.Floats[89][3]=mode;
        if (mode==2) from.VertexShader.Uniforms.Floats[92][0]=to.VertexShader.Uniforms.Floats[92][0]=4;
        check(from,to,10,1,"active CMB UV branch was not wrapped");
    }
    // Each unit and S/T axis: rows 12/13/16/19 are never guessed translations.
    for (uint8_t unit=0; unit<3; ++unit) for (uint8_t axis=0; axis<2; ++axis) {
        from=make(); to=from;
        const size_t row=std::array<size_t,3>{10,14,17}[unit]+axis;
        for (auto* plan : {&from,&to}) {
            plan->State.Textures[0].Enabled=false;
            auto& texture=plan->State.Textures[unit];
            texture.Enabled=true; texture.WrapS=texture.WrapT=2;
        }
        from.VertexShader.Uniforms.Floats[row][3]=0.875F; to.VertexShader.Uniforms.Floats[row][3]=0.125F;
        check(from,to,row,1.125F,"texture unit/axis mapping was lost");
    }
    for (size_t row : {12U,13U,16U,19U,8U,76U}) {
        from=make(); to=from;
        from.VertexShader.Uniforms.Floats[row][3]=0.875F; to.VertexShader.Uniforms.Floats[row][3]=0.125F;
        check(from,to,row,0.125F,"an unproven uniform row was wrapped");
    }
    // row.z and row.w both multiply homogeneous 1 in the proven branch.
    from=make(); to=from;
    from.VertexShader.Uniforms.Floats[10][2]=0.125F; from.VertexShader.Uniforms.Floats[10][3]=0.875F;
    to.VertexShader.Uniforms.Floats[10][2]=0.25F; to.VertexShader.Uniforms.Floats[10][3]=-0.25F;
    check(from,to,10,0.75F,"combined additive translation lost its period");
    // Unit 2 can share coordinate 1: all consumers must agree on periodicity.
    for (uint8_t secondWrap : {0U,2U,3U}) {
        from=make(); to=from;
        for (auto* plan : {&from,&to}) {
            plan->State.Textures[0].Enabled=false;
            plan->State.Texture2UsesCoordinate1=true;
            for (uint8_t unit : {1U,2U}) {
                plan->State.Textures[unit].Enabled=true;
                plan->State.Textures[unit].WrapS=unit==1 ? 2U : secondWrap;
                plan->State.Textures[unit].WrapT=2;
            }
        }
        from.VertexShader.Uniforms.Floats[14][3]=1.875F; to.VertexShader.Uniforms.Floats[14][3]=0.125F;
        check(from,to,14,secondWrap==0 ? 0.125F : 2.125F,"aliased coordinate ignored a sampler consumer");
    }
}

} // namespace

int main() {
    TestPeriodicTextureInterpolationContract();
    TestPeriodicTextureTranslationSampling();
    TestFactoredCmbUniformInterpolation();
    TestSharedActorPaletteMotion();
    Oot3dNativeGame::Oot3dPicaShaderState mutableShader;
    mutableShader.Program[0] = 0x88000000U;
    const uint64_t sharedProgramIdentity =
        mutableShader.Program.MutationIdentity();
    const auto shaderSnapshot = mutableShader;
    Require(shaderSnapshot.Program.MutationIdentity() ==
                sharedProgramIdentity &&
                shaderSnapshot.Program.Values()[0] == 0x88000000U,
            "PICA shader snapshot did not share immutable backing storage");
    mutableShader.Program[0] = 0x88000001U;
    Require(mutableShader.Program.MutationIdentity() !=
                sharedProgramIdentity &&
                mutableShader.Program.Values()[0] == 0x88000001U &&
                shaderSnapshot.Program.MutationIdentity() ==
                    sharedProgramIdentity &&
                shaderSnapshot.Program.Values()[0] == 0x88000000U,
            "PICA shader copy-on-write mutation changed an existing snapshot");

    Oot3dNativeGame::Oot3dPicaDrawSubmission submission;
    submission.Id = 9U;
    submission.MinimumVertexIndex = 2U;
    submission.MaximumVertexIndex = 5U;
    submission.Packet.Indexed = true;
    submission.Packet.CommandListAddress = 0x14001200U;
    submission.Packet.CommandListOffsetWords = 0x34U;
    submission.Packet.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene;
    submission.Packet.Composition = {
        Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld,
        Oot3dNativeGame::Oot3dPicaCompositionProvenance::NativeCmbDrawPass,
        0x0030F4D0U,
        0U,
    };
    auto roomPending = std::make_shared<Fast::Renderer3ds::RoomShadowShellScene>();
    roomPending->ScopeIdentity = 123;
    roomPending->Rooms.push_back({456, {}});
    submission.Packet.LightBinding.RoomShadows = roomPending;
    submission.Packet.VertexShader.Program[0] = 0x88000000U;
    submission.Packet.VertexShader.ProgramWordCount = 1U;
    submission.Packet.VertexShader.SwizzleWordCount = 1U;
    submission.Packet.Registers[0x04F] = 1U;
    submission.Packet.Registers[0x050] = 0x03020100U;
    submission.Packet.Registers[0x2BD] = 1U;
    submission.Packet.DefaultAttributes[1] = {1.0F, 2.0F, 3.0F, 4.0F};

    auto& state = submission.State;
    state.VertexInput.Indexed = true;
    state.VertexInput.IndicesAre16Bit = false;
    state.VertexInput.VertexCount = 3U;
    state.VertexInput.AttributeCount = 2U;
    state.VertexInput.Attributes[0] = {
        Oot3dNativeGame::Oot3dPicaVertexFormat::Float, 3U, false};
    state.VertexInput.Attributes[1] = {
        Oot3dNativeGame::Oot3dPicaVertexFormat::Float, 4U, true};
    state.VertexInput.Loaders[0].ByteStride = 12U;
    state.VertexInput.Loaders[0].ComponentCount = 1U;
    state.VertexInput.Loaders[0].Components[0] = 0U;
    state.ShaderInterface.OutputMask = 1U;
    state.ShaderInterface.InputRegisterByAttribute[0] = 0U;
    state.ShaderInterface.InputRegisterByAttribute[1] = 2U;

    auto semanticState = state;
    semanticState.VertexInput.AttributeCount = 5U;
    semanticState.VertexInput.Attributes[1].ComponentCount = 3U;
    semanticState.VertexInput.Attributes[2].ComponentCount = 4U;
    semanticState.VertexInput.Attributes[3].ComponentCount = 2U;
    semanticState.VertexInput.Attributes[4].ComponentCount = 2U;
    semanticState.ShaderInterface.InputRegisterByAttribute[0] = 5U;
    semanticState.ShaderInterface.InputRegisterByAttribute[3] = 7U;
    semanticState.ShaderInterface.InputRegisterByAttribute[4] = 9U;
    const auto semantics =
        Oot3dNativeGame::ResolveOot3dCmbVertexSemanticLocations(
            semanticState);
    Require(semantics.Position == 5U && semantics.TexCoord0 == 7U,
            "CMB vertex semantics did not follow the PICA input map");

    Oot3dNativeGame::Oot3dPicaResourceSnapshot indices;
    indices.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::IndexBuffer;
    indices.Bytes = {2U, 5U, 3U};
    indices.ContentVersion = 11U;
    indices.ContentVersionAvailable = true;
    submission.Resources.push_back(indices);
    Oot3dNativeGame::Oot3dPicaResourceSnapshot vertices;
    vertices.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::VertexLoader;
    vertices.Slot = 0U;
    vertices.FirstElement = 2U;
    vertices.Bytes.resize(48U);
    vertices.ContentVersion = 12U;
    vertices.ContentVersionAvailable = true;
    submission.Resources.push_back(vertices);

    std::string error;
    Oot3dNativeGame::Oot3dPicaVulkanShaderSourceCache shaderCache;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan plan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, plan, &error, &shaderCache),
            error);
    Require(plan.LightBinding.RoomShadows == roomPending, "draw plan lost the room snapshot");
    Require(plan.SubmissionId == 9U && plan.CommandListAddress == 0x14001200U &&
                plan.CommandListOffsetWords == 0x34U && plan.Indexed &&
                plan.CompositionDomain ==
                    Oot3dNativeGame::Oot3dPicaCompositionDomain::Scene &&
                plan.Composition.Layer ==
                    Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld &&
                plan.Composition.Provenance ==
                    Oot3dNativeGame::Oot3dPicaCompositionProvenance::
                        NativeCmbDrawPass &&
                plan.Composition.SourcePc == 0x0030F4D0U &&
                plan.Composition.NativeValue == 0U &&
                plan.BaseVertex == -2 && plan.VertexCount == 3U &&
                plan.IndexBytes == std::vector<uint8_t>({2U, 5U, 3U}) &&
                plan.VertexBindings.size() == 2U &&
                plan.VertexBindings[0].ByteStride == 12U &&
                plan.VertexBindings[0].Bytes.size() == 48U &&
                plan.VertexBindings[1].InputRate ==
                    Oot3dNativeGame::Oot3dPicaVertexInputRate::PerInstance &&
                plan.VertexBindings[1].Bytes.size() == 32U &&
                plan.VertexAttributes.size() == 16U &&
                plan.VertexAttributes[0].Binding == 0U &&
                plan.VertexAttributes[0].ComponentCount == 3U &&
                plan.VertexAttributes[1].Binding == 1U &&
                plan.VertexAttributes[1].ByteOffset == 0U &&
                plan.VertexAttributes[2].Binding == 1U &&
                plan.VertexAttributes[2].ByteOffset == 16U &&
                plan.GeometryIdentityAvailable && plan.GeometryIdentity != 0U &&
                plan.GeometryContentVersion != 0U &&
                plan.VertexShader.StateKey != 0U &&
                plan.FragmentShader.StateKey != 0U &&
                plan.VertexShader.SourceIdentity ==
                    Oot3d::Renderer::IdentifyPicaShaderSource(
                        plan.ResolvedVertexShaderSource()) &&
                plan.FragmentShader.SourceIdentity ==
                    Oot3d::Renderer::IdentifyPicaShaderSource(
                        plan.ResolvedFragmentShaderSource()) &&
                plan.VertexShader.TemporalProgram != nullptr &&
                plan.VertexShader.TemporalProgram->Hooks.ValidFor(
                    plan.ResolvedVertexShaderSource()) &&
                plan.FragmentShader.Hooks.ValidFor(
                    plan.ResolvedFragmentShaderSource()),
            "native PICA submission did not produce a complete Vulkan plan");

    {
        auto metadata = submission;
        metadata.Resources.clear();
        Oot3dNativeGame::Oot3dPicaVulkanDrawPlan prepared, rejectedDraw;
        Require(Oot3dNativeGame::BuildOot3dPicaVulkanPipelinePlan(metadata, prepared, &error), error);
        Require(!Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(metadata, rejectedDraw, &error),
                "pipeline preparation must not make resource-less draws executable");
        Require(prepared.ResolvedVertexShaderSource() == plan.ResolvedVertexShaderSource() &&
                prepared.ResolvedFragmentShaderSource() == plan.ResolvedFragmentShaderSource() &&
                prepared.VertexBindings.size() == plan.VertexBindings.size() &&
                prepared.VertexAttributes.size() == plan.VertexAttributes.size() &&
                prepared.IndexBytes.empty() && prepared.Textures.empty() &&
                prepared.VertexBindings[0].ResolvedBytes().empty() && !prepared.GeometryIdentityAvailable,
                "preparation must share live shaders/layout without copying geometry or textures");
        for (size_t i = 0; i < plan.VertexBindings.size(); ++i) {
            const auto& a = prepared.VertexBindings[i];
            const auto& b = plan.VertexBindings[i];
            Require(a.Binding == b.Binding && a.ByteStride == b.ByteStride && a.InputRate == b.InputRate,
                    "preparation/live vertex binding mismatch");
        }
        for (size_t i = 0; i < plan.VertexAttributes.size(); ++i) {
            const auto& a = prepared.VertexAttributes[i];
            const auto& b = plan.VertexAttributes[i];
            Require(a.Location == b.Location && a.Binding == b.Binding && a.Format == b.Format &&
                    a.ComponentCount == b.ComponentCount && a.ByteOffset == b.ByteOffset,
                    "preparation/live vertex attribute mismatch");
        }
        error.clear();
    }

    const std::string vertexSource(plan.ResolvedVertexShaderSource());
    const std::string fragmentSource(plan.ResolvedFragmentShaderSource());
    submission.Packet.VertexShader.BooleanUniforms[3] = true;
    submission.Packet.Registers[0x0C3U] = 0x44332211U;
    auto roomPublished = std::make_shared<Fast::Renderer3ds::RoomShadowShellScene>(*roomPending);
    submission.Packet.LightBinding.RoomShadows = roomPublished;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan cachedPlan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, cachedPlan, &error, &shaderCache),
            error);
    Require(cachedPlan.LightBinding.RoomShadows == roomPublished,
            "shader cache reused an old room snapshot");
    Require(shaderCache.VertexHits == 1U &&
                shaderCache.VertexMisses == 1U &&
                shaderCache.FragmentHits == 1U &&
                shaderCache.FragmentMisses == 1U &&
                cachedPlan.ResolvedVertexShaderSource() == vertexSource &&
                cachedPlan.ResolvedFragmentShaderSource() == fragmentSource &&
                cachedPlan.VertexShader.SourceIdentity ==
                    plan.VertexShader.SourceIdentity &&
                cachedPlan.FragmentShader.SourceIdentity ==
                    plan.FragmentShader.SourceIdentity &&
                cachedPlan.VertexShader.TemporalProgram != nullptr &&
                cachedPlan.VertexShader.TemporalProgram->Hooks.ValidFor(
                    cachedPlan.ResolvedVertexShaderSource()) &&
                cachedPlan.FragmentShader.Hooks.ValidFor(
                    cachedPlan.ResolvedFragmentShaderSource()) &&
                (cachedPlan.VertexShader.Uniforms.BooleanMask & (1U << 3U)) !=
                    0U &&
                cachedPlan.FragmentShader.Uniforms.TevConstants[0][0] > 0.0F,
            "native PICA shader cache must retain dynamic uniforms");

    const uint64_t cachedProgramIdentity =
        submission.Packet.VertexShader.Program.MutationIdentity();
    const uint32_t unchangedProgramWord =
        submission.Packet.VertexShader.Program.Values()[0];
    submission.Packet.VertexShader.Program[0] = unchangedProgramWord;
    Require(submission.Packet.VertexShader.Program.MutationIdentity() !=
                cachedProgramIdentity,
            "PICA shader mutation did not invalidate structural identity");
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan invalidatedStructuralPlan;
    Require(Oot3dNativeGame::BuildOot3dPicaVulkanDrawPlan(
                submission, invalidatedStructuralPlan, &error, &shaderCache),
            error);
    Require(shaderCache.VertexStructuralStates.size() == 2U &&
                shaderCache.VertexHits == 2U &&
                shaderCache.VertexMisses == 1U &&
                invalidatedStructuralPlan.VertexShader.StateKey ==
                    cachedPlan.VertexShader.StateKey &&
                invalidatedStructuralPlan.CanonicalIdentity.VertexProgramId ==
                    cachedPlan.CanonicalIdentity.VertexProgramId,
            "PICA structural cache ignored mutation or changed canonical identity");

    auto consumingSubmission = submission;
    consumingSubmission.State.Textures[0].Enabled = true;
    consumingSubmission.State.Textures[0].Width = 16U;
    consumingSubmission.State.Textures[0].Height = 16U;
    consumingSubmission.State.Textures[0].MaxMipLevel = 1U;
    Oot3dNativeGame::Oot3dPicaResourceSnapshot texture;
    texture.Kind = Oot3dNativeGame::Oot3dPicaResourceKind::Texture;
    texture.Slot = 0U;
    texture.Bytes.resize(1280U, 0x22U);
    std::fill_n(texture.Bytes.begin(), 1024U, 0x11U);
    const std::vector<uint8_t> expectedTextureBytes = texture.Bytes;
    const uint64_t expectedResourceHash = HashBytes(texture.Bytes);
    const uint64_t expectedBaseLevelHash =
        HashBytes(std::span<const uint8_t>(texture.Bytes).first(1024U));
    texture.ContentHash = expectedResourceHash;
    texture.ContentHashAvailable = true;
    consumingSubmission.Resources.push_back(texture);
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan consumingPlan;
    Require(Oot3dNativeGame::
                BuildOot3dPicaVulkanDrawPlanAndConsumeResources(
                    consumingSubmission, consumingPlan, &error,
                    &shaderCache),
            error);
    Require(consumingPlan.IndexBytes ==
                std::vector<uint8_t>({2U, 5U, 3U}) &&
                consumingPlan.VertexBindings[0].Bytes.size() == 48U &&
                consumingPlan.Textures.size() == 1U &&
                consumingPlan.Textures[0].NativeBytes ==
                    expectedTextureBytes &&
                consumingPlan.Textures[0].NativeContentHashAvailable &&
                consumingPlan.Textures[0].NativeContentHash ==
                    expectedResourceHash &&
                consumingPlan.Textures[0]
                    .NativeBaseLevelContentHashAvailable &&
                consumingPlan.Textures[0].NativeBaseLevelContentHash ==
                    expectedBaseLevelHash &&
                expectedResourceHash != expectedBaseLevelHash &&
                std::all_of(consumingSubmission.Resources.begin(),
                            consumingSubmission.Resources.end(),
                            [](const auto& resource) {
                                return resource.Bytes.empty();
                            }),
            "consuming Vulkan plan retained copied resource snapshots");

    Oot3dNativeGame::Oot3dPicaVulkanTextureBinding singleMipTexture;
    singleMipTexture.State.Enabled = true;
    singleMipTexture.State.Width = 8U;
    singleMipTexture.State.Height = 8U;
    singleMipTexture.NativeBytes.resize(256U, 0x5AU);
    singleMipTexture.NativeContentHash = 0x123456789ABCDEF0ULL;
    singleMipTexture.NativeContentHashAvailable = true;
    Require(Oot3dNativeGame::ResolveOot3dPicaTextureContentIdentity(
                singleMipTexture, &error) &&
                singleMipTexture.NativeBaseLevelContentHashAvailable &&
                singleMipTexture.NativeBaseLevelContentHash ==
                    singleMipTexture.NativeContentHash,
            "single-mip texture did not reuse its authoritative payload hash");

    plan.VertexShader.Uniforms.Floats[0][0] = 2.0F;
    plan.VertexShader.Uniforms.Floats[0x59U][0] = 12.0F;
    plan.FragmentShader.Uniforms.FogColor[0] = 0.2F;
    cachedPlan.VertexShader.Uniforms.Floats[0][0] = 6.0F;
    cachedPlan.VertexShader.Uniforms.Floats[0x59U][0] = 24.0F;
    cachedPlan.FragmentShader.Uniforms.FogColor[0] = 0.6F;
    WriteFloat(plan.VertexBindings[0].MutableBytes(), 0U, 2.0F);
    WriteFloat(cachedPlan.VertexBindings[0].MutableBytes(), 0U, 6.0F);
    WriteFloat(plan.VertexBindings[1].MutableBytes(), 16U, 10.0F);
    WriteFloat(cachedPlan.VertexBindings[1].MutableBytes(), 16U, 14.0F);
    auto& previousPoint = plan.LightBinding.Sources[0];
    previousPoint.Kind = Fast::Renderer3ds::PicaNativeLightSource::Type::Point;
    previousPoint.SourceIdentity = 1U;
    previousPoint.PositionWorld = {2.0F, 0.0F, 0.0F};
    previousPoint.Radius = 10.0F;
    cachedPlan.LightBinding = plan.LightBinding;
    cachedPlan.LightBinding.RoomShadows = roomPublished;
    cachedPlan.LightBinding.Sources[0].PositionWorld[0] = 6.0F;
    plan.LightBinding.Sources[1] = previousPoint;
    cachedPlan.LightBinding.Sources[1] = cachedPlan.LightBinding.Sources[0];
    cachedPlan.LightBinding.Sources[1].SourceIdentity = 2U;
    auto sceneBefore = std::make_shared<Fast::Renderer3ds::PicaScenePointLights>();
    sceneBefore->Count = 2;
    sceneBefore->Lights[0] = {11, {2,0,0}, {1,0,0}, 80, 0};
    sceneBefore->Lights[1] = {22, {8,0,0}, {0,1,0}, 40, 1};
    auto sceneAfter = std::make_shared<Fast::Renderer3ds::PicaScenePointLights>(*sceneBefore);
    std::swap(sceneAfter->Lights[0], sceneAfter->Lights[1]);
    sceneAfter->Lights[0].PositionWorld[0] = 12;
    sceneAfter->Lights[1].PositionWorld[0] = 6;
    sceneAfter->Lights[1].Radius = 0;
    plan.LightBinding.ScenePoints = sceneBefore;
    cachedPlan.LightBinding.ScenePoints = sceneAfter;
    auto directionsBefore = std::make_shared<Fast::Renderer3ds::PicaSceneDirectionalLights>();
    directionsBefore->Count = 2;
    directionsBefore->Lights[0] = {51, {0,1,0}, {1,0,0}};
    directionsBefore->Lights[1] = {52, {0,-1,0}, {0,0,1}};
    auto directionsAfter = std::make_shared<Fast::Renderer3ds::PicaSceneDirectionalLights>(*directionsBefore);
    std::swap(directionsAfter->Lights[0], directionsAfter->Lights[1]);
    directionsAfter->Lights[1].DirectionTowardSource = {1,0,0};
    directionsAfter->Lights[1].Color = {0,1,0};
    plan.LightBinding.SceneDirectionals = directionsBefore;
    cachedPlan.LightBinding.SceneDirectionals = directionsAfter;
    Oot3dNativeGame::Oot3dPicaVulkanDrawPlan interpolated;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan) &&
                Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, cachedPlan, 0.5F, interpolated),
            "compatible native PICA draws were not interpolated");
    Require(interpolated.VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                interpolated.LightBinding.Sources[0].PositionWorld[0] == 4.0F &&
                interpolated.LightBinding.Sources[1].PositionWorld[0] == 6.0F &&
                interpolated.VertexShader.Uniforms.Floats[0x59U][0] ==
                    24.0F &&
                std::abs(interpolated.FragmentShader.Uniforms.FogColor[0] -
                         0.4F) < 0.00001F &&
                ReadFloat(interpolated.VertexBindings[0].Bytes, 0U) ==
                    4.0F &&
                ReadFloat(interpolated.VertexBindings[1].Bytes, 16U) ==
                    12.0F &&
                interpolated.VertexShader.Uniforms.BooleanMask ==
                    cachedPlan.VertexShader.Uniforms.BooleanMask,
            "visual interpolation did not separate continuous and discrete state");
    const auto points = interpolated.LightBinding.ScenePoints;
    Require(interpolated.LightBinding.RoomShadows == roomPublished,
            "interpolation dropped or blended the current room snapshot");
    Require(points && points->Lights[0].SourceIdentity == 22 &&
                points->Lights[0].PositionWorld[0] == 10 &&
                points->Lights[1].PositionWorld[0] == 4 && points->Lights[1].Radius == 40 &&
                sceneBefore->Lights[0].PositionWorld[0] == 2 && sceneAfter->Lights[1].Radius == 0,
            "scene lights interpolated by slot rather than owner, or changed a native snapshot");
    const auto directions = interpolated.LightBinding.SceneDirectionals;
    Require(directions && directions->Lights[0].SourceIdentity == 52 &&
                directions->Lights[0].DirectionTowardSource == directionsBefore->Lights[1].DirectionTowardSource &&
                directions->Lights[1].DirectionTowardSource == std::array<float, 3>{.5F,.5F,0} &&
                directions->Lights[1].Color == std::array<float, 3>{.5F,.5F,0},
            "native scene directions interpolated by selected slot instead of source identity");

    auto atlasPrevious = plan;
    auto atlasCurrent = cachedPlan;
    auto atlasProgram = std::make_shared<Oot3dNativeGame::Oot3dPicaTemporalVertexProgram>();
    auto& coordinate = atlasProgram->Hooks.TextureCoordinates[0];
    coordinate.Operation = Fast::Renderer3ds::PicaVertexTextureCoordinateOperation::CmbAffine;
    coordinate.SourceCount = 1;
    coordinate.Sources[0].InputRegister = atlasCurrent.VertexAttributes[0].Location;
    atlasPrevious.VertexShader.TemporalProgram = atlasProgram;
    atlasCurrent.VertexShader.TemporalProgram = atlasProgram;
    for (const float alpha : {0.0F, 1.0F / 3.0F, 0.5F, 2.0F / 3.0F, 1.0F}) {
        Require(Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    atlasPrevious, atlasCurrent, alpha, interpolated) &&
                    ReadFloat(interpolated.VertexBindings[0].ResolvedBytes(), 0U) == 6.0F &&
                    std::abs(ReadFloat(interpolated.VertexBindings[1].ResolvedBytes(), 16U) -
                             (10.0F + 4.0F * alpha)) < 0.00001F,
                "atlas selectors must remain native while geometry interpolates at x2/x3");
    }

    auto unsignedPrevious = plan;
    auto unsignedCurrent = cachedPlan;
    unsignedPrevious.VertexAttributes[0].Format =
        Oot3dNativeGame::Oot3dPicaVertexFormat::UnsignedByte;
    unsignedCurrent.VertexAttributes[0].Format =
        Oot3dNativeGame::Oot3dPicaVertexFormat::UnsignedByte;
    unsignedPrevious.VertexAttributes[0].ComponentCount = 1U;
    unsignedCurrent.VertexAttributes[0].ComponentCount = 1U;
    unsignedPrevious.VertexBindings[0].MutableBytes()[0] = 7U;
    unsignedCurrent.VertexBindings[0].MutableBytes()[0] = 19U;
    Require(Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                unsignedPrevious, unsignedCurrent, 0.5F, interpolated) &&
                interpolated.VertexBindings[0].Bytes[0] == 19U,
            "discrete unsigned vertex data was interpolated");

    Oot3dNativeGame::Oot3dPicaVisualFrameAccumulator accumulator;
    plan.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
    cachedPlan.State.Framebuffer.ColorPhysicalAddress = 0x18000000U;
    Oot3dNativeGame::Oot3dPicaMemoryFillSubmission fill;
    fill.StartPhysicalAddress = 0x18000000U;
    fill.EndPhysicalAddress = 0x18010000U;
    fill.Control = 1U;
    accumulator.Append(fill);
    accumulator.Append(plan);
    Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission transfer;
    transfer.CompletionId = 3U;
    transfer.InputPhysicalAddress = 0x18000000U;
    transfer.AfterDrawSubmissionId = plan.SubmissionId;
    accumulator.Append(transfer);
    auto previousFrame = accumulator.Finish(transfer);
    accumulator.Append(fill);
    accumulator.Append(cachedPlan);
    accumulator.Append(transfer);
    auto currentFrame = accumulator.Finish(transfer);
    Require(previousFrame.has_value() && currentFrame.has_value() &&
                previousFrame->Sequence == 1U &&
                currentFrame->Sequence == 2U &&
                accumulator.PendingDrawCount() == 0U,
            "native PICA visual frame boundaries were not retained");
    {
        // Two guest frames accumulated before the host selected the second:
        // one visual frame each, split at the earlier snapshot into a known
        // display framebuffer; a capture into another buffer stays in its frame.
        Oot3dNativeGame::Oot3dPicaVisualFrameAccumulator behind;
        auto first = plan;
        first.SubmissionId = 100U;
        auto second = cachedPlan;
        second.SubmissionId = 101U;
        Oot3dNativeGame::Oot3dPicaDisplayTransferSubmission firstTop;
        firstTop.CompletionId = 10U;
        firstTop.InputPhysicalAddress = 0x18000000U;
        firstTop.OutputPhysicalAddress = 0x18460000U;
        firstTop.AfterDrawSubmissionId = first.SubmissionId;
        auto capture = firstTop;
        capture.CompletionId = 11U;
        capture.OutputPhysicalAddress = 0x18700000U;
        auto secondTop = firstTop;
        secondTop.CompletionId = 12U;
        secondTop.OutputPhysicalAddress = 0x184A0000U;
        secondTop.AfterDrawSubmissionId = second.SubmissionId;
        auto secondFill = fill;
        secondFill.BeforeDrawSubmissionId = second.SubmissionId;
        behind.Append(first);
        behind.Append(capture);
        behind.Append(firstTop);
        behind.Append(secondFill);
        behind.Append(second);
        behind.Append(secondTop);
        const std::array<uint32_t, 2> outputs{0x18460000U, 0x184A0000U};
        auto frames = behind.FinishFrames(secondTop, outputs);
        Require(frames.size() == 2U && frames[0].Draws.size() == 1U &&
                    frames[0].Draws[0].SubmissionId == first.SubmissionId &&
                    frames[0].TopTransfer.CompletionId == firstTop.CompletionId &&
                    frames[0].DisplayTransfers.size() == 2U && frames[0].MemoryFills.empty() &&
                    frames[1].Draws.size() == 1U &&
                    frames[1].Draws[0].SubmissionId == second.SubmissionId &&
                    frames[1].TopTransfer.CompletionId == secondTop.CompletionId &&
                    frames[1].DisplayTransfers.size() == 1U && frames[1].MemoryFills.size() == 1U &&
                    frames[1].Sequence == frames[0].Sequence + 1U &&
                    behind.PendingDrawCount() == 0U,
                "accumulated guest frames were not separated");
        behind.Append(first);
        behind.Append(capture);
        behind.Append(second);
        behind.Append(secondTop);
        frames = behind.FinishFrames(secondTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 2U,
                "a capture into a non-display buffer split a guest frame");
        behind.Append(first);
        behind.Append(firstTop);
        behind.Append(second);
        behind.Append(secondTop);
        frames = behind.FinishFrames(secondTop, {});
        Require(frames.size() == 1U && frames[0].Draws.size() == 2U,
                "a frame was split without known display framebuffers");

        // The guest drew its next frame before the host selected the first:
        // that work stays pending; the first frame's bottom-screen draw after
        // its top snapshot stays with it.
        auto bottom = cachedPlan;
        bottom.SubmissionId = 200U;
        bottom.State.Framebuffer.ColorPhysicalAddress = 0x18300000U;
        auto nextWorld = plan;
        nextWorld.SubmissionId = 201U;
        auto nextFill = fill;
        nextFill.BeforeDrawSubmissionId = nextWorld.SubmissionId;
        auto nextTop = secondTop;
        nextTop.CompletionId = 13U;
        nextTop.AfterDrawSubmissionId = nextWorld.SubmissionId;
        behind.Append(first);
        behind.Append(firstTop);
        behind.Append(bottom);
        behind.Append(nextFill);
        behind.Append(nextWorld);
        behind.Append(nextTop);
        frames = behind.FinishFrames(firstTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 2U &&
                    frames[0].Draws.back().SubmissionId == bottom.SubmissionId &&
                    frames[0].MemoryFills.empty() && frames[0].DisplayTransfers.size() == 1U &&
                    behind.PendingDrawCount() == 1U,
                "the guest's next frame was merged into the selected one");
        frames = behind.FinishFrames(nextTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 1U &&
                    frames[0].Draws[0].SubmissionId == nextWorld.SubmissionId &&
                    frames[0].MemoryFills.size() == 1U && frames[0].DisplayTransfers.size() == 1U &&
                    behind.PendingDrawCount() == 0U,
                "retained next-frame work was not closed with its own snapshot");

        // TopScreen draws the lower menu onto the upper target after the
        // upper snapshot. On a continuation canvas that work completes the
        // snapshot's frame, also an earlier one; the next frame still starts
        // at its first other draw into the source.
        Oot3dNativeGame::Oot3dPicaViewportState sceneCanvas;
        sceneCanvas.HalfWidth = 240.0F;
        sceneCanvas.HalfHeight = 200.0F;
        Oot3dNativeGame::Oot3dPicaViewportState menuCanvas = sceneCanvas;
        menuCanvas.CornerY = 40;
        menuCanvas.HalfHeight = 160.0F;
        const std::array<Oot3dNativeGame::Oot3dPicaViewportState, 1> continuation{menuCanvas};
        const auto sourceDraw = [&](uint64_t id, const Oot3dNativeGame::Oot3dPicaViewportState& viewport) {
            auto draw = cachedPlan;
            draw.SubmissionId = id;
            draw.State.Framebuffer.ColorPhysicalAddress = firstTop.InputPhysicalAddress;
            draw.State.Viewport = viewport;
            return draw;
        };
        const auto snapshot = [&](uint32_t completion, uint32_t output, uint64_t afterDraw) {
            auto transfer = firstTop;
            transfer.CompletionId = completion;
            transfer.OutputPhysicalAddress = output;
            transfer.AfterDrawSubmissionId = afterDraw;
            return transfer;
        };
        auto bottomSnapshot = snapshot(21U, 0x18800000U, 301U);
        bottomSnapshot.InputPhysicalAddress = 0x18300000U;
        const auto menuTop = snapshot(20U, outputs[0], 300U);
        const auto appendMenuFrame = [&]() {
            behind.Append(sourceDraw(300U, sceneCanvas));
            behind.Append(menuTop);
            behind.Append(sourceDraw(301U, menuCanvas));
            behind.Append(bottomSnapshot);
            behind.Append(sourceDraw(302U, sceneCanvas));
        };
        appendMenuFrame();
        frames = behind.FinishFrames(menuTop, outputs, continuation);
        Require(frames.size() == 1U && frames[0].Draws.size() == 2U &&
                    frames[0].Draws.back().SubmissionId == 301U &&
                    frames[0].DisplayTransfers.size() == 2U &&
                    behind.PendingDrawCount() == 1U,
                "a relocated menu drawn after the snapshot left its frame");
        behind.Append(snapshot(22U, outputs[1], 302U));
        frames = behind.FinishFrames(snapshot(22U, outputs[1], 302U), outputs, continuation);
        Require(frames.size() == 1U && frames[0].Draws.size() == 1U &&
                    frames[0].Draws[0].SubmissionId == 302U && behind.PendingDrawCount() == 0U,
                "the frame after a relocated menu did not start at its scene");
        appendMenuFrame();
        frames = behind.FinishFrames(menuTop, outputs);
        Require(frames.size() == 1U && frames[0].Draws.size() == 1U &&
                    behind.PendingDrawCount() == 2U,
                "without a continuation canvas a later source draw stayed in the frame");
        frames = behind.FinishFrames(snapshot(23U, outputs[1], 302U), outputs);
        Require(behind.PendingDrawCount() == 0U, "cannot drain the continuation fixture");
        // Two guest frames, each with its relocated menu, before the host
        // selects the second.
        behind.Append(sourceDraw(400U, sceneCanvas));
        behind.Append(snapshot(30U, outputs[0], 400U));
        behind.Append(sourceDraw(401U, menuCanvas));
        auto firstBottom = bottomSnapshot;
        firstBottom.CompletionId = 31U;
        firstBottom.AfterDrawSubmissionId = 401U;
        behind.Append(firstBottom);
        behind.Append(sourceDraw(402U, sceneCanvas));
        const auto secondMenuTop = snapshot(32U, outputs[1], 402U);
        behind.Append(secondMenuTop);
        behind.Append(sourceDraw(403U, menuCanvas));
        frames = behind.FinishFrames(secondMenuTop, outputs, continuation);
        Require(frames.size() == 2U && frames[0].Draws.size() == 2U &&
                    frames[0].Draws.back().SubmissionId == 401U &&
                    frames[0].DisplayTransfers.size() == 2U &&
                    frames[1].Draws.size() == 2U && frames[1].Draws[0].SubmissionId == 402U &&
                    frames[1].Draws.back().SubmissionId == 403U &&
                    behind.PendingDrawCount() == 0U,
                "an earlier guest frame lost its relocated menu");
    }
    const auto transition =
        Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(
            *previousFrame, *currentFrame);
    Require(transition.PreviousDraws == 1U &&
                transition.CurrentDraws == 1U &&
                transition.MatchedDraws == 1U &&
                transition.ChangedContinuousDraws == 1U &&
                transition.ChangedPerInstanceVertexDraws == 1U &&
                transition.ChangedPerVertexDraws == 1U &&
                transition.AmbiguousPreviousDraws == 0U &&
                transition.AmbiguousCurrentDraws == 0U &&
                transition.TopTargetMatchedDraws == 1U &&
                transition.MatchedDrawCoverage == 1.0 &&
                transition.MedianNormalizedContinuousDelta > 0.6 &&
                transition.P90NormalizedContinuousDelta > 0.6,
            "native PICA visual transition coverage is incorrect");
    Oot3dNativeGame::Oot3dPicaVisualContinuityTracker continuity;
    auto smoothTransition = transition;
    smoothTransition.MedianNormalizedContinuousDelta = 0.2;
    Require(continuity.Accept(smoothTransition) &&
                continuity.CurrentThreshold() >= 0.5,
            "smooth native PICA transition was rejected");
    auto fastMotionTransition = transition;
    fastMotionTransition.MedianNormalizedContinuousDelta = 1.2;
    Require(continuity.Accept(fastMotionTransition),
            "structurally continuous native PICA motion was rejected");
    auto cutTransition = transition;
    cutTransition.PreviousDraws = 4U;
    cutTransition.CurrentDraws = 4U;
    cutTransition.MatchedDraws = 1U;
    cutTransition.MatchedDrawCoverage = 0.25;
    cutTransition.MedianNormalizedContinuousDelta = 1.2;
    Oot3dNativeGame::Oot3dPicaVisualContinuityTracker cutContinuity;
    Require(cutContinuity.Accept(smoothTransition) &&
                !cutContinuity.Accept(cutTransition) &&
                cutContinuity.Accept(smoothTransition),
            "native PICA structural discontinuity was interpolated or did not "
            "reset history");
    {
        auto before = *previousFrame, after = *previousFrame;
        before.Draws[0].Composition.Layer = Oot3dNativeGame::Oot3dPicaCompositionLayer::OpaqueWorld;
        auto program = std::make_shared<Oot3dNativeGame::Oot3dPicaTemporalVertexProgram>();
        before.Draws[0].VertexShader.TemporalProgram = program;
        auto& layout = program->Hooks.Transform;
        layout.Operation = Fast::Renderer3ds::PicaVertexTransformOperation::ModelViewProjection3x4;
        layout.ProjectionFirstUniform = 8; layout.ProjectionRowCount = 4;
        layout.ViewFirstUniform = 16; layout.ViewRowCount = 3;
        layout.ModelFirstUniform = 20; layout.ModelRowCount = 3;
        layout.PositionInputRegister = 0; layout.NormalInputRegister = 1;
        auto& floats = before.Draws[0].VertexShader.Uniforms.Floats;
        for (auto& row : floats) row = {};
        floats[8] = {1,0,0,0}; floats[9] = {0,1,0,0};
        floats[10] = {0,0,-1.002F,-20.02F}; floats[11] = {0,0,-1,0};
        floats[16] = {1,0,0,0}; floats[17] = {0,1,0,0}; floats[18] = {0,0,1,0};
        floats[20] = {1,0,0,0}; floats[21] = {0,1,0,0}; floats[22] = {0,0,1,0};
        before.Draws[0].LightBinding.WorldToView = std::array<float, 16>{
            1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        after = before;
        auto& next = after.Draws[0].VertexShader.Uniforms.Floats;
        // The same static world point must map to the same displayed position
        // through the sampled geometry and its lighting/shadow transform.
        next[20] = {0,0,1,-11}; next[21] = {0,1,0,5}; next[22] = {-1,0,0,7};
        after.Draws[0].LightBinding.WorldToView = std::array<float, 16>{
            0,0,-1,0, 0,1,0,0, 1,0,0,0, -11,5,7,1};
        for (float alpha : {0.0F, 0.5F, 1.0F}) {
            Oot3dNativeGame::Oot3dPicaVulkanDrawPlan sampled;
            Require(Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                        before.Draws[0], after.Draws[0], alpha, sampled),
                    "static geometry could not be sampled");
            Require(sampled.LightBinding.WorldToView.has_value(), "sample lost its world/view anchor");
            const std::array<float, 4> worldPoint{3,5,7,1};
            for (size_t row = 0; row < 3; ++row) {
                float geometryPosition = 0, lightingPosition = 0;
                for (size_t column = 0; column < 4; ++column) {
                    geometryPosition += sampled.VertexShader.Uniforms.Floats[20 + row][column] * worldPoint[column];
                    lightingPosition += (*sampled.LightBinding.WorldToView)[column * 4 + row] * worldPoint[column];
                }
                Require(std::abs(geometryPosition - lightingPosition) < 1e-6F,
                        "interpolation detached lighting/shadows from static geometry");
            }
        }
        next = floats;
        after.Draws[0].LightBinding = before.Draws[0].LightBinding;
        next[16][3] = -2;
        auto camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "ordinary camera translation was rejected");
        next[16] = {-1,0,0,0}; next[18] = {0,0,-1,0};
        const auto sameSceneCut = Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(before, after);
        Oot3dNativeGame::Oot3dPicaVisualContinuityTracker cameraTracker;
        Require(sameSceneCut.MatchedDrawCoverage == 1 && sameSceneCut.CameraDiscontinuousDraws == 1 &&
                    !cameraTracker.Accept(sameSceneCut), "same-geometry camera cut was interpolated");
        next = floats;
        next[16][3] = -5000;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && camera->Discontinuous(), "pure translation camera cut was missed");
        next = floats;
        next[20][3] = -5000;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "actor transform was mistaken for a camera cut");
        camera = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        Require(camera && camera->Discontinuous(), "precomposed rigid view cut was missed");
        next = floats;
        next[20] = {-1,0,0,0}; next[22] = {0,0,-1,0};
        const auto precomposedCut = Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(before, after);
        Require(precomposedCut.CameraDiscontinuousDraws == 1 && !cameraTracker.Accept(precomposedCut),
                "camera precomposed in model rows was not consumed");
        auto isolatedObject = smoothTransition;
        isolatedObject.CameraMatchedDraws = 12;
        isolatedObject.CameraDiscontinuousDraws = 1;
        Require(cameraTracker.Accept(isolatedObject), "one rigid actor reset the whole camera history");
        next = floats;
        next[16][3] = 1;
        floats[16][3] = -1;
        camera = Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next);
        Require(camera && !camera->Discontinuous(), "crossing the origin caused a false camera cut");
        floats[16][3] = 0;
        next = floats;
        next[16][3] = -5000;
        const auto fullScale = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        for (size_t row = 20; row < 23; ++row)
            for (size_t col = 0; col < 3; ++col) { floats[row][col] *= 0.01F; next[row][col] *= 0.01F; }
        const auto smallScale = Fast::Renderer3ds::MeasurePicaRigidViewTemporalDelta(layout, floats, next);
        Require(fullScale && smallScale && std::abs(fullScale->NearPlaneTravel-smallScale->NearPlaneTravel) < 0.01,
                "camera cut policy depends on asset model scale");
        next = floats;
        next[16][0] = 0;
        Require(!Fast::Renderer3ds::MeasurePicaCameraTemporalDelta(layout, floats, next),
                "singular view matrix was accepted");
    }
    Oot3dNativeGame::Oot3dPicaVisualFrameSample sample;
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                *previousFrame, *currentFrame, 0.5F, sample) &&
                sample.Draws.size() == 1U && sample.MatchedDraws == 1U &&
                sample.InterpolatedDraws == 1U &&
                sample.InterpolatedPerInstanceVertexDraws == 1U &&
                sample.InterpolatedPerVertexDraws == 1U &&
                sample.MemoryFills.size() == 1U &&
                sample.DisplayTransfers.size() == 1U,
            "native PICA full-frame visual sample is incorrect");
    Oot3dNativeGame::Oot3dPicaVisualTransitionStats combinedTransition;
    Oot3dNativeGame::Oot3dPicaVisualFrameSample combinedSample;
    Require(Oot3dNativeGame::AnalyzeAndSampleOot3dPicaVisualFrame(*previousFrame, *currentFrame, 0.5F,
                                                                  combinedTransition, combinedSample) &&
                combinedTransition.MatchedDraws == transition.MatchedDraws &&
                combinedTransition.ChangedContinuousDraws == transition.ChangedContinuousDraws &&
                combinedTransition.ChangedPerVertexDraws == transition.ChangedPerVertexDraws &&
                combinedSample.MatchedDraws == sample.MatchedDraws &&
                combinedSample.InterpolatedDraws == sample.InterpolatedDraws &&
                combinedSample.Draws[0].VertexShader.Uniforms.Floats[0][0] ==
                    sample.Draws[0].VertexShader.Uniforms.Floats[0][0],
            "combined native PICA analysis changed transition or sample semantics");
    Oot3dNativeGame::Oot3dPicaVisualTransitionStats viewTransition;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample viewSample;
    Require(Oot3dNativeGame::AnalyzeAndSampleOot3dPicaVisualFrameView(*previousFrame, *currentFrame, 0.5F,
                                                                      viewTransition, viewSample) &&
                viewSample.Draws.size() == 1U && viewSample.Draws[0].BasePlan == &currentFrame->Draws[0] &&
                viewSample.InterpolatedDraws == sample.InterpolatedDraws &&
                viewSample.Draws[0].VertexUniforms.has_value() &&
                viewSample.Draws[0].LightBinding.has_value() &&
                viewSample.Draws[0].LightBinding->Sources[0].PositionWorld[0] == 4.0F &&
                viewSample.Draws[0].LightBinding->ScenePoints &&
                viewSample.Draws[0].LightBinding->ScenePoints->Lights[0].PositionWorld[0] == 10 &&
                viewSample.Draws[0].VertexUniforms->Floats[0][0] ==
                    sample.Draws[0].VertexShader.Uniforms.Floats[0][0] &&
                viewSample.Draws[0].HasVertexBindingOverrides &&
                viewSample.Draws[0].VertexBindings.size() == sample.Draws[0].VertexBindings.size() &&
                viewSample.Draws[0].VertexBindings[0].Bytes == sample.Draws[0].VertexBindings[0].Bytes,
            "non-owning native PICA visual sample changed interpolation semantics");

    Oot3dNativeGame::Oot3dPicaPreparedVisualTransition preparedTransition;
    Oot3dNativeGame::Oot3dPicaVisualInterpolationTiming preparedTiming;
    Require(preparedTransition.Prepare(*previousFrame, *currentFrame,
                                       &preparedTiming) &&
                preparedTransition.Ready() &&
                preparedTransition.Stats().MatchedDraws ==
                    transition.MatchedDraws,
            "prepared visual transition did not retain its analysis");
    const uint64_t preparedMatchingNanoseconds =
        preparedTiming.MatchingNanoseconds;
    const uint64_t preparedAnalysisNanoseconds =
        preparedTiming.AnalysisNanoseconds;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample x3FirstSample;
    Oot3dNativeGame::Oot3dPicaVisualFrameViewSample x3SecondSample;
    Require(preparedTransition.Sample(*previousFrame, *currentFrame,
                                      1.0F / 3.0F, x3FirstSample,
                                      &preparedTiming) &&
                preparedTransition.Sample(*previousFrame, *currentFrame,
                                          2.0F / 3.0F, x3SecondSample,
                                          &preparedTiming) &&
                preparedTiming.MatchingNanoseconds ==
                    preparedMatchingNanoseconds &&
                preparedTiming.AnalysisNanoseconds ==
                    preparedAnalysisNanoseconds &&
                x3FirstSample.InterpolatedDraws == 1U &&
                x3SecondSample.InterpolatedDraws == 1U &&
                x3FirstSample.Draws[0].VertexUniforms.has_value() &&
                x3SecondSample.Draws[0].VertexUniforms.has_value() &&
                x3FirstSample.Draws[0].VertexUniforms->Floats[0][0] <
                    x3SecondSample.Draws[0].VertexUniforms->Floats[0][0],
            "x3 visual samples repeated matching or lost temporal order");
    preparedTransition.Reset();
    Require(!preparedTransition.Ready(),
            "prepared visual transition reset retained stale state");

    cachedPlan.CommandListOffsetWords += 1U;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan),
            "volatile native command-list storage broke draw identity");
    cachedPlan.State.VertexInput.Loaders[0].PhysicalAddress += 0x40U;
    cachedPlan.State.VertexInput.PhysicalBaseAddress += 0x40U;
    cachedPlan.State.VertexInput.IndexPhysicalAddress += 0x40U;
    Require(Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, cachedPlan) &&
                Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, cachedPlan, 0.5F, interpolated),
            "rotating native buffer addresses broke structural identity");

    auto repeatedPreviousA = plan;
    auto repeatedPreviousB = plan;
    auto repeatedCurrentA = plan;
    auto repeatedCurrentB = plan;
    repeatedPreviousA.VertexShader.Uniforms.Floats[0][0] = 2.0F;
    repeatedPreviousB.VertexShader.Uniforms.Floats[0][0] = 20.0F;
    repeatedCurrentA.VertexShader.Uniforms.Floats[0][0] = 6.0F;
    repeatedCurrentB.VertexShader.Uniforms.Floats[0][0] = 28.0F;
    Oot3dNativeGame::Oot3dPicaVisualFrame repeatedPreviousFrame;
    repeatedPreviousFrame.TopTransfer = transfer;
    repeatedPreviousFrame.Draws = {repeatedPreviousA, repeatedPreviousB};
    Oot3dNativeGame::Oot3dPicaVisualFrame repeatedCurrentFrame;
    repeatedCurrentFrame.TopTransfer = transfer;
    repeatedCurrentFrame.Draws = {repeatedCurrentA, repeatedCurrentB};
    Oot3dNativeGame::Oot3dPicaVisualFrameSample repeatedSample;
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.MatchedDraws == 2U &&
                repeatedSample.StrictUniqueMatchedDraws == 0U &&
                repeatedSample.StrictOrdinalMatchedDraws == 2U &&
                repeatedSample.StructuralOrdinalMatchedDraws == 0U &&
                repeatedSample.Draws[0]
                        .VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                repeatedSample.Draws[1]
                        .VertexShader.Uniforms.Floats[0][0] == 24.0F,
            "repeated native draws were not paired by stable occurrence");

    auto relocatedCurrentA = repeatedCurrentA;
    auto relocatedCurrentB = repeatedCurrentB;
    for (auto* relocated : {&relocatedCurrentA, &relocatedCurrentB}) {
        relocated->State.VertexInput.PhysicalBaseAddress += 0x200U;
        relocated->State.VertexInput.IndexPhysicalAddress += 0x200U;
        relocated->State.VertexInput.Loaders[0].PhysicalAddress += 0x200U;
        for (auto& texture : relocated->Textures) {
            texture.State.PhysicalAddress += 0x400U;
        }
    }
    repeatedCurrentFrame.Draws = {relocatedCurrentA, relocatedCurrentB};
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.MatchedDraws == 2U &&
                repeatedSample.StrictUniqueMatchedDraws == 0U &&
                repeatedSample.StrictOrdinalMatchedDraws == 0U &&
                repeatedSample.StructuralOrdinalMatchedDraws == 2U &&
                repeatedSample.Draws[0]
                        .VertexShader.Uniforms.Floats[0][0] == 4.0F &&
                repeatedSample.Draws[1]
                        .VertexShader.Uniforms.Floats[0][0] == 24.0F,
            "relocated repeated native draws lost visual interpolation");

    auto newlyVisibleDraw = relocatedCurrentB;
    newlyVisibleDraw.VertexShader.Uniforms.Floats[0][0] = 100.0F;
    repeatedCurrentFrame.Draws.push_back(newlyVisibleDraw);
    const auto changedCardinality =
        Oot3dNativeGame::AnalyzeOot3dPicaVisualTransition(
            repeatedPreviousFrame, repeatedCurrentFrame);
    Require(changedCardinality.MatchedDraws == 2U &&
                changedCardinality.StructuralOrdinalMatchedDraws == 2U &&
                changedCardinality.UnmatchedPreviousDraws == 0U &&
                changedCardinality.UnmatchedCurrentDraws == 1U,
            "changed draw cardinality interrupted stable occurrences");

    auto incompatibleGeometry = relocatedCurrentA;
    incompatibleGeometry.VertexCount += 1U;
    Require(!Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, incompatibleGeometry) &&
                !Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, incompatibleGeometry, 0.5F, interpolated),
            "different native pipeline layout was treated as one visual draw");

    auto uiDomainDraw = plan;
    uiDomainDraw.CompositionDomain =
        Oot3dNativeGame::Oot3dPicaCompositionDomain::Ui;
    Require(!Oot3dNativeGame::AreOot3dPicaVisualDrawsCompatible(
                plan, uiDomainDraw) &&
                !Oot3dNativeGame::InterpolateOot3dPicaVisualDraw(
                    plan, uiDomainDraw, 0.5F, interpolated),
            "scene and UI draws shared one interpolation identity");

    auto changedNativeGeometry = relocatedCurrentA;
    changedNativeGeometry.IndexBytes[0] ^= 1U;
    changedNativeGeometry.IndexContentVersionAvailable = false;
    changedNativeGeometry.GeometryIdentityAvailable = false;
    repeatedPreviousFrame.Draws = {repeatedPreviousA};
    repeatedCurrentFrame.Draws = {changedNativeGeometry};
    Require(Oot3dNativeGame::SampleOot3dPicaVisualFrame(
                repeatedPreviousFrame, repeatedCurrentFrame, 0.5F,
                repeatedSample) &&
                repeatedSample.PipelineOrdinalMatchedDraws == 1U &&
                repeatedSample.Draws[0].IndexBytes ==
                    changedNativeGeometry.IndexBytes,
            "discrete native geometry change blocked continuous transforms");

    std::cout << "oot3d_native_pica_vulkan_plan_tests: ok\n";
    return 0;
}
