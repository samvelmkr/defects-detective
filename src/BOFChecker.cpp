#include "BOFChecker.h"

namespace llvm {

BOFChecker::BOFChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}

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

std::pair<Instruction *, Instruction *> BOFChecker::ScanfValidation() {
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

size_t BOFChecker::GetMallocedSize(llvm::Instruction *malloc) {
  auto *opInst = dyn_cast<Instruction>(malloc->getOperand(0));

  Instruction *sizeInst;

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

  if (variableValues.find(sizeInst->getName().str()) == variableValues.end() ||
      !variableValues[sizeInst->getName().str()]) {
    llvm::report_fatal_error("Cannot find malloc size");
  }

  int64_t size = variableValues[sizeInst->getName().str()];
  if (size < 0) {
    llvm::report_fatal_error("Negative size of allocated memory");
  }
  return static_cast<size_t>(size);
}

size_t BOFChecker::GetGepOffset(GetElementPtrInst *gep) {
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

  if (variableValues.find(offsetInst->getName().str()) == variableValues.end() ||
      !variableValues[offsetInst->getName().str()]) {
    llvm::report_fatal_error("Cannot find offset");
  }

  int64_t offset = variableValues[offsetInst->getName().str()];
  if (offset < 0) {
    llvm::report_fatal_error("Negative offset of allocated memory");
  }
  return static_cast<size_t>(offset);
}

void BOFChecker::ValueAnalysis(Instruction *inst) {

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
    variableValues[storeInst->getPointerOperand()->getName().str()] = constValue->getSExtValue();

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

  }

}

Instruction *BOFChecker::ProcessMalloc(Instruction *malloc, const std::vector<Instruction *> &geps) {
  Instruction *start = &*function->getEntryBlock().begin();
  Instruction *bofInst = nullptr;
  size_t mallocSize = 0;
  size_t numOfGeps = geps.size();
  funcInfo->DFS(AnalyzerMap::ForwardFlowMap,
                start,
                [malloc, &mallocSize, geps, &bofInst, &numOfGeps, this](Instruction *curr) {
                  if (!numOfGeps) {
                    return true;
                  }
                  ValueAnalysis(curr);

                  if (curr == malloc) {
                    mallocSize = GetMallocedSize(malloc);
                  }
                  if (curr->getOpcode() == Instruction::GetElementPtr &&
                      std::find(geps.begin(), geps.end(), curr) != geps.end()) {
                    auto *gepInst = dyn_cast<GetElementPtrInst>(curr);
                    if (mallocSize <= GetGepOffset(gepInst)) {
                      bofInst = curr;
                      return true;
                    }
                    --numOfGeps;
                  }
                  return false;
                });

  ClearData();
  return bofInst;
}

void BOFChecker::ClearData() {
  variableValues.clear();
  numOfVariables = 0;
}

std::pair<Instruction *, Instruction *> BOFChecker::OutOfBoundAccessChecker() {

//    errs() << "\n~~~~~~~~~~~~~~~~~~FLOW~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::ForwardFlowMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~~DATA~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::ForwardDependencyMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~BDATA~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::BackwardDependencyMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";

  for (auto &obj : funcInfo->mallocedObjs) {
    std::vector<Instruction *> geps = funcInfo->CollecedAllGeps(obj.first);
    if (geps.empty()) {
      continue;
    }

    if (auto *bofInst = ProcessMalloc(obj.first, geps)) {
      return {obj.first, bofInst};
    }

    variableValues.clear();
  }

  return {};
}

std::pair<Instruction *, Instruction *> BOFChecker::Check() {
  auto scanfBOF = ScanfValidation();
  if (scanfBOF.first && scanfBOF.second) {
    return scanfBOF;
  }
  auto outOfBoundAcc = OutOfBoundAccessChecker();
  if (outOfBoundAcc.first && outOfBoundAcc.second) {
    return outOfBoundAcc;
  }
  return {};
}

} // namespace llvm