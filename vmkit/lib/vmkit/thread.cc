#include "vmkit/thread.h"
#include "vmkit/system.h"
#include "vmkit/vmkit.h"

using namespace vmkit;

__thread Thread* Thread::_thread = 0;

Thread::Thread(VMKit* vm) { 
	_vm = vm; 
}

void* Thread::operator new(size_t n, BumpAllocator* allocator) {
	return ThreadAllocator::allocator()->allocate();
}

void Thread::operator delete(void* p) {
	VMKit::internalError(L"not yet implemented");
}

void* Thread::doRun(void* _thread) {
	Thread* thread = (Thread*)_thread;
	set(thread);
	thread->run();
	return 0;
}

void Thread::start() {
	pthread_create(&_tid, 0, doRun, this);
}

void Thread::join() {
	void* res;
	pthread_join(_tid, &res);
}

StackWalker::StackWalker(uint32_t initialPop) {
	unw_getcontext(&uc);
  unw_init_local(&cursor, &uc);
	next(initialPop+1);
}

bool StackWalker::next(uint32_t nbPop) {
	while(nbPop--) {
		if(unw_step(&cursor) <= 0)
			return 0;
	}
	return 1;
}
	
void* StackWalker::ip() {
	unw_word_t ip;
	unw_get_reg(&cursor, UNW_REG_IP, &ip);
	return (void*)ip;
}

void* StackWalker::sp() {
	unw_word_t sp;
	unw_get_reg(&cursor, UNW_REG_SP, &sp);
	return (void*)sp;
}
