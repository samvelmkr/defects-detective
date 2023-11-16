#ifndef ANALYZER_SRC_MLCHECKER_H
#define ANALYZER_SRC_MLCHECKER_H

#include "FuncAnalyzer.h"

namespace llvm {

class MLChecker {
  Function* function;
  FuncAnalyzer* funcInfo;

  std::vector<std::vector<Instruction*>> allMallocRetPaths = {};

  static void ProcessTermInstOfPath(std::vector<Instruction *> &path);

  // Malloced instruction value is null.
  bool IsNullMallocedInst(std::vector<Instruction *> &path, Instruction* icmp);
  Instruction* hasCmpWithNull(Instruction* icmp);
public:
  MLChecker(Function* func, FuncAnalyzer* analyzer);
  bool hasMallocFreePath(MallocedObject* obj, Instruction* free);
  bool hasMallocFreePathWithOffset(MallocedObject *obj, Instruction *free);
  std::pair<Instruction *, Instruction *> checkFreeExistence(std::vector<Instruction *> &path);
  std::pair<Instruction *, Instruction *> Check();
};

} // namespace llvm
#endif //ANALYZER_SRC_MLCHECKER_H
