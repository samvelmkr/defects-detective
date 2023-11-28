#include "BOFChecker.h"

namespace llvm {

BOFChecker::BOFChecker(const std::unordered_map<Function *, std::shared_ptr<FuncInfo>> &funcInfos)
    : Checker(funcInfos) {}

size_t BOFChecker::GetFormatStringSize(GlobalVariable *var) {
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
            size_t res;
            ss >> res;
            return res;
          }
        }
      }

    }
  }
  return 0;
}

std::pair<Instruction *, Instruction *> BOFChecker::ScanfValidation(Function *function) {
  FuncInfo *funcInfo = funcInfos[function].get();
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
      size_t formatStringSize = GetFormatStringSize(formatStringGV);
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

void BOFChecker::SetLoopHeaderInfo(Function *function) {
  FuncInfo *funcInfo = funcInfos[function].get();
  std::shared_ptr<LoopsInfo> loopInfo = funcInfo->GetLoopInfo();
  if (!loopInfo) {
    return;
  }

  Instruction *loopVar = loopInfo->GetLoopVar();
  Instruction *loopSize = loopInfo->GetLoopSize();

  if (!loopVar->hasName() || !loopSize->hasName()) {
    return;
  }

  if (variableValues.find(loopVar->getName().str()) == variableValues.end() ||
      variableValues.find(loopSize->getName().str()) == variableValues.end()) {
    return;
  }
  std::pair<int64_t, int64_t> range = {variableValues[loopVar->getName().str()][0],
                                       variableValues[loopSize->getName().str()][0]};

  funcInfo->SetLoopRange(range);
}

size_t BOFChecker::GetMallocedSize(Instruction *malloc) {
  if (auto *constInt = dyn_cast<ConstantInt>(malloc->getOperand(0))) {
    uint64_t size = constInt->getZExtValue();
    return static_cast<size_t>(size);
  }
  auto *opInst = dyn_cast<Instruction>(malloc->getOperand(0));
  Instruction *sizeInst = GetDeclaration(opInst);

  if (variableValues.find(sizeInst->getName().str()) == variableValues.end()) {
    llvm::report_fatal_error("Cannot find malloc size");
  }

  int64_t size = variableValues[sizeInst->getName().str()][0];
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

  auto *offsetInst = dyn_cast<Instruction>(gep->getOperand(1));

  if (variableValues.find(offsetInst->getName().str()) == variableValues.end()) {
    llvm::report_fatal_error("Cannot find offset");
  }

  int64_t offset = variableValues[offsetInst->getName().str()][0];
  if (offset < 0) {
    llvm::report_fatal_error("Negative offset of allocated memory");
  }
  return static_cast<size_t>(offset);
}

std::string BOFChecker::GetGepVarName(GetElementPtrInst *gep) {
  auto *pointerOp = dyn_cast<Instruction>(gep->getPointerOperand());
  Instruction *allocaInst;
  if (auto loadOp = dyn_cast<LoadInst>(pointerOp)) {
    allocaInst = dyn_cast<Instruction>(loadOp->getPointerOperand());
  } else {
    allocaInst = dyn_cast<Instruction>(gep->getPointerOperand());
  }
  return allocaInst->getName().str();
}

std::string BOFChecker::GetArgName(Argument *arg) {
  std::string funcName = arg->getParent()->getName().str();
  return funcName + "." + arg->getName().str();
}

void BOFChecker::StoreConstant(StoreInst *storeInst) {
  Value *storedValue = storeInst->getValueOperand();
  auto *constValue = dyn_cast<ConstantInt>(storedValue);
  auto *pointerOp = storeInst->getPointerOperand();
  if (auto *gep = dyn_cast<GetElementPtrInst>(pointerOp)) {
    variableValues[GetGepVarName(gep)][GetGepOffset(gep)] = constValue->getSExtValue();
  } else {
    std::string operandName = storeInst->getPointerOperand()->getName().str();
    variableValues[operandName][0] = constValue->getSExtValue();
  }
}

void BOFChecker::StoreInstruction(llvm::StoreInst *storeInst) {
  auto *storedValue = storeInst->getValueOperand();
  auto *pointerOp = storeInst->getPointerOperand();
  if (auto *arg = dyn_cast<Argument>(storedValue)) {
    variableValues[pointerOp->getName().str()] = variableValues[GetArgName(arg)];
  } else if (isa<AllocaInst>(storedValue) || isa<LoadInst>(storedValue)) {
    variableValues[pointerOp->getName().str()] = variableValues[storedValue->getName().str()];
  }
}

void BOFChecker::ValueAnalysis(Instruction *inst) {
  SetLoopHeaderInfo(inst->getFunction());

  if (inst->getOpcode() == Instruction::Alloca) {
    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()].resize(1, 0);

  } else if (inst->getOpcode() == Instruction::Store) {
    auto *storeInst = dyn_cast<StoreInst>(inst);
    Value *storedValue = storeInst->getValueOperand();
    if (isa<ConstantInt>(storedValue)) {
      StoreConstant(storeInst);
    } else {
      StoreInstruction(storeInst);
    }

  } else if (auto *load = dyn_cast<LoadInst>(inst)) {
    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()] = variableValues[load->getPointerOperand()->getName().str()];

  } else if (inst->getOpcode() == Instruction::Sub) {
    auto *op1 = dyn_cast<Instruction>(inst->getOperand(0));
    Value *op2 = inst->getOperand(1);
    if (!isa<ConstantInt>(op2)) {
      return;
    }
    auto *constValue = dyn_cast<ConstantInt>(op2);

    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()].resize(1, 0);
    variableValues[inst->getName().str()][0] = variableValues[op1->getName().str()][0] - constValue->getSExtValue();

  } else if (inst->getOpcode() == Instruction::SExt) {
    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()] = variableValues[inst->getOperand(0)->getName().str()];

  } else if (inst->getOpcode() == Instruction::GetElementPtr) {
    auto *gep = dyn_cast<GetElementPtrInst>(inst);
    if (!isa<Instruction>(gep->getPointerOperand())) {
      return;
    }
    size_t offset = GetGepOffset(gep);

    // Todo: later get size from malloc or alloca if type is array
    if (variableValues[GetGepVarName(gep)].size() < offset + 1) {
      variableValues[GetGepVarName(gep)].resize(offset + 1, 0);
    }

    if (!inst->hasName()) {
      inst->setName("var" + std::to_string(++numOfVariables));
    }
    variableValues[inst->getName().str()].resize(1, 0);
    variableValues[inst->getName().str()][0] = variableValues[GetGepVarName(gep)][offset];

  } else if (inst->getOpcode() == Instruction::Call) {
    auto *call = dyn_cast<CallInst>(inst);
    Function *calledFunc = call->getCalledFunction();
    if (calledFunc->isDeclarationForLinker()) {
      return;
    }

    size_t argNum = 0;
    for (Argument &arg : calledFunc->args()) {
      if (arg.getType()->isPointerTy()) {
//        errs() << arg << arg.getName() << "\n";
        arg.setName(std::to_string(argNum));
        variableValues[GetArgName(&arg)] = variableValues[call->getArgOperand(argNum)->getName().str()];
        return;
      }
      argNum++;
    }

  }
