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

void BOFChecker::ValueAnalysis() {
  size_t numOfVariables = 0;

  for (auto &bb : *function) {
    errs() << "Analyzing Basic Block: " << bb.getName() << "\n";
    for (auto &inst : bb) {
      if (inst.getOpcode() == Instruction::Alloca) {
        if (!inst.hasName()) {
          inst.setName("var" + std::to_string(++numOfVariables));
        }
        variableValues[inst.getName().str()] = dyn_cast<Value>(&inst);

      } else if (inst.getOpcode() == Instruction::Store) {
        auto *storeInst = dyn_cast<StoreInst>(&inst);
        Value *storedValue = storeInst->getValueOperand();
        if (!isa<Constant>(storedValue)) {
          continue;
        }
        errs() << inst << "\n\tpointer op: " << *storeInst->getPointerOperand() << "\n";
        errs() << "storedValue" << *storedValue << "\n\n";
        variableValues[storeInst->getPointerOperand()->getName().str()] = storedValue;
//      } else if (LoadInst *Load = dyn_cast<LoadInst>(&I)) {
//        // Track the values loaded from variables
//        Value *LoadedValue = variableValues[Load->getPointerOperand()->getName()];
//        errs() << "Loaded Value: " << *LoadedValue << "\n";
      }
    }
  }
}

std::pair<Instruction *, Instruction *> BOFChecker::OutOfBoundAccessChecker() {

//    errs() << "\n~~~~~~~~~~~~~~~~~~FLOW~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::ForwardFlowMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~~DATA~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::ForwardDependencyMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~BDATA~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//  funcInfo->printMap(AnalyzerMap::BackwardDependencyMap);
//  errs() << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
  ValueAnalysis();
  errs() << "\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n";
  for (auto &obj : funcInfo->mallocedObjs) {
    Instruction* malloc = obj.first;
    errs() << *obj.first << "\n\t base: " << *obj.second->getBaseInst() << "\n";
    Instruction *base = obj.second->getBaseInst();
    errs() << base->getName() << "\n";
    funcInfo->DFS(AnalyzerMap::ForwardDependencyMap, malloc, [](Instruction* curr){
      if (auto* gep = dyn_cast<GetElementPtrInst>(curr)) {
        errs() << "op: " <<  *gep->getOperand(0) << "\n";
        errs() << "op: " <<  *gep->getOperand(1) << "\n";
        errs() << "pointerop" << *gep->getPointerOperand() << "\n";
      }
      return false;
    });
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