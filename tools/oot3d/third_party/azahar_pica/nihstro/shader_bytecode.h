// Copyright 2014 Tony Wasserka
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of the owner nor the names of its contributors may
//       be used to endorse or promote products derived from this software
//       without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <sstream>

#include "bit_field.h"

namespace nihstro {

enum class RegisterType {
    Input,
    Output,
    Temporary,
    FloatUniform,
    IntUniform,
    BoolUniform,
    Address,
    ConditionalCode,
    Unknown
};

static std::string GetRegisterName(RegisterType type) {
    switch (type) {
    case RegisterType::Input:           return "v";
    case RegisterType::Output:          return "o";
    case RegisterType::Temporary:       return "r";
    case RegisterType::FloatUniform:    return "c";
    case RegisterType::IntUniform:      return "i";
    case RegisterType::BoolUniform:     return "b";
    case RegisterType::ConditionalCode: return "cc";
    case RegisterType::Unknown:         return "u";
    default:                            return "";
    }
}

struct SourceRegister {
    SourceRegister() = default;

    SourceRegister(uint32_t value) {
        this->value = value;
    }

    RegisterType GetRegisterType() const {
        if (value < 0x10)
            return RegisterType::Input;
        else if (value < 0x20)
            return RegisterType::Temporary;
        else
            return RegisterType::FloatUniform;
    }

    int GetIndex() const {
        if (GetRegisterType() == RegisterType::Input)
            return value;
        else if (GetRegisterType() == RegisterType::Temporary)
            return value - 0x10;
        else if (GetRegisterType() == RegisterType::FloatUniform)
            return value - 0x20;
    }

    static const SourceRegister FromTypeAndIndex(RegisterType type, int index) {
        SourceRegister reg;
        if (type == RegisterType::Input)
            reg.value = index;
        else if (type == RegisterType::Temporary)
            reg.value = index + 0x10;
        else if (type == RegisterType::FloatUniform)
            reg.value = index + 0x20;
        else {
            // TODO: Should throw an exception or something.
        }
        return reg;
    }

    static const SourceRegister MakeInput(int index) {
        return FromTypeAndIndex(RegisterType::Input, index);
    }

    static const SourceRegister MakeTemporary(int index) {
        return FromTypeAndIndex(RegisterType::Temporary, index);
    }

    static const SourceRegister MakeFloat(int index) {
        return FromTypeAndIndex(RegisterType::FloatUniform, index);
    }

    std::string GetName() const {
        std::stringstream ss;
        ss << GetRegisterName(GetRegisterType()) << GetIndex();
        return ss.str();
    }

    operator uint32_t() const {
        return value;
    }

    template<typename T>
    decltype(uint32_t{} - T{}) operator -(const T& oth) const {
        return value - oth;
    }

    template<typename T>
    decltype(uint32_t{} & T{}) operator &(const T& oth) const {
        return value & oth;
    }

    uint32_t operator &(const SourceRegister& oth) const {
        return value & oth.value;
    }

    uint32_t operator ~() const {
        return ~value;
    }

private:
    uint32_t value;
};

struct DestRegister {
    DestRegister() = default;

    DestRegister(uint32_t value) {
        this->value = value;
    }

    RegisterType GetRegisterType() const {
        if (value < 0x10)
            return RegisterType::Output;
        else
            return RegisterType::Temporary;
    }

    int GetIndex() const {
        if (GetRegisterType() == RegisterType::Output)
            return value;
        else if (GetRegisterType() == RegisterType::Temporary)
            return value - 0x10;
        else // if (GetRegisterType() == RegisterType::FloatUniform)
            // TODO: This will lead to negative returned values...
            return value - 0x20;
    }

