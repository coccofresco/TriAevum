#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

namespace {

bool IsEightHexDigits(llvm::StringRef Name) {
    if (Name.size() != 8) {
        return false;
    }
    return llvm::all_of(Name, [](char Character) {
        return llvm::isHexDigit(Character);
    });
}

bool IsTargetStorageName(llvm::StringRef Name) {
    if (Name.consume_front("DAT_")) {
        return IsEightHexDigits(Name);
    }
    size_t Separator = Name.rfind('_');
    return Name.starts_with("PTR_") && Separator != llvm::StringRef::npos &&
           IsEightHexDigits(Name.substr(Separator + 1));
}

llvm::GlobalVariable* TargetGlobalForPointer(llvm::Value* Pointer) {
    Pointer = Pointer->stripPointerCasts();
    auto* Global = llvm::dyn_cast<llvm::GlobalVariable>(Pointer);
    if (Global == nullptr || !IsTargetStorageName(Global->getName()) ||
        !Global->getValueType()->isPointerTy()) {
        return nullptr;
    }
    return Global;
}

bool IsGuestAddressIntegerImpl(
    llvm::Value* Value,
    llvm::SmallPtrSetImpl<llvm::Value*>& Visited) {
    if (!Value->getType()->isIntegerTy()) return false;
    if (Value->getType()->getIntegerBitWidth() <= 32) return true;
    if (!Visited.insert(Value).second) return false;

    if (auto* Extension = llvm::dyn_cast<llvm::ZExtInst>(Value)) {
        return IsGuestAddressIntegerImpl(Extension->getOperand(0), Visited);
    }
    if (auto* Binary = llvm::dyn_cast<llvm::BinaryOperator>(Value)) {
        if (Binary->getOpcode() == llvm::Instruction::And) {
            for (llvm::Value* Operand : Binary->operands()) {
                auto* Mask = llvm::dyn_cast<llvm::ConstantInt>(Operand);
                if (Mask != nullptr && Mask->getValue().getActiveBits() <= 32) {
                    return true;
                }
            }
        }
    }
    if (auto* Phi = llvm::dyn_cast<llvm::PHINode>(Value)) {
        return llvm::all_of(Phi->incoming_values(), [&](llvm::Value* Incoming) {
            return IsGuestAddressIntegerImpl(Incoming, Visited);
        });
    }
    if (auto* Select = llvm::dyn_cast<llvm::SelectInst>(Value)) {
        return IsGuestAddressIntegerImpl(Select->getTrueValue(), Visited) &&
               IsGuestAddressIntegerImpl(Select->getFalseValue(), Visited);
    }
    return false;
}

bool IsGuestAddressInteger(llvm::Value* Value) {
    llvm::SmallPtrSet<llvm::Value*, 8> Visited;
    return IsGuestAddressIntegerImpl(Value, Visited);
}

llvm::Value* GuestAddressForIndirectCallee(llvm::Value* Callee) {
    Callee = Callee->stripPointerCasts();
    auto* IntToPointer = llvm::dyn_cast<llvm::IntToPtrInst>(Callee);
    if (IntToPointer == nullptr) return nullptr;
    llvm::Value* Address = IntToPointer->getOperand(0);
    if (auto* Extension = llvm::dyn_cast<llvm::ZExtInst>(Address)) {
        Address = Extension->getOperand(0);
    }
    return Address->getType()->isIntegerTy(32) ? Address : nullptr;
}

llvm::CallBase* TargetResolverCall(llvm::Value* Callee) {
    Callee = Callee->stripPointerCasts();
    auto* ResolverCall = llvm::dyn_cast<llvm::CallBase>(Callee);
    if (ResolverCall == nullptr) return nullptr;
    llvm::Function* Resolver = ResolverCall->getCalledFunction();
    return Resolver != nullptr &&
                   Resolver->getName() == "oot3d_host_resolve_target_function"
               ? ResolverCall
               : nullptr;
}

bool IsResolvedTargetCallee(llvm::Value* Callee) {
    return TargetResolverCall(Callee) != nullptr;
}

bool NeedsTargetResolution(llvm::CallBase& Call) {
    return Call.getCalledFunction() == nullptr &&
           !IsResolvedTargetCallee(Call.getCalledOperand());
}

bool IsTargetStorageAddressImpl(llvm::Value* Value,
                                llvm::SmallPtrSetImpl<llvm::Value*>& Visited) {
    Value = Value->stripPointerCasts();
    if (!Visited.insert(Value).second) {
        return true;
    }
    if (TargetGlobalForPointer(Value) != nullptr) {
        return true;
    }
    if (auto* Phi = llvm::dyn_cast<llvm::PHINode>(Value)) {
        return llvm::all_of(Phi->incoming_values(), [&](llvm::Value* Incoming) {
            return IsTargetStorageAddressImpl(Incoming, Visited);
        });
    }
    if (auto* Select = llvm::dyn_cast<llvm::SelectInst>(Value)) {
        return IsTargetStorageAddressImpl(Select->getTrueValue(), Visited) &&
               IsTargetStorageAddressImpl(Select->getFalseValue(), Visited);
    }
    return false;
}

bool IsTargetStorageAddress(llvm::Value* Value) {
    llvm::SmallPtrSet<llvm::Value*, 8> Visited;
    return IsTargetStorageAddressImpl(Value, Visited);
}

bool IsGuestMemoryAddressImpl(llvm::Value* Value,
                              llvm::SmallPtrSetImpl<llvm::Value*>& Visited) {
    if (!Visited.insert(Value).second) return false;
    if (IsTargetStorageAddress(Value)) return true;
    if (auto* IntToPointer = llvm::dyn_cast<llvm::IntToPtrInst>(Value)) {
        llvm::Value* Address = IntToPointer->getOperand(0);
        return IsGuestAddressInteger(Address);
    }
    if (auto* Expression = llvm::dyn_cast<llvm::ConstantExpr>(Value)) {
        if (Expression->getOpcode() == llvm::Instruction::IntToPtr)
            return IsGuestAddressInteger(Expression->getOperand(0));
    }
    // stripPointerCasts() also strips inttoptr. Inspect that operation first,
    // otherwise an ordinary `(T*)(uint32_t)guestAddress` loses the only proof
    // that its storage is 32-bit guest memory and survives as a host-width
    // pointer load/store.
    llvm::Value* Stripped = Value->stripPointerCasts();
    if (Stripped != Value)
        return IsGuestMemoryAddressImpl(Stripped, Visited);
    if (auto* Gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Value))
        return IsGuestMemoryAddressImpl(Gep->getPointerOperand(), Visited);
    if (auto* Phi = llvm::dyn_cast<llvm::PHINode>(Value))
        return llvm::all_of(Phi->incoming_values(), [&](llvm::Value* Incoming) {
            return IsGuestMemoryAddressImpl(Incoming, Visited);
        });
    if (auto* Select = llvm::dyn_cast<llvm::SelectInst>(Value))
        return IsGuestMemoryAddressImpl(Select->getTrueValue(), Visited) &&
               IsGuestMemoryAddressImpl(Select->getFalseValue(), Visited);
    return false;
}

