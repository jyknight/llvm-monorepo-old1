/* High-level LLVM backend interface 
Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
Contributed by Chris Lattner (sabre@nondot.org)

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 59 Temple Place - Suite 330, Boston, MA
02111-1307, USA.  */

// LLVM headers
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/StandardPasses.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/System/Program.h"

// System headers
#include <cassert>
#include <gmp.h>

// GCC headers
#undef VISIBILITY_HIDDEN

extern "C" {
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "tree.h"

#include "cgraph.h"
#include "diagnostic.h"
#include "except.h"
#include "flags.h"
#include "function.h"
#include "gcc-plugin.h"
#include "intl.h"
#include "langhooks.h"
#include "output.h"
#include "params.h"
#include "plugin-version.h"
#include "toplev.h"
#include "tree-inline.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "version.h"
}

// Plugin headers
#include "llvm-internal.h"
#include "llvm-debug.h"
#include "llvm-target.h"
#include "bits_and_bobs.h"

// Non-zero if bytecode from PCH is successfully read.
int flag_llvm_pch_read;

// Non-zero if libcalls should not be simplified.
int flag_no_simplify_libcalls;

// Non-zero if red-zone is disabled.
static int flag_disable_red_zone = 0;

// Non-zero if implicit floating point instructions are disabled.
static int flag_no_implicit_float = 0;

/// llvm_asm_file_name - Name of file to use for assembly code output.
static const char *llvm_asm_file_name;

// Global state for the LLVM backend.
Module *TheModule = 0;
DebugInfo *TheDebugInfo = 0;
TargetMachine *TheTarget = 0;
TargetFolder *TheFolder = 0;
TypeConverter *TheTypeConverter = 0;
llvm::raw_ostream *OutStream = 0; // Stream to write assembly code to.
llvm::formatted_raw_ostream FormattedOutStream;

static bool DisableLLVMOptimizations = false;//TODO
static bool emit_llvm = false;//TODO
static bool emit_llvm_bc = false;//TODO
static int flag_inline_trees = 2;//TODO

std::vector<std::pair<Constant*, int> > StaticCtors, StaticDtors;
SmallSetVector<Constant*, 32> AttributeUsedGlobals;
SmallSetVector<Constant*, 32> AttributeCompilerUsedGlobals;
std::vector<Constant*> AttributeAnnotateGlobals;

/// PerFunctionPasses - This is the list of cleanup passes run per-function
/// as each is compiled.  In cases where we are not doing IPO, it includes the 
/// code generator.
static FunctionPassManager *PerFunctionPasses = 0;
static PassManager *PerModulePasses = 0;
static FunctionPassManager *CodeGenPasses = 0;

static void createPerFunctionOptimizationPasses();
static void createPerModuleOptimizationPasses();
//TODOstatic void destroyOptimizationPasses();

//TODO//===----------------------------------------------------------------------===//
//TODO//                   Matching LLVM Values with GCC DECL trees
//TODO//===----------------------------------------------------------------------===//
//TODO//
//TODO// LLVMValues is a vector of LLVM Values. GCC tree nodes keep track of LLVM
//TODO// Values using this vector's index. It is easier to save and restore the index
//TODO// than the LLVM Value pointer while using PCH.
//TODO
//TODO// Collection of LLVM Values
//TODOstatic std::vector<Value *> LLVMValues;
//TODOtypedef DenseMap<Value *, unsigned> LLVMValuesMapTy;
//TODOstatic LLVMValuesMapTy LLVMValuesMap;
//TODO
//TODO/// LocalLLVMValueIDs - This is the set of local IDs we have in our mapping,
//TODO/// this allows us to efficiently identify and remove them.  Local IDs are IDs
//TODO/// for values that are local to the current function being processed.  These do
//TODO/// not need to go into the PCH file, but DECL_LLVM still needs a valid index
//TODO/// while converting the function.  Using "Local IDs" allows the IDs for
//TODO/// function-local decls to be recycled after the function is done.
//TODOstatic std::vector<unsigned> LocalLLVMValueIDs;
//TODO
//TODO/// llvm_set_decl - Remember the LLVM value for GCC tree node.
//TODOvoid llvm_set_decl(tree Tr, Value *V) {
//TODO
//TODO  // If there is not any value then do not add new LLVMValues entry.
//TODO  // However clear Tr index if it is non zero.
//TODO  if (!V) {
//TODO    if (GET_DECL_LLVM_INDEX(Tr))
//TODO      SET_DECL_LLVM_INDEX(Tr, 0);
//TODO    return;
//TODO  }
//TODO
//TODO  unsigned &ValueSlot = LLVMValuesMap[V];
//TODO  if (ValueSlot) {
//TODO    // Already in map
//TODO    SET_DECL_LLVM_INDEX(Tr, ValueSlot);
//TODO    return;
//TODO  }
//TODO
//TODO  LLVMValues.push_back(V);
//TODO  unsigned Index = LLVMValues.size();
//TODO  SET_DECL_LLVM_INDEX(Tr, Index);
//TODO  LLVMValuesMap[V] = Index;
//TODO
//TODO  // Remember local values.
//TODO  if (!isa<Constant>(V))
//TODO    LocalLLVMValueIDs.push_back(Index);
//TODO}
//TODO
//TODO/// llvm_set_decl_p - Return TRUE if there is a LLVM Value associate with GCC
//TODO/// tree node.
//TODObool llvm_set_decl_p(tree Tr) {
//TODO  unsigned Index = GET_DECL_LLVM_INDEX(Tr);
//TODO  if (Index == 0)
//TODO    return false;
//TODO
//TODO  return LLVMValues[Index - 1] != 0;
//TODO}
//TODO
//TODO/// llvm_get_decl - Get LLVM Value for the GCC tree node based on LLVMValues
//TODO/// vector index.  If there is not any value associated then use
//TODO/// make_decl_llvm() to make LLVM value. When GCC tree node is initialized, it
//TODO/// has 0 as the index value. This is why all recorded indices are offset by 1.
//TODOValue *llvm_get_decl(tree Tr) {
//TODO
//TODO  unsigned Index = GET_DECL_LLVM_INDEX(Tr);
//TODO  if (Index == 0) {
//TODO    make_decl_llvm(Tr);
//TODO    Index = GET_DECL_LLVM_INDEX(Tr);
//TODO
//TODO    // If there was an error, we may have disabled creating LLVM values.
//TODO    if (Index == 0) return 0;
//TODO  }
//TODO  assert((Index - 1) < LLVMValues.size() && "Invalid LLVM value index");
//TODO  assert(LLVMValues[Index - 1] && "Trying to use deleted LLVM value!");
//TODO
//TODO  return LLVMValues[Index - 1];
//TODO}
//TODO
//TODO/// changeLLVMConstant - Replace Old with New everywhere, updating all maps
//TODO/// (except for AttributeAnnotateGlobals, which is a different kind of animal).
//TODO/// At this point we know that New is not in any of these maps.
//TODOvoid changeLLVMConstant(Constant *Old, Constant *New) {
//TODO  assert(Old->use_empty() && "Old value has uses!");
//TODO
//TODO  if (AttributeUsedGlobals.count(Old)) {
//TODO    AttributeUsedGlobals.remove(Old);
//TODO    AttributeUsedGlobals.insert(New);
//TODO  }
//TODO
//TODO  if (AttributeCompilerUsedGlobals.count(Old)) {
//TODO    AttributeCompilerUsedGlobals.remove(Old);
//TODO    AttributeCompilerUsedGlobals.insert(New);
//TODO  }
//TODO
//TODO  for (unsigned i = 0, e = StaticCtors.size(); i != e; ++i) {
//TODO    if (StaticCtors[i].first == Old)
//TODO      StaticCtors[i].first = New;
//TODO  }
//TODO
//TODO  for (unsigned i = 0, e = StaticDtors.size(); i != e; ++i) {
//TODO    if (StaticDtors[i].first == Old)
//TODO      StaticDtors[i].first = New;
//TODO  }
//TODO
//TODO  assert(!LLVMValuesMap.count(New) && "New cannot be in the LLVMValues map!");
//TODO
//TODO  // Find Old in the table.
//TODO  LLVMValuesMapTy::iterator I = LLVMValuesMap.find(Old);
//TODO  if (I == LLVMValuesMap.end()) return;
//TODO
//TODO  unsigned Idx = I->second-1;
//TODO  assert(Idx < LLVMValues.size() && "Out of range index!");
//TODO  assert(LLVMValues[Idx] == Old && "Inconsistent LLVMValues mapping!");
//TODO
//TODO  LLVMValues[Idx] = New;
//TODO
//TODO  // Remove the old value from the value map.
//TODO  LLVMValuesMap.erase(I);
//TODO
//TODO  // Insert the new value into the value map.  We know that it can't already
//TODO  // exist in the mapping.
//TODO  if (New)
//TODO    LLVMValuesMap[New] = Idx+1;
//TODO}
//TODO
//TODO/// readLLVMValues - Read LLVM Types string table
//TODOvoid readLLVMValues() {
//TODO  GlobalValue *V = TheModule->getNamedGlobal("llvm.pch.values");
//TODO  if (!V)
//TODO    return;
//TODO
//TODO  GlobalVariable *GV = cast<GlobalVariable>(V);
//TODO  ConstantStruct *ValuesFromPCH = cast<ConstantStruct>(GV->getOperand(0));
//TODO
//TODO  for (unsigned i = 0; i < ValuesFromPCH->getNumOperands(); ++i) {
//TODO    Value *Va = ValuesFromPCH->getOperand(i);
//TODO
//TODO    if (!Va) {
//TODO      // If V is empty then insert NULL to represent empty entries.
//TODO      LLVMValues.push_back(Va);
//TODO      continue;
//TODO    }
//TODO    if (ConstantArray *CA = dyn_cast<ConstantArray>(Va)) {
//TODO      std::string Str = CA->getAsString();
//TODO      Va = TheModule->getValueSymbolTable().lookup(Str);
//TODO    }
//TODO    assert (Va != NULL && "Invalid Value in LLVMValues string table");
//TODO    LLVMValues.push_back(Va);
//TODO  }
//TODO
//TODO  // Now, llvm.pch.values is not required so remove it from the symbol table.
//TODO  GV->eraseFromParent();
//TODO}
//TODO
//TODO/// writeLLVMValues - GCC tree's uses LLVMValues vector's index to reach LLVM
//TODO/// Values.  Create a string table to hold these LLVM Values' names. This string
//TODO/// table will be used to recreate LTypes vector after loading PCH.
//TODOvoid writeLLVMValues() {
//TODO  if (LLVMValues.empty())
//TODO    return;
//TODO
//TODO  LLVMContext &Context = getGlobalContext();
//TODO
//TODO  std::vector<Constant *> ValuesForPCH;
//TODO  for (std::vector<Value *>::iterator I = LLVMValues.begin(),
//TODO         E = LLVMValues.end(); I != E; ++I)  {
//TODO    if (Constant *C = dyn_cast_or_null<Constant>(*I))
//TODO      ValuesForPCH.push_back(C);
//TODO    else
//TODO      // Non constant values, e.g. arguments, are not at global scope.
//TODO      // When PCH is read, only global scope values are used.
//TODO      ValuesForPCH.push_back(Constant::getNullValue(Type::getInt32Ty(Context)));
//TODO  }
//TODO
//TODO  // Create string table.
//TODO  Constant *LLVMValuesTable = ConstantStruct::get(Context, ValuesForPCH, false);
//TODO
//TODO  // Create variable to hold this string table.
//TODO  new GlobalVariable(*TheModule, LLVMValuesTable->getType(), true,
//TODO                     GlobalValue::ExternalLinkage,
//TODO                     LLVMValuesTable,
//TODO                     "llvm.pch.values");
//TODO}
//TODO
//TODO/// eraseLocalLLVMValues - drop all non-global values from the LLVM values map.
//TODOvoid eraseLocalLLVMValues() {
//TODO  // Erase all the local values, these are stored in LocalLLVMValueIDs.
//TODO  while (!LocalLLVMValueIDs.empty()) {
//TODO    unsigned Idx = LocalLLVMValueIDs.back()-1;
//TODO    LocalLLVMValueIDs.pop_back();
//TODO
//TODO    if (Value *V = LLVMValues[Idx]) {
//TODO      assert(!isa<Constant>(V) && "Found global value");
//TODO      LLVMValuesMap.erase(V);
//TODO    }
//TODO
//TODO    if (Idx == LLVMValues.size()-1)
//TODO      LLVMValues.pop_back();
//TODO    else
//TODO      LLVMValues[Idx] = 0;
//TODO  }
//TODO}

