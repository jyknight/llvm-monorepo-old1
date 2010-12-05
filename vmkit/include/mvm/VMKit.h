#ifndef _VMKIT_H_
#define _VMKIT_H_

#include <vector>
#include "mvm/Allocator.h"
#include "mvm/Threads/CollectionRV.h"
#include "mvm/VirtualMachine.h"

namespace mvm {
class MethodInfo;
class VMKit;
class gc;

class FunctionMap {
public:
  /// Functions - Map of applicative methods to function pointers. This map is
  /// used when walking the stack so that VMKit knows which applicative method
  /// is executing on the stack.
  ///
  std::map<void*, MethodInfo*> Functions;

  /// FunctionMapLock - Spin lock to protect the Functions map.
  ///
  mvm::SpinLock FunctionMapLock;

  /// IPToMethodInfo - Map a code start instruction instruction to the MethodInfo.
  ///
  MethodInfo* IPToMethodInfo(void* ip);

  /// addMethodInfo - A new instruction pointer in the function map.
  ///
  void addMethodInfo(MethodInfo* meth, void* ip);

  /// removeMethodInfos - Remove all MethodInfo owned by the given owner.
  void removeMethodInfos(void* owner);

  FunctionMap();
};

class VMKit : public mvm::PermanentObject {
public:
  /// allocator - Bump pointer allocator to allocate permanent memory of VMKit
  mvm::BumpPtrAllocator& allocator;

  VMKit(mvm::BumpPtrAllocator &Alloc);

	SpinLock                     _vmkitLock;

	void vmkitLock() { _vmkitLock.lock(); }
	void vmkitUnlock() { _vmkitLock.unlock(); }

	/// ------------------------------------------------- ///
	/// ---             vm managment                  --- ///
	/// ------------------------------------------------- ///
	// vms - the list of vms. Could be directly an array and we could also directly use the vmID as index in this array.
	// synchronize with vmkitLock
	VirtualMachine**             vms;
	size_t                       numberOfVms;

	size_t addVM(VirtualMachine* vm);
	void   removeVM(size_t id);

	/// ------------------------------------------------- ///
	/// ---             thread managment              --- ///
	/// ------------------------------------------------- ///
  /// preparedThreads - the list of prepared threads, they are not yet running.
  ///
	CircularBase<Thread>         preparedThreads;

  /// runningThreads - the list of running threads
  ///
	CircularBase<Thread>         runningThreads;

  /// numberOfRunningThreads - The number of threads that currently run under this VM.
  size_t                       numberOfRunningThreads;

  /// rendezvous - The rendezvous implementation for garbage collection.
  ///
#ifdef WITH_LLVM_GCC
  CooperativeCollectionRV rendezvous;
#else
  UncooperativeCollectionRV rendezvous;
#endif

	void registerPreparedThread(mvm::Thread* th);  
	void unregisterPreparedThread(mvm::Thread* th);

	void registerRunningThread(mvm::Thread* th);  
	void unregisterRunningThread(mvm::Thread* th);

	/// ------------------------------------------------- ///
	/// ---    backtrace related methods              --- ///
	/// ------------------------------------------------- ///
  /// FunctionsCache - cache of compiled functions
	//  
  FunctionMap FunctionsCache;

  MethodInfo* IPToMethodInfo(void* ip) {
    return FunctionsCache.IPToMethodInfo(ip);
  }

  void removeMethodInfos(void* owner) {
    FunctionsCache.removeMethodInfos(owner);
  }
};

}

#endif
