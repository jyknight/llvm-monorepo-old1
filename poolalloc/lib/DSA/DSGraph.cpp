//===- DataStructure.cpp - Implement the core data structure analysis -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the core data structure functionality.
//
//===----------------------------------------------------------------------===//

#include "dsa/DSGraphTraits.h"
#include "dsa/DataStructure.h"
#include "dsa/DSGraph.h"
#include "dsa/DSSupport.h"
#include "dsa/DSNode.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Instructions.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <algorithm>
using namespace llvm;

#define COLLAPSE_ARRAYS_AGGRESSIVELY 0
namespace {
  STATISTIC (NumCallNodesMerged, "Number of call nodes merged");
  STATISTIC (NumDNE            , "Number of nodes removed by reachability");
  STATISTIC (NumTrivialDNE     , "Number of nodes trivially removed");
  STATISTIC (NumTrivialGlobalDNE, "Number of globals trivially removed");
  static cl::opt<unsigned>
}

/// getFunctionNames - Return a space separated list of the name of the
/// functions in this graph (if any)
std::string DSGraph::getFunctionNames() const {
  switch (getReturnNodes().size()) {
  case 0: return "Globals graph";
  case 1: return retnodes_begin()->first->getName();
  default:
    std::string Return;
    for (DSGraph::retnodes_iterator I = retnodes_begin();
         I != retnodes_end(); ++I)
      Return += I->first->getNameStr() + " ";
    Return.erase(Return.end()-1, Return.end());   // Remove last space character
    return Return;
  }
}


DSGraph::DSGraph(DSGraph* G, EquivalenceClasses<const GlobalValue*> &ECs,
                 SuperSet<const Type*>& tss,
                 unsigned CloneFlags)
  : GlobalsGraph(0), ScalarMap(ECs), TD(G->TD), TypeSS(tss) {
  PrintAuxCalls = false;
  cloneInto(G, CloneFlags);
}

DSGraph::~DSGraph() {
  FunctionCalls.clear();
  AuxFunctionCalls.clear();
  ScalarMap.clear();
  ReturnNodes.clear();

  // Drop all intra-node references, so that assertions don't fail...
  for (node_iterator NI = node_begin(), E = node_end(); NI != E; ++NI)
    NI->dropAllReferences();

  // Free all of the nodes.
  Nodes.clear();
}

// dump - Allow inspection of graph in a debugger.
void DSGraph::dump() const { print(errs()); }

void DSGraph::removeFunctionCalls(Function& F) {
  for (std::list<DSCallSite>::iterator I = FunctionCalls.begin(),
         E = FunctionCalls.end(); I != E; ++I)
    if (I->isDirectCall() && I->getCalleeFunc() == &F) {
      FunctionCalls.erase(I);
      break;
    }

  for (std::list<DSCallSite>::iterator I = AuxFunctionCalls.begin(),
         E = AuxFunctionCalls.end(); I != E; ++I)
    if (I->isDirectCall() && I->getCalleeFunc() == &F) {
      AuxFunctionCalls.erase(I);
      break;
    }
}

/// addObjectToGraph - This method can be used to add global, stack, and heap
/// objects to the graph.  This can be used when updating DSGraphs due to the
/// introduction of new temporary objects.  The new object is not pointed to
/// and does not point to any other objects in the graph.
DSNode *DSGraph::addObjectToGraph(Value *Ptr, bool UseDeclaredType) {
  assert(isa<PointerType>(Ptr->getType()) && "Ptr is not a pointer!");
  DSNode *N = new DSNode(this);
  assert(ScalarMap[Ptr].isNull() && "Object already in this graph!");
  ScalarMap[Ptr] = N;

  if (GlobalValue *GV = dyn_cast<GlobalValue>(Ptr)) {
    N->addGlobal(GV);
//  } else if (isa<MallocInst>(Ptr)) {
//   N->setHeapMarker();
  } else if (isa<AllocaInst>(Ptr)) {
    N->setAllocaMarker();
  } else {
    assert(0 && "Illegal memory object input!");
  }
  return N;
}


/// cloneInto - Clone the specified DSGraph into the current graph.  The
/// translated ScalarMap for the old function is filled into the ScalarMap
/// for the graph, and the translated ReturnNodes map is returned into
/// ReturnNodes.
///
/// The CloneFlags member controls various aspects of the cloning process.
///
void DSGraph::cloneInto( DSGraph* G, unsigned CloneFlags) {
  assert(G != this && "Cannot clone graph into itself!");

  NodeMapTy OldNodeMap;

  // Remove alloca or mod/ref bits as specified...
  unsigned BitsToClear = ((CloneFlags & StripAllocaBit)? DSNode::AllocaNode : 0)
    | ((CloneFlags & StripModRefBits)? (DSNode::ModifiedNode | DSNode::ReadNode) : 0)
    | ((CloneFlags & StripIncompleteBit)? DSNode::IncompleteNode : 0);
  BitsToClear |= DSNode::DeadNode;  // Clear dead flag...

  for (node_const_iterator I = G->node_begin(), E = G->node_end(); I != E; ++I) {
    assert(!I->isForwarding() &&
           "Forward nodes shouldn't be in node list!");
    DSNode *New = new DSNode(*I, this);
    New->maskNodeTypes(~BitsToClear);
    OldNodeMap[I] = New;
  }

#ifndef NDEBUG
  Timer::addPeakMemoryMeasurement();
#endif

  // Rewrite the links in the new nodes to point into the current graph now.
  // Note that we don't loop over the node's list to do this.  The problem is
  // that remaping links can cause recursive merging to happen, which means
  // that node_iterator's can get easily invalidated!  Because of this, we
  // loop over the OldNodeMap, which contains all of the new nodes as the
  // .second element of the map elements.  Also note that if we remap a node
  // more than once, we won't break anything.
  for (NodeMapTy::iterator I = OldNodeMap.begin(), E = OldNodeMap.end();
       I != E; ++I)
    I->second.getNode()->remapLinks(OldNodeMap);

  // Copy the scalar map... merging all of the global nodes...
  for (DSScalarMap::const_iterator I = G->ScalarMap.begin(),
         E = G->ScalarMap.end(); I != E; ++I) {
    DSNodeHandle &MappedNode = OldNodeMap[I->second.getNode()];
    DSNodeHandle &H = ScalarMap.getRawEntryRef(I->first);
    DSNode *MappedNodeN = MappedNode.getNode();
    H.mergeWith(DSNodeHandle(MappedNodeN,
                             I->second.getOffset()+MappedNode.getOffset()));
  }

  if (!(CloneFlags & DontCloneCallNodes)) {
    // Copy the function calls list.
    for (fc_iterator I = G->fc_begin(), E = G->fc_end(); I != E; ++I)
      FunctionCalls.push_back(DSCallSite(*I, OldNodeMap));
  }

  if (!(CloneFlags & DontCloneAuxCallNodes)) {
    // Copy the auxiliary function calls list.
    for (afc_iterator I = G->afc_begin(), E = G->afc_end(); I != E; ++I)
      AuxFunctionCalls.push_back(DSCallSite(*I, OldNodeMap));
  }

  // Map the return node pointers over...
  for (retnodes_iterator I = G->retnodes_begin(),
         E = G->retnodes_end(); I != E; ++I) {
    const DSNodeHandle &Ret = I->second;
    DSNodeHandle &MappedRet = OldNodeMap[Ret.getNode()];
    DSNode *MappedRetN = MappedRet.getNode();
    ReturnNodes.insert(std::make_pair(I->first,
                                      DSNodeHandle(MappedRetN,
                                     MappedRet.getOffset()+Ret.getOffset())));
  }
}

