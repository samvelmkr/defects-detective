#ifndef DATA_DEPENDENCE_GRAPH_H
#define DATA_DEPENDENCE_GRAPH_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>

namespace llvm {

class DataFlowGraph {
private:
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> DependencyMap;
  std::unordered_set<Instruction *> callInstructions;
public:
  void addEdge(Instruction *Source, Instruction *Destination);
  bool hasEdge(Instruction *Source, Instruction *Destination);

  void collectDependencies(Function *Func);

  bool MallocFreePathChecker();

  void print();
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);
  bool hasMallocFreePath(Instruction *PInstruction);
};

};

#endif //DATA_DEPENDENCE_GRAPH_H
