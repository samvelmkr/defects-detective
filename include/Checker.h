#ifndef ANALYZER_SRC_CHECKER_H
#define ANALYZER_SRC_CHECKER_H

#include "FuncInfo.h"

namespace llvm {

class LoopsInfo {
private:
  BasicBlock *header;
  Instruction *latch;
  std::vector<BasicBlock *> scope;
  Instruction *loopVariableInst = nullptr;

  std::pair<int64_t, int64_t> range = {};

  Instruction *condition;
  ICmpInst::Predicate predicate;

  void ProcessHeader() {
    for (auto &i : *header) {
      if (auto *iCmp = dyn_cast<ICmpInst>(&i)) {
        predicate = iCmp->getPredicate();
        condition = &i;
      }
    }
  }
public:
  LoopsInfo(BasicBlock *bb, Instruction *inst)
      : header(bb),
        latch(inst) {
    ProcessHeader();
  }

  bool HasInst(Instruction *inst) {
    for (const auto &bb : scope) {
      if (inst->getParent() == bb) {
        return true;
      }
    }
    return false;
  }
  void SetScope(const std::vector<BasicBlock *> &vec) {
    scope = vec;
  }
  void SetLoopVar(Instruction *inst) {
    loopVariableInst = inst;
  }
  void SetRange(std::pair<int64_t, int64_t> pair) {
    range = pair;
  }
  std::pair<int64_t, int64_t> GetRange() {
    return range;
  }
  BasicBlock *GetHeader() {
    return header;
  }
  Instruction *GetLatch() {
    return latch;
  }
  Instruction *GetCondition() {
    return condition;
  }
  Instruction *GetLoopVar() {
    return loopVariableInst;
  }
  ICmpInst::Predicate GetPredicate() {
    return predicate;
  }
};

struct DFSOptions {
  std::function<bool(Instruction *)> terminationCondition = nullptr;
  std::function<bool(Instruction *)> continueCondition = nullptr;
  std::function<bool(Instruction *)> getLoopInfo = nullptr;
};

struct DFSContext {
  AnalyzerMap mapID = {};
  Instruction *start = nullptr;
  DFSOptions options;
};

struct DFSResult {
  bool status = false;
};

class Checker {
private:
  std::vector<std::string> libraryCalls = {CallInstruction::Memcpy,
                                           CallInstruction::Strlen,
                                           CallInstruction::Scanf,
                                           CallInstruction::Malloc,
                                           CallInstruction::Free,
                                           CallInstruction::Time,
                                           CallInstruction::Srand,
                                           CallInstruction::Rand};

  bool IsLibraryFunction(Instruction *inst);
protected:
  std::unordered_map<Function *, std::shared_ptr<FuncInfo>> funcInfos;
  std::unique_ptr<LoopsInfo> loopInfo = {nullptr};

public:

  Checker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);

  // TODO: later change the name
  DFSResult DFSTraverse(const DFSContext &context,
                        std::unordered_set<Instruction *> &visitedInstructions);

  DFSResult DFS(const DFSContext &context);

  size_t CalculNumOfArg(CallInst *cInst,
                        Instruction *pred);

  void AnalyzeFunctions();

  virtual std::pair<Instruction *, Instruction *> Check(Function *function) = 0;

  void CollectPaths(Instruction *from, Instruction *to,
                    std::vector<std::vector<Instruction *>> &allPaths);

  void FindPaths(std::unordered_set<Instruction *> &visitedInsts,
                 std::vector<std::vector<Instruction *>> &paths,
                 std::vector<Instruction *> &currentPath,
                 Instruction *from,
                 Instruction *to,
                 Function *function);

  void ProcessTermInstOfPath(std::vector<Instruction *> &path);

  bool HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to);

  Instruction *FindInstWithType(Function *function,
                                const std::function<bool(Instruction *)> &typeCond);

  std::vector<Instruction *> CollectAllInstsWithType(Function *function,
                                                     const std::function<bool(Instruction *)> &typeCond);

  Instruction *GetDeclaration(Instruction *inst);

  void LoopDetection(Function *function);
  void SetLoopScope(Function *function);

  size_t GetArraySize(AllocaInst *pointerArray);

  std::vector<std::vector<Instruction *>> CollectAllPaths(Instruction *start, Instruction *end);
  void ExtractPaths(Instruction *current, Instruction *end,
                    std::unordered_set<Instruction *> &visited,
                    std::vector<Instruction *> &currentPath,
                    std::vector<std::vector<Instruction *>> &allPaths);
};

} // namespace llvm

#endif // ANALYZER_SRC_CHECKER_H