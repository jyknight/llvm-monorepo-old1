//===---- IsolateCommonClass.h - User visible classes with isolates -------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef ISOLATE_COMMON_CLASS_H
#define ISOLATE_COMMON_CLASS_H

#include "mvm/Object.h"

namespace jnjvm {

class ArrayUInt8;
class AssessorDesc;
class CommonClass;
class Class;
class ClassArray;
class ClassPrimitive;
class JavaConstantPool;
class JavaField;
class JavaMethod;
class JavaObject;
class Jnjvm;
class JnjvmClassLoader;
class UserClass;
class UserClassArray;
class UTF8;

class UserCommonClass : public mvm::Object {
public:
  CommonClass* classDef;
  JnjvmClassLoader* classLoader;
  JavaObject* delegatee;
  uint8 status;

  virtual void TRACER;

  bool isOfTypeName(const UTF8* name);
  bool isAssignableFrom(UserCommonClass* cl);
  
  /// subclassOf - If this class is a regular class, is it a subclass of the
  /// given class?
  ///
  bool subclassOf(UserCommonClass* cl);

  bool isArray();
  bool isPrimitive();
  bool isInterface();
  bool isReady();
  uint8 getAccess();
  const UTF8* getName();

  void getDeclaredConstructors(std::vector<JavaMethod*>& res, bool publicOnly);
  void getDeclaredFields(std::vector<JavaField*>& res, bool publicOnly);
  void getDeclaredMethods(std::vector<JavaMethod*>& res, bool publicOnly);
  
  void initialiseClass(Jnjvm* vm);
  JavaObject* getClassDelegatee(Jnjvm* vm, JavaObject* pd = 0);

  void resolveClass();
  UserClass* getSuper();

  std::vector<UserClass*>* getInterfaces();

  JavaMethod* lookupMethodDontThrow(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse);
  JavaMethod* lookupMethod(const UTF8* name, const UTF8* type,
                           bool isStatic, bool recurse);
  JavaField* lookupField(const UTF8* name, const UTF8* type,
                         bool isStatic, bool recurse,
                         UserCommonClass*& fieldCl);
  
  uint64 getVirtualSize();
  VirtualTable* getVirtualVT();

  void setInterfaces(std::vector<UserClass*> Is);
  void setSuper(UserClass* S);

  bool instantiationOfArray(UserClassArray* cl);
  bool implements(UserCommonClass* cl);

  /// constructMethod - Add a new method in this class method map.
  ///
  JavaMethod* constructMethod(const UTF8* name, const UTF8* type,
                              uint32 access);
  
  /// constructField - Add a new field in this class field map.
  ///
  JavaField* constructField(const UTF8* name, const UTF8* type,
                            uint32 access);
};

class UserClass : public UserCommonClass {
public:
  static VirtualTable* VT;
  JavaObject* staticInstance;
  
  virtual void TRACER;

  UserClass(JnjvmClassLoader* JCL, const UTF8* name, ArrayUInt8* bytes);
  
  JavaObject* doNew(Jnjvm* vm);
  
  std::vector<UserClass*>* getInnerClasses();
  UserClass* getOuterClass();
  void resolveInnerOuterClasses();
  JavaObject* getStaticInstance();
  JavaConstantPool* getConstantPool();

  void setStaticSize(uint64 size);
  void setStaticVT(VirtualTable* VT);
  
  uint64 getStaticSize();
  VirtualTable* getStaticVT();
};

class UserClassArray : public UserCommonClass {
public:
  static VirtualTable* VT;
  UserCommonClass* _baseClass;
  
  virtual void TRACER;
  UserClassArray(JnjvmClassLoader* JCL, const UTF8* name);

  UserCommonClass* baseClass();

  AssessorDesc* funcs();
};

class UserClassPrimitive : public UserCommonClass {
  static VirtualTable* VT;
public:
  
  virtual void TRACER;
  UserClassPrimitive(JnjvmClassLoader* JCL, const UTF8* name, uint32 nb);
};

} // end namespace jnjvm

#endif // JNJVM_CLASS_ISOLATE_H
