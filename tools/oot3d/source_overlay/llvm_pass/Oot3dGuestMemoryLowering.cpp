#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <optional>
#include <string>

namespace {

llvm::cl::opt<std::string> InputPath(
    llvm::cl::Positional, llvm::cl::Required,
    llvm::cl::desc("<input LLVM IR or bitcode>"));
llvm::cl::opt<std::string> OutputPath(
    "o", llvm::cl::Required, llvm::cl::desc("Output bitcode path"));
llvm::cl::list<std::string> GuestPointerSpecs(
    "guest-pointer", llvm::cl::ZeroOrMore,
    llvm::cl::desc("Guest pointer argument as Function:Index"));
llvm::cl::list<std::string> GuestReturnSpecs(
    "guest-return", llvm::cl::ZeroOrMore,
    llvm::cl::desc("Function whose pointer return is a guest address"));
llvm::cl::opt<bool> Strict(
    "strict", llvm::cl::init(false),
    llvm::cl::desc("Reject unresolved guest memory operations"));

struct GuestPointerSpec {
    std::string Function;
    unsigned Index = 0;
};

std::optional<GuestPointerSpec> ParseGuestPointerSpec(llvm::StringRef Text) {
    const auto Parts = Text.rsplit(':');
    if (Parts.first.empty() || Parts.second.empty()) {
        return std::nullopt;
    }
    unsigned Index = 0;
    if (Parts.second.getAsInteger(10, Index)) {
        return std::nullopt;
    }
    return GuestPointerSpec{Parts.first.str(), Index};
}

bool IsGuestReturn(llvm::StringRef Name) {
    return llvm::any_of(GuestReturnSpecs, [&](const std::string& Candidate) {
        return Name == Candidate;
    });
}

class GuestProvenance {
  public:
    explicit GuestProvenance(llvm::Module& Module) : Module(Module) {}

    void Seed() {
        for (const std::string& Text : GuestPointerSpecs) {
            const auto Spec = ParseGuestPointerSpec(Text);
            if (!Spec.has_value()) {
                llvm::report_fatal_error(llvm::Twine(
                    "invalid --guest-pointer specification: ") + Text);
            }
            llvm::Function* Function = Module.getFunction(Spec->Function);
            if (Function == nullptr || Spec->Index >= Function->arg_size()) {
                llvm::report_fatal_error(
                    llvm::Twine("unknown guest pointer argument: ") + Text);
            }
            llvm::Argument* Argument = Function->getArg(Spec->Index);
            if (!Argument->getType()->isPointerTy()) {
                llvm::report_fatal_error(llvm::Twine(
                    "guest pointer argument is not a pointer: ") + Text);
            }
            GuestPointers.insert(Argument);
        }

        for (llvm::Function& Function : Module) {
            for (llvm::Instruction& Instruction : llvm::instructions(Function)) {
                if (auto* IntToPointer =
                        llvm::dyn_cast<llvm::IntToPtrInst>(&Instruction)) {
                    llvm::Type* Source =
                        IntToPointer->getOperand(0)->getType();
                    if (Source->isIntegerTy() &&
                        Source->getIntegerBitWidth() <= 32) {
                        GuestPointers.insert(IntToPointer);
                    }
                }
                auto* Call = llvm::dyn_cast<llvm::CallBase>(&Instruction);
                llvm::Function* Callee =
                    Call == nullptr ? nullptr : Call->getCalledFunction();
                if (Callee != nullptr && Call->getType()->isPointerTy() &&
                    IsGuestReturn(Callee->getName())) {
                    GuestPointers.insert(Call);
                }
            }
        }
    }

    void Solve() {
        bool Changed = true;
        while (Changed) {
            Changed = false;
            for (llvm::Function& Function : Module) {
                for (llvm::Instruction& Instruction :
                     llvm::instructions(Function)) {
                    Changed |= Propagate(Instruction);
                }
            }
        }
    }

