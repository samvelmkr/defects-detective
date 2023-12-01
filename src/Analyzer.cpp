#include "Analyzer.h"

namespace llvm {

const std::pair<std::string, int> BugType::UseAfterFree = {"use-after-free", 0};
const std::pair<std::string, int> BugType::MemoryLeak = {"memory-leak", 1};
const std::pair<std::string, int> BugType::BufferOverFlow = {"buffer-overflow", 2};

Analyzer::Analyzer(Module &m) {
  module = &m;
  mainFunc = m.getFunction("main");
  if (!mainFunc) {
    return;
  }
  callGraph = std::make_unique<CallGraph>(*module);
  AnalyzeFunctions();
}

void Analyzer::AnalyzeFunctions() {
  std::stack<Function *> functionStack;
  std::unordered_set<Function *> visitedFunctions;
  functionStack.push(mainFunc);

  while (!functionStack.empty()) {
    Function *current = functionStack.top();
    functionStack.pop();

    funcQueue.push_back(current);
    funcInfos[current] = std::make_shared<FuncInfo>(current);

    errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";
    funcInfos[current]->printMap(AnalyzerMap::ForwardDependencyMap);
    errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";

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

std::shared_ptr<BugTrace> Analyzer::MLCheck() {
  std::shared_ptr<MLChecker> mlChecker = std::make_shared<MLChecker>(funcInfos);
  auto trace = mlChecker->Check(mainFunc);
  if (trace.first && trace.second) {
    return std::make_shared<BugTrace>(trace, BugType::MemoryLeak);
  }
  return {nullptr};
}

std::shared_ptr<BugTrace> Analyzer::UAFCheck() {
  std::unique_ptr<UAFChecker> uafChecker = std::make_unique<UAFChecker>(funcInfos);
  auto trace = uafChecker->Check(mainFunc);
  if (trace.first && trace.second) {
    return std::make_shared<BugTrace>(trace, BugType::UseAfterFree);
  }
  return {nullptr};
}

std::shared_ptr<BugTrace> Analyzer::BOFCheck() {
  std::shared_ptr<BOFChecker> bofChecker = std::make_shared<BOFChecker>(funcInfos);
  auto trace = bofChecker->Check(mainFunc);
  if (trace.first && trace.second) {
    return std::make_shared<BugTrace>(trace, BugType::BufferOverFlow);
  }
  return {nullptr};
}

} // namespace llvm
