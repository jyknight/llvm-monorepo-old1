//===-- PointerCompress.cpp - Pointer Compression Pass --------------------===//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the -pointercompress pass.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "pointercompress"
#include "EquivClassGraphs.h"
#include "PoolAllocate.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstVisitor.h"
using namespace llvm;

/// UINTTYPE - This is the actual type we are compressing to.  This is really
/// only capable of being UIntTy, except when we are doing tests for 16-bit
/// integers, when it's UShortTy.
static const Type *UINTTYPE;

namespace {
  cl::opt<bool>
  SmallIntCompress("compress-to-16-bits",
                   cl::desc("Pointer compress data structures to 16 bit "
                            "integers instead of 32-bit integers"));

  Statistic<> NumCompressed("pointercompress",
                            "Number of pools pointer compressed");
  Statistic<> NumNotCompressed("pointercompress",
                               "Number of pools not compressible");

  class CompressedPoolInfo;

  /// PointerCompress - This transformation hacks on type-safe pool allocated
  /// data structures to reduce the size of pointers in the program.
  class PointerCompress : public ModulePass {
    PoolAllocate *PoolAlloc;
    PA::EquivClassGraphs *ECG;

    /// ClonedFunctionMap - Every time we clone a function to compress its
    /// arguments, keep track of the clone and which arguments are compressed.
    std::map<std::pair<Function*, std::vector<unsigned> >,
             Function *> ClonedFunctionMap;
  public:
    Function *PoolInitPC, *PoolDestroyPC, *PoolAllocPC, *PoolFreePC;
    typedef std::map<const DSNode*, CompressedPoolInfo> PoolInfoMap;

    bool runOnModule(Module &M);

    void getAnalysisUsage(AnalysisUsage &AU) const;

    Function *GetFunctionClone(Function *F, 
                               const std::vector<unsigned> &OpsToCompress);

  private:
    void InitializePoolLibraryFunctions(Module &M);
    bool CompressPoolsInFunction(Function &F);
    void FindPoolsToCompress(std::vector<const DSNode*> &Pools, Function &F,
                             DSGraph &DSG, PA::FuncInfo *FI);
  };

  RegisterOpt<PointerCompress>
  X("pointercompress", "Compress type-safe data structures");
}

//===----------------------------------------------------------------------===//
//               CompressedPoolInfo Class and Implementation
//===----------------------------------------------------------------------===//

namespace {
  /// CompressedPoolInfo - An instance of this structure is created for each
  /// pool that is compressed.
  class CompressedPoolInfo {
    const DSNode *Pool;
    Value *PoolDesc;
    const Type *NewTy;
    unsigned NewSize;
  public:
    CompressedPoolInfo(const DSNode *N, Value *PD)
      : Pool(N), PoolDesc(PD), NewTy(0) {}
    
    /// Initialize - When we know all of the pools in a function that are going
    /// to be compressed, initialize our state based on that data.
    void Initialize(std::map<const DSNode*, CompressedPoolInfo> &Nodes,
                    const TargetData &TD);

    const DSNode *getNode() const { return Pool; }
    const Type *getNewType() const { return NewTy; }

    /// getNewSize - Return the size of each node after compression.
    ///
    unsigned getNewSize() const { return NewSize; }
    
    /// getPoolDesc - Return the Value* for the pool descriptor for this pool.
    ///
    Value *getPoolDesc() const { return PoolDesc; }

    // dump - Emit a debugging dump of this pool info.
    void dump() const;

  private:
    const Type *ComputeCompressedType(const Type *OrigTy, unsigned NodeOffset,
                           std::map<const DSNode*, CompressedPoolInfo> &Nodes);
  };
}

