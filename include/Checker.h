#ifndef ANALYZER_SRC_CHECKER_H
#define ANALYZER_SRC_CHECKER_H

#include "MLChecker.h"
#include "llvm/Analysis/CallGraph.h"
#include <queue>

namespace llvm {

class BugType{
  static const std::pair<std::string, int> MemoryLeak;
  static const std::pair<std::string, int> UseAfterFree;
  static const std::pair<std::string, int> BufferOverFlow;
};

class Checker {
private:
  Module *module;
  Function *mainFunc;
  std::unique_ptr<CallGraph> callGraph;
  std::queue<Function *> funcQueue;

  std::unordered_map<Function *, FuncAnalyzer> funcAnalysis;

public:
  Checker(Module &m);

  std::pair<Instruction *, Instruction *> MLCheck();
  std::pair<Instruction *, Instruction *> UAFCheck();
  std::pair<Instruction *, Instruction *> BOFCheck();
};

} // namespace llvm

#endif //ANALYZER_SRC_CHECKER_H