#ifndef ANALYZER_SRC_UAFCHECKER_H
#define ANALYZER_SRC_UAFCHECKER_H

#include "FuncAnalyzer.h"

namespace llvm {

class UAFChecker {
  Function* function;
  FuncAnalyzer* funcInfo;

  std::vector<std::vector<Instruction*>> allMallocRetPaths = {};

public:
  UAFChecker(Function* func, FuncAnalyzer* analyzer);
  bool isUseAfterFree(Instruction* inst);
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm

#endif //ANALYZER_SRC_UAFCHECKER_H