/// Initialize - When we know all of the pools in a function that are going
/// to be compressed, initialize our state based on that data.
void CompressedPoolInfo::Initialize(std::map<const DSNode*, 
                                             CompressedPoolInfo> &Nodes,
                                    const TargetData &TD) {
  // First step, compute the type of the compressed node.  This basically
  // replaces all pointers to compressed pools with uints.
  NewTy = ComputeCompressedType(Pool->getType(), 0, Nodes);

  // Get the compressed type size.
  NewSize = TD.getTypeSize(NewTy);
}


/// ComputeCompressedType - Recursively compute the new type for this node after
/// pointer compression.  This involves compressing any pointers that point into
/// compressed pools.
const Type *CompressedPoolInfo::
ComputeCompressedType(const Type *OrigTy, unsigned NodeOffset,
                      std::map<const DSNode*, CompressedPoolInfo> &Nodes) {
  if (const PointerType *PTY = dyn_cast<PointerType>(OrigTy)) {
    // FIXME: check to see if this pointer is actually compressed!
    return UINTTYPE;
  } else if (OrigTy->isFirstClassType())
    return OrigTy;

  // Okay, we have an aggregate type.
  if (const StructType *STy = dyn_cast<StructType>(OrigTy)) {
    std::vector<const Type*> Elements;
    Elements.reserve(STy->getNumElements());
    for (unsigned i = 0, e = STy->getNumElements(); i != e; ++i)
      Elements.push_back(ComputeCompressedType(STy->getElementType(i),
                                               NodeOffset, Nodes));
    return StructType::get(Elements);
  } else {
    assert(0 && "FIXME: Unhandled aggregate type!");
  }
}

/// dump - Emit a debugging dump for this pool info.
///
void CompressedPoolInfo::dump() const {
  std::cerr << "Node: "; getNode()->dump();
  std::cerr << "New Type: " << *NewTy << "\n";
}


//===----------------------------------------------------------------------===//
//                    PointerCompress Implementation
//===----------------------------------------------------------------------===//

void PointerCompress::getAnalysisUsage(AnalysisUsage &AU) const {
  // Need information about how pool allocation happened.
  AU.addRequired<PoolAllocatePassAllPools>();

  // Need information from DSA.
  AU.addRequired<PA::EquivClassGraphs>();
}

/// PoolIsCompressible - Return true if we can pointer compress this node.
/// If not, we should DEBUG print out why.
static bool PoolIsCompressible(const DSNode *N, Function &F) {
  assert(!N->isForwarding() && "Should not be dealing with merged nodes!");
  if (N->isNodeCompletelyFolded()) {
    DEBUG(std::cerr << "Node is not type-safe:\n");
    return false;
  }

  // If this has no pointer fields, don't compress.
  bool HasFields = false;
  for (DSNode::const_edge_iterator I = N->edge_begin(), E = N->edge_end();
       I != E; ++I)
    if (!I->isNull()) {
      HasFields = true;
      if (I->getNode() != N) {
        // We currently only handle trivially self cyclic DS's right now.
        DEBUG(std::cerr << "Node points to nodes other than itself:\n");
        return false;
      }        
    }

  if (!HasFields) {
    DEBUG(std::cerr << "Node does not contain any pointers to compress:\n");
    return false;
  }

  if (N->isArray()) {
    DEBUG(std::cerr << "Node is an array (not yet handled!):\n");
    return false;
  }

  if ((N->getNodeFlags() & DSNode::Composition) != DSNode::HeapNode) {
    DEBUG(std::cerr << "Node contains non-heap values:\n");
    return false;
  }

  return true;
}

/// FindPoolsToCompress - Inspect the specified function and find pools that are
/// compressible that are homed in that function.  Return those pools in the
/// Pools vector.
void PointerCompress::FindPoolsToCompress(std::vector<const DSNode*> &Pools,
                                          Function &F, DSGraph &DSG,
                                          PA::FuncInfo *FI) {
  DEBUG(std::cerr << "In function '" << F.getName() << "':\n");
  for (unsigned i = 0, e = FI->NodesToPA.size(); i != e; ++i) {
    const DSNode *N = FI->NodesToPA[i];

    if (PoolIsCompressible(N, F)) {
      Pools.push_back(N);
      ++NumCompressed;
    } else {
      DEBUG(std::cerr << "PCF: "; N->dump());
      ++NumNotCompressed;
    }
  }
}


