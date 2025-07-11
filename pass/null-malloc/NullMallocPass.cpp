//===-- NullMallocPass.cpp - inject out-of-memory error -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Inject out-of-memory error by replacing malloc, realloc, calloc and
// reallocarray to out own allocator, which randomly returns nullptr.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

namespace llvm {

  class NullMallocPass : public PassInfoMixin<NullMallocPass> {
  public:
    NullMallocPass(void) {
      prefix = "cfg_";
    }

    PreservedAnalyses run(Module &mod, ModuleAnalysisManager &MAM);
    static bool isRequired() {
      return true;
    }

  protected:
  private:
    std::string prefix;

    PointerType *PtrTy;
    IntegerType *uSizeType{nullptr};
    FunctionType *typeOfMalloc{nullptr};
    FunctionType *typeOfCalloc{nullptr};
    FunctionType *typeOfRealloc{nullptr};
    FunctionType *typeOfReallocArray{nullptr};
  };

} // namespace llvm

using namespace llvm;

PreservedAnalyses
NullMallocPass::run(Module &mod, ModuleAnalysisManager &MAM) {
  PtrTy = PointerType::get(Type::getVoidTy(mod.getContext()), 0);
  uSizeType = IntegerType::get(mod.getContext(), mod.getDataLayout().getPointerSizeInBits());
  /// void *cfg_malloc(size_t size);
  typeOfMalloc = FunctionType::get(PtrTy, {uSizeType}, false);
  /// void *cfg_calloc(size_t nmemb, size_t size);
  typeOfCalloc = FunctionType::get(PtrTy, {uSizeType, uSizeType}, false);
  /// void *cfg_realloc(void *ptr, size_t size)
  typeOfRealloc = FunctionType::get(PtrTy, {PtrTy, uSizeType}, false);
  /// void *cfg_reallocarray(void *ptr, size_t, size_t)
  typeOfReallocArray = FunctionType::get(PtrTy, {PtrTy, uSizeType, uSizeType}, false);

  std::unordered_map<std::string, Value *> nullFunctions;

  // insert cfg_malloc declaration
  Value *cfgMalloc = mod.getOrInsertFunction("cfg_malloc", typeOfMalloc)
                        .getCallee();
  Value *cfgCalloc = mod.getOrInsertFunction("cfg_calloc", typeOfCalloc)
                        .getCallee();
  Value *cfgRealloc = mod.getOrInsertFunction("cfg_realloc", typeOfRealloc)
                         .getCallee();
  Value *cfgReallocArr = mod.getOrInsertFunction("cfg_reallocarray", typeOfReallocArray)
                            .getCallee();
  nullFunctions["malloc"] = cfgMalloc;
  nullFunctions["calloc"] = cfgCalloc;
  nullFunctions["realloc"] = cfgRealloc;
  nullFunctions["reallocarray"] = cfgReallocArr;

  for (Function &f : mod) {
    for (BasicBlock &bb : f) {
      for (Instruction &i : bb) {
        if (CallBase *cb = dyn_cast<CallBase>(&i)) {
          Function *Callee = cb->getCalledFunction();
          if (!Callee)
            continue;
          const std::string calleeName = Callee->getName().str();
          auto ptr = nullFunctions.find(calleeName);
          if (ptr != nullFunctions.end()) {
            Function *fobj = dyn_cast<Function>(ptr->second);
            assert(fobj && "fobj is not a fuction");
            cb->setCalledFunction(fobj);
          }
        }
      }
    }
  }

  auto PA = PreservedAnalyses::all();
  return PA;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "null-malloc", "v0.1",
          /* lambda to insert our pass into the pass pipeline. */
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL
#if LLVM_VERSION_MAJOR >= 20
                   ,
                   ThinOrFullLTOPhase Phase
#endif

                )
                { MPM.addPass(NullMallocPass()); });
          }};
}
