//===-- hlvm/AST/AST.h - AST Container Class --------------------*- C++ -*-===//
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
/// @file hlvm/AST/AST.h
/// @author Reid Spencer <reid@hlvm.org> (original author)
/// @date 2006/05/04
/// @since 0.1.0
/// @brief Declares the class hlvm::AST::AST
//===----------------------------------------------------------------------===//

#ifndef HLVM_AST_AST_H
#define HLVM_AST_AST_H

#include <string>

/// This namespace is for all HLVM software. It ensures that HLVM software does
/// not collide with any other software. Hopefully HLVM is not a namespace used
/// elsewhere. 
namespace hlvm
{
/// This namespace contains all the AST (Abstract Syntax Tree) module code. All
/// node types of the AST are declared in this namespace.
namespace AST
{
  class Bundle;   
  class Function; 
  class Import;
  class Locator; 
  class SignatureType;
  class Type;
  class Variable; 

  /// This class is used to hold or contain an Abstract Syntax Tree. It provides
  /// those aspects of the tree that are not part of the tree itself.
  /// @brief AST Container Class
  class AST
  {
    /// @name Constructors
    /// @{
    public:
      AST() : sysid(), pubid(), root(0) {}

    /// @}
    /// @name Accessors
    /// @{
    public:
      const std::string& getSystemID() { return sysid; }
      const std::string& getPublicID() { return pubid; }
      Bundle* getRoot() { return root; }

    /// @}
    /// @name Mutators
    /// @{
    public:
      void setSystemID(const std::string& id) { sysid = id; }
      void setPublicID(const std::string& id) { pubid = id; }
      void setRoot(Bundle* top) { root = top; }

    /// @}
    /// @name Lookup
    /// @{
    public:
      Type* resolveType(const std::string& name) const;

    /// @}
    /// @name Factories
    /// @{
    public:
      Bundle* new_Bundle(const Locator& loc, const std::string& id);
      Function* new_Function(const Locator& loc, const std::string& id);
      Import* new_Import(const Locator& loc, const std::string& id);
      SignatureType* new_SignatureType(const Locator& l, const std::string& id);
      Variable* new_Variable(const Locator& loc, const std::string& id);
      Type* new_IntegerType(
        const Locator&loc,      ///< The locator of the declaration
        const std::string& id,  ///< The name of the atom
        uint64_t bits = 32,     ///< The number of bits
        bool isSigned = true    ///< The signedness
      );
      Type* new_RangeType(
        const Locator&loc,      ///< The locator of the declaration
        const std::string& id,  ///< The name of the atom
        int64_t min,            ///< The minimum value accepted in range
        int64_t max             ///< The maximum value accepted in range
      );
      Type* new_RealType(
        const Locator&loc,      ///< The locator of the declaration
        const std::string& id,  ///< The name of the atom
        uint32_t mantissa = 52, ///< The bits in the mantissa (fraction)
        uint32_t exponent = 11  ///< The bits in the exponent
      );
      Type* new_AnyType(const Locator&loc, const std::string& id);
      Type* new_BooleanType(const Locator&loc, const std::string& id);
      Type* new_CharacterType(const Locator&loc, const std::string& id);
      Type* new_OctetType(const Locator&loc, const std::string& id);
      Type* new_VoidType(const Locator&loc, const std::string& id);
    /// @}
    /// @name Data
    /// @{
    protected:
      std::string sysid;
      std::string pubid;
      Bundle* root;
    /// @}
  };
} // AST
} // hlvm
#endif
