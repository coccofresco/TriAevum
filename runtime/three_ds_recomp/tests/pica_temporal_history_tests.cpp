#include "fast/renderer3ds/pica_temporal_history.h"
#include "fast/renderer3ds/pica_motion_provenance.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace Fast::Renderer3ds;
using Reason = PicaTemporalMatchReason;
static void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    PicaTemporalHistory History;
    PicaFrameTemporalSample Sample;
    std::array<uint8_t, 4> Uniforms{1,2,3,4}, Vertices{5,6,7,8}, Defaults{9,10,11,12}, Indices{0,1,2,3};
    std::array<PicaVertexBindingView, 2> Bindings{{{0,4,false,Vertices},{1,4,true,Defaults}}};
    std::array<PicaVertexAttributeView, 1> Attributes{{{0,0,PicaVertexFormat::Float,1,0}}};
    uint64_t Frame = 0;
    Fixture() {
        Sample.SchemaVersion = kPicaFrameTemporalSampleSchemaVersion;
        Sample.ContinuityEpoch = 1; Sample.CurrentSourceFrameId = 1;
        Sample.NativeRateHz = 30; Sample.TargetPresentationRateHz = 60;
        Sample.SampleDeltaSeconds = 1.0F/60.0F;
    }
    PicaDrawView Draw(uint64_t instance, uint64_t submission = 1) {
        PicaDrawView draw;
        draw.SubmissionId = submission;
        draw.Composition.TemporalIdentity = {1,instance,30,0,1};
        draw.RenderTargetNamespace = 2;
        draw.FramebufferColorPhysicalAddress = 100;
        draw.FramebufferDepthPhysicalAddress = 200;
        draw.FramebufferWidth = 400; draw.FramebufferHeight = 240;
        draw.VertexShaderKey = 11; draw.CanonicalVertexProgramId = 12;
        draw.VertexUniformBytes = Uniforms; draw.VertexBindings = Bindings;
        draw.VertexAttributes = Attributes; draw.IndexBytes = Indices;
        draw.VertexCount = 4; draw.Indexed = true;
        return draw;
    }
    void Begin(std::span<const PicaDrawView> draws, uint64_t step = 1) {
        Frame += step;
        std::vector<PicaCompositionDrawReference> refs;
        for (const auto& draw : draws) refs.push_back(BuildPicaCompositionDrawReference(draw));
        Require(History.PrepareSequence({kPicaCompositionSequenceSchemaVersion, Frame, refs}, Frame), "prepare failed");
    }
    PicaTemporalMatch Track(const PicaDrawView& draw) {
        return History.Track(draw, Frame, Sample, {float(Frame)/100.0F,0});
    }
};
int main() {
    Fixture f;
    auto a = f.Draw(10,1), b = f.Draw(20,2);
    std::array draws{a,b};
    f.Begin(draws);
    Require(f.Track(a).Reason == Reason::MissingPrevious, "first sample accepted history");
    f.Uniforms[0] = 20;
    Require(!f.Track(b).Exact(), "first B accepted history");
    // Remove A, reorder B to the first draw and change its transforms. Identity
    // remains B across source transitions / synthetic samples / repeats.
    f.Uniforms[0] = 25;
    f.Sample.CurrentSourceFrameId = 2;
    f.Begin(std::array{b});
    auto match = f.Track(b);
    Require(match.Exact() && match.Previous->Bytes[0] == 20, "culled A contaminated B");
    Require(match.Previous->JitterNdc[0] == .01F, "wrong presented jitter");
    f.Begin(std::array{b,a});
    Require(f.Track(b).Exact(), "reorder lost B");
    Require(!f.Track(a).Exact(), "culled A reused stale history");
    f.Sample.CurrentSourceFrameId = 3;
    f.Begin(std::array{a,b});
    Require(f.Track(a).Exact() && f.Track(b).Exact(), "new transition changed identity");
    Require(f.Track(b).Reason == Reason::DuplicateSubmission, "same submission accepted twice");

    // Ambiguity must reject the FIRST duplicate, and neither duplicate may
    // seed a history that a subsequent unique draw could accidentally use.
    auto duplicate = b; duplicate.SubmissionId = 3;
    f.Begin(std::array{b,duplicate});
    Require(f.Track(b).Reason == Reason::Ambiguous && f.Track(duplicate).Reason == Reason::Ambiguous,
            "first duplicate was considered exact");
    f.Begin(std::array{b});
    Require(!f.Track(b).Exact(), "ambiguous prior sample seeded history");
    f.Begin(std::array{b}); Require(f.Track(b).Exact(), "unique history did not recover");
    b.Composition.TemporalIdentity = {};
    f.Begin(std::array{b});
    Require(f.Track(b).Reason == Reason::MissingIdentity, "geometry fallback admitted missing identity");

    Fixture cadence;
    auto moving = cadence.Draw(77);
    cadence.Sample.Interpolation = PicaVisualInterpolationMode::Fixed2x;
    cadence.Sample.FixedSampleMultiplier = 2;
    cadence.Sample.Kind = PicaFrameTemporalSampleKind::Transition;
    cadence.Sample.Synthetic = true;
    for (unsigned sample = 0; sample < 6; ++sample) {
        cadence.Sample.PreviousSourceFrameId = 10 + sample / 2;
        cadence.Sample.CurrentSourceFrameId = 11 + sample / 2;
        cadence.Sample.SampleOrdinal = sample % 2;
        cadence.Sample.Alpha = float(sample % 2) * .5F;
        cadence.Uniforms[0] = uint8_t(sample * 10);
        cadence.Begin(std::array{moving});
        const auto history = cadence.Track(moving);
        Require(sample == 0 ? !history.Exact() :
            history.Exact() && history.Previous->Bytes[0] == (sample - 1) * 10,
            "alpha 0/0.5 or source transition advanced the wrong history");
    }

    const auto rejects = [](auto mutate, Reason reason) {
        Fixture test;
        auto draw = test.Draw(10);
        test.Begin(std::array{draw}); (void)test.Track(draw);
        mutate(test, draw);
        test.Begin(std::array{draw});
        Require(test.Track(draw).Reason == reason, "compatibility/invalidation boundary failed");
    };
    rejects([](auto&, auto& d){ ++d.CanonicalVertexProgramId; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto&, auto& d){ ++d.VertexShaderKey; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto&, auto& d){ d.VertexShaderKey = d.CanonicalVertexProgramId = 0; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto& t, auto&){ ++t.Bindings[0].ByteStride; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto& t, auto&){ ++t.Attributes[0].ByteOffset; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto&, auto& d){ ++d.BaseVertex; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto&, auto& d){ d.FramebufferFlipped = true; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto&, auto& d){ d.ViewportX = 1; }, Reason::IncompatibleProgramOrLayout);
    rejects([](auto& t, auto&){ ++t.Vertices[0]; }, Reason::ChangedInputs);
    rejects([](auto& t, auto&){ ++t.Defaults[0]; }, Reason::ChangedInputs);
    rejects([](auto& t, auto&){ ++t.Indices[0]; }, Reason::ChangedInputs);
    rejects([](auto&, auto& d){ d.VertexUniformBytes = {}; }, Reason::UniformSize);
    rejects([](auto& t, auto&){ ++t.Sample.ContinuityEpoch; }, Reason::MissingPrevious);
    rejects([](auto& t, auto&){ t.Sample.HistoryReset = true; }, Reason::Reset);
    rejects([](auto& t, auto&){ t.Sample.SchemaVersion = 0; }, Reason::InvalidSample);
    rejects([](auto&, auto& d){ ++d.RenderTargetNamespace; }, Reason::MissingPrevious);
    rejects([](auto&, auto& d){ ++d.FramebufferColorPhysicalAddress; }, Reason::MissingPrevious);
    rejects([](auto&, auto& d){ ++d.FramebufferDepthPhysicalAddress; }, Reason::MissingPrevious);
    rejects([](auto&, auto& d){ ++d.Composition.TemporalIdentity.Lifetime; }, Reason::MissingPrevious);
    rejects([](auto& t, auto&){ ++t.Frame; }, Reason::MissingPrevious);

    // Independent namespaces/targets may interleave; their identity is a tuple,
    // never a pre-hashed scalar susceptible to occurrence shifts.
    Fixture views;
    a = views.Draw(10,1); b = views.Draw(10,2); b.RenderTargetNamespace++;
    views.Begin(std::array{a,b}); (void)views.Track(a); (void)views.Track(b);
    views.Begin(std::array{b,a});
    Require(views.Track(a).Exact() && views.Track(b).Exact(), "view contamination");
    auto ref = BuildPicaCompositionDrawReference(a);
    Require(!views.History.PrepareSequence({kPicaCompositionSequenceSchemaVersion,99,{&ref,1}}, views.Frame),
            "second partial view declaration admitted");
    // A NativeMemory replay neither seeds nor advances object history.
    Fixture replay;
    a = replay.Draw(10);
    replay.Begin(std::array{a}); (void)replay.Track(a);
    ref = BuildPicaCompositionDrawReference(a);
    Require(replay.History.PrepareSequence({kPicaCompositionSequenceSchemaVersion,99,{&ref,1},
        PicaCompositionPurpose::NativeMemory}, replay.Frame), "native replay rejected");
    Require(replay.Track(a).Reason == Reason::Unpublished, "native replay consumed history");
    replay.Begin(std::array{a}); Require(replay.Track(a).Exact(), "native replay advanced history");

    Require(ResolvePicaMotionProvenance(true,true,true) == PicaMotionProvenance::Exact, "exact precedence");
    Require(ResolvePicaMotionProvenance(false,true,true) == PicaMotionProvenance::Rigid, "rigid precedence");
    Require(ResolvePicaMotionProvenance(false,false,true) == PicaMotionProvenance::Camera, "explicit camera proof");
    Require(ResolvePicaMotionProvenance(false,false,false) == PicaMotionProvenance::Invalid, "missing moving history");
    std::cout << "temporal history: reorder, cull, ambiguity, views, epochs, inputs, replay, provenance passed\n";
}
