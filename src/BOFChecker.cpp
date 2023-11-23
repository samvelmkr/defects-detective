#include "BOFChecker.h"

namespace llvm {

BOFChecker::BOFChecker(const std::unordered_map<Function *, std::shared_ptr<llvm::FuncAnalyzer>> &funcInfos)
    : funcAnalysis(funcInfos) {}

unsigned int BOFChecker::GetFormatStringSize(GlobalVariable *var) {
  if (Constant *formatStringConst = var->getInitializer()) {
    if (auto *formatArray = dyn_cast<ConstantDataArray>(formatStringConst)) {
      StringRef formatString = formatArray->getAsString();
      std::stringstream ss(formatString.data());

      char c;
      while (ss >> c) {
        if (c == '%') {
          c = (char) ss.peek();
          if (c && !isdigit(c)) {
            return 0;
          } else {
            unsigned int res;
            ss >> res;
            return res;
          }
        }
      }

    }
  }
  return 0;
}

unsigned int BOFChecker::GetArraySize(AllocaInst *pointerArray) {
  Type *basePointerType = pointerArray->getAllocatedType();
  if (auto *arrType = dyn_cast<ArrayType>(basePointerType)) {
    return arrType->getNumElements();
  }
  return 0;
}

std::pair<Instruction *, Instruction *> BOFChecker::ScanfValidation(Function *function) {
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
  auto scanfCalls = funcInfo->getCalls(CallInstruction::Scanf);
  if (scanfCalls.empty()) {
    return {};
  }

  for (Instruction *inst : scanfCalls) {
    auto *call = dyn_cast<CallInst>(inst);

    Value *formatStringArg = call->getOperand(0);
    auto *bufArg = dyn_cast<GetElementPtrInst>(call->getOperand(1));

    Value *basePointer = bufArg->getPointerOperand();
    auto *basePointerArr = dyn_cast<AllocaInst>(basePointer);
    if (!basePointer) {
      return {};
    }

    if (auto *formatStringGV = dyn_cast<GlobalVariable>(formatStringArg->stripPointerCasts())) {
      unsigned formatStringSize = GetFormatStringSize(formatStringGV);
      if (!formatStringSize) {
        return {basePointerArr, inst};
      }
      if (formatStringSize >= GetArraySize(basePointerArr)) {
        return {basePointerArr, inst};
      }
    }

  }

  return {};
}

void BOFChecker::SetLoopScope(Function *function) {
  BasicBlock *header = li->GetHeader();
  Instruction *latch = li->GetLatch();

  std::vector<BasicBlock *> scope;
  bool startLoopScope = false;
  BasicBlock *endLoop = latch->getParent();
  for (auto &bb : *function) {
    if (&bb == header) {
      startLoopScope = true;
    }
    if (!startLoopScope) {
      continue;
    }
    scope.push_back(&bb);
    if (&bb == endLoop) {
      break;
    }
  }

  li->SetScope(scope);
}

void BOFChecker::SetLoopHeaderInfo() {
  Instruction *condition = li->GetCondition();
  auto *opInst1 = dyn_cast<Instruction>(condition->getOperand(0));
  Instruction *loopVar;

  FuncAnalyzer *funcInfo = funcAnalysis[opInst1->getFunction()].get();
  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                opInst1,
                [&loopVar](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Alloca) {
                    loopVar = curr;
                    return true;
                  }
                  return false;
                });

  li->SetLoopVar(loopVar);

  auto *opInst2 = dyn_cast<Instruction>(condition->getOperand(1));
  Instruction *loopSize;
  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                opInst2,
                [&loopSize](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Alloca) {
                    loopSize = curr;
                    return true;
                  }
                  return false;
                });

  std::pair<int64_t, int64_t> range = {variableValues[loopVar->getName().str()],
                                       variableValues[loopSize->getName().str()]};

  // validate only ICMP_SLT and ICMP_SLE
  auto predicate = li->GetPredicate();
  if (predicate == CmpInst::ICMP_SLT) {
    --range.second;
  }

  li->SetRange(range);
}

void BOFChecker::LoopDetection(Function *function) {
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();

  Instruction *start = &*function->getEntryBlock().begin();

  //Todo: store vector of Instructions if there are more than one loop
  Instruction *latch;

  funcInfo->DFS(AnalyzerMap::ForwardFlowMap,
                start,
                [](Instruction *curr) {
                  // termination condition
                  return false;
                },
                nullptr, // continue condition
                [&latch](Instruction *inst) {
                  // loop info
                  latch = inst;
                  // Todo: validate also latch/exit (conditional br: one edge - exit, other - backEdge)
                  return true;
                });

  if (!latch) {
    return;
  }

  if (latch->getOpcode() != Instruction::Br) {
    return;
  }
  auto *brInst = dyn_cast<BranchInst>(latch);
  if (!brInst->isUnconditional()) {
    return;
  }

  BasicBlock *header = brInst->getSuccessor(0);
  li = std::make_unique<LoopsInfo>(header, latch);
  SetLoopScope(function);
}

