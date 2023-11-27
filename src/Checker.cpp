#include "Checker.h"

namespace llvm {

Checker::Checker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &info)
    : funcInfos(info) {}

//DFSOptions options;
//options.terminationCondition = [](Instruction *inst) { return false; }; // Replace with your condition
//options.continueCondition = [](Instruction *inst) { return false; };   // Replace with your condition
//options.getLoopInfo = [](Instruction *inst) { return false; };         // Replace with your condition
//
//DFSContext context{mapID, startInstruction, options};

bool Checker::IsLibraryFunction(Instruction *inst) {
  for (const std::string &name : libraryCalls) {
    if (IsCallWithName(inst, name)) {
      return true;
    }
  }
  return false;
}

DFSResult Checker::DFSTraverse(const DFSContext &context,
                               std::unordered_set<Instruction *> &visitedInstructions) {

  DFSResult result;

  FuncInfo *funcInfo = funcInfos[context.start->getFunction()].get();
  auto *map = funcInfo->SelectMap(context.mapID);

  std::stack<Instruction *> dfsStack;
  dfsStack.push(context.start);

  Instruction *previous = nullptr;

  while (!dfsStack.empty()) {
    Instruction *current = dfsStack.top();
    dfsStack.pop();

    if (visitedInstructions.find(current) != visitedInstructions.end()) {
      // Already visited this instruction
      continue;
    }

    if (context.options.terminationCondition &&
        context.options.terminationCondition(current)) {
      result.status = true;
      return result;
    }

    if (context.options.continueCondition &&
        context.options.continueCondition(current)) {
      continue;
    }

    if (map->operator[](current).empty()) {
      // change path
    }

    visitedInstructions.insert(current);

    // Check if the current instruction is a call instruction
    if (auto *callInst = dyn_cast<CallInst>(current)) {
      // Handle call instruction
      Function *calledFunction = callInst->getCalledFunction();
      if (calledFunction) {
        if (!calledFunction->isDeclarationForLinker() ||
            !IsLibraryFunction(current)) {
          DFSOptions newOptions;
          newOptions.terminationCondition = context.options.terminationCondition;
          newOptions.continueCondition = context.options.continueCondition;
          newOptions.getLoopInfo = context.options.getLoopInfo;

          Instruction *startInstruction = nullptr;
          if (context.mapID == AnalyzerMap::ForwardFlowMap) {
            startInstruction = &*calledFunction->getEntryBlock().begin();
          } else if (context.mapID == AnalyzerMap::ForwardDependencyMap) {
            // Fixme: find corresponding arg and start tracking from instruction
            //  dependent on argument from called function
            size_t argNum = CalculNumOfArg(callInst, previous);
            FuncInfo* calledFuncInfo = funcInfos[calledFunction].get();
            calledFuncInfo->argumentsMap[calledFunction]
            startInstruction = &*calledFunction->getEntryBlock().begin();
          }

          DFSContext newContext{context.mapID, startInstruction, newOptions};
          // Recursively traverse the called function
          DFSTraverse(newContext, visitedInstructions);
          // Process results from the called function if needed
          // ...

          // May be need to combine results from the called function with the main result
          // result.combine(calledFunctionResult);
        }
      }
    }
    previous = current;

    for (Instruction *next : map->operator[](current)) {
      if (visitedInstructions.find(next) == visitedInstructions.end()) {
        dfsStack.push(next);
      }
    }

  }
  result.status = false;
  return result;
}

DFSResult Checker::DFS(const DFSContext &context) {
  std::unordered_set<Instruction *> visitedInstructions;
  return DFSTraverse(context, visitedInstructions);
}

size_t Checker::CalculNumOfArg(llvm::CallInst *cInst,
                               llvm::Instruction *pred) {
  if (pred) {
    size_t argNum = 0;
    for (auto &arg : cInst->operands()) {
      if (pred == arg) {
        return argNum;
      }
      ++argNum;
    }
  }
  return SIZE_MAX;
}



void Checker::CollectPaths(Instruction *from, Instruction *to,
                           std::vector<std::vector<Instruction *>> &allPaths) {

  std::unordered_set<Instruction *> visitedInsts;
  std::vector<Instruction *> currentPath;
  Function *function = from->getFunction();
  FindPaths(visitedInsts, allPaths, currentPath, from, to, function);
}

// Todo: add support of other maps
// TODO: write iterative algorithm to avoid stack overflow
void Checker::FindPaths(std::unordered_set<Instruction *> &visitedInsts,
                        std::vector<std::vector<Instruction *>> &paths,
                        std::vector<Instruction *> &currentPath,
                        Instruction *from,
                        Instruction *to,
                        Function *function) {
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

  FuncInfo *funcInfo = funcInfos[function].get();

  for (Instruction *next : funcInfo->SelectMap(AnalyzerMap::ForwardFlowMap)->operator[](from)) {
    FindPaths(visitedInsts, paths, currentPath, next, to, function);
  }
  currentPath.pop_back();
  visitedInsts.erase(from);

}