    static const DestRegister FromTypeAndIndex(RegisterType type, int index) {
        DestRegister reg;
        if (type == RegisterType::Output)
            reg.value = index;
        else if (type == RegisterType::Temporary)
            reg.value = index + 0x10;
        else if (type == RegisterType::FloatUniform) // TODO: Wait what? These shouldn't be writable..
            reg.value = index + 0x20;
        else {
            // TODO: Should throw an exception or something.
        }
        return reg;
    }

    static const DestRegister MakeOutput(int index) {
        return FromTypeAndIndex(RegisterType::Output, index);
    }

    static const DestRegister MakeTemporary(int index) {
        return FromTypeAndIndex(RegisterType::Temporary, index);
    }

    std::string GetName() const {
        std::stringstream ss;
        ss << GetRegisterName(GetRegisterType()) << GetIndex();
        return ss.str();
    }

    operator uint32_t() const {
        return value;
    }

    template<typename T>
    decltype(uint32_t{} - T{}) operator -(const T& oth) const {
        return value - oth;
    }

    template<typename T>
    decltype(uint32_t{} & T{}) operator &(const T& oth) const {
        return value & oth;
    }

    uint32_t operator &(const DestRegister& oth) const {
        return value & oth.value;
    }

    uint32_t operator ~() const {
        return ~value;
    }

private:
    uint32_t value;
};

struct OpCode {
    enum class Id : uint32_t {
        ADD     = 0x00,
        DP3     = 0x01,
        DP4     = 0x02,
        DPH     = 0x03,   // Dot product of Vec4 and Vec3; the Vec3 is made into
                          // a Vec4 by appending 1.0 as the fourth component
        DST     = 0x04,   // Distance, same as in vs_3_0
        EX2     = 0x05,   // Base-2 exponential
        LG2     = 0x06,   // Base-2 logarithm
        LIT     = 0x07,   // Clamp for lighting
        MUL     = 0x08,
        SGE     = 0x09,   // Set to 1.0 if SRC1 is greater or equal to SRC2
        SLT     = 0x0A,   // Set to 1.0 if SRC1 is less than SRC2
        FLR     = 0x0B,
        MAX     = 0x0C,
        MIN     = 0x0D,
        RCP     = 0x0E,   // Reciprocal
        RSQ     = 0x0F,   // Reciprocal of square root

        MOVA    = 0x12,   // Move to Address Register
        MOV     = 0x13,

        DPHI    = 0x18,
        DSTI    = 0x19,
        SGEI    = 0x1A,
        SLTI    = 0x1B,

        BREAK   = 0x20,
        NOP     = 0x21,
        END     = 0x22,
        BREAKC  = 0x23,
        CALL    = 0x24,
        CALLC   = 0x25,
        CALLU   = 0x26,
        IFU     = 0x27,
        IFC     = 0x28,
        LOOP    = 0x29,
        EMIT    = 0x2A,
        SETEMIT = 0x2B,
        JMPC    = 0x2C,
        JMPU    = 0x2D,
        CMP     = 0x2E, // LSB opcode bit ignored

        // lower 3 opcode bits ignored for these
        MADI    = 0x30,
        MAD     = 0x38, // lower 3 opcode bits ignored

        // Pseudo-instructions, used internally by the assembler
        PSEUDO_INSTRUCTION_START = 0x40,

        GEN_IF = PSEUDO_INSTRUCTION_START, // Generic IF (IFC or IFU)
        ELSE,
        ENDIF,
        GEN_CALL,       // Generic CALL (CALL, CALC, or CALLU)
        GEN_JMP,        // Generic JMP (JMPC or JMPU)
        //RET,          // Return from function (not supported yet)
        ENDLOOP,
    };

    enum class Type {
        Trivial,            // 3dbrew format 0
        Arithmetic,         // 3dbrew format 1
        Conditional,        // 3dbrew format 2
        UniformFlowControl, // 3dbrew format 3
        SetEmit,            // 3dbrew format 4
        MultiplyAdd,        // 3dbrew format 5
        Unknown
    };

    struct Info {
        Type type;