size_t BOFChecker::GetMallocedSize(Instruction *malloc) {
  if (auto *constInt = dyn_cast<ConstantInt>(malloc->getOperand(0))) {
    uint64_t size = constInt->getZExtValue();
    return static_cast<size_t>(size);
  }
  auto *opInst = dyn_cast<Instruction>(malloc->getOperand(0));

  Instruction *sizeInst;
  FuncAnalyzer *funcInfo = funcAnalysis[malloc->getFunction()].get();
  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                opInst,
                [&sizeInst](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Load ||
                      curr->getOpcode() == Instruction::Alloca) {
                    sizeInst = curr;
                    return true;
                  }
                  return false;
                });

  if (variableValues.find(sizeInst->getName().str()) == variableValues.end()) {
    llvm::report_fatal_error("Cannot find malloc size");
  }

  int64_t size = variableValues[sizeInst->getName().str()];
  if (size < 0) {
    llvm::report_fatal_error("Negative size of allocated memory");
  }
  return static_cast<size_t>(size);
}

size_t BOFChecker::GetGepOffset(GetElementPtrInst *gep) {
  if (auto *constInt = dyn_cast<ConstantInt>(gep->getOperand(1))) {
    uint64_t offset = constInt->getZExtValue();
    return static_cast<size_t>(offset);
  }

  FuncAnalyzer *funcInfo = funcAnalysis[gep->getFunction()].get();
  auto *opInst = dyn_cast<Instruction>(gep->getOperand(1));

  Instruction *offsetInst;

  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                opInst,
                [&offsetInst](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Load ||
                      curr->getOpcode() == Instruction::Alloca) {
                    offsetInst = curr;
                    return true;
                  }
                  return false;
                });

  if (variableValues.find(offsetInst->getName().str()) == variableValues.end()) {
    llvm::report_fatal_error("Cannot find offset");
  }

  int64_t offset = variableValues[offsetInst->getName().str()];
  if (offset < 0) {
    llvm::report_fatal_error("Negative offset of allocated memory");
  }
  return static_cast<size_t>(offset);
}

std::string BOFChecker::GetGepVarName(GetElementPtrInst* gep) {
  auto* pointerOp = dyn_cast<Instruction>(gep->getPointerOperand());
  Instruction* allocaInst;
  if (auto loadOp = dyn_cast<LoadInst>(pointerOp)) {
    allocaInst = dyn_cast<Instruction>(loadOp->getPointerOperand());
  } else {
    allocaInst = dyn_cast<Instruction>(gep->getPointerOperand());
  }
  int64_t offset = CalculateOffset(gep);
  return allocaInst->getName().str() + "." + std::to_string(offset);
}

void BOFChecker::ValueAnalysis(Instruction *inst) {
  if (li && inst == &*(li->GetHeader()->begin())) {
    SetLoopHeaderInfo();
  }

  if (inst->getOpcode() == Instruction::Alloca) {
    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()] = 0;

  } else if (inst->getOpcode() == Instruction::Store) {
    auto *storeInst = dyn_cast<StoreInst>(inst);
    Value *storedValue = storeInst->getValueOperand();
    if (!isa<ConstantInt>(storedValue)) {
      return;
    }
    auto *constValue = dyn_cast<ConstantInt>(storedValue);
    auto* pointerOp = storeInst->getPointerOperand();
    std::string operandName;
    if (auto* gep = dyn_cast<GetElementPtrInst>(pointerOp)) {
      operandName = GetGepVarName(gep);
    } else {
      operandName = storeInst->getPointerOperand()->getName().str();
    }
    variableValues[operandName] = constValue->getSExtValue();

  } else if (auto *load = dyn_cast<LoadInst>(inst)) {
    int64_t loadedValue = variableValues[load->getPointerOperand()->getName().str()];
    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()] = loadedValue;

  } else if (inst->getOpcode() == Instruction::Sub) {
    auto *op1 = dyn_cast<Instruction>(inst->getOperand(0));
    Value *op2 = inst->getOperand(1);
    if (!isa<ConstantInt>(op2)) {
      return;
    }
    auto *constValue = dyn_cast<ConstantInt>(op2);
    variableValues[op1->getName().str()] = variableValues[op1->getName().str()] - constValue->getSExtValue();

  }else if (inst->getOpcode() == Instruction::GetElementPtr) {
    auto* gep = dyn_cast<GetElementPtrInst>(inst);
    if (!isa<Instruction>(gep->getPointerOperand())) {
      return;
    }
    variableValues[GetGepVarName(gep)] = 0;
  }

}

