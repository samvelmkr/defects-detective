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

  collectMallocedObjs();

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

void Checker::collectMallocedObjs() {
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return;
  }

  for (Instruction *callInst : callInstructions.at("malloc")) {
    DFS(CheckerMaps::ForwardDependencyMap, callInst, [callInst, this](Instruction *curr) {
      if (curr->getOpcode() == Instruction::Alloca) {
        auto Obj = std::make_shared<MallocedObject>(curr);
        Obj->setMallocCall(callInst);
        MallocedObjs.emplace_back(Obj);
        return true;
      }
      if (curr->getOpcode() == Instruction::GetElementPtr) {
        auto Obj = std::make_shared<MallocedObject>(curr);
        Obj->setMallocCall(callInst);

        Module *M = curr->getModule();
        APInt accumulateOffset;
        auto *gep = dyn_cast<GetElementPtrInst>(curr);
        gep->accumulateConstantOffset(M->getDataLayout(), accumulateOffset);

        Instruction* nextInst = *(ForwardDependencyMap[curr].begin());
        Obj->setOffset(nextInst, accumulateOffset.getZExtValue());
        return true;
      }
      return false;
    });
  }
}

void Checker::createIntraBBEdges(BasicBlock &BB) {
  for (auto &Inst : BB) {
    if (Inst.isDebugOrPseudoInst()) {
      continue;
    }
    Instruction *nextInst = Inst.getNextNonDebugInstruction();
    if (nextInst) {
      addEdge(ForwardFlowMap, &Inst, nextInst);
    }
  }
}
void Checker::constructFlow(Function *Func) {
  // TODO: construct also backward flow graph

  for (auto &BB : *Func) {
    createIntraBBEdges(BB);
    Instruction *lastInst = &BB.back();

    if (lastInst->getOpcode() == Instruction::Br) {
      auto *Branch = dyn_cast<BranchInst>(lastInst);
      if (Branch->isConditional()) {
        addEdge(ForwardFlowMap, lastInst, Branch->getSuccessor(0)->getFirstNonPHIOrDbg());
        addEdge(ForwardFlowMap, lastInst, Branch->getSuccessor(1)->getFirstNonPHIOrDbg());
      } else if (Branch->isUnconditional()) {
        addEdge(ForwardFlowMap, lastInst, Branch->getSuccessor(0)->getFirstNonPHIOrDbg());
      }
    }
  }
}

void Checker::printMap(CheckerMaps MapID) {
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *Map = nullptr;
  switch (MapID) {
  case CheckerMaps::ForwardDependencyMap:Map = &ForwardDependencyMap;
    break;
  case CheckerMaps::BackwardDependencyMap:Map = &BackwardDependencyMap;
    break;
  case CheckerMaps::ForwardFlowMap:Map = &ForwardFlowMap;
    break;
  case CheckerMaps::BackwardFlowMap:Map = &BackwardFlowMap;
    break;
  }

  if (!Map)
    return;

  for (auto &Pair : *Map) {
    Instruction *To = Pair.first;
    std::unordered_set<Instruction *> Successors = Pair.second;

    for (Instruction *Successor : Successors) {
      errs() << *To << "-->" << *Successor << "\n";
    }
  }
}

//===--------------------------------------------------------------------===//
// Memory leak checker.
//===--------------------------------------------------------------------===//

//MallocType Checker::getMallocType(Instruction *mallocInst) {
//  for (auto &depInst : ForwardDependencyMap[mallocInst]) {
//    if (depInst->getOpcode() == Instruction::Alloca) {
//      auto *allocaInst = dyn_cast<AllocaInst>(depInst);
//      Type *allocatedType = allocaInst->getAllocatedType();
//      if (allocatedType->isPointerTy() && allocatedType->getPointerElementType()->isStructTy()) {
//        return MallocType::AllocateMemForStruct;
//      }
//      break;
//    }
//    if (depInst->getOpcode() == Instruction::GetElementPtr) {
//      auto *GEP = dyn_cast<GetElementPtrInst>(depInst);
//      Type *opBasePointer = GEP->getSourceElementType();
//      if (opBasePointer->isStructTy()) {
//        return MallocType::AllocateMemForStructField;
//      }
//      break;
//    }
//  }
//  return MallocType::SimpleMemAllocation;
//}

