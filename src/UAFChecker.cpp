#include "UAFChecker.h"

namespace llvm {

UAFChecker::UAFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos)
    : Checker(funcInfos) {}

bool UAFChecker::isUseAfterFree(Instruction *inst) {
  bool useAfterFree = false;

  DFSOptions options;
  options.terminationCondition = [&useAfterFree](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto* currInst = dyn_cast<Instruction>(curr);

    if (currInst->getOpcode() == Instruction::Store) {
      useAfterFree = !(isa<ConstantPointerNull>(currInst->getOperand(0)));
      return true;
    }
    if (currInst->getOpcode() == Instruction::Call) {
      auto *call = dyn_cast<CallInst>(currInst);
      Function *calledFunc = call->getCalledFunction();
      useAfterFree = calledFunc->getName() != "free";
      return true;
    }
    return false ;
  };

  DFSContext context{AnalyzerMap::ForwardDependencyMap, inst, options};
  DFSResult result = DFS(context);
  return useAfterFree;
}

std::pair<Value *, Instruction *> UAFChecker::Check(Function* function) {
  FuncInfo* funcInfo = funcInfos[function].get();

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
        auto* currInst = dyn_cast<Instruction>(curr);

        if (currInst != free && HasPath(AnalyzerMap::BackwardDependencyMap, currInst, malloc)) {
          useAfterFree = currInst;
          return true;
        }
        return false;
      };

      DFSContext context{AnalyzerMap::ForwardFlowMap, free, options};
      DFSResult result = DFS(context);
      if (useAfterFree && isUseAfterFree(useAfterFree)) {
        return {free, useAfterFree};
      }
    }
  }
  return {};
}

} // namespace llvm