std::vector<Instruction *> BOFChecker::CollectMemcpyInst(Instruction *malloc) {
  FuncAnalyzer *funcInfo = funcAnalysis[malloc->getFunction()].get();
  std::unique_ptr<CallDataDepInfo> callInfo = std::make_unique<CallDataDepInfo>();

  std::vector<Instruction *> memcpies = funcInfo->CollectAllDepInst(malloc, [](Instruction *curr) {
    return IsCallWithName(curr, CallInstruction::Memcpy);
  }, callInfo.get());

  if (callInfo) {
    Function *calledFunc = callInfo->call->getCalledFunction();;
    auto *arg = calledFunc->getArg(callInfo->argNum);
    FuncAnalyzer *calledFuncInfo = funcAnalysis[calledFunc].get();
    std::vector<Instruction *> memcpiesFromCalledFunc =
        calledFuncInfo->CollectSpecialDependenciesOnArg(arg,
                                                        callInfo->argNum,
                                                        [](Instruction *curr) {
                                                          return IsCallWithName(
                                                              curr,
                                                              CallInstruction::Memcpy);
                                                        });
    if (!memcpiesFromCalledFunc.empty()) {
      memcpies.insert(memcpies.end(), memcpiesFromCalledFunc.begin(), memcpiesFromCalledFunc.end());
    }
  }
  return memcpies;
}

bool BOFChecker::AccessToOutOfBoundInCycle(GetElementPtrInst *gep, size_t mallocSize) {
  Function *function = gep->getFunction();
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
  auto *opInst = dyn_cast<Instruction>(gep->getOperand(1));

  Instruction *offsetInst;

  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                opInst,
                [&offsetInst](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Alloca) {
                    offsetInst = curr;
                    return true;
                  }
                  return false;
                });

  if (offsetInst != li->GetLoopVar()) {
    return false;
  }

  std::pair<int64_t, int16_t> range = li->GetRange();
  if (range.first < 0 || range.second < 0) {
    llvm::report_fatal_error("Loop range values are negative");
  }

  return mallocSize <= static_cast<size_t>(range.first) ||
      mallocSize <= static_cast<size_t>(range.second);
}

bool BOFChecker::IsBOFGep(GetElementPtrInst *gep, size_t mallocSize) {
  if (li && li->HasInst(dyn_cast<Instruction>(gep))) {
    return AccessToOutOfBoundInCycle(gep, mallocSize);
  }
  return mallocSize <= GetGepOffset(gep);
}

Instruction *BOFChecker::FindBOFInst(Instruction *inst, size_t mallocSize,
                                     const std::vector<Instruction *> &geps,
                                     const std::vector<Instruction *> &memcpies) {
  // Analyse geps
  if (inst->getOpcode() == Instruction::GetElementPtr &&
      std::find(geps.begin(), geps.end(), inst) != geps.end()) {
    auto *gepInst = dyn_cast<GetElementPtrInst>(inst);
    if (IsBOFGep(gepInst, mallocSize)) {
      return inst;
    }
  }
  // Analyse memcpies
  if (IsCallWithName(inst, CallInstruction::Memcpy) &&
      std::find(memcpies.begin(), memcpies.end(), inst) != memcpies.end()) {
    IsCorrectMemcpy2(inst);
    return nullptr;

    return FindBOFAfterWrongMemcpy(inst);
  }
  return nullptr;
}