    bool IsPointer(const llvm::Value* Value) const {
        return GuestPointers.contains(Value);
    }

    bool IsInteger(const llvm::Value* Value) const {
        return GuestIntegers.contains(Value);
    }

  private:
    bool MarkPointer(llvm::Value* Value) {
        return Value->getType()->isPointerTy() &&
               GuestPointers.insert(Value).second;
    }

    bool MarkInteger(llvm::Value* Value) {
        return Value->getType()->isIntegerTy() &&
               GuestIntegers.insert(Value).second;
    }

    bool AnyPointerOperand(const llvm::Instruction& Instruction) const {
        return llvm::any_of(Instruction.operands(), [&](const llvm::Use& Use) {
            return IsPointer(Use.get());
        });
    }

    bool AnyIntegerOperand(const llvm::Instruction& Instruction) const {
        return llvm::any_of(Instruction.operands(), [&](const llvm::Use& Use) {
            return IsInteger(Use.get());
        });
    }

    bool Propagate(llvm::Instruction& Instruction) {
        if (auto* Gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&Instruction)) {
            return IsPointer(Gep->getPointerOperand()) && MarkPointer(Gep);
        }
        if (auto* Cast = llvm::dyn_cast<llvm::CastInst>(&Instruction)) {
            if (Cast->getOpcode() == llvm::Instruction::PtrToInt) {
                return IsPointer(Cast->getOperand(0)) && MarkInteger(Cast);
            }
            if (Cast->getOpcode() == llvm::Instruction::IntToPtr) {
                return IsInteger(Cast->getOperand(0)) && MarkPointer(Cast);
            }
            if (Instruction.getType()->isPointerTy()) {
                return IsPointer(Cast->getOperand(0)) && MarkPointer(Cast);
            }
            if (Instruction.getType()->isIntegerTy()) {
                return IsInteger(Cast->getOperand(0)) && MarkInteger(Cast);
            }
        }
        if (auto* Phi = llvm::dyn_cast<llvm::PHINode>(&Instruction)) {
            if (Phi->getType()->isPointerTy() && AnyPointerOperand(*Phi)) {
                return MarkPointer(Phi);
            }
            if (Phi->getType()->isIntegerTy() && AnyIntegerOperand(*Phi)) {
                return MarkInteger(Phi);
            }
        }
        if (auto* Select = llvm::dyn_cast<llvm::SelectInst>(&Instruction)) {
            if (Select->getType()->isPointerTy() &&
                (IsPointer(Select->getTrueValue()) ||
                 IsPointer(Select->getFalseValue()))) {
                return MarkPointer(Select);
            }
            if (Select->getType()->isIntegerTy() &&
                (IsInteger(Select->getTrueValue()) ||
                 IsInteger(Select->getFalseValue()))) {
                return MarkInteger(Select);
            }
        }
        if (auto* Binary =
                llvm::dyn_cast<llvm::BinaryOperator>(&Instruction)) {
            return AnyIntegerOperand(*Binary) && MarkInteger(Binary);
        }
        if (auto* Load = llvm::dyn_cast<llvm::LoadInst>(&Instruction)) {
            return Load->getType()->isPointerTy() &&
                   IsPointer(Load->getPointerOperand()) && MarkPointer(Load);
        }
        if (auto* Call = llvm::dyn_cast<llvm::CallBase>(&Instruction)) {
            llvm::Function* Callee = Call->getCalledFunction();
            if (Callee != nullptr && Call->getType()->isPointerTy() &&
                IsGuestReturn(Callee->getName())) {
                return MarkPointer(Call);
            }
        }
        return false;
    }

    llvm::Module& Module;
    llvm::SmallPtrSet<llvm::Value*, 32> GuestPointers;
    llvm::SmallPtrSet<llvm::Value*, 32> GuestIntegers;
};

