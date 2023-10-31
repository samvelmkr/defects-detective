#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>

namespace llvm {

enum CheckerMaps {
  ForwardDependencyMap,
  BackwardDependencyMap,
  FlowMap,
};

class Checker {
private:
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> ForwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> BackwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> FlowMap;

  std::unordered_map<std::string, std::unordered_set<Instruction *>> callInstructions;

public:
  static void addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>>& Map,
                      Instruction *Source, Instruction *Destination);
  static void removeEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>>& Map,
                      Instruction *Source, Instruction *Destination);
  static bool hasEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>>& Map,
                      Instruction *Source, Instruction *Destination);

  void collectDependencies(Function *Func);

  void createIntraBBEdges(BasicBlock &BB);
  void constructFlow(Function *Func);

  void updateDependencies();

  // If it doesn't find such path, return malloc call.
  Instruction* MallocFreePathChecker();

  bool DFS(CheckerMaps MapID, Instruction *startInst,
           const std::function<bool(Instruction*)>& terminationCondition);

  bool buildBackwardDependencyPath(Instruction* from, Instruction* to);

  void printMap(const std::string& map);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);
  bool hasMallocFreePath(Instruction *PInstruction);
};

};

#endif //ANALYZER_H
