#include "SimplePass.h"

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

SmallVector<std::pair<std::string, SmallVector<unsigned>>>
SimplePass::getAllFunctionsTrace(Module &M) {
  std::string FilePath;
  unsigned LineNum, ColumnNum;
  SmallVector<std::pair<std::string, SmallVector<unsigned>>> Trace;

  for (auto &Func : M.getFunctionList()) {
    if (Func.isDeclarationForLinker())
      continue;

    FilePath = getFunctionLocation(&Func);
    LineNum = getFunctionLine(&Func);
    ColumnNum = getFunctionColumn(&Func);
    Trace.emplace_back(FilePath, SmallVector<unsigned>{LineNum, ColumnNum});
  }

  return Trace;
}

unsigned SimplePass::getFunctionLine(const Function *Func) {
  for (auto InstIt = inst_begin(Func), ItEnd = inst_end(Func); InstIt != ItEnd; ++InstIt) {
    if (DILocation *Location = InstIt->getDebugLoc()) {
      return Location->getLine();
    }
  }
  return 0;
}

unsigned SimplePass::getFunctionColumn(const Function *Func) {
  for (auto InstIt = inst_begin(Func), ItEnd = inst_end(Func); InstIt != ItEnd; ++InstIt) {
    if (DILocation *Location = InstIt->getDebugLoc()) {
      return Location->getColumn();
    }
  }
  return 0;
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
  /// These properties are added for example, you must generate a trace according to the report.
  SmallVector<std::pair<std::string, SmallVector<unsigned>>> Trace = getAllFunctionsTrace(M);

  GenSarif.addResult(BugReport(Trace, "use-after-free", 2));

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