void Checker::ProcessTermInstOfPath(std::vector<Instruction *> &path) {
  Instruction *lastInst = path.back();
  if (lastInst->getOpcode() != Instruction::Ret) {
    return;
  }
  BasicBlock *termBB = lastInst->getParent();
  if (termBB->getInstList().size() != 2) {
    return;
  }
  Instruction *firstInst = &termBB->getInstList().front();
  if (firstInst->getOpcode() != Instruction::Load) {
    return;
  }
  path.pop_back();
  path.pop_back();
}

bool Checker::HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to) {
  DFSOptions options;
  options.terminationCondition = [to](Instruction *inst) { return inst == to; };

  DFSContext context{mapID, from, options};
  DFSResult result = DFS(context);
  return result.status;
}

Instruction *Checker::FindInstWithType(Function *function, const std::function<bool(Instruction *)> &typeCond) {
  Instruction *res = nullptr;
  DFSOptions options;
  options.terminationCondition = [&res, typeCond](Instruction *curr) {
    if (typeCond && typeCond(curr)) {
      res = curr;
      return true;
    }
    return false;
  };

  Instruction *start = &*function->getEntryBlock().begin();
  DFSContext context{AnalyzerMap::ForwardFlowMap, start, options};
  DFS(context);
  return res;
}

std::vector<Instruction *> Checker::CollectAllInstsWithType(Function *function,
                                                            const std::function<bool(Instruction *)> &typeCond) {
  std::vector<Instruction *> results = {};
  DFSOptions options;
  options.terminationCondition = [&results, typeCond](Instruction *curr) {
    if (typeCond && typeCond(curr)) {
      results.push_back(curr);
    }
    return false;
  };

  Instruction *start = &*function->getEntryBlock().begin();
  DFSContext context{AnalyzerMap::ForwardFlowMap, start, options};
  DFS(context);
  return results;
}

Instruction *Checker::GetDeclaration(Instruction *inst) {
  Instruction *declaration = nullptr;
  DFSOptions options;
  options.terminationCondition = [&declaration](Instruction *curr) {
    if (curr->getOpcode() == Instruction::Alloca) {
      declaration = curr;
      return true;
    }
    return false;
  };

  DFSContext context{AnalyzerMap::BackwardDependencyMap, inst, options};
  DFS(context);
  return declaration;
}

size_t Checker::GetArraySize(AllocaInst *pointerArray) {
  Type *basePointerType = pointerArray->getAllocatedType();
  if (auto *arrType = dyn_cast<ArrayType>(basePointerType)) {
    return static_cast<size_t>(arrType->getNumElements());
  }
  return 0;
}

void Checker::LoopDetection(Function *function) {
  Instruction *start = &*function->getEntryBlock().begin();

  //Todo: store vector of Instructions if there are more than one loop
  Instruction *latch;

  DFSOptions options;
  options.getLoopInfo = [&latch](Instruction *inst) {
    // loop info
    latch = inst;
    // Todo: validate also latch/exit (conditional br: one edge - exit, other - backEdge)
    return true;
  };

  DFSContext context{AnalyzerMap::ForwardFlowMap, start, options};
  DFSResult result = DFS(context);

  if (!latch) {
    return;
  }

  if (latch->getOpcode() != Instruction::Br) {
    return;
  }
  auto *brInst = dyn_cast<BranchInst>(latch);
  if (!brInst->isUnconditional()) {
    return;
  }

  BasicBlock *header = brInst->getSuccessor(0);
  loopInfo = std::make_unique<LoopsInfo>(header, latch);
  SetLoopScope(function);
}

void Checker::SetLoopScope(Function *function) {
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


//std::vector<std::vector<Instruction *>> Checker::CollectAllPaths(Instruction *start, Instruction *end) {
//  std::vector<std::vector<Instruction *>> allPaths;
//  std::unordered_set<Instruction *> visitedInstructions;
//
//  // Define DFS options
//  DFSOptions options;
//  options.terminationCondition = [end](Instruction *inst) {
//    return inst == end;
//  };
//
//  // Define DFS context
//  DFSContext context{start->getFunction(), AnalyzerMap::ForwardFlowMap, start, options};
//
//  // Perform DFS traversal
//  DFSResult result = DFSTraverse(context, visitedInstructions);
//
//  // If the end instruction is reached, collect the paths
//  if (result.status) {
//    std::vector<Instruction *> currentPath;
//    ExtractPaths(start, end, visitedInstructions, currentPath, allPaths);
//  }
//
//  return allPaths;
//}
//void Checker::ExtractPaths(Instruction *current,
//                           Instruction *end,
//                           std::unordered_set<Instruction *> &visited,
//                           std::vector<Instruction *> &currentPath,
//                           std::vector<std::vector<Instruction *>> &allPaths) {
//  currentPath.push_back(current);
//
//  if (current == end) {
//    // If the end instruction is reached, add the current path to allPaths
//    allPaths.push_back(currentPath);
//  } else {
//    // Continue DFS traversal for each successor
//    for (Instruction *successor : funcInfos[current->getFunction()]->SelectMap(AnalyzerMap::ForwardFlowMap)->operator[](current)) {
//      if (visited.find(successor) == visited.end()) {
//        ExtractPaths(successor, end, visited, currentPath, allPaths);
//      }
//    }
//  }
//
//  // Backtrack
//  currentPath.pop_back();
//
//}

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