bool IsGuestMemoryAddress(llvm::Value* Value) {
    llvm::SmallPtrSet<llvm::Value*, 8> Visited;
    return IsGuestMemoryAddressImpl(Value, Visited);
}

class Oot3dGuestStorageLegalizerPass final
    : public llvm::PassInfoMixin<Oot3dGuestStorageLegalizerPass> {
  public:
    llvm::PreservedAnalyses run(llvm::Module& Module,
                                llvm::ModuleAnalysisManager&) {
        llvm::SmallVector<llvm::LoadInst*> Loads;
        llvm::SmallVector<llvm::StoreInst*> Stores;
        llvm::SmallVector<llvm::CallBase*> IndirectCalls;
        for (llvm::Function& Function : Module) {
            for (llvm::BasicBlock& Block : Function) {
                for (llvm::Instruction& Instruction : Block) {
                    if (auto* Load = llvm::dyn_cast<llvm::LoadInst>(&Instruction)) {
                        if (IsGuestMemoryAddress(Load->getPointerOperand()) &&
                            Load->getType()->isPointerTy()) {
                            Loads.push_back(Load);
                        }
                    } else if (auto* Store =
                                   llvm::dyn_cast<llvm::StoreInst>(&Instruction)) {
                        if (IsGuestMemoryAddress(Store->getPointerOperand()) &&
                            Store->getValueOperand()->getType()->isPointerTy()) {
                            Stores.push_back(Store);
                        }
                    } else if (auto* Call = llvm::dyn_cast<llvm::CallBase>(&Instruction)) {
                        if (NeedsTargetResolution(*Call)) IndirectCalls.push_back(Call);
                    }
                }
            }
        }

        llvm::Type* GuestWord = llvm::Type::getInt32Ty(Module.getContext());
        llvm::Type* HostWord = llvm::Type::getInt64Ty(Module.getContext());
        llvm::FunctionCallee Resolver = Module.getOrInsertFunction(
            "oot3d_host_resolve_target_function",
            llvm::PointerType::getUnqual(Module.getContext()), HostWord);
        for (auto* Call : IndirectCalls) {
            llvm::IRBuilder<> Builder(Call);
            llvm::Value* Address = Builder.CreatePtrToInt(
                Call->getCalledOperand(), HostWord, "indirect_target_address");
            llvm::Value* Native = Builder.CreateCall(
                Resolver, {Address}, "native_target_function");
            Call->setCalledOperand(Native);
        }
        for (llvm::LoadInst* Load : Loads) {
            llvm::IRBuilder<> Builder(Load);
            auto* GuestLoad = Builder.CreateLoad(
                GuestWord, Load->getPointerOperand(), Load->getName() + ".guest32");
            GuestLoad->setVolatile(Load->isVolatile());
            GuestLoad->setAlignment(llvm::Align(4));
            if (Load->isAtomic()) {
                GuestLoad->setAtomic(Load->getOrdering(), Load->getSyncScopeID());
            }
            GuestLoad->copyMetadata(*Load);
            llvm::Value* HostPointer = Builder.CreateIntToPtr(
                GuestLoad, Load->getType(), Load->getName() + ".hostptr");
            Load->replaceAllUsesWith(HostPointer);
            Load->eraseFromParent();
        }
        for (llvm::StoreInst* Store : Stores) {
            llvm::IRBuilder<> Builder(Store);
            llvm::Value* GuestPointer = Builder.CreatePtrToInt(
                Store->getValueOperand(), GuestWord, "guestptr32");
            auto* GuestStore = Builder.CreateStore(
                GuestPointer, Store->getPointerOperand(), Store->isVolatile());
            GuestStore->setAlignment(llvm::Align(4));
            if (Store->isAtomic()) {
                GuestStore->setAtomic(Store->getOrdering(), Store->getSyncScopeID());
            }
            GuestStore->copyMetadata(*Store);
            Store->eraseFromParent();
        }

        // Pointer loads become inttoptr only during the legalization above.
        // Route indirect calls exposed by that rewrite in the same pass.
        llvm::SmallVector<llvm::CallBase*> ExposedCalls;
        for (llvm::Function& Function : Module) {
            for (llvm::BasicBlock& Block : Function) {
                for (llvm::Instruction& Instruction : Block) {
                    auto* Call = llvm::dyn_cast<llvm::CallBase>(&Instruction);
                    if (Call != nullptr && NeedsTargetResolution(*Call))
                        ExposedCalls.push_back(Call);
                }
            }
        }
        for (auto* Call : ExposedCalls) {
            llvm::IRBuilder<> Builder(Call);
            llvm::Value* Address = Builder.CreatePtrToInt(
                Call->getCalledOperand(), HostWord, "indirect_target_address");
            llvm::Value* Native = Builder.CreateCall(
                Resolver, {Address}, "native_target_function");
            Call->setCalledOperand(Native);
        }

        return Loads.empty() && Stores.empty() && IndirectCalls.empty() &&
                       ExposedCalls.empty()
                   ? llvm::PreservedAnalyses::all()
                   : llvm::PreservedAnalyses::none();
    }
};

} // namespace