InstructionPairPtr::Ptr Checker::MemoryLeakChecker() {
//  if (MemAllocInfos.empty()) {
//    return {};
//  }
//  for (auto& InfoPtr : MemAllocInfos) {
//    MemAllocationInfo *Info = InfoPtr.get();
//    if (Info->isStructMalloc()) {
//      InfoPtr = std::make_shared<MemAllocationInfo>(hasMallocFreePathForStruct(Info));
//    } else if (Info->isStructFieldMalloc()) {
//      InfoPtr = std::make_shared<MemAllocationInfo>(hasMallocFreePathForStructField(Info));
//    } else {
//      InfoPtr = std::make_shared<MemAllocationInfo>(hasMallocFreePath(Info));
//    }
//  }
  return {};
}
//    hasMallocFreePathForStructField(callInst);
//    hasMallocFreePathForStruct(callInst);
//
//  }
//  if (hasMallocFreePath(callInst)) {
//    // Malloc-Free Path exists starting from: callInst
//  } else {
//    // No Malloc-Free Path found starting from: callInst
//    return callInst;
//  }
//}
//return nullptr;
//}

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

MemAllocationInfo *Checker::hasMallocFreePathForStruct(MemAllocationInfo *Info) {
  Instruction* mallocInst = Info->getMallocInst();
  DFS(CheckerMaps::ForwardDependencyMap, mallocInst, [Info, this](Instruction *curr) {
    if (isFreeCall(curr)) {
      Info->setFreeCall(curr);
//      StructInfos[structName].get()->setMallocScope(pair);
      return true;
    }
    return false;
  });

  return {};
}
//
//bool Checker::hasMallocFreePathForStructField(Instruction *mallocInst) {
//  auto getOffset = [](GetElementPtrInst *gep) {
//    Module *M = gep->getModule();
//    APInt accumulateOffset;
//    if (gep->accumulateConstantOffset(M->getDataLayout(), accumulateOffset)) {
//      return accumulateOffset.getZExtValue();
//    }
//    return SIZE_MAX;
//  };
//
//  bool reachedAlloca = false;
//  bool reachedGEP = false;
//  size_t structFieldOffset;
//
//  return DFS(CheckerMaps::ForwardDependencyMap, mallocInst,
//             [mallocInst, &reachedAlloca, &reachedGEP, &structFieldOffset, &getOffset, &structName, this](Instruction *curr) {
//               if (curr->getOpcode() == Instruction::Alloca) {
//                 reachedAlloca = true;
//               }
//               if (reachedAlloca && curr->getOpcode() == Instruction::GetElementPtr) {
//                 auto *gep = dyn_cast<GetElementPtrInst>(curr);
//                 Type *sourceType = gep->getSourceElementType();
//                 if (sourceType->isStructTy() && structFieldOffset == getOffset(gep)) {
//                   reachedGEP = true;
//                 }
//               }
//               if (reachedAlloca && reachedGEP && isFreeCall(curr)) {
//                 InstructionPairPtr pair = InstructionPairPtr::makePair(mallocInst, curr);
//                 MallocFreePairs.push_back(pair);
//                 StructInfos[structName].get()->setFieldMallocScope(structFieldOffset, pair);
//                 return true;
//               }
//               return false;
//             });
//}
//
//bool Checker::hasMallocFreePath(Instruction *mallocInst) {
//  bool isMallocStruct = false;
//  bool isMallocStructField = false;
//  std::string structName;
//
//  for (auto &depInst : ForwardDependencyMap[mallocInst]) {
//    if (depInst->getOpcode() == Instruction::Alloca) {
//      auto *allocaInst = dyn_cast<AllocaInst>(depInst);
//      Type *allocatedType = allocaInst->getAllocatedType();
//      if (allocatedType->isPointerTy() && allocatedType->getPointerElementType()->isStructTy()) {
//        auto *structType = dyn_cast<StructType>(allocatedType->getPointerElementType());
//        structName = structType->getName().str();
//        isMallocStruct = true;
//      }
//      break;
//    }
//    if (depInst->getOpcode() == Instruction::GetElementPtr) {
//      auto *GEP = dyn_cast<GetElementPtrInst>(depInst);
//      Type *opBasePointer = GEP->getSourceElementType();
//      if (opBasePointer->isStructTy()) {
//        isMallocStructField = true;
//      }
//      break;
//    }
//  }
//
//  if (isMallocStructField) {
//    *isStructField = true;
//    return hasMallocFreePathForStructField(mallocInst, structName);
//  }
//  if (isMallocStruct) {
//    *isStruct = true;
//    return hasMallocFreePathForStruct(mallocInst, structName);
//  }
//
//  std::function<bool(Instruction * )> terminationCondition = [mallocInst, this](Instruction *inst) {
//    if (isFreeCall(inst)) {
//      InstructionPairPtr pair = InstructionPairPtr::makePair(mallocInst, inst);
//      MallocFreePairs.push_back(pair);
//      return true;
//    }
//    return false;
//  };
//
//  return DFS(CheckerMaps::ForwardDependencyMap, mallocInst, terminationCondition);
//}

