#include "FuncAnalyzer.h"

namespace llvm {

const std::string CallInstructions::Malloc = "malloc";
const std::string CallInstructions::Free = "free";
const std::string CallInstructions::Scanf = "__isoc99_scanf";

bool FuncAnalyzer::IsCallWithName(Instruction *inst, const std::string &name) {
  if (auto *callInst = dyn_cast<CallInst>(inst)) {
    Function *calledFunc = callInst->getCalledFunction();
    return calledFunc->getName().str() == name;
  }
  return false;
}

void FuncAnalyzer::CollectCalls(Instruction *callInst) {
  if (IsCallWithName(callInst, CallInstructions::Malloc)) {
    callInstructions[CallInstructions::Malloc].push_back(callInst);
  }
  if (IsCallWithName(callInst, CallInstructions::Free)) {
    callInstructions[CallInstructions::Free].push_back(callInst);
  }
  if (IsCallWithName(callInst, CallInstructions::Scanf)) {
    callInstructions[CallInstructions::Scanf].push_back(callInst);
  }
}

std::unordered_map<Instruction *, std::unordered_set<Instruction *>> *FuncAnalyzer::SelectMap(AnalyzerMap mapID) {
  switch (mapID) {
  case AnalyzerMap::ForwardDependencyMap:return &forwardDependencyMap;
  case AnalyzerMap::BackwardDependencyMap:return &backwardDependencyMap;
  case AnalyzerMap::ForwardFlowMap:return &forwardFlowMap;
  case AnalyzerMap::BackwardFlowMap:return &backwardFlowMap;
  }
  llvm::report_fatal_error("Not found corresponding map.");
}

void FuncAnalyzer::AddEdge(llvm::AnalyzerMap mapID, llvm::Instruction *source, llvm::Instruction *destination) {
  auto map = SelectMap(mapID);
  map->operator[](source).insert(destination);
}

bool FuncAnalyzer::HasEdge(llvm::AnalyzerMap mapID, llvm::Instruction *Source, llvm::Instruction *Destination) {
  auto map = SelectMap(mapID);
  auto SourceIt = map->find(Source);
  if (SourceIt != map->end()) {
    return SourceIt->second.find(Destination) != SourceIt->second.end();
  }
  return false;
}

void FuncAnalyzer::RemoveEdge(llvm::AnalyzerMap mapID, llvm::Instruction *Source, llvm::Instruction *Destination) {
  auto map = SelectMap(mapID);
  if (HasEdge(mapID, Source, Destination)) {
    map->operator[](Source).erase(Destination);
  }
}

bool FuncAnalyzer::ProcessStoreInsts(Instruction *Inst) {
  auto *SInst = dyn_cast<StoreInst>(Inst);
  Value *FirstOp = SInst->getValueOperand();
  Value *SecondOp = SInst->getPointerOperand();
  if (!isa<Constant>(FirstOp)) {
    auto *FromInst = dyn_cast<Instruction>(FirstOp);
    auto *ToInst = dyn_cast<Instruction>(SecondOp);
    if (!HasEdge(AnalyzerMap::ForwardDependencyMap, FromInst, ToInst)) {
      AddEdge(AnalyzerMap::ForwardDependencyMap, FromInst, ToInst);
    }
    if (!HasEdge(AnalyzerMap::BackwardDependencyMap, ToInst, FromInst)) {
      AddEdge(AnalyzerMap::BackwardDependencyMap, ToInst, FromInst);
    }
    return true;
  }
  return false;
}

void FuncAnalyzer::UpdateDependencies() {
  if (callInstructions.empty() ||
      callInstructions.find(CallInstructions::Malloc) == callInstructions.end()) {
    return;
  }

  for (Instruction* CInst : callInstructions.at(CallInstructions::Malloc)) {
    for (auto& DependentInst : forwardDependencyMap[CInst]) {
      if (DependentInst->getOpcode() == Instruction::GetElementPtr) {
        auto* gepInst =
      }
    }
  }
}

FuncAnalyzer::FuncAnalyzer(llvm::Function *Func) {
  const BasicBlock &lastBB = *(--(Func->end()));
  if (!lastBB.empty()) {
    Ret = const_cast<Instruction *>(&*(--(lastBB.end())));
  }

  for (auto &BB : *Func) {
    for (auto &Inst : BB) {
      if (Inst.getOpcode() == Instruction::Call) {
        collectCalls(&Inst);
      }

      auto Uses = Inst.uses();
      if (Uses.empty()) {
        continue;
      }
      for (auto &use : Uses) {
        if (auto *DependentInst = dyn_cast<llvm::Instruction>(use.getUser())) {
          if (DependentInst->getOpcode() == Instruction::Store &&
              processStoreInsts(DependentInst)) {
            continue;
          }

          addEdge(AnalyzerMap::ForwardDependencyMap, &Inst, DependentInst);
          addEdge(AnalyzerMap::BackwardDependencyMap, DependentInst, &Inst);
        }
      }

    }
  }

  // GEP validation
  UpdateDependencies();

  collectMallocedObjs();

  // create cfg
  constructFlow(Func);
}

} // namespace llvm