/// spliceFrom - Logically perform the operation of cloning the RHS graph into
/// this graph, then clearing the RHS graph.  Instead of performing this as
/// two seperate operations, do it as a single, much faster, one.
///
void DSGraph::spliceFrom(DSGraph* RHS) {
  assert(this != RHS && "Splicing self");
  // Change all of the nodes in RHS to think we are their parent.
  for (NodeListTy::iterator I = RHS->Nodes.begin(), E = RHS->Nodes.end();
       I != E; ++I)
    I->setParentGraph(this);
  // Take all of the nodes.
  Nodes.splice(Nodes.end(), RHS->Nodes);

  // Take all of the calls.
  FunctionCalls.splice(FunctionCalls.end(), RHS->FunctionCalls);
  AuxFunctionCalls.splice(AuxFunctionCalls.end(), RHS->AuxFunctionCalls);

  // Take all of the return nodes.
  if (ReturnNodes.empty()) {
    ReturnNodes.swap(RHS->ReturnNodes);
  } else {
    ReturnNodes.insert(RHS->ReturnNodes.begin(), RHS->ReturnNodes.end());
    RHS->ReturnNodes.clear();
  }

 // Merge the scalar map in.
  ScalarMap.spliceFrom(RHS->ScalarMap);
}

/// getFunctionArgumentsForCall - Given a function that is currently in this
/// graph, return the DSNodeHandles that correspond to the pointer-compatible
/// function arguments.  The vector is filled in with the return value (or
/// null if it is not pointer compatible), followed by all of the
/// pointer-compatible arguments.
void DSGraph::getFunctionArgumentsForCall(const Function *F,
                                       std::vector<DSNodeHandle> &Args) const {
  Args.push_back(getReturnNodeFor(*F));
  for (Function::const_arg_iterator AI = F->arg_begin(), E = F->arg_end();
       AI != E; ++AI)
    if (isa<PointerType>(AI->getType())) {
      Args.push_back(getNodeForValue(AI));
      assert(!Args.back().isNull() && "Pointer argument w/o scalarmap entry!?");
    }
}

namespace {
  // HackedGraphSCCFinder - This is used to find nodes that have a path from the
  // node to a node cloned by the ReachabilityCloner object contained.  To be
  // extra obnoxious it ignores edges from nodes that are globals, and truncates
  // search at RC marked nodes.  This is designed as an object so that
  // intermediate results can be memoized across invocations of
  // PathExistsToClonedNode.
  struct HackedGraphSCCFinder {
    ReachabilityCloner &RC;
    unsigned CurNodeId;
    std::vector<const DSNode*> SCCStack;
    std::map<const DSNode*, std::pair<unsigned, bool> > NodeInfo;

    HackedGraphSCCFinder(ReachabilityCloner &rc) : RC(rc), CurNodeId(1) {
      // Remove null pointer as a special case.
      NodeInfo[0] = std::make_pair(0, false);
    }

    std::pair<unsigned, bool> &VisitForSCCs(const DSNode *N);

    bool PathExistsToClonedNode(const DSNode *N) {
      return VisitForSCCs(N).second;
    }

    bool PathExistsToClonedNode(const DSCallSite &CS) {
      if (PathExistsToClonedNode(CS.getRetVal().getNode()))
        return true;
      if (CS.isDirectCall() || PathExistsToClonedNode(CS.getCalleeNode()))
        return true;
      for (unsigned i = 0, e = CS.getNumPtrArgs(); i != e; ++i)
        if (PathExistsToClonedNode(CS.getPtrArg(i).getNode()))
          return true;
      return false;
    }
  };
}

