//===----------- VmkitGC.cpp - Garbage Collection Interface -----------------===//
//
//                     The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "VmkitGC.h"
#include "MutatorThread.h"
#include "vmkit/VirtualMachine.h"

#include <set>

using namespace vmkit;

static vmkit::SpinLock lock;
std::set<gc*> __InternalSet__;
int Collector::verbose = 0;

extern "C" void* prealloc(uint32_t sz) {
	gc* res = 0;
  gcHeader* head = 0;

  sz = llvm::RoundUpToAlignment(sz, sizeof(void*));
  head = (gcHeader*) malloc(sz);
  memset((void*)head, 0, sz);
  res = head->toReference();

  lock.acquire();
  __InternalSet__.insert(res);
  lock.release();

  return res;
}

extern "C" void postalloc(gc* obj, void* type, uint32_t size) {
	vmkit::Thread::get()->MyVM->setType(obj, type);
}

extern "C" void* gcmalloc(uint32_t sz, void* type) {
  gc* res = 0;
  sz += gcHeader::hiddenHeaderSize();
  res = (gc*) prealloc(sz);
  postalloc(res, type, sz);
  return res;
}

extern "C" void* gcmallocUnresolved(uint32_t sz, void* type) {
	gc* res = (gc*)gcmalloc(sz, type);
	vmkit::Thread::get()->MyVM->addFinalizationCandidate(res);
	return res;
}

// Do not insert MagicArray ref to InternalSet of references.
extern "C" void* AllocateMagicArray(int32_t sz, void* length) {
	gcHeader* head = 0;
  gc* res = 0;
	sz += gcHeader::hiddenHeaderSize();
	head = (gcHeader*)malloc(sz);
	memset((void*)head, 0, sz);
	res = head->toReference();
	vmkit::Thread::get()->MyVM->setType(res, length);
  return res;
}

extern "C" void addFinalizationCandidate(gc* obj) {
  vmkit::Thread::get()->MyVM->addFinalizationCandidate(obj);
}

void* Collector::begOf(gc* obj) {
  lock.acquire();
  std::set<gc*>::iterator I = __InternalSet__.find(obj);
  std::set<gc*>::iterator E = __InternalSet__.end();
  lock.release();
    
  if (I != E) return obj;
  return 0;
}

void MutatorThread::init(Thread* _th) {
  MutatorThread* th = (MutatorThread*)_th;
  th->realRoutine(_th);
}

bool Collector::isLive(gc* ptr, word_t closure) {
  abort();
  return false;
}

void Collector::scanObject(void** ptr, word_t closure) {
  abort();
}
 
void Collector::markAndTrace(void* source, void* ptr, word_t closure) {
  abort();
}
  
void Collector::markAndTraceRoot(void* source, void* ptr, word_t closure) {
  abort();
}

gc* Collector::retainForFinalize(gc* val, word_t closure) {
  abort();
  return NULL;
}
  
gc* Collector::retainReferent(gc* val, word_t closure) {
  abort();
  return NULL;
}
  
gc* Collector::getForwardedFinalizable(gc* val, word_t closure) {
  abort();
  return NULL;
}
  
gc* Collector::getForwardedReference(gc* val, word_t closure) {
  abort();
  return NULL;
}
  
gc* Collector::getForwardedReferent(gc* val, word_t closure) {
  abort();
  return NULL;
}

extern "C" void arrayWriteBarrier(void* ref, void** ptr, void* value) {
  *ptr = value;
}

extern "C" void fieldWriteBarrier(void* ref, void** ptr, void* value) {
  *ptr = value;
}

extern "C" void nonHeapWriteBarrier(void** ptr, void* value) {
  *ptr = value;
}


void Collector::objectReferenceWriteBarrier(gc* ref, gc** slot, gc* value) {
  *slot = value;
}

void Collector::objectReferenceArrayWriteBarrier(gc* ref, gc** slot, gc* value) {
  *slot = value;
}

void Collector::objectReferenceNonHeapWriteBarrier(gc** slot, gc* value) {
  *slot = value;
}

bool Collector::objectReferenceTryCASBarrier(gc*ref, gc** slot, gc* old, gc* value) {
  gc* res = __sync_val_compare_and_swap(slot, old, value);
  return (old == res);
}

void Collector::collect() {
  // Do nothing.
}

void Collector::initialise(int argc, char** argv) {
}

bool Collector::needsWriteBarrier() {
  return false;
}

bool Collector::needsNonHeapWriteBarrier() {
  return false;
}
