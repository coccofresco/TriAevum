#include "oot3d_native_pica_composition.h"
#include "oot3d_native_pica_visual_frame.h"
#include "oot3d_native_a32_memory.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_runtime.h"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace Oot3dNativeGame;
static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    NativeA32Memory memory;
    std::string error;
    Require(memory.MapRegion({"cursor",0x0054C000U,0x1000U,true,false,{}}, &error), "map cursor");
    Require(memory.MapRegion({"models",0x10000000U,0x1000U,true,false,{}}, &error), "map models");
    Require(memory.Write32(0x10000010U,16) && memory.Write16(0x10000100U,7), "seed model/actor");
    Oot3dNativePicaCompositionTracker tracker;
    oot3d::recomp::a32::GuestState state{};
    const auto capture = [&](uint32_t renderer, uint32_t destination, uint32_t pass = 0) {
        state.r[1] = 0x10000100U;
        tracker.ObserveBlockEntry(kOot3dActorDrawEntry,state,memory);
        state.r[0] = renderer; state.r[1] = pass;
        state.r[14] = pass == 0 ? 0x002FADDCU : 0x002FADF4U;
        tracker.ObserveBlockEntry(kOot3dCmbRendererSubmitDrawHandleEntry,state,memory);
        Require(memory.Write32(0x0054CC4CU,destination), "cursor write");
        state.r[0] = 0x10000000U;
        tracker.ObserveBlockEntry(kOot3dMeshCommandPacketSubmitEntry,state,memory);
        tracker.ObserveBlockEntry(state.r[14],state,memory);
        tracker.ObserveBlockEntry(kOot3dActorDrawCallbackReturn,state,memory);
        std::vector<Oot3dPicaCommandListCompositionSpan> spans;
        Require(tracker.TakeCommandListCompositionSpans(destination,16,spans,&error) && spans.size()==1,
                "missing native evidence span");
        Oot3dNativePicaFrontend frontend;
        frontend.SetCommandListCompositionDomain(Oot3dPicaCompositionDomain::Scene);
        Require(frontend.SetNextCommandListCompositionSpans(destination,16,spans,&error), "span publication");
        Oot3dGspCommandPacket command{}; command.Control=1;
        command.Parameters[0]=destination; command.Parameters[1]=16;
        const std::array<uint32_t,4> words{1,0x000F022E,1,0x000F022E};
        Require(frontend.SubmitGspCommand(command,words,&error), "frontend draw");
        const auto draws=frontend.PendingDrawPackets();
        Require(draws.size()==2, "packet draw count");
        const auto first=draws[0].Composition.TemporalIdentity;
        const auto second=draws[1].Composition.TemporalIdentity;
        Require(first.Available() && first.Owner==((uint64_t{7}<<32U)|0x10000100U) &&
                first.Instance==renderer && first.Part==0x10000000U, "native instance address evidence");
        Require(second.Slot==first.Slot+8 && draws[0].Composition.TemporalPacketBeginAddress==0,
                "source-relative draw slot / transient cursor");
        return first;
    };
    const auto first=capture(0x10000200U,0x14001000U);
    Require(capture(0x10000200U,0x14002000U)==first, "destination ping-pong changed identity");
    Require(capture(0x10000300U,0x14003000U)!=first, "shared mesh merged model instances");
    Require(capture(0x10000200U,0x14004000U,1)!=first, "native passes merged");
    state.r[0]=0x10000900U;
    tracker.ObserveBlockEntry(0x002D644CU,state,memory);
    Require(capture(0x10000200U,0x14004800U)==first, "unrelated actor destroy changed surviving identity");
    state.r[0]=0x10000100U;
    tracker.ObserveBlockEntry(0x002D644CU,state,memory);
    Require(capture(0x10000200U,0x14005000U).Lifetime!=first.Lifetime, "destroy/recycle retained lifetime");
    tracker.Reset();
    Require(capture(0x10000200U,0x14006000U).Lifetime!=first.Lifetime, "tracker reset retained lifetime");

    // Known instance correspondence also governs interpolation before renderer
    // history sees a sample. Same geometry and program, changing model uniforms.
    Oot3dPicaVisualFrame previous, current;
    previous.Sequence=1; current.Sequence=2;
    Oot3dPicaVulkanDrawPlan a,b;
    a.Composition.TemporalIdentity=first;
    b=a; b.Composition.TemporalIdentity.Instance++;
    a.VertexShader.Uniforms.Floats[10][0]=10;
    b.VertexShader.Uniforms.Floats[10][0]=100;
    previous.Draws={a,b};
    a.VertexShader.Uniforms.Floats[10][0]=20;
    b.VertexShader.Uniforms.Floats[10][0]=120;
    current.Draws={b,a};
    Oot3dPicaVisualFrameSample sampled;
    Require(SampleOot3dPicaVisualFrame(previous,current,.5F,sampled), "sample");
    Require(sampled.MatchedDraws==2 && sampled.Draws[0].VertexShader.Uniforms.Floats[10][0]==110 &&
            sampled.Draws[1].VertexShader.Uniforms.Floats[10][0]==15, "reordered instances blended across owners");
    Require(sampled.Draws[0].Composition.TemporalIdentity==b.Composition.TemporalIdentity, "sample lost ID");
    current.Draws={b};
    Require(SampleOot3dPicaVisualFrame(previous,current,.5F,sampled) && sampled.MatchedDraws==1 &&
            sampled.Draws[0].VertexShader.Uniforms.Floats[10][0]==110, "culling changed correspondence");
    current.Draws={b,b};
    Require(SampleOot3dPicaVisualFrame(previous,current,.5F,sampled) && sampled.MatchedDraws==0,
            "ambiguous instance fell through to ordinal interpolation");
    current.Draws={b}; previous.Draws={b,b};
    Require(SampleOot3dPicaVisualFrame(previous,current,.5F,sampled) && sampled.MatchedDraws==0,
            "ambiguous prior instance fell through to ordinal interpolation");
    previous.Draws={a}; current.Draws={a};
    current.Draws[0].Composition.TemporalIdentity.Lifetime++;
    Require(SampleOot3dPicaVisualFrame(previous,current,.5F,sampled) && sampled.MatchedDraws==0,
            "recycled lifetime was interpolated");
    std::cout << "native temporal evidence, packet slots, lifetime and visual correspondence passed\n";
}
