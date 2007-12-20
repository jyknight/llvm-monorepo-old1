//===-- PoolAllocate.cpp - Pool Allocation Pass ---------------------------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This transform changes programs so that disjoint data structures are
// allocated out of different pools of memory, increasing locality.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "poolalloc"

#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"
#include "dsa/CallTargets.h"
#include "poolalloc/PoolAllocate.h"
#include "Heuristic.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Constants.h"
#include "llvm/Support/CFG.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Timer.h"

#include <iostream>

using namespace llvm;
using namespace PA;
#ifdef SAFECODE
using namespace CUA;
#endif

char PoolAllocate::ID = 0;
char PoolAllocatePassAllPools::ID = 0;

const Type *PoolAllocate::PoolDescPtrTy = 0;

#if 0
#define TIME_REGION(VARNAME, DESC) \
   NamedRegionTimer VARNAME(DESC)
#else
#define TIME_REGION(VARNAME, DESC)
#endif

namespace {
  RegisterPass<PoolAllocate>
  X("poolalloc", "Pool allocate disjoint data structures");
  RegisterPass<PoolAllocatePassAllPools>
  Y("poolalloc-passing-all-pools", "Pool allocate disjoint data structures");

  STATISTIC (NumArgsAdded, "Number of function arguments added");
  STATISTIC (MaxArgsAdded, "Maximum function arguments added to one function");
  STATISTIC (NumCloned   , "Number of functions cloned");
  STATISTIC (NumPools    , "Number of pools allocated");
  STATISTIC (NumTSPools  , "Number of typesafe pools");
  STATISTIC (NumPoolFree , "Number of poolfree's elided");
  STATISTIC (NumNonprofit, "Number of DSNodes not profitable");
  STATISTIC (NumColocated, "Number of DSNodes colocated");

  const Type *VoidPtrTy;

  // The type to allocate for a pool descriptor.
  const Type *PoolDescType;

  cl::opt<bool>
  DisableInitDestroyOpt("poolalloc-force-simple-pool-init",
                        cl::desc("Always insert poolinit/pooldestroy calls at start and exit of functions"));//, cl::init(true));
  cl::opt<bool>
  DisablePoolFreeOpt("poolalloc-force-all-poolfrees",
                     cl::desc("Do not try to elide poolfree's where possible"));

  cl::opt<bool>
  UseTDResolve("poolalloc-usetd-resolve",
	       cl::desc("Use Top-Down Graph as a resolve source"));
}

void PoolAllocate::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredTransitive<EquivClassGraphs>();
  AU.addPreserved<EquivClassGraphs>();
#ifdef SAFECODE  
  //Dinakar for preserving the pool information across passes
  AU.setPreservesAll();
#endif  
#ifdef BOUNDS_CHECK
  //Dinakar hack for preserving the pool information across passes
  AU.setPreservesAll();
#endif
  AU.addRequired<TargetData>();
  if (UseTDResolve)
    AU.addRequired<CallTargetFinder>();
}

bool PoolAllocate::runOnModule(Module &M) {
  if (M.begin() == M.end()) return false;
#ifdef SAFECODE  
#if 0
  CUAPass = &getAnalysis<ConvertUnsafeAllocas>();
#endif
#endif  
  CurModule = &M;
  ECGraphs = &getAnalysis<EquivClassGraphs>();   // folded inlined CBU graphs
  if (UseTDResolve)
    CTF = &getAnalysis<CallTargetFinder>();
  else
    CTF = 0;

  CurHeuristic = Heuristic::create();
  CurHeuristic->Initialize(M, ECGraphs->getGlobalsGraph(), *this);

  // Add the pool* prototypes to the module
  AddPoolPrototypes();

  // Create the pools for memory objects reachable by global variables.
  if (SetupGlobalPools(M))
    return true;

  // Loop over the functions in the original program finding the pool desc.
  // arguments necessary for each function that is indirectly callable.
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    if (!I->isDeclaration() && ECGraphs->ContainsDSGraphFor(*I))
      FindFunctionPoolArgs(*I);

  std::map<Function*, Function*> FuncMap;

  // Now clone a function using the pool arg list obtained in the previous pass
  // over the modules.  Loop over only the function initially in the program,
  // don't traverse newly added ones.  If the function needs new arguments, make
  // its clone.
  std::set<Function*> ClonedFunctions;
{TIME_REGION(X, "MakeFunctionClone");
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    if (!I->isDeclaration() && !ClonedFunctions.count(I) &&
        ECGraphs->ContainsDSGraphFor(*I))
      if (Function *Clone = MakeFunctionClone(*I)) {
        FuncMap[I] = Clone;
        ClonedFunctions.insert(Clone);
      }
}
  
  // Now that all call targets are available, rewrite the function bodies of the
  // clones.
{TIME_REGION(X, "ProcessFunctionBody");
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    if (!I->isDeclaration() && !ClonedFunctions.count(I) &&
        ECGraphs->ContainsDSGraphFor(*I)) {
      std::map<Function*, Function*>::iterator FI = FuncMap.find(I);
      ProcessFunctionBody(*I, FI != FuncMap.end() ? *FI->second : *I);
    }
}
  // Replace all uses of original functions with the transformed function.
  for (std::map<Function *, Function *>::iterator I = FuncMap.begin(),
         E = FuncMap.end(); I != E; ++I) {
    Function *F = I->first;
    F->replaceAllUsesWith(ConstantExpr::getPointerCast(I->second, F->getType()));
  }

  if (CurHeuristic->IsRealHeuristic())
    MicroOptimizePoolCalls();

  delete CurHeuristic;
  return true;
}

