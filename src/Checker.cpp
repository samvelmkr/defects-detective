#include "Checker.h"

#include <stack>

namespace llvm {

void Checker::addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                      Instruction *Source, Instruction *Destination) {
  Map[Source].insert(Destination);
}

bool Checker::hasEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                      Instruction *Source, Instruction *Destination) {
  auto sourceIter = Map.find(Source);
  if (sourceIter != Map.end()) {
    return sourceIter->second.find(Destination) != sourceIter->second.end();
  }
  return false;
}

void Checker::collectDependencies(Function *Func) {

  for (auto &BB : *Func) {
    for (auto &Inst : BB) {
      if (Inst.getOpcode() == Instruction::Call) {
        if (isMallocCall(&Inst)) {
          callInstructions["malloc"].insert(&Inst);
        }
        if (isFreeCall(&Inst)) {
          callInstructions["free"].insert(&Inst);
        }
      }

      auto Uses = Inst.uses();
      if (Uses.empty()) {
        continue;
      }
      for (auto &use : Uses) {
        if (auto *DependentInst = dyn_cast<llvm::Instruction>(use.getUser())) {
          if (DependentInst->getOpcode() == Instruction::GetElementPtr) {
            // Go up to the dependent instruction 'alloca'
//            errs() << *DependentInst << "\n";
            auto* GEPInst = dyn_cast<GetElementPtrInst>(DependentInst);
//            errs() << "first: " <<  *GEPInst->getOperand(0) << "\n";
            Instruction* firstOp = dyn_cast<Instruction>(GEPInst->getOperand(0));
//            errs() << "Uses\n";
            for (Instruction* Predecessor: BackwardDependencyMap.at(firstOp)) {
//              errs() << "\tbdf: " << *Predecessor << "\n";
              if (Predecessor->getOpcode() == Instruction::Alloca) {
                addEdge(DependencyMap, DependentInst, Predecessor);
                addEdge(BackwardDependencyMap, Predecessor, DependentInst);
                break;
              }
            }
          }
          if (DependentInst->getOpcode() == Instruction::Store) {
            auto *sInst = dyn_cast<StoreInst>(DependentInst);
            Value *firstOp = sInst->getValueOperand();
            Value *secondOp = sInst->getPointerOperand();
            if (!isa<Constant>(firstOp)) {
              auto *fromInst = dyn_cast<Instruction>(firstOp);
              auto *toInst = dyn_cast<Instruction>(secondOp);
              if (!hasEdge(DependencyMap, fromInst, toInst)) {
                addEdge(DependencyMap, fromInst, toInst);
              }
              if (!hasEdge(BackwardDependencyMap, toInst, fromInst)) {
                addEdge(BackwardDependencyMap, toInst, fromInst);
              }
              continue;
            }
          }

          addEdge(DependencyMap, &Inst, DependentInst);
          addEdge(BackwardDependencyMap, DependentInst, &Inst);
        }
      }
    }
  }
}

void Checker::createIntraBBEdges(BasicBlock &BB) {
  for (auto &Inst : BB) {
    if (Inst.isDebugOrPseudoInst()) {
      continue;
    }
    Instruction *nextInst = Inst.getNextNonDebugInstruction();
    if (nextInst) {
      addEdge(FlowMap, &Inst, nextInst);
    }
  }
}
void Checker::constructFlow(Function *Func) {
  for (auto &BB : *Func) {
    createIntraBBEdges(BB);
    Instruction *lastInst = &BB.back();

    if (lastInst->getOpcode() == Instruction::Br) {
      auto *Branch = dyn_cast<BranchInst>(lastInst);
      if (Branch->isConditional()) {
        addEdge(FlowMap, lastInst, Branch->getSuccessor(0)->getFirstNonPHIOrDbg());
        addEdge(FlowMap, lastInst, Branch->getSuccessor(1)->getFirstNonPHIOrDbg());
      } else if (Branch->isUnconditional()) {
        addEdge(FlowMap, lastInst, Branch->getSuccessor(0)->getFirstNonPHIOrDbg());
      }
    }
  }
}

void Checker::printMap(const std::string &map) {
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *Map = nullptr;
  if (map == "flow") {
    Map = &FlowMap;
  } else if (map == "back_dep") {
    Map = &BackwardDependencyMap;
  } else {
    Map = &ForwardDependencyMap;
  }
  for (auto &Pair : *Map) {
    Instruction *To = Pair.first;
    std::unordered_set<Instruction *> Successors = Pair.second;

    for (Instruction *Successor : Successors) {
      errs() << *To << "-->" << *Successor << "\n";
    }
  }
}

