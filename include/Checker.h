#ifndef ANALYZER_SRC_CHECKER_H
#define ANALYZER_SRC_CHECKER_H

#include "FuncInfo.h"

namespace llvm {

struct DFSOptions {
  std::function<bool(Value *)> terminationCondition = nullptr;
  std::function<bool(Value *)> continueCondition = nullptr;
};

struct DFSContext {
  AnalyzerMap mapID = {};
  Value *start = nullptr;
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
                                           CallInstruction::Rand,
                                           CallInstruction::Printf};

  bool IsLibraryFunction(Value *inst);
protected:
  std::unordered_map<Function *, std::shared_ptr<FuncInfo>> funcInfos;

public:

  Checker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);

  // TODO: later change the name
  DFSResult DFSTraverse(Function* function, const DFSContext &context,
                        std::unordered_set<Value *> &visitedNodes);

  DFSResult DFS(const DFSContext &context);

  size_t CalculNumOfArg(CallInst *cInst,
                        Instruction *pred);

  virtual std::pair<Instruction *, Instruction *> Check(Function *function) = 0;

  void CollectPaths(Instruction *from, Instruction *to,
                    std::vector<std::vector<Value *>> &allPaths);

  void FindPaths(std::unordered_set<Value *> &visitedNodes,
                 std::vector<std::vector<Value *>> &paths,
                 std::vector<Value *> &currentPath,
                 Value *from,
                 Value *to,
                 Function *function);

  void ProcessTermInstOfPath(std::vector<Value *> &path);

  bool HasPath(AnalyzerMap mapID, Instruction *from, Instruction *to);

  Instruction *FindInstWithType(Function *function, AnalyzerMap mapID, Instruction* start,
                                const std::function<bool(Instruction *)> &typeCond);

  std::vector<Instruction *> CollectAllInstsWithType(Function *function, AnalyzerMap mapID, Instruction* start,
                                                     const std::function<bool(Instruction *)> &typeCond);

  Instruction *GetDeclaration(Instruction *inst);

  size_t GetArraySize(AllocaInst *pointerArray);

//  std::vector<std::vector<Instruction *>> CollectAllPaths(Instruction *start, Instruction *end);
//  void ExtractPaths(Instruction *current, Instruction *end,
//                    std::unordered_set<Instruction *> &visited,
//                    std::vector<Instruction *> &currentPath,
//                    std::vector<std::vector<Instruction *>> &allPaths);
};

} // namespace llvm

#endif // ANALYZER_SRC_CHECKER_H