#include "a32_thumb.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

using namespace oot3d::recomp::a32;

// 16 KiB of byte-addressed little-endian guest memory at address 0.
class ByteMemory final : public MemoryBus {
public:
    bool Read32(std::uint32_t address, std::uint32_t* value) override {
        return Copy(address, value, 4U);
    }
    bool Write32(std::uint32_t address, std::uint32_t value) override {
        return Store(address, &value, 4U);
    }
    bool Read16(std::uint32_t address, std::uint16_t* value) override {
        return Copy(address, value, 2U);
    }
    bool Write16(std::uint32_t address, std::uint16_t value) override {
        return Store(address, &value, 2U);
    }
    bool Read8(std::uint32_t address, std::uint8_t* value) override {
        return Copy(address, value, 1U);
    }
    bool Write8(std::uint32_t address, std::uint8_t value) override {
        return Store(address, &value, 1U);
    }

    void Thumb(std::uint32_t address, std::initializer_list<std::uint16_t> code) {
        for (const std::uint16_t half : code) {
            Write16(address, half);
            address += 2U;
        }
    }

private:
    bool Copy(std::uint32_t address, void* value, std::uint32_t size) const {
        if (address > mBytes.size() - size) {
            return false;
        }
        std::memcpy(value, mBytes.data() + address, size);
        return true;
    }
    bool Store(std::uint32_t address, const void* value, std::uint32_t size) {
        if (address > mBytes.size() - size) {
            return false;
        }
        std::memcpy(mBytes.data() + address, value, size);
        return true;
    }

    std::array<std::uint8_t, 0x4000> mBytes{};
};

int gFailures = 0;

void Expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++gFailures;
    }
}

// Runs exactly `count` Thumb instructions from `pc` (odd).
ExecutionResult Run(ByteMemory& memory, GuestState& state, std::uint32_t pc,
                    std::uint32_t count) {
    return ExecuteThumbBlock(pc, state, memory, count);
}

constexpr std::uint32_t kNZCV = kFlagN | kFlagZ | kFlagC | kFlagV;

void TestInterworkingThroughDispatch() {
    // A32 0x1000: BLX #imm to Thumb 0x1010; 0x1004: SVC #0.
    const PackedOp blx{0xFA000002U, EncodeMetadata(Opcode::Branch, Condition::Al, Link)};
    const PackedOp svc{0xEF000000U, EncodeMetadata(Opcode::Svc, Condition::Al)};
    const std::array<Block, 2> blocks{{{0x1000U, &blx, 1U, false},
                                       {0x1004U, &svc, 1U, false}}};
    const BlockShard shard{0x1000U, 0x1004U, blocks.data(), 2U};
    const Registry registry{&shard, 1U, nullptr, 0U};

    ByteMemory memory;
    // Thumb 0x1010: MOVS r0,#5; ADDS r0,#3; BX lr
    memory.Thumb(0x1010U, {0x2005U, 0x3003U, 0x4770U});
    GuestState state{};
    const auto exit = Dispatch(registry, 0x1000U, state, memory);
    Expect(exit.kind == ExitKind::Svc && exit.pc == 0x1004U, "BLX #imm / BX lr returns to A32");
    Expect(state.r[0] == 8U, "Thumb arithmetic result");
    Expect(state.r[14] == 0x1004U, "BLX #imm link register");
    Expect((state.cpsr & kCpsrThumb) == 0U, "CPSR.T cleared back in A32");

    // BLX #imm with H=1 lands one halfword further: 0x1000+8+8+2.
    const PackedOp blxHalf{0xFB000002U, EncodeMetadata(Opcode::Branch, Condition::Al, Link)};
    const std::array<Block, 2> halfBlocks{{{0x1000U, &blxHalf, 1U, false},
                                           {0x1004U, &svc, 1U, false}}};
    const BlockShard halfShard{0x1000U, 0x1004U, halfBlocks.data(), 2U};
    const Registry halfRegistry{&halfShard, 1U, nullptr, 0U};
    memory.Thumb(0x1012U, {0x2009U, 0x4770U});  // MOVS r0,#9; BX lr
    state = {};
    Dispatch(halfRegistry, 0x1000U, state, memory);
    Expect(state.r[0] == 9U, "BLX #imm H bit adds a halfword");

    // Thumb caller 0x2100: BL 0x2000; BX r5 (A32 SVC at 0x1004).
    // Thumb callee 0x2000: PUSH {r4,lr}; sum 4+3+2+1 into r4; LDR r0,=lit;
    // ADDS r0,r0,r4; POP {r4,pc}.
    memory.Thumb(0x2000U, {0xB510U, 0x2400U, 0x2104U, 0x1864U, 0x3901U, 0xD1FCU,
                           0x4801U, 0x1900U, 0xBD10U, 0x0000U});
    memory.Write32(0x2014U, 0x12340000U);
    memory.Thumb(0x2100U, {0xF7FFU, 0xFF7EU, 0x4728U});
    state = {};
    state.r[4] = 0x77U;
    state.r[5] = 0x1004U;
    state.r[13] = 0x3000U;
    const auto call = Dispatch(registry, 0x2101U, state, memory);
    Expect(call.kind == ExitKind::Svc && call.pc == 0x1004U, "Thumb BL/POP {pc}/BX r5 reach A32");
    Expect(state.r[0] == 0x1234000AU, "LDR literal plus loop sum");
    Expect(state.r[4] == 0x77U && state.r[13] == 0x3000U, "PUSH/POP restore r4 and SP");
    Expect(state.r[14] == 0x2105U, "BL suffix link register is Thumb");

    // Thumb 0x2200: BLX 0x1004 (prefix + BLX suffix) switches to A32.
    memory.Thumb(0x2200U, {0xF7FEU, 0xEF00U});
    state = {};
    const auto blxArm = Dispatch(registry, 0x2201U, state, memory);
    Expect(blxArm.kind == ExitKind::Svc && blxArm.pc == 0x1004U, "Thumb BLX #imm reaches A32");
    Expect(state.r[14] == 0x2205U, "Thumb BLX #imm link register");
    Expect((state.cpsr & kCpsrThumb) == 0U, "CPSR.T cleared by Thumb BLX #imm");
}