std::pair<unsigned, bool> &HackedGraphSCCFinder::
VisitForSCCs(const DSNode *N) {
  std::map<const DSNode*, std::pair<unsigned, bool> >::iterator
    NodeInfoIt = NodeInfo.lower_bound(N);
  if (NodeInfoIt != NodeInfo.end() && NodeInfoIt->first == N)
    return NodeInfoIt->second;

  unsigned Min = CurNodeId++;
  unsigned MyId = Min;
  std::pair<unsigned, bool> &ThisNodeInfo =
    NodeInfo.insert(NodeInfoIt,
                    std::make_pair(N, std::make_pair(MyId, false)))->second;

  // Base case: if we find a global, this doesn't reach the cloned graph
  // portion.
  if (N->isGlobalNode()) {
    ThisNodeInfo.second = false;
    return ThisNodeInfo;
  }

  // Base case: if this does reach the cloned graph portion... it does. :)
  if (RC.hasClonedNode(N)) {
    ThisNodeInfo.second = true;
    return ThisNodeInfo;
  }

  SCCStack.push_back(N);

  // Otherwise, check all successors.
  bool AnyDirectSuccessorsReachClonedNodes = false;
  for (DSNode::const_edge_iterator EI = N->edge_begin(), EE = N->edge_end();
       EI != EE; ++EI)
    if (DSNode * Succ = EI->second.getNode()) {
      std::pair<unsigned, bool> &SuccInfo = VisitForSCCs(Succ);
      if (SuccInfo.first < Min) Min = SuccInfo.first;
      AnyDirectSuccessorsReachClonedNodes |= SuccInfo.second;
    }

  if (Min != MyId)
    return ThisNodeInfo;  // Part of a large SCC.  Leave self on stack.

  if (SCCStack.back() == N) {  // Special case single node SCC.
    SCCStack.pop_back();
    ThisNodeInfo.second = AnyDirectSuccessorsReachClonedNodes;
    return ThisNodeInfo;
  }

  // Find out if any direct successors of any node reach cloned nodes.
  if (!AnyDirectSuccessorsReachClonedNodes)
    for (unsigned i = SCCStack.size() - 1; SCCStack[i] != N; --i)
      for (DSNode::const_edge_iterator EI = N->edge_begin(), EE = N->edge_end();
           EI != EE; ++EI)
        if (DSNode * N = EI->second.getNode())
          if (NodeInfo[N].second) {
            AnyDirectSuccessorsReachClonedNodes = true;
            goto OutOfLoop;
          }
OutOfLoop:
  // If any successor reaches a cloned node, mark all nodes in this SCC as
  // reaching the cloned node.
  if (AnyDirectSuccessorsReachClonedNodes)
    while (SCCStack.back() != N) {
      NodeInfo[SCCStack.back()].second = true;
      SCCStack.pop_back();
    }
  SCCStack.pop_back();
  ThisNodeInfo.second = true;
  return ThisNodeInfo;
}

/// mergeInCallFromOtherGraph - This graph merges in the minimal number of
/// nodes from G2 into 'this' graph, merging the bindings specified by the
/// call site (in this graph) with the bindings specified by the vector in G2.
/// The two DSGraphs must be different.
///
void DSGraph::mergeInGraph(const DSCallSite &CS,
                           std::vector<DSNodeHandle> &Args,
                           const DSGraph &Graph, unsigned CloneFlags) {
  assert((CloneFlags & DontCloneCallNodes) &&
         "Doesn't support copying of call nodes!");

  // If this is not a recursive call, clone the graph into this graph...
  if (&Graph == this) {
    // Merge the return value with the return value of the context.
    Args[0].mergeWith(CS.getRetVal());

    // Resolve all of the function arguments.
    for (unsigned i = 0, e = CS.getNumPtrArgs(); i != e; ++i) {
      if (i == Args.size()-1)
        break;

      // Add the link from the argument scalar to the provided value.
      Args[i+1].mergeWith(CS.getPtrArg(i));
    }
    return;
  }

  // Clone the callee's graph into the current graph, keeping track of where
  // scalars in the old graph _used_ to point, and of the new nodes matching
  // nodes of the old graph.
  ReachabilityCloner RC(this, &Graph, CloneFlags);

  // Map the return node pointer over.
  if (!CS.getRetVal().isNull())
    RC.merge(CS.getRetVal(), Args[0]);

  // Map over all of the arguments.
  for (unsigned i = 0, e = CS.getNumPtrArgs(); i != e; ++i) {
    if (i == Args.size()-1)
      break;

    // Add the link from the argument scalar to the provided value.
    RC.merge(CS.getPtrArg(i), Args[i+1]);
  }

  // We generally don't want to copy global nodes or aux calls from the callee
  // graph to the caller graph.  However, we have to copy them if there is a
  // path from the node to a node we have already copied which does not go
  // through another global.  Compute the set of node that can reach globals and
  // aux call nodes to copy over, then do it.
  std::vector<const DSCallSite*> AuxCallToCopy;
  std::vector<const GlobalValue*> GlobalsToCopy;

  // NodesReachCopiedNodes - Memoize results for efficiency.  Contains a
  // true/false value for every visited node that reaches a copied node without
  // going through a global.
  HackedGraphSCCFinder SCCFinder(RC);

  if (!(CloneFlags & DontCloneAuxCallNodes))
    for (afc_const_iterator I = Graph.afc_begin(), E = Graph.afc_end(); I!=E; ++I)
      if (SCCFinder.PathExistsToClonedNode(*I))
        AuxCallToCopy.push_back(&*I);
//       else if (I->isIndirectCall()){
//  	//If the call node doesn't have any callees, clone it
//  	std::vector< Function *> List;
//  	I->getCalleeNode()->addFullFunctionList(List);
//  	if (!List.size())
//  	  AuxCallToCopy.push_back(&*I);
//        }

  const DSScalarMap &GSM = Graph.getScalarMap();
  for (DSScalarMap::global_iterator GI = GSM.global_begin(),
         E = GSM.global_end(); GI != E; ++GI) {
    DSNode *GlobalNode = Graph.getNodeForValue(*GI).getNode();
    for (DSNode::edge_iterator EI = GlobalNode->edge_begin(),
           EE = GlobalNode->edge_end(); EI != EE; ++EI)
      if (SCCFinder.PathExistsToClonedNode(EI->second.getNode())) {
        GlobalsToCopy.push_back(*GI);
        break;
      }
  }

  // Copy aux calls that are needed.
  for (unsigned i = 0, e = AuxCallToCopy.size(); i != e; ++i)
    AuxFunctionCalls.push_back(DSCallSite(*AuxCallToCopy[i], RC));

  // Copy globals that are needed.
  for (unsigned i = 0, e = GlobalsToCopy.size(); i != e; ++i)
    RC.getClonedNH(Graph.getNodeForValue(GlobalsToCopy[i]));
}



/// mergeInGraph - The method is used for merging graphs together.  If the
/// argument graph is not *this, it makes a clone of the specified graph, then
/// merges the nodes specified in the call site with the formal arguments in the
/// graph.
///
void DSGraph::mergeInGraph(const DSCallSite &CS, const Function &F,
                           const DSGraph &Graph, unsigned CloneFlags) {
  // Set up argument bindings.
  std::vector<DSNodeHandle> Args;
  Graph.getFunctionArgumentsForCall(&F, Args);

  mergeInGraph(CS, Args, Graph, CloneFlags);
}

