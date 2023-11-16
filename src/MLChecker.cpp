#include "MLChecker.h"

namespace llvm {

MLChecker::MLChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}

bool MLChecker::hasMallocFreePath(MallocedObject *obj, Instruction *free) {
  return funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                       free,
                       [obj, free](Instruction *curr) {
                         // Termination condition
                         if (curr == obj->getMallocCall()) {
                           obj->addFreeCall(free);
                           return true;
                         }
                         return false;
                       },
                       [](Instruction *curr) {
                         // Continue condition
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
                           obj->addFreeCall(free);
                           return true;
                         }
                         return false;
                       });
}

Instruction* MLChecker::hasCmpWithNull(Instruction *icmp) {
  Instruction *opInst = nullptr;
  bool compareWithNull = false;
  for (Use &op : icmp->operands()) {
    compareWithNull = isa<ConstantPointerNull>(op);
    if (!compareWithNull && isa<Instruction>(op)) {
      opInst = dyn_cast<Instruction>(op);
    }
  }

  if (compareWithNull) {
    return opInst;
  }

  return nullptr;
}

bool MLChecker::IsNullMallocedInst(std::vector<Instruction *> &path, Instruction *inst) {
  Instruction* operand = hasCmpWithNull(inst);
  if (!operand) {
    return false;
  }

  Instruction *malloc = path.front();
  if (!funcInfo->hasPath(AnalyzerMap::BackwardDependencyMap, operand, malloc)) {
    return false;
  }

  auto it = std::find(path.begin(), path.end(), inst);
  auto *br = dyn_cast<BranchInst>(*(++it));
  Value *Condition = br->getCondition();

  if (it + 1 == path.end()) {
    return false;
  }

  auto *iCmp = dyn_cast<ICmpInst>(inst);
  ICmpInst::Predicate predicate = iCmp->getPredicate();

  if (br->isConditional() && br->getNumSuccessors() == 2) {
    auto* trueBr = br->getSuccessor(0);
    auto* falseBr = br->getSuccessor(1);
    Instruction* next = *(++it);
    if ((predicate == CmpInst::ICMP_EQ && next->getParent() == trueBr) ||
      (predicate == CmpInst::ICMP_NE && next->getParent() == falseBr)) {
      return true;
    }
  }
  return false;
}

std::pair<Instruction *, Instruction *> MLChecker::checkFreeExistence(std::vector<Instruction *> &path) {
  Instruction *malloc = path[0];

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

    if (inst->getOpcode() == Instruction::ICmp && IsNullMallocedInst(path, inst)) {
      // Malloced instruction value is null. No need free call on this path.
      return {};
    }
  }

  if (!foundMallocFreePath) {
    Instruction *endInst = path.back();
    if (mallocWithOffset) {
      MallocedObject *main = funcInfo->mallocedObjs[malloc]->getMainObj();
      if (main->isDeallocated()) {
        // FIXME: take free corresponding to path
        endInst = main->getFreeCalls().front();
      }
    }
    return {malloc, endInst};
  }

  return {};
}

void MLChecker::ProcessTermInstOfPath(std::vector<Instruction *> &path) {
  Instruction *lastInst = path.back();
  if (lastInst->getOpcode() != Instruction::Ret) {
    return;
  }
  BasicBlock *termBB = lastInst->getParent();
  if (termBB->getInstList().size() != 2) {
    return;
  }
  Instruction *firstInst = &termBB->getInstList().front();
  if (firstInst->getOpcode() != Instruction::Load) {
    return;
  }
  path.pop_back();
  path.pop_back();
}

std::pair<Instruction *, Instruction *> MLChecker::Check() {
  auto mallocCalls = funcInfo->getCalls(CallInstruction::Malloc);
  if (mallocCalls.empty()) {
    return {};
  }

  for (Instruction *malloc : mallocCalls) {
    funcInfo->CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);
  }

  for (auto &path : allMallocRetPaths) {
    ProcessTermInstOfPath(path);
  }
//  errs() << "NUM of PATHs" << allMallocRetPaths.size() << "\n";
//  for (const auto &path : allMallocRetPaths) {
//    for (auto &inst : path) {
//      errs() << *inst << "\n\t|\n";
//    }
//    errs() << "\n";
//  }

  for (auto &path : allMallocRetPaths) {
    std::pair<Instruction *, Instruction *> mlTrace = checkFreeExistence(path);
    if (mlTrace.first && mlTrace.second) {
      return mlTrace;
    }
  }

  return {};
}

} // namespace llvm