void TestDataProcessing() {
    ByteMemory memory;
    GuestState state{};

    // LSRS r1,r0,#32 (imm5 0) and ASRS r2,r0,#32.
    memory.Thumb(0x100U, {0x0801U, 0x1002U});
    state.r[0] = 0x80000001U;
    Run(memory, state, 0x101U, 2U);
    Expect(state.r[1] == 0U && state.r[2] == 0xFFFFFFFFU, "LSR/ASR #32 results");
    Expect((state.cpsr & (kFlagN | kFlagC)) == (kFlagN | kFlagC), "ASR #32 flags");

    // LSLS r1,r0,#0 keeps C; MOVS clears Z/N; CMP r0,#1 -> Z,C.
    memory.Thumb(0x110U, {0x0001U, 0x2001U, 0x2801U});
    state = {};
    state.cpsr = kFlagC;
    state.r[0] = 1U;
    Run(memory, state, 0x111U, 3U);
    Expect((state.cpsr & kNZCV) == (kFlagZ | kFlagC), "LSL #0 keeps C; CMP equal sets Z,C");

    // NEGS r1,r0 (0x4241); ADCS r2,r0 with C; SBCS r3,r0 with C clear.
    memory.Thumb(0x120U, {0x4241U});
    state = {};
    state.r[0] = 5U;
    Run(memory, state, 0x121U, 1U);
    Expect(state.r[1] == 0xFFFFFFFBU && (state.cpsr & kNZCV) == kFlagN, "NEG result and flags");

    memory.Thumb(0x130U, {0x4142U, 0x4183U});  // ADCS r2,r0; SBCS r3,r0
    state = {};
    state.cpsr = kFlagC;
    state.r[0] = 1U;
    state.r[2] = 0xFFFFFFFFU;
    state.r[3] = 10U;
    Run(memory, state, 0x131U, 1U);
    Expect(state.r[2] == 1U && (state.cpsr & kFlagC) != 0U, "ADC with carry");
    state.cpsr = 0U;
    Run(memory, state, 0x133U, 1U);
    Expect(state.r[3] == 8U && (state.cpsr & kFlagC) != 0U, "SBC without carry");

    // CMP r0,r1 overflow: 0x80000000 - 1.
    memory.Thumb(0x140U, {0x4288U});
    state = {};
    state.r[0] = 0x80000000U;
    state.r[1] = 1U;
    Run(memory, state, 0x141U, 1U);
    Expect((state.cpsr & kNZCV) == (kFlagC | kFlagV), "CMP signed overflow");

    // MULS r0,r1 keeps C; RORS r2,r1 by 8; MVNS r3,r1; BICS r4,r1.
    memory.Thumb(0x150U, {0x4348U, 0x41CAU, 0x43CBU, 0x438CU});
    state = {};
    state.cpsr = kFlagC;
    state.r[0] = 3U;
    state.r[1] = 8U;
    state.r[2] = 0x12345678U;
    state.r[4] = 0xFFU;
    Run(memory, state, 0x151U, 4U);
    Expect(state.r[0] == 24U, "MUL result");
    Expect(state.r[2] == 0x78123456U, "ROR by register");
    Expect(state.r[3] == 0xFFFFFFF7U && state.r[4] == 0xF7U, "MVN and BIC");

    // High registers: MOV r8,r0; ADD r8,r1; CMP r8,r1; MOV pc,r8 stays Thumb.
    memory.Thumb(0x160U, {0x4680U, 0x4488U, 0x4588U, 0x46C7U});
    state = {};
    state.r[0] = 2U;
    state.r[1] = 0x200U;
    const auto mov = Run(memory, state, 0x161U, 8U);
    Expect(state.r[8] == 0x202U, "high-register MOV/ADD");
    Expect(mov.kind == ExitKind::Branch && mov.pc == 0x203U, "MOV pc stays Thumb");

    // v6 extend and reverse forms.
    memory.Thumb(0x170U, {0xB201U, 0xB242U, 0xB283U, 0xB2C4U, 0xBA05U, 0xBA46U, 0xBAC7U});
    state = {};
    state.r[0] = 0x12348180U;
    Run(memory, state, 0x171U, 7U);
    Expect(state.r[1] == 0xFFFF8180U && state.r[2] == 0xFFFFFF80U, "SXTH/SXTB");
    Expect(state.r[3] == 0x8180U && state.r[4] == 0x80U, "UXTH/UXTB");
    Expect(state.r[5] == 0x80813412U && state.r[6] == 0x34128081U && state.r[7] == 0xFFFF8081U,
           "REV/REV16/REVSH");
}

