//===-- Class.cpp - Compiler representation of a Java class -----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Class class that represents a
// compile time representation of a Java class (java.lang.Class). This unlike
// a classfile representation, it resolves the constant pool, creates global
// variables for the static members of this class and also creates the class
// record (vtable) of this class.
//
//===----------------------------------------------------------------------===//

#include "Class.h"
#include "Resolver.h"
#include <llvm/DerivedTypes.h>
#include <llvm/Java/ClassFile.h>

#define LLVM_JAVA_OBJECT_BASE "struct.llvm_java_object_base"

using namespace llvm;
using namespace llvm::Java;

Class::Class(Resolver& resolver)
  : resolver_(&resolver),
    componentClass_(NULL),
    structType_(OpaqueType::get()),
    type_(PointerType::get(structType_)),
    interfaceIndex_(INVALID_INTERFACE_INDEX)
{

}

Class::Class(Resolver& resolver, const Type* type)
  : resolver_(&resolver),
    componentClass_(NULL),
    structType_(0),
    type_(type),
    interfaceIndex_(INVALID_INTERFACE_INDEX)
{

}

void Class::addField(const std::string& name, const Type* type)
{
  f2iMap_.insert(std::make_pair(name, elementTypes.size()));
  elementTypes.push_back(type);
}

int Class::getFieldIndex(const std::string& name) const {
  Field2IndexMap::const_iterator it = f2iMap_.find(name);
  return it == f2iMap_.end() ? -1 : it->second;
}

void Class::resolveType() {
  PATypeHolder holder = structType_;
  Type* resolvedType = StructType::get(elementTypes);
  cast<OpaqueType>(structType_)->refineAbstractTypeTo(resolvedType);
  structType_ = holder.get();
  type_ = PointerType::get(structType_);
}

void Class::buildClass(const std::string& className)
{
  const ClassFile* cf = ClassFile::get(className);

  if (cf->isInterface())
    interfaceIndex_ = resolver_->getNextInterfaceIndex();

  // This is any class but java/lang/Object.
  if (cf->getSuperClass()) {
    const Class& superClass =
      resolver_->getClass(cf->getSuperClass()->getName()->str());

    // We first add the struct of the super class.
    addField("super", superClass.getStructType());
  }
  // This is java/lang/Object.
  else
    addField("base", resolver_->getObjectBaseType());

  // Then we add the rest of the fields.
  const Fields& fields = cf->getFields();
  for (unsigned i = 0, e = fields.size(); i != e; ++i) {
    Field& field = *fields[i];
    if (!field.isStatic())
      addField(field.getName()->str(), resolver_->getClass(field).getType());
  }

  resolveType();

  assert(!isa<OpaqueType>(getStructType()) &&"Class not initialized properly!");
}

void Class::buildArrayClass(const Class& componentClass)
{
  componentClass_ = &componentClass;
  addField("super", resolver_->getClass("java/lang/Object").getStructType());
  addField("<length>", Type::UIntTy);
  addField("<data>", ArrayType::get(componentClass_->getType(), 0));

  resolveType();
}

