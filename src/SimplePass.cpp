#include "SimplePass.h"
#include "Analyzer.h"

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

  Sarif GenSarif;

  auto analyzer = std::make_shared<Analyzer>(M);
  auto mlLoc = analyzer->MLCheck();
  if (mlLoc) {
//    errs() << mlLoc->getType().first << ": " << *mlLoc->getTrace().first << "|" <<  *mlLoc->getTrace().second << "\n";
    auto Trace = createTraceOfPairInst(mlLoc->getTrace().first, mlLoc->getTrace().second);
    GenSarif.addResult(BugReport(Trace, mlLoc->getType().first, mlLoc->getType().second));
    GenSarif.save();
    return;
  }

  auto uafLoc = analyzer->UAFCheck();
  if (uafLoc) {
//    errs() << uafLoc->getType().first << ": " << *uafLoc->getTrace().first << "|" <<  *uafLoc->getTrace().second << "\n";
    auto Trace = createTraceOfPairInst(uafLoc->getTrace().first, uafLoc->getTrace().second);
    GenSarif.addResult(BugReport(Trace, uafLoc->getType().first, uafLoc->getType().second));
    GenSarif.save();
    return;
  }

  auto bofLoc = analyzer->BOFCheck();
  if (bofLoc) {
//    errs() << bofLoc->getType().first << ": " << *bofLoc->getTrace().first << "|" <<  *bofLoc->getTrace().second << "\n";
    auto Trace = createTraceOfPairInst(bofLoc->getTrace().first, bofLoc->getTrace().second);
    GenSarif.addResult(BugReport(Trace, bofLoc->getType().first, bofLoc->getType().second));
    GenSarif.save();
    return;
  }

  GenSarif.save();
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
