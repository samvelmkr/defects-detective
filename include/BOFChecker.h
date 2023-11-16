#ifndef ANALYZER_SRC_BOFCHECKER_H
#define ANALYZER_SRC_BOFCHECKER_H

#include "FuncAnalyzer.h"
#include "llvm/IR/GlobalVariable.h"
#include <sstream>

namespace llvm {

class BOFChecker {
  Function* function;
  FuncAnalyzer* funcInfo;

  static unsigned int GetFormatStringSize(GlobalVariable *var);
  static unsigned int GetArraySize(AllocaInst *pointerArray);

public:
  BOFChecker(Function* func, FuncAnalyzer* analyzer);
  std::pair<Instruction*, Instruction*> ScanfValidation();
  std::pair<Instruction*, Instruction*> OutOfBoundAccessChecker();
  std::pair<Instruction*, Instruction*> Check();
};

} // namespace llvm

#endif //ANALYZER_SRC_BOFCHECKER_H
