#ifndef SIMPLE_PASS_H
#define SIMPLE_PASS_H

#include "Sarif.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

class SimplePass : public PassInfoMixin<SimplePass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
  void analyze(Module &M);
  std::string getFunctionLocation(const Function *Func);
  SmallVector<std::pair<std::string, SmallVector<unsigned>>> getAllFunctionsTrace(Module &M);
  unsigned getFunctionLine(const Function *Func);
  unsigned getFunctionColumn(const Function *Func);
};

#endif // SIMPLE_PASS_H