/// handleVisibility - Forward decl visibility style to global.
void handleVisibility(tree decl, GlobalValue *GV) {
  // If decl has visibility specified explicitely (via attribute) - honour
  // it. Otherwise (e.g. visibility specified via -fvisibility=hidden) honour
  // only if symbol is local.
  if (TREE_PUBLIC(decl) &&
      (DECL_VISIBILITY_SPECIFIED(decl) || !DECL_EXTERNAL(decl))) {
    if (DECL_VISIBILITY(decl) == VISIBILITY_HIDDEN)
      GV->setVisibility(GlobalValue::HiddenVisibility);
    else if (DECL_VISIBILITY(decl) == VISIBILITY_PROTECTED)
      GV->setVisibility(GlobalValue::ProtectedVisibility);
    else if (DECL_VISIBILITY(decl) == VISIBILITY_DEFAULT)
      GV->setVisibility(Function::DefaultVisibility);
  }
}

//TODO// GuessAtInliningThreshold - Figure out a reasonable threshold to pass llvm's
//TODO// inliner.  There are 12 user-settable gcc params that affect inlining.  llvm
//TODO// (so far) only has one knob; the param that corresponds most closely, and
//TODO// which we use, is max-inline-insns-auto (set by -finline-limit, which is
//TODO// what most users actually use).  This maps only very approximately to what
//TODO// llvm's inliner is doing, but it's the best we've got.
//TODOstatic unsigned GuessAtInliningThreshold() {
//TODO  unsigned threshold = 200;
//TODO  // Get the default value for gcc's max-inline-insns-auto.  This is the value
//TODO  // after all language and target dependent changes to the global default are
//TODO  // applied, but before parsing the command line.
//TODO  unsigned default_miia = default_max_inline_insns_auto;
//TODO  // See if the actual value is the same as the default.
//TODO  unsigned miia = MAX_INLINE_INSNS_AUTO;
//TODO  if (miia == default_miia) {
//TODO    if (optimize_size || optimize < 3)
//TODO      // Reduce inline limit.
//TODO      threshold = 50;
//TODO  } else {
//TODO    // We have an overriding user-specified value.  Multiply by 20/9, which is
//TODO    // the Magic Number converting 90 to 200.
//TODO    threshold = miia * 20 / 9;
//TODO  }
//TODO  return threshold;
//TODO}

#ifndef LLVM_TARGET_NAME
#error LLVM_TARGET_NAME macro not specified by GCC backend
#endif

namespace llvm {
#define Declare2(TARG, MOD)   extern "C" void LLVMInitialize ## TARG ## MOD()
#define Declare(T, M) Declare2(T, M)
  Declare(LLVM_TARGET_NAME, TargetInfo);
  Declare(LLVM_TARGET_NAME, Target);
  Declare(LLVM_TARGET_NAME, AsmPrinter);
#undef Declare
#undef Declare2
}

/// LazilyConfigureLLVM - Set LLVM configuration options, if not already set.
/// already created.
static void LazilyConfigureLLVM(void) {
  static bool Configured = false;
  if (Configured)
    return;

  // Initialize the LLVM backend.
#define DoInit2(TARG, MOD)  LLVMInitialize ## TARG ## MOD()
#define DoInit(T, M) DoInit2(T, M)
  DoInit(LLVM_TARGET_NAME, TargetInfo);
  DoInit(LLVM_TARGET_NAME, Target);
  DoInit(LLVM_TARGET_NAME, AsmPrinter);
#undef DoInit
#undef DoInit2

  // Initialize LLVM command line options.
  std::vector<const char*> Args;
  Args.push_back(progname); // program name

//TODO  // Allow targets to specify PIC options and other stuff to the corresponding
//TODO  // LLVM backends.
//TODO#ifdef LLVM_SET_RED_ZONE_FLAG
//TODO  LLVM_SET_RED_ZONE_FLAG(flag_disable_red_zone)
//TODO#endif
//TODO#ifdef LLVM_SET_TARGET_OPTIONS
//TODO  LLVM_SET_TARGET_OPTIONS(Args);
//TODO#endif
//TODO#ifdef LLVM_SET_MACHINE_OPTIONS
//TODO  LLVM_SET_MACHINE_OPTIONS(Args);
//TODO#endif
//TODO#ifdef LLVM_SET_IMPLICIT_FLOAT
//TODO  LLVM_SET_IMPLICIT_FLOAT(flag_no_implicit_float)
//TODO#endif

  if (time_report)
    Args.push_back("--time-passes");
  if (fast_math_flags_set_p())
    Args.push_back("--enable-unsafe-fp-math");
  if (!flag_omit_frame_pointer)
    Args.push_back("--disable-fp-elim");
  if (!flag_zero_initialized_in_bss)
    Args.push_back("--nozero-initialized-in-bss");
  if (flag_debug_asm)
    Args.push_back("--asm-verbose");
//TODO  if (flag_debug_pass_structure)
//TODO    Args.push_back("--debug-pass=Structure");
//TODO  if (flag_debug_pass_arguments)
//TODO    Args.push_back("--debug-pass=Arguments");
  if (flag_unwind_tables)
    Args.push_back("--unwind-tables");

//TODO  // If there are options that should be passed through to the LLVM backend
//TODO  // directly from the command line, do so now.  This is mainly for debugging
//TODO  // purposes, and shouldn't really be for general use.
//TODO  std::vector<std::string> ArgStrings;
//TODO
//TODO  if (flag_inline_trees > 1) {
//TODO    unsigned threshold = GuessAtInliningThreshold();
//TODO    std::string Arg("--inline-threshold="+utostr(threshold));
//TODO    ArgStrings.push_back(Arg);
//TODO  }
//TODO
//TODO  if (flag_limited_precision > 0) {
//TODO    std::string Arg("--limit-float-precision="+utostr(flag_limited_precision));
//TODO    ArgStrings.push_back(Arg);
//TODO  }
//TODO
//TODO  if (flag_stack_protect > 0) {
//TODO    std::string Arg("--stack-protector-buffer-size=" +
//TODO                    utostr(PARAM_VALUE(PARAM_SSP_BUFFER_SIZE)));
//TODO    ArgStrings.push_back(Arg);
//TODO  }
//TODO
//TODO  for (unsigned i = 0, e = ArgStrings.size(); i != e; ++i)
//TODO    Args.push_back(ArgStrings[i].c_str());
//TODO
//TODO  std::vector<std::string> LLVM_Optns; // Avoid deallocation before opts parsed!
//TODO  if (llvm_optns) {
//TODO    SplitString(llvm_optns, LLVM_Optns);
//TODO    for(unsigned i = 0, e = LLVM_Optns.size(); i != e; ++i)
//TODO      Args.push_back(LLVM_Optns[i].c_str());
//TODO  }

  Args.push_back(0);  // Null terminator.
  int pseudo_argc = Args.size()-1;
  llvm::cl::ParseCommandLineOptions(pseudo_argc, (char**)&Args[0]);

  Configured = true;
}

