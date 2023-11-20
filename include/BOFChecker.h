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
  Instruction *loopVariable = nullptr;
  ICmpInst::Predicate predicate;
  std::pair<int, int> range = {};
  Instruction *condition;

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
    loopVariable = inst;
  }
  void SetRange(std::pair<int, int> pair) {
    range = pair;
  }
  std::pair<int, int> GetRange() {
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
};

class BOFChecker {
  Function *function;
  FuncAnalyzer *funcInfo;

  size_t numOfVariables = 0;
  // Todo: Perhaps later change to <string, string>
  std::unordered_map<std::string, int64_t> variableValues;

  std::unique_ptr<LoopsInfo> li;

  static unsigned int GetFormatStringSize(GlobalVariable *var);
  static unsigned int GetArraySize(AllocaInst *pointerArray);

  Instruction *ProcessMalloc(MallocedObject *obj, const std::vector<Instruction *> &geps);
  void ValueAnalysis(Instruction *inst);
  size_t GetMallocedSize(Instruction *malloc);
  size_t GetGepOffset(GetElementPtrInst *gep);
  void ClearData();

  void LoopDetection();
  void SetLoopScope();
  void SetLoopVariable();
  bool AccessToOutOfBoundInCycle(GetElementPtrInst *gep);
public:
  BOFChecker(Function *func, FuncAnalyzer *analyzer);
  std::pair<Instruction *, Instruction *> ScanfValidation();
  std::pair<Instruction *, Instruction *> OutOfBoundAccessChecker();
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
