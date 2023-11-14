#include "Checker.h"

#include "llvm/IR/GlobalVariable.h"

#include <sstream>
#include <queue>
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
          callInstructions["malloc"].push_back(&Inst);
        }
        if (isFreeCall(&Inst)) {
          callInstructions["free"].push_back(&Inst);
        }
        if (isScanfCall(&Inst)) {
          callInstructions["scanf"].push_back(&Inst);
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

  const BasicBlock &lastBB = *(--(Func->end()));
  if (!lastBB.empty()) {
    RET = const_cast<Instruction *>(&*(--(lastBB.end())));
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

MallocedObject *Checker::findSuitableObj(Instruction *baseInst) {
  for (auto &ObjPair : MallocedObjs) {
    if (ObjPair.second->getBaseInst() == baseInst) {
      return ObjPair.second.get();
    }
  }
  return nullptr;
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
        MallocedObjs[callInst] = Obj;
        return true;
      }
      if (curr->getOpcode() == Instruction::GetElementPtr) {
        auto Obj = std::make_shared<MallocedObject>(curr);
        Obj->setMallocCall(callInst);

        Module *M = curr->getModule();
        APInt accumulateOffset;
        auto *gep = dyn_cast<GetElementPtrInst>(curr);
        gep->accumulateConstantOffset(M->getDataLayout(), accumulateOffset);

        // nextInst = parentInst. Alloca is the next to gep, see updateDependencies()
        Instruction *nextInst = *(ForwardDependencyMap[curr].begin());
        Obj->setOffset(findSuitableObj(nextInst), accumulateOffset.getZExtValue());
        MallocedObjs[callInst] = Obj;
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

bool Checker::hasPath(CheckerMaps MapID, Instruction *from, Instruction *to) {
  return DFS(MapID, from, [to](Instruction *inst) { return inst == to; });
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

//void Checker::findAllPathsFromInstToRet(Instruction *startInst) {
//  std::vector<std::vector<Instruction*>> Paths;
//
//  std::unordered_set<Instruction *> visitedNodes;
//  std::stack<Instruction *> dfsStack;
//  dfsStack.push(startInst);
//
//  while (!dfsStack.empty()) {
//    Instruction *currInst = dfsStack.top();
//    dfsStack.pop();
//
//    if (currInst->getOpcode() == Instruction::Ret) {
//      errs() << "####################\n";
//      break;
//    }
//    errs() << "CURR: " <<  *currInst << "\n";
//    visitedNodes.insert(currInst);
//
//
//    for (Instruction *nextInst : ForwardFlowMap[currInst]) {
//      if (visitedNodes.find(nextInst) == visitedNodes.end()) {
//        dfsStack.push(nextInst);
//      }
//    }
//  }
//}


//void Checker::dfs(Instruction *current,
//                  Instruction *end,
//                  std::vector<Instruction *> &path,
//                  std::unordered_set<int> &visited) {
//  path.push_back(current);
//  visited.insert(current);
//
//  if (current == end) {
//    // Print or process the path as needed.
//    for (int vertex : path) {
//      errs() << vertex << " ";
//    }
//    std::cout << std::endl;
//  } else {
//    auto range = edges.equal_range(current);
//    for (auto it = range.first; it != range.second; ++it) {
//      int neighbor = it->second;
//      if (visited.find(neighbor) == visited.end()) {
//        dfs(neighbor, end, path, visited);
//      }
//    }
//  }
//
//  path.pop_back();
//  visited.erase(current);
//}

//void Checker::findPaths(std::vector<std::vector<Instruction*>>& paths, std::vector<Instruction*>& path,
//                        Instruction* from, Instruction* to) {
//    if (from == to) {
//      paths.push_back(path);
//      return;
//    }
//
//    for (Instruction *nextInst : ForwardFlowMap[from]) {
//      path.push_back(to);
//      findPaths(paths, path, from, answer[to][p]);
//      path.pop_back();
//    }
//  }
//}

// TODO: write iterative algorithm to avoid stack overflow
void Checker::collectPaths(std::unordered_set<Instruction *> &visitedInsts,
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

  for (Instruction *nextInst : ForwardFlowMap[from]) {
    collectPaths(visitedInsts, paths, currentPath, nextInst, to);
  }
  currentPath.pop_back();
  visitedInsts.erase(from);
}

void find_paths2(std::vector<std::vector<int>> &paths,
                 std::vector<int> &path,
                 std::vector<std::vector<int>> &answer,
                 int from,
                 int to) {

  std::stack<int> s_path;
  std::stack<int> s_index;
  s_path.push(from);
  s_index.push(0);

  while (!s_path.empty()) {
    int vertex = s_path.top();
    int ind = s_index.top();
    path.push_back(vertex);

    if (vertex == to) {
      paths.push_back(path);
    }

    if (ind < answer[vertex].size() &&
        answer[vertex][ind] != -1) {

      int tmp = answer[vertex][ind];
      s_path.push(tmp);
      s_index.push(0);
    } else {
      s_path.pop();
      s_index.pop();
      path.pop_back();
      if (s_path.empty()) {
        break;
      }

      vertex = s_path.top();
      ind = s_index.top();
      ++ind;

      s_path.pop();
      s_index.pop();
      path.pop_back();

      s_path.push(vertex);
      s_index.push(ind);
    }
  }
}

// Function to find all paths from Instruction A to Instruction B using DFS.
//std::vector<std::vector<Instruction*>> Checker::findAllPaths(Instruction* start, Instruction* end) {
//  // Vector to store all paths.
//  std::vector<std::vector<Instruction*>> allPaths;
//
//  // Stack to perform non-recursive DFS.
//  std::stack<std::pair<Instruction*, std::vector<Instruction*>>> pathStack;
//
//  // Set to keep track of visited instructions.
//  std::unordered_set<Instruction *> visitedInsts;
//
//  // Initialize the stack with the start node (Instruction A).
//  errs() << "{" << *start <<  "}, {" << *start << "}}\n";
//  pathStack.push({start, {start}});
//
//  while (!pathStack.empty()) {
//    auto currentPair = pathStack.top();
//    pathStack.pop();
//
//    Instruction* currentInst = currentPair.first;
//    std::vector<Instruction*> currentPath = currentPair.second;
//
//    // Mark the current instruction as visited.
//    visitedInsts.insert(currentInst);
//
//    // Check if the current instruction is the target (Instruction B).
//    if (currentInst == end) {
//      // If yes, add the current path to the list of all paths.
//      allPaths.push_back(currentPath);
//      currentPath.pop_back();
//      visitedInsts.erase(currentInst);
//      continue;
//    }
//
//    // Iterate over the successors of the current instruction.
//    for (Instruction *nextInst : ForwardFlowMap[currentInst]) {
//      // Check if the successor is not visited to avoid cycles.
//      if (visitedInsts.find(nextInst) == visitedInsts.end()) {
//        // Create a new path by extending the current path with the successor.
//        std::vector<Instruction*> newPath = currentPath;
//        newPath.push_back(nextInst);
//
//        // Push the successor and the new path to the stack.
//        errs() << "{" << *nextInst <<  "}, {";
//        for (auto& inst: newPath) {
//          errs() << *inst << " ";
//        }
//        errs() << "}}\n";
//        pathStack.push({nextInst, newPath});
//      }
//    }
//  }
//
//  return allPaths;
//}

bool Checker::hasMallocFreePath(MallocedObject *Obj, Instruction *freeInst) {
  Instruction *startInst = Obj->getBaseInst();
  return DFS(CheckerMaps::ForwardDependencyMap,
             startInst,
      // Termination condition
             [Obj, freeInst](Instruction *curr) {
               if (curr == freeInst) {
                 Obj->setFreeCall(freeInst);
                 return true;
               }
               return false;
             },
      // Continue condition
             [](Instruction *curr) {
               if (curr->getOpcode() == Instruction::GetElementPtr) {
                 return true;
               }
               return false;
             });
}

bool Checker::hasMallocFreePathWithOffset(MallocedObject *Obj, Instruction *freeInst) {
  auto getOffset = [](GetElementPtrInst *gep) {
    Module *M = gep->getModule();
    APInt accumulateOffset;
    if (gep->accumulateConstantOffset(M->getDataLayout(), accumulateOffset)) {
      return accumulateOffset.getZExtValue();
    }
    return SIZE_MAX;
  };

  bool reachedFirstGEP = false;
  bool reachedAlloca = false;
  bool reachedSecondGEP = false;

  Instruction *startInst = Obj->getBaseInst();

  return DFS(CheckerMaps::ForwardDependencyMap,
             startInst,
             [Obj, &reachedAlloca, &reachedFirstGEP, &getOffset, &reachedSecondGEP, freeInst](Instruction *curr) {
               if (curr->getOpcode() == Instruction::GetElementPtr) {
                 auto *gep = dyn_cast<GetElementPtrInst>(curr);
                 if (Obj->getOffset() == getOffset(gep)) {
                   reachedFirstGEP = true;
                 }
               }
               if (reachedFirstGEP && curr->getOpcode() == Instruction::Alloca) {
                 reachedAlloca = true;
               }
               if (reachedAlloca && curr->getOpcode() == Instruction::GetElementPtr) {
                 auto *gep = dyn_cast<GetElementPtrInst>(curr);
                 if (Obj->getOffset() == getOffset(gep)) {
                   reachedSecondGEP = true;
                 }
               }
               if (reachedSecondGEP && isFreeCall(curr) && curr == freeInst) {
                 Obj->setFreeCall(freeInst);
                 return true;
               }
               return false;
             });
}

std::pair<Instruction *, Instruction *> Checker::checkFreeExistence(std::vector<Instruction *> &Path) {
  Instruction *mallocInst = Path[0];
  errs() << "Malloc: " << *mallocInst << " | base: " << *(MallocedObjs[mallocInst]->getBaseInst()) << "\n";
  bool mallocWithOffset = MallocedObjs[mallocInst]->isMallocedWithOffset();

  bool foundMallocFreePath = false;

  for (Instruction *Inst : Path) {
    if (Inst->getOpcode() == Instruction::Call && isFreeCall(Inst)) {
      if (mallocWithOffset) {
        if (hasMallocFreePathWithOffset(MallocedObjs[mallocInst].get(), Inst)) {
          foundMallocFreePath = true;
          break;
        }
      } else {
        if (hasMallocFreePath(MallocedObjs[mallocInst].get(), Inst)) {
          foundMallocFreePath = true;
          break;
        }
      }
    }
  }

  errs() << "\tfound: " << foundMallocFreePath << "\n";

  if (!foundMallocFreePath) {
    Instruction *endInst = RET;
    if (mallocWithOffset) {
      MallocedObject *parentObj = MallocedObjs[mallocInst]->getParent();
      if (parentObj->isDeallocated()) {
        endInst = parentObj->getFreeCall();
      }
    }
    errs() << "MALLOC trace: " << *mallocInst << " | " << *endInst << "\n";
    return {mallocInst, endInst};
  }

  return {};
}

std::pair<Instruction *, Instruction *> Checker::MemoryLeakChecker() {
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return {};
  }

  std::vector<std::vector<Instruction *>> allPaths;
  for (Instruction *callInst : callInstructions.at("malloc")) {
    std::unordered_set<Instruction *> visitedInsts;
    std::vector<Instruction *> path;
    collectPaths(visitedInsts, allPaths, path, callInst, RET);
  }
  errs() << "NUM of PATHs" << allPaths.size() << "\n";
  for (const auto &path : allPaths) {
    for (auto &inst : path) {
      errs() << *inst << "\n\t|\n";
    }
    errs() << "\n";
  }

  for (auto &path : allPaths) {
    std::pair<Instruction *, Instruction *> MemLeakTrace = checkFreeExistence(path);
    if (!MemLeakTrace.first || !MemLeakTrace.second) {
      continue;
    }
    return MemLeakTrace;
  }
  return {};
}

//  return {};
//  for (auto &ObjEntry : MallocedObjs) {
//    errs() << "\n\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//    errs() << "Base: " << *(ObjEntry.second->getBaseInst()) << "\n";
//    errs() << "m: " << *(ObjEntry.second->getMallocCall()) << " | f: " << ObjEntry.second->isDeallocated() << "\n";
//    if (ObjEntry.second->isMallocedWithOffset()) {
//      hasMallocFreePathWithOffset(ObjEntry.second.get());
//    } else {
//      hasMallocFreePath(ObjEntry.second.get());
//    }
//    errs() << "after work\n";
//    errs() << "Base: " << *(ObjEntry.second->getBaseInst()) << "\n";
//    errs() << "m: " << *(ObjEntry.second->getMallocCall()) << " | f: " << *(ObjEntry.second->getFreeCall());
//    errs() << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n";
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
//}
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

//MemAllocationInfo *Checker::hasMallocFreePathForStruct(MemAllocationInfo *Info) {
//  Instruction *mallocInst = Info->getMallocInst();
//  DFS(CheckerMaps::ForwardDependencyMap, mallocInst, [Info, this](Instruction *curr) {
//    if (isFreeCall(curr)) {
//      Info->setFreeCall(curr);
////      StructInfos[structName].get()->setMallocScope(pair);
//      return true;
//    }
//    return false;
//  });
//
//  return {};
//}

//bool Checker::hasMallocFreePath(MallocedObject *Obj) {
//  Instruction *mallocInst = Obj->getMallocCall();
//  return DFS(CheckerMaps::ForwardDependencyMap, mallocInst, [Obj, this](Instruction *curr) {
//    if (curr->getOpcode() == Instruction::GetElementPtr) {
//
//    }
//    if (isFreeCall(curr)) {
//      Obj->setFreeCall(curr);
//      return true;
//    }
//    return false;
//  });
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
                  const std::function<bool(Instruction *)> &terminationCondition,
                  const std::function<bool(Instruction *)> &continueCondition) {
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

    if (continueCondition && continueCondition(currInst)) {
      continue;
    }

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

std::pair<Instruction *, Instruction *> Checker::UseAfterFreeChecker() {
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

std::pair<Instruction *, Instruction *> Checker::ScanfValidation() {
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
        return {basePointerArray, cInst};
      }
      if (formatStringSize >= getArraySize(basePointerArray)) {
        return {basePointerArray, cInst};
      }
    }
  }
  return {};
}

std::pair<Instruction *, Instruction *> Checker::OutOfBoundsAccessChecker() {
  if (callInstructions.empty() ||
      callInstructions.find("malloc") == callInstructions.end()) {
    return {};
  }
//  for (Instruction *callInst : callInstructions.at("malloc")) {
//    std::unique_ptr<MallocInfo> mInfo = std::make_unique<MallocInfo>(callInst);
//  }
  return {};
}

std::pair<Instruction *, Instruction *> Checker::BuffOverflowChecker() {
  std::pair<Instruction *, Instruction *> scanfBOF = ScanfValidation();
  if (scanfBOF.first && scanfBOF.second) {
    return scanfBOF;
  }
  std::pair<Instruction *, Instruction *> outOfBoundAcc = OutOfBoundsAccessChecker();
  if (outOfBoundAcc.first && outOfBoundAcc.second) {
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