/// LazilyInitializeModule - Create a module to output LLVM IR to, if it wasn't
/// already created.
static void LazilyInitializeModule(void) {
  static bool Initialized = false;
  if (Initialized)
    return;

  LazilyConfigureLLVM();

  TheModule = new Module("", getGlobalContext());

  if (main_input_filename)
    TheModule->setModuleIdentifier(main_input_filename);

  // Insert a special .ident directive to identify the version of the plugin
  // which compiled this code.  The format of the .ident string is patterned
  // after the ones produced by GCC.
#ifdef IDENT_ASM_OP
  if (!flag_no_ident) {
    const char *pkg_version = "(GNU) ";

    if (strcmp ("(GCC) ", pkgversion_string))
      pkg_version = pkgversion_string;

    std::string IdentString = IDENT_ASM_OP;
    IdentString += "\"GCC: ";
    IdentString += pkg_version;
    IdentString += version_string;
    IdentString += "; LLVM: ";
    IdentString += REVISION;
    IdentString += '"';
    TheModule->setModuleInlineAsm(IdentString);
  }
#endif

  // If the target wants to override the architecture, e.g. turning
  // powerpc-darwin-... into powerpc64-darwin-... when -m64 is enabled, do so
  // now.
  std::string TargetTriple = TARGET_NAME;
#ifdef LLVM_OVERRIDE_TARGET_ARCH
  std::string Arch = LLVM_OVERRIDE_TARGET_ARCH();
  if (!Arch.empty()) {
    std::string::size_type DashPos = TargetTriple.find('-');
    if (DashPos != std::string::npos)// If we have a sane t-t, replace the arch.
      TargetTriple = Arch + TargetTriple.substr(DashPos);
  }
#endif
#ifdef LLVM_OVERRIDE_TARGET_VERSION
  char *NewTriple;
  bool OverRidden = LLVM_OVERRIDE_TARGET_VERSION(TargetTriple.c_str(),
                                                 &NewTriple);
  if (OverRidden)
    TargetTriple = std::string(NewTriple);
#endif
  TheModule->setTargetTriple(TargetTriple);

  TheTypeConverter = new TypeConverter();

  // Create the TargetMachine we will be generating code with.
  // FIXME: Figure out how to select the target and pass down subtarget info.
  std::string Err;
  const Target *TME =
    TargetRegistry::lookupTarget(TargetTriple, Err);
  if (!TME)
    llvm_report_error(Err);

  // Figure out the subtarget feature string we pass to the target.
  std::string FeatureStr;
//TODO  // The target can set LLVM_SET_SUBTARGET_FEATURES to configure the LLVM
//TODO  // backend.
//TODO#ifdef LLVM_SET_SUBTARGET_FEATURES
//TODO  SubtargetFeatures Features;
//TODO  LLVM_SET_SUBTARGET_FEATURES(Features);
//TODO  FeatureStr = Features.getString();
//TODO#endif
  TheTarget = TME->createTargetMachine(TargetTriple, FeatureStr);
  assert(TheTarget->getTargetData()->isBigEndian() == BYTES_BIG_ENDIAN);

  TheFolder = new TargetFolder(TheTarget->getTargetData(), getGlobalContext());

  // Install information about target datalayout stuff into the module for
  // optimizer use.
  TheModule->setDataLayout(TheTarget->getTargetData()->
                           getStringRepresentation());

//TODO  if (optimize)
//TODO    RegisterRegAlloc::setDefault(createLinearScanRegisterAllocator);
//TODO  else
//TODO    RegisterRegAlloc::setDefault(createLocalRegisterAllocator);

//TODO  // FIXME - Do not disable debug info while writing pch.
//TODO  if (!flag_pch_file &&
//TODO      debug_info_level > DINFO_LEVEL_NONE)
//TODO    TheDebugInfo = new DebugInfo(TheModule);
//TODO  if (TheDebugInfo)
//TODO    TheDebugInfo->Initialize();
//TODO}
//TODO
//TODO/// performLateBackendInitialization - Set backend options that may only be
//TODO/// known at codegen time.
//TODOvoid performLateBackendInitialization(void) {
//TODO  // The Ada front-end sets flag_exceptions only after processing the file.
//TODO  if (USING_SJLJ_EXCEPTIONS)
//TODO    SjLjExceptionHandling = flag_exceptions;
//TODO  else
//TODO    DwarfExceptionHandling = flag_exceptions;
//TODO  for (Module::iterator I = TheModule->begin(), E = TheModule->end();
//TODO       I != E; ++I)
//TODO    if (!I->isDeclaration()) {
//TODO      if (flag_disable_red_zone)
//TODO        I->addFnAttr(Attribute::NoRedZone);
//TODO      if (flag_no_implicit_float)
//TODO        I->addFnAttr(Attribute::NoImplicitFloat);
//TODO    }
//TODO}
  Initialized = true;
}

/// InitializeOutputStreams - Initialize the assembly code output streams.
static void InitializeOutputStreams(bool Binary) {
  assert(!OutStream && "Output stream already initialized!");
  std::string Error;
  unsigned Flags = raw_fd_ostream::F_Force;

  if (Binary)
    Flags |= raw_fd_ostream::F_Binary;

  OutStream = new raw_fd_ostream(llvm_asm_file_name, Error, Flags);

  if (!Error.empty())
    llvm_report_error(Error);

  FormattedOutStream.setStream(*OutStream,
                               formatted_raw_ostream::PRESERVE_STREAM);
}

//TODOoFILEstream *AsmIntermediateOutStream = 0;
//TODO
//TODO/// llvm_pch_read - Read bytecode from PCH file. Initialize TheModule and setup
//TODO/// LTypes vector.
//TODOvoid llvm_pch_read(const unsigned char *Buffer, unsigned Size) {
//TODO  std::string ModuleName = TheModule->getModuleIdentifier();
//TODO
//TODO  delete TheModule;
//TODO  delete TheDebugInfo;
//TODO
//TODO  clearTargetBuiltinCache();
//TODO
//TODO  MemoryBuffer *MB = MemoryBuffer::getNewMemBuffer(Size, ModuleName.c_str());
//TODO  memcpy((char*)MB->getBufferStart(), Buffer, Size);
//TODO
//TODO  std::string ErrMsg;
//TODO  TheModule = ParseBitcodeFile(MB, getGlobalContext(), &ErrMsg);
//TODO  delete MB;
//TODO
//TODO  // FIXME - Do not disable debug info while writing pch.
//TODO  if (!flag_pch_file && debug_info_level > DINFO_LEVEL_NONE) {
//TODO    TheDebugInfo = new DebugInfo(TheModule);
//TODO    TheDebugInfo->Initialize();
//TODO  }
//TODO
//TODO  if (!TheModule) {
//TODO    errs() << "Error reading bytecodes from PCH file\n";
//TODO    errs() << ErrMsg << "\n";
//TODO    exit(1);
//TODO  }
//TODO
//TODO  if (PerFunctionPasses || PerModulePasses) {
//TODO    destroyOptimizationPasses();
//TODO
//TODO    // Don't run codegen, when we should output PCH
//TODO    if (flag_pch_file)
//TODO      llvm_pch_write_init();
//TODO  }
//TODO
//TODO  // Read LLVM Types string table
//TODO  readLLVMTypesStringTable();
//TODO  readLLVMValues();
//TODO
//TODO  flag_llvm_pch_read = 1;
//TODO}
//TODO
//TODO/// llvm_pch_write_init - Initialize PCH writing. 
//TODOvoid llvm_pch_write_init(void) {
//TODO  timevar_push(TV_LLVM_INIT);
//TODO  AsmOutStream = new oFILEstream(asm_out_file);
//TODO  // FIXME: disentangle ostream madness here.  Kill off ostream and FILE.
//TODO  AsmOutRawStream =
//TODO    new formatted_raw_ostream(*new raw_os_ostream(*AsmOutStream),
//TODO                              formatted_raw_ostream::DELETE_STREAM);
//TODO
//TODO  PerModulePasses = new PassManager();
//TODO  PerModulePasses->add(new TargetData(*TheTarget->getTargetData()));
//TODO
//TODO  // If writing to stdout, set binary mode.
//TODO  if (asm_out_file == stdout)
//TODO    sys::Program::ChangeStdoutToBinary();
//TODO
//TODO  // Emit an LLVM .bc file to the output.  This is used when passed
//TODO  // -emit-llvm -c to the GCC driver.
//TODO  PerModulePasses->add(createBitcodeWriterPass(*AsmOutStream));
//TODO  
//TODO  // Disable emission of .ident into the output file... which is completely
//TODO  // wrong for llvm/.bc emission cases.
//TODO  flag_no_ident = 1;
//TODO
//TODO  flag_llvm_pch_read = 0;
//TODO
//TODO  timevar_pop(TV_LLVM_INIT);
//TODO}

//TODOstatic void destroyOptimizationPasses() {
//TODO  delete PerFunctionPasses;
//TODO  delete PerModulePasses;
//TODO  delete CodeGenPasses;
//TODO
//TODO  PerFunctionPasses = 0;
//TODO  PerModulePasses   = 0;
//TODO  CodeGenPasses     = 0;
//TODO}

static void createPerFunctionOptimizationPasses() {
  if (PerFunctionPasses) 
    return;

  // Create and set up the per-function pass manager.
  // FIXME: Move the code generator to be function-at-a-time.
  PerFunctionPasses =
    new FunctionPassManager(new ExistingModuleProvider(TheModule));
  PerFunctionPasses->add(new TargetData(*TheTarget->getTargetData()));

  // In -O0 if checking is disabled, we don't even have per-function passes.
  bool HasPerFunctionPasses = false;
#ifdef ENABLE_CHECKING
  PerFunctionPasses->add(createVerifierPass());
  HasPerFunctionPasses = true;
#endif

  if (optimize > 0 && !DisableLLVMOptimizations) {
    HasPerFunctionPasses = true;
    PerFunctionPasses->add(createCFGSimplificationPass());
    if (optimize == 1)
      PerFunctionPasses->add(createPromoteMemoryToRegisterPass());
    else
      PerFunctionPasses->add(createScalarReplAggregatesPass());
    PerFunctionPasses->add(createInstructionCombiningPass());
  }

  // If there are no module-level passes that have to be run, we codegen as
  // each function is parsed.
  // FIXME: We can't figure this out until we know there are no always-inline
  // functions.
  // FIXME: This is disabled right now until bugs can be worked out.  Reenable
  // this for fast -O0 compiles!
  if (!emit_llvm_bc && !emit_llvm && 0) {
    FunctionPassManager *PM = PerFunctionPasses;    
    HasPerFunctionPasses = true;

    CodeGenOpt::Level OptLevel = CodeGenOpt::Default;

    switch (optimize) {
    default: break;
    case 0: OptLevel = CodeGenOpt::None; break;
    case 3: OptLevel = CodeGenOpt::Aggressive; break;
    }

    // Normal mode, emit a .s file by running the code generator.
    // Note, this also adds codegenerator level optimization passes.
    InitializeOutputStreams(false);
    switch (TheTarget->addPassesToEmitFile(*PM, FormattedOutStream,
                                           TargetMachine::AssemblyFile,
                                           OptLevel)) {
    default:
    case FileModel::Error:
      errs() << "Error interfacing to target machine!\n";
      exit(1);
    case FileModel::AsmFile:
      break;
    }

    if (TheTarget->addPassesToEmitFileFinish(*PM, (MachineCodeEmitter *)0,
                                             OptLevel)) {
      errs() << "Error interfacing to target machine!\n";
      exit(1);
    }
  }
  
  if (HasPerFunctionPasses) {
    PerFunctionPasses->doInitialization();
  } else {
    delete PerFunctionPasses;
    PerFunctionPasses = 0;
  }
}

