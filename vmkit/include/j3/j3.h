#ifndef _J3_J3_
#define _J3_J3_

#include <pthread.h>
#include <vector>

#include "vmkit/names.h"
#include "vmkit/vmkit.h"
#include "vmkit/allocator.h"

#include "j3/j3options.h"
#include "j3/j3typesdef.h"
#include "j3/j3jni.h"
#include "j3/j3monitor.h"
#include "j3/j3constants.h"

namespace llvm {
	class FunctionType;
}

namespace j3 {
	class J3InitialClassLoader;
	class J3Class;
	class J3ArrayClass;
	class J3Type;
	class J3Object;
	class J3ObjectHandle;
	class J3Primitive;
	class J3Lib;
	class J3Method;
	class J3LLVMSignature;
	class J3Signature;

	class J3 : public vmkit::VMKit {
		typedef std::map<J3ObjectHandle*, J3ObjectHandle*, vmkit::T_ptr_less_t<J3ObjectHandle*>, 
										 vmkit::StdAllocator<std::pair<J3ObjectHandle*, J3ObjectHandle*> > > StringMap;

		typedef std::map<llvm::FunctionType*, J3LLVMSignature*, vmkit::T_ptr_less_t<llvm::FunctionType*>, 
										 vmkit::StdAllocator<std::pair<llvm::FunctionType*, J3LLVMSignature*> > > SignatureMap;

		static vmkit::T_ptr_less_t<J3ObjectHandle*>     charArrayLess;
	  static vmkit::T_ptr_less_t<llvm::FunctionType*> llvmFunctionTypeLess;

		J3Options                             _options;
	
		pthread_mutex_t                       stringsMutex;
		vmkit::NameMap<J3ObjectHandle*>::map  nameToCharArrays;
		StringMap                             charArrayToStrings;
		vmkit::Names                          _names;

		void                       introspect();

		void        runApplication();
		void        compileApplication();

		J3(vmkit::BumpAllocator* allocator);
	public:
		J3InitialClassLoader*                 initialClassLoader;

		static J3*  create();

#define defJavaConstantName(name, id) \
	  const vmkit::Name* name;
	  onJavaConstantNames(defJavaConstantName)
#undef defJavaConstantName
		J3Signature*       clinitSign;

#define defPrimitive(name, ctype, llvmtype, scale)	\
		J3Primitive* type##name;
		onJavaTypes(defPrimitive)
#undef defPrimitive

		J3MonitorManager monitorManager;
	  SignatureMap     llvmSignatures; /* protected by the lock of the compiler */

		void*            interfaceTrampoline;

		J3Class**        arrayInterfaces;
		uint32_t         nbArrayInterfaces;
		J3Class*         objectClass;
		J3ArrayClass*    charArrayClass;

		J3Class*         stringClass;
		J3Method*        stringClassInit;
		J3Field*         stringClassValue;

		J3Class*         classClass;
		J3Method*        classClassInit;
		J3Field*         classClassVMData;

		J3Class*         classLoaderClass;
		J3Field*         classLoaderClassVMData;
		J3Method*        classLoaderClassLoadClass;
		J3Method*        classLoaderClassGetSystemClassLoader;

		J3Class*         threadClass;
		J3Field*         threadClassVMData;
		J3Method*        threadClassRun;

		J3Class*         fieldClass;
		J3Field*         fieldClassClass;
		J3Field*         fieldClassSlot;
		J3Field*         fieldClassAccess;
		J3Method*        fieldClassInit;

		J3Class*         constructorClass;
		J3Field*         constructorClassClass;
		J3Field*         constructorClassSlot;
		J3Method*        constructorClassInit;

		J3Class*         methodClass;
		J3Field*         methodClassClass;
		J3Field*         methodClassSlot;
		J3Method*        methodClassInit;

		J3Field*         throwableClassBacktrace;

		J3Class*         stackTraceElementClass;
		J3Method*        stackTraceElementClassInit;

		llvm::Type* typeJNIEnvPtr;
		llvm::Type* typeJ3VirtualTablePtr;
		llvm::Type* typeJ3Type;
		llvm::Type* typeJ3TypePtr;
		llvm::Type* typeJ3LayoutPtr;
		llvm::Type* typeJ3Thread;
		llvm::Type* typeJ3ObjectType;
		llvm::Type* typeJ3ObjectTypePtr;
		llvm::Type* typeJ3Class;
		llvm::Type* typeJ3ClassPtr;
		llvm::Type* typeJ3ArrayClass;
		llvm::Type* typeJ3ArrayClassPtr;
		llvm::Type* typeJ3Method;
		llvm::Type* typeJ3ArrayObject;
		llvm::Type* typeJ3ArrayObjectPtr;
		llvm::Type* typeJ3Object;
		llvm::Type* typeJ3ObjectPtr;
		llvm::Type* typeJ3ObjectHandlePtr;
		llvm::Type* typeJ3LockRecord;
		llvm::Type* typeGXXException;

		J3Options*                 options() { return &_options; }
		vmkit::Names*              names() { return &_names; }
		J3ObjectHandle*            utfToString(const char* name, bool doPush=1);
		J3ObjectHandle*            nameToString(const vmkit::Name* name, bool doPush=1);
		J3ObjectHandle*            arrayToString(J3ObjectHandle* array, bool doPush=1);
		const vmkit::Name*         arrayToName(J3ObjectHandle* array);
		const vmkit::Name*         stringToName(J3ObjectHandle* str);

		const vmkit::Name*         qualifiedToBinaryName(const char* type, size_t length=-1);

		void                       run();
		void                       start(int argc, char** argv);

		void                       vinternalError(const char* msg, va_list va) __attribute__((noreturn));

		static JNIEnv* jniEnv();

		static void    classNotFoundException(const vmkit::Name* name) __attribute__((noreturn));
		static void    noClassDefFoundError(const vmkit::Name* name) __attribute__((noreturn));
		static void    classFormatError(J3ObjectType* cl, const char* reason, ...) __attribute__((noreturn));
		static void    noSuchMethodError(const char* msg, 
																		 J3ObjectType* clName, const vmkit::Name* name, J3Signature* signature) __attribute__((noreturn));
		static void    noSuchFieldError(const char* msg, 
																		J3ObjectType* clName, const vmkit::Name* name, J3Type* type) __attribute__((noreturn));
		static void    linkageError(J3Method* method) __attribute__((noreturn));

		static void    outOfMemoryError() __attribute__((noreturn));

		static void    nullPointerException() __attribute__((noreturn));
		static void    classCastException() __attribute__((noreturn));

		static void    negativeArraySizeException(int32_t length) __attribute__((noreturn));
		static void    arrayStoreException() __attribute__((noreturn));
		static void    arrayIndexOutOfBoundsException() __attribute__((noreturn));

		static void    illegalMonitorStateException() __attribute__((noreturn));

		static void    illegalArgumentException(const char* msg) __attribute__((noreturn));

		void           printStackTrace();
		void           uncatchedException(void* e);

		void forceSymbolDefinition();
	};
}

#endif
