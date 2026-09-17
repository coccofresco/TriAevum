#define TRIAEVUM_PROJECTION_KERNEL_TEST
#include "oot3d_projection_kernel_probe.cpp"
#include <cstdio>

int main() {
    uint32_t seed=0x145769U;
    auto random=[&] { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return seed; };
    unsigned accepted=0, rejected=0;
    for (unsigned test=0;test<40000;++test) {
        std::array<uint32_t,16> matrix;
        std::array<uint32_t,4> point, output;
        auto value=[&] {
            const auto bits=random();
            if (test%5==0) return bits;
            return (bits&0x807FFFFFU) | ((100U+(random()%50U))<<23U);
        };
        for (auto& x:matrix) x=value();
        for (auto& x:point) x=value();
        if (test%11==0) point[test%4]=test%2 ? 0x80000000U : 0;
        const uint32_t fpscr=(test%4)*0x01000000U;
        const auto host=_mm_getcsr();
        uint32_t flags=0;
        if (!Project(matrix,point,fpscr,output,flags)) {
            if (_mm_getcsr()!=host) return 2;
            ++rejected; continue;
        }
        if (_mm_getcsr()!=host) return 2;
        uint32_t expectedFlags=0;
        for (unsigned row=0;row<4;++row) {
            auto r=a32::VfpBinary32Multiply(matrix[row*4],point[0],fpscr);
            expectedFlags|=r.exception_flags;
            for (unsigned col=1;col<4;++col) {
                r=a32::VfpBinary32MultiplyAccumulate(r.value,matrix[row*4+col],point[col],fpscr);
                expectedFlags|=r.exception_flags;
            }
            if (output[row]!=r.value) { std::printf("value mismatch %u row %u\n",test,row); return 3; }
        }
        if (flags!=expectedFlags) {
            std::printf("flags mismatch %u actual=%08x expected=%08x fpscr=%08x\n",test,flags,expectedFlags,fpscr);
            for (auto x:matrix) std::printf("%08x ",x);
            std::puts(""); for(auto x:point) std::printf("%08x ",x); std::puts("");
            return 4;
        }
        ++accepted;
    }
    std::printf("projection differential: accepted=%u rejected=%u\n",accepted,rejected);
    return accepted>30000 ? 0 : 5;
}