static unsigned ReportUnsupportedStorageUses(llvm::Module& Module) {
    unsigned Count = 0;
    for (llvm::Function& Function : Module) {
        for (llvm::BasicBlock& Block : Function) {
            for (llvm::Instruction& Instruction : Block) {
                for (llvm::Use& Operand : Instruction.operands()) {
                    if (!IsTargetStorageAddress(Operand.get())) {
                        continue;
                    }
                    bool Supported = false;
                    if (auto* Load = llvm::dyn_cast<llvm::LoadInst>(&Instruction)) {
                        Supported = Operand.getOperandNo() ==
                                        llvm::LoadInst::getPointerOperandIndex() &&
                                    Load->getType()->isIntegerTy(32);
                    } else if (auto* Store =
                                   llvm::dyn_cast<llvm::StoreInst>(&Instruction)) {
                        Supported = Operand.getOperandNo() ==
                                        llvm::StoreInst::getPointerOperandIndex() &&
                                    Store->getValueOperand()->getType()->isIntegerTy(32);
                    } else if (llvm::isa<llvm::PHINode, llvm::SelectInst,
                                         llvm::CastInst>(&Instruction)) {
                        Supported = true;
                    }
                    if (!Supported) {
                        if (Count < 20) {
                            llvm::errs() << "unsupported pointer32 storage use: "
                                         << Instruction << '\n';
                        }
                        ++Count;
                    }
                }
            }
        }
    }
    return Count;
}