static void createPerModuleOptimizationPasses() {
  if (PerModulePasses)
    // llvm_pch_write_init has already created the per module passes.
    return;

  // FIXME: AT -O0/O1, we should stream out functions at a time.
  PerModulePasses = new PassManager();
  PerModulePasses->add(new TargetData(*TheTarget->getTargetData()));
  bool HasPerModulePasses = false;

  if (!DisableLLVMOptimizations) {
    bool NeedAlwaysInliner = false;
    llvm::Pass *InliningPass = 0;
    if (flag_inline_trees > 1) {                // respect -fno-inline-functions
      InliningPass = createFunctionInliningPass();    // Inline small functions
    } else {
      // If full inliner is not run, check if always-inline is needed to handle
      // functions that are  marked as always_inline.
      for (Module::iterator I = TheModule->begin(), E = TheModule->end();
           I != E; ++I)
        if (I->hasFnAttr(Attribute::AlwaysInline)) {
          NeedAlwaysInliner = true;
          break;
        }

      if (NeedAlwaysInliner)
        InliningPass = createAlwaysInlinerPass();  // Inline always_inline funcs
    }

    HasPerModulePasses = true;
    createStandardModulePasses(PerModulePasses, optimize,
                               optimize_size || optimize < 3,
                               flag_unit_at_a_time, flag_unroll_loops,
                               !flag_no_simplify_libcalls, flag_exceptions,
                               InliningPass);
  }

  if (emit_llvm_bc) {
    // Emit an LLVM .bc file to the output.  This is used when passed
    // -emit-llvm -c to the GCC driver.
    InitializeOutputStreams(true);
    PerModulePasses->add(createBitcodeWriterPass(*OutStream));
    HasPerModulePasses = true;
  } else if (emit_llvm) {
    // Emit an LLVM .ll file to the output.  This is used when passed 
    // -emit-llvm -S to the GCC driver.
    InitializeOutputStreams(false);
    PerModulePasses->add(createPrintModulePass(OutStream));
    HasPerModulePasses = true;
  } else {
    // If there are passes we have to run on the entire module, we do codegen
    // as a separate "pass" after that happens.
    // However if there are no module-level passes that have to be run, we
    // codegen as each function is parsed.
    // FIXME: This is disabled right now until bugs can be worked out.  Reenable
    // this for fast -O0 compiles!
    if (PerModulePasses || 1) {
      FunctionPassManager *PM = CodeGenPasses =
        new FunctionPassManager(new ExistingModuleProvider(TheModule));
      PM->add(new TargetData(*TheTarget->getTargetData()));

      CodeGenOpt::Level OptLevel = CodeGenOpt::Default;

      switch (optimize) {
      default: break;
      case 0: OptLevel = CodeGenOpt::None; break;
      case 3: OptLevel = CodeGenOpt::Aggressive; break;
      }

      // Normal mode, emit a .s file by running the code generator.
      // Note, this also adds codegenerator level optimization passes.
      InitializeOutputStreams(false);
      switch (TheTarget->addPassesToEmitFile(*PM, FormattedOutStream,
                                             TargetMachine::AssemblyFile,
                                             OptLevel)) {
      default:
      case FileModel::Error:
        errs() << "Error interfacing to target machine!\n";
        exit(1);
      case FileModel::AsmFile:
        break;
      }

      if (TheTarget->addPassesToEmitFileFinish(*PM, (MachineCodeEmitter *)0,
                                               OptLevel)) {
        errs() << "Error interfacing to target machine!\n";
        exit(1);
      }
    }
  }

  if (!HasPerModulePasses) {
    delete PerModulePasses;
    PerModulePasses = 0;
  }
}

//TODO/// llvm_asm_file_start - Start the .s file.
//TODOvoid llvm_asm_file_start(void) {
//TODO  timevar_push(TV_LLVM_INIT);
//TODO  AsmOutStream = new oFILEstream(asm_out_file);
//TODO  // FIXME: disentangle ostream madness here.  Kill off ostream and FILE.
//TODO  AsmOutRawStream =
//TODO    new formatted_raw_ostream(*new raw_os_ostream(*AsmOutStream),
//TODO                              formatted_raw_ostream::DELETE_STREAM);
//TODO
//TODO  flag_llvm_pch_read = 0;
//TODO
//TODO  if (emit_llvm_bc || emit_llvm)
//TODO    // Disable emission of .ident into the output file... which is completely
//TODO    // wrong for llvm/.bc emission cases.
//TODO    flag_no_ident = 1;
//TODO
//TODO  // If writing to stdout, set binary mode.
//TODO  if (asm_out_file == stdout)
//TODO    sys::Program::ChangeStdoutToBinary();
//TODO
//TODO  AttributeUsedGlobals.clear();
//TODO  AttributeCompilerUsedGlobals.clear();
//TODO  timevar_pop(TV_LLVM_INIT);
//TODO}

/// ConvertStructorsList - Convert a list of static ctors/dtors to an
/// initializer suitable for the llvm.global_[cd]tors globals.
static void CreateStructorsList(std::vector<std::pair<Constant*, int> > &Tors,
                                const char *Name) {
  std::vector<Constant*> InitList;
  std::vector<Constant*> StructInit;
  StructInit.resize(2);
  
  LLVMContext &Context = getGlobalContext();
  
  const Type *FPTy =
    FunctionType::get(Type::getVoidTy(Context),
                      std::vector<const Type*>(), false);
  FPTy = PointerType::getUnqual(FPTy);
  
  for (unsigned i = 0, e = Tors.size(); i != e; ++i) {
    StructInit[0] = ConstantInt::get(Type::getInt32Ty(Context), Tors[i].second);
    
    // __attribute__(constructor) can be on a function with any type.  Make sure
    // the pointer is void()*.
    StructInit[1] = TheFolder->CreateBitCast(Tors[i].first, FPTy);
    InitList.push_back(ConstantStruct::get(Context, StructInit, false));
  }
  Constant *Array = ConstantArray::get(
    ArrayType::get(InitList[0]->getType(), InitList.size()), InitList);
  new GlobalVariable(*TheModule, Array->getType(), false,
                     GlobalValue::AppendingLinkage,
                     Array, Name);
}

/// llvm_finish_unit - Finish the .s file.
static void llvm_finish_unit(void *gcc_data, void *user_data) {
  LazilyInitializeModule();

//TODO  timevar_push(TV_LLVM_PERFILE);
  LLVMContext &Context = getGlobalContext();

//TODO  performLateBackendInitialization();
  createPerFunctionOptimizationPasses();
//TODO
//TODO  if (flag_pch_file) {
//TODO    writeLLVMTypesStringTable();
//TODO    writeLLVMValues();
//TODO  }

  // Add an llvm.global_ctors global if needed.
  if (!StaticCtors.empty())
    CreateStructorsList(StaticCtors, "llvm.global_ctors");
  // Add an llvm.global_dtors global if needed.
  if (!StaticDtors.empty())
    CreateStructorsList(StaticDtors, "llvm.global_dtors");

  if (!AttributeUsedGlobals.empty()) {
    std::vector<Constant *> AUGs;
    const Type *SBP= PointerType::getUnqual(Type::getInt8Ty(Context));
    for (SmallSetVector<Constant *,32>::iterator
           AI = AttributeUsedGlobals.begin(),
           AE = AttributeUsedGlobals.end(); AI != AE; ++AI) {
      Constant *C = *AI;
      AUGs.push_back(TheFolder->CreateBitCast(C, SBP));
    }

    ArrayType *AT = ArrayType::get(SBP, AUGs.size());
    Constant *Init = ConstantArray::get(AT, AUGs);
    GlobalValue *gv = new GlobalVariable(*TheModule, AT, false,
                                         GlobalValue::AppendingLinkage, Init,
                                         "llvm.used");
    gv->setSection("llvm.metadata");
    AttributeUsedGlobals.clear();
  }

  if (!AttributeCompilerUsedGlobals.empty()) {
    std::vector<Constant *> ACUGs;
    const Type *SBP= PointerType::getUnqual(Type::getInt8Ty(Context));
    for (SmallSetVector<Constant *,32>::iterator
           AI = AttributeCompilerUsedGlobals.begin(),
           AE = AttributeCompilerUsedGlobals.end(); AI != AE; ++AI) {
      Constant *C = *AI;
      ACUGs.push_back(TheFolder->CreateBitCast(C, SBP));
    }

    ArrayType *AT = ArrayType::get(SBP, ACUGs.size());
    Constant *Init = ConstantArray::get(AT, ACUGs);
    GlobalValue *gv = new GlobalVariable(*TheModule, AT, false,
                                         GlobalValue::AppendingLinkage, Init,
                                         "llvm.compiler.used");
    gv->setSection("llvm.metadata");
    AttributeCompilerUsedGlobals.clear();
  }

  // Add llvm.global.annotations
  if (!AttributeAnnotateGlobals.empty()) {
    Constant *Array = ConstantArray::get(
      ArrayType::get(AttributeAnnotateGlobals[0]->getType(),
                                      AttributeAnnotateGlobals.size()),
                       AttributeAnnotateGlobals);
    GlobalValue *gv = new GlobalVariable(*TheModule, Array->getType(), false,
                                         GlobalValue::AppendingLinkage, Array,
                                         "llvm.global.annotations");
    gv->setSection("llvm.metadata");
    AttributeAnnotateGlobals.clear();
  }

  // Finish off the per-function pass.
  if (PerFunctionPasses)
    PerFunctionPasses->doFinalization();

//TODO  // Emit intermediate file before module level optimization passes are run.
//TODO  if (flag_debug_llvm_module_opt) {
//TODO    
//TODO    static PassManager *IntermediatePM = new PassManager();
//TODO    IntermediatePM->add(new TargetData(*TheTarget->getTargetData()));
//TODO
//TODO    char asm_intermediate_out_filename[MAXPATHLEN];
//TODO    strcpy(&asm_intermediate_out_filename[0], llvm_asm_file_name);
//TODO    strcat(&asm_intermediate_out_filename[0],".0");
//TODO    FILE *asm_intermediate_out_file = fopen(asm_intermediate_out_filename, "w+b");
//TODO    AsmIntermediateOutStream = new oFILEstream(asm_intermediate_out_file);
//TODO    raw_ostream *AsmIntermediateRawOutStream = 
//TODO      new raw_os_ostream(*AsmIntermediateOutStream);
//TODO    if (emit_llvm_bc)
//TODO      IntermediatePM->add(createBitcodeWriterPass(*AsmIntermediateOutStream));
//TODO    if (emit_llvm)
//TODO      IntermediatePM->add(createPrintModulePass(AsmIntermediateRawOutStream));
//TODO    IntermediatePM->run(*TheModule);
//TODO    AsmIntermediateRawOutStream->flush();
//TODO    delete AsmIntermediateRawOutStream;
//TODO    AsmIntermediateRawOutStream = 0;
//TODO    AsmIntermediateOutStream->flush();
//TODO    fflush(asm_intermediate_out_file);
//TODO    delete AsmIntermediateOutStream;
//TODO    AsmIntermediateOutStream = 0;
//TODO  }

  // Run module-level optimizers, if any are present.
  createPerModuleOptimizationPasses();
  if (PerModulePasses)
    PerModulePasses->run(*TheModule);
  
  // Run the code generator, if present.
  if (CodeGenPasses) {
    CodeGenPasses->doInitialization();
    for (Module::iterator I = TheModule->begin(), E = TheModule->end();
         I != E; ++I)
      if (!I->isDeclaration())
        CodeGenPasses->run(*I);
    CodeGenPasses->doFinalization();
  }

  FormattedOutStream.flush();
  OutStream->flush();
//TODO  delete AsmOutRawStream;
//TODO  AsmOutRawStream = 0;
//TODO  delete AsmOutStream;
//TODO  AsmOutStream = 0;
//TODO  timevar_pop(TV_LLVM_PERFILE);
}