/// getCallSiteForArguments - Get the arguments and return value bindings for
/// the specified function in the current graph.
///
DSCallSite DSGraph::getCallSiteForArguments(const Function &F) const {
  std::vector<DSNodeHandle> Args;

  for (Function::const_arg_iterator I = F.arg_begin(), E = F.arg_end(); I != E; ++I)
    if (isa<PointerType>(I->getType()))
      Args.push_back(getNodeForValue(I));

  return DSCallSite(CallSite(), getReturnNodeFor(F), &F, Args);
}

/// getDSCallSiteForCallSite - Given an LLVM CallSite object that is live in
/// the context of this graph, return the DSCallSite for it.
DSCallSite DSGraph::getDSCallSiteForCallSite(CallSite CS) const {
  DSNodeHandle RetVal;
  Instruction *I = CS.getInstruction();
  if (isa<PointerType>(I->getType()))
    RetVal = getNodeForValue(I);

  std::vector<DSNodeHandle> Args;
  Args.reserve(CS.arg_end()-CS.arg_begin());

  // Calculate the arguments vector...
  for (CallSite::arg_iterator I = CS.arg_begin(), E = CS.arg_end(); I != E; ++I)
    if (isa<PointerType>((*I)->getType())) {
      if (isa<ConstantPointerNull>(*I))
        Args.push_back(DSNodeHandle());
      else
        Args.push_back(getNodeForValue(*I));
    }

  // Add a new function call entry...
  if (Function *F = CS.getCalledFunction())
    return DSCallSite(CS, RetVal, F, Args);
  else
    return DSCallSite(CS, RetVal,
                      getNodeForValue(CS.getCalledValue()).getNode(), Args);
}



// markIncompleteNodes - Mark the specified node as having contents that are not
// known with the current analysis we have performed.  Because a node makes all
// of the nodes it can reach incomplete if the node itself is incomplete, we
// must recursively traverse the data structure graph, marking all reachable
// nodes as incomplete.
//
static void markIncompleteNode(DSNode *N) {
  // Stop recursion if no node, or if node already marked...
  if (N == 0 || N->isIncompleteNode()) return;

  // Actually mark the node
  N->setIncompleteMarker();

  // Recursively process children...
  for (DSNode::edge_iterator ii = N->edge_begin(), ee = N->edge_end();
       ii != ee; ++ii)
    markIncompleteNode(ii->second.getNode());
}

static void markIncomplete(DSCallSite &Call) {
  // Then the return value is certainly incomplete!
  markIncompleteNode(Call.getRetVal().getNode());

  // All objects pointed to by function arguments are incomplete!
  for (unsigned i = 0, e = Call.getNumPtrArgs(); i != e; ++i)
    markIncompleteNode(Call.getPtrArg(i).getNode());
}

// markIncompleteNodes - Traverse the graph, identifying nodes that may be
// modified by other functions that have not been resolved yet.  This marks
// nodes that are reachable through three sources of "unknownness":
//
//  Global Variables, Function Calls, and Incoming Arguments
//
// For any node that may have unknown components (because something outside the
// scope of current analysis may have modified it), the 'Incomplete' flag is
// added to the NodeType.
//
void DSGraph::markIncompleteNodes(unsigned Flags) {
  // Mark any incoming arguments as incomplete.
  if (Flags & DSGraph::MarkFormalArgs)
    for (ReturnNodesTy::iterator FI = ReturnNodes.begin(), E =ReturnNodes.end();
         FI != E; ++FI) {
      const Function &F = *FI->first;
      for (Function::const_arg_iterator I = F.arg_begin(), E = F.arg_end();
           I != E; ++I)
        if (isa<PointerType>(I->getType()))
          markIncompleteNode(getNodeForValue(I).getNode());
      markIncompleteNode(FI->second.getNode());
    }

  // Mark stuff passed into functions calls as being incomplete.
  if (!shouldPrintAuxCalls())
    for (std::list<DSCallSite>::iterator I = FunctionCalls.begin(),
           E = FunctionCalls.end(); I != E; ++I)
      markIncomplete(*I);
  else
    for (std::list<DSCallSite>::iterator I = AuxFunctionCalls.begin(),
           E = AuxFunctionCalls.end(); I != E; ++I)
      markIncomplete(*I);

#if 0
  // Mark stuff passed into external functions as being incomplete.
  // External functions may not appear in Aux during td, so process
  // them specially
  for (std::list<DSCallSite>::iterator I = FunctionCalls.begin(),
         E = FunctionCalls.end(); I != E; ++I)
    if(I->isDirectCall() && I->getCalleeFunc()->isDeclaration())
      markIncomplete(*I);
#endif

  // Mark all global nodes as incomplete.
  for (DSScalarMap::global_iterator I = ScalarMap.global_begin(),
         E = ScalarMap.global_end(); I != E; ++I)
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(*I))
      if (!GV->hasInitializer() ||    // Always mark external globals incomp.
          (!GV->isConstant() && (Flags & DSGraph::IgnoreGlobals) == 0))
        markIncompleteNode(ScalarMap[GV].getNode());

  // Mark any node with the VAStart flag as incomplete.
  if (Flags & DSGraph::MarkVAStart) {
    for (node_iterator i=node_begin(); i != node_end(); ++i) {
      markIncompleteNode(i);
    }
  }
}

static inline void killIfUselessEdge(DSNodeHandle &Edge) {
  if (DSNode * N = Edge.getNode()) // Is there an edge?
    if (N->getNumReferrers() == 1)  // Does it point to a lonely node?
      // No interesting info?
      if ((N->getNodeFlags() & ~DSNode::IncompleteNode) == 0
          && N->hasNoType()
          && !N->isNodeCompletelyFolded())
        Edge.setTo(0, 0);  // Kill the edge!
}

