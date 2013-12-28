#ifndef _VMKIT_VM_H_
#define _VMKIT_VM_H_

#include "vmkit/util.h"
#include "vmkit/allocator.h"

namespace llvm {
	class LLVMContext;
	class Module;
	class DataLayout;
	class GlobalValue;
	class Function;
	class Type;
}

namespace vmkit {
	class Thread;
	class Safepoint;

	class VMKit {
		typedef std::map<const char*, llvm::GlobalValue*, Util::char_less_t, 
										 StdAllocator<std::pair<const char*, llvm::GlobalValue*> > > MangleMap;

		std::map<void*, Safepoint*>             safepointMap;    
		pthread_mutex_t                         safepointMapLock;
		MangleMap                               mangleMap;
		BumpAllocator*                          _allocator;
		llvm::Module*                           _self;
		llvm::DataLayout*                       _dataLayout;
		void*                                   ptrTypeInfo;

		void                            addSymbol(llvm::GlobalValue* gv);

		static void                     defaultInternalError(const wchar_t* msg, va_list va) __attribute__((noreturn));
	protected:
		void* operator new(size_t n, BumpAllocator* allocator);

	public:
		static void                destroy(VMKit* vm);

		VMKit(BumpAllocator* allocator);

		void                       addSafepoint(Safepoint* sf);
		Safepoint*                 getSafepoint(void* addr);

		BumpAllocator*             allocator() { return _allocator; }

		void                       vmkitBootstrap(Thread* initialThread, const char* selfBitCodePath);

		llvm::DataLayout*          dataLayout() { return _dataLayout; }
		llvm::LLVMContext&         llvmContext();
		llvm::Module*              self() { return _self; }
		llvm::Function*            getGCRoot(llvm::Module* mod);
		llvm::Function*            introspectFunction(llvm::Module* dest, const char* name);
		llvm::GlobalValue*         introspectGlobalValue(llvm::Module* dest, const char* name);
		llvm::Type*                introspectType(const char* name);

		void log(const wchar_t* msg, ...);

		virtual void vinternalError(const wchar_t* msg, va_list va) __attribute__((noreturn));

		static void internalError(const wchar_t* msg, ...) __attribute__((noreturn));		
		static void throwException(void* obj) __attribute__((noreturn));
	};
};

#endif
