#include "vmkit/allocator.h"
#include "vmkit/thread.h"
#include "vmkit/vmkit.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>

using namespace vmkit;

pthread_mutex_t     ThreadAllocator::mutex;
uintptr_t           ThreadAllocator::baseStack = 0;
uintptr_t           ThreadAllocator::_magic = 0;
std::vector<void*>* ThreadAllocator::spaces = 0;
std::vector<void*>* ThreadAllocator::freeThreads = 0;

void* BumpAllocator::operator new(size_t n) {
	return (void*)((uintptr_t)map(bucketSize) + sizeof(BumpAllocatorNode));
}

BumpAllocator::BumpAllocator() {
	pthread_mutex_init(&mutex, 0);
	current = (BumpAllocatorNode*)((uintptr_t)this - sizeof(BumpAllocatorNode));
	current->next = 0;
	current->top  = (uint8_t*)round((uintptr_t)(this + 1), 64);
}

BumpAllocator* BumpAllocator::create() {
	return new BumpAllocator();
}

BumpAllocator::~BumpAllocator() {
	while(current->next) {
		BumpAllocatorNode* tmp = current->next;
		unmap(current, (uintptr_t)current->top - (uintptr_t)current);
		current = tmp;
	}
}

void BumpAllocator::operator delete(void* p) {
	unmap(p, bucketSize);
}

void BumpAllocator::destroy(BumpAllocator* allocator) {
	delete allocator;
}

void* BumpAllocator::allocate(size_t size) {
	if(size > (bucketSize - sizeof(BumpAllocatorNode))) {
		pthread_mutex_lock(&mutex);
		size_t total = round((uintptr_t)size + sizeof(BumpAllocatorNode), 4096);
		BumpAllocatorNode* newBucket = (BumpAllocatorNode*)map(total);
		newBucket->next = current->next;
		current->next = newBucket;
		newBucket->top  = (uint8_t*)((uintptr_t)newBucket + bucketSize);
		pthread_mutex_unlock(&mutex);
		return newBucket + 1;
	}

	while(1) {
		BumpAllocatorNode* node = current;
		uint8_t* res = __sync_fetch_and_add(&node->top, (uint8_t*)round(size, 8));
		uint8_t* end = res + size;

		if(res >= (uint8_t*)node && (res + size) < ((uint8_t*)node) + bucketSize)
			return res;

		pthread_mutex_lock(&mutex);
		BumpAllocatorNode* newBucket = (BumpAllocatorNode*)map(bucketSize);
		newBucket->next = current;
		newBucket->top  = (uint8_t*)(newBucket + 1);
		current = newBucket;
		pthread_mutex_unlock(&mutex);
	}
}

void* BumpAllocator::map(size_t n) {
	void* res = mmap(0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, 0, 0);
	if(res == MAP_FAILED)
		Thread::get()->vm()->internalError("unable to map %ld bytes", n);
	return res;
}

void BumpAllocator::unmap(void* p, size_t n) {
	munmap(0, n);
}

void PermanentObject::operator delete(void* ptr) {
	//Thread::get()->vm()->internalError("should not happen");
}
  
void PermanentObject::operator delete[](void* ptr) {
	//Thread::get()->vm()->internalError("should not happen");
}

void ThreadAllocator::initialize(uintptr_t minThreadStruct) {
	if(spaces)
		VMKit::internalError("thread allocation system is already initialized");
	spaces = new std::vector<void*>();
	freeThreads = new std::vector<void*>();

	pthread_mutex_init(&mutex, 0);
	spaces->reserve(1);
	freeThreads->reserve(refill);

	minThreadStruct = ((minThreadStruct - 1) & -PAGE_SIZE) + PAGE_SIZE;
	baseStack = minThreadStruct + PAGE_SIZE;

	_magic = -VMKIT_STACK_SIZE;
}

void* ThreadAllocator::allocate() {
	pthread_mutex_lock(&mutex);
	if(!freeThreads->size()) {
		void* space = mmap(0, VMKIT_STACK_SIZE*refill, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

		if(space == MAP_FAILED) {
			fprintf(stderr, "unable to allocate a thread\n");
			abort();
		}

		spaces->push_back(space);

		uintptr_t base = (((uintptr_t)space - 1) & -VMKIT_STACK_SIZE) + VMKIT_STACK_SIZE;
		uint32_t n = (base == (uintptr_t)space) ? refill : (refill - 1);

		for(uint32_t i=0; i<n; i++) {
			mprotect((void*)(base + baseStack), PROT_NONE, PAGE_SIZE);
			freeThreads->push_back((void*)base);
			base += VMKIT_STACK_SIZE;
		}
	}

	void* res = freeThreads->back();
	freeThreads->pop_back();
	pthread_mutex_unlock(&mutex);
	return res;
}

void ThreadAllocator::release(void* thread) {
	pthread_mutex_lock(&mutex);
	freeThreads->push_back(thread);
	pthread_mutex_unlock(&mutex);
}

void* ThreadAllocator::stackAddr(void* thread) {
	return (void*)((uintptr_t)thread + baseStack);
}

size_t ThreadAllocator::stackSize(void* thread) {
	return VMKIT_STACK_SIZE - baseStack;
}

void* ThreadAllocator::alternateStackAddr(void* thread) {
	return (void*)((uintptr_t)thread + baseStack - PAGE_SIZE);
}

size_t ThreadAllocator::alternateStackSize(void* thread) {
	return PAGE_SIZE;
}
