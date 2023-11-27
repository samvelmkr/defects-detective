#ifndef ANALYZER_SRC_FUNCINFO_H
#define ANALYZER_SRC_FUNCINFO_H

#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DataLayout.h"

#include <unordered_set>
#include <utility>
#include <stack>

namespace llvm {

class MallocedObject {
private:
  Instruction *base = {};
  size_t offset = SIZE_MAX;
  MallocedObject *main = {};
  std::pair<Instruction *, std::vector<Instruction *>> mallocFree = {};
public:
  MallocedObject(Instruction *inst);
  MallocedObject(MallocedObject *obj);
  void setOffset(MallocedObject *obj, size_t num);
  size_t getOffset() const;
  bool isMallocedWithOffset() const;
  void setMallocCall(Instruction *malloc);
  void addFreeCall(Instruction *free);
  MallocedObject *getMainObj() const;
  Instruction *getMallocCall() const;
  std::vector<Instruction *> getFreeCalls() const;
  Instruction *getBaseInst();
  bool isDeallocated() const;
};

bool IsCallWithName(Instruction *inst, const std::string &name);

struct CallInstruction {
  static const std::string Malloc;
  static const std::string Free;
  static const std::string Scanf;
  static const std::string Memcpy;
  static const std::string Strlen;
  static const std::string Time;
  static const std::string Srand;
  static const std::string Rand;
  static const std::string Printf;
};

class CallDataDepInfo {
public:
  CallInst *call = nullptr;
  size_t argNum = SIZE_MAX;

  CallDataDepInfo() = default;
  void Init(CallInst *cInst, Instruction *pred) {
    if (!cInst->getCalledFunction()->isDeclarationForLinker() &&
        !IsCallWithName(dyn_cast<Instruction>(cInst), CallInstruction::Memcpy)) {
      if (pred) {
        call = cInst;
        size_t num = 0;
        for (auto &arg : call->operands()) {
          if (pred == arg) {
            argNum = num;
          }
          ++num;
        }
      }
    }
  }
};

enum AnalyzerMap {
  ForwardDependencyMap,
  BackwardDependencyMap,
  ForwardFlowMap,
  BackwardFlowMap
};

int64_t CalculateOffset(GetElementPtrInst *inst);
Instruction *GetCmpNullOperand(Instruction *icmp);

class FuncInfo {
private:
  Function *function = {};
  Instruction *ret = {};

  std::unordered_map<std::string, std::vector<Instruction *>> callInstructions;

  std::unordered_map<Value *, std::unordered_set<Value *>> forwardDependencyMap;
  std::unordered_map<Value *, std::unordered_set<Value *>> backwardDependencyMap;
  std::unordered_map<Value *, std::unordered_set<Value *>> forwardFlowMap;
  std::unordered_map<Value *, std::unordered_set<Value *>> backwardFlowMap;


  void CollectCalls(Instruction *callInst);

  void AddEdge(AnalyzerMap mapID, Value * source, Value * destination);
  void RemoveEdge(AnalyzerMap mapID, Value * source, Value * destination);
  bool HasEdge(AnalyzerMap mapID, Value * source, Value * destination);

  bool ProcessStoreInsts(Instruction *storeInst);
  bool ProcessGepInsts(Instruction *gepInst);
  void UpdateDataDeps();
  void ConstructDataDeps();

  void CollectMallocedObjs();

  void CreateEdgesInBB(BasicBlock *bb);
  void ConstructFlowDeps();

  bool DFS(AnalyzerMap mapID,
           Instruction *start,
           const std::function<bool(Value *)> &terminationCondition,
           const std::function<bool(Value *)> &continueCondition = nullptr,
           const std::function<void(Value *)> &getLoopInfo = nullptr);

  void ProcessArgs();
public:
  FuncInfo() = default;
  FuncInfo(Function *func);

  std::unordered_map<Value *, std::unordered_set<Value *>> *SelectMap(AnalyzerMap mapID);

  MallocedObject *FindSuitableObj(Instruction *base);

  void printMap(AnalyzerMap mapID);
  std::vector<Instruction *> getCalls(const std::string &funcName);
  Instruction *getRet() const;

//  void CollectPaths(Instruction *from, Instruction *to,
//                    std::vector<std::vector<Instruction *>> &allPaths);

//  bool HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to);

  bool FindSpecialDependenceOnArg(Argument *arg, size_t argNum,
                                  const std::function<bool(Instruction *)> &type);
  bool HasPathToSpecificTypeOfInst(AnalyzerMap mapID, Instruction *from,
                                   const std::function<bool(Instruction *)> &type,
                                   CallDataDepInfo *callInfo = nullptr);

  std::unordered_map<Instruction *, std::shared_ptr<MallocedObject>> mallocedObjs;

  std::vector<Instruction *> CollectAllGeps(Instruction *malloc);

  std::vector<Instruction *> CollectAllDepInst(Instruction *from,
                                               const std::function<bool(Instruction *)> &type,
                                               CallDataDepInfo *callInfo = nullptr);
  std::vector<Instruction *> CollectSpecialDependenciesOnArg(Argument *arg,
                                                             size_t argNum,
                                                             const std::function<bool(Instruction *)> &type);
  size_t GetArgsNum();
};

} // namespace llvm

#endif // ANALYZER_SRC_FUNCINFO_H
