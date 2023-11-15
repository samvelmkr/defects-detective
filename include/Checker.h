#ifndef ANALYZER_SRC_CHECKER_H
#define ANALYZER_SRC_CHECKER_H

#include "MLChecker.h"

namespace llvm {

class Checker {
private:
  Function* main;
  std::unordered_map<Function *, FuncAnalyzer> funcAnalysis;

  std::shared_ptr<MLChecker> mlChecker;
public:
  Checker(Module& m);
  void Check();

};

} // namespace llvm

#endif //ANALYZER_SRC_CHECKER_H