static unsigned ReportNestedTargetResolution(llvm::Module& Module) {
    unsigned Count = 0;
    for (llvm::Function& Function : Module) {
        for (llvm::BasicBlock& Block : Function) {
            for (llvm::Instruction& Instruction : Block) {
                auto* TargetCall = llvm::dyn_cast<llvm::CallBase>(&Instruction);
                if (TargetCall == nullptr) continue;
                llvm::CallBase* Resolver =
                    TargetResolverCall(TargetCall->getCalledOperand());
                if (Resolver == nullptr || Resolver->arg_empty()) continue;
                llvm::Value* Input = Resolver->getArgOperand(0);
                if (auto* PointerToInt = llvm::dyn_cast<llvm::PtrToIntInst>(Input))
                    Input = PointerToInt->getPointerOperand();
                if (TargetResolverCall(Input) == nullptr) continue;
                if (Count < 20)
                    llvm::errs() << "nested target resolution: " << *TargetCall
                                 << '\n';
                ++Count;
            }
        }
    }
    return Count;
}

int main(int ArgumentCount, char** Arguments) {
    llvm::cl::opt<std::string> Input(
        llvm::cl::Positional, llvm::cl::Required, llvm::cl::desc("<input bitcode>"));
    llvm::cl::opt<std::string> Output(
        "o", llvm::cl::Required, llvm::cl::desc("Output bitcode"));
    llvm::cl::opt<bool> Strict(
        "strict", llvm::cl::desc("Reject pointer storage uses not legalized"));
    llvm::cl::ParseCommandLineOptions(ArgumentCount, Arguments);

    llvm::LLVMContext Context;
    llvm::SMDiagnostic Diagnostic;
    std::unique_ptr<llvm::Module> Module = llvm::parseIRFile(Input, Diagnostic, Context);
    if (Module == nullptr) {
        Diagnostic.print(Arguments[0], llvm::errs());
        return 1;
    }
    llvm::ModuleAnalysisManager AnalysisManager;
    Oot3dGuestStorageLegalizerPass Pass;
    Pass.run(*Module, AnalysisManager);
    unsigned Unsupported = ReportUnsupportedStorageUses(*Module);
    if (Unsupported != 0) {
        llvm::errs() << "unsupported pointer32 storage uses: " << Unsupported << '\n';
        if (Strict) {
            return 2;
        }
    }
    unsigned NestedResolution = ReportNestedTargetResolution(*Module);
    if (NestedResolution != 0) {
        llvm::errs() << "nested target resolutions: " << NestedResolution << '\n';
        if (Strict) {
            return 3;
        }
    }

    std::error_code Error;
    llvm::ToolOutputFile File(Output, Error, llvm::sys::fs::OF_None);
    if (Error) {
        llvm::errs() << "cannot open output: " << Error.message() << '\n';
        return 1;
    }
    llvm::WriteBitcodeToFile(*Module, File.os());
    File.keep();
    return 0;
}