static void removeIdenticalCalls(std::list<DSCallSite> &Calls) {
  // Remove trivially identical function calls
  Calls.sort();  // Sort by callee as primary key!

  // Scan the call list cleaning it up as necessary...
  DSNodeHandle LastCalleeNode;
#if 0
  Function *LastCalleeFunc = 0;
  unsigned NumDuplicateCalls = 0;
#endif
  bool LastCalleeContainsExternalFunction = false;

  unsigned NumDeleted = 0;
  for (std::list<DSCallSite>::iterator I = Calls.begin(), E = Calls.end();
       I != E;) {
    DSCallSite &CS = *I;
    std::list<DSCallSite>::iterator OldIt = I++;

    if (!CS.isIndirectCall()) {
      LastCalleeNode = 0;
    } else {
      DSNode *Callee = CS.getCalleeNode();

      // If the Callee is a useless edge, this must be an unreachable call site,
      // eliminate it.
      if (Callee->getNumReferrers() == 1 && Callee->isCompleteNode() &&
          Callee->isEmptyGlobals()) {  // No useful info?
        DEBUG(errs() << "WARNING: Useless call site found.\n");
        Calls.erase(OldIt);
        ++NumDeleted;
        continue;
      }

      // If the last call site in the list has the same callee as this one, and
      // if the callee contains an external function, it will never be
      // resolvable, just merge the call sites.
      if (!LastCalleeNode.isNull() && LastCalleeNode.getNode() == Callee) {
        LastCalleeContainsExternalFunction = Callee->isExternFuncNode();

        std::list<DSCallSite>::iterator PrevIt = OldIt;
        --PrevIt;
        PrevIt->mergeWith(CS);

        // No need to keep this call anymore.
        Calls.erase(OldIt);
        ++NumDeleted;
        continue;
      } else {
        LastCalleeNode = Callee;
      }
    }

    // If the return value or any arguments point to a void node with no
    // information at all in it, and the call node is the only node to point
    // to it, remove the edge to the node (killing the node).
    //
    killIfUselessEdge(CS.getRetVal());
    for (unsigned a = 0, e = CS.getNumPtrArgs(); a != e; ++a)
      killIfUselessEdge(CS.getPtrArg(a));

#if 0
    // If this call site calls the same function as the last call site, and if
    // the function pointer contains an external function, this node will
    // never be resolved.  Merge the arguments of the call node because no
    // information will be lost.
    //
    if ((CS.isDirectCall()   && CS.getCalleeFunc() == LastCalleeFunc) ||
        (CS.isIndirectCall() && CS.getCalleeNode() == LastCalleeNode)) {
      ++NumDuplicateCalls;
      if (NumDuplicateCalls == 1) {
        if (LastCalleeNode)
          LastCalleeContainsExternalFunction =
            nodeContainsExternalFunction(LastCalleeNode);
        else
          LastCalleeContainsExternalFunction = LastCalleeFunc->isExternal();
      }

      // It is not clear why, but enabling this code makes DSA really
      // sensitive to node forwarding.  Basically, with this enabled, DSA
      // performs different number of inlinings based on which nodes are
      // forwarding or not.  This is clearly a problem, so this code is
      // disabled until this can be resolved.
#if 1
      if (LastCalleeContainsExternalFunction
#if 0
          ||
          // This should be more than enough context sensitivity!
          // FIXME: Evaluate how many times this is tripped!
          NumDuplicateCalls > 20
#endif
          ) {

        std::list<DSCallSite>::iterator PrevIt = OldIt;
        --PrevIt;
        PrevIt->mergeWith(CS);

        // No need to keep this call anymore.
        Calls.erase(OldIt);
        ++NumDeleted;
        continue;
      }
#endif
    } else {
      if (CS.isDirectCall()) {
        LastCalleeFunc = CS.getCalleeFunc();
        LastCalleeNode = 0;
      } else {
        LastCalleeNode = CS.getCalleeNode();
        LastCalleeFunc = 0;
      }
      NumDuplicateCalls = 0;
    }
#endif

    if (I != Calls.end() && CS == *I) {
      LastCalleeNode = 0;
      Calls.erase(OldIt);
      ++NumDeleted;
      continue;
    }
  }

  // Resort now that we simplified things.
  Calls.sort();

  // Now that we are in sorted order, eliminate duplicates.
  std::list<DSCallSite>::iterator CI = Calls.begin(), CE = Calls.end();
  if (CI != CE)
    while (1) {
      std::list<DSCallSite>::iterator OldIt = CI++;
      if (CI == CE) break;

      // If this call site is now the same as the previous one, we can delete it
      // as a duplicate.
      if (*OldIt == *CI) {
        DEBUG(errs() << "Deleteing " << CI->getCallSite().getInstruction() << "\n");
        Calls.erase(CI);
        CI = OldIt;
        ++NumDeleted;
      }
    }

  //Calls.erase(std::unique(Calls.begin(), Calls.end()), Calls.end());

  // Track the number of call nodes merged away...
  NumCallNodesMerged += NumDeleted;

  if (NumDeleted)
    DEBUG(errs() << "Merged " << NumDeleted << " call nodes.\n");
}


// removeTriviallyDeadNodes - After the graph has been constructed, this method
// removes all unreachable nodes that are created because they got merged with
// other nodes in the graph.  These nodes will all be trivially unreachable, so
// we don't have to perform any non-trivial analysis here.
//
void DSGraph::removeTriviallyDeadNodes() {
  /// NOTE: This code is disabled.  This slows down DSA on 177.mesa
  /// substantially!

  // Loop over all of the nodes in the graph, calling getNode on each field.
  // This will cause all nodes to update their forwarding edges, causing
  // forwarded nodes to be delete-able.  Further, reclaim any memory used by
  // useless edge or type entries
  for (node_iterator NI = node_begin(), E = node_end(); NI != E; ++NI)
    for (DSNode::edge_iterator ii = NI->edge_begin(), ee = NI->edge_end();
         ii != ee; ++ii) {
      ii->second.getNode()->cleanEdges();
    }

  // Likewise, forward any edges from the scalar nodes.  While we are at it,
  // clean house a bit.
  for (DSScalarMap::iterator I = ScalarMap.begin(), E = ScalarMap.end();
       I != E; ++I)
    I->second.getNode();

  bool isGlobalsGraph = !GlobalsGraph;

  for (NodeListTy::iterator NI = Nodes.begin(), E = Nodes.end(); NI != E; ) {
    DSNode &Node = *NI;

    // Do not remove *any* global nodes in the globals graph.
    // This is a special case because such nodes may not have I, M, R flags set.
    if (Node.isGlobalNode() && isGlobalsGraph) {
      ++NI;
      continue;
    }

    if (Node.isCompleteNode() && !Node.isModifiedNode() && !Node.isReadNode()) {
      // This is a useless node if it has no mod/ref info (checked above),
      // outgoing edges (which it cannot, as it is not modified in this
      // context), and it has no incoming edges.  If it is a global node it may
      // have all of these properties and still have incoming edges, due to the
      // scalar map, so we check those now.
      //
      if (Node.getNumReferrers() == Node.numGlobals()) {

        // Loop through and make sure all of the globals are referring directly
        // to the node...
        for (DSNode::globals_iterator j = Node.globals_begin(), e = Node.globals_end();
             j != e; ++j) {
          DSNode *N = getNodeForValue(*j).getNode();
          assert(N == &Node && "ScalarMap doesn't match globals list!");
        }

        // Make sure NumReferrers still agrees, if so, the node is truly dead.
        if (Node.getNumReferrers() == Node.numGlobals()) {
          for (DSNode::globals_iterator j = Node.globals_begin(), e = Node.globals_end();
               j != e; ++j)
            if (ScalarMap.find(*j) != ScalarMap.end())
              ScalarMap.erase(*j);
          Node.makeNodeDead();
          ++NumTrivialGlobalDNE;
        }
      }
    }

    if ((Node.getNodeFlags() == 0 && Node.hasNoReferrers())
        || (isGlobalsGraph && Node.hasNoReferrers() && !Node.isGlobalNode())){
      // This node is dead!
      NI = Nodes.erase(NI);    // Erase & remove from node list.
      ++NumTrivialDNE;
    } else {
      ++NI;
    }
  }

  removeIdenticalCalls(FunctionCalls);
  removeIdenticalCalls(AuxFunctionCalls);
}

