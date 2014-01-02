#ifndef _J3_METHOD_H_
#define _J3_METHOD_H_

#include <stdint.h>
#include <vector>

#include "vmkit/compiler.h"

namespace llvm {
	class FunctionType;
	class Function;
}

namespace vmkit {
	class Name;
}

namespace j3 {
	class J3LLVMSignature;
	class J3Type;
	class J3Attributes;
	class J3Class;
	class J3Method;
	class J3Value;
	class J3ObjectHandle;

	class J3MethodType : public vmkit::PermanentObject {
		J3LLVMSignature*             _llvmSignature;
		J3Type*                      _out;
		uint32_t                     _nbIns;
		J3Type*                      _ins[1];

	public:
		J3MethodType(J3Type** args, size_t nbArgs);

		void                setLLVMSignature(J3LLVMSignature* llvmSignature) { _llvmSignature = llvmSignature; }
		J3LLVMSignature*    llvmSignature() { return _llvmSignature; }
		uint32_t            nbIns() { return _nbIns; }
		J3Type*             out() { return _out; }
		J3Type*             ins(uint32_t idx) { return _ins[idx]; }

		void* operator new(size_t unused, vmkit::BumpAllocator* allocator, size_t n) {
			return vmkit::PermanentObject::operator new(sizeof(J3MethodType) + (n - 1) * sizeof(J3Type*), allocator);
		}
	};

	class J3MethodCode : public vmkit::Symbol {
	public:
		J3Method* self;

		J3MethodCode(J3Method* _self) { self = _self; }

		void* getSymbolAddress();
	};

	class J3Method : public vmkit::Symbol {
	public:
		J3MethodCode                 _selfCode;
		uint16_t                     _access;
		J3Class*                     _cl;
		const vmkit::Name*           _name;
		const vmkit::Name*           _sign;
		J3MethodType*                _methodType;
		J3Attributes*                _attributes;
		uint32_t                     _index;
		uint32_t                     _slot;
		llvm::Function*              _llvmFunction;
		void*                        _fnPtr;
		char* volatile               _llvmAllNames; /* stub + _ + native_name */
		void*                        _nativeFnPtr;
		void* volatile               _staticTrampoline;
		void* volatile               _virtualTrampoline;
		J3ObjectHandle* volatile     _javaMethod;

		J3Value            internalInvoke(bool statically, J3ObjectHandle* handle, va_list va);
		J3Value            internalInvoke(bool statically, J3ObjectHandle* handle, J3Value* args);
		J3Value            internalInvoke(bool statically, J3Value* args);
		void               buildLLVMNames(J3Class* from);
	public:
		J3Method(uint16_t access, J3Class* cl, const vmkit::Name* name, const vmkit::Name* sign);

		uint32_t            slot() { return _slot; }

		J3ObjectHandle*     javaMethod();

		void*               nativeFnPtr() { return _nativeFnPtr; }

		void                markCompiled(llvm::Function* llvmFunction, void* fnPtr);

		uint32_t            interfaceIndex();

		void*               getSymbolAddress();

		char*               llvmFunctionName(J3Class* from=0);
		char*               llvmDescriptorName(J3Class* from=0);
		char*               llvmStubName(J3Class* from=0);

		void                postInitialise(uint32_t access, J3Attributes* attributes);
		void                setResolved(uint32_t index); 

		J3Method*           resolve(J3ObjectHandle* obj);

		uint32_t            index();
		uint32_t*           indexPtr() { return &_index; }
		bool                isResolved() { return _index != -1; }
		bool                isCompiled() { return _fnPtr; }

		J3Attributes*       attributes() const { return _attributes; }
		uint16_t            access() const { return _access; }
		J3Class*            cl()     const { return _cl; }
		const vmkit::Name*  name()   const { return _name; }
		const vmkit::Name*  sign()   const { return _sign; }
		J3MethodType*       methodType(J3Class* from=0);

		void                registerNative(void* ptr);

		J3Value             invokeStatic(...);
		J3Value             invokeStatic(J3Value* args);
		J3Value             invokeStatic(va_list va);
		J3Value             invokeSpecial(J3ObjectHandle* obj, ...);
		J3Value             invokeSpecial(J3ObjectHandle* obj, J3Value* args);
		J3Value             invokeSpecial(J3ObjectHandle* obj, va_list va);
		J3Value             invokeVirtual(J3ObjectHandle* obj, ...);
		J3Value             invokeVirtual(J3ObjectHandle* obj, J3Value* args);
		J3Value             invokeVirtual(J3ObjectHandle* obj, va_list va);

		void*               fnPtr(bool withCaller);
		void*               functionPointerOrStaticTrampoline();
		void*               functionPointerOrVirtualTrampoline();

		void                dump();
	};
}

#endif