// AddPoolPrototypes - Add prototypes for the pool functions to the specified
// module and update the Pool* instance variables to point to them.
//
// NOTE: If these are changed, make sure to update PoolOptimize.cpp as well!
//
void PoolAllocate::AddPoolPrototypes() {
  if (VoidPtrTy == 0) {
    // NOTE: If these are changed, make sure to update PoolOptimize.cpp as well!
    VoidPtrTy = PointerType::getUnqual(Type::Int8Ty);
#ifdef SAFECODE    
    PoolDescType = ArrayType::get(VoidPtrTy, 50);
#else
    PoolDescType = ArrayType::get(VoidPtrTy, 16);
#endif    
    PoolDescPtrTy = PointerType::getUnqual(PoolDescType);
  }

  CurModule->addTypeName("PoolDescriptor", PoolDescType);
  
  // Get poolinit function.
  PoolInit = CurModule->getOrInsertFunction("poolinit", Type::VoidTy,
                                            PoolDescPtrTy, Type::Int32Ty,
                                            Type::Int32Ty, NULL);

  // Get pooldestroy function.
  PoolDestroy = CurModule->getOrInsertFunction("pooldestroy", Type::VoidTy,
                                               PoolDescPtrTy, NULL);
  
  // The poolalloc function.
  PoolAlloc = CurModule->getOrInsertFunction("poolalloc", 
                                             VoidPtrTy, PoolDescPtrTy,
                                             Type::Int32Ty, NULL);
  
  // The poolrealloc function.
  PoolRealloc = CurModule->getOrInsertFunction("poolrealloc",
                                               VoidPtrTy, PoolDescPtrTy,
                                               VoidPtrTy, Type::Int32Ty, NULL);
  // The poolmemalign function.
  PoolMemAlign = CurModule->getOrInsertFunction("poolmemalign",
                                                VoidPtrTy, PoolDescPtrTy,
                                                Type::Int32Ty, Type::Int32Ty, 
                                                NULL);

  // Get the poolfree function.
  PoolFree = CurModule->getOrInsertFunction("poolfree", Type::VoidTy,
                                            PoolDescPtrTy, VoidPtrTy, NULL);
#ifdef SAFECODE
  //Get the poolregister function
  PoolRegister = CurModule->getOrInsertFunction("poolregister", Type::VoidTy,
                                   PoolDescPtrTy, Type::Int32Ty, VoidPtrTy, NULL);
#endif
#ifdef BOUNDS_CHECK
  PoolRegister = CurModule->getOrInsertFunction("poolregister", Type::VoidTy,
                                   PoolDescPtrTy, VoidPtrTy, Type::Int32Ty, NULL);
#endif  
}

static void getCallsOf(Constant *C, std::vector<CallInst*> &Calls) {
  // Get the Function out of the constant
  Function * F;
  ConstantExpr * CE;
  if (!(F=dyn_cast<Function>(C)))
    if ((CE = dyn_cast<ConstantExpr>(C)) && (CE->isCast()))
      F = dyn_cast<Function>(CE->getOperand(0));
    else
      assert (0 && "Constant is not a Function of ConstantExpr!"); 
  Calls.clear();
  for (Value::use_iterator UI = F->use_begin(), E = F->use_end(); UI != E; ++UI)
    Calls.push_back(cast<CallInst>(*UI));
}

static void OptimizePointerNotNull(Value *V) {
  for (Value::use_iterator I = V->use_begin(), E = V->use_end(); I != E; ++I) {
    Instruction *User = cast<Instruction>(*I);
    if (isa<ICmpInst>(User) && cast<ICmpInst>(User)->isEquality()) {
      ICmpInst * ICI = cast<ICmpInst>(User);
      if (isa<Constant>(User->getOperand(1)) && 
          cast<Constant>(User->getOperand(1))->isNullValue()) {
        bool CondIsTrue = ICI->getPredicate() == ICmpInst::ICMP_NE;
        User->replaceAllUsesWith(ConstantInt::get(Type::Int1Ty, CondIsTrue));
      }
    } else if ((User->getOpcode() == Instruction::Trunc) ||
               (User->getOpcode() == Instruction::ZExt) ||
               (User->getOpcode() == Instruction::SExt) ||
               (User->getOpcode() == Instruction::FPToUI) ||
               (User->getOpcode() == Instruction::FPToSI) ||
               (User->getOpcode() == Instruction::UIToFP) ||
               (User->getOpcode() == Instruction::SIToFP) ||
               (User->getOpcode() == Instruction::FPTrunc) ||
               (User->getOpcode() == Instruction::FPExt) ||
               (User->getOpcode() == Instruction::PtrToInt) ||
               (User->getOpcode() == Instruction::IntToPtr) ||
               (User->getOpcode() == Instruction::BitCast)) {
      // Casted pointers are also not null.
      if (isa<PointerType>(User->getType()))
        OptimizePointerNotNull(User);
    } else if (User->getOpcode() == Instruction::GetElementPtr) {
      // GEP'd pointers are also not null.
      OptimizePointerNotNull(User);
    }
  }
}