uint64_t FixedStoreSize(const llvm::DataLayout& Layout, llvm::Type* Type) {
    const llvm::TypeSize Size = Layout.getTypeStoreSize(Type);
    if (Size.isScalable()) {
        llvm::report_fatal_error("scalable guest memory type is unsupported");
    }
    return Size.getFixedValue();
}

llvm::Value* AsGuestAddress(llvm::IRBuilder<>& Builder, llvm::Value* Pointer) {
    llvm::Type* I32 = Builder.getInt32Ty();
    llvm::Value* Address = Builder.CreatePtrToInt(Pointer, I32,
                                                  "guest.address");
    return Address;
}

llvm::Value* AsSize64(llvm::IRBuilder<>& Builder, llvm::Value* Size) {
    if (!Size->getType()->isIntegerTy()) {
        llvm::report_fatal_error("memory intrinsic has non-integer size");
    }
    return Builder.CreateZExtOrTrunc(Size, Builder.getInt64Ty(),
                                    "guest.size");
}

llvm::AllocaInst* CreateEntryAlloca(llvm::Function& Function,
                                    llvm::Type* Type,
                                    const llvm::Twine& Name) {
    llvm::IRBuilder<> Builder(&*Function.getEntryBlock().getFirstInsertionPt());
    auto* Storage = Builder.CreateAlloca(Type, nullptr, Name);
    Storage->setAlignment(llvm::Align(1));
    return Storage;
}

class GuestMemoryLowering {
  public:
    GuestMemoryLowering(llvm::Module& Module, GuestProvenance& Provenance)
        : Module(Module), Provenance(Provenance), Layout(Module.getDataLayout()) {
        llvm::LLVMContext& Context = Module.getContext();
        llvm::Type* Void = llvm::Type::getVoidTy(Context);
        llvm::Type* I32 = llvm::Type::getInt32Ty(Context);
        llvm::Type* I64 = llvm::Type::getInt64Ty(Context);
        llvm::Type* Pointer = llvm::PointerType::getUnqual(Context);
        Read = Module.getOrInsertFunction(
            "oot3d_overlay_guest_read", Void, I32, Pointer, I64);
        Write = Module.getOrInsertFunction(
            "oot3d_overlay_guest_write", Void, I32, Pointer, I64);
        Copy = Module.getOrInsertFunction(
            "oot3d_overlay_guest_copy", Void, I32, I32, I64);
        Fill = Module.getOrInsertFunction(
            "oot3d_overlay_guest_fill", Void, I32, I32, I64);
    }

    void Run() {
        Collect();
        for (llvm::GetElementPtrInst* Gep : GuestGeps) {
            Gep->setIsInBounds(false);
        }
        for (llvm::LoadInst* Load : Loads) {
            LowerLoad(*Load);
        }
        for (llvm::StoreInst* Store : Stores) {
            LowerStore(*Store);
        }
        for (llvm::MemIntrinsic* Intrinsic : MemoryIntrinsics) {
            LowerMemoryIntrinsic(*Intrinsic);
        }
        if (Strict) {
            Audit();
        }
    }

  private:
    void Collect() {
        for (llvm::Function& Function : Module) {
            if (Function.isDeclaration()) {
                continue;
            }
            for (llvm::Instruction& Instruction :
                 llvm::instructions(Function)) {
                if (auto* Gep =
                        llvm::dyn_cast<llvm::GetElementPtrInst>(&Instruction)) {
                    if (Provenance.IsPointer(Gep)) {
                        GuestGeps.push_back(Gep);
                    }
                } else if (auto* Load =
                               llvm::dyn_cast<llvm::LoadInst>(&Instruction)) {
                    if (Provenance.IsPointer(Load->getPointerOperand())) {
                        if (Load->isAtomic()) {
                            llvm::report_fatal_error(
                                "atomic guest load is unsupported");
                        }
                        Loads.push_back(Load);
                    }
                } else if (auto* Store =
                               llvm::dyn_cast<llvm::StoreInst>(&Instruction)) {
                    if (Provenance.IsPointer(Store->getPointerOperand())) {
                        if (Store->isAtomic()) {
                            llvm::report_fatal_error(
                                "atomic guest store is unsupported");
                        }
                        Stores.push_back(Store);
                    }
                } else if (auto* Intrinsic =
                               llvm::dyn_cast<llvm::MemIntrinsic>(&Instruction)) {
                    const bool DestinationGuest =
                        Provenance.IsPointer(Intrinsic->getRawDest());
                    bool SourceGuest = false;
                    if (auto* Transfer =
                            llvm::dyn_cast<llvm::MemTransferInst>(Intrinsic)) {
                        SourceGuest =
                            Provenance.IsPointer(Transfer->getRawSource());
                    }
                    if (DestinationGuest || SourceGuest) {
                        MemoryIntrinsics.push_back(Intrinsic);
                    }
                }
            }
        }
    }

