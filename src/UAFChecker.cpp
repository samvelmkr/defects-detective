#include "UAFChecker.h"

namespace llvm {

UAFChecker::UAFChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}

bool UAFChecker::isUseAfterFree(Instruction *inst) {
  bool useAfterFree = false;
  funcInfo->DFS(AnalyzerMap::ForwardDependencyMap, inst, [&useAfterFree](Instruction* curr){
    if (curr->getOpcode() == Instruction::Store) {
      useAfterFree = !(isa<ConstantPointerNull>(curr->getOperand(0)));
      return true;
    }
    if(curr->getOpcode() == Instruction::Call) {
      auto* call = dyn_cast<CallInst>(curr);
      Function* calledFunc = call->getCalledFunction();
      useAfterFree = calledFunc->getName() != "free";
      return true;
    }
    return false;
  });

  return useAfterFree;
}

std::pair<Instruction *, Instruction *> UAFChecker::Check() {
  Instruction *useAfterFree = nullptr;

  for (auto& obj : funcInfo->mallocedObjs) {
    Instruction* malloc = obj.first;
    std::vector<Instruction*> freeInsts = obj.second->getFreeCalls();
    for (auto* free : freeInsts) {
      bool foundUsageAfterFree =
          funcInfo->DFS(AnalyzerMap::ForwardFlowMap,
                        free,
                        [malloc, free, &useAfterFree, this](Instruction *curr) {
                          if (curr != free && funcInfo->HasPath(AnalyzerMap::BackwardDependencyMap, curr, malloc)) {
                            useAfterFree = curr;
                            return true;
                          }
                          return false;
                        });

      if (!foundUsageAfterFree) {
        continue;
      }

      if (useAfterFree && isUseAfterFree(useAfterFree)) {
        return {free, useAfterFree};
      }
    }
  }
  return {};
}

} // namespace llvm