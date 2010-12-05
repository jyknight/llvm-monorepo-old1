#include "mvm/VMKit.h"
#include "mvm/VirtualMachine.h"

using namespace mvm;

VMKit::VMKit(mvm::BumpPtrAllocator &Alloc) : allocator(Alloc) {
	vms         = 0;
	numberOfVms = 0;
}

size_t VMKit::addVM(VirtualMachine* vm) {
	vmkitLock();

	for(size_t i=0; i<numberOfVms; i++)
		if(!vms[i]) {
			vms[i] = vm;
			vmkitUnlock();
			return i;
		}

	int res = numberOfVms;
	numberOfVms = numberOfVms ? (numberOfVms<<1) : 1;
	// reallocate the vms
	VirtualMachine **newVms = new VirtualMachine*[numberOfVms];
	memcpy(newVms, vms, res*sizeof(VirtualMachine*));
	VirtualMachine **oldVms = vms;
	vms = newVms; // vms must always contain valid data
	delete[] oldVms;
	
 	// reallocate the allVMDatas
 	for(Thread* cur=preparedThreads.next(); cur!=&preparedThreads; cur=cur->next()) {
		cur->reallocAllVmsData(res, numberOfVms);
	}

 	for(Thread* cur=runningThreads.next(); cur!=&runningThreads; cur=cur->next()) {
		cur->reallocAllVmsData(res, numberOfVms);
	}

	vmkitUnlock();

	return res;
}

void VMKit::removeVM(size_t id) {
	// what can I do with the VMThreadData?
	vms[id] = 0;
}

void VMKit::registerPreparedThread(mvm::Thread* th) {
	vmkitLock();
	th->appendTo(&preparedThreads);
	th->reallocAllVmsData(0, numberOfVms);
	vmkitUnlock();
}
  
void VMKit::unregisterPreparedThread(mvm::Thread* th) {
	vmkitLock();
	th->remove();
	for(size_t i=0; i<numberOfVms; i++)
		if(th->allVmsData[i])
			delete th->allVmsData[i];
	delete th->allVmsData;
	vmkitUnlock();
}

void VMKit::registerRunningThread(mvm::Thread* th) {
	vmkitLock();
	numberOfRunningThreads++;
	th->remove();
	th->appendTo(&runningThreads);
	vmkitUnlock();
}
  
void VMKit::unregisterRunningThread(mvm::Thread* th) {
	vmkitLock();
	numberOfRunningThreads--;
	th->remove();
	th->appendTo(&preparedThreads);
	vmkitUnlock();
}