// CanReachAliveNodes - Simple graph walker that recursively traverses the graph
// looking for a node that is marked alive.  If an alive node is found, return
// true, otherwise return false.  If an alive node is reachable, this node is
// marked as alive...
//
static bool CanReachAliveNodes(DSNode *N, DenseSet<const DSNode*> &Alive,
                               DenseSet<const DSNode*> &Visited,
                               bool IgnoreGlobals) {
  if (N == 0) return false;
  assert(N->isForwarding() == 0 && "Cannot mark a forwarded node!");

  // If this is a global node, it will end up in the globals graph anyway, so we
  // don't need to worry about it.
  if (IgnoreGlobals && N->isGlobalNode()) return false;

  // If we know that this node is alive, return so!
  if (Alive.count(N)) return true;

  // Otherwise, we don't think the node is alive yet, check for infinite
  // recursion.
  if (Visited.count(N)) return false;  // Found a cycle
  Visited.insert(N);   // No recursion, insert into Visited...

  for (DSNode::edge_iterator I = N->edge_begin(),E = N->edge_end(); I != E; ++I)
    if (CanReachAliveNodes(I->second.getNode(), Alive, Visited, IgnoreGlobals)) {
      N->markReachableNodes(Alive);
      return true;
    }
  return false;
}

// CallSiteUsesAliveArgs - Return true if the specified call site can reach any
// alive nodes.
//
static bool CallSiteUsesAliveArgs(const DSCallSite &CS,
                                  DenseSet<const DSNode*> &Alive,
                                  DenseSet<const DSNode*> &Visited,
                                  bool IgnoreGlobals) {
  if (CanReachAliveNodes(CS.getRetVal().getNode(), Alive, Visited,
                         IgnoreGlobals))
    return true;
  if (CS.isIndirectCall() &&
      CanReachAliveNodes(CS.getCalleeNode(), Alive, Visited, IgnoreGlobals))
    return true;
  for (unsigned i = 0, e = CS.getNumPtrArgs(); i != e; ++i)
    if (CanReachAliveNodes(CS.getPtrArg(i).getNode(), Alive, Visited,
                           IgnoreGlobals))
      return true;
  return false;
}

