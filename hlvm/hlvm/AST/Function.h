//
// Copyright (C) 2006 HLVM Group. All Rights Reserved.
//
// This program is open source software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License (GPL) as published by
// the Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. You should have received a copy of the GPL in a
// file named COPYING that was included with this program; if not, you can
// obtain a copy of the license through the Internet at http://www.fsf.org/
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.
//
////////////////////////////////////////////////////////////////////////////////
/// @file hlvm/AST/Function.h
/// @author Reid Spencer <reid@hlvm.org> (original author)
/// @date 2006/05/04
/// @since 0.1.0
/// @brief Declares the class hlvm::AST::Function
////////////////////////////////////////////////////////////////////////////////

#ifndef HLVM_AST_FUNCTION_H
#define HLVM_AST_FUNCTION_H

#include <hlvm/AST/LinkageItem.h>

namespace hlvm
{
namespace AST
{
  class Block; // Forward declare
  class SignatureType;  // Forward declare

  /// This class represents an Function in the HLVM Abstract Syntax Tree.  
  /// A Function is a callable block of code that accepts parameters and 
  /// returns a result.  This is the basic unit of code in HLVM. A Function
  /// has a name, a set of formal arguments, a return type, and a block of
  /// code to execute.
  /// @brief HLVM AST Function Node
  class Function : public LinkageItem
  {
    /// @name Constructors
    /// @{
    public:
      Function(
        Bundle* parent, ///< The bundle in which the function is defined
        const std::string& name ///< The name of the function
      ) : LinkageItem(FunctionID,parent,name) {}
      virtual ~Function();

    /// @}
    /// @name Accessors
    /// @{
    public:
      static inline bool classof(const Function*) { return true; }
      static inline bool classof(const Node* N) { return N->isFunction(); }

    /// @}
    /// @name Data
    /// @{
    protected:
      Block * block_;                   ///< The code block to be executed
      SignatureType* signature_;        ///< The function signature.
    /// @}
  };
} // AST
} // hlvm
#endif
