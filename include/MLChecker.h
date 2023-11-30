#ifndef ANALYZER_SRC_MLCHECKER_H
#define ANALYZER_SRC_MLCHECKER_H

#include "Checker.h"

namespace llvm {

class MLChecker : public Checker {

  std::vector<std::vector<Value*>> allMallocRetPaths = {};

  // Malloced instruction value is null.
  bool IsNullMallocedInst(std::vector<Value *> &path, Instruction* icmp);

  bool HasMallocFreePath(MallocedObject* obj, Instruction* free);
  bool HasMallocFreePathWithOffset(MallocedObject *obj, Instruction *free);
  std::pair<Instruction *, Instruction *> CheckFreeExistence(std::vector<Value *> &path);

  std::vector<Instruction* > FindAllMallocCalls(Function* function);

  bool FunctionCallDeallocation(CallInst* call);

  bool HasSwitchWithFreeCall(Function* function);

public:

  MLChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);
  std::pair<Value *, Instruction *> Check(Function* function) override;
};

} // namespace llvm
#endif //ANALYZER_SRC_MLCHECKER_H
