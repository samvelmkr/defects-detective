#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Module.h"
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

class StructInfo {
private:
  struct FieldInfo {
    InstructionPairPtr memAllocInfo = InstructionPairPtr::makePair(nullptr, nullptr);
  };

  StructType *type;
  InstructionPairPtr memAllocInfo = InstructionPairPtr::makePair(nullptr, nullptr);
  std::unordered_map<size_t, FieldInfo> fieldInfos;
  AllocaInst *declaration;

public:
  StructInfo(StructType *structType, AllocaInst *declareInst) {
    type = structType;
    declaration = declareInst;
  }

  void setMallocScope(InstructionPairPtr pair) {
    memAllocInfo = std::move(pair);
  }

  void addField(size_t offset) {
    FieldInfo field;
    fieldInfos[offset] = field;
  }

  void setFieldMallocScope(size_t offset, InstructionPairPtr pair) {
    fieldInfos[offset].memAllocInfo = std::move(pair);
  }

  size_t getFieldCount() const {
    return fieldInfos.size();
  }

//  const FieldInfo &getFieldInfo(size_t index) const {
//    return fieldInfos[index];
//  }
};

//class MallocInfo {
//public:
//  Instruction *mallocInst;
//  Value *size;
//  MallocInfo(Instruction *mallocInst);
//};

enum MallocType {
  SimpleMemAllocation,
  AllocateMemForStruct,
  AllocateMemForStructField
};

enum CheckerMaps {
  ForwardDependencyMap,
  BackwardDependencyMap,
  ForwardFlowMap,
  BackwardFlowMap
};

class Checker {
private:
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> ForwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> BackwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> ForwardFlowMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> BackwardFlowMap;

  std::unordered_map<std::string, std::unordered_set<Instruction *>> callInstructions;

  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructInfos;

  std::vector<InstructionPairPtr> MallocFreePairs;
public:
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

  bool DFS(CheckerMaps MapID, Instruction *startInst,
           const std::function<bool(Instruction *)> &terminationCondition);

  bool buildBackwardDependencyPath(Instruction *from, Instruction *to);

  void printMap(CheckerMaps MapID);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);
  bool isUsedForStruct(GetElementPtrInst* gep);

  //===--------------------------------------------------------------------===//
  // Memory leak checker.
  //===--------------------------------------------------------------------===//

  InstructionPairPtr::Ptr MemoryLeakChecker();
  MallocType getMallocType(Instruction *mallocInst);
  bool hasMallocFreePath(Instruction *mallocInst);
  bool hasMallocFreePathForStruct(Instruction *mallocInst);
  bool hasMallocFreePathForStructField(Instruction *mallocInst);

  //===--------------------------------------------------------------------===//
  // Use after free checker.
  //===--------------------------------------------------------------------===//

  bool isUseAfterFree(Instruction *Inst);
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