// removeDeadNodes - Use a more powerful reachability analysis to eliminate
// subgraphs that are unreachable.  This often occurs because the data
// structure doesn't "escape" into it's caller, and thus should be eliminated
// from the caller's graph entirely.  This is only appropriate to use when
// inlining graphs.
//
void DSGraph::removeDeadNodes(unsigned Flags) {
  DEBUG(AssertGraphOK(); if (GlobalsGraph) GlobalsGraph->AssertGraphOK());

  // Reduce the amount of work we have to do... remove dummy nodes left over by
  // merging...
  removeTriviallyDeadNodes();

  // FIXME: Merge non-trivially identical call nodes...

  // Alive - a set that holds all nodes found to be reachable/alive.
  DenseSet<const DSNode*> Alive;
  std::vector<std::pair<const Value*, DSNode*> > GlobalNodes;

  // Copy and merge all information about globals to the GlobalsGraph if this is
  // not a final pass (where unreachable globals are removed).
  //
  // Strip all alloca bits since the current function is only for the BU pass.
  // Strip all incomplete bits since they are short-lived properties and they
  // will be correctly computed when rematerializing nodes into the functions.
  //
  ReachabilityCloner GGCloner(GlobalsGraph, this, DSGraph::StripAllocaBit |
                              DSGraph::StripIncompleteBit);

  // Mark all nodes reachable by (non-global) scalar nodes as alive...
  for (DSScalarMap::iterator I = ScalarMap.begin(), E = ScalarMap.end();
          I != E; ++I)
    if (isa<GlobalValue > (I->first)) { // Keep track of global nodes
      assert(!I->second.isNull() && "Null global node?");
      assert(I->second.getNode()->isGlobalNode() && "Should be a global node!");
      GlobalNodes.push_back(std::make_pair(I->first, I->second.getNode()));

      // Make sure that all globals are cloned over as roots.
      if (!(Flags & DSGraph::RemoveUnreachableGlobals) && GlobalsGraph) {
        DSGraph::ScalarMapTy::iterator SMI =
          GlobalsGraph->getScalarMap().find(I->first);
        if (SMI != GlobalsGraph->getScalarMap().end())
          GGCloner.merge(SMI->second, I->second);
        else
          GGCloner.getClonedNH(I->second);
      }
    } else {
      I->second.getNode()->markReachableNodes(Alive);
    }

  // The return values are alive as well.
  for (ReturnNodesTy::iterator I = ReturnNodes.begin(), E = ReturnNodes.end();
       I != E; ++I)
    I->second.getNode()->markReachableNodes(Alive);

  // Mark any nodes reachable by primary calls as alive...
  for (fc_iterator I = fc_begin(), E = fc_end(); I != E; ++I)
    I->markReachableNodes(Alive);


  // Now find globals and aux call nodes that are already live or reach a live
  // value (which makes them live in turn), and continue till no more are found.
  //
  bool Iterate;
  DenseSet<const DSNode*> Visited;
  std::set<const DSCallSite*> AuxFCallsAlive;
  do {
    Visited.clear();
    // If any global node points to a non-global that is "alive", the global is
    // "alive" as well...  Remove it from the GlobalNodes list so we only have
    // unreachable globals in the list.
    //
    Iterate = false;
    if (!(Flags & DSGraph::RemoveUnreachableGlobals))
      for (unsigned i = 0; i != GlobalNodes.size(); ++i)
        if (CanReachAliveNodes(GlobalNodes[i].second, Alive, Visited,
                               Flags & DSGraph::RemoveUnreachableGlobals)) {
          std::swap(GlobalNodes[i--], GlobalNodes.back()); // Move to end to...
          GlobalNodes.pop_back();                          // erase efficiently
          Iterate = true;
        }

    // Mark only unresolvable call nodes for moving to the GlobalsGraph since
    // call nodes that get resolved will be difficult to remove from that graph.
    // The final unresolved call nodes must be handled specially at the end of
    // the BU pass (i.e., in main or other roots of the call graph).
    for (afc_iterator CI = afc_begin(), E = afc_end(); CI != E; ++CI)
      if (!AuxFCallsAlive.count(&*CI) &&
          (CI->isIndirectCall()
           || CallSiteUsesAliveArgs(*CI, Alive, Visited,
                                  Flags & DSGraph::RemoveUnreachableGlobals))) {
        CI->markReachableNodes(Alive);
        AuxFCallsAlive.insert(&*CI);
        Iterate = true;
      }
  } while (Iterate);

  // Move dead aux function calls to the end of the list
  for (std::list<DSCallSite>::iterator CI = AuxFunctionCalls.begin(),
         E = AuxFunctionCalls.end(); CI != E; )
    if (AuxFCallsAlive.count(&*CI))
      ++CI;
    else {
      // Copy and merge global nodes and dead aux call nodes into the
      // GlobalsGraph, and all nodes reachable from those nodes.  Update their
      // target pointers using the GGCloner.
      //
      if (!(Flags & DSGraph::RemoveUnreachableGlobals))
        GlobalsGraph->AuxFunctionCalls.push_back(DSCallSite(*CI, GGCloner));

      AuxFunctionCalls.erase(CI++);
    }

  // We are finally done with the GGCloner so we can destroy it.
  GGCloner.destroy();

  // At this point, any nodes which are visited, but not alive, are nodes
  // which can be removed.  Loop over all nodes, eliminating completely
  // unreachable nodes.
  //
  std::vector<DSNode*> DeadNodes;
  DeadNodes.reserve(Nodes.size());
  for (NodeListTy::iterator NI = Nodes.begin(), E = Nodes.end(); NI != E;) {
    DSNode *N = NI++;
    assert(!N->isForwarding() && "Forwarded node in nodes list?");

    if (!Alive.count(N)) {
      Nodes.remove(N);
      assert(!N->isForwarding() && "Cannot remove a forwarding node!");
      DeadNodes.push_back(N);
      N->dropAllReferences();
      ++NumDNE;
    }
  }

  // Remove all unreachable globals from the ScalarMap.
  // If flag RemoveUnreachableGlobals is set, GlobalNodes has only dead nodes.
  // In either case, the dead nodes will not be in the set Alive.
  for (unsigned i = 0, e = GlobalNodes.size(); i != e; ++i)
    if (!Alive.count(GlobalNodes[i].second))
      ScalarMap.erase(GlobalNodes[i].first);
    else
      assert((Flags & DSGraph::RemoveUnreachableGlobals) && "non-dead global");

  // Delete all dead nodes now since their referrer counts are zero.
  for (unsigned i = 0, e = DeadNodes.size(); i != e; ++i)
    delete DeadNodes[i];

  DEBUG(AssertGraphOK(); GlobalsGraph->AssertGraphOK());
}

void DSGraph::AssertNodeContainsGlobal(const DSNode *N, const GlobalValue *GV) const {
  assert(std::find(N->globals_begin(),N->globals_end(), GV) !=
         N->globals_end() && "Global value not in node!");
}

void DSGraph::AssertCallSiteInGraph(const DSCallSite &CS) const {
  if (CS.isIndirectCall()) {
    AssertNodeInGraph(CS.getCalleeNode());
#if 0
    if (CS.getNumPtrArgs() && CS.getCalleeNode() == CS.getPtrArg(0).getNode() &&
        CS.getCalleeNode() && CS.getCalleeNode()->getGlobals().empty())
      DEBUG(errs() << "WARNING: WEIRD CALL SITE FOUND!\n");
#endif
  }
  AssertNodeInGraph(CS.getRetVal().getNode());
  for (unsigned j = 0, e = CS.getNumPtrArgs(); j != e; ++j)
    AssertNodeInGraph(CS.getPtrArg(j).getNode());
}

void DSGraph::AssertCallNodesInGraph() const {
  for (fc_iterator I = fc_begin(), E = fc_end(); I != E; ++I)
    AssertCallSiteInGraph(*I);
}
void DSGraph::AssertAuxCallNodesInGraph() const {
  for (afc_const_iterator I = afc_begin(), E = afc_end(); I != E; ++I)
    AssertCallSiteInGraph(*I);
}

void DSGraph::AssertGraphOK() const {
  for (node_const_iterator NI = node_begin(), E = node_end(); NI != E; ++NI)
    NI->assertOK();

  for (ScalarMapTy::const_iterator I = ScalarMap.begin(),
         E = ScalarMap.end(); I != E; ++I) {
    assert(!I->second.isNull() && "Null node in scalarmap!");
    AssertNodeInGraph(I->second.getNode());
    if (const GlobalValue *GV = dyn_cast<GlobalValue>(I->first)) {
      assert(I->second.getNode()->isGlobalNode() &&
             "Global points to node, but node isn't global?");
      AssertNodeContainsGlobal(I->second.getNode(), GV);
    }
  }
  AssertCallNodesInGraph();
  AssertAuxCallNodesInGraph();

  // Check that all pointer arguments to any functions in this graph have
  // destinations.
  for (ReturnNodesTy::const_iterator RI = ReturnNodes.begin(),
         E = ReturnNodes.end();
       RI != E; ++RI) {
    const Function &F = *RI->first;
    for (Function::const_arg_iterator AI = F.arg_begin(); AI != F.arg_end(); ++AI)
      if (isa<PointerType>(AI->getType()))
        assert(!getNodeForValue(AI).isNull() &&
               "Pointer argument must be in the scalar map!");
  }
}

