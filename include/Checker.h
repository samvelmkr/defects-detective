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
  std::unordered_map<std::string, bool> funcsStats = {};
  std::vector<Value *> path;
  void combine(const DFSResult &other, const std::string& funcName) {
    path.insert(path.end(), other.path.begin(), other.path.end());
    status = status || other.status;
    funcsStats[funcName] = other.status;
  }
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
                                           CallInstruction::Printf,
                                           CallInstruction::Snprintf,
                                           CallInstruction::Memset,
                                           CallInstruction::Strcpy,
                                           CallInstruction::Fopen,
                                           CallInstruction::Fprint,
  };

protected:
  std::unordered_map<Function *, std::shared_ptr<FuncInfo>> funcInfos;
  bool IsLibraryFunction(Value *inst);
  std::vector<Value*> tmpPath;

public:

  Checker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos);

  // TODO: later change the name
  DFSResult DFSTraverse(Function *function, const DFSContext &context,
                        std::unordered_set<Value *> &visitedNodes);

  DFSResult DFS(const DFSContext &context);

  size_t CalculNumOfArg(CallInst *cInst,
                        Instruction *pred);

  virtual std::pair<Value *, Instruction *> Check(Function *function) = 0;

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

  Instruction *FindInstWithType(AnalyzerMap mapID, Instruction *start,
                                const std::function<bool(Instruction *)> &typeCond);

  std::vector<Instruction *> CollectAllInstsWithType(AnalyzerMap mapID, Instruction *start,
                                                     const std::function<bool(Instruction *)> &typeCond);

  Instruction *GetDeclaration(Instruction *inst);

  size_t GetArraySize(AllocaInst *pointerArray);

  void CollectAllPaths(Instruction *start, Instruction *end,
                       std::vector<std::vector<Value *>> &allPaths);
//  void ExtractPaths(Instruction *current, Instruction *end,
//                    std::unordered_set<Instruction *> &visited,
//                    std::vector<Instruction *> &currentPath,
//                    std::vector<std::vector<Instruction *>> &allPaths);
};

} // namespace llvm

#endif // ANALYZER_SRC_CHECKER_H