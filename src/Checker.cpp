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
}

std::shared_ptr<BugTrace> Checker::MLCheck() {
  funcQueue.push(mainFunc);

  std::unique_ptr<MLChecker> mlChecker;

  while(!funcQueue.empty()) {
    Function* current = funcQueue.front();
    funcQueue.pop();

    funcAnalysis[current] = FuncAnalyzer(current);

    mlChecker = std::make_unique<MLChecker>(current, &funcAnalysis[current]);
    auto trace = mlChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::MemoryLeak);
    }


    for (const auto& node : *callGraph->operator[](current)) {
      Function* next = node.second->getFunction();
      if (next->isDeclarationForLinker()) {
        continue;
      }
      funcQueue.push(next);
    }
  }

  return {nullptr};
}

std::shared_ptr<BugTrace> Checker::UAFCheck() {
  funcQueue.push(mainFunc);
  std::unique_ptr<UAFChecker> uafChecker;

  while(!funcQueue.empty()) {
    Function* current = funcQueue.front();
    funcQueue.pop();

    uafChecker = std::make_unique<UAFChecker>(current, &funcAnalysis[current]);
    auto trace = uafChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::UseAfterFree);
    }

    for (const auto& node : *callGraph->operator[](current)) {
      Function* next = node.second->getFunction();
      if (next->isDeclarationForLinker()) {
        continue;
      }
      funcQueue.push(next);
    }
  }

  return {nullptr};
}

std::shared_ptr<BugTrace> Checker::BOFCheck() {
  funcQueue.push(mainFunc);
  std::unique_ptr<BOFChecker> bofChecker;

  while(!funcQueue.empty()) {
    Function* current = funcQueue.front();
    funcQueue.pop();

    bofChecker = std::make_unique<BOFChecker>(current, &funcAnalysis[current]);
    auto trace = bofChecker->Check();
    if (trace.first && trace.second) {
      return std::make_shared<BugTrace>(trace, BugType::BufferOverFlow);
    }

    for (const auto& node : *callGraph->operator[](current)) {
      Function* next = node.second->getFunction();
      if (next->isDeclarationForLinker()) {
        continue;
      }
      funcQueue.push(next);
    }
  }

  return {nullptr};
}

} // namespace llvm