//===-- FuncEntryPass.cpp - map function address to sancov pc -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each function, insert a record into a global array at the entry block.
// The array is put into a section named __sancov_entries.
//
// Example usage with clang-20:
// clang -Xclang -fpass-plugin=/path/to/func-call.so -S -emit-llvm main.ll \
//  -o main2.ll
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/SanitizerCoverage.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/PostDominators.h"
// #include "llvm/IR/CallBase.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
// #include "llvm/IR/EHPersonalities.h"
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

#include <cstring>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {

class FuncEntryPass : public PassInfoMixin<FuncEntryPass> {
 public:
  FuncEntryPass() {
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool       isRequired() {
    return true;
  }

 protected:
 private:
  GlobalVariable *FunctionEntryArray;  // for function entries.
  Type           *PtrTy;

  Constant *GetFuncEntry(Module &mod, Function &f);
};

}  // namespace llvm

using namespace llvm;
using std::vector;

static const char *section = "__sancov_entries";
static inline bool StrRefStartsWith(const StringRef &str, const char *prefix) {
  const size_t len = strlen(prefix);
  if (str.size() < len) return false;

  for (size_t i = 0; i < len; i++) {
    if (str[i] != prefix[i]) return false;
  }

  return true;
}

static Constant *GetSancovPcGuardArg(BasicBlock &BB, Module &mod,
                                     bool *success = nullptr) {
  for (auto &I : BB) {
    if (auto *CB = dyn_cast<CallBase>(&I)) {
      Function *Callee = CB->getCalledFunction();
      if (!Callee) continue;
      /* StringRef::starts_with is not defined in llvm-15. */
      // Callee->getName().starts_with("__sanitizer_cov_trace_pc_guard")
      const std::string calleeName = Callee->getName().str();
      if (calleeName == "__sanitizer_cov_trace_pc_guard" ||
          calleeName == "__sanitizer_cov_trace_pc") {
        if (success) { *success = true; }
        return cast<Constant>(CB->getArgOperand(0));
      }
    }
  }

  if (success) { *success = false; }
  // do not create a nullptr because it will be overwrite.
  /* return Constant::getNullValue(
      PointerType::get(Type::getVoidTy(mod.getContext()), 0)); */
  return nullptr;
}

static inline bool isLLVMIntrinsicFn(const StringRef &str) {
  return StrRefStartsWith(str, "llvm.");
}

Constant *FuncEntryPass::GetFuncEntry(Module &mod, Function &f) {
  /* Constant *nullPtr = Constant::getNullValue(
      PointerType::get(Type::getVoidTy(mod.getContext()), 0)); */
  if (isLLVMIntrinsicFn(f.getName())) {
    // Skip LLVM intrinsic functions.
    return nullptr;
  }

  std::unordered_map<BasicBlock *, std::pair<bool, Constant *>> hasSancovGuard;
  struct UnionFindSet {
    std::unordered_map<BasicBlock *, BasicBlock *> parent;

    UnionFindSet() = default;
    ~UnionFindSet() = default;

    BasicBlock *root(BasicBlock *query) {
      auto ptr = parent.find(query);
      assert(ptr != parent.end() && "not found");

      if (ptr->second == nullptr) {
        return query;  // root
      } else {
        // path compression
        ptr->second = root(ptr->second);
        return ptr->second;
      }
    }

    void merge(BasicBlock *b1, BasicBlock *b2) {
      auto *r1 = root(b1);
      assert(r1 && "r1 is nullptr");
      auto *r2 = root(b2);
      assert(r2 && "r2 is nullptr");

      if (r1 == r2) return;  // already in the same set

      parent[r1] = r2;  // merge r1 into r2
    }
  } uset;

  for (auto &block : f) {
    uset.parent[&block] = nullptr;  // initialize each block's parent to nullptr
    bool      hasGuard = false;
    Constant *guard = GetSancovPcGuardArg(block, mod, &hasGuard);
    hasSancovGuard[&block] = std::make_pair(hasGuard, guard);
  }

  for (auto &block : f) {
    if (!hasSancovGuard[&block].first) { continue; }

    Constant                *elem = hasSancovGuard[&block].second;
    std::stack<BasicBlock *> stk;
    stk.push(&block);

    // use depth-first search to find all
    // reachable successors of the current block
    while (!stk.empty()) {
      BasicBlock *cur = stk.top();
      stk.pop();

      for (auto *Succ : successors(cur)) {
        auto &dat = hasSancovGuard[Succ];
        if (!dat.first) {
          dat.first = true;
          dat.second = elem;  // inherit the guard from the parent block
          uset.merge(Succ, &block);

          stk.push(Succ);
        }
      }
    }
  }

  Constant *ret;
  for (auto &BB : f) {
    BasicBlock *entryBlock = &f.getEntryBlock();
    entryBlock = uset.root(entryBlock);
    const auto &meta = hasSancovGuard[entryBlock];
    if (!meta.first || meta.second == nullptr) {
      ret = nullptr;
    } else {
      ret = meta.second;  // return the guard
    }
    break;
  }
  return ret;
}

PreservedAnalyses FuncEntryPass::run(Module &mod, ModuleAnalysisManager &MAM) {
  PtrTy = PointerType::get(Type::getVoidTy(mod.getContext()), 0);
  Triple triple = Triple(mod.getTargetTriple());

  std::vector<Constant *> init_vals;
  for (auto &func : mod) {
    // auto &entryBlock = func.getEntryBlock();
    if (isLLVMIntrinsicFn(func.getName()) ||
        func.isDeclaration()) {
      // Skip LLVM intrinsic functions and declarations.
      continue;
    }
    IRBuilder<> irb(mod.getContext());
    auto        funcPtr = irb.CreatePointerCast(&func, PtrTy);
    Constant   *entryArg = this->GetFuncEntry(mod, func);

    // assert(entryArg != nullptr && "Entry argument is nullptr");
    if (entryArg == nullptr) {
      // If the entry argument is nullptr, we skip this function.
      continue;
    }
    init_vals.push_back((Constant *)funcPtr);
    init_vals.push_back(entryArg);
  }

  auto *ArrayTy = ArrayType::get(PtrTy, init_vals.size());
  FunctionEntryArray =
      new GlobalVariable(mod, ArrayTy, false, GlobalVariable::PrivateLinkage,
                         Constant::getNullValue(ArrayTy), "__func_entries");
  FunctionEntryArray->setInitializer(ConstantArray::get(ArrayTy, init_vals));
  FunctionEntryArray->setSection(section);
  FunctionEntryArray->setConstant(true);
  FunctionEntryArray->setAlignment(
      Align(mod.getDataLayout().getTypeStoreSize(PtrTy).getFixedValue()));

  appendToUsed(mod, ArrayRef<GlobalValue *>({FunctionEntryArray}));
  appendToCompilerUsed(mod, ArrayRef<GlobalValue *>({FunctionEntryArray}));

  auto PA = PreservedAnalyses::all();
  return PA;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "func-entry", "v0.1",
          /* lambda to insert our pass into the pass pipeline. */
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL
#if LLVM_VERSION_MAJOR >= 20
                   ,
                   ThinOrFullLTOPhase Phase
#endif

                ) { MPM.addPass(FuncEntryPass()); });
          }};
}
