#include "FuncAnalyzer.h"

namespace llvm {

const std::string CallInstruction::Malloc = "malloc";
const std::string CallInstruction::Free = "free";
const std::string CallInstruction::Scanf = "__isoc99_scanf";
const std::string CallInstruction::Memcpy = "llvm.memcpy.p0i8.p0i8.i64";
const std::string CallInstruction::Strlen = "strlen";

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
  SmallVector<Value*, 8> indices(gep->op_begin() + 1, gep->op_end());

  Type* elemTy_ptr = gep->getPointerOperandType();
  errs() << "\nType " << *elemTy_ptr << "\n";
  uint64_t offset = SIZE_MAX;
  if (elemTy_ptr->getPointerElementType()->isArrayTy()) {
    elemTy_ptr = gep->getPointerOperandType()->getPointerElementType();
    errs() << *elemTy_ptr << "type arr\n";
    errs() << *elemTy_ptr->getArrayElementType() << "| " << dataLayout.getTypeAllocSize(elemTy_ptr->getArrayElementType()) <<"\n";
  } else {
    errs() << "Type " << *elemTy_ptr->getPointerElementType() << "\n";
  }

  offset = dataLayout.getIndexedOffsetInType(elemTy_ptr->getPointerElementType(), indices);
  errs() << "\toffset: " << offset << "| type size" << elemTy_ptr->getScalarSizeInBits()
         << "| " << dataLayout.getPointerTypeSizeInBits(elemTy_ptr) << "\n";
  errs() << "PointerTypeSize: "  << dataLayout.getPointerTypeSize(elemTy_ptr) << "\n";
  errs() << "getIndexSize: "  << dataLayout.getIndexSize(offset) << "\n";
  errs() << "getTypeAllocSize: "  << dataLayout.getTypeAllocSize(elemTy_ptr) << "\n";
//  errs() << "getTypeAllocSize: "  << dataLayout.getInde << "\n";
  return static_cast<int64_t>(offset);
}


