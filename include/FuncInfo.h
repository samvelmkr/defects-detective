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

class LoopsInfo {
private:
  BasicBlock *header;
  Instruction *latch;
  std::vector<BasicBlock *> scope;
  Instruction *loopVariableInst = nullptr;
  Value *loopSize = nullptr;

  std::pair<int64_t, int64_t> range = {};

  Instruction *condition;
  ICmpInst::Predicate predicate;

  void ProcessHeader() {
    for (auto &i : *header) {
      if (auto *iCmp = dyn_cast<ICmpInst>(&i)) {
        predicate = iCmp->getPredicate();
        condition = &i;
      }
    }
  }
public:
  LoopsInfo(BasicBlock *bb, Instruction *inst)
      : header(bb),
        latch(inst) {
    ProcessHeader();
  }

  bool HasInst(Instruction *inst) {
    for (const auto &bb : scope) {
      if (inst->getParent() == bb) {
        return true;
      }
    }
    return false;
  }
  void SetScope(const std::vector<BasicBlock *> &vec) {
    scope = vec;
  }
  void SetLoopVar(Instruction *inst) {
    loopVariableInst = inst;
  }
  void SetLoopSize(Value *val) {
    loopSize = val;
  }
  void SetRange(std::pair<int64_t, int64_t> pair) {
    range = pair;
  }
  std::pair<int64_t, int64_t> GetRange() {
    return range;
  }
  BasicBlock *GetHeader() {
    return header;
  }
  Instruction *GetLatch() {
    return latch;
  }
  Instruction *GetCondition() {
    return condition;
  }
  Instruction *GetLoopVar() {
    return loopVariableInst;
  }
  Value *GetLoopSize() {
    return loopSize;
  }
  ICmpInst::Predicate GetPredicate() {
    return predicate;
  }
};

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
  static const std::string Snprintf;
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

int64_t CalculateOffsetInBits(GetElementPtrInst *inst);
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

  std::shared_ptr<LoopsInfo> loopInfo = {nullptr};

  void CollectCalls(Instruction *callInst);

  void AddEdge(AnalyzerMap mapID, Value *source, Value *destination);
  void RemoveEdge(AnalyzerMap mapID, Value *source, Value *destination);
  bool HasEdge(AnalyzerMap mapID, Value *source, Value *destination);

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
           const std::function<bool(Value *)> &continueCondition = nullptr);

  Instruction *GetDeclaration(Instruction *inst);

  void ProcessArgs();

  bool DetectLoopsUtil(Function *f, BasicBlock *BB, std::unordered_set<BasicBlock *> &visited,
                       std::unordered_set<BasicBlock *> &recStack);

  void DetectLoops();
  void SetLoopHeaderInfo();
  void SetLoopScope();
public:
  FuncInfo() = default;
  FuncInfo(Function *func);

  std::unordered_map<Value *, std::unordered_set<Value *>> *SelectMap(AnalyzerMap mapID);

  MallocedObject *FindSuitableObj(Instruction *base);

  void printMap(AnalyzerMap mapID);
  std::vector<Instruction *> getCalls(const std::string &funcName);
  Instruction *getRet() const;

  std::shared_ptr<LoopsInfo> GetLoopInfo();
  void SetLoopRange(std::pair<int64_t, int64_t> range);


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
