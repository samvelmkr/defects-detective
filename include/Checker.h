#ifndef ANALYZER_SRC_CHECKER_H
#define ANALYZER_SRC_CHECKER_H

#include "MLChecker.h"
#include "UAFChecker.h"
#include "BOFChecker.h"
#include "llvm/Analysis/CallGraph.h"
#include <queue>

namespace llvm {

struct BugType {
  static const std::pair<std::string, int> MemoryLeak;
  static const std::pair<std::string, int> UseAfterFree;
  static const std::pair<std::string, int> BufferOverFlow;
};

class BugTrace {
private:
  std::pair<Instruction *, Instruction *> trace = {};
  std::pair<std::string, int> type = {};
public:
  BugTrace() = default;
  BugTrace(const std::pair<Instruction *, Instruction *> &bugPair,
           const std::pair<std::string, int> &bugType) {
    trace = bugPair;
    type = bugType;
  }
  std::pair<Instruction *, Instruction *> getTrace() {
    return trace;
  }
  std::pair<std::string, int> getType() {
    return type;
  }
};

class Checker {
private:
  Module *module;
  Function *mainFunc;
  std::unique_ptr<CallGraph> callGraph;
  std::vector<Function *> funcQueue;

  std::unordered_map<Function *, std::unique_ptr<FuncAnalyzer>> funcAnalysis;

  std::unique_ptr<BugTrace> bug;

  void AnalyzeFunctions();
public:
  Checker(Module &m);

  std::shared_ptr<BugTrace> MLCheck();
  std::shared_ptr<BugTrace> UAFCheck();
  std::shared_ptr<BugTrace> BOFCheck();
};

} // namespace llvm

#endif //ANALYZER_SRC_CHECKER_H