/// MicroOptimizePoolCalls - Apply any microoptimizations to calls to pool
/// allocation function calls that we can.  This runs after the whole program
/// has been transformed.
void PoolAllocate::MicroOptimizePoolCalls() {
  // Optimize poolalloc
  std::vector<CallInst*> Calls;
  getCallsOf(PoolAlloc, Calls);
  for (unsigned i = 0, e = Calls.size(); i != e; ++i) {
    CallInst *CI = Calls[i];
    // poolalloc never returns null.  Loop over all uses of the call looking for
    // set(eq|ne) X, null.
    OptimizePointerNotNull(CI);
  }

  // TODO: poolfree accepts a null pointer, so remove any check above it, like
  // 'if (P) poolfree(P)'
}




static void GetNodesReachableFromGlobals(DSGraph &G,
                                  hash_set<const DSNode*> &NodesFromGlobals) {
  for (DSScalarMap::global_iterator I = G.getScalarMap().global_begin(), 
         E = G.getScalarMap().global_end(); I != E; ++I)
    G.getNodeForValue(*I).getNode()->markReachableNodes(NodesFromGlobals);
}

static void MarkNodesWhichMustBePassedIn(hash_set<const DSNode*> &MarkedNodes,
                                         Function &F, DSGraph &G,
                                         bool PassAllArguments) {
  // Mark globals and incomplete nodes as live... (this handles arguments)
  if (F.getName() != "main") {
    // All DSNodes reachable from arguments must be passed in.
    for (Function::arg_iterator I = F.arg_begin(), E = F.arg_end();
         I != E; ++I) {
      DSGraph::ScalarMapTy::iterator AI = G.getScalarMap().find(I);
      if (AI != G.getScalarMap().end())
        if (DSNode *N = AI->second.getNode())
          N->markReachableNodes(MarkedNodes);
    }
  }

  // Marked the returned node as needing to be passed in.
  if (DSNode *RetNode = G.getReturnNodeFor(F).getNode())
    RetNode->markReachableNodes(MarkedNodes);

  // Calculate which DSNodes are reachable from globals.  If a node is reachable
  // from a global, we will create a global pool for it, so no argument passage
  // is required.
  hash_set<const DSNode*> NodesFromGlobals;
  GetNodesReachableFromGlobals(G, NodesFromGlobals);

  // Remove any nodes reachable from a global.  These nodes will be put into
  // global pools, which do not require arguments to be passed in.  Also, erase
  // any marked node that is not a heap node.  Since no allocations or frees
  // will be done with it, it needs no argument.
  for (hash_set<const DSNode*>::iterator I = MarkedNodes.begin(),
         E = MarkedNodes.end(); I != E; ) {
    const DSNode *N = *I++;
    if ((!(1 || N->isHeapNode()) && !PassAllArguments) || NodesFromGlobals.count(N))
      MarkedNodes.erase(N);
  }
}


/// FindFunctionPoolArgs - In the first pass over the program, we decide which
/// arguments will have to be added for each function, build the FunctionInfo
/// map and recording this info in the ArgNodes set.
void PoolAllocate::FindFunctionPoolArgs(Function &F) {
  DSGraph &G = ECGraphs->getDSGraph(F);

  // Create a new entry for F.
  FuncInfo &FI =
    FunctionInfo.insert(std::make_pair(&F, FuncInfo(F))).first->second;
  hash_set<const DSNode*> &MarkedNodes = FI.MarkedNodes;

  if (G.node_begin() == G.node_end())
    return;  // No memory activity, nothing is required

  // Find DataStructure nodes which are allocated in pools non-local to the
  // current function.  This set will contain all of the DSNodes which require
  // pools to be passed in from outside of the function.
  MarkNodesWhichMustBePassedIn(MarkedNodes, F, G, PassAllArguments);
  
  FI.ArgNodes.insert(FI.ArgNodes.end(), MarkedNodes.begin(), MarkedNodes.end());
}

