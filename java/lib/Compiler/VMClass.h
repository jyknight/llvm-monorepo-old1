//===-- Class.h - Compiler representation of a Java class -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Class class that represents a
// compile time representation of a Java class (java.lang.Class).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_JAVA_CLASS_H
#define LLVM_JAVA_CLASS_H

#include <llvm/Module.h>
#include <llvm/Type.h>
#include <map>
#include <vector>

namespace llvm { namespace Java {

  class Resolver;

  class Class {
    static const unsigned INVALID_INTERFACE_INDEX = 0xFFFFFFFF;

    Resolver* resolver_;
    const Class* componentClass_;
    Type* structType_;
    const Type* type_;
    unsigned interfaceIndex_;
    typedef std::map<std::string, int> Field2IndexMap;
    Field2IndexMap f2iMap_;
    typedef std::vector<const Type*> ElementTypes;
    ElementTypes elementTypes;

    void addField(const std::string& name, const Type* type);
    void resolveType();

    // Creates a dummy class.
    explicit Class(Resolver& resolver);

    // Creates primitive class for type.
    Class(Resolver& resolver, const Type* type);

    // Builds the class object for the named class.
    void buildClass(const std::string& className);
    // Builds the array class object of component type componentClass.
    void buildArrayClass(const Class& componentClass);

    friend class Resolver;
    
  public:
    const Type* getStructType() const { return structType_; }
    const Type* getType() const { return type_; }
    const Class* getComponentClass() const { return componentClass_; }
    bool isArray() const { return componentClass_; }
    unsigned getInterfaceIndex() const { return interfaceIndex_; }
    int getFieldIndex(const std::string& name) const;
  };

} } // namespace llvm::Java

#endif//LLVM_JAVA_CLASS_H
