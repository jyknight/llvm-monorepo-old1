#ifndef _COMPILER_H_
#define _COMPILER_H_

#include "llvm/ExecutionEngine/SectionMemoryManager.h"

#include "allocator.h"
#include "util.h"

namespace llvm {
	class Module;
	class ExecutionEngine;
	class GlobalValue;
	class Function;

	namespace legacy {
		class PassManager;
	}
	using legacy::PassManager;

};

namespace vmkit {
	class VMKit;

	class Symbol : public PermanentObject {
	public:
		virtual void*           getSymbolAddress();
		virtual llvm::Function* llvmFunction() { return 0; }
		virtual bool            isInlinable() { return 0; }
	};

	class NativeSymbol : public Symbol {
		llvm::Function* original;
		void*           addr;
	public:
		NativeSymbol(llvm::Function* _original, void* _addr) { original = _original; addr = _addr; }

		llvm::Function* llvmFunction() { return original; }
		void*           getSymbolAddress() { return addr; }
		virtual bool    isInlinable() { return 1; }
	};

	class CompilationUnit  : public llvm::SectionMemoryManager {
		typedef std::map<const char*, Symbol*, Util::char_less_t, StdAllocator<std::pair<const char*, Symbol*> > > SymbolMap;

		BumpAllocator*          _allocator;
		SymbolMap               _symbolTable;
		pthread_mutex_t         _mutexSymbolTable;
		llvm::ExecutionEngine*  _ee;
		llvm::PassManager*      pm;

	protected:
		void  operator delete(void* self);

	public:
		void* operator new(size_t n, BumpAllocator* allocator);

		CompilationUnit(BumpAllocator* allocator, const char* id, bool runInlinePass, bool onlyAlwaysInline);
		~CompilationUnit();

		static void destroy(CompilationUnit* unit);

		void                    addSymbol(const char* id, vmkit::Symbol* symbol);
		Symbol*                 getSymbol(const char* id, bool error=1);
		uint64_t                getSymbolAddress(const std::string &Name);

		BumpAllocator*          allocator() { return _allocator; }
		llvm::ExecutionEngine*  ee() { return _ee; }

		void                    compileModule(llvm::Module* module);
	};
}

#endif