bool Checker::isFreeCall(Instruction *Inst) {
  if (auto *callInst = dyn_cast<CallInst>(Inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName() == "free";
  }
  return false;
}

bool Checker::DFS(CheckerMaps MapID,
                  Instruction *startInst,
                  const std::function<bool(Instruction * )> &terminationCondition) {
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *Map = nullptr;
  switch (MapID) {
  case CheckerMaps::ForwardDependencyMap:Map = &ForwardDependencyMap;
    break;
  case CheckerMaps::BackwardDependencyMap:Map = &BackwardDependencyMap;
    break;
  case CheckerMaps::ForwardFlowMap:Map = &ForwardFlowMap;
    break;
  case CheckerMaps::BackwardFlowMap:Map = &BackwardFlowMap;
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
// Use after free checker.
//===--------------------------------------------------------------------===//

bool Checker::isUseAfterFree(Instruction *Inst) {
  bool UseAfterFree = false;
  DFS(CheckerMaps::ForwardDependencyMap, Inst, [&UseAfterFree](Instruction *currInst) {
    if (currInst->getOpcode() == Instruction::Store) {
      UseAfterFree = !(isa<ConstantPointerNull>(currInst->getOperand(0)));
      return true;
    }
    if (currInst->getOpcode() == Instruction::Call) {
      auto *callInst = dyn_cast<CallInst>(currInst);
      Function *calledFunc = callInst->getCalledFunction();
      UseAfterFree = calledFunc->getName() != "free";
      return true;
    }
    return false;
  });

  return UseAfterFree;
}

InstructionPairPtr::Ptr Checker::UseAfterFreeChecker() {
//  Instruction *useAfterFreeInst = nullptr;
//
//  for (auto &pair : MallocFreePairs) {
//    Instruction *mallocInst = pair.get()->first;
//    Instruction *freeInst = pair.get()->second;
//    bool foundUsageAfterFree =
//        DFS(CheckerMaps::ForwardFlowMap, freeInst, [mallocInst, freeInst, &useAfterFreeInst, this](Instruction *inst) {
//          if (inst != freeInst && buildBackwardDependencyPath(inst, mallocInst)) {
//            useAfterFreeInst = inst;
//            return true;
//          }
//          return false;
//        });
//
//    if (!foundUsageAfterFree) {
//      continue;
//    }
//
//    if (useAfterFreeInst && isUseAfterFree(useAfterFreeInst)) {
//      return InstructionPairPtr::makePair(freeInst, useAfterFreeInst);
//    }
//  }
  return {};
}

//===--------------------------------------------------------------------===//
// Buffer overflow checker.
//===--------------------------------------------------------------------===//

unsigned int Checker::getFormatStringSize(GlobalVariable *var) {
  if (Constant * formatStringConst = var->getInitializer()) {
    if (ConstantDataArray * formatArray = dyn_cast<ConstantDataArray>(formatStringConst)) {
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

InstructionPairPtr::Ptr Checker::ScanfValidation() {
  if (callInstructions.empty() ||
      callInstructions.find("scanf") == callInstructions.end()) {
    return {};
  }
  for (Instruction *cInst : callInstructions.at("scanf")) {
    auto *callInst = dyn_cast<CallInst>(cInst);

    Value *formatStringAgr = callInst->getOperand(0);
    auto *bufArgInst = dyn_cast<Instruction>(callInst->getOperand(1));
    auto *bufGEP = dyn_cast<GetElementPtrInst>(bufArgInst);

    Module *M = bufGEP->getModule();
    APInt accumulateOffset;

    if (bufGEP->accumulateConstantOffset(M->getDataLayout(), accumulateOffset)) {
      errs() << "offset: " << accumulateOffset << "\n";
    } else {
      errs() << "chkpav\n";
    }

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

InstructionPairPtr::Ptr Checker::OutOfBoundsAccessChecker() {
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return {};
  }
//  for (Instruction *callInst : callInstructions.at("malloc")) {
//    std::unique_ptr<MallocInfo> mInfo = std::make_unique<MallocInfo>(callInst);
//  }
  return {};
}

InstructionPairPtr::Ptr Checker::BuffOverflowChecker() {
  InstructionPairPtr::Ptr scanfBOF = ScanfValidation();
  if (scanfBOF) {
    return scanfBOF;
  }
  InstructionPairPtr::Ptr outOfBoundAcc = OutOfBoundsAccessChecker();
  if (outOfBoundAcc) {
    return outOfBoundAcc;
  }
  return {};
}

//MallocInfo::MallocInfo(Instruction *Inst) {
//  mallocInst = Inst;
////  errs() << "malloc: " << *mallocInst << "\n";
//  size = mallocInst->getOperand(0);
////  errs() << "size: " << *size << "\n";
//  if (auto *sizeInst = dyn_cast<Instruction>(size)) {
////    errs() << "CAN cast to instruction\n";
//  }
//}

};