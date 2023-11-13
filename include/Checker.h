#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>
#include <utility>

namespace llvm {

//class InstructionPairPtr {
//public:
//  using Ptr = std::shared_ptr<std::pair<Instruction *, Instruction *>>;
//
//  InstructionPairPtr(Ptr SharedPtr) : ptr(std::move(SharedPtr)) {}
//  static Ptr makePair(Instruction *inst1, Instruction *inst2) {
//    return std::make_shared<std::pair<Instruction *, Instruction *>>(inst1, inst2);
//  }
//  Ptr get() const {
//    return ptr;
//  }
//private:
//  Ptr ptr;
//};

//class StructInfo {
//private:
//  struct FieldInfo {
//    InstructionPairPtr memAllocInfo = InstructionPairPtr::makePair(nullptr, nullptr);
//  };
//
//  StructType *type;
//  InstructionPairPtr memAllocInfo = InstructionPairPtr::makePair(nullptr, nullptr);
//  std::unordered_map<size_t, FieldInfo> fieldInfos;
//  AllocaInst *declaration;
//
//public:
//  StructInfo(StructType *structType, AllocaInst *declareInst) {
//    type = structType;
//    declaration = declareInst;
//  }
//
//  void setMallocScope(InstructionPairPtr pair) {
//    memAllocInfo = std::move(pair);
//  }
//
//  void addField(size_t offset) {
//    FieldInfo field;
//    fieldInfos[offset] = field;
//  }
//
//  void setFieldMallocScope(size_t offset, InstructionPairPtr pair) {
//    fieldInfos[offset].memAllocInfo = std::move(pair);
//  }
//
//  size_t getFieldCount() const {
//    return fieldInfos.size();
//  }
//
////  const FieldInfo &getFieldInfo(size_t index) const {
////    return fieldInfos[index];
////  }
//};

//class MemAllocationInfo {
//private:
//  InstructionPairPtr MallocFree = InstructionPairPtr::makePair(nullptr, nullptr);
//  size_t offset = SIZE_MAX;
//public:
//  MemAllocationInfo(Instruction *baseInst, Instruction *mallocInst, Instruction *freeInst = nullptr) {
//    MallocFree = InstructionPairPtr::makePair(mallocInst, freeInst);
//  }
//  MemAllocationInfo(MemAllocationInfo *Info) {
//    MallocFree = Info->MallocFree;
//  }
//  void setFreeCall(Instruction *freeInst) {
//    MallocFree = InstructionPairPtr::makePair(MallocFree.get()->first, freeInst);
//  }
//  bool isDeallocated() {
//    return MallocFree.get()->second;
//  }
//  bool isMallocWithOffset() {
//    return offset != SIZE_MAX;
//  }
//  Instruction *getMallocInst() {
//    return MallocFree.get()->first;
//  }
//  Instruction *getFreeInst() {
//    return MallocFree.get()->second;
//  }
//};

class MallocedObject {
private:
  Instruction *BaseInst;
  size_t Offset = SIZE_MAX;
  MallocedObject *ParentObj;
  std::pair<Instruction *, Instruction *> MallocFree = {nullptr, nullptr};
public:
  MallocedObject(Instruction *Inst) {
    BaseInst = Inst;
  }
  MallocedObject(MallocedObject *Obj) {}
  void setOffset(MallocedObject *parentObj, size_t offset) {
    Offset = offset;
    ParentObj = parentObj;
  }
  size_t getOffset() const {
    return Offset;
  }
  bool isMallocedWithOffset() const {
    return Offset != SIZE_MAX;
  }
  void setMallocCall(Instruction *mallocInst) {
    MallocFree.first = mallocInst;
  }
  void setFreeCall(Instruction *freeInst) {
    MallocFree.second = freeInst;
  }
  MallocedObject *getParent() const {
    return ParentObj;
  }
  Instruction *getMallocCall() const {
    return MallocFree.first;
  }
  Instruction *getFreeCall() const {
    return MallocFree.second;
  }
  Instruction *getBaseInst() {
    return BaseInst;
  }
  bool isDeallocated() const {
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

//  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructInfos;
//  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructFieldInfos;

  std::unordered_map<Instruction *, std::shared_ptr<MallocedObject>> MallocedObjs;

  Instruction *RET;
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
  std::vector<std::vector<Instruction *>> findAllPaths(Instruction *start, Instruction *end);

  void collectPaths(std::unordered_set<Instruction *> &visitedInsts,
                    std::vector<std::vector<Instruction *>> &paths,
                    std::vector<Instruction *> &path,
                    Instruction *from, Instruction *to);
  bool DFS(CheckerMaps MapID, Instruction *startInst,
           const std::function<bool(Instruction *)> &terminationCondition,
           const std::function<bool(Instruction *)> &continueCondition = nullptr);

  bool hasPath(CheckerMaps MapID, Instruction *from, Instruction *to);

  void printMap(CheckerMaps MapID);
  static bool isMallocCall(Instruction *PInstruction);
  static bool isFreeCall(Instruction *Inst);

  //===--------------------------------------------------------------------===//
  // Memory leak checker.
  //===--------------------------------------------------------------------===//
  bool hasMallocFreePath(MallocedObject *Obj, Instruction *freeInst);
  bool hasMallocFreePathWithOffset(MallocedObject *Obj, Instruction *freeInst);
  std::pair<Instruction *, Instruction *> hasCorrespondingFree(std::vector<Instruction *> &path);
  std::pair<Instruction *, Instruction *> MemoryLeakChecker();

  //===--------------------------------------------------------------------===//
  // Use after free checker.
  //===--------------------------------------------------------------------===//

  bool isUseAfterFree(Instruction *Inst);
  std::pair<Instruction *, Instruction *> UseAfterFreeChecker();

  //===--------------------------------------------------------------------===//
  // Buffer overflow checker.
  //===--------------------------------------------------------------------===//
  static unsigned int getArraySize(AllocaInst *pointerArray);
  static unsigned int getFormatStringSize(GlobalVariable *var);

  static bool isScanfCall(Instruction *Inst);
  std::pair<Instruction *, Instruction *> ScanfValidation();
  std::pair<Instruction *, Instruction *> OutOfBoundsAccessChecker();
  std::pair<Instruction *, Instruction *> BuffOverflowChecker();
};

};

#endif //ANALYZER_H