namespace {
  /// InstructionRewriter - This class implements the rewriting neccesary to
  /// transform a function body from normal pool allocation to pointer
  /// compression.  It is constructed, then the 'visit' method is called on a
  /// function.  If is responsible for rewriting all instructions that refer to
  /// pointers into compressed pools.
  class InstructionRewriter : public llvm::InstVisitor<InstructionRewriter> {
    /// OldToNewValueMap - This keeps track of what new instructions we create
    /// for instructions that used to produce pointers into our pool.
    std::map<Value*, Value*> OldToNewValueMap;
  
    const PointerCompress::PoolInfoMap &PoolInfo;

    const TargetData &TD;
    const DSGraph &DSG;

    PointerCompress &PtrComp;
  public:
    InstructionRewriter(const PointerCompress::PoolInfoMap &poolInfo,
                        const DSGraph &dsg, PointerCompress &ptrcomp)
      : PoolInfo(poolInfo), TD(dsg.getTargetData()), DSG(dsg), PtrComp(ptrcomp){
    }

    ~InstructionRewriter();

    /// getTransformedValue - Return the transformed version of the specified
    /// value, creating a new forward ref value as needed.
    Value *getTransformedValue(Value *V) {
      if (isa<ConstantPointerNull>(V))                // null -> uint 0
        return Constant::getNullValue(UINTTYPE);

      assert(getNodeIfCompressed(V) && "Value is not compressed!");
      Value *&RV = OldToNewValueMap[V];
      if (RV) return RV;

      RV = new Argument(UINTTYPE);
      return RV;
    }

    /// setTransformedValue - When we create a new value, this method sets it as
    /// the current value.
    void setTransformedValue(Instruction &Old, Value *New) {
      Value *&EV = OldToNewValueMap[&Old];
      if (EV) {
        assert(isa<Argument>(EV) && "Not a forward reference!");
        EV->replaceAllUsesWith(New);
        delete EV;
      }
      EV = New;
    }

    /// getNodeIfCompressed - If the specified value is a pointer that will be
    /// compressed, return the DSNode corresponding to the pool it belongs to.
    const DSNode *getNodeIfCompressed(Value *V) {
      if (!isa<PointerType>(V->getType()) || isa<ConstantPointerNull>(V) ||
          isa<Function>(V))
        return false;
      DSNode *N = DSG.getNodeForValue(V).getNode();
      return PoolInfo.count(N) ? N : 0;
    }

    /// getPoolInfo - Return the pool info for the specified compressed pool.
    ///
    const CompressedPoolInfo &getPoolInfo(const DSNode *N) {
      assert(N && "Pool not compressed!");
      PointerCompress::PoolInfoMap::const_iterator I = PoolInfo.find(N);
      assert(I != PoolInfo.end() && "Pool is not compressed!");
      return I->second;
    }

    /// getPoolInfo - Return the pool info object for the specified value if the
    /// pointer points into a compressed pool, otherwise return null.
    const CompressedPoolInfo *getPoolInfo(Value *V) {
      if (const DSNode *N = getNodeIfCompressed(V))
        return &getPoolInfo(N);
      return 0;
    }

    /// getPoolInfoForPoolDesc - Given a pool descriptor as a Value*, return the
    /// pool info for the pool if it is compressed.
    const CompressedPoolInfo *getPoolInfoForPoolDesc(Value *PD) const {
      for (PointerCompress::PoolInfoMap::const_iterator I = PoolInfo.begin(),
             E = PoolInfo.end(); I != E; ++I)
        if (I->second.getPoolDesc() == PD)
          return &I->second;
      return 0;
    }

