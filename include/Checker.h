#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>
#include <utility>

namespace llvm {

class InstructionPairPtr {
public:
  using Ptr = std::shared_ptr<std::pair<Instruction *, Instruction *>>;

  InstructionPairPtr(Ptr SharedPtr) : ptr(std::move(SharedPtr)) {}
  static Ptr makePair(Instruction *inst1, Instruction *inst2) {
    return std::make_shared<std::pair<Instruction *, Instruction *>>(inst1, inst2);
  }
  Ptr get() const {
    return ptr;
  }
private:
  Ptr ptr;
};

class MallocInfo {
public:
  Instruction *mallocInst;
  Value *size;
  MallocInfo(Instruction *mallocInst);
};

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

  std::vector<InstructionPairPtr> MallocFreePairs;
public:
  //===--------------------------------------------------------------------===//
  // Memory leak checker.
  //===--------------------------------------------------------------------===//
  static void addEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                      Instruction *Source, Instruction *Destination);
  static void removeEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                         Instruction *Source, Instruction *Destination);
  static bool hasEdge(std::unordered_map<Instruction *, std::unordered_set<Instruction *>> &Map,
                      Instruction *Source, Instruction *Destination);

  void collectDependencies(Function *Func);

  void createIntraBBEdges(BasicBlock &BB);
  void constructFlow(Function *Func);

  void updateDependencies();

  // If it doesn't find such path, return malloc call.
  Instruction *MallocFreePathChecker();

  bool DFS(CheckerMaps MapID, Instruction *startInst,
           const std::function<bool(Instruction *)> &terminationCondition);

  bool buildBackwardDependencyPath(Instruction *from, Instruction *to);

  void printMap(const std::string &map);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);
  bool hasMallocFreePath(Instruction *PInstruction);

  //===--------------------------------------------------------------------===//
  // Use after free checker.
  //===--------------------------------------------------------------------===//

  bool isSetToNullPointer(Instruction* Inst);
  InstructionPairPtr::Ptr UseAfterFreeChecker();

  //===--------------------------------------------------------------------===//
  // Buffer overflow checker.
  //===--------------------------------------------------------------------===//
  static unsigned int getArraySize(AllocaInst *pointerArray);
  static unsigned int getFormatStringSize(GlobalVariable *var);

  static bool isScanfCall(Instruction *Inst);
  InstructionPairPtr::Ptr ScanfValidation();
  InstructionPairPtr::Ptr OutOfBoundsAccessChecker();
  InstructionPairPtr::Ptr BuffOverflowChecker();
};

};

#endif //ANALYZER_H
