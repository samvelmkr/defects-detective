#ifndef ANALYZER_H
#define ANALYZER_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>
#include <utility>

namespace llvm {

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

  std::unordered_map<std::string, std::vector<Instruction *>> callInstructions;

//  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructInfos;
//  std::unordered_map<std::string, std::shared_ptr<StructInfo>> StructFieldInfos;

  std::unordered_map<Instruction *, std::shared_ptr<MallocedObject>> MallocedObjs;

  Instruction *RET;
public:
  Checker(Module* M);

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
  MallocedObject* findSuitableObj(Instruction* baseInst);
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
  std::pair<Instruction *, Instruction *> checkFreeExistence(std::vector<Instruction *> &path);
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

//    analyzer.printMap(CheckerMaps::ForwardFlowMap);
//    errs() << "----------------------------------------\n";
//    analyzer.printMap(CheckerMaps::ForwardDependencyMap);
//    errs() << "----------------------------------------\n";
//    analyzer.printMap(CheckerMaps::BackwardDependencyMap);
//    errs() << "----------------------------------------\n";

Sarif GenSarif;
std::pair<Instruction*, Instruction*> mlLoc = analyzer.MemoryLeakChecker();
if (mlLoc.first && mlLoc.second) {
//      errs() << "MemLeak" << *mlLoc.first << " | " << *mlLoc.second << "\n";
auto Trace = createTraceOfPairInst(mlLoc.first, mlLoc.second);
GenSarif.addResult(BugReport(Trace, "memory-leak", 1));
GenSarif.save();
break;
}

std::pair<Instruction*, Instruction*> uafLoc = analyzer.UseAfterFreeChecker();
if (uafLoc.first && uafLoc.second) {
errs() << *uafLoc.first << " | " << *uafLoc.second << "\n";
auto Trace = createTraceOfPairInst(uafLoc.first, uafLoc.second);
GenSarif.addResult(BugReport(Trace, "use-after-free", 0));
GenSarif.save();
break;
}

std::pair<Instruction*, Instruction*> bofLoc = analyzer.BuffOverflowChecker();
if (bofLoc.first && bofLoc.second) {
//      errs() << *bofLoc.first << " | " << *bofLoc.second << "\n";
auto Trace = createTraceOfPairInst(bofLoc.first, bofLoc.second);
GenSarif.addResult(BugReport(Trace, "buffer-overflow", 2));
GenSarif.save();
break;
}


errs() << "CALL GRAPH\n";
for (std::pair<const Function *const, std::unique_ptr<CallGraphNode>> &Pair : *callGraph) {
const Function *F = Pair.first;
if (!F) {
errs() << "FUNC: chka\n";
}
else {
errs() << "FUNC: " << F->getName().str() << "\n";
if (F->isDeclarationForLinker()) {
continue;
}
}

CallGraphNode *node = Pair.second.get();
for (auto iter = node->begin(); iter != node->end(); ++iter) {
errs() << "   CALLS: " << iter->second->getFunction()->getName() << "\n";
}
}

};

#endif //ANALYZER_H