// MakeFunctionClone - If the specified function needs to be modified for pool
// allocation support, make a clone of it, adding additional arguments as
// necessary, and return it.  If not, just return null.
//
Function *PoolAllocate::MakeFunctionClone(Function &F) {
  DSGraph &G = ECGraphs->getDSGraph(F);
  if (G.node_begin() == G.node_end()) return 0;
    
  FuncInfo &FI = *getFuncInfo(F);
  if (FI.ArgNodes.empty())
    return 0;           // No need to clone if no pools need to be passed in!

  // Update statistics..
  NumArgsAdded += FI.ArgNodes.size();
  if (MaxArgsAdded < FI.ArgNodes.size()) MaxArgsAdded = FI.ArgNodes.size();
  ++NumCloned;
 
      
  // Figure out what the arguments are to be for the new version of the function
  const FunctionType *OldFuncTy = F.getFunctionType();
  std::vector<const Type*> ArgTys(FI.ArgNodes.size(), PoolDescPtrTy);
  ArgTys.reserve(OldFuncTy->getNumParams() + FI.ArgNodes.size());

  ArgTys.insert(ArgTys.end(), OldFuncTy->param_begin(), OldFuncTy->param_end());

  // Create the new function prototype
  FunctionType *FuncTy = FunctionType::get(OldFuncTy->getReturnType(), ArgTys,
                                           OldFuncTy->isVarArg());
  // Create the new function...
  Function *New = new Function(FuncTy, Function::InternalLinkage, F.getName());
  F.getParent()->getFunctionList().insert(&F, New);
  CloneToOrigMap[New] = &F;   // Remember original function.

  // Set the rest of the new arguments names to be PDa<n> and add entries to the
  // pool descriptors map
  std::map<const DSNode*, Value*> &PoolDescriptors = FI.PoolDescriptors;
  Function::arg_iterator NI = New->arg_begin();
  
  for (unsigned i = 0, e = FI.ArgNodes.size(); i != e; ++i, ++NI) {
    NI->setName("PDa");
    PoolDescriptors[FI.ArgNodes[i]] = NI;
  }

  // Map the existing arguments of the old function to the corresponding
  // arguments of the new function, and copy over the names.
#ifdef SAFECODE  
  DenseMap<const Value*, Value*> &ValueMap = FI.ValueMap;
#else
  DenseMap<const Value*, Value*> ValueMap;
#endif  
  for (Function::arg_iterator I = F.arg_begin();
       NI != New->arg_end(); ++I, ++NI) {
    ValueMap[I] = NI;
    NI->setName(I->getName());
  }

  // Perform the cloning.
  std::vector<ReturnInst*> Returns;
{TIME_REGION(X, "CloneFunctionInto");
  CloneFunctionInto(New, &F, ValueMap, Returns);
}
  // Invert the ValueMap into the NewToOldValueMap
  std::map<Value*, const Value*> &NewToOldValueMap = FI.NewToOldValueMap;

  for (DenseMap<const Value*, Value*>::iterator I = ValueMap.begin(),
         E = ValueMap.end(); I != E; ++I)
    NewToOldValueMap.insert(std::make_pair(I->second, I->first));
  return FI.Clone = New;
}

// SetupGlobalPools - Create global pools for all DSNodes in the globals graph
// which contain heap objects.  If a global variable points to a piece of memory
// allocated from the heap, this pool gets a global lifetime.  This is
// implemented by making the pool descriptor be a global variable of it's own,
// and initializing the pool on entrance to main.  Note that we never destroy
// the pool, because it has global lifetime.
//
// This method returns true if correct pool allocation of the module cannot be
// performed because there is no main function for the module and there are
// global pools.
//
bool PoolAllocate::SetupGlobalPools(Module &M) {
  // Get the globals graph for the program.
  DSGraph &GG = ECGraphs->getGlobalsGraph();

  // Get all of the nodes reachable from globals.
  hash_set<const DSNode*> GlobalHeapNodes;
  GetNodesReachableFromGlobals(GG, GlobalHeapNodes);

  // Filter out all nodes which have no heap allocations merged into them.
  for (hash_set<const DSNode*>::iterator I = GlobalHeapNodes.begin(),
         E = GlobalHeapNodes.end(); I != E; ) {
    hash_set<const DSNode*>::iterator Last = I++;
#ifndef SAFECODE
#ifndef BOUNDS_CHECK    
    //    if (!(*Last)->isHeapNode());
    //       GlobalHeapNodes.erase(Last);
#endif       
#endif
    const DSNode *tmp = *Last;
    //    std::cerr << "test \n";
    if (!(tmp->isHeapNode() || tmp->isArray()))
      GlobalHeapNodes.erase(Last);
  }
  
  // Otherwise get the main function to insert the poolinit calls.
  Function *MainFunc = M.getFunction("main");
  if (MainFunc == 0 || MainFunc->isDeclaration()) {
    std::cerr << "Cannot pool allocate this program: it has global "
              << "pools but no 'main' function yet!\n";
    return true;
  }

  std::cerr << "Pool allocating " << GlobalHeapNodes.size()
            << " global nodes!\n";


  std::vector<const DSNode*> NodesToPA(GlobalHeapNodes.begin(),
                                       GlobalHeapNodes.end());
  std::vector<Heuristic::OnePool> ResultPools;
  CurHeuristic->AssignToPools(NodesToPA, 0, GG, ResultPools);

  BasicBlock::iterator InsertPt = MainFunc->getEntryBlock().begin();
#ifndef SAFECODE
#ifndef BOUNDS_CHECK  
  while (isa<AllocaInst>(InsertPt)) ++InsertPt;
#endif  
#endif
  // Perform all global assignments as specified.
  for (unsigned i = 0, e = ResultPools.size(); i != e; ++i) {
    Heuristic::OnePool &Pool = ResultPools[i];
    Value *PoolDesc = Pool.PoolDesc;
    if (PoolDesc == 0) {
      PoolDesc = CreateGlobalPool(Pool.PoolSize, Pool.PoolAlignment, InsertPt);

      if (Pool.NodesInPool.size() == 1 &&
          !Pool.NodesInPool[0]->isNodeCompletelyFolded())
        ++NumTSPools;
    }
    for (unsigned N = 0, e = Pool.NodesInPool.size(); N != e; ++N) {
      GlobalNodes[Pool.NodesInPool[N]] = PoolDesc;
      GlobalHeapNodes.erase(Pool.NodesInPool[N]);  // Handled!
    }
  }

  // Any unallocated DSNodes get null pool descriptor pointers.
  for (hash_set<const DSNode*>::iterator I = GlobalHeapNodes.begin(),
         E = GlobalHeapNodes.end(); I != E; ++I) {
    GlobalNodes[*I] = Constant::getNullValue(PointerType::getUnqual(PoolDescType));
    ++NumNonprofit;
  }
  
  return false;
}

