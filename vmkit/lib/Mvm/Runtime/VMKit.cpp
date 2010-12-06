#include "mvm/VMKit.h"
#include "mvm/VirtualMachine.h"
#include "mvm/SystemThreads.h"

using namespace mvm;

#if 0
#define dprintf(...) do { printf("[%p] ", (void*)mvm::Thread::get()); printf(__VA_ARGS__); } while(0)
#else
#define dprintf(...)
#endif

VMKit::VMKit(mvm::BumpPtrAllocator &Alloc) : allocator(Alloc) {
	vms          = 0;
	vmsArraySize = 0;

  // First create system threads.
  finalizerThread = new FinalizerThread(this);
  finalizerThread->start((void (*)(mvm::Thread*))FinalizerThread::finalizerStart);
}

void VMKit::scanFinalizationQueue(uintptr_t closure) {
  finalizerThread->scanFinalizationQueue(closure);
}

void VMKit::addFinalizationCandidate(mvm::gc* object) {
  llvm_gcroot(object, 0);
  finalizerThread->addFinalizationCandidate(object);
}

void VMKit::tracer(uintptr_t closure) {
	// don't have to take the vmkitLock, already taken by the rendezvous.
	for(size_t i=0; i<vmsArraySize; i++)
		if(vms[i])
			vms[i]->tracer(closure);
}

bool VMKit::startCollection() {
	// do not take the lock here because if a gc is currently running, it could call enterUncooperativeCode 
	// which will execute the gc and we will therefore recall the gc just behind. Stupid because the previous one
	// should have freed some memory
  rendezvous.startRV();

  if (mvm::Thread::get()->doYield) {
    rendezvous.cancelRV();
    rendezvous.join();
    return 0;
  } else {
		dprintf("Start collection\n");
		// Lock thread lock, so that we can traverse the vm and thread lists safely. This will be released on finishRV.
		vmkitLock();

		finalizerThread->FinalizationQueueLock.acquire();

		// call first startCollection on each vm to avoid deadlock. 
		// indeed, a vm could want to execute applicative code
		for(size_t i=0; i<vmsArraySize; i++)
			if(vms[i])
				vms[i]->startCollection();

    rendezvous.synchronize();

		return 1;
	}
}

void VMKit::endCollection() {
	dprintf("End collection\n");

	rendezvous.finishRV();

	for(size_t i=0; i<vmsArraySize; i++)
		if(vms[i])
			vms[i]->endCollection();

  finalizerThread->FinalizationQueueLock.release();
  finalizerThread->FinalizationCond.broadcast();

	vmkitUnlock();
}

size_t VMKit::addVM(VirtualMachine* vm) {
	dprintf("add vm: %p\n", vm);
	vmkitLock();

	for(size_t i=0; i<vmsArraySize; i++)
		if(!vms[i]) {
			vms[i] = vm;
			vmkitUnlock();
			return i;
		}

	int res = vmsArraySize;
	vmsArraySize = vmsArraySize ? (vmsArraySize<<1) : 4;
	// reallocate the vms
	VirtualMachine **newVms = new VirtualMachine*[vmsArraySize];

	memcpy(newVms, vms, res*sizeof(VirtualMachine*));
	memset(newVms + res*sizeof(VirtualMachine*), 0, (vmsArraySize-res)*sizeof(VirtualMachine*));
	newVms[res] = vm;

	VirtualMachine **oldVms = vms;
	vms = newVms; // vms must always contain valid data
	delete[] oldVms;
	
 	// reallocate the allVMDatas
 	for(Thread* cur=preparedThreads.next(); cur!=&preparedThreads; cur=cur->next()) {
		cur->reallocAllVmsData(res, vmsArraySize);
	}

 	for(Thread* cur=runningThreads.next(); cur!=&runningThreads; cur=cur->next()) {
		cur->reallocAllVmsData(res, vmsArraySize);
	}

	vmkitUnlock();

	return res;
}

void VMKit::removeVM(size_t id) {
	dprintf("remove vm: %p\n", vm);
	// I think that we only should call this function when all the thread data are released
	vms[id] = 0;
}

void VMKit::registerPreparedThread(mvm::Thread* th) {
	dprintf("Create thread: %p\n", th);
	vmkitLock();
	th->appendTo(&preparedThreads);
	th->reallocAllVmsData(0, vmsArraySize);
	vmkitUnlock();
}
  
void VMKit::unregisterPreparedThread(mvm::Thread* th) {
	dprintf("Delete thread: %p\n", th);
	vmkitLock();
	th->remove();
	for(size_t i=0; i<vmsArraySize; i++)
		if(th->allVmsData[i])
			delete th->allVmsData[i];
	delete th->allVmsData;
	vmkitUnlock();
}

void VMKit::registerRunningThread(mvm::Thread* th) {
	dprintf("Register thread: %p\n", th);
	vmkitLock();
	numberOfRunningThreads++;
	th->remove();
	th->appendTo(&runningThreads);
	vmkitUnlock();
}
  
void VMKit::unregisterRunningThread(mvm::Thread* th) {
	dprintf("Unregister thread: %p\n", th);
	vmkitLock();
	numberOfRunningThreads--;
	th->remove();
	th->appendTo(&preparedThreads);
	vmkitUnlock();
}
