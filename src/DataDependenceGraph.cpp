#include "DataDependenceGraph.h"

#include <stack>

namespace llvm {

void Analyzer::addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                       Instruction *Source, Instruction *Destination) {
  Map[Source].insert(Destination);
}

bool Analyzer::hasEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                       Instruction *Source, Instruction *Destination) {
  auto sourceIter = Map.find(Source);
  if (sourceIter != Map.end()) {
    return sourceIter->second.find(Destination) != sourceIter->second.end();
  }
  return false;
}

void Analyzer::collectDependencies(Function *Func) {

  for (auto &BB : *Func) {
    for (auto &Inst : BB) {
      if (Inst.getOpcode() == Instruction::Call) {
        callInstructions.insert(&Inst);
      }

      auto Uses = Inst.uses();
      if (Uses.empty()) {
        continue;
      }
      for (auto &use : Uses) {
        if (auto *DependentInst = dyn_cast<llvm::Instruction>(use.getUser())) {

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
//              if (!hasEdge(BackwardDependencyMap, toInst, fromInst)) {
//                addEdge(BackwardDependencyMap, toInst, fromInst);
//              }
              continue;
            }
          }

          addEdge(DependencyMap, &Inst, DependentInst);
//          addEdge(BackwardDependencyMap, DependentInst, &Inst);
        }
      }
    }
  }

//  for (auto &BB : *Func) {
//    for (auto &I : BB) {
//      errs() << "Instr: " << I << "\n";
//      for (Use &U : I.operands()) {
//        errs() << "use: " << *U << "\n";
//        Value *Operand = U.get();
//        errs() << "operand: " << *Operand << "\n";
//        if (auto *DependentInst = dyn_cast<Instruction>(Operand)) {
//          addEdge(DependentInst, &I);
//        }
//      }
//      errs() << "\n";
//    }
//  }
}

void Analyzer::createIntraBBEdges(BasicBlock &BB) {
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
void Analyzer::constructFlow(Function *Func) {
  for (auto &BB : *Func) {
    createIntraBBEdges(BB);
    Instruction *lastInst = &BB.back();

    // TODO: is there any jump instructions except 'br'?
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

void Analyzer::printMap(const std::string &map) {
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *Map = nullptr;
  if (map == "flow") {
    Map = &FlowMap;
  } else {
    Map = &DependencyMap;
  }
  for (auto &Pair : *Map) {
    Instruction *To = Pair.first;
    std::unordered_set<Instruction *> Successors = Pair.second;

    for (Instruction *Successor : Successors) {
      errs() << *To << "-->" << *Successor << "\n";
    }
  }
}
bool Analyzer::MallocFreePathChecker() {
  if (callInstructions.empty()) {
    return true;
  }

  for (Instruction *callInst : callInstructions) {
    if (isMallocCall(callInst)) {
      if (hasMallocFreePath(callInst)) {
        errs() << "Malloc-Free Path exists starting from: " << *callInst << "\n";
      } else {
        errs() << "No Malloc-Free Path found starting from: " << *callInst << "\n";
      }
    }
  }

  return false;
}
bool Analyzer::isMallocCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "malloc";
  }
  return false;
}

bool Analyzer::buildDependencyPath(Instruction *from, Instruction *to) {
  std::unordered_set<Instruction *> visitedNodes;
  std::stack<Instruction *> dfsStack;
  dfsStack.push(from);

  while (!dfsStack.empty()) {
    Instruction *currInst = dfsStack.top();
    dfsStack.pop();

    if (currInst == to) {
      return true;
    }

    visitedNodes.insert(currInst);

    for (Instruction *depInst : DependencyMap[currInst]) {
      if (visitedNodes.find(depInst) == visitedNodes.end()) {
        dfsStack.push(depInst);
      }
    }
  }
  return false;
}

bool Analyzer::hasMallocFreePath(Instruction *startInst) {
  std::unordered_set<Instruction *> visitedNodes;
  std::stack<Instruction *> dfsStack;
  dfsStack.push(startInst);

  while (!dfsStack.empty()) {
    Instruction *currInst = dfsStack.top();
    dfsStack.pop();

//    errs() << "############# CURR: " << *currInst << "\n";

    if (isFreeCall(currInst)) {
      errs() << "Free inst: " << *currInst << "\n"
             << "\t op1: " << *currInst->getOperand(0) << "\n";

      if (!buildDependencyPath(startInst, currInst)) {
        return false;
      }
      continue;
    }

    // Reached "ret" without encountering a "free" call
    if (isa<ReturnInst>(currInst)) {
      errs() << "Ret inst: " << *currInst << "\n"
             << "\t op1: " << *currInst->getOperand(0) << "\n";
      return false;
    }

    visitedNodes.insert(currInst);

    for (Instruction *nextInst : FlowMap[currInst]) {
      if (visitedNodes.find(nextInst) == visitedNodes.end()) {
        // Check if the dependency is relevant to memory allocation and deallocation
        if (isRelevantToMemoryManagement(nextInst)) {
          dfsStack.push(nextInst);
        }
      }
    }
  }
  return true;
}
bool Analyzer::isFreeCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "free";
  }
  return false;
}
bool Analyzer::isRelevantToMemoryManagement(Instruction *Inst) {
  return Inst->getType()->isPointerTy();
}


};