//TODO// llvm_call_llvm_shutdown - Release LLVM global state.
//TODOvoid llvm_call_llvm_shutdown(void) {
//TODO  llvm_shutdown();
//TODO}
//TODO
//TODO/// llvm_emit_code_for_current_function - Top level interface for emitting a
//TODO/// function to the .s file.
//TODOvoid llvm_emit_code_for_current_function(tree fndecl) {
//TODO  if (cfun->nonlocal_goto_save_area)
//TODO    sorry("%Jnon-local gotos not supported by LLVM", fndecl);
//TODO
//TODO  if (errorcount || sorrycount) {
//TODO    TREE_ASM_WRITTEN(fndecl) = 1;
//TODO    return;  // Do not process broken code.
//TODO  }
//TODO  timevar_push(TV_LLVM_FUNCS);
//TODO
//TODO  // Convert the AST to raw/ugly LLVM code.
//TODO  Function *Fn;
//TODO  {
//TODO    TreeToLLVM Emitter(fndecl);
//TODO    enum symbol_visibility vis = DECL_VISIBILITY (fndecl);
//TODO
//TODO    if (vis != VISIBILITY_DEFAULT)
//TODO      // "asm_out.visibility" emits an important warning if we're using a
//TODO      // visibility that's not supported by the target.
//TODO      targetm.asm_out.visibility(fndecl, vis);
//TODO
//TODO    Fn = Emitter.EmitFunction();
//TODO  }
//TODO
//TODO#if 0
//TODO  if (dump_file) {
//TODO    fprintf (dump_file,
//TODO             "\n\n;;\n;; Full LLVM generated for this function:\n;;\n");
//TODO    Fn->dump();
//TODO  }
//TODO#endif
//TODO
//TODO  performLateBackendInitialization();
//TODO  createPerFunctionOptimizationPasses();
//TODO
//TODO  if (PerFunctionPasses)
//TODO    PerFunctionPasses->run(*Fn);
//TODO  
//TODO  // TODO: Nuke the .ll code for the function at -O[01] if we don't want to
//TODO  // inline it or something else.
//TODO  
//TODO  // There's no need to defer outputting this function any more; we
//TODO  // know we want to output it.
//TODO  DECL_DEFER_OUTPUT(fndecl) = 0;
//TODO  
//TODO  // Finally, we have written out this function!
//TODO  TREE_ASM_WRITTEN(fndecl) = 1;
//TODO  timevar_pop(TV_LLVM_FUNCS);
//TODO}

