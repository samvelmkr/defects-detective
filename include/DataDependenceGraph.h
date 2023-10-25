#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>

namespace llvm {

class Analyzer {
private:
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> DependencyMap;
//  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> BackwardDependencyMap;
  std::unordered_set<Instruction *> callInstructions;

  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> FlowMap;
public:
  void addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>>& Map,
               Instruction *Source, Instruction *Destination);
  bool hasEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>>& Map,
               Instruction *Source, Instruction *Destination);

  void collectDependencies(Function *Func);

  void createIntraBBEdges(BasicBlock &BB);
  void constructFlow(Function *Func);

  bool MallocFreePathChecker();

  bool buildDependencyPath(Instruction* from, Instruction* to);

  void printMap(const std::string& map);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);
  bool hasMallocFreePath(Instruction *PInstruction);
  bool isRelevantToMemoryManagement(Instruction *Inst);
};

};

#endif //ANALYZER_H