// Todo: rename func
// Fixme: Validate multiple malloc with one cycle
Instruction *BOFChecker::ProcessMalloc(MallocedObject *obj) {
  Instruction *malloc = obj->getMallocCall();
  Function *function = malloc->getFunction();
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();

  std::vector<std::unique_ptr<CallDataDepInfo>> externalCalls;
  Instruction *previous = nullptr;
  funcInfo->DFS(AnalyzerMap::ForwardDependencyMap,
                malloc,
                [&previous, &externalCalls](Instruction *curr) {
                  if (auto *cInst = dyn_cast<CallInst>(curr)) {
                    if (!cInst->getCalledFunction()->isDeclarationForLinker() &&
                        !IsCallWithName(dyn_cast<Instruction>(cInst), CallInstruction::Memcpy)) {
                      std::unique_ptr<CallDataDepInfo> callInfo = std::make_unique<CallDataDepInfo>();
                      callInfo->Init(cInst, previous);
                      externalCalls.push_back(std::move(callInfo));
                    }
                  }
                  previous = curr;
                  return false;
                });

  std::vector<Instruction *> frees = obj->getFreeCalls();
  Instruction *start = &*function->getEntryBlock().begin();
  Instruction *bofInst = nullptr;
  size_t mallocSize = 0;

  std::vector<Instruction *> geps = funcInfo->CollectAllGeps(malloc);
  std::vector<Instruction *> memcpies = CollectMemcpyInst(malloc);

  LoopDetection(function);

  funcInfo->DFS(AnalyzerMap::ForwardFlowMap,
                start,
                [malloc, frees, &mallocSize, &bofInst, &geps, &memcpies, this](Instruction *curr) {
                  if (IsCallWithName(curr, CallInstruction::Free) &&
                      std::find(frees.begin(), frees.end(), curr) != frees.end()) {
                    return true;
                  }
                  ValueAnalysis(curr);
                  errs () << "inst:" << *curr << "\n";

                  if (curr == malloc) {
                    mallocSize = GetMallocedSize(malloc);
                  }

                  bofInst = FindBOFInst(curr, mallocSize, geps, memcpies);
                  if (bofInst) {
                    return true;
                  }
                  return false;
                });

  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";
//  funcInfo->printMap(AnalyzerMap::ForwardDependencyMap);
//  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";

  if (!externalCalls.empty()) {
    for (auto &callInfo : externalCalls) {
      Function *calledFunc = callInfo->call->getCalledFunction();
      Instruction *startInst = &*calledFunc->getEntryBlock().begin();

      FuncAnalyzer *calledFuncInfo = funcAnalysis[calledFunc].get();
      calledFuncInfo->DFS(AnalyzerMap::ForwardFlowMap,
                          startInst,
                          [frees, &mallocSize, &bofInst, &geps, &memcpies, this](Instruction *curr) {
                            if (IsCallWithName(curr, CallInstruction::Free) &&
                                std::find(frees.begin(), frees.end(), curr) != frees.end()) {
                              return true;
                            }
                            ValueAnalysis(curr);
                            errs () << "inst:" << *curr << "\n";

                            bofInst = FindBOFInst(curr, mallocSize, geps, memcpies);
                            if (bofInst) {
                              return true;
                            }
                            return false;
                          });
    }
  }

  ClearData();
  return bofInst;
}

void BOFChecker::ClearData() {
  variableValues.clear();
  numOfVariables = 0;
}

std::pair<Instruction *, Instruction *> BOFChecker::OutOfBoundAccessChecker(Function *function) {
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
  for (auto &obj : funcInfo->mallocedObjs) {
//    if (geps.empty()) {
//      continue;
//    }
    if (auto *bofInst = ProcessMalloc(obj.second.get())) {
      return {obj.first, bofInst};
    }

    variableValues.clear();
  }

  return {};
}

bool BOFChecker::IsCorrectMemcpy2(Instruction *inst) {
  auto *mcCall = dyn_cast<CallInst>(inst);
  errs() << *mcCall << "\n";
  for (auto& op : mcCall->operands()) {
    errs() << "op:" << *op << "\n";
  }
  for (auto& val : variableValues) {
    errs() << "\t" << val.first << " = " << val.second << "\n";
  }
  errs() << "{{{{{{{{{{{{{{{{\n";
  auto* mcInst = dyn_cast<MemCpyInst>(inst);
  Value *dest = mcInst->getDest();
  Value *src = mcInst->getSource();
  Value *size = mcInst->getLength();

  errs() << "Found llvm.memcpy call:\n";
  errs() << "  Destination: " << *mcInst->getDest() << "\n";
  errs() << "  Source: " << *mcInst->getSource() << "\n";
  errs() << "  Size: " << *mcInst->getLength() << "\n";

  size_t numericSize = SIZE_MAX;
  if (auto* constSize = dyn_cast<ConstantInt>(size)) {
    numericSize = constSize->getZExtValue();
  } else {

  }




  return false;

  if (!isa<ConstantInt>(mcCall->getOperand(2)) ||
      !isa<GlobalVariable>(mcCall->getOperand(1)->stripPointerCasts())) {
    return true;
  }

  auto *constInt = dyn_cast<ConstantInt>(mcCall->getOperand(2));
  uint64_t mcSize = constInt->getZExtValue();

  auto *strGlobal = dyn_cast<GlobalVariable>(mcCall->getOperand(1)->stripPointerCasts());
  Type *globalVarType = strGlobal->getType()->getPointerElementType();
  if (!isa<ArrayType>(globalVarType)) {
    return true;
  }

  uint64_t sourceArraySize = dyn_cast<ArrayType>(globalVarType)->getNumElements();
  return sourceArraySize == mcSize;
}

