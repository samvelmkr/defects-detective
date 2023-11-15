#ifndef ANALYZER_SRC_MLCHECKER_H
#define ANALYZER_SRC_MLCHECKER_H

#include "FuncAnalyzer.h"

namespace llvm {

class MLChecker {
  Function* function;
  FuncAnalyzer* funcInfo;

  std::vector<std::vector<Instruction*>> allMallocRetPaths = {};

public:
  MLChecker(Function* func, FuncAnalyzer* analyzer);
  bool hasMallocFreePath(MallocedObject* obj, Instruction* free);
  bool hasMallocFreePathWithOffset(MallocedObject *Obj, Instruction *freeInst);
  std::pair<Instruction *, Instruction *> checkFreeExistence(std::vector<Instruction *> &path);
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm
#endif //ANALYZER_SRC_MLCHECKER_H
