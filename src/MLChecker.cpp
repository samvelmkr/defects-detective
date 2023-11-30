#include "MLChecker.h"

namespace llvm {

MLChecker::MLChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos)
    : Checker(funcInfos) {}

bool MLChecker::HasMallocFreePath(MallocedObject *obj, Instruction *free) {
  DFSOptions options;
  options.terminationCondition = [obj, free](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);
    if (currInst == free) {
      obj->addFreeCall(free);
      return true;
    }
    return false;
  };
  options.continueCondition = [](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);
    return currInst->getOpcode() == Instruction::GetElementPtr;
  };

  Instruction *malloc = obj->getMallocCall();
  DFSContext context{AnalyzerMap::ForwardDependencyMap, malloc, options};
  DFSResult result = DFS(context);
  return result.status;
}

bool MLChecker::HasMallocFreePathWithOffset(MallocedObject *obj, Instruction *free) {
  bool reachedFirstGEP = false;
  bool reachedAlloca = false;
  bool reachedSecondGEP = false;

  DFSOptions options;
  options.terminationCondition = [obj, &reachedFirstGEP, &reachedAlloca,
      &reachedSecondGEP, free](Value *curr) {

    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);

    if (currInst->getOpcode() == Instruction::GetElementPtr) {
      auto *gep = dyn_cast<GetElementPtrInst>(currInst);
      if (obj->getOffset() == CalculateOffsetInBits(gep)) {
        reachedFirstGEP = true;
      }
    }
    if (reachedFirstGEP && currInst->getOpcode() == Instruction::Alloca) {
      reachedAlloca = true;
    }
    if (reachedAlloca && currInst->getOpcode() == Instruction::GetElementPtr) {
      auto *gep = dyn_cast<GetElementPtrInst>(currInst);
      if (obj->getOffset() == CalculateOffsetInBits(gep)) {
        reachedSecondGEP = true;
      }
    }
    if (reachedSecondGEP && currInst == free) {
      obj->addFreeCall(free);
      return true;
    }
    return false;
  };

  Instruction *malloc = obj->getMallocCall();
  DFSContext context{AnalyzerMap::ForwardDependencyMap, malloc, options};
  DFSResult result = DFS(context);
  return result.status;
}

bool MLChecker::IsNullMallocedInst(std::vector<Value *> &path, Instruction *inst) {
  Instruction *operand = GetCmpNullOperand(inst);
  if (!operand) {
    return false;
  }

  auto *malloc = dyn_cast<Instruction>(path.front());
  if (!HasPath(AnalyzerMap::BackwardDependencyMap, operand, malloc)) {
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
    auto *trueBr = br->getSuccessor(0);
    auto *falseBr = br->getSuccessor(1);
    auto *next = dyn_cast<Instruction>(*(++it));
    if ((predicate == CmpInst::ICMP_EQ && next->getParent() == trueBr) ||
        (predicate == CmpInst::ICMP_NE && next->getParent() == falseBr)) {
      return true;
    }
  }
  return false;
}

bool MLChecker::HasSwitchWithFreeCall(llvm::Function *function) {
  for (BasicBlock &bb : *function) {
    for (Instruction &i : bb) {
      if (auto *switchInst = dyn_cast<SwitchInst>(&i)) {
        BasicBlock *defaultBB = switchInst->getDefaultDest();
        for (Instruction &defaultInst : *defaultBB) {
          if (auto *call = dyn_cast<CallInst>(&defaultInst)) {
            Function *calledFunction = call->getCalledFunction();
            if (calledFunction && calledFunction->getName() == "free") {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

bool MLChecker::FunctionCallDeallocation(CallInst *call) {
  Function *calledFunction = call->getCalledFunction();

  if (calledFunction->isDeclarationForLinker() || IsLibraryFunction(call)) {
    return false;
  }
  if (HasSwitchWithFreeCall(calledFunction)) {
    return true;
  }

//  FuncInfo *funcInfo = funcInfos[calledFunction].get();
//  CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);

  return false;
}

std::pair<Instruction *, Instruction *> MLChecker::CheckFreeExistence(std::vector<Value *> &path) {
  auto *malloc = dyn_cast<Instruction>(path.front());
  FuncInfo *funcInfo = funcInfos[malloc->getFunction()].get();

  bool mallocWithOffset = funcInfo->mallocedObjs[malloc]->isMallocedWithOffset();
  bool foundMallocFreePath = false;

  for (Value *val : path) {
    if (!isa<Instruction>(val)) {
      continue;
    }
    auto *inst = dyn_cast<Instruction>(val);
    if (inst->getOpcode() == Instruction::ICmp && IsNullMallocedInst(path, inst)) {
      // Malloced instruction value is null. No need free call on this path.
      return {};
    }

    if (auto *callInst = dyn_cast<CallInst>(inst)) {
      if (FunctionCallDeallocation(callInst)) {
        foundMallocFreePath = true;
        break;
      }
    }

    if (IsCallWithName(inst, CallInstruction::Free)) {
      if ((mallocWithOffset && HasMallocFreePathWithOffset(funcInfo->mallocedObjs[malloc].get(), inst))
          || (HasMallocFreePath(funcInfo->mallocedObjs[malloc].get(), inst))) {
        foundMallocFreePath = true;
        break;
      }
    }

  }

  if (!foundMallocFreePath) {
    auto *endInst = dyn_cast<Instruction>(path.back());
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

//void MLChecker::MLTraverse(Instruction *inst) {
//  Function *function = inst->getFunction();
//  Instruction *start = &*function->getEntryBlock().begin();
//  FuncInfo *funcInfo = funcInfos[function].get();
//  Instruction *end = funcInfo->getRet();
//  DFSOptions options;
//
//  options.terminationCondition = [&end, this](Value *curr) {
//    if (!isa<Instruction>(curr)) {
//      return false;
//    }
//    auto *currInst = dyn_cast<Instruction>(curr);
//
//    if (currInst == end) {
//      errs() << "-----1144---------------------\n";
//      for (auto *e : result.path) {
//        errs() << *e << "\n";
//      }
////    errs() << "Visited\n{ ";
////    for (auto* val : visitedNodes) {
////      errs() << *val << ", ";
////    }
////    errs() << " }\n";
//      errs() << "--------------------------\n";
//    }
//
//    return false;
//  };
//
//  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//
//  DFSContext context{AnalyzerMap::ForwardFlowMap, start, options};
//  DFSResult result = DFS(context);
//}

std::pair<Value *, Instruction *> MLChecker::Check(Function *function) {

  auto mallocCalls = FindAllMallocCalls(function);
  if (mallocCalls.empty()) {
    return {};
  }
  errs() << "NUM of mallocs " << mallocCalls.size() << "\n";

  for (Instruction *malloc : mallocCalls) {
    errs() << "\tmalloc " << *malloc << "\n";

  }

  for (Instruction *malloc : mallocCalls) {
    FuncInfo *funcInfo = funcInfos[malloc->getFunction()].get();
    CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);
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
    std::pair<Instruction *, Instruction *> mlTrace = CheckFreeExistence(path);
    if (mlTrace.first && mlTrace.second) {
      return mlTrace;
    }
  }

  return {};
}

std::vector<Instruction *> MLChecker::FindAllMallocCalls(Function *function) {
  return CollectAllInstsWithType(AnalyzerMap::ForwardFlowMap, &*function->getEntryBlock().begin(),
                                 [](Instruction *inst) {
                                   return IsCallWithName(inst, CallInstruction::Malloc);
                                 });
}

} // namespace llvm