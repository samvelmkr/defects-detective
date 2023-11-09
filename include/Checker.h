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

class MemAllocationInfo {
private:
  InstructionPairPtr MallocFree = InstructionPairPtr::makePair(nullptr, nullptr);
  size_t offset = SIZE_MAX;
public:
  MemAllocationInfo(Instruction *baseInst, Instruction *mallocInst, Instruction *freeInst = nullptr) {
    MallocFree = InstructionPairPtr::makePair(mallocInst, freeInst);
  }
  MemAllocationInfo(MemAllocationInfo *Info) {
    MallocFree = Info->MallocFree;
  }
  void setFreeCall(Instruction *freeInst) {
    MallocFree = InstructionPairPtr::makePair(MallocFree.get()->first, freeInst);
  }
  bool isDeallocated() {
    return MallocFree.get()->second;
  }
  bool isMallocWithOffset() {
    return offset != SIZE_MAX;
  }
  Instruction *getMallocInst() {
    return MallocFree.get()->first;
  }
  Instruction *getFreeInst() {
    return MallocFree.get()->second;
  }
};

class MallocedObject {
private:
  Instruction *BaseInst;
  size_t Offset = SIZE_MAX;
  Instruction *ParentInst;
  std::pair<Instruction *, Instruction *> MallocFree;
public:
  MallocedObject(Instruction *Inst) {
    BaseInst = Inst;
  }
  MallocedObject(MallocedObject *Obj) {}
  void setOffset(Instruction *parentInst, size_t offset) {
    Offset = offset;
    ParentInst = parentInst;
  }
  bool isMallocedWithOffset() {
    return Offset != SIZE_MAX;
  }
  void setMallocCall(Instruction *mallocInst) {
    MallocFree.first = mallocInst;
  }
  void setFreeCall(Instruction *freeInst) {
    MallocFree.second = freeInst;
  }
  Instruction *getMallocCall() {
    return MallocFree.first;
  }
  Instruction *getFreeCall(Instruction *mallocInst) {
    return MallocFree.second;
  }
  bool isDeallocated() {
    return MallocFree.second != nullptr;
  }
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
  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructFieldInfos;

  std::vector<std::shared_ptr<MallocedObject>> MallocedObjs;
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
  void collectMallocedObjs();

  bool DFS(CheckerMaps MapID, Instruction *startInst,
           const std::function<bool(Instruction *)> &terminationCondition);


  bool buildBackwardDependencyPath(Instruction *from, Instruction *to);

  void printMap(CheckerMaps MapID);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);

  //===--------------------------------------------------------------------===//
  // Memory leak checker.
  //===--------------------------------------------------------------------===//

  InstructionPairPtr::Ptr MemoryLeakChecker();
//  MallocType getMallocType(Instruction *mallocInst);
  MemAllocationInfo *hasMallocFreePath(MemAllocationInfo *Info);
  MemAllocationInfo *hasMallocFreePathForStruct(MemAllocationInfo *Info);
  MemAllocationInfo *hasMallocFreePathForStructField(MemAllocationInfo *Info);

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