void TestMemory() {
    ByteMemory memory;
    GuestState state{};
    memory.Write32(0x800U, 0x80FF7F01U);
    // LDRSB r1,[r0,r2]; LDRSH r3,[r0,r2]; LDRB r4,[r0,#3]; LDRH r5,[r0,#2];
    // STRH r1,[r0,#4]; STR r0,[sp,#4]; ADD r6,sp,#8; ADR r7,+4.
    memory.Thumb(0x200U, {0x5681U, 0x5E83U, 0x78C4U, 0x8845U, 0x8081U, 0x9001U, 0xAE02U, 0xA701U});
    state.r[0] = 0x800U;
    state.r[2] = 3U;
    state.r[13] = 0x900U;
    Run(memory, state, 0x201U, 8U);
    Expect(state.r[1] == 0xFFFFFF80U, "LDRSB");
    Expect(state.r[4] == 0x80U && state.r[5] == 0x80FFU, "LDRB/LDRH immediate");
    std::uint32_t word = 0U;
    memory.Read32(0x804U, &word);
    Expect(word == 0xFF80U, "STRH stores the low halfword");
    memory.Read32(0x904U, &word);
    Expect(word == 0x800U, "STR SP-relative");
    Expect(state.r[6] == 0x908U && state.r[7] == 0x214U, "ADD SP / ADR");

    // STMIA r0!,{r1,r2}; LDMIA r3!,{r4,r5}.
    memory.Thumb(0x300U, {0xC006U, 0xCB30U});
    state = {};
    state.r[0] = 0xA00U;
    state.r[1] = 0x11U;
    state.r[2] = 0x22U;
    state.r[3] = 0xA00U;
    Run(memory, state, 0x301U, 2U);
    Expect(state.r[0] == 0xA08U && state.r[3] == 0xA08U, "STMIA/LDMIA writeback");
    Expect(state.r[4] == 0x11U && state.r[5] == 0x22U, "LDMIA values");

    // A load outside memory faults at the instruction, PC odd.
    memory.Thumb(0x320U, {0x6800U});  // LDR r0,[r0]
    state = {};
    state.r[0] = 0x10000U;
    const auto fault = Run(memory, state, 0x321U, 1U);
    Expect(fault.kind == ExitKind::MemoryFault && fault.pc == 0x321U && fault.detail == 0x10000U,
           "memory fault reports instruction and address");

    // SVC and BKPT are reported, not executed.
    memory.Thumb(0x340U, {0xDF01U, 0xBE00U});
    state = {};
    const auto svc = Run(memory, state, 0x341U, 1U);
    Expect(svc.kind == ExitKind::Unsupported && svc.pc == 0x341U && svc.detail == 0xDF01U,
           "Thumb SVC is unsupported");
    const auto bkpt = Run(memory, state, 0x343U, 1U);
    Expect(bkpt.kind == ExitKind::Unsupported && bkpt.detail == 0xBE00U, "BKPT is unsupported");

    // Conditional branch not taken falls through; limit returns next PC.
    memory.Thumb(0x360U, {0xD0FEU, 0x2001U});  // BEQ self; MOVS r0,#1
    state = {};
    const auto limit = Run(memory, state, 0x361U, 2U);
    Expect(limit.kind == ExitKind::Branch && limit.pc == 0x365U && state.r[0] == 1U,
           "untaken branch and instruction limit");
}

}  // namespace

int main() {
    TestInterworkingThroughDispatch();
    TestDataProcessing();
    TestMemory();
    if (gFailures != 0) {
        std::cerr << gFailures << " Thumb check(s) failed\n";
        return 1;
    }
    std::cout << "a32 thumb tests passed\n";
    return 0;
}
