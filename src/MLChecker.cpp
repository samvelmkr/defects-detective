#include "MLChecker.h"

namespace llvm {

MLChecker::MLChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}

bool MLChecker::hasMallocFreePath(MallocedObject *obj, Instruction *free) {
  return false;
}

bool MLChecker::hasMallocFreePathWithOffset(MallocedObject *Obj, Instruction *freeInst) {
  return false;
}

std::pair<Instruction *, Instruction *> MLChecker::checkFreeExistence(std::vector<Instruction *> &path) {
  return std::pair<Instruction *, Instruction *>();
}

std::pair<Instruction *, Instruction *> MLChecker::Check() {
  auto mallocCalls = funcInfo->getCalls(CallInstruction::Malloc);

  for (Instruction* malloc : mallocCalls) {
    funcInfo->CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);
  }

  errs() << "NUM of PATHs" << allMallocRetPaths.size() << "\n";
  for (const auto &path : allMallocRetPaths) {
    for (auto &inst : path) {
      errs() << *inst << "\n\t|\n";
    }
    errs() << "\n";
  }

  return {};
}

} // namespace llvm