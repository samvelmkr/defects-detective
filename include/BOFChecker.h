#ifndef ANALYZER_SRC_BOFCHECKER_H
#define ANALYZER_SRC_BOFCHECKER_H

#include "Checker.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include <sstream>

namespace llvm {

struct Val {
  size_t size;
  std::vector<int64_t> val;
};

class BOFChecker : public Checker {
  size_t numOfVariables = 0;
  // Todo: later change to <string, some class reprs var>
  std::unordered_map<std::string, std::shared_ptr<Val>> variableValues;

  size_t GetFormatStringSize(GlobalVariable *var);

  std::pair<Value *, Instruction *> FindBOFInst(Instruction *inst,
                                                Instruction *malloc, size_t mallocSize,
                                                const std::vector<Instruction *> &geps,
                                                const std::vector<Instruction *> &memcpies);
  bool IsBOFGep(GetElementPtrInst *gep, size_t mallocSize);
  bool IsCorrespondingMemcpy(Instruction *mc, Instruction *malloc);

  std::pair<Value *, Instruction *> DetectOutOfBoundAccess(MallocedObject *obj);

  std::string GetGepVarName(GetElementPtrInst *gep);
  std::string GetArgName(Argument *arg);

  void StoreConstant(StoreInst *storeInst);
  void StoreInstruction(StoreInst *storeInst);
  void ValueAnalysis(Instruction *inst);
  void CreateNewVar(const std::string &name);
  size_t GetMallocedSize(Instruction *malloc);
  size_t GetGepOffset(GetElementPtrInst *gep);
  void ClearData();

  void SetLoopHeaderInfo(Function *function);
  bool AccessToOutOfBoundInCycle(GetElementPtrInst *gep, size_t mallocSize);

  Instruction *MemcpyValidation(Instruction *mcInst);
  Instruction *FindBOFAfterWrongMemcpy(Instruction *mcInst);
  Instruction *FindStrlenUsage(Instruction *alloca);

  std::pair<Value *, Instruction *> SnprintfCallValidation(Instruction *inst, Instruction *snprintInst);
//  bool StrcpyValidation()

  std::pair<Value *, Instruction *> OutOfBoundFromArray(Instruction *inst);

  // validate cases that cannot be reached by DetectOutOfBoundAccess
  std::pair<Value *, Instruction *> BuildPathsToSuspiciousInstructions(MallocedObject *obj);
  void printVA();
public:
  BOFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);
  std::pair<Instruction *, Instruction *> ScanfValidation(Function *function);
  std::pair<Value *, Instruction *> OutOfBoundAccessChecker(Function *function);
  std::pair<Value *, Instruction *> Check(Function *function) override;
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
