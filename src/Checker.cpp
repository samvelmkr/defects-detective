#include "Checker.h"

#include "llvm/IR/GlobalVariable.h"

#include <sstream>
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

unsigned int Checker::getFormatStringSize(GlobalVariable *var) {
  if (Constant *formatStringConst = var->getInitializer()) {
    if (ConstantDataArray *formatArray = dyn_cast<ConstantDataArray>(formatStringConst)) {
      StringRef formatString = formatArray->getAsCString();
      std::stringstream ss(formatString.data());
      char c;

      while (ss >> c) {
        if (c == '%') {
          c = (char) ss.peek();
          if (c && !isdigit(c)) {
            return 0;
          } else {
            unsigned int res;
            ss >> res;
            return res;
          }
        }
      }

    }
  }
  return 0;
}

unsigned int Checker::getArraySize(AllocaInst *pointerArray) {
  Type *basePointerType = pointerArray->getAllocatedType();
  if (auto *arrType = dyn_cast<ArrayType>(basePointerType)) {
    return arrType->getNumElements();
  }
  return 0;
}

bool Checker::isScanfCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "__isoc99_scanf";
  }
  return false;
}

InstructionPairPtr::Ptr Checker::BuffOverflowChecker(llvm::Function *Func) {
  if (callInstructions.empty() ||
      callInstructions.find("scanf") == callInstructions.end()) {
    return {};
  }

  for (Instruction *cInst : callInstructions.at("scanf")) {
    auto *callInst = dyn_cast<CallInst>(cInst);

    Value *formatStringAgr = callInst->getOperand(0);
    auto *bufArgInst = dyn_cast<Instruction>(callInst->getOperand(1));
    auto *bufGEP = dyn_cast<GetElementPtrInst>(bufArgInst);
    Value *basePointer = bufGEP->getPointerOperand();
    auto *basePointerArray = dyn_cast<AllocaInst>(basePointer);
    if (!basePointerArray) {
      return {};
    }

    if (auto *formatStringGV = dyn_cast<GlobalVariable>(formatStringAgr->stripPointerCasts())) {
      unsigned int formatStringSize = getFormatStringSize(formatStringGV);
      if (!formatStringSize) {
        return InstructionPairPtr::makePair(basePointerArray, cInst);
      }
      if (formatStringSize >= getArraySize(basePointerArray)) {
        return InstructionPairPtr::makePair(basePointerArray, cInst);
      }
    }
  }
  return {};
}

};