void FuncAnalyzer::CollectCalls(Instruction *callInst) {
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

std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *FuncAnalyzer::SelectMap(AnalyzerMap mapID) {
  switch (mapID) {
  case AnalyzerMap::ForwardDependencyMap:return &forwardDependencyMap;
  case AnalyzerMap::BackwardDependencyMap:return &backwardDependencyMap;
  case AnalyzerMap::ForwardFlowMap:return &forwardFlowMap;
  case AnalyzerMap::BackwardFlowMap:return &backwardFlowMap;
  }
  llvm::report_fatal_error("Not found corresponding map.");
}

void FuncAnalyzer::AddEdge(AnalyzerMap mapID, Instruction *source, Instruction *destination) {
  auto *map = SelectMap(mapID);
  map->operator[](source).insert(destination);
}

bool FuncAnalyzer::HasEdge(llvm::AnalyzerMap mapID, Instruction *source, Instruction *destination) {
  auto *map = SelectMap(mapID);
  auto sourceIt = map->find(source);
  if (sourceIt != map->end()) {
    return sourceIt->second.find(destination) != sourceIt->second.end();
  }
  return false;
}

void FuncAnalyzer::RemoveEdge(AnalyzerMap mapID, Instruction *source, Instruction *destination) {
  auto *map = SelectMap(mapID);
  if (HasEdge(mapID, source, destination)) {
    map->operator[](source).erase(destination);
  }
}

bool FuncAnalyzer::ProcessStoreInsts(Instruction *storeInst) {
  auto *sInst = dyn_cast<StoreInst>(storeInst);
  Value *firstOp = sInst->getValueOperand();
  Value *secondOp = sInst->getPointerOperand();
  if (!isa<Constant>(firstOp)) {
    if (auto *arg = dyn_cast<Argument>(firstOp)) {
      argumentsMap[arg] = dyn_cast<Instruction>(secondOp);
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

bool FuncAnalyzer::ProcessGepInsts(Instruction *gInst) {
  auto *gepInst = dyn_cast<GetElementPtrInst>(gInst);
  auto *firstOp = dyn_cast<Instruction>(gepInst->getOperand(0));
  // Go up to the 'alloca' instruction
  for (Instruction *predecessor : backwardDependencyMap.at(firstOp)) {
    if (predecessor->getOpcode() == Instruction::Alloca) {
      RemoveEdge(AnalyzerMap::ForwardDependencyMap, predecessor, firstOp);
      AddEdge(AnalyzerMap::ForwardDependencyMap, gInst, predecessor);

      RemoveEdge(AnalyzerMap::BackwardDependencyMap, firstOp, predecessor);
      AddEdge(AnalyzerMap::BackwardDependencyMap, predecessor, gInst);
      return true;
    }
  }
  return false;
}

void FuncAnalyzer::UpdateDataDeps() {
  if (callInstructions.empty() ||
      callInstructions.find(CallInstruction::Malloc) == callInstructions.end()) {
    return;
  }

  for (Instruction *mallocInst : callInstructions.at(CallInstruction::Malloc)) {
    for (auto &dependentInst : forwardDependencyMap[mallocInst]) {
      if (dependentInst->getOpcode() == Instruction::GetElementPtr) {
        ProcessGepInsts(dependentInst);
      }
    }
  }
}

void FuncAnalyzer::CollectMallocedObjs() {
  if (callInstructions.empty() ||
      callInstructions.find(CallInstruction::Malloc) == callInstructions.end()) {
    return;
  }

  for (Instruction *mallocInst : callInstructions[CallInstruction::Malloc]) {
    DFS(AnalyzerMap::ForwardDependencyMap, mallocInst, [mallocInst, this](Instruction *current) {
      if (current->getOpcode() == Instruction::Alloca) {
        auto obj = std::make_shared<MallocedObject>(current);
        obj->setMallocCall(mallocInst);
        mallocedObjs[mallocInst] = obj;
        return true;
      }
      if (current->getOpcode() == Instruction::GetElementPtr) {
        auto obj = std::make_shared<MallocedObject>(current);
        obj->setMallocCall(mallocInst);
        auto *gep = dyn_cast<GetElementPtrInst>(current);
        size_t offset = CalculateOffset(gep);

        // nextInst = parentInst. Alloca is the next to gep, see updateDependencies()
        Instruction *next = *(forwardDependencyMap[current].begin());
        obj->setOffset(FindSuitableObj(next), offset);
        mallocedObjs[mallocInst] = obj;
        return true;
      }
      return false;

    });
  }
}

void FuncAnalyzer::CreateEdgesInBB(BasicBlock *bb) {
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

void FuncAnalyzer::ConstructFlowDeps() {
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

void FuncAnalyzer::ConstructDataDeps() {
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

FuncAnalyzer::FuncAnalyzer(llvm::Function *func) {
  function = func;
  const BasicBlock &lastBB = *(--(func->end()));
  if (!lastBB.empty()) {
    ret = const_cast<Instruction *>(&*(--(lastBB.end())));
  }

  ConstructDataDeps();
  CollectMallocedObjs();
  ConstructFlowDeps();
}

MallocedObject *FuncAnalyzer::FindSuitableObj(Instruction *base) {
  for (auto &objPair : mallocedObjs) {
    if (objPair.second->getBaseInst() == base) {
      return objPair.second.get();
    }
  }
  return nullptr;
}

// Todo: improve this (arguments, architecture)
bool FuncAnalyzer::DFS(AnalyzerMap mapID,
                       Instruction *start,
                       const std::function<bool(Instruction *)> &terminationCondition,
                       const std::function<bool(Instruction *)> &continueCondition,
                       const std::function<void(Instruction *)> &getLoopInfo) {
  auto *map = SelectMap(mapID);

  std::unordered_set<Instruction *> visitedInstructions;
  std::stack<Instruction *> dfsStack;
  dfsStack.push(start);

  while (!dfsStack.empty()) {
    Instruction *current = dfsStack.top();
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

    for (Instruction *next : map->operator[](current)) {
      if (visitedInstructions.find(next) == visitedInstructions.end()) {
        dfsStack.push(next);
      } else if (getLoopInfo) {
        getLoopInfo(current);
      }
    }
  }
  return false;
}

void FuncAnalyzer::printMap(AnalyzerMap mapID) {
  auto *map = SelectMap(mapID);

  if (mapID == AnalyzerMap::ForwardDependencyMap && GetArgsNum()) {
    errs() << "ARG\n";
    for (auto &pair : argumentsMap) {
      errs() << *pair.first << "-->" << *pair.second << "\n";
    }
  }
  for (auto &pair : *map) {
    Instruction *to = pair.first;
    std::unordered_set<Instruction *> successors = pair.second;

    for (Instruction *successor : successors) {
      errs() << *to << "-->" << *successor << "\n";
    }
  }
}

std::vector<Instruction *> FuncAnalyzer::getCalls(const std::string &funcName) {
  if (callInstructions.empty() ||
      callInstructions.find(funcName) == callInstructions.end()) {
    return {};
  }
  return callInstructions[funcName];
}

Instruction *FuncAnalyzer::getRet() const {
  return ret;
}

// TODO: write iterative algorithm to avoid stack overflow
void FuncAnalyzer::FindPaths(std::unordered_set<Instruction *> &visitedInsts,
                             std::vector<std::vector<Instruction *>> &paths,
                             std::vector<Instruction *> &currentPath,
                             Instruction *from,
                             Instruction *to) {
  if (visitedInsts.find(from) != visitedInsts.end()) {
    return;
  }
  visitedInsts.insert(from);
  currentPath.push_back(from);

  if (from == to) {
    paths.push_back(currentPath);
    visitedInsts.erase(from);
    currentPath.pop_back();
    return;
  }

  for (Instruction *next : forwardFlowMap[from]) {
    FindPaths(visitedInsts, paths, currentPath, next, to);
  }
  currentPath.pop_back();
  visitedInsts.erase(from);
}

void FuncAnalyzer::CollectPaths(Instruction *from, Instruction *to,
                                std::vector<std::vector<Instruction *>> &allPaths) {
  std::unordered_set<Instruction *> visitedInsts;
  std::vector<Instruction *> currentPath;
  FindPaths(visitedInsts, allPaths, currentPath, from, to);
}

bool FuncAnalyzer::HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to) {
  return DFS(mapID, from, [to](Instruction *inst) { return inst == to; });
}

bool FuncAnalyzer::FindSpecialDependenceOnArg(Argument *arg,
                                              size_t argNum,
                                              const std::function<bool(Instruction *)> &type) {
  if (!GetArgsNum() || argNum >= GetArgsNum()) {
    return false;
  }

  if (argumentsMap.find(arg) == argumentsMap.end()) {
    return false;
  }

  // Todo: if there are any callInfo need to validate them, too.
  Instruction *dependent = argumentsMap[arg];
  return HasPathToSpecificTypeOfInst(AnalyzerMap::ForwardDependencyMap,
                                     dependent,
                                     [&type](Instruction *curr) {
                                       return type(curr);
                                     });
}

bool FuncAnalyzer::HasPathToSpecificTypeOfInst(AnalyzerMap mapID, Instruction *from,
                                               const std::function<bool(Instruction *)> &type,
                                               CallDataDepInfo *callInfo) {
  Instruction *previous = nullptr;
  return DFS(mapID, from, [mapID, type, callInfo, &previous](Instruction *curr) {
    if (callInfo && mapID == AnalyzerMap::ForwardDependencyMap) {
      if (auto *call = dyn_cast<CallInst>(curr)) {
        callInfo->Init(call, previous);
      }
    }
    previous = curr;
    return type(curr);
  });
}

std::vector<Instruction *> FuncAnalyzer::CollectAllGeps(Instruction *malloc) {
  if (forwardDependencyMap.find(malloc) == forwardDependencyMap.end()) {
    return {};
  }

  std::vector<Instruction *> geps = {};
  DFS(AnalyzerMap::ForwardDependencyMap, malloc, [&geps](Instruction *curr) {
    if (curr->getOpcode() == Instruction::GetElementPtr) {
      geps.push_back(curr);
    }
    return false;
  });

  return geps;
}

size_t FuncAnalyzer::GetArgsNum() {
  return static_cast<size_t>(function->getFunctionType()->getNumParams());
}

std::vector<Instruction *> FuncAnalyzer::CollectSpecialDependenciesOnArg(Argument *arg,
                                                                         size_t argNum,
                                                                         const std::function<bool(Instruction *)> &type) {
  if (!GetArgsNum() || argNum >= GetArgsNum()) {
    return {};
  }

  if (argumentsMap.find(arg) == argumentsMap.end()) {
    return {};
  }

  // Todo: if there are any callInfo need to validate them, too.
  Instruction *dependent = argumentsMap[arg];
  std::vector<Instruction *> insts = {};

  return CollectAllDepInst(dependent,
                           [&type](Instruction *curr) {
                             return type(curr);
                           });
}

std::vector<Instruction *> FuncAnalyzer::CollectAllDepInst(Instruction *from,
                                                           const std::function<bool(Instruction *)> &type,
                                                           CallDataDepInfo *callInfo) {

  if (forwardDependencyMap.find(from) == forwardDependencyMap.end()) {
    return {};
  }

  std::vector<Instruction *> insts = {};
  Instruction *previous = nullptr;
  DFS(AnalyzerMap::ForwardDependencyMap,
      from, [&insts, &type, callInfo, &previous](Instruction *curr) {
        if (callInfo) {
          if (auto *call = dyn_cast<CallInst>(curr)) {
            callInfo->Init(call, previous);
          }
        }
        if (type(curr)) {
          insts.push_back(curr);
        }
        previous = curr;
        return false;
      });

  return insts;
}


//void find_paths2(std::vector<std::vector<int>> &paths,
//                 std::vector<int> &path,
//                 std::vector<std::vector<int>> &answer,
//                 int from,
//                 int to) {
//
//  std::stack<int> s_path;
//  std::stack<int> s_index;
//  s_path.push(from);
//  s_index.push(0);
//
//  while (!s_path.empty()) {
//    int vertex = s_path.top();
//    int ind = s_index.top();
//    path.push_back(vertex);
//
//    if (vertex == to) {
//      paths.push_back(path);
//    }
//
//    if (ind < answer[vertex].size() &&
//        answer[vertex][ind] != -1) {
//
//      int tmp = answer[vertex][ind];
//      s_path.push(tmp);
//      s_index.push(0);
//    } else {
//      s_path.pop();
//      s_index.pop();
//      path.pop_back();
//      if (s_path.empty()) {
//        break;
//      }
//
//      vertex = s_path.top();
//      ind = s_index.top();
//      ++ind;
//
//      s_path.pop();
//      s_index.pop();
//      path.pop_back();
//
//      s_path.push(vertex);
//      s_index.push(ind);
//    }
//  }
//}

} // namespace llvm