/// emit_alias_to_llvm - Given decl and target emit alias to target.
void emit_alias_to_llvm(tree decl, tree target, tree target_decl) {
  if (errorcount || sorrycount) {
    TREE_ASM_WRITTEN(decl) = 1;
    return;  // Do not process broken code.
  }
  
  LLVMContext &Context = getGlobalContext();

//TODO  timevar_push(TV_LLVM_GLOBALS);

  // Get or create LLVM global for our alias.
  GlobalValue *V = cast<GlobalValue>(DECL_LLVM(decl));
  
  GlobalValue *Aliasee = NULL;
  
  if (target_decl)
    Aliasee = cast<GlobalValue>(DECL_LLVM(target_decl));
  else {
    // This is something insane. Probably only LTHUNKs can be here
    // Try to grab decl from IDENTIFIER_NODE

    // Query SymTab for aliasee
    const char* AliaseeName = IDENTIFIER_POINTER(target);
    Aliasee =
      dyn_cast_or_null<GlobalValue>(TheModule->
                                    getValueSymbolTable().lookup(AliaseeName));

    // Last resort. Query for name set via __asm__
    if (!Aliasee) {
      std::string starred = std::string("\001") + AliaseeName;
      Aliasee =
        dyn_cast_or_null<GlobalValue>(TheModule->
                                      getValueSymbolTable().lookup(starred));
    }
    
    if (!Aliasee) {
      if (lookup_attribute ("weakref", DECL_ATTRIBUTES (decl))) {
        if (GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
          Aliasee = new GlobalVariable(*TheModule, GV->getType(),
                                       GV->isConstant(),
                                       GlobalVariable::ExternalWeakLinkage,
                                       NULL, AliaseeName);
        else if (Function *F = dyn_cast<Function>(V))
          Aliasee = Function::Create(F->getFunctionType(),
                                     Function::ExternalWeakLinkage,
                                     AliaseeName, TheModule);
        else
          assert(0 && "Unsuported global value");
      } else {
        error ("%J%qD aliased to undefined symbol %qs", decl, decl, AliaseeName);
//TODO        timevar_pop(TV_LLVM_GLOBALS);
        return;
      }
    }
  }

  GlobalValue::LinkageTypes Linkage;

  // A weak alias has TREE_PUBLIC set but not the other bits.
  if (false)//FIXME DECL_LLVM_PRIVATE(decl))
    Linkage = GlobalValue::PrivateLinkage;
  else if (false)//FIXME DECL_LLVM_LINKER_PRIVATE(decl))
    Linkage = GlobalValue::LinkerPrivateLinkage;
  else if (DECL_WEAK(decl))
    // The user may have explicitly asked for weak linkage - ignore flag_odr.
    Linkage = GlobalValue::WeakAnyLinkage;
  else if (!TREE_PUBLIC(decl))
    Linkage = GlobalValue::InternalLinkage;
  else
    Linkage = GlobalValue::ExternalLinkage;

  GlobalAlias* GA = new GlobalAlias(Aliasee->getType(), Linkage, "",
                                    Aliasee, TheModule);

  handleVisibility(decl, GA);

  if (GA->getType()->canLosslesslyBitCastTo(V->getType()))
    V->replaceAllUsesWith(ConstantExpr::getBitCast(GA, V->getType()));
  else if (!V->use_empty()) {
    error ("%J Alias %qD used with invalid type!", decl, decl);
//TODO    timevar_pop(TV_LLVM_GLOBALS);
    return;
  }
    
  changeLLVMConstant(V, GA);
  GA->takeName(V);
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    GV->eraseFromParent();
  else if (GlobalAlias *GA = dyn_cast<GlobalAlias>(V))
    GA->eraseFromParent();
  else if (Function *F = dyn_cast<Function>(V))
    F->eraseFromParent();
  else
    assert(0 && "Unsuported global value");

  TREE_ASM_WRITTEN(decl) = 1;
  
//TODO  timevar_pop(TV_LLVM_GLOBALS);
  return;
}

/// ConvertMetadataStringToGV - Convert string to global value. Use existing
/// global if possible.
Constant* ConvertMetadataStringToGV(const char *str) {
  
  Constant *Init = ConstantArray::get(getGlobalContext(), std::string(str));

  // Use cached string if it exists.
  static std::map<Constant*, GlobalVariable*> StringCSTCache;
  GlobalVariable *&Slot = StringCSTCache[Init];
  if (Slot) return Slot;
  
  // Create a new string global.
  GlobalVariable *GV = new GlobalVariable(*TheModule, Init->getType(), true,
                                          GlobalVariable::PrivateLinkage,
                                          Init, ".str");
  GV->setSection("llvm.metadata");
  Slot = GV;
  return GV;
  
}

/// AddAnnotateAttrsToGlobal - Adds decls that have a annotate attribute to a
/// vector to be emitted later.
void AddAnnotateAttrsToGlobal(GlobalValue *GV, tree decl) {
  LLVMContext &Context = getGlobalContext();
  
  // Handle annotate attribute on global.
  tree annotateAttr = lookup_attribute("annotate", DECL_ATTRIBUTES (decl));
  if (annotateAttr == 0)
    return;
  
  // Get file and line number
  Constant *lineNo = ConstantInt::get(Type::getInt32Ty(Context),
                                      DECL_SOURCE_LINE(decl));
  Constant *file = ConvertMetadataStringToGV(DECL_SOURCE_FILE(decl));
  const Type *SBP= PointerType::getUnqual(Type::getInt8Ty(Context));
  file = TheFolder->CreateBitCast(file, SBP);
 
  // There may be multiple annotate attributes. Pass return of lookup_attr 
  //  to successive lookups.
  while (annotateAttr) {
    
    // Each annotate attribute is a tree list.
    // Get value of list which is our linked list of args.
    tree args = TREE_VALUE(annotateAttr);
    
    // Each annotate attribute may have multiple args.
    // Treat each arg as if it were a separate annotate attribute.
    for (tree a = args; a; a = TREE_CHAIN(a)) {
      // Each element of the arg list is a tree list, so get value
      tree val = TREE_VALUE(a);
      
      // Assert its a string, and then get that string.
      assert(TREE_CODE(val) == STRING_CST && 
             "Annotate attribute arg should always be a string");
      Constant *strGV = TreeConstantToLLVM::EmitLV_STRING_CST(val);
      Constant *Element[4] = {
        TheFolder->CreateBitCast(GV,SBP),
        TheFolder->CreateBitCast(strGV,SBP),
        file,
        lineNo
      };
 
      AttributeAnnotateGlobals.push_back(
        ConstantStruct::get(Context, Element, 4, false));
    }
      
    // Get next annotate attribute.
    annotateAttr = TREE_CHAIN(annotateAttr);
    if (annotateAttr)
      annotateAttr = lookup_attribute("annotate", annotateAttr);
  }
}

/// reset_initializer_llvm - Change the initializer for a global variable.
void reset_initializer_llvm(tree decl) {
  // If there were earlier errors we can get here when DECL_LLVM has not
  // been set.  Don't crash.
  // We can also get here when DECL_LLVM has not been set for some object
  // referenced in the initializer.  Don't crash then either.
  if (errorcount || sorrycount)
    return;

  // Get or create the global variable now.
  GlobalVariable *GV = cast<GlobalVariable>(DECL_LLVM(decl));
  
  // Visibility may also have changed.
  handleVisibility(decl, GV);

  // Convert the initializer over.
  Constant *Init = TreeConstantToLLVM::Convert(DECL_INITIAL(decl));

  // Set the initializer.
  GV->setInitializer(Init);
}
  
/// reset_type_and_initializer_llvm - Change the type and initializer for 
/// a global variable.
void reset_type_and_initializer_llvm(tree decl) {
  // If there were earlier errors we can get here when DECL_LLVM has not
  // been set.  Don't crash.
  // We can also get here when DECL_LLVM has not been set for some object
  // referenced in the initializer.  Don't crash then either.
  LLVMContext &Context = getGlobalContext();
  
  if (errorcount || sorrycount)
    return;

  // Get or create the global variable now.
  GlobalVariable *GV = cast<GlobalVariable>(DECL_LLVM(decl));
  
  // Visibility may also have changed.
  handleVisibility(decl, GV);

  // Temporary to avoid infinite recursion (see comments emit_global_to_llvm)
  GV->setInitializer(UndefValue::get(GV->getType()->getElementType()));

  // Convert the initializer over.
  Constant *Init = TreeConstantToLLVM::Convert(DECL_INITIAL(decl));

  // If we had a forward definition that has a type that disagrees with our
  // initializer, insert a cast now.  This sort of thing occurs when we have a
  // global union, and the LLVM type followed a union initializer that is
  // different from the union element used for the type.
  if (GV->getType()->getElementType() != Init->getType()) {
    GV->removeFromParent();
    GlobalVariable *NGV = new GlobalVariable(*TheModule, Init->getType(), 
                                             GV->isConstant(),
                                             GV->getLinkage(), 0,
                                             GV->getName());
    NGV->setVisibility(GV->getVisibility());
    NGV->setSection(GV->getSection());
    NGV->setAlignment(GV->getAlignment());
    NGV->setLinkage(GV->getLinkage());
    GV->replaceAllUsesWith(TheFolder->CreateBitCast(NGV, GV->getType()));
    changeLLVMConstant(GV, NGV);
    delete GV;
    SET_DECL_LLVM(decl, NGV);
    GV = NGV;
  }

  // Set the initializer.
  GV->setInitializer(Init);
}
  
/// emit_global_to_llvm - Emit the specified VAR_DECL or aggregate CONST_DECL to
/// LLVM as a global variable.  This function implements the end of
/// assemble_variable.
void emit_global_to_llvm(tree decl) {
  if (errorcount || sorrycount) {
    TREE_ASM_WRITTEN(decl) = 1;
    return;  // Do not process broken code.
  }

  // FIXME: Support alignment on globals: DECL_ALIGN.
  // FIXME: DECL_PRESERVE_P indicates the var is marked with attribute 'used'.

  // Global register variables don't turn into LLVM GlobalVariables.
  if (TREE_CODE(decl) == VAR_DECL && DECL_REGISTER(decl))
    return;

  // If tree nodes says defer output then do not emit global yet.
  if (CODE_CONTAINS_STRUCT (TREE_CODE (decl), TS_DECL_WITH_VIS) 
      && (DECL_DEFER_OUTPUT(decl)))
      return;

  // If we encounter a forward declaration then do not emit the global yet.
  if (!TYPE_SIZE(TREE_TYPE(decl)))
    return;

  LLVMContext &Context = getGlobalContext();

//TODO  timevar_push(TV_LLVM_GLOBALS);

  // Get or create the global variable now.
  GlobalVariable *GV = cast<GlobalVariable>(DECL_LLVM(decl));
  
  // Convert the initializer over.
  Constant *Init;
  if (DECL_INITIAL(decl) == 0 || DECL_INITIAL(decl) == error_mark_node) {
    // This global should be zero initialized.  Reconvert the type in case the
    // forward def of the global and the real def differ in type (e.g. declared
    // as 'int A[]', and defined as 'int A[100]').
    Init = Constant::getNullValue(ConvertType(TREE_TYPE(decl)));
  } else {
    assert((TREE_CONSTANT(DECL_INITIAL(decl)) || 
            TREE_CODE(DECL_INITIAL(decl)) == STRING_CST) &&
           "Global initializer should be constant!");
    
    // Temporarily set an initializer for the global, so we don't infinitely
    // recurse.  If we don't do this, we can hit cases where we see "oh a global
    // with an initializer hasn't been initialized yet, call emit_global_to_llvm
    // on it".  When constructing the initializer it might refer to itself.
    // this can happen for things like void *G = &G;
    //
    GV->setInitializer(UndefValue::get(GV->getType()->getElementType()));
    Init = TreeConstantToLLVM::Convert(DECL_INITIAL(decl));
  }

  // If we had a forward definition that has a type that disagrees with our
  // initializer, insert a cast now.  This sort of thing occurs when we have a
  // global union, and the LLVM type followed a union initializer that is
  // different from the union element used for the type.
  if (GV->getType()->getElementType() != Init->getType()) {
    GV->removeFromParent();
    GlobalVariable *NGV = new GlobalVariable(*TheModule, Init->getType(),
                                             GV->isConstant(),
                                             GlobalValue::ExternalLinkage, 0,
                                             GV->getName());
    GV->replaceAllUsesWith(TheFolder->CreateBitCast(NGV, GV->getType()));
    changeLLVMConstant(GV, NGV);
    delete GV;
    SET_DECL_LLVM(decl, NGV);
    GV = NGV;
  }
 
  // Set the initializer.
  GV->setInitializer(Init);

  // Set thread local (TLS)
  if (TREE_CODE(decl) == VAR_DECL && DECL_THREAD_LOCAL_P(decl))
    GV->setThreadLocal(true);

  // Set the linkage.
  GlobalValue::LinkageTypes Linkage;

  if (CODE_CONTAINS_STRUCT (TREE_CODE (decl), TS_DECL_WITH_VIS)
      && false) {// FIXME DECL_LLVM_PRIVATE(decl)) {
    Linkage = GlobalValue::PrivateLinkage;
  } else if (CODE_CONTAINS_STRUCT (TREE_CODE (decl), TS_DECL_WITH_VIS)
             && false) {//FIXME DECL_LLVM_LINKER_PRIVATE(decl)) {
    Linkage = GlobalValue::LinkerPrivateLinkage;
  } else if (!TREE_PUBLIC(decl)) {
    Linkage = GlobalValue::InternalLinkage;
  } else if (DECL_WEAK(decl)) {
    // The user may have explicitly asked for weak linkage - ignore flag_odr.
    Linkage = GlobalValue::WeakAnyLinkage;
  } else if (DECL_ONE_ONLY(decl)) {
    Linkage = GlobalValue::getWeakLinkage(flag_odr);
  } else if (DECL_COMMON(decl) &&  // DECL_COMMON is only meaningful if no init
             (!DECL_INITIAL(decl) || DECL_INITIAL(decl) == error_mark_node)) {
    // llvm-gcc also includes DECL_VIRTUAL_P here.
    Linkage = GlobalValue::CommonLinkage;
  } else if (DECL_COMDAT(decl)) {
    Linkage = GlobalValue::getLinkOnceLinkage(flag_odr);
  } else {
    Linkage = GV->getLinkage();
  }

  // Allow loads from constants to be folded even if the constant has weak
  // linkage.  Do this by giving the constant weak_odr linkage rather than
  // weak linkage.  It is not clear whether this optimization is valid (see
  // gcc bug 36685), but mainline gcc chooses to do it, and fold may already
  // have done it, so we might as well join in with gusto.
  if (GV->isConstant()) {
    if (Linkage == GlobalValue::WeakAnyLinkage)
      Linkage = GlobalValue::WeakODRLinkage;
    else if (Linkage == GlobalValue::LinkOnceAnyLinkage)
      Linkage = GlobalValue::LinkOnceODRLinkage;
  }
  GV->setLinkage(Linkage);

#ifdef TARGET_ADJUST_LLVM_LINKAGE
  TARGET_ADJUST_LLVM_LINKAGE(GV, decl);
#endif /* TARGET_ADJUST_LLVM_LINKAGE */

  handleVisibility(decl, GV);

  // Set the section for the global.
  if (TREE_CODE(decl) == VAR_DECL) {
    if (DECL_SECTION_NAME(decl)) {
      GV->setSection(TREE_STRING_POINTER(DECL_SECTION_NAME(decl)));
#ifdef LLVM_IMPLICIT_TARGET_GLOBAL_VAR_SECTION
    } else if (const char *Section = 
                LLVM_IMPLICIT_TARGET_GLOBAL_VAR_SECTION(decl)) {
      GV->setSection(Section);
#endif
    }
    
    // Set the alignment for the global if one of the following condition is met
    // 1) DECL_ALIGN is better than the alignment as per ABI specification
    // 2) DECL_ALIGN is set by user.
    if (DECL_ALIGN(decl)) {
      unsigned TargetAlign =
        getTargetData().getABITypeAlignment(GV->getType()->getElementType());
      if (DECL_USER_ALIGN(decl) ||
          8 * TargetAlign < (unsigned)DECL_ALIGN(decl))
        GV->setAlignment(DECL_ALIGN(decl) / 8);
    }

    // Handle used decls
    if (DECL_PRESERVE_P (decl)) {
      if (false)//FIXME DECL_LLVM_LINKER_PRIVATE (decl))
        AttributeCompilerUsedGlobals.insert(GV);
      else
        AttributeUsedGlobals.insert(GV);
    }
  
    // Add annotate attributes for globals
    if (DECL_ATTRIBUTES(decl))
      AddAnnotateAttrsToGlobal(GV, decl);
  
#ifdef LLVM_IMPLICIT_TARGET_GLOBAL_VAR_SECTION
  } else if (TREE_CODE(decl) == CONST_DECL) {
    if (const char *Section = 
        LLVM_IMPLICIT_TARGET_GLOBAL_VAR_SECTION(decl)) {
      GV->setSection(Section);

      /* LLVM LOCAL - begin radar 6389998 */
#ifdef TARGET_ADJUST_CFSTRING_NAME
      TARGET_ADJUST_CFSTRING_NAME(GV, Section);
#endif
      /* LLVM LOCAL - end radar 6389998 */
    }
#endif
  }

  // No debug info for globals when optimization is on.  While this is
  // something that would be accurate and useful to a user, it currently
  // affects some optimizations that, e.g., count uses.
  if (TheDebugInfo && !optimize)
    TheDebugInfo->EmitGlobalVariable(GV, decl);

  TREE_ASM_WRITTEN(decl) = 1;
//TODO  timevar_pop(TV_LLVM_GLOBALS);
}


/// ValidateRegisterVariable - Check that a static "asm" variable is
/// well-formed.  If not, emit error messages and return true.  If so, return
/// false.
bool ValidateRegisterVariable(tree decl) {
  LLVMContext &Context = getGlobalContext();
  int RegNumber = decode_reg_name(extractRegisterName(decl));
  const Type *Ty = ConvertType(TREE_TYPE(decl));

  if (errorcount || sorrycount)
    return true;  // Do not process broken code.

  /* Detect errors in declaring global registers.  */
  if (RegNumber == -1)
    error("%Jregister name not specified for %qD", decl, decl);
  else if (RegNumber < 0)
    error("%Jinvalid register name for %qD", decl, decl);
  else if (TYPE_MODE(TREE_TYPE(decl)) == BLKmode)
    error("%Jdata type of %qD isn%'t suitable for a register", decl, decl);
#if 0 // FIXME: enable this.
  else if (!HARD_REGNO_MODE_OK(RegNumber, TYPE_MODE(TREE_TYPE(decl))))
    error("%Jregister specified for %qD isn%'t suitable for data type",
          decl, decl);
#endif
  else if (DECL_INITIAL(decl) != 0 && TREE_STATIC(decl))
    error("global register variable has initial value");
  else if (!Ty->isSingleValueType())
    sorry("%JLLVM cannot handle register variable %qD, report a bug",
          decl, decl);
  else {
    if (TREE_THIS_VOLATILE(decl))
      warning(0, "volatile register variables don%'t work as you might wish");

    return false;  // Everything ok.
  }

  return true;
}


/// make_decl_llvm - Create the DECL_RTL for a VAR_DECL or FUNCTION_DECL.  DECL
/// should have static storage duration.  In other words, it should not be an
/// automatic variable, including PARM_DECLs.
///
/// There is, however, one exception: this function handles variables explicitly
/// placed in a particular register by the user.
///
/// This function corresponds to make_decl_rtl in varasm.c, and is implicitly
/// called by DECL_LLVM if a decl doesn't have an LLVM set.
void make_decl_llvm(tree decl) {
#ifdef ENABLE_CHECKING
  // Check that we are not being given an automatic variable.
  // A weak alias has TREE_PUBLIC set but not the other bits.
  if (TREE_CODE(decl) == PARM_DECL || TREE_CODE(decl) == RESULT_DECL
      || (TREE_CODE(decl) == VAR_DECL && !TREE_STATIC(decl) &&
          !TREE_PUBLIC(decl) && !DECL_EXTERNAL(decl) && !DECL_REGISTER(decl)))
    abort();
  // And that we were not given a type or a label.  */
  else if (TREE_CODE(decl) == TYPE_DECL || TREE_CODE(decl) == LABEL_DECL)
    abort ();
#endif

  LLVMContext &Context = getGlobalContext();
  
  // For a duplicate declaration, we can be called twice on the
  // same DECL node.  Don't discard the LLVM already made.
  if (DECL_LLVM_SET_P(decl)) return;

  if (errorcount || sorrycount)
    return;  // Do not process broken code.
  
  
  // Global register variable with asm name, e.g.:
  // register unsigned long esp __asm__("ebp");
  if (TREE_CODE(decl) != FUNCTION_DECL && DECL_REGISTER(decl)) {
    // This  just verifies that the variable is ok.  The actual "load/store"
    // code paths handle accesses to the variable.
    ValidateRegisterVariable(decl);
    return;
  }
  
//TODO  timevar_push(TV_LLVM_GLOBALS);

  const char *Name = "";
  if (DECL_NAME(decl))
    if (tree AssemblerName = DECL_ASSEMBLER_NAME(decl))
      Name = IDENTIFIER_POINTER(AssemblerName);
  
  // Now handle ordinary static variables and functions (in memory).
  // Also handle vars declared register invalidly.
  if (Name[0] == 1) {
#ifdef REGISTER_PREFIX
    if (strlen (REGISTER_PREFIX) != 0) {
      int reg_number = decode_reg_name(Name);
      if (reg_number >= 0 || reg_number == -3)
        error("%Jregister name given for non-register variable %qD",
              decl, decl);
    }
#endif
  }
  
  // Specifying a section attribute on a variable forces it into a
  // non-.bss section, and thus it cannot be common.
  if (TREE_CODE(decl) == VAR_DECL && DECL_SECTION_NAME(decl) != NULL_TREE &&
      DECL_INITIAL(decl) == NULL_TREE && DECL_COMMON(decl))
    DECL_COMMON(decl) = 0;
  
  // Variables can't be both common and weak.
  if (TREE_CODE(decl) == VAR_DECL && DECL_WEAK(decl))
    DECL_COMMON(decl) = 0;
  
  // Okay, now we need to create an LLVM global variable or function for this
  // object.  Note that this is quite possibly a forward reference to the
  // object, so its type may change later.
  if (TREE_CODE(decl) == FUNCTION_DECL) {
    assert(Name[0] && "Function with empty name!");
    // If this function has already been created, reuse the decl.  This happens
    // when we have something like __builtin_memset and memset in the same file.
    Function *FnEntry = TheModule->getFunction(Name);
    if (FnEntry == 0) {
      CallingConv::ID CC;
      AttrListPtr PAL;
      const FunctionType *Ty = 
        TheTypeConverter->ConvertFunctionType(TREE_TYPE(decl), decl, NULL,
                                              CC, PAL);
      FnEntry = Function::Create(Ty, Function::ExternalLinkage, Name, TheModule);
      FnEntry->setCallingConv(CC);
      FnEntry->setAttributes(PAL);

      // Check for external weak linkage.
      if (DECL_EXTERNAL(decl) && DECL_WEAK(decl))
        FnEntry->setLinkage(Function::ExternalWeakLinkage);

#ifdef TARGET_ADJUST_LLVM_LINKAGE
      TARGET_ADJUST_LLVM_LINKAGE(FnEntry,decl);
#endif /* TARGET_ADJUST_LLVM_LINKAGE */

      handleVisibility(decl, FnEntry);

      // If FnEntry got renamed, then there is already an object with this name
      // in the symbol table.  If this happens, the old one must be a forward
      // decl, just replace it with a cast of the new one.
      if (FnEntry->getName() != Name) {
        GlobalVariable *G = TheModule->getGlobalVariable(Name, true);
        assert(G && G->isDeclaration() && "A global turned into a function?");

        // Replace any uses of "G" with uses of FnEntry.
        Constant *GInNewType = TheFolder->CreateBitCast(FnEntry, G->getType());
        G->replaceAllUsesWith(GInNewType);

        // Update the decl that points to G.
        changeLLVMConstant(G, GInNewType);

        // Now we can give GV the proper name.
        FnEntry->takeName(G);
        
        // G is now dead, nuke it.
        G->eraseFromParent();
      }
    }
    SET_DECL_LLVM(decl, FnEntry);
  } else {
    assert((TREE_CODE(decl) == VAR_DECL ||
            TREE_CODE(decl) == CONST_DECL) && "Not a function or var decl?");
    const Type *Ty = ConvertType(TREE_TYPE(decl));
    GlobalVariable *GV ;

    // If we have "extern void foo", make the global have type {} instead of
    // type void.
    if (Ty == Type::getVoidTy(Context))
      Ty = StructType::get(Context);

    if (Name[0] == 0) {   // Global has no name.
      GV = new GlobalVariable(*TheModule, Ty, false, 
                              GlobalValue::ExternalLinkage, 0, "");

      // Check for external weak linkage.
      if (DECL_EXTERNAL(decl) && DECL_WEAK(decl))
        GV->setLinkage(GlobalValue::ExternalWeakLinkage);

#ifdef TARGET_ADJUST_LLVM_LINKAGE
      TARGET_ADJUST_LLVM_LINKAGE(GV,decl);
#endif /* TARGET_ADJUST_LLVM_LINKAGE */

      handleVisibility(decl, GV);
    } else {
      // If the global has a name, prevent multiple vars with the same name from
      // being created.
      GlobalVariable *GVE = TheModule->getGlobalVariable(Name, true);
    
      if (GVE == 0) {
        GV = new GlobalVariable(*TheModule, Ty, false,
                                GlobalValue::ExternalLinkage, 0, Name);

        // Check for external weak linkage.
        if (DECL_EXTERNAL(decl) && DECL_WEAK(decl))
          GV->setLinkage(GlobalValue::ExternalWeakLinkage);

#ifdef TARGET_ADJUST_LLVM_LINKAGE
        TARGET_ADJUST_LLVM_LINKAGE(GV,decl);
#endif /* TARGET_ADJUST_LLVM_LINKAGE */

	handleVisibility(decl, GV);

        // If GV got renamed, then there is already an object with this name in
        // the symbol table.  If this happens, the old one must be a forward
        // decl, just replace it with a cast of the new one.
        if (GV->getName() != Name) {
          Function *F = TheModule->getFunction(Name);
          assert(F && F->isDeclaration() && "A function turned into a global?");
          
          // Replace any uses of "F" with uses of GV.
          Constant *FInNewType = TheFolder->CreateBitCast(GV, F->getType());
          F->replaceAllUsesWith(FInNewType);

          // Update the decl that points to F.
          changeLLVMConstant(F, FInNewType);

          // Now we can give GV the proper name.
          GV->takeName(F);
          
          // F is now dead, nuke it.
          F->eraseFromParent();
        }
        
      } else {
        GV = GVE;  // Global already created, reuse it.
      }
    }

    if ((TREE_READONLY(decl) && !TREE_SIDE_EFFECTS(decl)) ||
        TREE_CODE(decl) == CONST_DECL) {
      if (DECL_EXTERNAL(decl)) {
        // Mark external globals constant even though they could be marked
        // non-constant in the defining translation unit.  The definition of the
        // global determines whether the global is ultimately constant or not,
        // marking this constant will allow us to do some extra (legal)
        // optimizations that we would otherwise not be able to do.  (In C++,
        // any global that is 'C++ const' may not be readonly: it could have a
        // dynamic initializer.
        //
        GV->setConstant(true);
      } else {
        // Mark readonly globals with constant initializers constant.
        if (DECL_INITIAL(decl) != error_mark_node && // uninitialized?
            DECL_INITIAL(decl) &&
            (TREE_CONSTANT(DECL_INITIAL(decl)) ||
             TREE_CODE(DECL_INITIAL(decl)) == STRING_CST))
          GV->setConstant(true);
      }
    }

    // Set thread local (TLS)
    if (TREE_CODE(decl) == VAR_DECL && DECL_THREAD_LOCAL_P(decl))
      GV->setThreadLocal(true);

    SET_DECL_LLVM(decl, GV);
  }
//TODO  timevar_pop(TV_LLVM_GLOBALS);
}

/// llvm_get_decl_name - Used by varasm.c, returns the specified declaration's
/// name.
const char *llvm_get_decl_name(void *LLVM) {
  if (LLVM)
    if (const ValueName *VN = ((Value*)LLVM)->getValueName())
      return VN->getKeyData();
  return "";
}

/// llvm_mark_decl_weak - Used by varasm.c, called when a decl is found to be
/// weak, but it already had an llvm object created for it. This marks the LLVM
/// object weak as well.
void llvm_mark_decl_weak(tree decl) {
  assert(DECL_LLVM_SET_P(decl) && DECL_WEAK(decl) &&
         isa<GlobalValue>(DECL_LLVM(decl)) && "Decl isn't marked weak!");
  GlobalValue *GV = cast<GlobalValue>(DECL_LLVM(decl));

  // Do not mark something that is already known to be linkonce or internal.
  // The user may have explicitly asked for weak linkage - ignore flag_odr.
  if (GV->hasExternalLinkage()) {
    GlobalValue::LinkageTypes Linkage;
    if (GV->isDeclaration()) {
      Linkage = GlobalValue::ExternalWeakLinkage;
    } else {
      Linkage = GlobalValue::WeakAnyLinkage;
      // Allow loads from constants to be folded even if the constant has weak
      // linkage.  Do this by giving the constant weak_odr linkage rather than
      // weak linkage.  It is not clear whether this optimization is valid (see
      // gcc bug 36685), but mainline gcc chooses to do it, and fold may already
      // have done it, so we might as well join in with gusto.
      if (GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV))
        if (GVar->isConstant())
          Linkage = GlobalValue::WeakODRLinkage;
    }
    GV->setLinkage(Linkage);
  }
}

