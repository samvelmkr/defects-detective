#ifndef ANALYZER_SRC_FUNCANALYZER_H
#define ANALYZER_SRC_FUNCANALYZER_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

#include <unordered_set>
#include <utility>

namespace llvm {

struct CallInstructions {
  static const std::string Malloc;
  static const std::string Free;
  static const std::string Scanf;
};

enum AnalyzerMap {
  ForwardDependencyMap,
  BackwardDependencyMap,
  ForwardFlowMap,
  BackwardFlowMap
};

class FuncAnalyzer {
private:
  Instruction* ret;

  std::unordered_map<std::string, std::vector<Instruction *>> callInstructions;

  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> forwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> backwardDependencyMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> forwardFlowMap;
  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> backwardFlowMap;

  static bool IsCallWithName(Instruction *inst, const std::string& name);

  void CollectCalls(Instruction* callInst);

  std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *SelectMap(AnalyzerMap mapID);

  void AddEdge(AnalyzerMap mapID, Instruction *source, Instruction *destination);
  void RemoveEdge(AnalyzerMap mapID, Instruction *source, Instruction *destination);
  bool HasEdge(AnalyzerMap mapID, Instruction *source, Instruction *destination);

  bool ProcessStoreInsts(Instruction* storeInst);

  void UpdateDependencies();
public:
  FuncAnalyzer();
  FuncAnalyzer(Function *func);

};

} // namespace llvm

#endif //ANALYZER_SRC_FUNCANALYZER_H
