#ifndef ANALYZER_SRC_MLCHECKER_H
#define ANALYZER_SRC_MLCHECKER_H

#include "Checker.h"

namespace llvm {

class MLChecker : public Checker {

  std::vector<std::vector<Value*>> allMallocRetPaths = {};

  // Malloced instruction value is null.
  bool IsNullMallocedInst(std::vector<Value *> &path, Instruction* icmp);
public:
  std::vector<Instruction* > FindAllMallocCalls(Function* function);

  MLChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);
  bool hasMallocFreePath(MallocedObject* obj, Instruction* free);
  bool hasMallocFreePathWithOffset(MallocedObject *obj, Instruction *free);
  std::pair<Instruction *, Instruction *> checkFreeExistence(std::vector<Value *> &path);
  std::pair<Value *, Instruction *> Check(Function* function) override;
};

} // namespace llvm
#endif //ANALYZER_SRC_MLCHECKER_H
