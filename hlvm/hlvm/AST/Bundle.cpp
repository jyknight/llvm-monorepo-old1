//===-- hlvm/AST/Bundle.cpp - AST Bundle Class ------------------*- C++ -*-===//
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
/// @file hlvm/AST/Bundle.cpp
/// @author Reid Spencer <reid@hlvm.org> (original author)
/// @date 2006/05/04
/// @since 0.1.0
/// @brief Implements the functions of class hlvm::AST::Bundle.
//===----------------------------------------------------------------------===//

#include <hlvm/AST/Bundle.h>
#include <hlvm/AST/Type.h>
#include <hlvm/AST/Variable.h>
#include <hlvm/AST/Function.h>
#include <hlvm/Base/Assert.h>

using namespace llvm; 

namespace hlvm {

Bundle*
Bundle::create(const Locator* loc, const std::string& id)
{
  Bundle* result = new Bundle();
  result->setLocator(loc);
  result->setName(id);
  return result;
}

Bundle::~Bundle()
{
}

void 
Bundle::insertChild(Node* kid)
{
  if (kid->isType())
    types.insert(cast<Type>(kid)->getName(), kid);
  else if (kid->is(VariableID))
    vars.insert(cast<Variable>(kid)->getName(), kid);
  else if (kid->isFunction())
    funcs.insert(cast<Function>(kid)->getName(), kid);
  else
    hlvmAssert("Don't know how to insert that in a Bundle");
}

void
Bundle::removeChild(Node* kid)
{
  hlvmAssert(isa<LinkageItem>(kid) && "Can't remove that here");
  // This is sucky slow, but we probably won't be removing nodes that much.
  if (kid->isType()) {
    types.erase(cast<Type>(kid)->getName());
  } else if (kid->is(VariableID)) {
    vars.erase(cast<Variable>(kid)->getName());
  } else if (kid->isFunction()) {
    funcs.erase(cast<Function>(kid)->getName());
  }
  hlvmAssert(!"That node isn't my child");
}

Type*  
Bundle::type_find(const std::string& name) const
{
  return llvm::cast<Type>(types.lookup(name));
}

Function*  
Bundle::func_find(const std::string& name) const
{
  return llvm::cast<Function>(funcs.lookup(name));
}

Variable*  
Bundle::var_find(const std::string& name) const
{
  return llvm::cast<Variable>(vars.lookup(name));
}

Import::~Import()
{
}

}
