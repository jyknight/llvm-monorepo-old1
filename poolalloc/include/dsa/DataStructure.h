//===- DataStructure.h - Build data structure graphs ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implement the LLVM data structure analysis library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DATA_STRUCTURE_H
#define LLVM_ANALYSIS_DATA_STRUCTURE_H

#include "llvm/Pass.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Support/CallSite.h"
#include "llvm/ADT/hash_map.h"
#include "llvm/ADT/hash_set.h"
#include "llvm/ADT/EquivalenceClasses.h"

#include <map>

namespace llvm {

class Type;
class Instruction;
class GlobalValue;
class DSGraph;
class DSCallSite;
class DSNode;
class DSNodeHandle;

FunctionPass *createDataStructureStatsPass();
FunctionPass *createDataStructureGraphCheckerPass();

class DataStructures : public ModulePass {
  typedef std::map<const Instruction*, std::set<const Function*> > ActualCalleesTy;
  typedef hash_map<const Function*, DSGraph*> DSInfoTy;
public:
  typedef std::set<const Function*>::const_iterator callee_iterator;
  
private:
  /// TargetData, comes in handy
  TargetData* TD;

  /// Pass to get Graphs from
  DataStructures* GraphSource;

  /// Do we clone Graphs or steal them?
  bool Clone;

  void buildGlobalECs(std::set<const GlobalValue*>& ECGlobals);

  void eliminateUsesOfECGlobals(DSGraph& G, const std::set<const GlobalValue*> &ECGlobals);

  // DSInfo, one graph for each function
  DSInfoTy DSInfo;

  // Callgraph, as computed so far
  ActualCalleesTy ActualCallees;

protected:

  /// The Globals Graph contains all information on the globals
  DSGraph *GlobalsGraph;

  /// GlobalECs - The equivalence classes for each global value that is merged
  /// with other global values in the DSGraphs.
  EquivalenceClasses<const GlobalValue*> GlobalECs;


  void init(DataStructures* D, bool clone, bool printAuxCalls);
  void init(TargetData* T);

  void formGlobalECs();


  void callee_add(const Instruction* I, const Function* F) {
    ActualCallees[I].insert(F);
  }

  DataStructures(intptr_t id) 
    :ModulePass(id), TD(0), GraphSource(0), GlobalsGraph(0) {}

public:
  callee_iterator callee_begin(const Instruction *I) const {
    ActualCalleesTy::const_iterator ii = ActualCallees.find(I);
    assert(ii != ActualCallees.end() && "No calls for instruction");
    return ii->second.begin();
  }

  callee_iterator callee_end(const Instruction *I) const {
    ActualCalleesTy::const_iterator ii = ActualCallees.find(I);
    assert(ii != ActualCallees.end() && "No calls for instruction");
    return ii->second.end();
  }

  unsigned callee_size() const {
    unsigned sum = 0;
    for (ActualCalleesTy::const_iterator ii = ActualCallees.begin(),
           ee = ActualCallees.end(); ii != ee; ++ii)
      sum += ii->second.size();
    return sum;
  }

  void callee_get_keys(std::vector<const Instruction*>& keys) {
    for (ActualCalleesTy::const_iterator ii = ActualCallees.begin(),
           ee = ActualCallees.end(); ii != ee; ++ii)
      keys.push_back(ii->first);
  }

  virtual void releaseMemory();

  bool hasDSGraph(const Function &F) const {
    return DSInfo.find(&F) != DSInfo.end();
  }

  /// getDSGraph - Return the data structure graph for the specified function.
  ///
  DSGraph &getDSGraph(const Function &F) const {
    hash_map<const Function*, DSGraph*>::const_iterator I = DSInfo.find(&F);
    assert(I != DSInfo.end() && "Function not in module!");
    return *I->second;
  }

  void setDSGraph(const Function& F, DSGraph* G) {
    assert(!DSInfo[&F] && "DSGraph already exists");
    DSInfo[&F] = G;
  }

  DSGraph& getOrCreateGraph(const Function* F);

  DSGraph &getGlobalsGraph() const { return *GlobalsGraph; }

  EquivalenceClasses<const GlobalValue*> &getGlobalECs() { return GlobalECs; }

  TargetData& getTargetData() const { return *TD; }

  /// deleteValue/copyValue - Interfaces to update the DSGraphs in the program.
  /// These correspond to the interfaces defined in the AliasAnalysis class.
  void deleteValue(Value *V);
  void copyValue(Value *From, Value *To);
};


// LocalDataStructures - The analysis that computes the local data structure
// graphs for all of the functions in the program.
//
// FIXME: This should be a Function pass that can be USED by a Pass, and would
// be automatically preserved.  Until we can do that, this is a Pass.
//
class LocalDataStructures : public DataStructures {
public:
  static char ID;
  LocalDataStructures() : DataStructures((intptr_t)&ID) {}
  ~LocalDataStructures() { releaseMemory(); }

  virtual bool runOnModule(Module &M);

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  /// getAnalysisUsage - This obviously provides a data structure graph.
  ///
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<TargetData>();
  }
};

// StdLibDataStructures - This analysis recognizes common standard c library
// functions and generates graphs for them.
class StdLibDataStructures : public DataStructures {
  void eraseCallsTo(Function* F);
public:
  static char ID;
  StdLibDataStructures() : DataStructures((intptr_t)&ID) {}
  ~StdLibDataStructures() { releaseMemory(); }

  virtual bool runOnModule(Module &M);

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  /// getAnalysisUsage - This obviously provides a data structure graph.
  ///
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<LocalDataStructures>();
  }
};

