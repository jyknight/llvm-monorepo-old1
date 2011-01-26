//=- lib/Analysis/IPA/CallTargets.cpp - Resolve Call Targets --*- C++ -*-=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass uses DSA to map targets of all calls, and reports on if it
// thinks it knows all targets of a given call.
//
// Loop over all callsites, and lookup the DSNode for that site.  Pull the
// Functions from the node as callees.
// This is essentially a utility pass to simplify later passes that only depend
// on call sites and callees to operate (such as a devirtualizer).
//
//===----------------------------------------------------------------------===//

#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"
#include "dsa/CallTargets.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Constants.h"
#include <ostream>
using namespace llvm;

namespace {
  STATISTIC (DirCall, "Number of direct calls");
  STATISTIC (IndCall, "Number of indirect calls");
  STATISTIC (CompleteInd, "Number of complete indirect calls");
  STATISTIC (CompleteEmpty, "Number of complete empty calls");

  RegisterPass<CallTargetFinder> X("calltarget","Find Call Targets (uses DSA)");
}

char CallTargetFinder::ID = 0;

void CallTargetFinder::findIndTargets(Module &M)
{
  EQTDDataStructures* T = &getAnalysis<EQTDDataStructures>();
  const DSCallGraph & callgraph = T->getCallGraph();
  DSGraph* G = T->getGlobalsGraph();
  DSGraph::ScalarMapTy& SM = G->getScalarMap();
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    if (!I->isDeclaration())
      for (Function::iterator F = I->begin(), FE = I->end(); F != FE; ++F)
        for (BasicBlock::iterator B = F->begin(), BE = F->end(); B != BE; ++B)
          if (isa<CallInst>(B) || isa<InvokeInst>(B)) {
            CallSite cs = CallSite::get(B);
            AllSites.push_back(cs);
            Function* CF = cs.getCalledFunction();

            //
            // If the called function is casted from one function type to
            // another, peer into the cast instruction and pull out the actual
            // function being called.
            //
            if (!CF)
              CF = dyn_cast<Function>(cs.getCalledValue()->stripPointerCasts());

            Value * calledValue = cs.getCalledValue()->stripPointerCasts();
            if (!CF) {
              if (isa<ConstantPointerNull>(calledValue)) {
                ++DirCall;
                CompleteSites.insert(cs);
              } else {
                IndCall++;

                DSCallGraph::callee_iterator csi = callgraph.callee_begin(cs),
                                   cse = callgraph.callee_end(cs);
                while(csi != cse) {
                  const Function *F = *csi;
                  DSCallGraph::scc_iterator sccii = callgraph.scc_begin(F),
                    sccee = callgraph.scc_end(F);
                  for(;sccii != sccee; ++sccii) {
                    DSGraph::ScalarMapTy::const_iterator I = SM.find(SM.getLeaderForGlobal(*sccii));
                    if (I != SM.end()) {
                      IndMap[cs].push_back (*sccii);
                    }
                  }
                  ++csi;
                }
                const Function *F1 = (cs).getInstruction()->getParent()->getParent();
                F1 = callgraph.sccLeader(&*F1);
                
                DSCallGraph::scc_iterator sccii = callgraph.scc_begin(F1),
                  sccee = callgraph.scc_end(F1);
                for(;sccii != sccee; ++sccii) {
                  DSGraph::ScalarMapTy::const_iterator I = SM.find(SM.getLeaderForGlobal(*sccii));
                  if (I != SM.end()) {
                    IndMap[cs].push_back (*sccii);
                  }
                }

                DSNode* N = T->getDSGraph(*cs.getCaller())
                  ->getNodeForValue(cs.getCalledValue()).getNode();
                assert (N && "CallTarget: findIndTargets: No DSNode!\n");

                if (N->isCompleteNode() && IndMap[cs].size()) {
                  CompleteSites.insert(cs);
                  ++CompleteInd;
                } 
                if (N->isCompleteNode() && !IndMap[cs].size()) {
                  ++CompleteEmpty;
                  DEBUG(errs() << "Call site empty: '"
                                << cs.getInstruction()->getName() 
                                << "' In '"
                                << cs.getInstruction()->getParent()->getParent()->getName()
                                << "'\n");
                }
              }
            } else {
              ++DirCall;
              IndMap[cs].push_back(cs.getCalledFunction());
              CompleteSites.insert(cs);
            }
          }
}

void CallTargetFinder::print(llvm::raw_ostream &O, const Module *M) const
{
  O << "[* = incomplete] CS: func list\n";
  for (std::map<CallSite, std::vector<const Function*> >::const_iterator ii =
       IndMap.begin(),
         ee = IndMap.end(); ii != ee; ++ii) {
    if (!ii->first.getCalledFunction()) { //only print indirect
      if (!isComplete(ii->first)) {
        O << "* ";
        CallSite cs = ii->first;
        cs.getInstruction()->dump();
        O << cs.getInstruction()->getParent()->getParent()->getNameStr() << " "
          << cs.getInstruction()->getNameStr() << " ";
      }
      O << ii->first.getInstruction() << ":";
      for (std::vector<const Function*>::const_iterator i = ii->second.begin(),
             e = ii->second.end(); i != e; ++i) {
        O << " " << (*i)->getNameStr();
      }
      O << "\n";
    }
  }
}

bool CallTargetFinder::runOnModule(Module &M) {
  findIndTargets(M);
  return false;
}

void CallTargetFinder::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<EQTDDataStructures>();
}

std::vector<const Function*>::iterator CallTargetFinder::begin(CallSite cs) {
  return IndMap[cs].begin();
}

std::vector<const Function*>::iterator CallTargetFinder::end(CallSite cs) {
  return IndMap[cs].end();
}

bool CallTargetFinder::isComplete(CallSite cs) const {
  return CompleteSites.find(cs) != CompleteSites.end();
}

std::list<CallSite>::iterator CallTargetFinder::cs_begin() {
  return AllSites.begin();
}

std::list<CallSite>::iterator CallTargetFinder::cs_end() {
  return AllSites.end();
}
