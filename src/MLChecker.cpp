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

// TODO: improve this
//ICmpInst::Predicate MLChecker::GetPredicateNullMallocedInst(Instruction *inst) {
//  auto *br = dyn_cast<BranchInst>(inst->getNextNonDebugInstruction());
//
//  auto *iCmp = dyn_cast<ICmpInst>(inst);
//  return
//
//  if (br->isConditional() && br->getNumSuccessors() == 2) {
//    auto *trueBr = br->getSuccessor(0);
//    auto *falseBr = br->getSuccessor(1);
//
//    auto *next = dyn_cast<Instruction>(*(++it));
//    if ((predicate == CmpInst::ICMP_EQ && next->getParent() == trueBr) ||
//        (predicate == CmpInst::ICMP_NE && next->getParent() == falseBr)) {
//      return true;
//    }
//  }
//  return false;
//}

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

// TODO: rename
bool MLChecker::FunctionCallDeallocation(CallInst *call) {
  Function *calledFunction = call->getCalledFunction();

  if (calledFunction->isDeclarationForLinker() || IsLibraryFunction(call)) {
    return false;
  }

  return HasSwitchWithFreeCall(calledFunction);
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
//    if (inst->getOpcode() == Instruction::ICmp && IsNullMallocedInst(path, inst)) {
//      // Malloced instruction value is null. No need free call on this path.
//      return {};
//    }

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
    auto res = FindMemleak(malloc);
    if (res.first && res.second) {
      return res;
    }
  }

//  for (Instruction *malloc : mallocCalls) {
//    FuncInfo *funcInfo = funcInfos[malloc->getFunction()].get();
//    CollectPaths(malloc, funcInfo->getRet(), allMallocRetPaths);
//  }
//
//  for (auto &path : allMallocRetPaths) {
//    ProcessTermInstOfPath(path);
//  }

//  errs() << "NUM of PATHs" << allMallocRetPaths.size() << "\n";
//  for (const auto &path : allMallocRetPaths) {
//    for (auto &inst : path) {
//      errs() << *inst << "\n\t|\n";
//    }
//    errs() << "\n";
//  }

//  for (auto &path : allMallocRetPaths) {
//    std::pair<Instruction *, Instruction *> mlTrace = CheckFreeExistence(path);
//    if (mlTrace.first && mlTrace.second) {
//      return mlTrace;
//    }
//  }

  return {};
}

std::vector<Instruction *> MLChecker::FindAllMallocCalls(Function *function) {
  return CollectAllInstsWithType(AnalyzerMap::ForwardFlowMap, &*function->getEntryBlock().begin(),
                                 [](Instruction *inst) {
                                   return IsCallWithName(inst, CallInstruction::Malloc);
                                 });
}