//  else if (auto *iCmp = dyn_cast<ICmpInst>(inst)) {
//    ICmpInst::Predicate predicate = iCmp->getPredicate();
//    if (predicate == CmpInst::ICMP_SGT &&
//        ) {
//
//    }
//    auto *call = dyn_cast<CallInst>(inst);
//    Function *calledFunc = call->getCalledFunction();
//    if (calledFunc->isDeclarationForLinker()) {
//      return;
//    }
//
//    size_t argNum = 0;
//    for (Argument &arg : calledFunc->args()) {
//      if (arg.getType()->isPointerTy()) {
//        errs() << arg << arg.getName() << "\n";
//        arg.setName(std::to_string(argNum));
//        variableValues[GetArgName(&arg)] = variableValues[call->getArgOperand(argNum)->getName().str()];
//        return;
//      }
//      argNum++;
//    }
//
//  }

}

bool BOFChecker::AccessToOutOfBoundInCycle(GetElementPtrInst *gep, size_t mallocSize) {
  FuncInfo *funcInfo = funcInfos[gep->getFunction()].get();
  std::shared_ptr<LoopsInfo> loopInfo = funcInfo->GetLoopInfo();

  auto *opInst = dyn_cast<Instruction>(gep->getOperand(1));
  Instruction *offsetInst = GetDeclaration(opInst);
//  errs() << *offsetInst << "!=" << *loopInfo->GetLoopVar() << "\n";
  if (offsetInst != loopInfo->GetLoopVar()) {
    return false;
  }

  std::pair<int64_t, int16_t> range = loopInfo->GetRange();
  if (range.first < 0 || range.second < 0) {
    llvm::report_fatal_error("Loop range values are negative");
  }
//  errs() << mallocSize << "r: " << range.first << range.second << "\n";
  return mallocSize <= static_cast<size_t>(range.first) ||
      mallocSize <= static_cast<size_t>(range.second);
}

bool BOFChecker::IsBOFGep(GetElementPtrInst *gep, size_t mallocSize) {
  FuncInfo *funcInfo = funcInfos[gep->getFunction()].get();
  std::shared_ptr<LoopsInfo> loopInfo = funcInfo->GetLoopInfo();
  if (loopInfo && loopInfo->HasInst(dyn_cast<Instruction>(gep))) {
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
    if (Instruction *bofInst = MemcpyValidation(inst)) {
//      errs() << "ssss" << *bofInst << "\n";
      return bofInst;
    }
  }
  return nullptr;
}

