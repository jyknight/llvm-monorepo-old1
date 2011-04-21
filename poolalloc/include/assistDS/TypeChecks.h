//===------------- TypeChecks.h - Insert runtime type checks --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass inserts checks to enforce type safety during runtime.
//
//===----------------------------------------------------------------------===//

#ifndef TYPE_CHECKS_H
#define TYPE_CHECKS_H

#include "assistDS/TypeAnalysis.h"

#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetData.h"

#include <map>

namespace llvm {

class Type;
class Value;

class TypeChecks : public ModulePass {
private:
  unsigned int maxType;
  std::map<const Type *, unsigned int> UsedTypes;
  std::map<const Value *, const Type *> UsedValues;

  // Analysis from other passes.
  TargetData *TD;

  // Incorporate one type and all of its subtypes into the collection of used types.
  void IncorporateType(const Type *Ty);

  // Incorporate all of the types used by this value.
  void IncorporateValue(const Value *V);

public:
  static char ID;
  TypeChecks() : ModulePass(&ID) {}
  virtual bool runOnModule(Module &M);
  virtual void print(raw_ostream &OS, const Module *M) const;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<TargetData>();
    AU.addRequired<TypeAnalysis>();
  }

  bool initShadow(Module &M, Instruction &I);
  bool unmapShadow(Module &M, Instruction &I);
  bool visitLoadInst(Module &M, LoadInst &LI);
  bool visitGlobal(Module &M, GlobalVariable &GV, 
                   Constant *C, Instruction &I, unsigned offset);
  bool visitStoreInst(Module &M, StoreInst &SI);
  bool visitCopyingStoreInst(Module &M, StoreInst &SI, Value *SS);
  bool visitInputFunctionValue(Module &M, Value *V, CallInst *CI);

  // Return the map containing all of the types used in the module.
  const std::map<const Type *, unsigned int> &getTypes() const {
    return UsedTypes;
  }
};

} // End llvm namespace

#endif
