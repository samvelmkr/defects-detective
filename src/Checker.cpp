#include "Checker.h"

namespace llvm {

Checker::Checker(Module &m) {
  main = m.getFunction("main");

  // TODO: Better way: build call graph from function 'main'
  for (auto& Func : m.getFunctionList()) {
    funcAnalysis[&Func] = FuncAnalyzer(&Func);
  }

  mlChecker = std::make_shared<MLChecker>(main, &funcAnalysis[main]);
}

void Checker::Check() {
  mlChecker->Check();
}

} // namespace llvm