/// llvm_emit_ctor_dtor - Called to emit static ctors/dtors to LLVM code.
/// fndecl is a 'void()' FUNCTION_DECL for the code, initprio is the init
/// priority, and isCtor indicates whether this is a ctor or dtor.
void llvm_emit_ctor_dtor(tree FnDecl, int InitPrio, int isCtor) {
  mark_decl_referenced(FnDecl);  // Inform cgraph that we used the global.

  if (errorcount || sorrycount) return;

  Constant *C = cast<Constant>(DECL_LLVM(FnDecl));
  (isCtor ? &StaticCtors:&StaticDtors)->push_back(std::make_pair(C, InitPrio));
}

void llvm_emit_typedef(tree decl) {
  // Need hooks for debug info?
  return;
}

/// llvm_emit_file_scope_asm - Emit the specified string as a file-scope inline
/// asm block.
void llvm_emit_file_scope_asm(const char *string) {
  if (TheModule->getModuleInlineAsm().empty())
    TheModule->setModuleInlineAsm(string);
  else
    TheModule->setModuleInlineAsm(TheModule->getModuleInlineAsm() + "\n" +
                                  string);
}

//FIXME/// print_llvm - Print the specified LLVM chunk like an operand, called by
//FIXME/// print-tree.c for tree dumps.
//FIXMEvoid print_llvm(FILE *file, void *LLVM) {
//FIXME  oFILEstream FS(file);
//FIXME  FS << "LLVM: ";
//FIXME  WriteAsOperand(FS, (Value*)LLVM, true, TheModule);
//FIXME}
//FIXME
//FIXME/// print_llvm_type - Print the specified LLVM type symbolically, called by
//FIXME/// print-tree.c for tree dumps.
//FIXMEvoid print_llvm_type(FILE *file, void *LLVM) {
//FIXME  oFILEstream FS(file);
//FIXME  FS << "LLVM: ";
//FIXME  
//FIXME  // FIXME: oFILEstream can probably be removed in favor of a new raw_ostream
//FIXME  // adaptor which would be simpler and more efficient.  In the meantime, just
//FIXME  // adapt the adaptor.
//FIXME  raw_os_ostream RO(FS);
//FIXME  WriteTypeSymbolic(RO, (const Type*)LLVM, TheModule);
//FIXME}

