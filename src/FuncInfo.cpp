#include "FuncInfo.h"

namespace llvm {

const std::string CallInstruction::Malloc = "malloc";
const std::string CallInstruction::Free = "free";
const std::string CallInstruction::Scanf = "__isoc99_scanf";
const std::string CallInstruction::Memcpy = "llvm.memcpy.p0i8.p0i8.i64";
const std::string CallInstruction::Strlen = "strlen";
const std::string CallInstruction::Time = "time";
const std::string CallInstruction::Srand = "srand";
const std::string CallInstruction::Rand = "rand";
const std::string CallInstruction::Printf = "printf";

bool IsCallWithName(Instruction *inst, const std::string &name) {
  if (auto *callInst = dyn_cast<CallInst>(inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName().str() == name;
  }
  return false;
}

MallocedObject::MallocedObject(Instruction *inst) {
  base = inst;
}

MallocedObject::MallocedObject(MallocedObject *obj) {}

void MallocedObject::setOffset(MallocedObject *obj, size_t num) {
  main = obj;
  offset = num;
}

size_t MallocedObject::getOffset() const {
  return offset;
}

bool MallocedObject::isMallocedWithOffset() const {
  return offset != SIZE_MAX;
}

void MallocedObject::setMallocCall(Instruction *malloc) {
  mallocFree.first = malloc;
}

void MallocedObject::addFreeCall(Instruction *free) {
  mallocFree.second.push_back(free);
}

MallocedObject *MallocedObject::getMainObj() const {
  return main;
}

Instruction *MallocedObject::getMallocCall() const {
  return mallocFree.first;
}

std::vector<Instruction *> MallocedObject::getFreeCalls() const {
  return mallocFree.second;
}

Instruction *MallocedObject::getBaseInst() {
  return base;
}

bool MallocedObject::isDeallocated() const {
  return !mallocFree.second.empty();
}

int64_t CalculateOffset(GetElementPtrInst *gep) {
  Module *module = gep->getModule();
  DataLayout dataLayout(module->getDataLayout());
  SmallVector<Value *, 8> indices(gep->op_begin() + 1, gep->op_end());

  Type *elemTy_ptr = gep->getPointerOperandType();
  uint64_t offset = SIZE_MAX;
  if (elemTy_ptr->getPointerElementType()->isArrayTy()) {
    elemTy_ptr = gep->getPointerOperandType();
  }

  offset = dataLayout.getIndexedOffsetInType(elemTy_ptr->getPointerElementType(), indices);
//         << "| " << dataLayout.getPointerTypeSizeInBits(elemTy_ptr) << "\n";
//  errs() << "PointerTypeSize: "  << dataLayout.getPointerTypeSize(elemTy_ptr) << "\n";
//  errs() << "getIndexSize: "  << dataLayout.getIndexSize(offset) << "\n";
//  errs() << "getTypeAllocSize: "  << dataLayout.getTypeAllocSize(elemTy_ptr) << "\n";
//  errs() << "getTypeAllocSize: "  << dataLayout.getInde << "\n";
  return static_cast<int64_t>(offset);
}

Instruction *GetCmpNullOperand(Instruction *icmp) {
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

void FuncInfo::CollectCalls(Instruction *callInst) {
  if (IsCallWithName(callInst, CallInstruction::Malloc)) {
    callInstructions[CallInstruction::Malloc].push_back(callInst);
  }
  if (IsCallWithName(callInst, CallInstruction::Free)) {
    callInstructions[CallInstruction::Free].push_back(callInst);
  }
  if (IsCallWithName(callInst, CallInstruction::Scanf)) {
    callInstructions[CallInstruction::Scanf].push_back(callInst);
  }
  if (IsCallWithName(callInst, CallInstruction::Memcpy)) {
    callInstructions[CallInstruction::Memcpy].push_back(callInst);
  }
}

std::unordered_map<Value *, std::unordered_set<Value *>> *FuncInfo::SelectMap(AnalyzerMap mapID) {
  switch (mapID) {
  case AnalyzerMap::ForwardDependencyMap:return &forwardDependencyMap;
  case AnalyzerMap::BackwardDependencyMap:return &backwardDependencyMap;
  case AnalyzerMap::ForwardFlowMap:return &forwardFlowMap;
  case AnalyzerMap::BackwardFlowMap:return &backwardFlowMap;
  }
  llvm::report_fatal_error("Not found corresponding map.");
}

void FuncInfo::AddEdge(AnalyzerMap mapID, Value * source, Value * destination) {
  auto *map = SelectMap(mapID);
  map->operator[](source).insert(destination);
}

bool FuncInfo::HasEdge(AnalyzerMap mapID, Value * source, Value * destination) {
  auto *map = SelectMap(mapID);
  auto sourceIt = map->find(source);
  if (sourceIt != map->end()) {
    return sourceIt->second.find(destination) != sourceIt->second.end();
  }
  return false;
}

void FuncInfo::RemoveEdge(AnalyzerMap mapID, Value * source, Value * destination) {
  auto *map = SelectMap(mapID);
  if (HasEdge(mapID, source, destination)) {
    map->operator[](source).erase(destination);
  }
}

bool FuncInfo::ProcessStoreInsts(Instruction *storeInst) {
  auto *sInst = dyn_cast<StoreInst>(storeInst);
  Value *firstOp = sInst->getValueOperand();
  Value *secondOp = sInst->getPointerOperand();
  if (!isa<Constant>(firstOp)) {
    if (auto *arg = dyn_cast<Argument>(firstOp)) {
      if (!HasEdge(AnalyzerMap::ForwardDependencyMap, arg, secondOp)) {
        AddEdge(AnalyzerMap::ForwardDependencyMap, arg, secondOp);
      }
      return true;
    }
    auto *fromInst = dyn_cast<Instruction>(firstOp);
    auto *toInst = dyn_cast<Instruction>(secondOp);
    if (!HasEdge(AnalyzerMap::ForwardDependencyMap, fromInst, toInst)) {
      AddEdge(AnalyzerMap::ForwardDependencyMap, fromInst, toInst);
    }
    if (!HasEdge(AnalyzerMap::BackwardDependencyMap, toInst, fromInst)) {
      AddEdge(AnalyzerMap::BackwardDependencyMap, toInst, fromInst);
    }
    return true;
  }
  return false;
}

bool FuncInfo::ProcessGepInsts(Instruction *gInst) {
  auto *gepInst = dyn_cast<GetElementPtrInst>(gInst);
  auto *firstOp = dyn_cast<Instruction>(gepInst->getOperand(0));
  // Go up to the 'alloca' instruction
  for (Value *predecessorVal : backwardDependencyMap.at(firstOp)) {
    auto predecessorInst = dyn_cast<Instruction>(predecessorVal);
    if (predecessorInst->getOpcode() == Instruction::Alloca) {
      RemoveEdge(AnalyzerMap::ForwardDependencyMap, predecessorInst, firstOp);
      AddEdge(AnalyzerMap::ForwardDependencyMap, gInst, predecessorInst);

      RemoveEdge(AnalyzerMap::BackwardDependencyMap, firstOp, predecessorInst);
      AddEdge(AnalyzerMap::BackwardDependencyMap, predecessorInst, gInst);
      return true;
    }
  }
  return false;
}

void FuncInfo::UpdateDataDeps() {
  if (callInstructions.empty() ||
      callInstructions.find(CallInstruction::Malloc) == callInstructions.end()) {
    return;
  }

  for (Instruction *mallocInst : callInstructions.at(CallInstruction::Malloc)) {
    for (auto &dependentVal : forwardDependencyMap[mallocInst]) {
      auto* dependentInst = dyn_cast<Instruction>(dependentVal);
      if (dependentInst->getOpcode() == Instruction::GetElementPtr) {
        ProcessGepInsts(dependentInst);
      }
    }
  }
}

void FuncInfo::CollectMallocedObjs() {
  if (callInstructions.empty() ||
      callInstructions.find(CallInstruction::Malloc) == callInstructions.end()) {
    return;
  }

  for (Instruction *mallocInst : callInstructions[CallInstruction::Malloc]) {
    DFS(AnalyzerMap::ForwardDependencyMap, mallocInst, [mallocInst, this](Value *current) {
      auto* currentInst = dyn_cast<Instruction>(current);
      if (currentInst->getOpcode() == Instruction::Alloca) {
        auto obj = std::make_shared<MallocedObject>(currentInst);
        obj->setMallocCall(mallocInst);
        mallocedObjs[mallocInst] = obj;
        return true;
      }
      if (currentInst->getOpcode() == Instruction::GetElementPtr) {
        auto obj = std::make_shared<MallocedObject>(currentInst);
        obj->setMallocCall(mallocInst);
        auto *gep = dyn_cast<GetElementPtrInst>(currentInst);
        size_t offset = CalculateOffset(gep);

        // nextInst = parentInst. Alloca is the next to gep, see updateDependencies()
        auto *next = dyn_cast<Instruction>(*(forwardDependencyMap[current].begin()));
        obj->setOffset(FindSuitableObj(next), offset);
        mallocedObjs[mallocInst] = obj;
        return true;
      }
      return false;

    });
  }
}

void FuncInfo::CreateEdgesInBB(BasicBlock *bb) {
  for (auto &inst : *bb) {
    if (inst.isDebugOrPseudoInst()) {
      continue;
    }
    Instruction *next = inst.getNextNonDebugInstruction();
    if (next) {
      AddEdge(AnalyzerMap::ForwardFlowMap, &inst, next);
    }
  }
}

void FuncInfo::ConstructFlowDeps() {
  // TODO: construct also backward flow graph
  for (auto &bb : *function) {
    CreateEdgesInBB(&bb);
    Instruction *lastInBB = &(bb.back());

    if (lastInBB->getOpcode() == Instruction::Br) {
      auto *branchInst = dyn_cast<BranchInst>(lastInBB);
      AddEdge(AnalyzerMap::ForwardFlowMap, lastInBB, branchInst->getSuccessor(0)->getFirstNonPHIOrDbg());
      if (branchInst->isConditional()) {
        AddEdge(AnalyzerMap::ForwardFlowMap, lastInBB, branchInst->getSuccessor(1)->getFirstNonPHIOrDbg());
      }
    }
  }
}

void FuncInfo::ConstructDataDeps() {
  for (auto &bb : *function) {
    for (auto &inst : bb) {
      if (inst.getOpcode() == Instruction::Call) {
        CollectCalls(&inst);
      }

      auto uses = inst.uses();
      if (uses.empty()) {
        continue;
      }
      for (auto &use : uses) {
        if (auto *dependentInst = dyn_cast<llvm::Instruction>(use.getUser())) {
          if (dependentInst->getOpcode() == Instruction::Store &&
              ProcessStoreInsts(dependentInst)) {
            continue;
          }

          AddEdge(AnalyzerMap::ForwardDependencyMap, &inst, dependentInst);
          AddEdge(AnalyzerMap::BackwardDependencyMap, dependentInst, &inst);
        }
      }
    }
  }
  UpdateDataDeps();
}

FuncInfo::FuncInfo(llvm::Function *func) {
  function = func;
  const BasicBlock &lastBB = *(--(func->end()));
  if (!lastBB.empty()) {
    ret = const_cast<Instruction *>(&*(--(lastBB.end())));
  }

  ConstructDataDeps();
  CollectMallocedObjs();
  ConstructFlowDeps();
}

MallocedObject *FuncInfo::FindSuitableObj(Instruction *base) {
  for (auto &objPair : mallocedObjs) {
    if (objPair.second->getBaseInst() == base) {
      return objPair.second.get();
    }
  }
  return nullptr;
}

//// Todo: improve this (arguments, architecture)
bool FuncInfo::DFS(AnalyzerMap mapID,
                   Instruction *start,
                   const std::function<bool(Value *)> &terminationCondition,
                   const std::function<bool(Value *)> &continueCondition,
                   const std::function<void(Value *)> &getLoopInfo) {
  auto *map = SelectMap(mapID);

  std::unordered_set<Value *> visitedInstructions;
  std::stack<Value *> dfsStack;
  dfsStack.push(start);

  while (!dfsStack.empty()) {
    Value *current = dfsStack.top();
    dfsStack.pop();

    if (terminationCondition(current)) {
      return true;
    }

    visitedInstructions.insert(current);

    if (continueCondition && continueCondition(current)) {
      continue;
    }

    if (map->operator[](current).empty()) {
      // change path
    }

    for (Value *next : map->operator[](current)) {
      if (visitedInstructions.find(next) == visitedInstructions.end()) {
        dfsStack.push(next);
      } else if (getLoopInfo) {
        getLoopInfo(current);
      }
    }
  }
  return false;
}

void FuncInfo::printMap(AnalyzerMap mapID) {
  auto *map = SelectMap(mapID);

  for (auto &pair : *map) {
    Value *to = pair.first;
    std::unordered_set<Value *> successors = pair.second;

    for (Value *successor : successors) {
      errs() << *to << "-->" << *successor << "\n";
    }
  }
}

std::vector<Instruction *> FuncInfo::getCalls(const std::string &funcName) {
  if (callInstructions.empty() ||
      callInstructions.find(funcName) == callInstructions.end()) {
    return {};
  }
  return callInstructions[funcName];
}

Instruction *FuncInfo::getRet() const {
  return ret;
}

//// TODO: write iterative algorithm to avoid stack overflow
//void FuncInfo::FindPaths(std::unordered_set<Instruction *> &visitedInsts,
//                             std::vector<std::vector<Instruction *>> &paths,
//                             std::vector<Instruction *> &currentPath,
//                             Instruction *from,
//                             Instruction *to) {
//  if (visitedInsts.find(from) != visitedInsts.end()) {
//    return;
//  }
//  visitedInsts.insert(from);
//  currentPath.push_back(from);
//
//  if (from == to) {
//    paths.push_back(currentPath);
//    visitedInsts.erase(from);
//    currentPath.pop_back();
//    return;
//  }
//
//  for (Instruction *next : forwardFlowMap[from]) {
//    FindPaths(visitedInsts, paths, currentPath, next, to);
//  }
//  currentPath.pop_back();
//  visitedInsts.erase(from);
//}
//
//void FuncInfo::CollectPaths(Instruction *from, Instruction *to,
//                                std::vector<std::vector<Instruction *>> &allPaths) {
//  std::unordered_set<Instruction *> visitedInsts;
//  std::vector<Instruction *> currentPath;
//  FindPaths(visitedInsts, allPaths, currentPath, from, to);
//}

//bool FuncInfo::HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to) {
//  return DFS(mapID, from, [to](Instruction *inst) { return inst == to; });
//}

//bool FuncInfo::FindSpecialDependenceOnArg(Argument *arg,
//                                          size_t argNum,
//                                          const std::function<bool(Instruction *)> &type) {
//  if (!GetArgsNum() || argNum >= GetArgsNum()) {
//    return false;
//  }
//
//  if (argumentsMap.find(arg) == argumentsMap.end()) {
//    return false;
//  }
//
//  // Todo: if there are any callInfo need to validate them, too.
//  Instruction *dependent = argumentsMap[arg];
//  return HasPathToSpecificTypeOfInst(AnalyzerMap::ForwardDependencyMap,
//                                     dependent,
//                                     [&type](Instruction *curr) {
//                                       return type(curr);
//                                     });
//}

//bool FuncInfo::HasPathToSpecificTypeOfInst(AnalyzerMap mapID, Instruction *from,
//                                           const std::function<bool(Instruction *)> &type,
//                                           CallDataDepInfo *callInfo) {
//  Instruction *previous = nullptr;
//  return DFS(mapID, from, [mapID, type, callInfo, &previous](Instruction *curr) {
//    if (callInfo && mapID == AnalyzerMap::ForwardDependencyMap) {
//      if (auto *call = dyn_cast<CallInst>(curr)) {
//        callInfo->Init(call, previous);
//      }
//    }
//    previous = curr;
//    return type(curr);
//  });
//}

//std::vector<Instruction *> FuncInfo::CollectAllGeps(Instruction *malloc) {
//  if (forwardDependencyMap.find(malloc) == forwardDependencyMap.end()) {
//    return {};
//  }
//
//  std::vector<Instruction *> geps = {};
//  DFS(AnalyzerMap::ForwardDependencyMap, malloc, [&geps](Instruction *curr) {
//    if (curr->getOpcode() == Instruction::GetElementPtr) {
//      geps.push_back(curr);
//    }
//    return false;
//  });
//
//  return geps;
//}

//size_t FuncInfo::GetArgsNum() {
//  return static_cast<size_t>(function->getFunctionType()->getNumParams());
//}

//std::vector<Instruction *> FuncInfo::CollectSpecialDependenciesOnArg(Argument *arg,
//                                                                     size_t argNum,
//                                                                     const std::function<bool(Instruction *)> &type) {
//  if (!GetArgsNum() || argNum >= GetArgsNum()) {
//    return {};
//  }
//
//  if (argumentsMap.find(arg) == argumentsMap.end()) {
//    return {};
//  }
//
//  // Todo: if there are any callInfo need to validate them, too.
//  Instruction *dependent = argumentsMap[arg];
//  std::vector<Instruction *> insts = {};
//
//  return CollectAllDepInst(dependent,
//                           [&type](Instruction *curr) {
//                             return type(curr);
//                           });
//}
//
//std::vector<Instruction *> FuncInfo::CollectAllDepInst(Instruction *from,
//                                                       const std::function<bool(Instruction *)> &type,
//                                                       CallDataDepInfo *callInfo) {
//
//  if (forwardDependencyMap.find(from) == forwardDependencyMap.end()) {
//    return {};
//  }
//
//  std::vector<Instruction *> insts = {};
//  Instruction *previous = nullptr;
//  DFS(AnalyzerMap::ForwardDependencyMap,
//      from, [&insts, &type, callInfo, &previous](Instruction *curr) {
//        if (callInfo) {
//          if (auto *call = dyn_cast<CallInst>(curr)) {
//            callInfo->Init(call, previous);
//          }
//        }
//        if (type(curr)) {
//          insts.push_back(curr);
//        }
//        previous = curr;
//        return false;
//      });
//
//  return insts;
//}




} // namespace llvm
