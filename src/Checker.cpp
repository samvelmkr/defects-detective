#include "Checker.h"

namespace llvm {

const std::pair<std::string, int> BugType::UseAfterFree = {"use-after-free", 0};
const std::pair<std::string, int> BugType::MemoryLeak = {"memory-leak", 1};
const std::pair<std::string, int> BugType::BufferOverFlow = {"buffer-overflow", 2};

Checker::Checker(Module &m) {
  module = &m;
  mainFunc = m.getFunction("main");
  if (!mainFunc) {
    return;
  }
  callGraph = std::make_unique<CallGraph>(*module);
  AnalyzeFunctions();
}

void Checker::AnalyzeFunctions() {
  std::stack<Function *> functionStack;
  std::unordered_set<Function *> visitedFunctions;
  functionStack.push(mainFunc);

  while (!functionStack.empty()) {
    Function *current = functionStack.top();
    functionStack.pop();

    funcQueue.push_back(current);
    funcAnalysis[current] = std::make_unique<FuncAnalyzer>(current);

    visitedFunctions.insert(current);
    for (const auto &node : *callGraph->operator[](current)) {
      Function *next = node.second->getFunction();
      if (visitedFunctions.find(next) == visitedFunctions.end()) {
        if (next->isDeclarationForLinker()) {
          continue;
        }
        functionStack.push(next);
      }
    }
  }
}

std::shared_ptr<BugTrace> Checker::MLCheck() {
  for (auto currentFunc : funcQueue) {
    std::unique_ptr<MLChecker> mlChecker = std::make_unique<MLChecker>(currentFunc, funcAnalysis[currentFunc].get());
    auto trace = mlChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::MemoryLeak);
    }
  }
  return {nullptr};
}

std::shared_ptr<BugTrace> Checker::UAFCheck() {
  for (auto currentFunc : funcQueue) {
    std::unique_ptr<UAFChecker> uafChecker = std::make_unique<UAFChecker>(currentFunc, funcAnalysis[currentFunc].get());
    auto trace = uafChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::MemoryLeak);
    }
  }
  return {nullptr};
}

std::shared_ptr<BugTrace> Checker::BOFCheck() {
  for (auto currentFunc : funcQueue) {

    std::unique_ptr<BOFChecker> bofChecker = std::make_unique<BOFChecker>(currentFunc, funcAnalysis[currentFunc].get());
    auto trace = bofChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::MemoryLeak);
    }
  }
  return {nullptr};
}

} // namespace llvm