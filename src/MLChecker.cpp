#include "MLChecker.h"

namespace llvm {

MLChecker::MLChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}

bool MLChecker::hasMallocFreePath(MallocedObject *obj, Instruction *free) {
  errs() << "stsart " << *free << "\n";
  return funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                       free,
      // Termination condition
                       [obj, free](Instruction *curr) {
                         if (curr == obj->getMallocCall()) {
                           obj->setFreeCall(free);
                           return true;
                         }
                         return false;
                       },
      // Continue condition
                       [](Instruction *curr) {
                         if (curr->getOpcode() == Instruction::GetElementPtr) {
                           return true;
                         }
                         return false;
                       }
  );
}

bool MLChecker::hasMallocFreePathWithOffset(MallocedObject *obj, Instruction *free) {
  auto getOffset = [](GetElementPtrInst *gep) {
    Module *m = gep->getModule();
    APInt accumulateOffset;
    if (gep->accumulateConstantOffset(m->getDataLayout(), accumulateOffset)) {
      return accumulateOffset.getZExtValue();
    }
    return SIZE_MAX;
  };

  bool reachedFirstGEP = false;
  bool reachedAlloca = false;
  bool reachedSecondGEP = false;
  return funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                       free,
                       [obj, &reachedFirstGEP, &reachedAlloca, &reachedSecondGEP, &getOffset, free](Instruction *curr) {
                         if (curr->getOpcode() == Instruction::GetElementPtr) {
                           auto *gep = dyn_cast<GetElementPtrInst>(curr);
                           if (obj->getOffset() == getOffset(gep)) {
                             reachedFirstGEP = true;
                           }
                         }
                         if (reachedFirstGEP && curr->getOpcode() == Instruction::Alloca) {
                           reachedAlloca = true;
                         }
                         if (reachedAlloca && curr->getOpcode() == Instruction::GetElementPtr) {
                           auto *gep = dyn_cast<GetElementPtrInst>(curr);
                           if (obj->getOffset() == getOffset(gep)) {
                             reachedSecondGEP = true;
                           }
                         }
                         if (reachedSecondGEP && curr == obj->getMallocCall()) {
                           obj->setFreeCall(free);
                           return true;
                         }
                         return false;
                       });
}

std::pair<Instruction *, Instruction *> MLChecker::checkFreeExistence(std::vector<Instruction *> &path) {
  Instruction *malloc = path[0];
  errs() << "MALLOC: " << *malloc << " | base: " << *(funcInfo->mallocedObjs[malloc]->getBaseInst()) << "\n";

  bool mallocWithOffset = funcInfo->mallocedObjs[malloc]->isMallocedWithOffset();
  bool foundMallocFreePath = false;

  for (Instruction *inst : path) {
    if (inst->getOpcode() == Instruction::Call && IsCallWithName(inst, CallInstruction::Free)) {
      if ((mallocWithOffset && hasMallocFreePathWithOffset(funcInfo->mallocedObjs[malloc].get(), inst))
          || (hasMallocFreePath(funcInfo->mallocedObjs[malloc].get(), inst))) {
        foundMallocFreePath = true;
        break;
      }
    }
  }

  errs() << "\tfound: " << foundMallocFreePath << "\n";

  if (!foundMallocFreePath) {
    Instruction *endInst = funcInfo->getRet();
    if (mallocWithOffset) {
      MallocedObject *main = funcInfo->mallocedObjs[malloc]->getMainObj();
      if (main->isDeallocated()) {
        endInst = main->getFreeCall();
      }
    }
    errs() << "Mem LEAK trace: " << *malloc << " | " << *endInst << "\n";
    return {malloc, endInst};
  }

  return {};
}

std::pair<Instruction *, Instruction *> MLChecker::Check() {
  auto mallocCalls = funcInfo->getCalls(CallInstruction::Malloc);
  errs() << "   malloc SIZE: " << mallocCalls.size() << "\n";
  for (Instruction *malloc : mallocCalls) {
    funcInfo->CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);
  }

  errs() << "NUM of PATHs" << allMallocRetPaths.size() << "\n";
  for (const auto &path : allMallocRetPaths) {
    for (auto &inst : path) {
      errs() << *inst << "\n\t|\n";
    }
    errs() << "\n";
  }

  for (auto &path : allMallocRetPaths) {
    std::pair<Instruction *, Instruction *> mlTrace = checkFreeExistence(path);
    if (mlTrace.first && mlTrace.second) {
      return mlTrace;
    }
  }

  return {};
}

} // namespace llvm