bool BOFChecker::IsCorrectMemcpy(Instruction *mcInst) {
  auto *mcCall = dyn_cast<CallInst>(mcInst);

  if (!isa<ConstantInt>(mcCall->getOperand(2)) ||
      !isa<GlobalVariable>(mcCall->getOperand(1)->stripPointerCasts())) {
    return true;
  }

  auto *constInt = dyn_cast<ConstantInt>(mcCall->getOperand(2));
  uint64_t mcSize = constInt->getZExtValue();

  auto *strGlobal = dyn_cast<GlobalVariable>(mcCall->getOperand(1)->stripPointerCasts());
  Type *globalVarType = strGlobal->getType()->getPointerElementType();
  if (!isa<ArrayType>(globalVarType)) {
    return true;
  }

  uint64_t sourceArraySize = dyn_cast<ArrayType>(globalVarType)->getNumElements();
  return sourceArraySize == mcSize;
}

Instruction *BOFChecker::FindStrlenUsage(Instruction *alloca) {
  Function *function = alloca->getFunction();
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
  Instruction *prev;
  std::unique_ptr<CallDataDepInfo> callInfo = std::make_unique<CallDataDepInfo>();
  Instruction *strlenCall;
  funcInfo->HasPathToSpecificTypeOfInst(AnalyzerMap::ForwardDependencyMap,
                                        alloca,
                                        [&strlenCall](Instruction *curr) {
                                          if (IsCallWithName(curr, CallInstruction::Strlen)) {
                                            strlenCall = curr;
                                            return true;
                                          }
                                          return false;
                                        }, callInfo.get());

  if (strlenCall) {
    return strlenCall;
  }

  if (callInfo) {
    Function *calledFunc = callInfo->call->getCalledFunction();;
    auto *arg = calledFunc->getArg(callInfo->argNum);
    FuncAnalyzer *calledFuncInfo = funcAnalysis[calledFunc].get();
    calledFuncInfo->FindSpecialDependenceOnArg(arg,
                                               callInfo->argNum,
                                               [&strlenCall](Instruction *curr) {
                                                 if (IsCallWithName(curr, CallInstruction::Strlen)) {
                                                   strlenCall = curr;
                                                   return true;
                                                 }
                                                 return false;
                                               });
    if (strlenCall) {
      return strlenCall;
    }
  }
  return {};
}

Instruction *BOFChecker::FindBOFAfterWrongMemcpy(Instruction *mcInst) {
  Function *function = mcInst->getFunction();
  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
  Instruction *alloca;

  funcInfo->DFS(AnalyzerMap::BackwardDependencyMap,
                mcInst,
                [&alloca](Instruction *curr) {
                  if (curr->getOpcode() == Instruction::Alloca) {
                    alloca = curr;
                    return true;
                  }
                  return false;
                });

  Instruction *bofInst = FindStrlenUsage(alloca);
  return bofInst;
}

//std::pair<Instruction *, Instruction *> BOFChecker::MemcpyValidation(Function *function) {
//  FuncAnalyzer *funcInfo = funcAnalysis[function].get();
//  auto memcpyCalls = funcInfo->getCalls(CallInstruction::Memcpy);
//  if (memcpyCalls.empty()) {
//    return {};
//  }
//
//  for (Instruction *mcInst : memcpyCalls) {
//    if (IsCorrectMemcpy(mcInst)) {
//      return {};
//    }
//    return FindBOFAfterWrongMemcpy(mcInst);
//  }
//
//  return {};
//}

std::pair<Instruction *, Instruction *> BOFChecker::Check(Function *function) {
  auto scanfBOF = ScanfValidation(function);
  if (scanfBOF.first && scanfBOF.second) {
    return scanfBOF;
  }

  auto outOfBoundAcc = OutOfBoundAccessChecker(function);
  if (outOfBoundAcc.first && outOfBoundAcc.second) {
    return outOfBoundAcc;
  }
//  auto mcBOF = MemcpyValidation(function);
//  if (mcBOF.first && mcBOF.second) {
//    return mcBOF;
//  }
  return {};
}

} // namespace llvm