/// CreateGlobalPool - Create a global pool descriptor object, and insert a
/// poolinit for it into main.  IPHint is an instruction that we should insert
/// the poolinit before if not null.
GlobalVariable *PoolAllocate::CreateGlobalPool(unsigned RecSize, unsigned Align,
                                               Instruction *IPHint) {
  GlobalVariable *GV =
    new GlobalVariable(PoolDescType, false, GlobalValue::InternalLinkage, 
                       Constant::getNullValue(PoolDescType), "GlobalPool",
                       CurModule);

  // Update the global DSGraph to include this.
  DSNode *GNode = ECGraphs->getGlobalsGraph().addObjectToGraph(GV);
  GNode->setModifiedMarker()->setReadMarker();

  Function *MainFunc = CurModule->getFunction("main");
  assert(MainFunc && "No main in program??");

  BasicBlock::iterator InsertPt;
  if (IPHint)
    InsertPt = IPHint;
  else {
    InsertPt = MainFunc->getEntryBlock().begin();
    while (isa<AllocaInst>(InsertPt)) ++InsertPt;
  }

  Value *ElSize = ConstantInt::get(Type::Int32Ty, RecSize);
  Value *AlignV = ConstantInt::get(Type::Int32Ty, Align);
  Value* Opts[3] = {GV, ElSize, AlignV};
  new CallInst(PoolInit, Opts, Opts + 3, "", InsertPt);
  ++NumPools;
  return GV;
}


// CreatePools - This creates the pool initialization and destruction code for
// the DSNodes specified by the NodesToPA list.  This adds an entry to the
// PoolDescriptors map for each DSNode.
//
void PoolAllocate::CreatePools(Function &F, DSGraph &DSG,
                               const std::vector<const DSNode*> &NodesToPA,
                               std::map<const DSNode*,
                                        Value*> &PoolDescriptors) {
  if (NodesToPA.empty()) return;
  TIME_REGION(X, "CreatePools");

  std::vector<Heuristic::OnePool> ResultPools;
  CurHeuristic->AssignToPools(NodesToPA, &F, *NodesToPA[0]->getParentGraph(),
                              ResultPools);

  std::set<const DSNode*> UnallocatedNodes(NodesToPA.begin(), NodesToPA.end());

  BasicBlock::iterator InsertPoint = F.front().begin();
#ifndef SAFECODE
#ifndef BOUNDS_CHECK  
  while (isa<AllocaInst>(InsertPoint)) ++InsertPoint;
#endif
#endif  
  // Is this main?  If so, make the pool descriptors globals, not automatic
  // vars.
  bool IsMain = F.getName() == "main" && F.hasExternalLinkage();

  // Perform all global assignments as specified.
  for (unsigned i = 0, e = ResultPools.size(); i != e; ++i) {
    Heuristic::OnePool &Pool = ResultPools[i];
    Value *PoolDesc = Pool.PoolDesc;
    if (PoolDesc == 0) {
      // Create a pool descriptor for the pool.  The poolinit will be inserted
      // later.
      if (!IsMain) {
        PoolDesc = new AllocaInst(PoolDescType, 0, "PD", InsertPoint);

        // Create a node in DSG to represent the new alloca.
        DSNode *NewNode = DSG.addObjectToGraph(PoolDesc);
        NewNode->setModifiedMarker()->setReadMarker();  // This is M/R
      } else {
        PoolDesc = CreateGlobalPool(Pool.PoolSize, Pool.PoolAlignment,
                                    InsertPoint);

        // Add the global node to main's graph.
        DSNode *NewNode = DSG.addObjectToGraph(PoolDesc);
        NewNode->setModifiedMarker()->setReadMarker();  // This is M/R

        if (Pool.NodesInPool.size() == 1 &&
            !Pool.NodesInPool[0]->isNodeCompletelyFolded())
          ++NumTSPools;
      }
    }
    for (unsigned N = 0, e = Pool.NodesInPool.size(); N != e; ++N) {
      PoolDescriptors[Pool.NodesInPool[N]] = PoolDesc;
      UnallocatedNodes.erase(Pool.NodesInPool[N]);  // Handled!
    }
  }

  // Any unallocated DSNodes get null pool descriptor pointers.
  for (std::set<const DSNode*>::iterator I = UnallocatedNodes.begin(),
         E = UnallocatedNodes.end(); I != E; ++I) {
    PoolDescriptors[*I] =Constant::getNullValue(PointerType::getUnqual(PoolDescType));
    ++NumNonprofit;
  }
}

