#ifndef ANALYZER_SRC_BOFCHECKER_H
#define ANALYZER_SRC_BOFCHECKER_H

#include "Checker.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include <sstream>

namespace llvm {


class BOFChecker : public Checker {
  size_t numOfVariables = 0;
  // Todo: later change to <string, some class reprs var>
  std::unordered_map<std::string, std::vector<int64_t>> variableValues;


  size_t GetFormatStringSize(GlobalVariable *var);

  Instruction *FindBOFInst(Instruction *inst, size_t mallocSize,
                           const std::vector<Instruction *> &geps,
                           const std::vector<Instruction *> &memcpies);
  bool IsBOFGep(GetElementPtrInst *gep, size_t mallocSize);
  bool IsCorrespondingMemcpy(Instruction *mc, Instruction *malloc);

  Instruction *DetectOutOfBoundAccess(MallocedObject *obj);

  std::string GetGepVarName(GetElementPtrInst *gep);
  std::string GetArgName(Argument* arg);

  void StoreConstant(StoreInst* storeInst);
  void StoreInstruction(StoreInst* storeInst);
  void ValueAnalysis(Instruction *inst);
  size_t GetMallocedSize(Instruction *malloc);
  size_t GetGepOffset(GetElementPtrInst *gep);
  void ClearData();

  void SetLoopHeaderInfo();
  bool AccessToOutOfBoundInCycle(GetElementPtrInst *gep, size_t mallocSize);

  bool IsCorrectMemcpy(Instruction *mcInst);
  bool IsCorrectMemcpy2(Instruction *mcInst);
  Instruction *FindBOFAfterWrongMemcpy(Instruction *mcInst);
  Instruction *FindStrlenUsage(Instruction *alloca);

  void printVA();
public:
  BOFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);
  std::pair<Instruction *, Instruction *> ScanfValidation(Function *function);
  std::pair<Instruction *, Instruction *> OutOfBoundAccessChecker(Function *function);
  std::pair<Instruction *, Instruction *> MemcpyValidation(Function *function);
  std::pair<Instruction *, Instruction *> Check(Function *function) override;
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