    //===------------------------------------------------------------------===//
    // Visitation methods.  These do all of the heavy lifting for the various
    // cases we have to handle.

    void visitCastInst(CastInst &CI);
    void visitPHINode(PHINode &PN);
    void visitSetCondInst(SetCondInst &SCI);
    void visitGetElementPtrInst(GetElementPtrInst &GEPI);
    void visitLoadInst(LoadInst &LI);
    void visitStoreInst(StoreInst &SI);

    void visitCallInst(CallInst &CI);
    void visitPoolInit(CallInst &CI);
    void visitPoolDestroy(CallInst &CI);

    void visitInstruction(Instruction &I) {
#ifndef NDEBUG
      bool Unhandled = !!getNodeIfCompressed(&I);
      for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i)
        Unhandled |= !!getNodeIfCompressed(I.getOperand(i));

      if (Unhandled) {
        std::cerr << "ERROR: UNHANDLED INSTRUCTION: " << I;
        //assert(0);
        //abort();
      }
#endif
    }
  };
} // end anonymous namespace.


InstructionRewriter::~InstructionRewriter() {
  // Nuke all of the old values from the program.
  for (std::map<Value*, Value*>::iterator I = OldToNewValueMap.begin(),
         E = OldToNewValueMap.end(); I != E; ++I) {
    assert((!isa<Argument>(I->second) || cast<Argument>(I->second)->getParent())
           && "ERROR: Unresolved value still left in the program!");
    // If there is anything still using this, provide a temporary value.
    if (!I->first->use_empty())
      I->first->replaceAllUsesWith(UndefValue::get(I->first->getType()));

    // Finally, remove it from the program.
    cast<Instruction>(I->first)->eraseFromParent();
  }
}


void InstructionRewriter::visitCastInst(CastInst &CI) {
  if (!isa<PointerType>(CI.getType())) return;

  const CompressedPoolInfo *PI = getPoolInfo(&CI);
  if (!PI) return;
  assert(getPoolInfo(CI.getOperand(0)) == PI && "Not cast from ptr -> ptr?");

  // A cast from one pointer to another turns into a cast from uint -> uint,
  // which is a noop.
  setTransformedValue(CI, getTransformedValue(CI.getOperand(0)));
}

void InstructionRewriter::visitPHINode(PHINode &PN) {
  const CompressedPoolInfo *DestPI = getPoolInfo(&PN);
  if (DestPI == 0) return;

  PHINode *New = new PHINode(UINTTYPE, PN.getName(), &PN);
  New->reserveOperandSpace(PN.getNumIncomingValues());

  for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i)
    New->addIncoming(getTransformedValue(PN.getIncomingValue(i)),
                     PN.getIncomingBlock(i));
  setTransformedValue(PN, New);
}

void InstructionRewriter::visitSetCondInst(SetCondInst &SCI) {
  if (!isa<PointerType>(SCI.getOperand(0)->getType())) return;
  Value *NonNullPtr = SCI.getOperand(0);
  if (isa<ConstantPointerNull>(NonNullPtr)) {
    NonNullPtr = SCI.getOperand(1);
    if (isa<ConstantPointerNull>(NonNullPtr))
      return;  // setcc null, null
  }

  const CompressedPoolInfo *SrcPI = getPoolInfo(NonNullPtr);
  if (SrcPI == 0) return;   // comparing non-compressed pointers.
 
  std::string Name = SCI.getName(); SCI.setName("");
  Value *New = new SetCondInst(SCI.getOpcode(),
                               getTransformedValue(SCI.getOperand(0)),
                               getTransformedValue(SCI.getOperand(1)),
                               Name, &SCI);
  SCI.replaceAllUsesWith(New);
  SCI.eraseFromParent();
}

