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
const std::string CallInstruction::Snprintf = "snprintf";
const std::string CallInstruction::Memset = "memset";
const std::string CallInstruction::Strcpy = "strcpy";
const std::string CallInstruction::Fopen = "fopen";
const std::string CallInstruction::Fprint = "fprint";

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

int64_t CalculateOffsetInBits(GetElementPtrInst *gep) {
  Module *module = gep->getModule();
  DataLayout dataLayout(module->getDataLayout());
  SmallVector<Value *, 8> indices(gep->op_begin() + 1, gep->op_end());

  Type *elemTy_ptr = gep->getPointerOperandType();
  uint64_t offset = SIZE_MAX;
  if (elemTy_ptr->getPointerElementType()->isArrayTy()) {
    elemTy_ptr = gep->getPointerOperandType();
  }

  offset = dataLayout.getIndexedOffsetInType(elemTy_ptr->getPointerElementType(), indices);
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

void FuncInfo::AddEdge(AnalyzerMap mapID, Value *source, Value *destination) {
  auto *map = SelectMap(mapID);
  map->operator[](source).insert(destination);
}

bool FuncInfo::HasEdge(AnalyzerMap mapID, Value *source, Value *destination) {
  auto *map = SelectMap(mapID);
  auto sourceIt = map->find(source);
  if (sourceIt != map->end()) {
    return sourceIt->second.find(destination) != sourceIt->second.end();
  }
  return false;
}

void FuncInfo::RemoveEdge(AnalyzerMap mapID, Value *source, Value *destination) {
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
    if (!isa<Instruction>(firstOp) || !isa<Instruction>(secondOp)) {
      return false;
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
  errs() << *gepInst << " - gep\n";
  errs() << *firstOp << "\n";
  // TODO: check this later
  if (backwardDependencyMap.find(firstOp) == backwardDependencyMap.end()) {
    return false;
  }

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
    errs() << "process " << *mallocInst << "\n";
    for (auto &dependentVal : forwardDependencyMap[mallocInst]) {
      auto *dependentInst = dyn_cast<Instruction>(dependentVal);
      if (dependentInst->getOpcode() == Instruction::GetElementPtr) {
        ProcessGepInsts(dependentInst);
      }
    }
  }
}

void FuncInfo::CollectMallocedObjs() {
  errs() << "}}}}}}}\n";
  if (callInstructions.empty() ||
      callInstructions.find(CallInstruction::Malloc) == callInstructions.end()) {
    return;
  }
  errs() << "}}}}}}}\n";

  for (Instruction *mallocInst : callInstructions[CallInstruction::Malloc]) {
    DFS(AnalyzerMap::ForwardDependencyMap, mallocInst, [mallocInst, this](Value *current) {
      auto *currentInst = dyn_cast<Instruction>(current);
      errs() << "curr: " << *currentInst << "\n";
      if (currentInst->getOpcode() == Instruction::Alloca) {
        auto obj = std::make_shared<MallocedObject>(currentInst);
        obj->setMallocCall(mallocInst);
        mallocedObjs[mallocInst] = obj;
        return true;
      }
      if (currentInst->getOpcode() == Instruction::GetElementPtr) {
        errs() << "MMM\n";
        auto obj = std::make_shared<MallocedObject>(currentInst);
        errs() << "MMM\n";

        obj->setMallocCall(mallocInst);
        errs() << "MMM\n";

        auto *gep = dyn_cast<GetElementPtrInst>(currentInst);
        errs() << "MMM\n";

        size_t offset = CalculateOffsetInBits(gep);
        errs() << "MMM\n";

        printMap(AnalyzerMap::ForwardDependencyMap);

        if (forwardDependencyMap.find(current) == forwardDependencyMap.end()) {
          errs() << "lav ches\n";
        }

        // nextInst = parentInst. Alloca is the next to gep, see updateDependencies()
        auto *next = dyn_cast<Instruction>(*(forwardDependencyMap[current].begin()));
        errs() << "MMM\n";

        obj->setOffset(FindSuitableObj(next), offset);

        errs() << "MMM\n";
        mallocedObjs[mallocInst] = obj;
        errs() << "MMM\n";

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
  errs() << "EEEEE\n";
  ConstructDataDeps();
  errs() << "EEEEE\n";

  CollectMallocedObjs();
  errs() << "EEEEE\n";

  ConstructFlowDeps();
  errs() << "EEEEE\n";

  DetectLoops();
  errs() << "EEEEE\n";

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
                   const std::function<bool(Value *)> &continueCondition) {
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
      }
    }
  }
  return false;
}

Instruction *FuncInfo::GetDeclaration(Instruction *inst) {
  Instruction *declaration = nullptr;
  DFS(AnalyzerMap::BackwardDependencyMap, inst, [&declaration](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);
    if (currInst->getOpcode() == Instruction::Alloca) {
      declaration = currInst;
      return true;
    }
    return false;
  });
  return declaration;
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

// Todo: Perhaps do iteratively
bool FuncInfo::DetectLoopsUtil(Function *f, BasicBlock *BB, std::unordered_set<BasicBlock *> &visited,
                               std::unordered_set<BasicBlock *> &recStack) {
  visited.insert(BB);
  recStack.insert(BB);

  for (BasicBlock *succ : successors(BB)) {
    // If the successor is not visited, perform DFS on it
    if (visited.find(succ) == visited.end()) {
      if (DetectLoopsUtil(f, succ, visited, recStack)) {
        return true; // Loop found
      }
    }

      // If the successor is in the recursion stack, it is a back edge, indicating a loop
    else if (recStack.find(succ) != recStack.end()) {
//      errs() << "Loop detected in function " << f->getName() << ":\n";
//      errs() << "  From: " << *BB->getTerminator() << "  To: " << *succ->getFirstNonPHIOrDbg() << "\n";
      auto *latch = dyn_cast<Instruction>(BB->getTerminator());
      if (latch->getOpcode() != Instruction::Br) {
        continue;
      }

      BasicBlock *headerBB = succ;
      loopInfo = std::make_unique<LoopsInfo>(headerBB, latch);
      return true;
    }
  }

  recStack.erase(BB);
  return false;
}

void FuncInfo::DetectLoops() {
  std::unordered_set<BasicBlock *> visited;
  std::unordered_set<BasicBlock *> recStack;

  for (BasicBlock &BB : *function) {
    if (visited.find(&BB) == visited.end() && DetectLoopsUtil(function, &BB, visited, recStack)) {
      SetLoopHeaderInfo();
      SetLoopScope();
      return;
    }
  }
}

void FuncInfo::SetLoopScope() {
  BasicBlock *header = loopInfo->GetHeader();
  Instruction *latch = loopInfo->GetLatch();

  std::vector<BasicBlock *> scope;
  bool startLoopScope = false;
  BasicBlock *endLoop = latch->getParent();
  for (auto &bb : *function) {
    if (&bb == header) {
      startLoopScope = true;
    }
    if (!startLoopScope) {
      continue;
    }
    scope.push_back(&bb);
    if (&bb == endLoop) {
      break;
    }
  }

  loopInfo->SetScope(scope);
}

void FuncInfo::SetLoopHeaderInfo() {
  Instruction *condition = loopInfo->GetCondition();

  auto *opInst1 = dyn_cast<Instruction>(condition->getOperand(0));
  Instruction *loopVar = GetDeclaration(opInst1);
  loopInfo->SetLoopVar(loopVar);

  Value* loopSize = condition->getOperand(1);
  if (auto *opInst2 = dyn_cast<Instruction>(loopSize)) {
    loopSize = dyn_cast<Value>(GetDeclaration(opInst2));
  }
  loopInfo->SetLoopSize(loopSize);
}

std::shared_ptr<LoopsInfo> FuncInfo::GetLoopInfo() {
  return loopInfo;
}

void FuncInfo::SetLoopRange(std::pair<int64_t, int64_t> range) {
  // validate only ICMP_SLT and ICMP_SLE
  auto predicate = loopInfo->GetPredicate();
  if (predicate == CmpInst::ICMP_SLT) {
    --range.second;
  }
  loopInfo->SetRange(range);
}

void FuncInfo::CreateBBCFG() {
  for (BasicBlock &BB : *function) {
    bbCFG[&BB]; // This will initialize an empty set for the basic block

    for (BasicBlock *Successor : successors(&BB)) {
      bbCFG[&BB].insert(Successor);
    }
  }
}

void FuncInfo::printBBCFG() {

  for (auto &pair : bbCFG) {
    BasicBlock *to = pair.first;
    std::unordered_set<BasicBlock *> successors = pair.second;

    for (BasicBlock *successor : successors) {
      errs() << *to << "-->" << *successor << "\n";
//      outs() << Successor->getName() << " ";
    }
  }
}

//void FuncInfo::DetectLoopsUtil(Value *u, std::unordered_set<Value *> &discovered,
//                               std::unordered_set<Value *> &finished) {
//
//  discovered.insert(u);
//
////  errs() << "discovered : ";
////  for (Value * val : discovered) {
////    errs() << *val << ", ";
////  } errs() << "\n\n";
////
////  errs() << "finished : ";
////  for (Value * val : finished) {
////    errs() << *val << ", ";
////  }errs() << "\n\n";
//
//  for (Value *v : forwardFlowMap[u]) {
////    errs() << "\ttmp: " << *v << "\n";
//    // Detect cycles
//    if (discovered.find(v) != discovered.end()) {
//      auto* latch = dyn_cast<Instruction>(u);
//      if (latch->getOpcode() != Instruction::Br) {
//        break;
//      }
//      auto* header = dyn_cast<Instruction>(v);
//      BasicBlock* headerBB = header->getParent();
//      loopInfo = std::make_unique<LoopsInfo>(headerBB, latch);
//      errs() << "Cycle detected: found a back edge from " << *u << " to " << *v << "\n";
//      break;
//    }
//
//    if (finished.find(v) == finished.end()) {
//      DetectLoopsUtil(v, discovered, finished);
//    }
//
//    discovered.erase(u);
//    finished.insert(u);
//  }
//}

//void FuncInfo::DetectLoops() {
//  std::unordered_set<Value *> discovered;
//  std::unordered_set<Value *> finished;
//
//  Instruction* latch = nullptr;
//  for (const auto &entry : forwardFlowMap) {
//    Value *u = entry.first;
//    if (entry.second.empty()) {
//      continue;
//    }
//    if (discovered.find(u) == discovered.end() && finished.find(u) == finished.end()) {
//      DetectLoopsUtil(u, discovered, finished);
//      if (loopInfo) {
//        SetLoopHeaderInfo();
//        SetLoopScope();
//        return;
//      }
//    }
//  }

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