    void LowerLoad(llvm::LoadInst& LoadInstruction) {
        llvm::IRBuilder<> Builder(&LoadInstruction);
        llvm::Type* ValueType = LoadInstruction.getType();
        llvm::Function& Function = *LoadInstruction.getFunction();
        llvm::Value* Replacement = nullptr;
        if (ValueType->isPointerTy()) {
            llvm::AllocaInst* Storage = CreateEntryAlloca(
                Function, Builder.getInt32Ty(), LoadInstruction.getName() + ".guest32");
            Builder.CreateCall(Read,
                               {AsGuestAddress(Builder,
                                               LoadInstruction.getPointerOperand()),
                                Storage, Builder.getInt64(4)});
            llvm::Value* Word = Builder.CreateLoad(Builder.getInt32Ty(), Storage,
                                                   "guest.pointer.word");
            Replacement = Builder.CreateIntToPtr(Word, ValueType,
                                                  "guest.pointer");
        } else {
            const uint64_t Size = FixedStoreSize(Layout, ValueType);
            llvm::AllocaInst* Storage = CreateEntryAlloca(
                Function, ValueType, LoadInstruction.getName() + ".guest");
            Builder.CreateCall(Read,
                               {AsGuestAddress(Builder,
                                               LoadInstruction.getPointerOperand()),
                                Storage, Builder.getInt64(Size)});
            Replacement = Builder.CreateLoad(ValueType, Storage,
                                              LoadInstruction.getName() + ".value");
        }
        LoadInstruction.replaceAllUsesWith(Replacement);
        LoadInstruction.eraseFromParent();
    }

    void LowerStore(llvm::StoreInst& StoreInstruction) {
        llvm::IRBuilder<> Builder(&StoreInstruction);
        llvm::Value* Value = StoreInstruction.getValueOperand();
        llvm::Function& Function = *StoreInstruction.getFunction();
        if (Value->getType()->isPointerTy()) {
            llvm::AllocaInst* Storage = CreateEntryAlloca(
                Function, Builder.getInt32Ty(), "guest.store.pointer");
            llvm::Value* Word = Builder.CreatePtrToInt(
                Value, Builder.getInt32Ty(), "guest.pointer.word");
            Builder.CreateStore(Word, Storage)->setAlignment(llvm::Align(1));
            Builder.CreateCall(
                Write,
                {AsGuestAddress(Builder, StoreInstruction.getPointerOperand()),
                 Storage, Builder.getInt64(4)});
        } else {
            const uint64_t Size = FixedStoreSize(Layout, Value->getType());
            llvm::AllocaInst* Storage = CreateEntryAlloca(
                Function, Value->getType(), "guest.store");
            Builder.CreateStore(Value, Storage)->setAlignment(llvm::Align(1));
            Builder.CreateCall(
                Write,
                {AsGuestAddress(Builder, StoreInstruction.getPointerOperand()),
                 Storage, Builder.getInt64(Size)});
        }
        StoreInstruction.eraseFromParent();
    }