void InstructionRewriter::visitGetElementPtrInst(GetElementPtrInst &GEPI) {
  const CompressedPoolInfo *PI = getPoolInfo(&GEPI);
  if (PI == 0) return;

  // For now, we only support very very simple getelementptr instructions, with
  // two indices, where the first is zero.
  assert(GEPI.getNumOperands() == 3 && isa<Constant>(GEPI.getOperand(1)) &&
         cast<Constant>(GEPI.getOperand(1))->isNullValue());
  const Type *IdxTy = 
    cast<PointerType>(GEPI.getOperand(0)->getType())->getElementType();
  assert(isa<StructType>(IdxTy) && "Can only handle structs right now!");

  Value *Val = getTransformedValue(GEPI.getOperand(0));

  unsigned Field = (unsigned)cast<ConstantUInt>(GEPI.getOperand(2))->getValue();
  if (Field) {
    const StructType *NTy = cast<StructType>(PI->getNewType());
    uint64_t FieldOffs = TD.getStructLayout(NTy)->MemberOffsets[Field];
    Constant *FieldOffsCst = ConstantUInt::get(UINTTYPE, FieldOffs);
    Val = BinaryOperator::createAdd(Val, FieldOffsCst, GEPI.getName(), &GEPI);
  }

  setTransformedValue(GEPI, Val);
}

void InstructionRewriter::visitLoadInst(LoadInst &LI) {
  if (isa<ConstantPointerNull>(LI.getOperand(0))) return; // load null ??

  const CompressedPoolInfo *SrcPI = getPoolInfo(LI.getOperand(0));
  if (SrcPI == 0) {
    assert(getPoolInfo(&LI) == 0 &&
           "Cannot load a compressed pointer from non-compressed memory!");
    return;
  }

  // We care about two cases, here:
  //  1. Loading a normal value from a ptr compressed data structure.
  //  2. Loading a compressed ptr from a ptr compressed data structure.
  bool LoadingCompressedPtr = getNodeIfCompressed(&LI) != 0;
  
  // Get the pool base pointer.
  Constant *Zero = Constant::getNullValue(Type::UIntTy);
  Value *BasePtrPtr = new GetElementPtrInst(SrcPI->getPoolDesc(), Zero, Zero,
                                            "poolbaseptrptr", &LI);
  Value *BasePtr = new LoadInst(BasePtrPtr, "poolbaseptr", &LI);

  // Get the pointer to load from.
  std::vector<Value*> Ops;
  Ops.push_back(getTransformedValue(LI.getOperand(0)));
  if (Ops[0]->getType() == Type::UShortTy)
    Ops[0] = new CastInst(Ops[0], Type::UIntTy, "extend_idx", &LI);
  Value *SrcPtr = new GetElementPtrInst(BasePtr, Ops,
                                        LI.getOperand(0)->getName()+".pp", &LI);
  const Type *DestTy = LoadingCompressedPtr ? UINTTYPE : LI.getType();
  SrcPtr = new CastInst(SrcPtr, PointerType::get(DestTy),
                        SrcPtr->getName(), &LI);
  std::string OldName = LI.getName(); LI.setName("");
  Value *NewLoad = new LoadInst(SrcPtr, OldName, &LI);

  if (LoadingCompressedPtr) {
    setTransformedValue(LI, NewLoad);
  } else {
    LI.replaceAllUsesWith(NewLoad);
    LI.eraseFromParent();
  }
}



