//===-- Pass Interface Class ------------------------------------*- C++ -*-===//
//
//                      High Level Virtual Machine (HLVM)
//
// Copyright (C) 2006 Reid Spencer. All Rights Reserved.
//
// This software is free software; you can redistribute it and/or modify it 
// under the terms of the GNU Lesser General Public License as published by 
// the Free Software Foundation; either version 2.1 of the License, or (at 
// your option) any later version.
//
// This software is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for 
// more details.
//
// You should have received a copy of the GNU Lesser General Public License 
// along with this library in the file named LICENSE.txt; if not, write to the 
// Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, 
// MA 02110-1301 USA
//
//===----------------------------------------------------------------------===//
/// @file hlvm/Pass/Pass.cpp
/// @author Reid Spencer <rspencer@reidspencer.org> (original author)
/// @date 2006/05/18
/// @since 0.1.0
/// @brief Implements the functions of class hlvm::Pass::Pass.
//===----------------------------------------------------------------------===//

#include <hlvm/Pass/Pass.h>
#include <hlvm/AST/AST.h>
#include <hlvm/AST/Bundle.h>
#include <hlvm/AST/ContainerType.h>
#include <hlvm/AST/LinkageItems.h>
#include <hlvm/Base/Assert.h>

using namespace hlvm;
using namespace llvm;

namespace {

class PassManagerImpl : public PassManager
{
public:
  PassManagerImpl() : PassManager(), pre(), post() {}
  void addPass(Pass* p);
  virtual void runOn(AST* tree);
  virtual void runOn(AST* tree, Node* startAt);

