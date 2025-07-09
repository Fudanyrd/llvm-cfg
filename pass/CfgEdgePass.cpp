//===-- CfgEdgePass.cpp - dump cfg edges ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Write all edges in control flow graph into the __sancov_cfg_edges section.
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

#include <sstream>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace llvm {

class CfgEdgePass : public PassInfoMixin<CfgEdgePass> {
 public:
  CfgEdgePass() {
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
  static bool       isRequired() {
    return true;
  }

 protected:
 private:
  GlobalVariable *CfgEdgeArray;  // for cfg edges.
  Type           *PtrTy;
  std::vector<GlobalValue *> CompilerUsed;  // for comdat.
  std::vector<GlobalValue *> Used;  // for comdat.
};

}  // namespace llvm

using namespace llvm;

static const char *section = "__sancov_cfg_edges";

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

static std::vector<Constant *> BuildCfg(Function &F, Module &M) {
  // std::vector<Constant *> basic_blocks;
  std::vector<Constant *> edges;

  // for (auto &BB : F) {
  //   basic_blocks.push_back(GetSancovPcGuardArg(BB));
  // }

  if (isLLVMIntrinsicFn(F.getName())) {
    // Skip LLVM intrinsic functions.
    return edges;
  }

  // when built with address sanitizer, some basic block
  // does not have an entry in the __sancov_guards section.

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

  for (auto iter = uset.parent.begin(); iter != uset.parent.end(); iter++) {
    if (iter->second) {
      // this is not a root node, skip.
      continue;
    }

    for (auto &block : F) {
      if (uset.root(&block) == iter->first) {
        for (auto *Succ : successors(&block)) {
          auto *child = uset.root(Succ);
          if (iter->first != child) {
            edges.push_back(hasSancovGuard[iter->first].second);
            edges.push_back(hasSancovGuard[child].second);
          }
        }
      }
    }
  }

  return edges;
}

PreservedAnalyses CfgEdgePass::run(Module &mod, ModuleAnalysisManager &MAM) {
  PtrTy = PointerType::get(Type::getVoidTy(mod.getContext()), 0);

  size_t func_cnt = 0;
  for (auto &func : mod) {
    if (func.isDeclaration()) continue;

    std::ostringstream oss;
    oss << "__cfg_edges_" << func_cnt;

    auto edges = BuildCfg(func, mod);
    if (edges.size() == 0) continue;

    auto *ArrayTy = ArrayType::get(PtrTy, edges.size());
    CfgEdgeArray =
        new GlobalVariable(mod, ArrayTy, false, GlobalVariable::PrivateLinkage,
                           Constant::getNullValue(ArrayTy), oss.str());

    Triple triple = Triple(mod.getTargetTriple());
    if (triple.supportsCOMDAT() &&
        (func.hasComdat() || triple.isOSBinFormatELF() ||
         !func.isInterposable())) {
      if (auto Comdat = getOrCreateFunctionComdat(func, triple)) {
        CfgEdgeArray->setComdat(Comdat);
      }
    }

    CfgEdgeArray->setInitializer(ConstantArray::get(ArrayTy, edges));
    CfgEdgeArray->setSection(section);
    CfgEdgeArray->setConstant(true);
    CfgEdgeArray->setAlignment(
        Align(mod.getDataLayout().getTypeStoreSize(PtrTy).getFixedValue()));

    // sancov_pcs parallels the other metadata section(s). Optimizers (e.g.
    // GlobalOpt/ConstantMerge) may not discard sancov_pcs and the other
    // section(s) as a unit, so we conservatively retain all unconditionally in
    // the compiler.
    //
    // With comdat (COFF/ELF), the linker can guarantee the associated sections
    // will be retained or discarded as a unit, so llvm.compiler.used is
    // sufficient. Otherwise, conservatively make all of them retained by the
    // linker.
    CompilerUsed.push_back(CfgEdgeArray);
    Used.push_back(CfgEdgeArray);

    func_cnt++;
  }

  appendToUsed(mod, ArrayRef<GlobalValue *>(Used));
  appendToCompilerUsed(mod, ArrayRef<GlobalValue *>(CompilerUsed));

  auto PA = PreservedAnalyses::all();
  return PA;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "cfg-edge", "v0.1",
          /* lambda to insert our pass into the pass pipeline. */
          [](PassBuilder &PB) {
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL
#if LLVM_VERSION_MAJOR >= 20
                   ,
                   ThinOrFullLTOPhase Phase
#endif

                ) { MPM.addPass(CfgEdgePass()); });
          }};
}
