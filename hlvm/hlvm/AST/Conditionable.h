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
/// @file hlvm/AST/Conditionable.h
/// @author Reid Spencer <reid@hlvm.org> (original author)
/// @date 2006/05/04
/// @since 0.1.0
/// @brief Declares the class hlvm::AST::Conditionable
////////////////////////////////////////////////////////////////////////////////

#ifndef HLVM_AST_CONDITIONABLE_H
#define HLVM_AST_CONDITIONABLE_H

#include <hlvm/AST/Node.h>

namespace hlvm {
namespace AST {

  /// This class represents an HLVM Bundle. A Bundle is simply a collection of
  /// declarations and definitions. It is the root of the AST tree and also
  /// the grouping and namespace construct in HLVM. Every compilation unit is
  /// a Bundle. Bundles can also be nested in other Bundles. All programming
  /// constructs are defined as child nodes of some Bundle.
  /// @brief HLVM AST Bundle Node
  class Conditionable : public Node
  {
    /// @name Constructors
    /// @{
    public:
      Conditionable(
        NodeIDs id,
        Node* parent, 
        const std::string& name,
        const std::string& condition_name) 
      : Node(id,parent,name), cond_name_(condition_name) {}
      virtual ~Conditionable();

    /// @}
    /// @name Data
    /// @{
    protected:
      std::string cond_name_;
    /// @}
  };
} // AST
} // hlvm
#endif