  inline void runIfInterested(Pass* p, Node* n, Pass::TraversalKinds m);
  inline void runPreOrder(Node* n);
  inline void runPostOrder(Node* n);
  inline void runOn(Operator* b);
  inline void runOn(Block* b);
  inline void runOn(Bundle* b);
  inline void runOn(Value* b);
  inline void runOn(Constant* b);

private:
  std::vector<Pass*> pre;
  std::vector<Pass*> post;
  std::vector<Pass*> all;
};

void
PassManagerImpl::addPass(Pass* p)
{
  all.push_back(p);
  if (p->mode() & Pass::PreOrderTraversal)
    pre.push_back(p);
  if (p->mode() & Pass::PostOrderTraversal)
    post.push_back(p);
}

inline void
PassManagerImpl::runIfInterested(Pass* p, Node* n, Pass::TraversalKinds m)
{
  int interest = p->interest();
  if (interest == 0 ||
     ((interest & Pass::Type_Interest) && n->isType()) ||
     ((interest & Pass::Function_Interest) && n->isFunction()) ||
     ((interest & Pass::Block_Interest) && n->is(BlockID)) ||
     ((interest & Pass::Operator_Interest) && n->isOperator()) ||
     ((interest & Pass::Program_Interest) && n->is(ProgramID)) ||
     ((interest & Pass::Variable_Interest) && n->is(VariableID))
     ) {
    p->handle(n,m);
  }
}

inline void 
PassManagerImpl::runPreOrder(Node* n)
{
  std::vector<Pass*>::iterator I = pre.begin(), E = pre.end();
  while (I != E) {
    runIfInterested(*I,n,Pass::PreOrderTraversal);
    ++I;
  }
}

inline void 
PassManagerImpl::runPostOrder(Node* n)
{
  std::vector<Pass*>::iterator I = post.begin(), E = post.end();
  while (I != E) {
    runIfInterested(*I,n,Pass::PostOrderTraversal);
    ++I;
  }
}

inline void
PassManagerImpl::runOn(Constant* cst)
{
  hlvmAssert(isa<Constant>(cst));
  runPreOrder(cst);
  // FIXME: Eventually we'll have structured constants which need to have 
  // their contents examined as well.
  runPostOrder(cst);
}

inline void
PassManagerImpl::runOn(Operator* op)
{
  runPreOrder(op);
  size_t limit = op->getNumOperands();
  for (size_t i = 0; i < limit; ++i) {
    // Skip non-operator operands as they've been handled elsewhere
    if (isa<Operator>(op))
      runOn(op->getOperand(i));
  }
  runPostOrder(op);
}

inline void
PassManagerImpl::runOn(Value* v)
{
  if (isa<Constant>(v))
    runOn(cast<Constant>(v));
  else if (isa<Operator>(v))
    runOn(cast<Operator>(v));
  else
    hlvmDeadCode("Value not an Operator or Constant?");
}

inline void
PassManagerImpl::runOn(Block* b)
{
  runPreOrder(b);
  for (Block::iterator I = b->begin(), E = b->end(); I != E; ++I) {
    if (!*I)
      break;
    if (isa<Block>(*I))
      runOn(cast<Block>(*I)); // recurse!
    else if (isa<Variable>(*I))
      runOn(cast<Variable>(*I));
    else if (isa<Operator>(*I))
      runOn(cast<Operator>(*I)); 
    else
      hlvmDeadCode("Block has invalid content");
  }
  runPostOrder(b);
}

inline void 
PassManagerImpl::runOn(Bundle* b)
{
  runPreOrder(b);
  for (Bundle::type_iterator TI =b->type_begin(), TE = b->type_end(); 
       TI != TE; ++TI) {
    runPreOrder(const_cast<Node*>(TI->second));
    runPostOrder(const_cast<Node*>(TI->second));
  }
  for (Bundle::constant_iterator CI = b->const_begin(), CE = b->const_end();
      CI != CE; ++CI)
  {
    runPreOrder(const_cast<Node*>(CI->second));
    runPostOrder(const_cast<Node*>(CI->second));
  }
  for (Bundle::var_iterator VI = b->var_begin(), VE = b->var_end(); 
       VI != VE; ++VI) {
    runPreOrder(const_cast<Node*>(VI->second));
    runPostOrder(const_cast<Node*>(VI->second));
  }
  for (Bundle::func_iterator FI = b->func_begin(), FE = b->func_end(); 
       FI != FE; ++FI) {
    runPreOrder(const_cast<Node*>(FI->second));
    runOn(llvm::cast<Function>(FI->second)->getBlock());
    runPostOrder(const_cast<Node*>(FI->second));
  }
  runPostOrder(b);
}

void PassManagerImpl::runOn(AST* tree)
{
  // Call the initializers
  std::vector<Pass*>::iterator PI = all.begin(), PE = all.end();
  while (PI != PE) { (*PI)->handleInitialize(tree); ++PI; }

  // Just a little optimization for empty pass managers
  if (pre.empty() && post.empty())
    return;

  // Traverse each of the bundles in the AST node.
  for (AST::iterator I = tree->begin(), E = tree->end(); I != E; ++I)
    runOn(*I);

  // Call the terminators
  PI = all.begin(), PE = all.end();
  while (PI != PE) { (*PI)->handleTerminate(); ++PI; }
}

void 
PassManagerImpl::runOn(AST* tree, Node* startAt)
{
  // Check to make sure startAt is in tree
  hlvmAssert(tree != 0 && "Can't run passes on null tree");
  hlvmAssert(startAt != 0 && "Can't run passes from null start");
  Node* parent = startAt;
  while (parent->getParent() != 0) parent = parent->getParent();
  hlvmAssert(parent == tree && "Can't run passes on node that isn't in tree");

  if (isa<Bundle>(startAt))
    runOn(cast<Bundle>(startAt));
  else if (isa<Block>(startAt))
    runOn(cast<Block>(startAt));
  else if (isa<Operator>(startAt))
    runOn(cast<Operator>(startAt));
  else if (isa<Constant>(startAt))
    runOn(cast<Constant>(startAt));
  else if (isa<Value>(startAt))
    runOn(cast<Value>(startAt));
}

} // anonymous namespace

Pass::~Pass()
{
}

void 
Pass::handleInitialize(AST* tree)
{
}

void 
Pass::handleTerminate()
{
}

PassManager::~PassManager()
{
}

PassManager*
PassManager::create()
{
  return new PassManagerImpl;
}