// processFunction - Pool allocate any data structures which are contained in
// the specified function.
//
void PoolAllocate::ProcessFunctionBody(Function &F, Function &NewF) {
  DSGraph &G = ECGraphs->getDSGraph(F);

  if (G.node_begin() == G.node_end()) return;  // Quick exit if nothing to do.
  
  FuncInfo &FI = *getFuncInfo(F);
  hash_set<const DSNode*> &MarkedNodes = FI.MarkedNodes;

  // Calculate which DSNodes are reachable from globals.  If a node is reachable
  // from a global, we will create a global pool for it, so no argument passage
  // is required.
  ECGraphs->getGlobalsGraph();

  // Map all node reachable from this global to the corresponding nodes in
  // the globals graph.
  DSGraph::NodeMapTy GlobalsGraphNodeMapping;
  G.computeGToGGMapping(GlobalsGraphNodeMapping);

  // Loop over all of the nodes which are non-escaping, adding pool-allocatable
  // ones to the NodesToPA vector.
  for (DSGraph::node_iterator I = G.node_begin(), E = G.node_end(); I != E;++I){
    // We only need to make a pool if there is a heap object in it...
    DSNode *N = I;
    if (
#ifdef BOUNDS_CHECK
  (N->isArray() ||
#endif		 
   (N->isHeapNode()))
      if (GlobalsGraphNodeMapping.count(N)) {
        // If it is a global pool, set up the pool descriptor appropriately.
        DSNode *GGN = GlobalsGraphNodeMapping[N].getNode();
        assert(GGN && GlobalNodes[GGN] && "No global node found??");
        FI.PoolDescriptors[N] = GlobalNodes[GGN];
      } else if (!MarkedNodes.count(N)) {
        // Otherwise, if it was not passed in from outside the function, it must
        // be a local pool!
        assert(!N->isGlobalNode() && "Should be in global mapping!");
        FI.NodesToPA.push_back(N);
      }
  }

  if (!FI.NodesToPA.empty()) {
    std::cerr << "[" << F.getName() << "] " << FI.NodesToPA.size()
              << " nodes pool allocatable\n";
    CreatePools(NewF, G, FI.NodesToPA, FI.PoolDescriptors);
  } else {
    DEBUG(std::cerr << "[" << F.getName() << "] transforming body.\n");
  }
  
  // Transform the body of the function now... collecting information about uses
  // of the pools.
  std::multimap<AllocaInst*, Instruction*> PoolUses;
  std::multimap<AllocaInst*, CallInst*> PoolFrees;
  TransformBody(G, FI, PoolUses, PoolFrees, NewF);

  // Create pool construction/destruction code
  if (!FI.NodesToPA.empty())
    InitializeAndDestroyPools(NewF, FI.NodesToPA, FI.PoolDescriptors,
                              PoolUses, PoolFrees);
  CurHeuristic->HackFunctionBody(NewF, FI.PoolDescriptors);
}

template<class IteratorTy>
static void AllOrNoneInSet(IteratorTy S, IteratorTy E,
                           std::set<BasicBlock*> &Blocks, bool &AllIn,
                           bool &NoneIn) {
  AllIn = true;
  NoneIn = true;
  for (; S != E; ++S)
    if (Blocks.count(*S))
      NoneIn = false;
    else
      AllIn = false;
}

static void DeleteIfIsPoolFree(Instruction *I, AllocaInst *PD,
                             std::multimap<AllocaInst*, CallInst*> &PoolFrees) {
  std::multimap<AllocaInst*, CallInst*>::iterator PFI, PFE;
  if (dyn_cast<CallInst>(I))
    for (tie(PFI,PFE) = PoolFrees.equal_range(PD); PFI != PFE; ++PFI)
      if (PFI->second == I) {
        PoolFrees.erase(PFI);
        I->eraseFromParent();
        ++NumPoolFree;
        return;
      }
}

void PoolAllocate::CalculateLivePoolFreeBlocks(std::set<BasicBlock*>&LiveBlocks,
                                               Value *PD) {
  for (Value::use_iterator I = PD->use_begin(), E = PD->use_end(); I != E; ++I){
    // The only users of the pool should be call & invoke instructions.
    CallSite U = CallSite::get(*I);
    if (U.getCalledValue() != PoolFree && U.getCalledValue() != PoolDestroy) {
      // This block and every block that can reach this block must keep pool
      // frees.
      for (idf_ext_iterator<BasicBlock*, std::set<BasicBlock*> >
             DI = idf_ext_begin(U.getInstruction()->getParent(), LiveBlocks),
             DE = idf_ext_end(U.getInstruction()->getParent(), LiveBlocks);
           DI != DE; ++DI)
        /* empty */;
    }
  }
}

/// InitializeAndDestroyPools- This inserts calls to poolinit and pooldestroy
/// into the function to initialize and destroy one pool.
///
void PoolAllocate::InitializeAndDestroyPool(Function &F, const DSNode *Node,
                          std::map<const DSNode*, Value*> &PoolDescriptors,
                          std::multimap<AllocaInst*, Instruction*> &PoolUses,
                          std::multimap<AllocaInst*, CallInst*> &PoolFrees) {
  AllocaInst *PD = cast<AllocaInst>(PoolDescriptors[Node]);

  // Convert the PoolUses/PoolFrees sets into something specific to this pool: a
  // set of which blocks are immediately using the pool.
  std::set<BasicBlock*> UsingBlocks;
    
  std::multimap<AllocaInst*, Instruction*>::iterator PUI, PUE;
  tie(PUI, PUE) = PoolUses.equal_range(PD);
  for (; PUI != PUE; ++PUI)
    UsingBlocks.insert(PUI->second->getParent());
    
  // To calculate all of the basic blocks which require the pool to be
  // initialized before, do a depth first search on the CFG from the using
  // blocks.
  std::set<BasicBlock*> InitializedBefore;
  std::set<BasicBlock*> DestroyedAfter;
  for (std::set<BasicBlock*>::iterator I = UsingBlocks.begin(),
         E = UsingBlocks.end(); I != E; ++I) {
    for (df_ext_iterator<BasicBlock*, std::set<BasicBlock*> >
           DI = df_ext_begin(*I, InitializedBefore),
           DE = df_ext_end(*I, InitializedBefore); DI != DE; ++DI)
      /* empty */;
      
    for (idf_ext_iterator<BasicBlock*, std::set<BasicBlock*> >
           DI = idf_ext_begin(*I, DestroyedAfter),
           DE = idf_ext_end(*I, DestroyedAfter); DI != DE; ++DI)
      /* empty */;
  }
  // Now that we have created the sets, intersect them.
  std::set<BasicBlock*> LiveBlocks;
  std::set_intersection(InitializedBefore.begin(),InitializedBefore.end(),
                        DestroyedAfter.begin(), DestroyedAfter.end(),
                        std::inserter(LiveBlocks, LiveBlocks.end()));
  InitializedBefore.clear();
  DestroyedAfter.clear();
    
  DEBUG(std::cerr << "POOL: " << PD->getName() << " information:\n");
  DEBUG(std::cerr << "  Live in blocks: ");
  DEBUG(for (std::set<BasicBlock*>::iterator I = LiveBlocks.begin(),
               E = LiveBlocks.end(); I != E; ++I)
          std::cerr << (*I)->getName() << " ");
  DEBUG(std::cerr << "\n");
    
 
  std::vector<Instruction*> PoolInitPoints;
  std::vector<Instruction*> PoolDestroyPoints;

  if (DisableInitDestroyOpt) {
    // Insert poolinit calls after all of the allocas...
    Instruction *InsertPoint;
    for (BasicBlock::iterator I = F.front().begin();
         isa<AllocaInst>(InsertPoint = I); ++I)
      /*empty*/;
    PoolInitPoints.push_back(InsertPoint);

    if (F.getName() != "main")
      for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
        if (isa<ReturnInst>(BB->getTerminator()) ||
            isa<UnwindInst>(BB->getTerminator()))
          PoolDestroyPoints.push_back(BB->getTerminator());
  } else {
    // Keep track of the blocks we have inserted poolinit/destroy into.
    std::set<BasicBlock*> PoolInitInsertedBlocks, PoolDestroyInsertedBlocks;
    
    for (std::set<BasicBlock*>::iterator I = LiveBlocks.begin(),
           E = LiveBlocks.end(); I != E; ++I) {
      BasicBlock *BB = *I;
      TerminatorInst *Term = BB->getTerminator();
      
      // Check the predecessors of this block.  If any preds are not in the
      // set, or if there are no preds, insert a pool init.
      bool AllIn, NoneIn;
      AllOrNoneInSet(pred_begin(BB), pred_end(BB), LiveBlocks, AllIn,
                     NoneIn);
      
      if (NoneIn) {
        if (!PoolInitInsertedBlocks.count(BB)) {
          BasicBlock::iterator It = BB->begin();
          while (isa<AllocaInst>(It) || isa<PHINode>(It)) ++It;
#if 0
          // Move through all of the instructions not in the pool
          while (!PoolUses.count(std::make_pair(PD, It)))
            // Advance past non-users deleting any pool frees that we run
            // across.
            DeleteIfIsPoolFree(It++, PD, PoolFrees);
#endif
          PoolInitPoints.push_back(It);
          PoolInitInsertedBlocks.insert(BB);
        }
      } else if (!AllIn) {
      TryAgainPred:
        for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E;
             ++PI)
          if (!LiveBlocks.count(*PI) && !PoolInitInsertedBlocks.count(*PI)){
            if (SplitCriticalEdge(BB, PI))
              // If the critical edge was split, *PI was invalidated
              goto TryAgainPred;
            
            // Insert at the end of the predecessor, before the terminator.
            PoolInitPoints.push_back((*PI)->getTerminator());
            PoolInitInsertedBlocks.insert(*PI);
          }
      }
      // Check the successors of this block.  If some succs are not in the
      // set, insert destroys on those successor edges.  If all succs are
      // not in the set, insert a destroy in this block.
      AllOrNoneInSet(succ_begin(BB), succ_end(BB), LiveBlocks,
                     AllIn, NoneIn);
      
      if (NoneIn) {
        // Insert before the terminator.
        if (!PoolDestroyInsertedBlocks.count(BB)) {
          BasicBlock::iterator It = Term;
          
          // Rewind to the first using instruction.
#if 0
          while (!PoolUses.count(std::make_pair(PD, It)))
            DeleteIfIsPoolFree(It--, PD, PoolFrees);
          ++It;
#endif
     
          // Insert after the first using instruction
          PoolDestroyPoints.push_back(It);
          PoolDestroyInsertedBlocks.insert(BB);
        }
      } else if (!AllIn) {
        for (succ_iterator SI = succ_begin(BB), E = succ_end(BB);
             SI != E; ++SI)
          if (!LiveBlocks.count(*SI) &&
              !PoolDestroyInsertedBlocks.count(*SI)) {
            // If this edge is critical, split it.
            SplitCriticalEdge(BB, SI);
            
            // Insert at entry to the successor, but after any PHI nodes.
            BasicBlock::iterator It = (*SI)->begin();
            while (isa<PHINode>(It)) ++It;
            PoolDestroyPoints.push_back(It);
            PoolDestroyInsertedBlocks.insert(*SI);
          }
      }
    }
  }

  DEBUG(std::cerr << "  Init in blocks: ");

  // Insert the calls to initialize the pool.
  unsigned ElSizeV = Heuristic::getRecommendedSize(Node);
  Value *ElSize = ConstantInt::get(Type::Int32Ty, ElSizeV);
  unsigned AlignV = Heuristic::getRecommendedAlignment(Node);
  Value *Align  = ConstantInt::get(Type::Int32Ty, AlignV);

  for (unsigned i = 0, e = PoolInitPoints.size(); i != e; ++i) {
    Value* Opts[3] = {PD, ElSize, Align};
    new CallInst(PoolInit, Opts, Opts + 3,  "", PoolInitPoints[i]);
    DEBUG(std::cerr << PoolInitPoints[i]->getParent()->getName() << " ");
  }

  DEBUG(std::cerr << "\n  Destroy in blocks: ");

  // Loop over all of the places to insert pooldestroy's...
  for (unsigned i = 0, e = PoolDestroyPoints.size(); i != e; ++i) {
    // Insert the pooldestroy call for this pool.
    new CallInst(PoolDestroy, PD, "", PoolDestroyPoints[i]);
    DEBUG(std::cerr << PoolDestroyPoints[i]->getParent()->getName()<<" ");
  }
  DEBUG(std::cerr << "\n\n");

  // We are allowed to delete any poolfree's which occur between the last
  // call to poolalloc, and the call to pooldestroy.  Figure out which
  // basic blocks have this property for this pool.
  std::set<BasicBlock*> PoolFreeLiveBlocks;
  if (!DisablePoolFreeOpt)
    CalculateLivePoolFreeBlocks(PoolFreeLiveBlocks, PD);
  else
    PoolFreeLiveBlocks = LiveBlocks;

  // Delete any pool frees which are not in live blocks, for correctness.
  std::multimap<AllocaInst*, CallInst*>::iterator PFI, PFE;
  for (tie(PFI,PFE) = PoolFrees.equal_range(PD); PFI != PFE; ) {
    CallInst *PoolFree = (PFI++)->second;
    if (!LiveBlocks.count(PoolFree->getParent()) ||
        !PoolFreeLiveBlocks.count(PoolFree->getParent()))
      DeleteIfIsPoolFree(PoolFree, PD, PoolFrees);
  }
}


