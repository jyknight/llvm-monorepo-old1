//===-- MergeGEP.cpp - Merge GEPs for indexing in arrays ------------ ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// 
//===----------------------------------------------------------------------===//
#define DEBUG_TYPE "gepexprargs"

#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Use.h"
#include <vector>
#include <map>

using namespace llvm;
STATISTIC(numSimplified, "Number of Calls Modified");


namespace {
  class GEPExprArg : public ModulePass {
  public:
    static char ID;
    GEPExprArg() : ModulePass(&ID) {}
    bool runOnModule(Module& M) {
      bool changed;
      do {
        changed = false;
        for (Module::iterator F = M.begin(); F != M.end(); ++F){
          for (Function::iterator B = F->begin(), FE = F->end(); B != FE; ++B) {
            for (BasicBlock::iterator I = B->begin(), BE = B->end(); I != BE;) {
              CallInst *CI = dyn_cast<CallInst>(I++);
              if(!CI)
                continue;

              if(CI->hasByValArgument())
                continue;
              // if the GEP calls a function, that is externally defined,
              // or might be changed, ignore this call site.
              Function *F = CI->getCalledFunction();

              if (!F || (F->isDeclaration() || F->mayBeOverridden())) 
                continue;
              if(F->hasStructRetAttr())
                continue;
              if(F->isVarArg())
                continue;

              // find the argument we must replace
              Function::arg_iterator ai = F->arg_begin(), ae = F->arg_end();
              unsigned argNum = 1;
              for(; argNum < CI->getNumOperands();argNum++, ++ai) {
                if(ai->use_empty())
                  continue;
                if (isa<GEPOperator>(CI->getOperand(argNum)))
                  break;
              }

              // if no argument was a GEP operator to be changed 
              if(ai == ae)
                continue;

              GEPOperator *GEP = dyn_cast<GEPOperator>(CI->getOperand(argNum));
              if(!GEP->hasAllConstantIndices())
                continue;

              // Construct the new Type
              // Appends the struct Type at the beginning
              std::vector<const Type*>TP;
              TP.push_back(GEP->getPointerOperand()->getType());
              for(unsigned c = 1; c < CI->getNumOperands();c++) {
                TP.push_back(CI->getOperand(c)->getType());
              }

              //return type is same as that of original instruction
              const FunctionType *NewFTy = FunctionType::get(CI->getType(), TP, false);
              Function *NewF;
              numSimplified++;
              if(numSimplified > 800) 
                return true;

              NewF = Function::Create(NewFTy,
                                      GlobalValue::InternalLinkage,
                                      F->getNameStr() + ".TEST",
                                      &M);

              Function::arg_iterator NI = NewF->arg_begin();
              NI->setName("Sarg");
              ++NI;

              DenseMap<const Value*, Value*> ValueMap;

              for (Function::arg_iterator II = F->arg_begin(); NI != NewF->arg_end(); ++II, ++NI) {
                ValueMap[II] = NI;
                NI->setName(II->getName());
              }
              // Perform the cloning.
              SmallVector<ReturnInst*,100> Returns;
              CloneFunctionInto(NewF, F, ValueMap, Returns);
              std::vector<Value*> fargs;
              for(Function::arg_iterator ai = NewF->arg_begin(), 
                  ae= NewF->arg_end(); ai != ae; ++ai) {
                fargs.push_back(ai);
              }

              NewF->setAlignment(F->getAlignment());
              //Get the point to insert the GEP instr.
              NI = NewF->arg_begin();
              SmallVector<Value*, 8> Ops(CI->op_begin()+1, CI->op_end());
              Instruction *InsertPoint;
              for (BasicBlock::iterator insrt = NewF->front().begin(); isa<AllocaInst>(InsertPoint = insrt); ++insrt) {;}

              SmallVector<Value*, 8> Indices;
              Indices.append(GEP->op_begin()+1, GEP->op_end());
              GetElementPtrInst *GEP_new = GetElementPtrInst::Create(cast<Value>(NI), Indices.begin(), Indices.end(), "", InsertPoint);
              fargs.at(argNum)->replaceAllUsesWith(GEP_new);
              unsigned j = argNum + 1;
              for(; j < CI->getNumOperands();j++) {
                if(CI->getOperand(j) == GEP)
                  fargs.at(j)->replaceAllUsesWith(GEP_new);
              }

              SmallVector<Value*, 8> Args;
              Args.push_back(GEP->getPointerOperand());
              for(unsigned j =1;j<CI->getNumOperands();j++) {
                Args.push_back(CI->getOperand(j));
              }
              CallInst *CallI = CallInst::Create(NewF,Args.begin(), Args.end(),"", CI);
              CallI->setCallingConv(CI->getCallingConv());
              CI->replaceAllUsesWith(CallI);
              CI->eraseFromParent();
              changed = true;
            }
          }
        }
      } while(changed);
      return true;
    }
  };
}

char GEPExprArg::ID = 0;
static RegisterPass<GEPExprArg>
X("gep-expr-arg", "Find GEP Exprs passed as args");