        // Arithmetic
        enum : uint32_t {
            OpDesc      = 1,
            Src1        = 2,
            Src2        = 4,
            Idx         = 8,
            Dest        = 16,
            SrcInversed = 32,
            CompareOps  = 64,
            MOVA        = 128 | OpDesc | Src1 | Idx,
            OneArgument = OpDesc | Src1 | Idx | Dest,
            TwoArguments = OneArgument | Src2,
            Compare = OpDesc | Idx | Src1 | Src2 | CompareOps,
        };

        // Flow Control
        enum : uint32_t {
            HasUniformIndex = 1,
            HasCondition    = 2,
            HasExplicitDest = 4,   // target code given explicitly and context-independently (contrary to e.g. BREAKC)
            HasFinishPoint  = 8,   // last instruction until returning to caller
            HasAlternative  = 16,  // has an "else" branch
            LOOP            = 32,

            BREAKC          = HasCondition,

            JMP             = HasExplicitDest,
            JMPC            = JMP | HasCondition,
            JMPU            = JMP | HasUniformIndex,

            CALL            = JMP | HasFinishPoint,
            CALLC           = CALL | HasCondition,
            CALLU           = CALL | HasUniformIndex,
            IFU             = CALLU | HasAlternative,
            IFC             = CALLC | HasAlternative,
        };

        enum : uint32_t {
            FullAndBool,
            SimpleAndInt,
        };

        uint32_t subtype;

        const char* name;

        // TODO: Deprecate.
        size_t NumArguments() const {
            if (type == Type::Arithmetic) {
                if (subtype & Src2)
                    return 3;
                else if (subtype & Src1)
                    return 2;
            }

            return 0;
        }
    };

    OpCode() = default;

    OpCode(Id value) {
        this->value = static_cast<uint32_t>(value);
    }

    OpCode(uint32_t value) {
        this->value = value;
    }

    Id EffectiveOpCode() const {
        uint32_t op = static_cast<uint32_t>(value);
        if (static_cast<Id>(op & ~0x7) == Id::MAD)
            return Id::MAD;
        else if (static_cast<Id>(op & ~0x7) == Id::MADI)
            return Id::MADI;
        else if (static_cast<Id>(op & ~0x1) == Id::CMP)
            return Id::CMP;
        else
            return static_cast<Id>(value);
    }

    const Info& GetInfo() const {
        #define unknown_instruction { OpCode::Type::Unknown, 0, "UNK" }
        static const OpCode::Info info_table[] =  {
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "add" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "dp3" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "dp4" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "dph" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "dst" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "exp" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "log" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "lit" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "mul" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "sge" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "slt" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "flr" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "max" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments, "min" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "rcp" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "rsq" },
            unknown_instruction,
            unknown_instruction,
            { OpCode::Type::Arithmetic, OpCode::Info::MOVA, "mova" },
            { OpCode::Type::Arithmetic, OpCode::Info::OneArgument, "mov" },
            unknown_instruction,
            unknown_instruction,
            unknown_instruction,
            unknown_instruction,
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments | OpCode::Info::SrcInversed, "dphi" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments | OpCode::Info::SrcInversed, "dsti" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments | OpCode::Info::SrcInversed, "sgei" },
            { OpCode::Type::Arithmetic, OpCode::Info::TwoArguments | OpCode::Info::SrcInversed, "slti" },
            unknown_instruction,
            unknown_instruction,
            unknown_instruction,
            unknown_instruction,
            { OpCode::Type::Trivial, 0, "break" },
            { OpCode::Type::Trivial, 0, "nop" },
            { OpCode::Type::Trivial, 0, "end" },
            { OpCode::Type::Conditional, OpCode::Info::BREAKC, "breakc" },
            { OpCode::Type::Conditional, OpCode::Info::CALL, "call" },
            { OpCode::Type::Conditional, OpCode::Info::CALLC, "callc" },
            { OpCode::Type::UniformFlowControl, OpCode::Info::CALLU, "callu" },
            { OpCode::Type::UniformFlowControl, OpCode::Info::IFU, "ifu" },
            { OpCode::Type::Conditional, OpCode::Info::IFC, "ifc" },
            { OpCode::Type::UniformFlowControl, OpCode::Info::LOOP, "loop" },
            { OpCode::Type::Trivial, 0, "emit" },
            { OpCode::Type::SetEmit, 0, "setemit" },
            { OpCode::Type::Conditional, OpCode::Info::JMPC, "jmpc" },
            { OpCode::Type::Conditional, OpCode::Info::JMPU, "jmpu" },
            { OpCode::Type::Arithmetic, OpCode::Info::Compare, "cmp" },
            { OpCode::Type::Arithmetic, OpCode::Info::Compare, "cmp" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, OpCode::Info::SrcInversed, "madi" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" },
            { OpCode::Type::MultiplyAdd, 0, "mad" }
        };
        #undef unknown_instruction
        return info_table[value];
    }

