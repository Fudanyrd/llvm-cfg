//===-- FuncCallPass.cpp - record called function -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each basic block, record the called function and its address in a global
// array. The array is put into a section named __sancov_func.
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

#include <cstring>
#include <iostream>
#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {

class FuncCallPass : public PassInfoMixin<FuncCallPass> {
 public:
  FuncCallPass() {
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  static bool isRequired() {
    return true;
  }

 protected:
 private:
  GlobalVariable *FunctionCallArray;  // for function calls.
  Type           *PtrTy;
};

}  // namespace llvm

using namespace llvm;

static const char *section = "__sancov_func";

static inline bool StrRefStartsWith(const StringRef &str, const char *prefix) {
  const size_t len = strlen(prefix);
  if (str.size() < len) return false;

  for (size_t i = 0; i < len; i++) {
    if (str[i] != prefix[i]) return false;
  }

  return true;
}

static inline bool isLLVMIntrinsicFn(const StringRef &str) {
  return StrRefStartsWith(str, "llvm.");
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

static void BuildCallGraph(Function &F, Module &M,
                           std::vector<Constant *> &calls) {
  if (isLLVMIntrinsicFn(F.getName())) {
    // Skip LLVM intrinsic functions.
    return;
  }

  auto *PtrTy = PointerType::get(Type::getVoidTy(M.getContext()), 0);
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

  for (auto &block : F) {
    uset.parent[&block] = nullptr;  // initialize each block's parent to nullptr
    bool      hasGuard = false;
    Constant *guard = GetSancovPcGuardArg(block, M, &hasGuard);
    hasSancovGuard[&block] = std::make_pair(hasGuard, guard);
  }

  for (auto &block : F) {
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

  for (auto &BB : F) {
    BasicBlock *rootBlock = uset.root(&BB);
    Constant   *guard = hasSancovGuard[rootBlock].second;
    // assert (guard && "guard is nullptr");
    if (!guard) {
      std::cerr << "\033[01;31m[!]\033[0;m Found empty block in function "
                << F.getName().str() << std::endl;
      continue;
    }
    for (auto &I : BB) {
      if (auto *CB = dyn_cast<CallBase>(&I)) {
        Function *Callee = CB->getCalledFunction();
        if (!Callee) continue;

        if (isLLVMIntrinsicFn(Callee->getName())) {
          // Skip LLVM intrinsic functions.
          continue;
        }
        std::string name = Callee->getName().str();
        if (name == "__sanitizer_cov_trace_pc_guard" ||
            name == "__sanitizer_cov_trace_pc_guard_init" ||
            name == "__sanitizer_cov_pcs_init")
          continue;

        Constant *src = guard;
        // if (!src) continue;

        // cast function to its address
        IRBuilder<> irb(&I);
        auto        funcPtr = irb.CreatePointerCast(Callee, PtrTy);
        calls.push_back(src);
        calls.push_back((Constant *)funcPtr);
      }
    }
  }
}

PreservedAnalyses FuncCallPass::run(Module &mod, ModuleAnalysisManager &MAM) {
  std::vector<Constant *> init_vals;
  for (auto &func : mod) {
    if (func.isDeclaration()) continue;
    BuildCallGraph(func, mod, init_vals);
  }

  PtrTy = PointerType::get(Type::getVoidTy(mod.getContext()), 0);
  Triple triple = Triple(mod.getTargetTriple());

  auto *ArrayTy = ArrayType::get(PtrTy, init_vals.size());
  FunctionCallArray =
      new GlobalVariable(mod, ArrayTy, false, GlobalVariable::PrivateLinkage,
                         Constant::getNullValue(ArrayTy), "__func_calls");
  FunctionCallArray->setInitializer(ConstantArray::get(ArrayTy, init_vals));
  FunctionCallArray->setSection(section);
  FunctionCallArray->setConstant(true);
  FunctionCallArray->setAlignment(
      Align(mod.getDataLayout().getTypeStoreSize(PtrTy).getFixedValue()));

  appendToUsed(mod, ArrayRef<GlobalValue *>({FunctionCallArray}));
  appendToCompilerUsed(mod, ArrayRef<GlobalValue *>({FunctionCallArray}));

  return PreservedAnalyses::all();
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "func-call", "v0.1",
          /* lambda to insert our pass into the pass pipeline. */
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL
#if LLVM_VERSION_MAJOR >= 20
                   ,
                   ThinOrFullLTOPhase Phase
#endif

                ) { MPM.addPass(FuncCallPass()); });
          }};
}
