// LLVM headers
#include "llvm/Constant.h"
#include "llvm/Value.h"

// System headers
#include <gmp.h>

// GCC headers
#undef VISIBILITY_HIDDEN

extern "C" {
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "tree.h"
}

using namespace llvm;

bool flag_odr = false;

void llvm_set_decl (tree t, Value *V) {
abort();
}

Value *llvm_get_decl(tree t) {
abort();
}

bool llvm_set_decl_p(tree t) {
abort();
}

void eraseLocalLLVMValues() {
abort();
}

void changeLLVMConstant(Constant *Old, Constant *New) {
abort();
}

void llvmEraseLType(const Type *Ty) {
abort();
}

int ix86_regparm;

extern "C" bool contains_aligned_value_p (tree type) {
abort();
}