    operator Id() const {
        return static_cast<Id>(value);
    }

    OpCode operator << (size_t bits) const {
        return value << bits;
    }

    template<typename T>
    decltype(uint32_t{} - T{}) operator -(const T& oth) const {
        return value - oth;
    }

    uint32_t operator &(const OpCode& oth) const {
        return value & oth.value;
    }

    uint32_t operator ~() const {
        return ~value;
    }

private:
    uint32_t value;
};

} // namespace nihstro

namespace std {
    template<>
    struct make_unsigned<nihstro::SourceRegister> {
        using type = nihstro::SourceRegister;
    };

    template<>
    struct make_unsigned<nihstro::DestRegister> {
        using type = nihstro::DestRegister;
    };

    template<>
    struct make_unsigned<nihstro::OpCode> {
        using type = nihstro::OpCode;
    };
}

namespace nihstro {

#pragma pack(1)
union Instruction {
    Instruction& operator =(const Instruction& instr) {
        hex = instr.hex;
        return *this;
    }

    uint32_t hex;

    BitField<0x1a, 0x6, OpCode> opcode;


    // General notes:
    //
    // When two input registers are used, one of them uses a 5-bit index while the other
    // one uses a 7-bit index. This is because at most one floating point uniform may be used
    // as an input.


    // Format used e.g. by arithmetic instructions and comparisons
    union Common { // TODO: Remove name
        BitField<0x00, 0x7, uint32_t> operand_desc_id;

        const SourceRegister GetSrc1(bool is_inverted) const {
            if (!is_inverted) {
                return src1;
            } else {
                return src1i;
            }
        }

        const SourceRegister GetSrc2(bool is_inverted) const {
            if (!is_inverted) {
                return src2;
            } else {
                return src2i;
            }
        }

        /**
         * Source inputs may be reordered for certain instructions.
         * Use GetSrc1 and GetSrc2 instead to access the input register indices hence.
         */
        BitField<0x07, 0x5, SourceRegister> src2;
        BitField<0x0c, 0x7, SourceRegister> src1;
        BitField<0x07, 0x7, SourceRegister> src2i;
        BitField<0x0e, 0x5, SourceRegister> src1i;

        // Address register value is used for relative addressing of src1 / src2 (inverted)
        BitField<0x13, 0x2, uint32_t> address_register_index;

        union CompareOpType {  // TODO: Make nameless once MSVC supports it
            enum Op : uint32_t {
                Equal        = 0,
                NotEqual     = 1,
                LessThan     = 2,
                LessEqual    = 3,
                GreaterThan  = 4,
                GreaterEqual = 5,
                Unk6         = 6,
                Unk7         = 7
            };

            BitField<0x15, 0x3, Op> y;
            BitField<0x18, 0x3, Op> x;