/// extractRegisterName - Get a register name given its decl. In 4.2 unlike 4.0
/// these names have been run through set_user_assembler_name which means they
/// may have a leading \1 at this point; compensate.
const char* extractRegisterName(tree decl) {
  const char* Name = IDENTIFIER_POINTER(DECL_ASSEMBLER_NAME(decl));
  return (*Name == 1) ? Name + 1 : Name;
}


//===----------------------------------------------------------------------===//
//                             Plugin interface
//===----------------------------------------------------------------------===//

// This plugin's code is licensed under the GPLv2.  The LLVM libraries use
// the GPL compatible University of Illinois/NCSA Open Source License.
int plugin_is_GPL_compatible; // This plugin is GPL compatible.

/// execute_emit_llvm - Turn a gimple function into LLVM IR.
static unsigned int
execute_emit_llvm (void)
{
  LazilyInitializeModule();

//TODO Don't want to use sorry at this stage...
//TODO  if (cfun->nonlocal_goto_save_area)
//TODO    sorry("%Jnon-local gotos not supported by LLVM", fndecl);

//TODO Do we want to do this?  Will the warning set sorry_count etc?
//TODO    enum symbol_visibility vis = DECL_VISIBILITY (current_function_decl);
//TODO
//TODO    if (vis != VISIBILITY_DEFAULT)
//TODO      // "asm_out.visibility" emits an important warning if we're using a
//TODO      // visibility that's not supported by the target.
//TODO      targetm.asm_out.visibility(current_function_decl, vis);

  // There's no need to defer outputting this function any more; we
  // know we want to output it.
  DECL_DEFER_OUTPUT(current_function_decl) = 0;

  // Convert the AST to raw/ugly LLVM code.
  TreeToLLVM Emitter(current_function_decl);
  Function *Fn = Emitter.EmitFunction();

//TODO  performLateBackendInitialization();
  createPerFunctionOptimizationPasses();

  if (PerFunctionPasses)
    PerFunctionPasses->run(*Fn);

  // TODO: Nuke the .ll code for the function at -O[01] if we don't want to
  // inline it or something else.

  // Finally, we have written out this function!
  TREE_ASM_WRITTEN(current_function_decl) = 1;

  execute_free_datastructures ();

  // When debugging, append the LLVM IR to the dump file.
  if (dump_file) {
    raw_fd_ostream dump_stream(fileno(dump_file), false);
    Fn->print(dump_stream);
  }

  return 0;
}

/// TakeoverAsmOutput - Obtain exclusive use of the assembly code output file.
/// Any GCC output will be thrown away.
static void TakeoverAsmOutput(void) {
  // Calculate the output file name as in init_asm_output (toplev.c).
  if (!dump_base_name && main_input_filename)
    dump_base_name = main_input_filename[0] ? main_input_filename : "gccdump";

  if (!main_input_filename && !asm_file_name) {
    llvm_asm_file_name = "-";
  } else if (!asm_file_name) {
    int len = strlen (dump_base_name);
    char *dumpname = XNEWVEC (char, len + 6);

    memcpy (dumpname, dump_base_name, len + 1);
    strip_off_ending (dumpname, len);
    strcat (dumpname, ".s");
    llvm_asm_file_name = dumpname;
  } else {
    llvm_asm_file_name = asm_file_name;
  }

  // Redirect any GCC output to /dev/null.
  asm_file_name = HOST_BIT_BUCKET;
}

/// pass_emit_llvm - RTL pass that turns gimple functions into LLVM IR.
static struct rtl_opt_pass pass_emit_llvm =
{
    {
      RTL_PASS,
      "emit_llvm",				/* name */
      NULL,					/* gate */
      execute_emit_llvm,			/* execute */
      NULL,					/* sub */
      NULL,					/* next */
      0,					/* static_pass_number */
      TV_EXPAND,				/* tv_id */
      PROP_ssa | PROP_gimple_leh
        | PROP_gimple_lomp | PROP_cfg,		/* properties_required */
      0,					/* properties_provided */
      PROP_ssa | PROP_trees,			/* properties_destroyed */
      TODO_dump_func | TODO_verify_ssa
        | TODO_verify_flow | TODO_verify_stmts,	/* todo_flags_start */
      TODO_ggc_collect				/* todo_flags_finish */
    }
};


/// gate_null - Gate method for a pass that does nothing.
static bool
gate_null (void)
{
  return false;
}


/// pass_gimple_null - Gimple pass that does nothing.
static struct gimple_opt_pass pass_gimple_null =
{
    {
      GIMPLE_PASS,
      NULL,					/* name */
      gate_null,				/* gate */
      NULL,					/* execute */
      NULL,					/* sub */
      NULL,					/* next */
      0,					/* static_pass_number */
      TV_NONE,					/* tv_id */
      0,					/* properties_required */
      0,					/* properties_provided */
      0,					/* properties_destroyed */
      0,					/* todo_flags_start */
      0						/* todo_flags_finish */
    }
};

/// pass_rtl_null - RTL pass that does nothing.
static struct rtl_opt_pass pass_rtl_null =
{
    {
      RTL_PASS,
      NULL,					/* name */
      gate_null,				/* gate */
      NULL,					/* execute */
      NULL,					/* sub */
      NULL,					/* next */
      0,					/* static_pass_number */
      TV_NONE,					/* tv_id */
      0,					/* properties_required */
      0,					/* properties_provided */
      0,					/* properties_destroyed */
      0,					/* todo_flags_start */
      0						/* todo_flags_finish */
    }
};

/// llvm_plugin_info - Information about this plugin.  Users can access this
/// using "gcc --help -v".
static struct plugin_info llvm_plugin_info = {
  REVISION,	// version
  // TODO provide something useful here
  NULL		// help
};

/// plugin_init - The initialization routine called by GCC.  Defined in
/// gcc-plugin.h.
int plugin_init (struct plugin_name_args *plugin_info,
                 struct plugin_gcc_version *version) {
  const char *plugin_name = plugin_info->base_name;
  bool disable_gcc_optimizations = true;
  struct plugin_pass pass_info;

  // Check that the running gcc is the same as the gcc we were built against.
  // If not, refuse to load.  This seems wise when developing against a fast
  // moving gcc tree.  TODO: Use a milder check if doing a "release build".
  if (!plugin_default_version_check (version, &gcc_version)) {
    errs() << "Incompatible plugin version\n";
    return 1;
  }

  // Process any plugin arguments.
  {
    int argc = plugin_info->argc;
    struct plugin_argument *argv = plugin_info->argv;

    for (int i = 0; i < argc; ++i) {
      if (!strcmp (argv[i].key, "enable-gcc-optzns")) {
        if (argv[i].value)
          warning (0, G_("option '-fplugin-arg-%s-%s=%s' ignored"
                         " (superfluous '=%s')"),
                   plugin_name, argv[i].key, argv[i].value, argv[i].value);
        else
          disable_gcc_optimizations = false;
      } else {
        warning (0, G_("plugin %qs: unrecognized argument %qs ignored"),
                 plugin_name, argv[i].key);
      }
    }
  }

  // Provide our version and help information.
  register_callback (plugin_name, PLUGIN_INFO, NULL, &llvm_plugin_info);

  // Obtain exclusive use of the assembly code output file.
  TakeoverAsmOutput();

  // Replace rtl expansion with gimple to LLVM conversion.
  pass_info.pass = &pass_emit_llvm.pass;
  pass_info.reference_pass_name = "expand";
  pass_info.ref_pass_instance_number = 0;
  pass_info.pos_op = PASS_POS_REPLACE;
  register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);

  // Turn off all rtl passes.
  pass_info.pass = &pass_gimple_null.pass;
  pass_info.reference_pass_name = "*rest_of_compilation";
  pass_info.ref_pass_instance_number = 0;
  pass_info.pos_op = PASS_POS_REPLACE;
  register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);

  pass_info.pass = &pass_rtl_null.pass;
  pass_info.reference_pass_name = "*clean_state";
  pass_info.ref_pass_instance_number = 0;
  pass_info.pos_op = PASS_POS_REPLACE;
  register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);

  if (disable_gcc_optimizations) {
    // Turn off all gcc optimization passes.
    // TODO: figure out a good way of turning off ipa passes.
    pass_info.pass = &pass_gimple_null.pass;
    pass_info.reference_pass_name = "*all_optimizations";
    pass_info.ref_pass_instance_number = 0;
    pass_info.pos_op = PASS_POS_REPLACE;
    register_callback (plugin_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &pass_info);
  }

  // Finish the .s file.
  register_callback (plugin_name, PLUGIN_FINISH_UNIT, llvm_finish_unit, NULL);

  return 0;
}
