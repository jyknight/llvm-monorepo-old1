//===-- OperandStack.cpp - Java operand stack -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the abstraction of a Java operand stack. We
// model the java operand stack as a stack of LLVM allocas.
//
//===----------------------------------------------------------------------===//

#include "OperandStack.h"
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/ADT/StringExtras.h>

using namespace llvm::Java;

void OperandStack::push(Value* value, BasicBlock* insertAtEnd)
{
  TheStack.push(new AllocaInst(value->getType(),
			       NULL,
			       "opStack" + utostr(TheStack.size()),
			       insertAtEnd));
  new StoreInst(value, TheStack.top(), insertAtEnd);
}

llvm::Value* OperandStack::pop(BasicBlock* insertAtEnd)
{
  Value* val = TheStack.top();
  TheStack.pop();
  return new LoadInst(val, "pop", insertAtEnd);
}