/// computeNodeMapping - Given roots in two different DSGraphs, traverse the
/// nodes reachable from the two graphs, computing the mapping of nodes from the
/// first to the second graph.  This mapping may be many-to-one (i.e. the first
/// graph may have multiple nodes representing one node in the second graph),
/// but it will not work if there is a one-to-many or many-to-many mapping.
///
void DSGraph::computeNodeMapping(const DSNodeHandle &NH1,
                                 const DSNodeHandle &NH2, NodeMapTy &NodeMap,
                                 bool StrictChecking) {
  DSNode *N1 = NH1.getNode(), *N2 = NH2.getNode();
  if (N1 == 0 || N2 == 0) return;

  DSNodeHandle &Entry = NodeMap[N1];
  if (!Entry.isNull()) {
    // Termination of recursion!
    if (StrictChecking) {
      assert(Entry.getNode() == N2 && "Inconsistent mapping detected!");
      assert((Entry.getOffset() == (NH2.getOffset()-NH1.getOffset()) ||
              Entry.getNode()->isNodeCompletelyFolded()) &&
             "Inconsistent mapping detected!");
    }
    return;
  }

  Entry.setTo(N2, NH2.getOffset()-NH1.getOffset());

  // Loop over all of the fields that N1 and N2 have in common, recursively
  // mapping the edges together now.
  int N2Idx = NH2.getOffset()-NH1.getOffset();
  unsigned N2Size = N2->getSize();
  if (N2Size == 0) return;   // No edges to map to.

  for (unsigned i = 0, e = N1->getSize(); i < e; ++i) {
    const DSNodeHandle &N1NH = N1->getLink(i);
    // Don't call N2->getLink if not needed (avoiding crash if N2Idx is not
    // aligned right).
    if (!N1NH.isNull()) {
      if (unsigned(N2Idx)+i < N2Size)
        computeNodeMapping(N1NH, N2->getLink(N2Idx+i), NodeMap);
      else
        computeNodeMapping(N1NH,
                           N2->getLink(unsigned(N2Idx+i) % N2Size), NodeMap);
    }
  }
}


/// computeGToGGMapping - Compute the mapping of nodes in the global graph to
/// nodes in this graph.
void DSGraph::computeGToGGMapping(NodeMapTy &NodeMap) {
  DSGraph &GG = *getGlobalsGraph();

  DSScalarMap &SM = getScalarMap();
  for (DSScalarMap::global_iterator I = SM.global_begin(),
         E = SM.global_end(); I != E; ++I)
    DSGraph::computeNodeMapping(SM[*I], GG.getNodeForValue(*I), NodeMap);
}

/// computeGGToGMapping - Compute the mapping of nodes in the global graph to
/// nodes in this graph.  Note that any uses of this method are probably bugs,
/// unless it is known that the globals graph has been merged into this graph!
void DSGraph::computeGGToGMapping(InvNodeMapTy &InvNodeMap) {
  NodeMapTy NodeMap;
  computeGToGGMapping(NodeMap);

  while (!NodeMap.empty()) {
    InvNodeMap.insert(std::make_pair(NodeMap.begin()->second,
                                     NodeMap.begin()->first));
    NodeMap.erase(NodeMap.begin());
  }
}


/// computeCalleeCallerMapping - Given a call from a function in the current
/// graph to the 'Callee' function (which lives in 'CalleeGraph'), compute the
/// mapping of nodes from the callee to nodes in the caller.
void DSGraph::computeCalleeCallerMapping(DSCallSite CS, const Function &Callee,
                                         DSGraph &CalleeGraph,
                                         NodeMapTy &NodeMap) {

  DSCallSite CalleeArgs =
    CalleeGraph.getCallSiteForArguments(const_cast<Function&>(Callee));

  computeNodeMapping(CalleeArgs.getRetVal(), CS.getRetVal(), NodeMap);

  unsigned NumArgs = CS.getNumPtrArgs();
  if (NumArgs > CalleeArgs.getNumPtrArgs())
    NumArgs = CalleeArgs.getNumPtrArgs();

  for (unsigned i = 0; i != NumArgs; ++i)
    computeNodeMapping(CalleeArgs.getPtrArg(i), CS.getPtrArg(i), NodeMap);

  // Map the nodes that are pointed to by globals.
  DSScalarMap &CalleeSM = CalleeGraph.getScalarMap();
  DSScalarMap &CallerSM = getScalarMap();

  if (CalleeSM.global_size() >= CallerSM.global_size()) {
    for (DSScalarMap::global_iterator GI = CallerSM.global_begin(),
           E = CallerSM.global_end(); GI != E; ++GI)
      if (CalleeSM.global_count(*GI))
        computeNodeMapping(CalleeSM[*GI], CallerSM[*GI], NodeMap);
  } else {
    for (DSScalarMap::global_iterator GI = CalleeSM.global_begin(),
           E = CalleeSM.global_end(); GI != E; ++GI)
      if (CallerSM.global_count(*GI))
        computeNodeMapping(CalleeSM[*GI], CallerSM[*GI], NodeMap);
  }
}

/// updateFromGlobalGraph - This function rematerializes global nodes and
/// nodes reachable from them from the globals graph into the current graph.
///
void DSGraph::updateFromGlobalGraph() {
  ReachabilityCloner RC(this, GlobalsGraph, 0);

  // Clone the non-up-to-date global nodes into this graph.
  for (DSScalarMap::global_iterator I = getScalarMap().global_begin(),
         E = getScalarMap().global_end(); I != E; ++I) {
    DSScalarMap::iterator It = GlobalsGraph->ScalarMap.find(*I);
    if (It != GlobalsGraph->ScalarMap.end())
      RC.merge(getNodeForValue(*I), It->second);
  }
}