void InstructionRewriter::visitStoreInst(StoreInst &SI) {
  const CompressedPoolInfo *DestPI = getPoolInfo(SI.getOperand(1));
  if (DestPI == 0) {
    assert(getPoolInfo(SI.getOperand(0)) == 0 &&
           "Cannot store a compressed pointer into non-compressed memory!");
    return;
  }

  // We care about two cases, here:
  //  1. Storing a normal value into a ptr compressed data structure.
  //  2. Storing a compressed ptr into a ptr compressed data structure.  Note
  //     that we cannot use the src value to decide if this is a compressed
  //     pointer if it's a null pointer.  We have to try harder.
  //
  Value *SrcVal = SI.getOperand(0);
  if (!isa<ConstantPointerNull>(SrcVal)) {
    const CompressedPoolInfo *SrcPI = getPoolInfo(SrcVal);
    if (SrcPI)     // If the stored value is compressed, get the xformed version
      SrcVal = getTransformedValue(SrcVal);
  } else {
    // FIXME: This assumes that all pointers are compressed!
    SrcVal = getTransformedValue(SrcVal);
  }
  
  // Get the pool base pointer.
  Constant *Zero = Constant::getNullValue(Type::UIntTy);
  Value *BasePtrPtr = new GetElementPtrInst(DestPI->getPoolDesc(), Zero, Zero,
                                            "poolbaseptrptr", &SI);
  Value *BasePtr = new LoadInst(BasePtrPtr, "poolbaseptr", &SI);

  // Get the pointer to store to.
  std::vector<Value*> Ops;
  Ops.push_back(getTransformedValue(SI.getOperand(1)));
  if (Ops[0]->getType() == Type::UShortTy)
    Ops[0] = new CastInst(Ops[0], Type::UIntTy, "extend_idx", &SI);

  Value *DestPtr = new GetElementPtrInst(BasePtr, Ops,
                                         SI.getOperand(1)->getName()+".pp",
                                         &SI);
  DestPtr = new CastInst(DestPtr, PointerType::get(SrcVal->getType()),
                         DestPtr->getName(), &SI);
  new StoreInst(SrcVal, DestPtr, &SI);

  // Finally, explicitly remove the store from the program, as it does not
  // produce a pointer result.
  SI.eraseFromParent();
}


void InstructionRewriter::visitPoolInit(CallInst &CI) {
  // Transform to poolinit_pc if this is initializing a pool that we are
  // compressing.
  const CompressedPoolInfo *PI = getPoolInfoForPoolDesc(CI.getOperand(1));
  if (PI == 0) return;  // Pool isn't compressed.

  std::vector<Value*> Ops;
  Ops.push_back(CI.getOperand(1));
  // Transform to pass in the orig and compressed sizes.
  Ops.push_back(CI.getOperand(2));
  Ops.push_back(ConstantUInt::get(Type::UIntTy, PI->getNewSize()));
  Ops.push_back(CI.getOperand(3));
  // TODO: Compression could reduce the alignment restriction for the pool!
  new CallInst(PtrComp.PoolInitPC, Ops, "", &CI);
  CI.eraseFromParent();
}

void InstructionRewriter::visitPoolDestroy(CallInst &CI) {
  // Transform to pooldestroy_pc if this is destroying a pool that we are
  // compressing.
  const CompressedPoolInfo *PI = getPoolInfoForPoolDesc(CI.getOperand(1));
  if (PI == 0) return;  // Pool isn't compressed.

  std::vector<Value*> Ops;
  Ops.push_back(CI.getOperand(1));
  new CallInst(PtrComp.PoolDestroyPC, Ops, "", &CI);
  CI.eraseFromParent();
}

