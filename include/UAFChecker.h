#ifndef ANALYZER_SRC_UAFCHECKER_H
#define ANALYZER_SRC_UAFCHECKER_H

#include "Checker.h"

namespace llvm {

class UAFChecker : public Checker {

  std::vector<std::vector<Instruction*>> allMallocRetPaths = {};

public:
  UAFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);
  bool isUseAfterFree(Instruction* inst);
  std::pair<Value *, Instruction *> Check(Function* function) override;
};

} // namespace llvm

#endif //ANALYZER_SRC_UAFCHECKER_H