std::pair<Value *, Instruction *> MLChecker::FindMemleak(Instruction *malloc) {
  Function *function = malloc->getFunction();
  Instruction *start = &*function->getEntryBlock().begin();
  FuncInfo *funcInfo = funcInfos[function].get();
  Instruction *end = funcInfo->getRet();

  bool mallocWithOffset = funcInfo->mallocedObjs[malloc]->isMallocedWithOffset();
  errs() << "mallocWithOffset" << mallocWithOffset << "\n";

  DFSOptions options;

  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";
  funcInfo->printMap(AnalyzerMap::ForwardDependencyMap);
  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";

  std::vector<std::pair<BasicBlock *, BasicBlock *>> nullValueEdge;
  bool deallocated = false;

  options.continueCondition = [malloc, this, &funcInfo,
      &mallocWithOffset, &nullValueEdge,
      &end, &deallocated](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);

    const BasicBlock &lastBB = *(--(currInst->getFunction()->end()));
    Instruction *ret = const_cast<Instruction *>(&*(--(lastBB.end())));
    if (currInst == ret) { // && ret != end) {

      // reached end of main
      if (ret == end) {
        if (deallocated) {
            return false;
        }
      }

      errs() << "-----AAAA---------------------\n";
      for (auto *e : tmpPath) {
        errs() << *e << "\n";
      }
      errs() << "-----BVBB---------------------\n";

      if (tmpPath.size() >= 2) {
        errs() << "Check " << *tmpPath.rbegin()[1] << " , " << *tmpPath.back() << " | curr " << *currInst << "\n";
        auto *previous = dyn_cast<Instruction>(tmpPath.rbegin()[1]);

        for (auto &nullEdge : nullValueEdge) {
          if (previous->getParent() == nullEdge.first &&
              currInst->getParent() == nullEdge.second) {
            errs() << "^^^^^^^^^Malloced instruction value is null. No need free call on this path.\n";
            errs() << *previous << " --null-> " << *currInst << "\n";
            deallocated = true;
            return true;
          }
        }
      }
    }

    // Malloced instruction value is null. No need free call on this path.
    if (auto *iCmp = dyn_cast<ICmpInst>(currInst)) {
      errs() << "***********************GetCmpNullOperand\n";
      if (Instruction *operand = GetCmpNullOperand(currInst)) {
        errs() << "***********************Has Path to " << *operand << "\n";
        if (HasPath(AnalyzerMap::ForwardDependencyMap, malloc, operand)) {
          auto *nullCmpBr = dyn_cast<BranchInst>(currInst->getNextNonDebugInstruction());
          errs() << "*********************************Found NULL cmp\n";
          ICmpInst::Predicate predicate = iCmp->getPredicate();
          BasicBlock *nullEdgeFrom = currInst->getParent();
          BasicBlock *nullEdgeTo = nullptr;

          auto *trueBr = nullCmpBr->getSuccessor(0);
          if (predicate == CmpInst::ICMP_EQ) {
            nullEdgeTo = trueBr;
            errs() << "VAL: " << *nullEdgeTo->getFirstNonPHIOrDbg() << "\n";
            errs() << *nullEdgeFrom->getTerminator() << " --null-> " << *nullEdgeTo->getFirstNonPHIOrDbg() << "\n";
            errs() << "*********************************NULL PATH\n";
            nullValueEdge.emplace_back(nullEdgeFrom, nullEdgeTo);
            return false;
          }
          if (nullCmpBr->isConditional() && nullCmpBr->getNumSuccessors() == 2) {
            auto *falseBr = nullCmpBr->getSuccessor(1);
            if (predicate == CmpInst::ICMP_NE) {
              nullEdgeTo = falseBr;
              errs() << "VAL: " << *nullEdgeTo->getFirstNonPHIOrDbg() << "\n";
              errs() << *nullEdgeFrom->getTerminator() << " --null-> " << *nullEdgeTo->getFirstNonPHIOrDbg() << "\n";
              errs() << "*********************************NULL PATH\n";
              nullValueEdge.emplace_back(nullEdgeFrom, nullEdgeTo);
              return false;
            }
          }
        }
      }
    }

    if (tmpPath.size() >= 2) {
      errs() << "Check " << *tmpPath.rbegin()[1] << " , " << *tmpPath.back() << " | curr " << *currInst << "\n";
      auto *previous = dyn_cast<Instruction>(tmpPath.rbegin()[1]);

      for (auto &nullEdge : nullValueEdge) {
        if (previous->getParent() == nullEdge.first &&
            currInst->getParent() == nullEdge.second) {
          errs() << "^^^^^^^^^Malloced instruction value is null. No need free call on this path.\n";
          errs() << *previous << " --null-> " << *currInst << "\n";
          return true;
        }
      }
    }

    if (auto *callInst = dyn_cast<CallInst>(currInst)) {
      if (FunctionCallDeallocation(callInst)) {
        deallocated = true;
        return true;
      }
    }

    if (IsCallWithName(currInst, CallInstruction::Free)) {
      if ((mallocWithOffset && HasMallocFreePathWithOffset(funcInfo->mallocedObjs[malloc].get(), currInst))
          || (HasMallocFreePath(funcInfo->mallocedObjs[malloc].get(), currInst))) {
        errs() << "***********************SUITABLE FREE-------\n";
        deallocated = true;
        return true;
      }
    }

    return false;
  };

  options.terminationCondition = [&end, this, &deallocated](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);

    const BasicBlock &lastBB = *(--(currInst->getFunction()->end()));
    Instruction *ret = const_cast<Instruction *>(&*(--(lastBB.end())));
    if (currInst == end) {
      if (deallocated) {
        return false;
      }
      return true;
    }

    return false;
  };

  errs()
      << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";

  DFSContext context{AnalyzerMap::ForwardFlowMap, malloc, options};
  DFSResult result = DFS(context);

  for (auto &st : result.funcsStats) {
    errs() << st.first << ": " << st.second << "\n";
  }

  errs() << "Is deallocated-" << deallocated << "\n";

  if (!result.status) {
    return {};
  }

  std::vector<Value *> path = result.path;
  ProcessTermInstOfPath(path);

  errs()
      << "------------------------------------\n";
  for (
    auto *e
      : path) {
    errs()
        << "\t" << *e << "\n";
  }
  errs()
      << "------------------------------------\n";

  auto *endInst = dyn_cast<Instruction>(path.back());
  if (mallocWithOffset) {
    MallocedObject *main = funcInfo->mallocedObjs[malloc]->getMainObj();
    if (main->
        isDeallocated()
        ) {
// FIXME: take free corresponding to path
      endInst = main->getFreeCalls().front();
    }
  }
  return {
      malloc, endInst};

}

} // namespace llvm