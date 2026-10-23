#include "oot3d_native_skel_anime.h"

#include <cmath>
#include <stdexcept>

namespace {

void ExpectNear(float actual, float expected) {
    if (std::abs(actual - expected) > 0.00001f) {
        throw std::runtime_error("native SkelAnime frame mismatch");
    }
}

} // namespace

void RunNativeSkelAnimeTests() {
    Oot3dNativeGame::SkelAnimeClock loop;
    loop.Change(Oot3dNativeGame::SkelAnimeMode::Loop, 1.0f, 0.0f, 1.0f);
    loop.AdvanceDisplayTick();
    ExpectNear(loop.State().CurrentFrame, 2.0f / 3.0f);
    loop.AdvanceDisplayTick();
    ExpectNear(loop.State().CurrentFrame, 4.0f / 3.0f);
    loop.AdvanceDisplayTick();
    ExpectNear(loop.State().CurrentFrame, 0.0f);

    Oot3dNativeGame::SkelAnimeClock once;
    once.Change(Oot3dNativeGame::SkelAnimeMode::Once, 1.0f, 0.0f, 1.0f);
    if (once.AdvanceDisplayTick()) {
        throw std::runtime_error("native once animation completed before its terminal frame");
    }
    once.AdvanceDisplayTick();
    ExpectNear(once.State().CurrentFrame, 1.0f);
    if (!once.AdvanceDisplayTick() || !once.State().Complete) {
        throw std::runtime_error("native once animation did not complete after its terminal frame");
    }

    Oot3dNativeGame::SkelAnimeClock reverse;
    reverse.Change(Oot3dNativeGame::SkelAnimeMode::Once, -1.0f, 1.0f, 0.0f);
    if (reverse.AdvanceDisplayTick()) {
        throw std::runtime_error("native reverse animation completed before its start frame");
    }
    reverse.AdvanceDisplayTick();
    ExpectNear(reverse.State().CurrentFrame, 0.0f);
    if (!reverse.AdvanceDisplayTick() || !reverse.State().Complete) {
        throw std::runtime_error("native reverse animation did not complete at its start frame");
    }

    Oot3dNativeGame::SkelAnimeClock interpolated;
    interpolated.Change(Oot3dNativeGame::SkelAnimeMode::LoopInterpolated,
                        1.0f, 0.0f, 1.0f);
    if (interpolated.State().NativeUpdateMode != 4) {
        throw std::runtime_error("native interpolated loop dispatch mode mismatch");
    }
    interpolated.AdvanceDisplayTick();
    ExpectNear(interpolated.State().CurrentFrame, 2.0f / 3.0f);

    Oot3dNativeGame::SkelAnimeClock partial;
    partial.Change(Oot3dNativeGame::SkelAnimeMode::PartialLoop,
                   1.0f, 3.0f, 5.0f);
    if (partial.State().NativeUpdateMode != 5) {
        throw std::runtime_error("native partial-loop dispatch mode mismatch");
    }
    partial.AdvanceDisplayTick();
    ExpectNear(partial.State().CurrentFrame, 14.0f / 3.0f);
    partial.AdvanceDisplayTick();
    ExpectNear(partial.State().CurrentFrame, 10.0f / 3.0f);

    Oot3dNativeGame::SkelAnimeClock timed;
    timed.Change(Oot3dNativeGame::SkelAnimeMode::Loop, 1.0f, 0.0f, 9.0f);
    timed.AdvanceSeconds(1.0 / 60.0);
    ExpectNear(timed.State().CurrentFrame, 1.0f / 3.0f);
    timed.AdvanceSeconds(1.0 / 60.0);
    ExpectNear(timed.State().CurrentFrame, 2.0f / 3.0f);

    Oot3dNativeGame::SkelAnimeClock timed120;
    timed120.Change(Oot3dNativeGame::SkelAnimeMode::Loop, 1.0f, 0.0f, 9.0f);
    timed120.AdvanceSeconds(1.0 / 120.0);
    ExpectNear(timed120.State().CurrentFrame, 1.0f / 6.0f);
    timed120.AdvanceSeconds(1.0 / 120.0);
    ExpectNear(timed120.State().CurrentFrame,
               timed.State().CurrentFrame / 2.0f);
}
