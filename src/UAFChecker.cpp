#include "UAFChecker.h"

namespace llvm {

UAFChecker::UAFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos)
    : Checker(funcInfos) {}

Instruction *UAFChecker::FindUseAfterFree(Instruction *inst) {
  Instruction *useAfterFree = nullptr;

  DFSOptions options;
  options.terminationCondition = [&useAfterFree](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);
    errs() << "uaf curr: " << *currInst << "\n";
    if (currInst->getOpcode() == Instruction::Store) {
      if (!isa<ConstantPointerNull>(currInst->getOperand(0))) {
        useAfterFree = currInst;
        return true;
      }
    }
//    if (currInst->getOpcode() == Instruction::Call) {
//      auto *call = dyn_cast<CallInst>(currInst);
//      Function *calledFunc = call->getCalledFunction();
//      useAfterFree = calledFunc->getName() != "free";
//      return true;
//    }
    return false;
  };

  DFSContext context{AnalyzerMap::ForwardDependencyMap, inst, options};
  DFSResult result = DFS(context);
  return useAfterFree;
}

std::pair<Value *, Instruction *> UAFChecker::Check(Function *function) {
  FuncInfo *funcInfo = funcInfos[function].get();

  Instruction *useAfterFree = nullptr;

  for (auto &obj : funcInfo->mallocedObjs) {
    Instruction *malloc = obj.first;
    std::vector<Instruction *> freeInsts = obj.second->getFreeCalls();
    for (auto *free : freeInsts) {
      DFSOptions options;
      options.terminationCondition = [malloc, free, &useAfterFree, this](Value *curr) {
        if (!isa<Instruction>(curr)) {
          return false;
        }
        auto *currInst = dyn_cast<Instruction>(curr);
        errs() << "curr: " << *currInst << "\n";
        if (currInst->getOpcode() == Instruction::Store &&
            !isa<ConstantPointerNull>(currInst->getOperand(0))) {
          errs() << "\tStores that wothout nullptr: " << *currInst << "\n\n";
            if (HasPath(AnalyzerMap::ForwardDependencyMap, malloc, currInst)) {
              useAfterFree = currInst;
              return true;
            }
        }
        return false;
      };

      options.continueCondition = [malloc, this](Value* curr) {
        if (!isa<Instruction>(curr)) {
          return false;
        }
        auto *currInst = dyn_cast<Instruction>(curr);
        if (currInst->getOpcode() == Instruction::Store &&
            isa<ConstantPointerNull>(currInst->getOperand(0)) &&
            HasPath(AnalyzerMap::ForwardDependencyMap, malloc, currInst)) {
          return true;
        }
        return false;
      };

      DFSContext context{AnalyzerMap::ForwardFlowMap, free, options};
      DFSResult result = DFS(context);

      if (useAfterFree) {
//        if (auto *useAfterFree = FindUseAfterFree(useAfterFree))) {
          return {free, useAfterFree};
//        }
      }
    }
  }
  return {};
}

} // namespace llvm