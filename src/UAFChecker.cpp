#include "UAFChecker.h"

namespace llvm {

UAFChecker::UAFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos)
    : Checker(funcInfos) {}

bool UAFChecker::isUseAfterFree(Instruction *inst) {
  bool useAfterFree = false;

  DFSOptions options;
  options.terminationCondition = [&useAfterFree](Instruction *curr) {
    if (curr->getOpcode() == Instruction::Store) {
      useAfterFree = !(isa<ConstantPointerNull>(curr->getOperand(0)));
      return true;
    }
    if (curr->getOpcode() == Instruction::Call) {
      auto *call = dyn_cast<CallInst>(curr);
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

std::pair<Instruction *, Instruction *> UAFChecker::Check(Function* function) {
  FuncInfo* funcInfo = funcInfos[function].get();

  Instruction *useAfterFree = nullptr;

  for (auto &obj : funcInfo->mallocedObjs) {
    Instruction *malloc = obj.first;
    std::vector<Instruction *> freeInsts = obj.second->getFreeCalls();
    for (auto *free : freeInsts) {
      DFSOptions options;
      options.terminationCondition = [malloc, free, &useAfterFree, this](Instruction *curr) {
        if (curr != free && HasPath(AnalyzerMap::BackwardDependencyMap, curr, malloc)) {
          useAfterFree = curr;
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