//===-- hlvm/AST/ContainerType.cpp - AST ContainerType Class ----*- C++ -*-===//
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
/// @file hlvm/AST/ContainerType.cpp
/// @author Reid Spencer <rspencer@reidspencer.org> (original author)
/// @date 2006/05/18
/// @since 0.1.0
/// @brief Implements the functions of the various AST container types
//===----------------------------------------------------------------------===//

#include <hlvm/AST/ContainerType.h>

using namespace llvm;

namespace hlvm { namespace AST {

ContainerType::~ContainerType()
{
}

void 
ContainerType::insertChild(Node* n)
{
  assert(isa<Type>(n) && "Can't insert those here");
  types.push_back(cast<Type>(n));
}

void 
ContainerType::removeChild(Node* n)
{
  assert(isa<Type>(n) && "Can't remove those here");
  // This is sucky slow, but we probably won't be removing nodes that much.
  for (iterator I = begin(), E = end(); I != E; ++I ) {
    if (*I == n) { types.erase(I); break; }
  }
  assert(!"That node isn't my child");
}

const char* 
ContainerType::getPrimitiveName()
{
  return 0;
}

PointerType::~PointerType()
{
}

void
PointerType::insertChild(Node* n)
{
  assert(this->empty() && "Can't point to multiple types");
  ContainerType::insertChild(n);
}

ArrayType::~ArrayType()
{
}

void
ArrayType::insertChild(Node* n)
{
  assert(this->empty() && "Can't have multi-typed arrays");
  ContainerType::insertChild(n);
}

VectorType::~VectorType()
{
}

void
VectorType::insertChild(Node* n)
{
  assert(this->empty() && "Can't have multi-typed vectors");
  ContainerType::insertChild(n);
}

StructureType::~StructureType()
{
}

void
StructureType::insertChild(Node* n)
{
  assert(isa<NamedType>(n) && "Can't insert those here");
  types.push_back(cast<NamedType>(n));
}

SignatureType::~SignatureType()
{
}

void 
SignatureType::insertChild(Node* n)
{
  assert(isa<NamedType>(n) && "Can't insert those here");
  types.push_back(cast<NamedType>(n));
}

}}
