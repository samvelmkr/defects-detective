#include "SimplePass.h"
#include "Checker.h"

std::string SimplePass::getFunctionLocation(const Function *Func) {
  for (auto InstIt = inst_begin(Func), ItEnd = inst_end(Func); InstIt != ItEnd; ++InstIt) {
    if (DILocation *Location = InstIt->getDebugLoc()) {
      std::filesystem::path FilePathWithPrefix = std::filesystem::path("file://");
      std::filesystem::path FilePath = Location->getFilename().str();
      if (FilePath.is_absolute()) {
        FilePathWithPrefix /= FilePath;
        return FilePathWithPrefix.string();
      }
      std::filesystem::path Directory = Location->getDirectory().str();
      Directory /= FilePath;
      FilePathWithPrefix += Directory;
      return FilePathWithPrefix.string();
    }
  }
  return "";
}

SmallVector<std::pair<std::string, unsigned>>
SimplePass::getAllFunctionsTrace(Module &M) {
  std::string FilePath;
  unsigned LineNum;
  SmallVector<std::pair<std::string, unsigned>> Trace;

  for (auto &Func : M.getFunctionList()) {
    if (Func.isDeclarationForLinker())
      continue;

    FilePath = getFunctionLocation(&Func);
    LineNum = getFunctionFirstLine(&Func);
    Trace.emplace_back(FilePath, LineNum);
  }

  return Trace;
}



unsigned SimplePass::getInstructionLine(const Instruction *Inst) {
  if (DILocation *Location = Inst->getDebugLoc()) {
    return Location->getLine();
  } else {
    const Function* Func = Inst->getFunction();
    for (auto InstIt = inst_begin(Func), ItEnd = inst_end(Func); InstIt != ItEnd; ++InstIt) {
      if (auto* dbgDeclare = dyn_cast<DbgDeclareInst>(&*InstIt)) {
        auto* declaredInst = dyn_cast<Instruction>(dbgDeclare->getAddress());
        if (declaredInst && declaredInst == Inst) {
          return dbgDeclare->getDebugLoc().getLine();
        }
      }
    }
  }
  return -1;
}

unsigned SimplePass::getFunctionFirstLine(const Function *Func) {
  const BasicBlock &entryBB = Func->getEntryBlock();
  if (!entryBB.empty()) {
    return getInstructionLine(&entryBB.front());
  }
  return -1;
}

unsigned SimplePass::getFunctionLastLine(const Function *Func) {
  const BasicBlock &lastBB = *(--(Func->end()));
  if (!lastBB.empty()) {
    return getInstructionLine(&*(--(lastBB.end())));
  }
  return -1;
}

SmallVector<std::pair<std::string, unsigned>> SimplePass::createMemLeakTrace(Instruction *mallocCall) {
  if (!mallocCall) {
    return {};
  }
  SmallVector<std::pair<std::string, unsigned>> Trace;

  std::string FilePath = getFunctionLocation(mallocCall->getFunction());
  unsigned mallocLine = getInstructionLine(mallocCall);
  unsigned expectedFreeLine = getFunctionLastLine(mallocCall->getFunction());
  Trace.emplace_back(FilePath, mallocLine);
  Trace.emplace_back(FilePath, expectedFreeLine);
  return Trace;
}

SmallVector<std::pair<std::string, unsigned>> SimplePass::createTraceOfPairInst(Instruction *start, Instruction *end) {
  if (!start || !end) {
    return {};
  }
  SmallVector<std::pair<std::string, unsigned>> Trace;

  std::string FilePath = getFunctionLocation(start->getFunction());
  unsigned startLine = getInstructionLine(start);
  unsigned endLIne = getInstructionLine(end);
  Trace.emplace_back(FilePath, startLine);
  Trace.emplace_back(FilePath, endLIne);
  return Trace;
}

PreservedAnalyses SimplePass::run(Module &M, ModuleAnalysisManager &) {
  analyze(M);
  return PreservedAnalyses::all();
}

void SimplePass::analyze(Module &M) {
  if (M.getFunctionList().empty()) {
    return;
  }

  for (auto &Func : M.getFunctionList()) {
    if (Func.isDeclarationForLinker()) {
      continue;
    }
    Checker analyzer;
    analyzer.collectDependencies(&Func);

    Sarif GenSarif;

    if (Instruction *mlLoc = analyzer.MallocFreePathChecker()) {
      SmallVector<std::pair<std::string, unsigned>> Trace = createMemLeakTrace(mlLoc);
      GenSarif.addResult(BugReport(Trace, "memory-leak", 1));
    }

    if (InstructionPairPtr::Ptr uafLoc = analyzer.UseAfterFreeChecker()) {
      SmallVector<std::pair<std::string, unsigned>> Trace = createTraceOfPairInst(uafLoc->first, uafLoc->second);
      GenSarif.addResult(BugReport(Trace, "use-after-free", 0));
    }

    if (InstructionPairPtr::Ptr bofLoc = analyzer.BuffOverflowChecker()) {
      SmallVector<std::pair<std::string, unsigned>> Trace = createTraceOfPairInst(bofLoc->first, bofLoc->second);
      GenSarif.addResult(BugReport(Trace, "buffer-overflow", 2));
    }

    GenSarif.save();
  }
}

/// Register the pass.
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "simple", LLVM_VERSION_STRING, [](PassBuilder &PB) {
    PB.registerPipelineParsingCallback([&](StringRef Name, ModulePassManager &MPM,
                                           ArrayRef<PassBuilder::PipelineElement>) {
      if (Name == "simple") {
        MPM.addPass(SimplePass());
        return true;
      }
      return false;
    });
  }};
}
