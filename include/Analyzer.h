#ifndef ANALYZER_SRC_ANALYZER_H
#define ANALYZER_SRC_ANALYZER_H

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
  std::pair<Value *, Instruction *> trace = {};
  std::pair<std::string, int> type = {};
public:
  BugTrace() = default;
  BugTrace(const std::pair<Value *, Instruction *> &bugPair,
           const std::pair<std::string, int> &bugType) {
    trace = bugPair;
    type = bugType;
  }
  std::pair<Value *, Instruction *> getTrace() {
    return trace;
  }
  std::pair<std::string, int> getType() {
    return type;
  }
};

class Analyzer {
private:
  Module *module;
  Function *mainFunc;
  std::unique_ptr<CallGraph> callGraph;
  std::vector<Function *> funcQueue;

  std::unordered_map<Function *, std::shared_ptr<FuncInfo>> funcInfos;

  std::unique_ptr<BugTrace> bug;

  void AnalyzeFunctions();
public:
  Analyzer(Module &m);

  std::shared_ptr<BugTrace> MLCheck();
  std::shared_ptr<BugTrace> UAFCheck();
  std::shared_ptr<BugTrace> BOFCheck();
};

} // namespace llvm

#endif // ANALYZER_SRC_ANALYZER_H