/// InitializeAndDestroyPools - This inserts calls to poolinit and pooldestroy
/// into the function to initialize and destroy the pools in the NodesToPA list.
///
void PoolAllocate::InitializeAndDestroyPools(Function &F,
                               const std::vector<const DSNode*> &NodesToPA,
                          std::map<const DSNode*, Value*> &PoolDescriptors,
                          std::multimap<AllocaInst*, Instruction*> &PoolUses,
                          std::multimap<AllocaInst*, CallInst*> &PoolFrees) {
  std::set<AllocaInst*> AllocasHandled;

  // Insert all of the poolinit/destroy calls into the function.
  for (unsigned i = 0, e = NodesToPA.size(); i != e; ++i) {
    const DSNode *Node = NodesToPA[i];

    if (isa<GlobalVariable>(PoolDescriptors[Node]) ||
        isa<ConstantPointerNull>(PoolDescriptors[Node]))
      continue;

    assert(isa<AllocaInst>(PoolDescriptors[Node]) && "Why pool allocate this?");
    AllocaInst *PD = cast<AllocaInst>(PoolDescriptors[Node]);
    
    // FIXME: Turn this into an assert and fix the problem!!
    //assert(PoolUses.count(PD) && "Pool is not used, but is marked heap?!");
    if (!PoolUses.count(PD) && !PoolFrees.count(PD)) continue;
    if (!AllocasHandled.insert(PD).second) continue;
    
    ++NumPools;
    if (!Node->isNodeCompletelyFolded())
      ++NumTSPools;
    
    InitializeAndDestroyPool(F, Node, PoolDescriptors, PoolUses, PoolFrees);
  }
}
