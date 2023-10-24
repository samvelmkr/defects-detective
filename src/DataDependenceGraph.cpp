#include "DataDependenceGraph.h"

#include <stack>

namespace llvm {

void DataFlowGraph::addEdge(Instruction *Source, Instruction *Destination) {
  DependencyMap[Source].insert(Destination);
}

bool DataFlowGraph::hasEdge(Instruction *Source, Instruction *Destination) {
  auto sourceIter = DependencyMap.find(Source);
  if (sourceIter != DependencyMap.end()) {
    return sourceIter->second.find(Destination) != sourceIter->second.end();
  }
  return false;
}

void DataFlowGraph::collectDependencies(Function *Func) {

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
              if (!hasEdge(fromInst, toInst)) {
                addEdge(fromInst, toInst);
              }
              continue;
            }
          }

          addEdge(&Inst, DependentInst);
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
void DataFlowGraph::print() {
  for (auto &Pair : DependencyMap) {
    Instruction *To = Pair.first;
    std::unordered_set<Instruction *> Dependencies = Pair.second;

    for (Instruction *Dependency : Dependencies) {
      errs() << *To << "-->" << *Dependency << "\n";
    }
  }
}
bool DataFlowGraph::MallocFreePathChecker() {
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
bool DataFlowGraph::isMallocCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "malloc";
  }
  return false;
}

bool DataFlowGraph::hasMallocFreePath(Instruction *startInst) {
  std::unordered_set<Instruction *> visitedNodes;
  std::stack<Instruction *> dfsStack;
  dfsStack.push(startInst);

  while (!dfsStack.empty()) {
    Instruction *currInst = dfsStack.top();
    dfsStack.pop();

    //    if (DependencyMap[currInst].empty()) {
    errs() << "############# CURR: " << *currInst << "\n";
//    }

    if (isFreeCall(currInst)) {
      continue;
    }

    // Reached "ret" without encountering a "free" call
    if (isa<ReturnInst>(currInst)) {
      return false;
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
bool DataFlowGraph::isFreeCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "free";
  }
  return false;
}

};