#include "oot3d_cpu_phase_probe.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>

int main() {
    using namespace Oot3dNativeGame::CpuPhaseProbe;
    Ledger ledger;
    ledger.StartAt({10, 3, 2, 100, true});
    ledger.ChangeAt(Phase::Aot, {11, 3.5, 2.1, 150, true});
    ledger.ChangeAt(Phase::Svc, {15, 6.5, 2.2, 450, true});
    ledger.ChangeAt(Phase::PicaFrontend, {16, 6.8, 2.3, 490, true});
    ledger.ChangeAt(Phase::Svc, {18, 8.3, 2.4, 690, true});
    ledger.ChangeAt(Phase::Host, {19, 8.7, 2.5, 730, true});
    Total sum{};
    for (const auto& value : ledger.Values) {
        sum.Wall += value.Wall; sum.User += value.User;
        sum.Kernel += value.Kernel; sum.Cycles += value.Cycles;
    }
    assert(sum.Wall == 9 && std::abs(sum.User - 5.7) < 1e-10);
    assert(std::abs(sum.Kernel - 0.5) < 1e-10 && sum.Cycles == 630);
    assert(ledger.Values[static_cast<size_t>(Phase::Svc)].Wall == 2);
    assert(ledger.Values[static_cast<size_t>(Phase::Aot)].Wall == 4);
    ledger.ChangeAt(Phase::Aot, {});
    assert(!ledger.Active && ledger.ReadFailures == 1);
    Thread.Active = false;
    { Scope scope(Phase::Aot); scope.Stop(); }
    assert(!Thread.Active);
    if constexpr (BuildEnabled) {
        Thread.StartAt(Read());
        assert(Thread.Active);
        {
            Scope aot(Phase::Aot);
            assert(Thread.Current == Phase::Aot);
            { Scope svc(Phase::Svc); assert(Thread.Current == Phase::Svc); }
            assert(Thread.Current == Phase::Aot);
            aot.Stop();
            aot.Stop();
            assert(Thread.Current == Phase::Host);
        }
        const auto measured = Thread.Snapshot();
        assert(measured[static_cast<size_t>(Phase::Aot)].Intervals == 2);
        assert(measured[static_cast<size_t>(Phase::Aot)].Cycles > 0);
        assert(Thread.ReadFailures == 0);
        Thread.Active = false;
    }
    return 0;
}
