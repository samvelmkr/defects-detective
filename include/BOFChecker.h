#ifndef ANALYZER_SRC_BOFCHECKER_H
#define ANALYZER_SRC_BOFCHECKER_H

#include "FuncAnalyzer.h"
#include "llvm/IR/GlobalVariable.h"
#include <sstream>

namespace llvm {

class LoopsInfo {
private:
  BasicBlock *header;
  Instruction *latch;
  std::vector<BasicBlock *> scope;
  Instruction *loopVariableInst = nullptr;

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
  void SetRange(std::pair<int64_t, int64_t> pair) {
    range = pair;
  }
  std::pair<int64_t, int64_t> GetRange() {
    return range;
  }
  BasicBlock* GetHeader() {
    return header;
  }
  Instruction* GetLatch() {
    return latch;
  }
  Instruction *GetCondition() {
    return condition;
  }
  Instruction* GetLoopVar() {
    return loopVariableInst;
  }
  ICmpInst::Predicate GetPredicate() {
    return predicate;
  }
};

class BOFChecker {
  Function *function;
  FuncAnalyzer *funcInfo;

  size_t numOfVariables = 0;
  // Todo: Perhaps later change to <string, string>
  std::unordered_map<std::string, int64_t> variableValues;

  std::unique_ptr<LoopsInfo> li = {nullptr};

  static unsigned int GetFormatStringSize(GlobalVariable *var);
  static unsigned int GetArraySize(AllocaInst *pointerArray);

  Instruction *ProcessMalloc(MallocedObject *obj, const std::vector<Instruction *> &geps);
  void ValueAnalysis(Instruction *inst);
  size_t GetMallocedSize(Instruction *malloc);
  size_t GetGepOffset(GetElementPtrInst *gep);
  void ClearData();

  void LoopDetection();
  void SetLoopScope();
  void SetLoopHeaderInfo();
  bool AccessToOutOfBoundInCycle(GetElementPtrInst *gep, size_t mallocSize);
public:
  BOFChecker(Function *func, FuncAnalyzer *analyzer);
  std::pair<Instruction *, Instruction *> ScanfValidation();
  std::pair<Instruction *, Instruction *> OutOfBoundAccessChecker();
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