void InstructionRewriter::visitCallInst(CallInst &CI) {
  if (Function *F = CI.getCalledFunction())
    // These functions are handled specially.
    if (F->getName() == "poolinit") {
      visitPoolInit(CI);
      return;
    } else if (F->getName() == "pooldestroy") {
      visitPoolDestroy(CI);
      return;
    }
  
  // Normal function call: check to see if this call produces or uses a pointer
  // into a compressed pool.  If so, we will need to transform the callee or use
  // a previously transformed version.  For these purposes, we treat the return
  // value as "operand #0".
  std::vector<unsigned> OpsToCompress;

  // Do we need to compress the return value?
  if (isa<PointerType>(CI.getType()) && getNodeIfCompressed(&CI))
    OpsToCompress.push_back(0);

  // Check to see if we need to compress any arguments.
  for (unsigned i = 1, e = CI.getNumOperands(); i != e; ++i)
    if (isa<PointerType>(CI.getOperand(i)->getType()) &&
        getNodeIfCompressed(CI.getOperand(i)))
      OpsToCompress.push_back(i);

  // If this function doesn't require compression, there is nothing to do!
  if (OpsToCompress.empty()) return;
  Function *Callee = CI.getCalledFunction();
  assert(Callee && "Indirect calls not implemented yet!");
  
  // Get the clone of this function that uses compressed pointers instead of
  // normal pointers.
  Function *Clone = PtrComp.GetFunctionClone(Callee, OpsToCompress);

  // Okay, we now have our clone: rewrite the call instruction.
  std::vector<Value*> Operands;
  Operands.reserve(CI.getNumOperands()-1);
  for (unsigned i = 1, e = CI.getNumOperands(); i != e; ++i)
    if (isa<PointerType>(CI.getOperand(i)->getType()) &&
        getNodeIfCompressed(CI.getOperand(i)))
      Operands.push_back(getTransformedValue(CI.getOperand(i)));
    else
      Operands.push_back(CI.getOperand(i));
  Value *NC = new CallInst(Clone, Operands, CI.getName(), &CI);
  if (OpsToCompress[0] == 0)      // Compressing return value?
    setTransformedValue(CI, NC);
  else {
    if (!CI.use_empty())
      CI.replaceAllUsesWith(NC);
    CI.eraseFromParent();
  }
}


/// CompressPoolsInFunction - Find all pools that are compressible in this
/// function and compress them.
bool PointerCompress::CompressPoolsInFunction(Function &F) {
  if (F.isExternal()) return false;

  PA::FuncInfo *FI = PoolAlloc->getFuncInfoOrClone(F);
  if (FI == 0) {
    std::cerr << "DIDN'T FIND POOL INFO FOR: "
              << *F.getType() << F.getName() << "!\n";
    return false;
  }

  // If this function was cloned, and this is the original function, ignore it
  // (it's dead).  We'll deal with the cloned version later when we run into it
  // again.
  if (FI->Clone && &FI->F == &F)
    return false;

  // There are no pools in this function.
  if (FI->NodesToPA.empty())
    return false;

  // Get the DSGraph for this function.
  DSGraph &DSG = ECG->getDSGraph(FI->F);

  // Compute the set of compressible pools in this function.
  std::vector<const DSNode*> PoolsToCompressList;
  FindPoolsToCompress(PoolsToCompressList, F, DSG, FI);

  if (PoolsToCompressList.empty()) return false;

  // Compute the initial collection of compressed pointer infos.
  std::map<const DSNode*, CompressedPoolInfo> PoolsToCompress;
  for (unsigned i = 0, e = PoolsToCompressList.size(); i != e; ++i) {
    const DSNode *N = PoolsToCompressList[i];
    Value *PD = FI->PoolDescriptors[N];
    assert(PD && "No pool descriptor available for this pool???");
    PoolsToCompress.insert(std::make_pair(N, CompressedPoolInfo(N, PD)));
  }

  // Use these to compute the closure of compression information.  In
  // particular, if one pool points to another, we need to know if the outgoing
  // pointer is compressed.
  const TargetData &TD = DSG.getTargetData();
  std::cerr << "In function '" << F.getName() << "':\n";
  for (std::map<const DSNode*, CompressedPoolInfo>::iterator
         I = PoolsToCompress.begin(), E = PoolsToCompress.end(); I != E; ++I) {
    I->second.Initialize(PoolsToCompress, TD);
    std::cerr << "  COMPRESSING POOL:\nPCS:";
    I->second.dump();
  }
  
  // Finally, rewrite the function body to use compressed pointers!
  InstructionRewriter(PoolsToCompress, DSG, *this).visit(F);
  return true;
}


