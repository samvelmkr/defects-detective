#include "BOFChecker.h"

namespace llvm {

BOFChecker::BOFChecker(Function *func, FuncAnalyzer *analyzer)
    : function(func),
      funcInfo(analyzer) {}


unsigned int BOFChecker::GetFormatStringSize(GlobalVariable *var) {
  if (Constant* formatStringConst = var->getInitializer()) {
    if(auto* formatArray = dyn_cast<ConstantDataArray>(formatStringConst)) {
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
  Type * basePointerType = pointerArray->getAllocatedType();
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

std::pair<Instruction *, Instruction *> BOFChecker::OutOfBoundAccessChecker() {
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