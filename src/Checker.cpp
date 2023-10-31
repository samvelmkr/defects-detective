#include "Checker.h"

#include "llvm/IR/GlobalVariable.h"

#include <stack>

namespace llvm {

void Checker::addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                      Instruction *Source, Instruction *Destination) {
  Map[Source].insert(Destination);
}

void Checker::removeEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                         Instruction *Source,
                         Instruction *Destination) {
  if (Map.find(Source) != Map.end()) {
    Map[Source].erase(Destination);
  }
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
        if (isScanfCall(&Inst)) {
          errs() << "AAAAAAAAAAAAAAAAAAAAAA Found\n";
          callInstructions["scanf"].insert(&Inst);
        }
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
              if (!hasEdge(ForwardDependencyMap, fromInst, toInst)) {
                addEdge(ForwardDependencyMap, fromInst, toInst);
              }
              if (!hasEdge(BackwardDependencyMap, toInst, fromInst)) {
                addEdge(BackwardDependencyMap, toInst, fromInst);
              }
              continue;
            }
          }

          addEdge(ForwardDependencyMap, &Inst, DependentInst);
          addEdge(BackwardDependencyMap, DependentInst, &Inst);
        }
      }
    }
  }

  // GEP validation
  updateDependencies();

  // create cfg
  constructFlow(Func);
}

void Checker::updateDependencies() {
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return;
  }
  for (Instruction *callInst : callInstructions.at("malloc")) {
    for (auto &depInst : ForwardDependencyMap[callInst]) {
      if (depInst->getOpcode() == Instruction::GetElementPtr) {
        auto *GEPInst = dyn_cast<GetElementPtrInst>(depInst);
        auto *firstOp = dyn_cast<Instruction>(GEPInst->getOperand(0));
        // Go up to the dependent instruction 'alloca'
        for (Instruction *Predecessor : BackwardDependencyMap.at(firstOp)) {
          if (Predecessor->getOpcode() == Instruction::Alloca) {
            removeEdge(ForwardDependencyMap, Predecessor, firstOp);
            removeEdge(BackwardDependencyMap, firstOp, Predecessor);

            addEdge(ForwardDependencyMap, depInst, Predecessor);
            addEdge(BackwardDependencyMap, Predecessor, depInst);
          }
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
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return nullptr;
  }
  for (Instruction *callInst : callInstructions.at("malloc")) {
    if (hasMallocFreePath(callInst)) {
      // Malloc-Free Path exists starting from: callInst
    } else {
      // No Malloc-Free Path found starting from: callInst
      return callInst;
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
  return DFS(CheckerMaps::BackwardDependencyMap, from, [to](Instruction *inst) { return inst == to; });
}

bool Checker::hasMallocFreePath(Instruction *startInst) {
  bool reachedAlloca = false;
  bool reachedGEP = false;
  bool structField = false;

  // Check for struct field access
  for (auto &depInst : ForwardDependencyMap[startInst]) {
    if (depInst->getOpcode() == Instruction::GetElementPtr) {
      structField = true;
      startInst = depInst;
    }
  }

  std::function<bool(Instruction *)> terminationCondition;
  if (structField) {
    terminationCondition = [&reachedAlloca, &reachedGEP](Instruction *inst) {
      if (inst->getOpcode() == Instruction::Alloca) {
        reachedAlloca = true;
      }
      if (reachedAlloca && inst->getOpcode() == Instruction::GetElementPtr) {
        reachedGEP = true;
      }
      if (reachedAlloca && reachedGEP && isFreeCall(inst)) {
        return true;
      }
      return false;
    };
  } else {
    terminationCondition = [](Instruction *inst) {
      if (isFreeCall(inst)) {
        return true;
      }
      return false;
    };
  }

  return DFS(CheckerMaps::ForwardDependencyMap, startInst, terminationCondition);
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
  case CheckerMaps::ForwardDependencyMap:Map = &ForwardDependencyMap;
    break;
  case CheckerMaps::BackwardDependencyMap:Map = &BackwardDependencyMap;
    break;
  case CheckerMaps::FlowMap:Map = &FlowMap;
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

    for (Instruction *nextInst : Map->operator[](currInst)) {
      if (visitedNodes.find(nextInst) == visitedNodes.end()) {
        dfsStack.push(nextInst);
      }
    }
  }
  return false;
}
//===--------------------------------------------------------------------===//
// Buffer overflow checker.
//===--------------------------------------------------------------------===//

bool Checker::isScanfCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "__isoc99_scanf";
  }
  return false;
}

std::pair<Instruction *, Instruction *> *Checker::BuffOverflowChecker(Function *Func) {
  if (callInstructions.empty() ||
      callInstructions.find("scanf") == callInstructions.end()) {
    return nullptr;
  }

  for (Instruction *Inst : callInstructions.at("scanf")) {
    errs() << "scanf call" << *Inst << "\n";
    CallInst* callInst = dyn_cast<CallInst>(Inst);
    for (auto& op : callInst->operands()) {
      errs() << "\top: " << *op << "\n";
    }

    Value* formatStringAgr = callInst->getOperand(0);
    Value* bufArg = callInst->getOperand(1);
    errs() << "Aaaaaaa\n";
    auto *formatStringConst = dyn_cast<GetElementPtrInst>(formatStringAgr);
//    errs() << formatString->getType() << "\n";
    if (!formatStringConst) {
      errs() << "ebani vrot\n";
    }
    errs() << formatStringConst << "\n";
    return;
    ConstantDataArray *formatArray = dyn_cast<ConstantDataArray>(formatStringConst->getInitializer());
    errs() << "Ccccccc\n";
    StringRef formatString = formatArray->getAsCString();
    errs() << "Dddddddd\n";
    errs() << formatString << "\n";
  }

//  if (auto *call = dyn_cast<CallInst>(&I)) {
//    Function *calledFunc = call->getCalledFunction();
//    if (calledFunc && calledFunc->getName() == "__isoc99_scanf") {
//      // This is a call to scanf.
//      Value *formatStringArg =
//      Value *bufferArg = call->getArgOperand(1);
//      if (auto *formatStringConst = dyn_cast<GlobalVariable>(formatStringArg)) {
//        ConstantDataArray *formatArray = dyn_cast<ConstantDataArray>(formatStringConst->getInitializer());
//        if (formatArray) {
//          StringRef formatString = formatArray->getAsCString();
//          if (formatString.find("%s") != StringRef::npos) {
//            // The format string contains %s.
//            // Analyze the buffer size and check for potential overflows.
//            // You can compare the buffer size with the input size expected by %s.
//            // Note: This is a simplified example and may not cover all cases.
//          }
//        }
//      }
//    }
//  }
  return nullptr;
}

};