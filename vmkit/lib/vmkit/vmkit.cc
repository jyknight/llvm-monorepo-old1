#include <stdio.h>
#include <stdarg.h>
#include <wchar.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "vmkit/system.h"
#include "vmkit/vmkit.h"
#include "vmkit/thread.h"
#include "vmkit/safepoint.h"

#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/DataLayout.h"

#include "llvm/ExecutionEngine/ExecutionEngine.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/MemoryBuffer.h"

#include "llvm/Bitcode/ReaderWriter.h"

#include "llvm/CodeGen/MachineCodeEmitter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/GCStrategy.h"

using namespace vmkit;

VMKit::VMKit(vmkit::BumpAllocator* allocator) :
	mangleMap(Util::char_less, allocator) {
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();
	llvm::InitializeNativeTargetDisassembler();
	llvm::llvm_start_multithreaded();
	_allocator = allocator;
	pthread_mutex_init(&safepointMapLock, 0);

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&_compilerLock, &attr);
	pthread_mutexattr_destroy(&attr);
}

void* VMKit::operator new(size_t n, vmkit::BumpAllocator* allocator) {
	return allocator->allocate(n);
}

void VMKit::destroy(VMKit* vm) {
	vmkit::BumpAllocator::destroy(vm->allocator());
}

void VMKit::lockCompiler() {
	pthread_mutex_lock(&_compilerLock);
}

void VMKit::unlockCompiler() {
	pthread_mutex_unlock(&_compilerLock);
}

void VMKit::addSafepoint(Safepoint* sf) {
	pthread_mutex_lock(&safepointMapLock);
	safepointMap[sf->addr()] = sf;
	pthread_mutex_unlock(&safepointMapLock);
}

Safepoint* VMKit::getSafepoint(void* addr) {
	pthread_mutex_lock(&safepointMapLock);
	Safepoint* res = safepointMap[addr];
	pthread_mutex_unlock(&safepointMapLock);
	return res;
}

llvm::LLVMContext& VMKit::llvmContext() {
	return self()->getContext();
}

llvm::Type* VMKit::introspectType(const char* name) {
	llvm::Type* res = self()->getTypeByName(name);
	if(!res)
		internalError("unable to find internal type: %s", name);
	return res;
}

llvm::Function* VMKit::introspectFunction(llvm::Module* dest, const char* name) {
	llvm::Function* orig = (llvm::Function*)mangleMap[name];
	if(!orig)
		internalError("unable to find internal function: %s", name);

	return dest ? (llvm::Function*)dest->getOrInsertFunction(orig->getName(), orig->getFunctionType()) : orig;
}

llvm::GlobalValue* VMKit::introspectGlobalValue(llvm::Module* dest, const char* name) {
	llvm::GlobalValue* orig = mangleMap[name];
	if(!orig)
		internalError("unable to find internal global value: %s", name);
	return dest ? (llvm::GlobalValue*)dest->getOrInsertGlobal(orig->getName(), orig->getType()) : orig;
}

void VMKit::addSymbol(llvm::GlobalValue* gv) {
	const char* id = gv->getName().data();
	int   status;
	char* realname;
	realname = abi::__cxa_demangle(id, 0, 0, &status);
	const char* tmp = realname ? realname : id;
	uint32_t length = strlen(tmp);
	char* mangled = (char*)allocator()->allocate(length+1);
	strcpy(mangled, tmp);
	mangleMap[mangled] = gv;
	gv->Materialize();
	free(realname);
}

void VMKit::vmkitBootstrap(Thread* initialThread, const char* selfBitCodePath) {
	_selfBitCodePath = selfBitCodePath;
	std::string err;

	llvm::OwningPtr<llvm::MemoryBuffer> buf;
	if (llvm::MemoryBuffer::getFile(selfBitCodePath, buf))
		VMKit::internalError("Error while opening bitcode file %s", selfBitCodePath);
	_self = llvm::getLazyBitcodeModule(buf.take(), llvm::getGlobalContext(), &err);

	if(!self())
		VMKit::internalError("Error while reading bitcode file %s: %s", selfBitCodePath, err.c_str());

	for(llvm::Module::iterator cur=self()->begin(); cur!=self()->end(); cur++)
		addSymbol(cur);

	for(llvm::Module::global_iterator cur=self()->global_begin(); cur!=self()->global_end(); cur++)
		addSymbol(cur);

	_dataLayout = new llvm::DataLayout(self());

	llvm::GlobalValue* typeInfoGV = mangleMap["typeinfo for void*"];
	ptrTypeInfo = typeInfoGV ? dlsym(SELF_HANDLE, typeInfoGV->getName().data()) : 0;

	if(!ptrTypeInfo)
		internalError("unable to find typeinfo for void*"); 

	initialThread->start();
	initialThread->join();
}


llvm::Function* VMKit::getGCRoot(llvm::Module* mod) {
	return llvm::Intrinsic::getDeclaration(mod, llvm::Intrinsic::gcroot);
}

void VMKit::log(const char* msg, ...) {
	va_list va;
	va_start(va, msg);
	fprintf(stderr, "[vmkit]: ");
	vfprintf(stderr, msg, va);
	fprintf(stderr, "\n");
	va_end(va);
}

void VMKit::vinternalError(const char* msg, va_list va) {
	defaultInternalError(msg, va);
}

void VMKit::defaultInternalError(const char* msg, va_list va) {
	fprintf(stderr, "Fatal error: ");
	vfprintf(stderr, msg, va);
	fprintf(stderr, "\n");
	abort();
}

void VMKit::internalError(const char* msg, ...) {
	va_list va;
	va_start(va, msg);
	if(Thread::get() && Thread::get()->vm())
		Thread::get()->vm()->vinternalError(msg, va);
	else
		defaultInternalError(msg, va);
	va_end(va);
	fprintf(stderr, "SHOULD NOT BE THERE\n");
	abort();
}

void VMKit::sigsegv(uintptr_t addr) {
	internalError("sigsegv at %p", (void*)addr);
}

void VMKit::sigend() {
	internalError("sig terminate");
}

void VMKit::throwException(void* obj) {
	//	internalError("throw exception...\n");
	//	fprintf(stderr, "throw %p\n", obj);
	//	Thread::get()->vm()->printStackTrace();
	//	abort();
	throw obj;
}

void VMKit::printStackTrace() {
	fprintf(stderr, " TODO: baseline printStackTrace\n");
}

void VMKit::uncatchedException(void* e) {
	fprintf(stderr, "Uncatched exception: %p\n", e);
}
