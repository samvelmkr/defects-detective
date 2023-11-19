#ifndef ANALYZER_SRC_BOFCHECKER_H
#define ANALYZER_SRC_BOFCHECKER_H

#include "FuncAnalyzer.h"
#include "llvm/IR/GlobalVariable.h"
#include <sstream>

namespace llvm {

class BOFChecker {
  Function *function;
  FuncAnalyzer *funcInfo;

  size_t numOfVariables = 0;
  // Todo: Perhaps later change to <string, string>
  std::unordered_map<std::string, int64_t> variableValues;

  static unsigned int GetFormatStringSize(GlobalVariable *var);
  static unsigned int GetArraySize(AllocaInst *pointerArray);

  Instruction *ProcessMalloc(Instruction *malloc, const std::vector<Instruction *> &geps);
  void ValueAnalysis(Instruction *inst);
  size_t GetMallocedSize(Instruction *malloc);
  size_t GetGepOffset(GetElementPtrInst *gep);
  void ClearData();
public:
  BOFChecker(Function *func, FuncAnalyzer *analyzer);
  std::pair<Instruction *, Instruction *> ScanfValidation();
  std::pair<Instruction *, Instruction *> OutOfBoundAccessChecker();
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