/// GetFunctionClone - Lazily create clones of pool allocated functions that we
/// need in compressed form.  This memoizes the functions that have been cloned
/// to allow only one clone of each function in a desired permutation.
Function *PointerCompress::
GetFunctionClone(Function *F, const std::vector<unsigned> &OpsToCompress) {
  assert(!OpsToCompress.empty() && "No clone needed!");

  // Check to see if we have already compressed this function, if so, there is
  // no need to make another clone.
  Function *&Clone = ClonedFunctionMap[std::make_pair(F, OpsToCompress)];
  if (Clone) return Clone;

  // First step, construct the new function prototype.
  const FunctionType *FTy = F->getFunctionType();
  const Type *RetTy = FTy->getReturnType();
  unsigned OTCIdx = 0;
  if (OpsToCompress[0] == 0) {
    RetTy = UINTTYPE;
    OTCIdx++;
  }
  std::vector<const Type*> ParamTypes;
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
    if (OTCIdx != OpsToCompress.size() && OpsToCompress[OTCIdx] == i+1) {
      assert(isa<PointerType>(FTy->getParamType(i)) && "Not a pointer?");
      ParamTypes.push_back(UINTTYPE);
      ++OTCIdx;
    } else {
      ParamTypes.push_back(FTy->getParamType(i));
    }
  FunctionType *CFTy = FunctionType::get(RetTy, ParamTypes, FTy->isVarArg());

  // Next, create the clone prototype and insert it into the module.
  Clone = new Function(CFTy, GlobalValue::/*Internal*/ ExternalLinkage,
                       F->getName()+".pc");
  F->getParent()->getFunctionList().insert(F, Clone);

  return Clone;
}


/// InitializePoolLibraryFunctions - Create the function prototypes for pointer
/// compress runtime library functions.
void PointerCompress::InitializePoolLibraryFunctions(Module &M) {
  const Type *VoidPtrTy = PointerType::get(Type::SByteTy);
  const Type *PoolDescPtrTy = PointerType::get(ArrayType::get(VoidPtrTy, 16));

  PoolInitPC = M.getOrInsertFunction("poolinit_pc", Type::VoidTy,
                                     PoolDescPtrTy, Type::UIntTy,
                                     Type::UIntTy, Type::UIntTy, 0);
  PoolDestroyPC = M.getOrInsertFunction("pooldestroy_pc", Type::VoidTy,
                                        PoolDescPtrTy, 0);
  PoolAllocPC = M.getOrInsertFunction("poolalloc_pc",  UINTTYPE,
                                      PoolDescPtrTy, Type::UIntTy, 0);
  PoolFreePC = M.getOrInsertFunction("poolfree_pc",  Type::VoidTy,
                                      PoolDescPtrTy, UINTTYPE, 0);
  // FIXME: Need bumppointer versions as well as realloc??/memalign??

  // PoolAllocPC/PoolFreePC can be handled just like any other compressed
  // functions.
  std::vector<unsigned> Ops;
  Ops.push_back(0);   // poolalloc -> compress return value.
  ClonedFunctionMap[std::make_pair(PoolAlloc->PoolAlloc, Ops)] = PoolAllocPC;

  Ops[0] = 2;         // Pool free -> compress second argument.
  ClonedFunctionMap[std::make_pair(PoolAlloc->PoolFree, Ops)] = PoolFreePC;
}

bool PointerCompress::runOnModule(Module &M) {
  PoolAlloc = &getAnalysis<PoolAllocatePassAllPools>();
  ECG = &getAnalysis<PA::EquivClassGraphs>();
  
  if (SmallIntCompress)
    UINTTYPE = Type::UShortTy;
  else 
    UINTTYPE = Type::UIntTy;

  // Create the function prototypes for pointer compress runtime library
  // functions.
  InitializePoolLibraryFunctions(M);

  // Iterate over all functions in the module, looking for compressible data
  // structures.
  bool Changed = false;
  for (Module::iterator I = M.begin(), E = M.end(); I != E; ++I)
    Changed |= CompressPoolsInFunction(*I);

  ClonedFunctionMap.clear();
  return Changed;
}