// Fixme: Validate multiple malloc with one cycle
Instruction *BOFChecker::DetectOutOfBoundAccess(MallocedObject *obj) {
  Instruction *malloc = obj->getMallocCall();
  Function *function = malloc->getFunction();

  std::vector<Instruction *> frees = obj->getFreeCalls();
  Instruction *start = &*function->getEntryBlock().begin();
  Instruction *bofInst = nullptr;
  size_t mallocSize = 0;

  std::vector<Instruction *> geps = CollectAllInstsWithType(function, AnalyzerMap::ForwardDependencyMap, malloc,
                                                            [](Instruction *inst) {
                                                              return inst->getOpcode() == Instruction::GetElementPtr;
                                                            });

  std::vector<Instruction *> memcpies = CollectAllInstsWithType(function, AnalyzerMap::ForwardDependencyMap, malloc,
                                                                [](Instruction *inst) {
                                                                  return IsCallWithName(inst, CallInstruction::Memcpy);
                                                                });

  DFSOptions options;
  options.terminationCondition = [malloc, frees, &mallocSize, &bofInst,
      &geps, &memcpies, this](Value *curr) {
    if (!isa<Instruction>(curr)) {
      return false;
    }
    auto *currInst = dyn_cast<Instruction>(curr);

    if (currInst->getOpcode() == Instruction::Ret ||
        IsCallWithName(currInst, CallInstruction::Free) &&
        std::find(frees.begin(), frees.end(), currInst) != frees.end()) {
      return true;
    }

//    errs() << "inst:" << *currInst << "\n";
    ValueAnalysis(currInst);
//    errs() << "inst:" << *currInst << "\n";
//    printVA();
//    errs() << "\n";
    if (currInst == malloc) {
      mallocSize = GetMallocedSize(malloc);
    }

    auto *foundInst = FindBOFInst(currInst, mallocSize, geps, memcpies);
    if (foundInst) {
      bofInst = foundInst;
      return true;
    }
    return false;
  };

  DFSContext context{AnalyzerMap::ForwardFlowMap, start, options};
  DFSResult result = DFS(context);

//  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";
//  funcInfo->printMap(AnalyzerMap::ForwardDependencyMap);
//  errs() << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`\n";

  ClearData();
  return bofInst;
}

void BOFChecker::ClearData() {
  variableValues.clear();
  numOfVariables = 0;
}

std::pair<Instruction *, Instruction *> BOFChecker::OutOfBoundAccessChecker(Function *function) {
  FuncInfo *funcInfo = funcInfos[function].get();
  for (auto &obj : funcInfo->mallocedObjs) {
//    if (geps.empty()) {
//      continue;
//    }

    if (auto *bofInst = DetectOutOfBoundAccess(obj.second.get())) {
      return {obj.first, bofInst};
    }

    variableValues.clear();
  }

  return {};
}

Instruction *BOFChecker::MemcpyValidation(Instruction *mcInst) {
  auto *mcIntrisic = dyn_cast<MemCpyInst>(mcInst);
  Value *src = mcIntrisic->getSource();
  Value *size = mcIntrisic->getLength();

  uint64_t mcSize = 0;
  uint64_t sourceArraySize = 0;

  if (auto *constInt = dyn_cast<ConstantInt>(size)) {
    mcSize = constInt->getZExtValue();
  } else {
    auto *sizeInst = dyn_cast<Instruction>(size);
    mcSize = variableValues[sizeInst->getName().str()][0];
  }

  if (auto *globalVar = dyn_cast<GlobalVariable>(src)) {
    Type *globalVarType = globalVar->getType()->getPointerElementType();
    if (isa<ArrayType>(globalVarType)) {
      sourceArraySize = dyn_cast<ArrayType>(globalVarType)->getNumElements();
    }

  } else if (auto *gep = dyn_cast<GetElementPtrInst>(src)) {
    mcSize += CalculateOffsetInBits(gep);
    auto *inst = GetDeclaration(dyn_cast<Instruction>(gep->getPointerOperand()));
    sourceArraySize = variableValues[inst->getName().str()].size();
  }

  if (sourceArraySize > mcSize) {
    return FindBOFAfterWrongMemcpy(mcInst);
  }
  if (sourceArraySize < mcSize) {
    return mcInst;
  }

  return nullptr;
}

Instruction *BOFChecker::FindBOFAfterWrongMemcpy(Instruction *mcInst) {
  Instruction *alloca = GetDeclaration(mcInst);

  Instruction *bofInst = FindInstWithType(alloca->getFunction(), AnalyzerMap::ForwardDependencyMap,
                                          alloca, [](Instruction *curr) {
        return IsCallWithName(curr, CallInstruction::Strlen);
      });

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

  return {};
}

void BOFChecker::printVA() {
  for (auto &val : variableValues) {
    errs() << "\t" << val.first << " = { ";
    for (auto &elem : val.second) {
      errs() << elem << " ";
    }
    errs() << "}\n";
  }
}

} // namespace llvm