Instruction *Checker::MallocFreePathChecker() {
  for (Instruction *callInst : callInstructions.at("malloc")) {
    if (isMallocCall(callInst)) {
      if (hasMallocFreePath(callInst)) {
        // Malloc-Free Path exists starting from: callInst
        errs() << "Malloc-Free Path exists starting from:" <<  *callInst << "\n";
      } else {
        // No Malloc-Free Path found starting from: callInst
        errs() << "No Malloc-Free Path found starting from:" << *callInst << "\n";
        return callInst;
      }
    }
  }
  return nullptr;
}

bool Checker::isMallocCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "malloc";
  }
  return false;
}

bool Checker::buildBackwardDependencyPath(Instruction *from, Instruction *to) {
  return DFS(CheckerMaps::BackwardDependencyMap, from, [to](Instruction* inst) { return inst == to; });

//  std::unordered_set<Instruction *> visitedNodes;
//  std::stack<Instruction *> dfsStack;
//  dfsStack.push(from);
//
//  while (!dfsStack.empty()) {
//    Instruction *currInst = dfsStack.top();
//    dfsStack.pop();
//
//    if (currInst == to) {
//      return true;
//    }
//
//    visitedNodes.insert(currInst);
//
//    for (Instruction *depInst : BackwardDependencyMap[currInst]) {
//      if (visitedNodes.find(depInst) == visitedNodes.end()) {
//        dfsStack.push(depInst);
//      }
//    }
//  }
//  return false;
}

bool Checker::hasMallocFreePath(Instruction *startInst) {
  bool freeCallFound = false;

  return DFS(CheckerMaps::FlowMap, startInst, [&startInst, &freeCallFound, this](Instruction* inst) {
    if (isFreeCall(inst)) {
      freeCallFound = buildBackwardDependencyPath(inst, startInst);
    }
    if (inst->getOpcode() == Instruction::Ret) {
      return !freeCallFound;
    }
    return false;
  }) && freeCallFound;

//  std::unordered_set<Instruction *> visitedNodes;
//  std::stack<Instruction *> dfsStack;
//  dfsStack.push(startInst);
//
//  bool freeCallFound = false;
//  while (!dfsStack.empty()) {
//    Instruction *currInst = dfsStack.top();
//    dfsStack.pop();
//
//    if (isFreeCall(currInst)) {
//      freeCallFound = buildBackwardDependencyPath(currInst, startInst);
//    }
//
//    // Reached "ret" without encountering a "free" call
//    if (currInst->getOpcode() == Instruction::Ret) {
//      if (!freeCallFound) {
//        return false;
//      }
//    }
//
//    visitedNodes.insert(currInst);
//
//    for (Instruction *nextInst : FlowMap[currInst]) {
//      if (visitedNodes.find(nextInst) == visitedNodes.end()) {
//        dfsStack.push(nextInst);
//      }
//    }
//  }
//  if (!freeCallFound) {
//    return false;
//  }
//  return true;
}

bool Checker::isFreeCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "free";
  }
  return false;
}

bool Checker::DFS(CheckerMaps MapID,
                  Instruction *startInst,
                  const std::function<bool(Instruction *)> &terminationCondition) {
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *Map = nullptr;
  switch (MapID) {
  case CheckerMaps::ForwardDependencyMap:
    Map = &ForwardDependencyMap;
    break;
  case CheckerMaps::BackwardDependencyMap:
    Map = &BackwardDependencyMap;
    break;
  case CheckerMaps::FlowMap:
    Map = &FlowMap;
    break;
  }

  std::unordered_set<Instruction *> visitedNodes;
  std::stack<Instruction *> dfsStack;
  dfsStack.push(startInst);

  while (!dfsStack.empty()) {
    Instruction *currInst = dfsStack.top();
    dfsStack.pop();

    if (terminationCondition(currInst)) {
      return true;
    }

    visitedNodes.insert(currInst);

    for (Instruction *nextInst : Map[currInst]) {
      if (visitedNodes.find(nextInst) == visitedNodes.end()) {
        dfsStack.push(nextInst);
      }
    }
  }
  return false;
}

};