/// BUDataStructures - The analysis that computes the interprocedurally closed
/// data structure graphs for all of the functions in the program.  This pass
/// only performs a "Bottom Up" propagation (hence the name).
///
class BUDataStructures : public DataStructures {
protected:

  // This map is only maintained during construction of BU Graphs
  std::map<std::vector<const Function*>,
           std::pair<DSGraph*, std::vector<DSNodeHandle> > > IndCallGraphMap;

  BUDataStructures(intptr_t id) : DataStructures(id) {}
public:
  static char ID;
  BUDataStructures() : DataStructures((intptr_t)&ID) {}
  ~BUDataStructures() { releaseMemory(); }

  virtual bool runOnModule(Module &M);

  /// deleteValue/copyValue - Interfaces to update the DSGraphs in the program.
  /// These correspond to the interfaces defined in the AliasAnalysis class.
  void deleteValue(Value *V);
  void copyValue(Value *From, Value *To);

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<StdLibDataStructures>();
  }

private:
  void calculateGraph(DSGraph &G);

  void inlineUnresolved(DSGraph &G);

  unsigned calculateGraphs(const Function *F, 
                           std::vector<const Function*> &Stack,
                           unsigned &NextID,
                           hash_map<const Function*, unsigned> &ValMap);


  void CloneAuxIntoGlobal(DSGraph& G);
  void finalizeGlobals(void);
};


/// TDDataStructures - Analysis that computes new data structure graphs
/// for each function using the closed graphs for the callers computed
/// by the bottom-up pass.
///
class TDDataStructures : public DataStructures {
  hash_set<const Function*> ArgsRemainIncomplete;
  BUDataStructures *BUInfo;

  /// CallerCallEdges - For a particular graph, we keep a list of these records
  /// which indicates which graphs call this function and from where.
  struct CallerCallEdge {
    DSGraph *CallerGraph;        // The graph of the caller function.
    const DSCallSite *CS;        // The actual call site.
    const Function *CalledFunction;    // The actual function being called.

    CallerCallEdge(DSGraph *G, const DSCallSite *cs, const Function *CF)
      : CallerGraph(G), CS(cs), CalledFunction(CF) {}

    bool operator<(const CallerCallEdge &RHS) const {
      return CallerGraph < RHS.CallerGraph ||
            (CallerGraph == RHS.CallerGraph && CS < RHS.CS);
    }
  };

  std::map<DSGraph*, std::vector<CallerCallEdge> > CallerEdges;


  // IndCallMap - We memoize the results of indirect call inlining operations
  // that have multiple targets here to avoid N*M inlining.  The key to the map
  // is a sorted set of callee functions, the value is the DSGraph that holds
  // all of the caller graphs merged together, and the DSCallSite to merge with
  // the arguments for each function.
  std::map<std::vector<const Function*>, DSGraph*> IndCallMap;

public:
  static char ID;
  TDDataStructures() : DataStructures((intptr_t)&ID) {}
  ~TDDataStructures() { releaseMemory(); }

  virtual bool runOnModule(Module &M);

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  /// getAnalysisUsage - This obviously provides a data structure graph.
  ///
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<BUDataStructures>();
  }

private:
  void markReachableFunctionsExternallyAccessible(DSNode *N,
                                                  hash_set<DSNode*> &Visited);

  void InlineCallersIntoGraph(DSGraph &G);
  void ComputePostOrder(const Function &F, hash_set<DSGraph*> &Visited,
                        std::vector<DSGraph*> &PostOrder);
};


/// CompleteBUDataStructures - This is the exact same as the bottom-up graphs,
/// but we use take a completed call graph and inline all indirect callees into
/// their callers graphs, making the result more useful for things like pool
/// allocation.
///
class CompleteBUDataStructures : public  DataStructures {
public:
  static char ID;
  CompleteBUDataStructures() : DataStructures((intptr_t)&ID) {}
  ~CompleteBUDataStructures() { releaseMemory(); }

  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<BUDataStructures>();
  }

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  virtual void releaseMemory();

private:
  unsigned calculateSCCGraphs(DSGraph &FG, std::vector<DSGraph*> &Stack,
                              unsigned &NextID,
                              hash_map<DSGraph*, unsigned> &ValMap);
  void processGraph(DSGraph &G);
};


/// EquivClassGraphs - This is the same as the complete bottom-up graphs, but
/// with functions partitioned into equivalence classes and a single merged
/// DS graph for all functions in an equivalence class.  After this merging,
/// graphs are inlined bottom-up on the SCCs of the final (CBU) call graph.
///
struct EquivClassGraphs : public DataStructures {

  // Equivalence class where functions that can potentially be called via the
  // same function pointer are in the same class.
  EquivalenceClasses<const Function*> FuncECs;

  /// OneCalledFunction - For each indirect call, we keep track of one
  /// target of the call.  This is used to find equivalence class called by
  /// a call site.
  std::map<DSNode*, const Function *> OneCalledFunction;

public:
  static char ID;
  EquivClassGraphs();

  /// EquivClassGraphs - Computes the equivalence classes and then the
  /// folded DS graphs for each class.
  ///

  virtual bool runOnModule(Module &M);

  /// print - Print out the analysis results...
  ///
  void print(std::ostream &O, const Module *M) const;

  /// getSomeCalleeForCallSite - Return any one callee function at
  /// a call site.
  ///
  const Function *getSomeCalleeForCallSite(const CallSite &CS) const;

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<CompleteBUDataStructures>();
  }

private:
  void buildIndirectFunctionSets(Module &M);

  unsigned processSCC(DSGraph &FG, std::vector<DSGraph*> &Stack,
                      unsigned &NextID,
                      std::map<DSGraph*, unsigned> &ValMap);
  void processGraph(DSGraph &FG);

};

} // End llvm namespace

#endif