            const std::string ToString(Op op) const {
                switch (op) {
                case Equal:        return "==";
                case NotEqual:     return "!=";
                case LessThan:     return "<";
                case LessEqual:    return "<=";
                case GreaterThan:  return ">";
                case GreaterEqual: return ">=";
                case Unk6:         return "UNK6";
                case Unk7:         return "UNK7";
                default:           return "";
                };
            }
        } compare_op;

        std::string AddressRegisterName() const {
            if (address_register_index == 0) return "";
            else if (address_register_index == 1) return "a0.x";
            else if (address_register_index == 2) return "a0.y";
            else /*if (address_register_index == 3)*/ return "aL";
        }

        BitField<0x15, 0x5, DestRegister> dest;
    } common;

    union FlowControlType {  // TODO: Make nameless once MSVC supports it
        enum Op : uint32_t {
            Or    = 0,
            And   = 1,
            JustX = 2,
            JustY = 3
        };

        BitField<0x00, 0x8, uint32_t> num_instructions;
        BitField<0x0a, 0xc, uint32_t> dest_offset;

        BitField<0x16, 0x2, Op> op;
        BitField<0x16, 0x4, uint32_t> bool_uniform_id;
        BitField<0x16, 0x2, uint32_t> int_uniform_id; // TODO: Verify that only this many bits are used...

        BitFlag<0x18, uint32_t> refy;
        BitFlag<0x19, uint32_t> refx;
    } flow_control;

    union {
        const SourceRegister GetSrc1(bool is_inverted) const {
            // The inverted form for src1 is the same, this function is just here for consistency
            return src1;
        }

        const SourceRegister GetSrc2(bool is_inverted) const {
            if (!is_inverted) {
                return src2;
            } else {
                return src2i;
            }
        }

        const SourceRegister GetSrc3(bool is_inverted) const {
            if (!is_inverted) {
                return src3;
            } else {
                return src3i;
            }
        }

        BitField<0x00, 0x5, uint32_t> operand_desc_id;

        BitField<0x05, 0x5, SourceRegister> src3;
        BitField<0x0a, 0x7, SourceRegister> src2;
        BitField<0x11, 0x5, SourceRegister> src1;

        BitField<0x05, 0x7, SourceRegister> src3i;
        BitField<0x0c, 0x5, SourceRegister> src2i;

        // Address register value is used for relative addressing of src2 / src3 (inverted)
        BitField<0x16, 0x2, uint32_t> address_register_index;

        std::string AddressRegisterName() const {
            if (address_register_index == 0) return "";
            else if (address_register_index == 1) return "a0.x";
            else if (address_register_index == 2) return "a0.y";
            else /*if (address_register_index == 3)*/ return "aL";
        }

        BitField<0x18, 0x5, DestRegister> dest;
    } mad;

    union {
        BitField<0x16, 1, uint32_t> winding;
        BitField<0x17, 1, uint32_t> prim_emit;
        BitField<0x18, 2, uint32_t> vertex_id;
    } setemit;
};
static_assert(sizeof(Instruction) == 0x4, "Incorrect structure size");
static_assert(std::is_standard_layout<Instruction>::value, "Structure does not have standard layout");

union SwizzlePattern {
    SwizzlePattern& operator =(const SwizzlePattern& instr) {
        hex = instr.hex;
        return *this;
    }

    uint32_t hex;

    enum class Selector : uint32_t {
        x = 0,
        y = 1,
        z = 2,
        w = 3
    };

    /**
     * Gets the raw 8-bit selector for the specified (1-indexed) source register.
     */
    unsigned GetRawSelector(unsigned src) const {
        if (src == 0 || src > 3)
            throw std::out_of_range("src needs to be between 1 and 3");

        unsigned selectors[] = {
            src1_selector, src2_selector, src3_selector
        };
        return selectors[src - 1];
    }

    Selector GetSelectorSrc1(int comp) const {
        Selector selectors[] = {
            src1_selector_0, src1_selector_1, src1_selector_2, src1_selector_3
        };
        return selectors[comp];
    }