    void LowerMemoryIntrinsic(llvm::MemIntrinsic& Intrinsic) {
        llvm::IRBuilder<> Builder(&Intrinsic);
        llvm::Value* Destination = Intrinsic.getRawDest();
        const bool DestinationGuest = Provenance.IsPointer(Destination);
        llvm::Value* Size = AsSize64(Builder, Intrinsic.getLength());

        if (auto* Transfer = llvm::dyn_cast<llvm::MemTransferInst>(&Intrinsic)) {
            llvm::Value* Source = Transfer->getRawSource();
            const bool SourceGuest = Provenance.IsPointer(Source);
            if (DestinationGuest && SourceGuest) {
                Builder.CreateCall(
                    Copy, {AsGuestAddress(Builder, Destination),
                           AsGuestAddress(Builder, Source), Size});
            } else if (DestinationGuest) {
                Builder.CreateCall(
                    Write, {AsGuestAddress(Builder, Destination), Source, Size});
            } else if (SourceGuest) {
                Builder.CreateCall(
                    Read, {AsGuestAddress(Builder, Source), Destination, Size});
            }
        } else if (auto* Set = llvm::dyn_cast<llvm::MemSetInst>(&Intrinsic)) {
            if (DestinationGuest) {
                llvm::Value* Value = Builder.CreateZExtOrTrunc(
                    Set->getValue(), Builder.getInt32Ty(), "guest.fill.value");
                Builder.CreateCall(
                    Fill, {AsGuestAddress(Builder, Destination), Value, Size});
            }
        }
        Intrinsic.eraseFromParent();
    }

    void Audit() {
        for (llvm::Function& Function : Module) {
            for (llvm::Instruction& Instruction :
                 llvm::instructions(Function)) {
                llvm::Value* Pointer = nullptr;
                if (auto* Load = llvm::dyn_cast<llvm::LoadInst>(&Instruction)) {
                    Pointer = Load->getPointerOperand();
                } else if (auto* Store =
                               llvm::dyn_cast<llvm::StoreInst>(&Instruction)) {
                    Pointer = Store->getPointerOperand();
                }
                if (Pointer != nullptr && Provenance.IsPointer(Pointer)) {
                    llvm::errs() << "unlowered guest memory operation: "
                                 << Instruction << '\n';
                    llvm::report_fatal_error(
                        "strict guest memory lowering audit failed");
                }
            }
        }
    }

    llvm::Module& Module;
    GuestProvenance& Provenance;
    const llvm::DataLayout& Layout;
    llvm::FunctionCallee Read;
    llvm::FunctionCallee Write;
    llvm::FunctionCallee Copy;
    llvm::FunctionCallee Fill;
    llvm::SmallVector<llvm::GetElementPtrInst*> GuestGeps;
    llvm::SmallVector<llvm::LoadInst*> Loads;
    llvm::SmallVector<llvm::StoreInst*> Stores;
    llvm::SmallVector<llvm::MemIntrinsic*> MemoryIntrinsics;
};

} // namespace

int main(int ArgumentCount, char** Arguments) {
    llvm::cl::ParseCommandLineOptions(
        ArgumentCount, Arguments, "OOT3D guest memory lowering\n");

    llvm::LLVMContext Context;
    llvm::SMDiagnostic Diagnostic;
    std::unique_ptr<llvm::Module> Module =
        llvm::parseIRFile(InputPath, Diagnostic, Context);
    if (Module == nullptr) {
        Diagnostic.print(Arguments[0], llvm::errs());
        return 1;
    }

    GuestProvenance Provenance(*Module);
    Provenance.Seed();
    Provenance.Solve();
    GuestMemoryLowering(*Module, Provenance).Run();

    std::error_code Error;
    llvm::ToolOutputFile Output(OutputPath, Error, llvm::sys::fs::OF_None);
    if (Error) {
        llvm::errs() << "cannot open output '" << OutputPath
                     << "': " << Error.message() << '\n';
        return 1;
    }
    llvm::WriteBitcodeToFile(*Module, Output.os());
    Output.keep();
    return 0;
}
