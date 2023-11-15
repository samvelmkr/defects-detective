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

std::pair<Instruction *, Instruction *> Checker::MLCheck() {
  funcQueue.push(mainFunc);

  std::unique_ptr<MLChecker> mlChecker;

  while(!funcQueue.empty()) {
    Function* current = funcQueue.front();
    funcQueue.pop();

    funcAnalysis[current] = FuncAnalyzer(current);

    errs() << "\tCURRRR: " << current->getName() << "\n";

    mlChecker = std::make_unique<MLChecker>(current, &funcAnalysis[current]);
    auto trace = mlChecker->Check();
    if (trace.first && trace.second) {
      if (trace.second->getOpcode() == Instruction::Ret) {
        auto retInst = dyn_cast<ReturnInst>(trace.second);
        errs() << "1RET: " << *retInst->getReturnValue() << "\n";
        errs() << "5RET: " << *retInst->getParent() << "\n";
        errs() << "6RET: " << retInst->getParent()->getNumUses() << "\n";
        errs() << "7RET: " << *(retInst->getParent()->getPrevNode()) << "\n";
        errs() << "8RET: " << retInst->getParent()->hasOneUse() << "\n";
        for (User *user: retInst->getParent()->users()) {
          errs() << "\tUse:" << *user << "\n";
        }


      }
      return trace;
    }

    for (const auto& node : *callGraph->operator[](current)) {
      Function* next = node.second->getFunction();
      if (next->isDeclarationForLinker()) {
        continue;
      }
      funcQueue.push(next);
    }
  }

  return {};
}

} // namespace llvm