    Selector GetSelectorSrc2(int comp) const {
        Selector selectors[] = {
            src2_selector_0, src2_selector_1, src2_selector_2, src2_selector_3
        };
        return selectors[comp];
    }

    Selector GetSelectorSrc3(int comp) const {
        Selector selectors[] = {
            src3_selector_0, src3_selector_1, src3_selector_2, src3_selector_3
        };
        return selectors[comp];
    }

    void SetSelectorSrc1(int comp, Selector value) {
        if (comp == 0)
            src1_selector_0 = value;
        else if (comp == 1)
            src1_selector_1 = value;
        else if (comp == 2)
            src1_selector_2 = value;
        else if (comp == 3)
            src1_selector_3 = value;
        else
            throw std::out_of_range("comp needs to be smaller than 4");
    }

    void SetSelectorSrc2(int comp, Selector value) {
        if (comp == 0)
            src2_selector_0 = value;
        else if (comp == 1)
            src2_selector_1 = value;
        else if (comp == 2)
            src2_selector_2 = value;
        else if (comp == 3)
            src2_selector_3 = value;
        else
            throw std::out_of_range("comp needs to be smaller than 4");
    }

    void SetSelectorSrc3(int comp, Selector value) {
        if (comp == 0)
            src3_selector_0 = value;
        else if (comp == 1)
            src3_selector_1 = value;
        else if (comp == 2)
            src3_selector_2 = value;
        else if (comp == 3)
            src3_selector_3 = value;
        else
            throw std::out_of_range("comp needs to be smaller than 4");
    }

    std::string SelectorToString(bool src2) const {
        std::map<Selector, std::string> map = {
            { Selector::x, "x" },
            { Selector::y, "y" },
            { Selector::z, "z" },
            { Selector::w, "w" }
        };
        std::string ret;
        for (int i = 0; i < 4; ++i) {
            ret += map.at(src2 ? GetSelectorSrc2(i) : GetSelectorSrc1(i));
        }
        return ret;
    }

    bool DestComponentEnabled(unsigned int i) const {
        return (dest_mask & (0x8 >> i)) != 0;
    }

    void SetDestComponentEnabled(unsigned int i, bool enabled) {
        int mask = 0xffff & (0x8 >> i);
        dest_mask = (dest_mask & ~mask) | (enabled * mask);
    }

    std::string DestMaskToString() const {
        std::string ret;
        for (int i = 0; i < 4; ++i) {
            if (!DestComponentEnabled(i))
                ret += "_";
            else
                ret += "xyzw"[i];
        }
        return ret;
    }

    // Components of "dest" that should be written to: LSB=dest.w, MSB=dest.x
    BitField< 0, 4, uint32_t> dest_mask;

    BitFlag < 4,    uint32_t> negate_src1;
    BitField< 5, 8, uint32_t> src1_selector;
    BitField< 5, 2, Selector> src1_selector_3;
    BitField< 7, 2, Selector> src1_selector_2;
    BitField< 9, 2, Selector> src1_selector_1;
    BitField<11, 2, Selector> src1_selector_0;

    BitFlag <13,    uint32_t> negate_src2;
    BitField<14, 8, uint32_t> src2_selector;
    BitField<14, 2, Selector> src2_selector_3;
    BitField<16, 2, Selector> src2_selector_2;
    BitField<18, 2, Selector> src2_selector_1;
    BitField<20, 2, Selector> src2_selector_0;

    BitFlag <22,    uint32_t> negate_src3;
    BitField<23, 8, uint32_t> src3_selector;
    BitField<23, 2, Selector> src3_selector_3;
    BitField<25, 2, Selector> src3_selector_2;
    BitField<27, 2, Selector> src3_selector_1;
    BitField<29, 2, Selector> src3_selector_0;
};
static_assert(sizeof(SwizzlePattern) == 0x4, "Incorrect structure size");


#pragma pack()

} // namespace
